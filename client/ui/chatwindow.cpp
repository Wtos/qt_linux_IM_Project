#include "chatwindow.h"
#include "ui_chatwindow.h"

#include <QAbstractItemView>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QTableWidgetItem>
#include <QUuid>
#include <QtGlobal>

namespace {
QDateTime fromEpochSeconds(quint64 seconds) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 8, 0)
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(seconds));
#else
    return QDateTime::fromTime_t(static_cast<uint>(seconds));
#endif
}
} // namespace

ChatWindow::ChatWindow(TcpClient *client, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ChatWindow)
    , tcpClient_(client) {
    ui->setupUi(this);
    setupUI();

    connect(ui->btn_send, &QPushButton::clicked, this, &ChatWindow::onSendClicked);
    connect(ui->btn_file, &QToolButton::clicked, this, &ChatWindow::onSelectFile);

    if (tcpClient_) {
        connect(tcpClient_, &TcpClient::chatMessageReceived, this, &ChatWindow::onChatMessageReceived);
        connect(tcpClient_, &TcpClient::userListUpdated, this, &ChatWindow::onUserListUpdated);
        connect(tcpClient_, &TcpClient::fileOfferReceived, this, &ChatWindow::onFileOfferReceived);
        connect(tcpClient_, &TcpClient::fileOfferResponseReceived,
                this, &ChatWindow::onFileOfferResponseReceived);
        connect(tcpClient_, &TcpClient::fileTransferProgress,
                this, &ChatWindow::onFileTransferProgress);
        connect(tcpClient_, &TcpClient::fileTransferCompleted,
                this, &ChatWindow::onFileTransferCompleted);
        connect(tcpClient_, &TcpClient::connected, this, &ChatWindow::onConnected);
        connect(tcpClient_, &TcpClient::disconnected, this, &ChatWindow::onDisconnected);
    }

    ui->plainTextEdit_input->installEventFilter(this);
    if (tcpClient_) {
        tcpClient_->requestUserList();
    }
}

ChatWindow::~ChatWindow() {
    delete ui;
}

