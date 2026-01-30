#include "loginwindow.h"
#include "ui_loginwindow.h"

#include "chatwindow.h"

#include <QDateTime>
#include <QFont>
#include <QMessageBox>
#include <QPalette>
#include <QRandomGenerator>

LoginWindow::LoginWindow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::LoginWindow)
    , tcpClient_(nullptr)
    , settings_(nullptr)
    , chatWindow_(nullptr) {
    ui->setupUi(this);

    settings_ = new QSettings("config/config.ini", QSettings::IniFormat, this);
    tcpClient_ = new TcpClient(this);

    setupUI();
    setupConnections();
    loadConfig();
}

LoginWindow::~LoginWindow() {
    delete ui;
}

void LoginWindow::setupUI() {
    setWindowTitle("IM Client - Login");
    resize(400, 350);
    setMinimumSize(400, 350);

    ui->lineEdit_ip->setText("127.0.0.1");
    ui->lineEdit_port->setText("8888");
    ui->lineEdit_clientId->setText(generateClientId());
    ui->lineEdit_nickname->setPlaceholderText("Enter nickname");

    QFont font = ui->label_status->font();
    font.setPointSize(10);
    ui->label_status->setFont(font);

    updateStatus("Disconnected", Qt::gray);
}

void LoginWindow::setupConnections() {
    connect(ui->btn_connect, &QPushButton::clicked, this, &LoginWindow::onConnectClicked);

    connect(tcpClient_, &TcpClient::connected, this, &LoginWindow::onConnected);
    connect(tcpClient_, &TcpClient::connectError, this, &LoginWindow::onConnectError);
    connect(tcpClient_, &TcpClient::disconnected, this, &LoginWindow::onDisconnected);
    connect(tcpClient_, &TcpClient::loginResponse, this, &LoginWindow::onLoginResponse);
}

void LoginWindow::onConnectClicked() {
    QString ip = ui->lineEdit_ip->text().trimmed();
    QString portStr = ui->lineEdit_port->text().trimmed();
    QString clientId = ui->lineEdit_clientId->text().trimmed();
    QString nickname = ui->lineEdit_nickname->text().trimmed();

    if (ip.isEmpty()) {
        QMessageBox::warning(this, "Warning", "Please enter a server IP.");
        return;
    }

    if (portStr.isEmpty()) {
        QMessageBox::warning(this, "Warning", "Please enter a port.");
        return;
    }

    if (nickname.isEmpty()) {
        QMessageBox::warning(this, "Warning", "Please enter a nickname.");
        return;
    }

    bool ok = false;
    int port = portStr.toInt(&ok);
    if (!ok || port <= 0 || port > 65535) {
        QMessageBox::warning(this, "Warning", "Invalid port number.");
        return;
    }

    ui->btn_connect->setEnabled(false);
    updateStatus("Connecting...", QColor(255, 165, 0));

    saveConfig();

    tcpClient_->setIdentity(clientId, nickname);
    tcpClient_->connectToServer(ip, port);
}

void LoginWindow::onConnected() {
    updateStatus("Connected, logging in...", QColor(0, 128, 255));

    tcpClient_->sendLoginRequest(tcpClient_->clientId(), tcpClient_->nickname());
}

void LoginWindow::onConnectError(const QString &error) {
    updateStatus("Connection failed", Qt::red);

    QMessageBox::critical(this, "Connection Error",
                          QString("Unable to connect to server:\n%1").arg(error));

    ui->btn_connect->setEnabled(true);
}

void LoginWindow::onDisconnected() {
    updateStatus("Disconnected", Qt::red);

    if (isVisible()) {
        QMessageBox::information(this, "Disconnected", "Connection lost. Please reconnect.");
    }

    ui->btn_connect->setEnabled(true);
}

void LoginWindow::onLoginResponse(bool success, const QString &message) {
    if (success) {
        updateStatus("Login successful", QColor(0, 200, 0));

        QMessageBox::information(this, "Login", QString("Login successful.\n%1").arg(message));

        if (!chatWindow_) {
            chatWindow_ = new ChatWindow(tcpClient_);
            connect(chatWindow_, &ChatWindow::windowClosed, this, [this]() {
                this->show();
                ui->btn_connect->setEnabled(true);
            });
        }

        chatWindow_->show();
        chatWindow_->raise();
        chatWindow_->activateWindow();
        this->hide();
    } else {
        updateStatus("Login failed", Qt::red);

        QMessageBox::critical(this, "Login Failed", QString("Login failed:\n%1").arg(message));

        tcpClient_->disconnectFromServer();
        ui->btn_connect->setEnabled(true);
    }
}

void LoginWindow::saveConfig() {
    settings_->setValue("server/ip", ui->lineEdit_ip->text());
    settings_->setValue("server/port", ui->lineEdit_port->text());
    settings_->setValue("user/nickname", ui->lineEdit_nickname->text());
    settings_->sync();
}

void LoginWindow::loadConfig() {
    if (settings_->contains("server/ip")) {
        ui->lineEdit_ip->setText(settings_->value("server/ip").toString());
    }
    if (settings_->contains("server/port")) {
        ui->lineEdit_port->setText(settings_->value("server/port").toString());
    }
    if (settings_->contains("user/nickname")) {
        ui->lineEdit_nickname->setText(settings_->value("user/nickname").toString());
    }
}

QString LoginWindow::generateClientId() {
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    int random = QRandomGenerator::global()->bounded(1000, 9999);
    return QString("CLIENT_%1_%2").arg(timestamp).arg(random);
}

void LoginWindow::updateStatus(const QString &status, const QColor &color) {
    ui->label_status->setText(QString("Status: %1").arg(status));

    QPalette palette = ui->label_status->palette();
    palette.setColor(QPalette::WindowText, color);
    ui->label_status->setPalette(palette);
}
