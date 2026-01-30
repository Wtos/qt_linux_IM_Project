#ifndef CHATWINDOW_H
#define CHATWINDOW_H

#include <QCloseEvent>
#include <QColor>
#include <QDateTime>
#include <QHash>
#include <QWidget>

#include "tcpclient.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class ChatWindow;
}
QT_END_NAMESPACE

class ChatWindow : public QWidget {
    Q_OBJECT

public:
    explicit ChatWindow(TcpClient *client, QWidget *parent = nullptr);
    ~ChatWindow();

signals:
    void windowClosed();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onSendClicked();
    void onSelectFile();
    void onChatMessageReceived(const QString &fromId,
                               const QString &fromNick,
                               const QString &message,
                               bool isPrivate,
                               const QString &toId,
                               quint64 timestamp);
    void onUserListUpdated();
    void onFileOfferReceived(const QString &fileId,
                             const QString &fileName,
                             quint64 fileSize,
                             const QString &fromId,
                             const QString &fromNick);
    void onFileOfferResponseReceived(const QString &fileId,
                                     uint32_t result,
                                     const QString &message);
    void onFileTransferProgress(const QString &fileId,
                                quint64 bytesTransferred,
                                quint64 totalBytes,
                                bool incoming);
    void onFileTransferCompleted(const QString &fileId,
                                 bool incoming,
                                 bool success,
                                 const QString &message);
    void onConnected();
    void onDisconnected();

private:
    void setupUI();
    void updateUserList();
    void setStatus(const QString &text, const QColor &color);
    void appendMessage(const QString &sender,
                       const QString &message,
                       const QDateTime &timestamp,
                       bool outgoing,
                       const QString &target);
    void addTransferRow(const QString &fileId,
                        const QString &fileName,
                        quint64 fileSize,
                        const QString &status,
                        bool incoming);
    void updateTransferStatus(const QString &fileId, const QString &status);
    QString formatSize(quint64 size) const;

private:
    Ui::ChatWindow *ui;
    TcpClient *tcpClient_;
    QHash<QString, int> transferRows_;
};

#endif // CHATWINDOW_H