bool ChatWindow::eventFilter(QObject *obj, QEvent *event) {
    if (obj == ui->plainTextEdit_input && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if ((keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)
            && keyEvent->modifiers() == Qt::NoModifier) {
            onSendClicked();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void ChatWindow::closeEvent(QCloseEvent *event) {
    if (tcpClient_) {
        tcpClient_->disconnectFromServer();
    }
    emit windowClosed();
    QWidget::closeEvent(event);
}

void ChatWindow::setupUI() {
    setWindowTitle("IM Client - Chat");
    resize(900, 600);

    ui->combo_target->setEditable(true);
    ui->combo_target->setInsertPolicy(QComboBox::NoInsert);
    ui->combo_target->addItem("Group (All)", QString());

    ui->plainTextEdit_input->setPlaceholderText("Type a message...");

    ui->table_transfers->setColumnCount(4);
    ui->table_transfers->setHorizontalHeaderLabels({"File", "Size", "Status", "Action"});
    ui->table_transfers->horizontalHeader()->setStretchLastSection(true);
    ui->table_transfers->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->table_transfers->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->table_transfers->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->table_transfers->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->table_transfers->verticalHeader()->setVisible(false);

    if (tcpClient_ && tcpClient_->isConnected()) {
        setStatus("Connected", QColor(0, 160, 0));
    } else {
        setStatus("Disconnected", Qt::gray);
    }
}

void ChatWindow::setStatus(const QString &text, const QColor &color) {
    ui->label_status->setText(text);
    QPalette palette = ui->label_status->palette();
    palette.setColor(QPalette::WindowText, color);
    ui->label_status->setPalette(palette);
}

void ChatWindow::onConnected() {
    setStatus("Connected", QColor(0, 160, 0));
    if (tcpClient_) {
        tcpClient_->requestUserList();
    }
}

void ChatWindow::onDisconnected() {
    setStatus("Disconnected", Qt::red);
}

void ChatWindow::onSendClicked() {
    if (!tcpClient_ || !tcpClient_->isConnected()) {
        QMessageBox::warning(this, "Warning", "Not connected to the server.");
        return;
    }

    QString text = ui->plainTextEdit_input->toPlainText().trimmed();
    if (text.isEmpty()) {
        return;
    }

    QString toId = ui->combo_target->currentData().toString().trimmed();
    QString targetText = ui->combo_target->currentText().trimmed();

    bool isGroup = (toId.isEmpty() && targetText == "Group (All)");
    if (!isGroup && toId.isEmpty()) {
        toId = targetText;
    }

    ChatScope scope = isGroup ? CHAT_GROUP : CHAT_PRIVATE;
    tcpClient_->sendChatMessage(scope, toId, text);

    QString sender = tcpClient_->nickname().isEmpty() ? "Me" : tcpClient_->nickname();
    appendMessage(sender, text, QDateTime::currentDateTime(), true,
                  isGroup ? "Group" : toId);

    ui->plainTextEdit_input->clear();
}

void ChatWindow::onSelectFile() {
    if (!tcpClient_ || !tcpClient_->isConnected()) {
        QMessageBox::warning(this, "Warning", "Not connected to the server.");
        return;
    }

    QString toId = ui->combo_target->currentData().toString().trimmed();
    QString targetText = ui->combo_target->currentText().trimmed();
    bool isGroup = (toId.isEmpty() && targetText == "Group (All)");
    if (!isGroup && toId.isEmpty()) {
        toId = targetText;
    }
    if (isGroup || toId.isEmpty()) {
        QMessageBox::warning(this, "Warning", "Please select a user for file transfer.");
        return;
    }

    QString filePath = QFileDialog::getOpenFileName(this, "Select File");
    if (filePath.isEmpty()) {
        return;
    }

    QFileInfo info(filePath);
    QString fileId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString fileName = info.fileName();
    quint64 fileSize = static_cast<quint64>(info.size());

    tcpClient_->sendFileOffer(fileId, filePath, fileName, fileSize, toId);
    addTransferRow(fileId, fileName, fileSize, "Waiting for accept", false);
}

void ChatWindow::onChatMessageReceived(const QString &fromId,
                                       const QString &fromNick,
                                       const QString &message,
                                       bool isPrivate,
                                       const QString &toId,
                                       quint64 timestamp) {
    QString sender = fromNick.isEmpty() ? fromId : fromNick;
    QString target = isPrivate ? (toId.isEmpty() ? "Private" : toId) : QString();
    QDateTime ts = timestamp == 0
        ? QDateTime::currentDateTime()
        : fromEpochSeconds(timestamp);

    appendMessage(sender, message, ts, false, target);
}

void ChatWindow::onUserListUpdated() {
    updateUserList();
}

void ChatWindow::updateUserList() {
    ui->combo_target->clear();
    ui->combo_target->addItem("Group (All)", QString());

    ui->list_online->clear();

    if (!tcpClient_) {
        return;
    }

    const auto &users = tcpClient_->userList();
    for (const auto &user : users) {
        QString id = QString::fromUtf8(user.clientId);
        QString nick = QString::fromUtf8(user.nickname);
        if (!tcpClient_->clientId().isEmpty() && id == tcpClient_->clientId()) {
            continue;
        }
        QString label = nick.isEmpty() ? id : QString("%1 (%2)").arg(nick, id);
        ui->combo_target->addItem(label, id);
        ui->list_online->addItem(label);
    }
}

void ChatWindow::onFileOfferReceived(const QString &fileId,
                                     const QString &fileName,
                                     quint64 fileSize,
                                     const QString &fromId,
                                     const QString &fromNick) {
    QString label = fromNick.isEmpty() ? fromId : fromNick;
    QString status = QString("Incoming from %1").arg(label);
    addTransferRow(fileId, fileName, fileSize, status, true);
}

void ChatWindow::onFileOfferResponseReceived(const QString &fileId,
                                             uint32_t result,
                                             const QString &message) {
    QString status;
    switch (result) {
        case FILE_OFFER_ACCEPT:
            status = "Accepted";
            break;
        case FILE_OFFER_DECLINE:
            status = "Declined";
            break;
        case FILE_OFFER_BUSY:
            status = "Busy";
            break;
        default:
            status = "Unknown";
            break;
    }

    if (!message.isEmpty()) {
        status = QString("%1 (%2)").arg(status, message);
    }

    updateTransferStatus(fileId, status);
}

void ChatWindow::onFileTransferProgress(const QString &fileId,
                                        quint64 bytesTransferred,
                                        quint64 totalBytes,
                                        bool incoming) {
    if (totalBytes == 0) {
        return;
    }

    double percent = (static_cast<double>(bytesTransferred) / static_cast<double>(totalBytes)) * 100.0;
    QString status = QString("%1 %2% (%3 / %4)")
        .arg(incoming ? "Receiving" : "Sending")
        .arg(QString::number(percent, 'f', 1))
        .arg(formatSize(bytesTransferred))
        .arg(formatSize(totalBytes));

    updateTransferStatus(fileId, status);
}

void ChatWindow::onFileTransferCompleted(const QString &fileId,
                                         bool incoming,
                                         bool success,
                                         const QString &message) {
    QString status;
    if (success) {
        status = incoming ? "Received" : "Sent";
        if (!message.isEmpty()) {
            status = QString("%1 (%2)").arg(status, message);
        }
    } else {
        status = message.isEmpty() ? "Failed" : QString("Failed (%1)").arg(message);
    }

    updateTransferStatus(fileId, status);
}

void ChatWindow::appendMessage(const QString &sender,
                               const QString &message,
                               const QDateTime &timestamp,
                               bool outgoing,
                               const QString &target) {
    QString timeStr = timestamp.isValid() ? timestamp.toString("HH:mm:ss")
                                          : QDateTime::currentDateTime().toString("HH:mm:ss");
    QString prefix = QString("[%1]").arg(timeStr);

    QString line;
    if (outgoing) {
        if (target.isEmpty()) {
            line = QString("%1 %2: %3").arg(prefix, sender, message);
        } else {
            line = QString("%1 %2 -> %3: %4").arg(prefix, sender, target, message);
        }
    } else {
        if (target.isEmpty()) {
            line = QString("%1 %2: %3").arg(prefix, sender, message);
        } else {
            line = QString("%1 %2 -> %3: %4").arg(prefix, sender, target, message);
        }
    }

    ui->textBrowser_messages->append(line);
}

void ChatWindow::addTransferRow(const QString &fileId,
                                const QString &fileName,
                                quint64 fileSize,
                                const QString &status,
                                bool incoming) {
    if (transferRows_.contains(fileId)) {
        updateTransferStatus(fileId, status);
        return;
    }

    int row = ui->table_transfers->rowCount();
    ui->table_transfers->insertRow(row);

    ui->table_transfers->setItem(row, 0, new QTableWidgetItem(fileName));
    ui->table_transfers->setItem(row, 1, new QTableWidgetItem(formatSize(fileSize)));
    ui->table_transfers->setItem(row, 2, new QTableWidgetItem(status));

    QWidget *actionWidget = new QWidget(ui->table_transfers);
    QHBoxLayout *actionLayout = new QHBoxLayout(actionWidget);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(4);

    if (incoming) {
        auto *acceptBtn = new QPushButton("Accept", actionWidget);
        auto *declineBtn = new QPushButton("Decline", actionWidget);

        connect(acceptBtn, &QPushButton::clicked, this, [this, fileId, acceptBtn, declineBtn]() {
            if (tcpClient_) {
                tcpClient_->sendFileOfferResponse(fileId, FILE_OFFER_ACCEPT, "Accepted");
            }
            updateTransferStatus(fileId, "Accepted");
            acceptBtn->setEnabled(false);
            declineBtn->setEnabled(false);
        });
        connect(declineBtn, &QPushButton::clicked, this, [this, fileId, acceptBtn, declineBtn]() {
            if (tcpClient_) {
                tcpClient_->sendFileOfferResponse(fileId, FILE_OFFER_DECLINE, "Declined");
            }
            updateTransferStatus(fileId, "Declined");
            acceptBtn->setEnabled(false);
            declineBtn->setEnabled(false);
        });

        actionLayout->addWidget(acceptBtn);
        actionLayout->addWidget(declineBtn);
    } else {
        auto *cancelBtn = new QPushButton("Cancel", actionWidget);
        connect(cancelBtn, &QPushButton::clicked, this, [this, fileId, cancelBtn]() {
            updateTransferStatus(fileId, "Canceled");
            cancelBtn->setEnabled(false);
        });
        actionLayout->addWidget(cancelBtn);
    }

    actionWidget->setLayout(actionLayout);
    ui->table_transfers->setCellWidget(row, 3, actionWidget);
    transferRows_.insert(fileId, row);
}

void ChatWindow::updateTransferStatus(const QString &fileId, const QString &status) {
    auto it = transferRows_.find(fileId);
    if (it == transferRows_.end()) {
        return;
    }

    int row = it.value();
    QTableWidgetItem *item = ui->table_transfers->item(row, 2);
    if (!item) {
        item = new QTableWidgetItem(status);
        ui->table_transfers->setItem(row, 2, item);
        return;
    }

    item->setText(status);
}

QString ChatWindow::formatSize(quint64 size) const {
    double value = static_cast<double>(size);
    const char *units[] = {"B", "KB", "MB", "GB"};
    int unitIndex = 0;

    while (value >= 1024.0 && unitIndex < 3) {
        value /= 1024.0;
        ++unitIndex;
    }

    int precision = (unitIndex == 0) ? 0 : 1;
    return QString::number(value, 'f', precision) + " " + units[unitIndex];
}
