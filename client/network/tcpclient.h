#ifndef TCPCLIENT_H
#define TCPCLIENT_H

#include <QObject>
#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QSharedPointer>
#include <QString>
#include <QTcpSocket>
#include <QTimer>
#include <QVector>

#include "protocol.h"

class TcpClient : public QObject {
    Q_OBJECT

public:
    explicit TcpClient(QObject *parent = nullptr);
    ~TcpClient();

    void connectToServer(const QString &ip, int port);
    void disconnectFromServer();
    void setIdentity(const QString &clientId, const QString &nickname);
    QString clientId() const;
    QString nickname() const;
    void sendLoginRequest(const QString &clientId, const QString &nickname);
    void sendLogoutRequest();
    void sendHeartbeat();
    void sendChatMessage(ChatScope scope, const QString &toId, const QString &message);
    void sendFileOffer(const QString &fileId,
                       const QString &filePath,
                       const QString &fileName,
                       quint64 fileSize,
                       const QString &toId);
    void sendFileOfferResponse(const QString &fileId, uint32_t result, const QString &message);
    void requestUserList();
    bool isConnected() const;
    const QVector<UserInfo> &userList() const;

signals:
    void connected();
    void connectError(const QString &error);
    void disconnected();
    void loginResponse(bool success, const QString &message);
    void chatMessageReceived(const QString &fromId,
                             const QString &fromNick,
                             const QString &message,
                             bool isPrivate,
                             const QString &toId,
                             quint64 timestamp);
    void userListUpdated();
    void fileOfferReceived(const QString &fileId,
                           const QString &fileName,
                           quint64 fileSize,
                           const QString &fromId,
                           const QString &fromNick);
    void fileOfferResponseReceived(const QString &fileId, uint32_t result, const QString &message);
    void fileTransferProgress(const QString &fileId,
                              quint64 bytesTransferred,
                              quint64 totalBytes,
                              bool incoming);
    void fileTransferCompleted(const QString &fileId,
                               bool incoming,
                               bool success,
                               const QString &message);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onError(QAbstractSocket::SocketError error);
    void onHeartbeatTimeout();

private:
    struct PendingOffer {
        QString fileId;
        QString fileName;
        quint64 fileSize = 0;
        QString fromId;
        QString fromNick;
    };

    struct FileSendSession {
        QString fileId;
        QString filePath;
        QString fileName;
        quint64 fileSize = 0;
        QString toId;
        quint64 bytesSent = 0;
        bool started = false;
        QSharedPointer<QFile> file;
    };

    struct FileReceiveSession {
        QString fileId;
        QString fileName;
        quint64 fileSize = 0;
        quint64 bytesReceived = 0;
        QString savePath;
        QSharedPointer<QFile> file;
    };

    void sendData(const QByteArray &data);
    void processMessage(const MessageHeader &header, const QByteArray &body);
    void startFileSend(const QString &fileId);
    void handleFileData(const QString &fileId, quint64 offset, const QByteArray &payload);
    QString buildDownloadPath(const QString &fileName) const;
    void clearFileSessions();

private:
    QTcpSocket *socket_;
    QTimer *heartbeatTimer_;
    QByteArray recvBuffer_;
    uint32_t sequence_;
    QString clientId_;
    QString nickname_;
    QVector<UserInfo> userList_;
    QHash<QString, PendingOffer> pendingOffers_;
    QHash<QString, FileSendSession> sendSessions_;
    QHash<QString, FileReceiveSession> recvSessions_;
};

#endif // TCPCLIENT_H
