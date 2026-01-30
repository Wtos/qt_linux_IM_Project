#include "tcpclient.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QtGlobal>

#include <cstring>

namespace {
constexpr int kFileChunkSize = 16 * 1024;

uint64_t currentEpochSeconds() {
#if QT_VERSION >= QT_VERSION_CHECK(5, 8, 0)
    return static_cast<uint64_t>(QDateTime::currentSecsSinceEpoch());
#else
    return static_cast<uint64_t>(QDateTime::currentDateTime().toTime_t());
#endif
}
} // namespace

TcpClient::TcpClient(QObject *parent)
    : QObject(parent)
    , socket_(nullptr)
    , heartbeatTimer_(nullptr)
    , sequence_(0) {
    socket_ = new QTcpSocket(this);

    heartbeatTimer_ = new QTimer(this);
    heartbeatTimer_->setInterval(5000);

    connect(socket_, &QTcpSocket::connected, this, &TcpClient::onConnected);
    connect(socket_, &QTcpSocket::disconnected, this, &TcpClient::onDisconnected);
    connect(socket_, &QTcpSocket::readyRead, this, &TcpClient::onReadyRead);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(socket_, &QTcpSocket::errorOccurred, this, &TcpClient::onError);
#else
    connect(socket_, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
            this, &TcpClient::onError);
#endif
    connect(heartbeatTimer_, &QTimer::timeout, this, &TcpClient::onHeartbeatTimeout);
}

TcpClient::~TcpClient() {
    disconnectFromServer();
}

void TcpClient::connectToServer(const QString &ip, int port) {
    qDebug() << "Connecting to" << ip << ":" << port;
    socket_->connectToHost(ip, port);
}

void TcpClient::disconnectFromServer() {
    heartbeatTimer_->stop();
    clearFileSessions();
    if (isConnected()) {
        sendLogoutRequest();
    }
    if (socket_->state() != QAbstractSocket::UnconnectedState) {
        socket_->disconnectFromHost();
    }
}

void TcpClient::setIdentity(const QString &clientId, const QString &nickname) {
    clientId_ = clientId.trimmed();
    nickname_ = nickname.trimmed();
}

QString TcpClient::clientId() const {
    return clientId_;
}

QString TcpClient::nickname() const {
    return nickname_;
}

void TcpClient::sendLoginRequest(const QString &clientId, const QString &nickname) {
    qDebug() << "Sending login request" << clientId << nickname;

    auto data = ProtocolParser::packLoginRequest(
        ++sequence_,
        clientId.toStdString(),
        nickname.toStdString());

    sendData(QByteArray(reinterpret_cast<const char *>(data.data()),
                        static_cast<int>(data.size())));
}

void TcpClient::sendLogoutRequest() {
    qDebug() << "Sending logout request";

    auto data = ProtocolParser::packLogoutRequest(++sequence_);
    sendData(QByteArray(reinterpret_cast<const char *>(data.data()),
                        static_cast<int>(data.size())));
}

void TcpClient::sendHeartbeat() {
    qDebug() << "Sending heartbeat";

    auto data = ProtocolParser::packHeartbeatRequest(++sequence_);
    sendData(QByteArray(reinterpret_cast<const char *>(data.data()),
                        static_cast<int>(data.size())));
}

void TcpClient::sendChatMessage(ChatScope scope, const QString &toId, const QString &message) {
    qDebug() << "Sending chat message" << (scope == CHAT_PRIVATE ? "private" : "group");

    uint64_t timestamp = currentEpochSeconds();
    auto data = ProtocolParser::packChatMessage(
        ++sequence_,
        scope,
        clientId_.toStdString(),
        nickname_.toStdString(),
        toId.toStdString(),
        message.toStdString(),
        timestamp);

    sendData(QByteArray(reinterpret_cast<const char *>(data.data()),
                        static_cast<int>(data.size())));
}

void TcpClient::sendFileOffer(const QString &fileId,
                              const QString &filePath,
                              const QString &fileName,
                              quint64 fileSize,
                              const QString &toId) {
    qDebug() << "Sending file offer" << fileName << fileSize;

    QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        emit fileTransferCompleted(fileId, false, false, "File not found");
        return;
    }

    FileSendSession session;
    session.fileId = fileId;
    session.filePath = filePath;
    session.fileName = fileName;
    session.fileSize = fileSize;
    session.toId = toId;
    sendSessions_.insert(fileId, session);

    auto data = ProtocolParser::packFileOffer(
        ++sequence_,
        fileId.toStdString(),
        fileName.toStdString(),
        static_cast<uint64_t>(fileSize),
        clientId_.toStdString(),
        nickname_.toStdString(),
        toId.toStdString());

    sendData(QByteArray(reinterpret_cast<const char *>(data.data()),
                        static_cast<int>(data.size())));
}

void TcpClient::sendFileOfferResponse(const QString &fileId,
                                      uint32_t result,
                                      const QString &message) {
    qDebug() << "Sending file offer response" << fileId << result;

    QString responseMessage = message;
    auto pendingIt = pendingOffers_.find(fileId);
    if (result == FILE_OFFER_ACCEPT) {
        if (pendingIt == pendingOffers_.end()) {
            result = FILE_OFFER_DECLINE;
            responseMessage = "Offer expired";
        } else {
            const PendingOffer &offer = pendingIt.value();
            FileReceiveSession session;
            session.fileId = offer.fileId;
            session.fileName = offer.fileName;
            session.fileSize = offer.fileSize;
            session.savePath = buildDownloadPath(session.fileName);

            auto file = QSharedPointer<QFile>::create(session.savePath);
            if (!file->open(QIODevice::WriteOnly)) {
                result = FILE_OFFER_DECLINE;
                responseMessage = "Cannot save file";
                emit fileTransferCompleted(fileId, true, false, responseMessage);
            } else {
                session.file = file;
                recvSessions_.insert(fileId, session);
            }
        }
    }

    auto data = ProtocolParser::packFileOfferResponse(
        ++sequence_,
        fileId.toStdString(),
        result,
        responseMessage.toStdString());

    sendData(QByteArray(reinterpret_cast<const char *>(data.data()),
                        static_cast<int>(data.size())));

    if (pendingIt != pendingOffers_.end()) {
        pendingOffers_.erase(pendingIt);
    }
}

void TcpClient::requestUserList() {
    qDebug() << "Requesting user list";

    auto data = ProtocolParser::packUserListRequest(++sequence_);
    sendData(QByteArray(reinterpret_cast<const char *>(data.data()),
                        static_cast<int>(data.size())));
}

bool TcpClient::isConnected() const {
    return socket_->state() == QAbstractSocket::ConnectedState;
}

const QVector<UserInfo> &TcpClient::userList() const {
    return userList_;
}

void TcpClient::onConnected() {
    qDebug() << "TCP connected";

    recvBuffer_.clear();
    sequence_ = 0;
    heartbeatTimer_->start();

    emit connected();
}

void TcpClient::onDisconnected() {
    qDebug() << "TCP disconnected";

    heartbeatTimer_->stop();
    clearFileSessions();

    emit disconnected();
}

// 如何处理粘包和半包？
void TcpClient::onReadyRead() {
    QByteArray newData = socket_->readAll();
    recvBuffer_.append(newData);

    qDebug() << "Received" << newData.size() << "bytes";

    while (recvBuffer_.size() >= static_cast<int>(sizeof(MessageHeader))) {
        MessageHeader header;
        std::memcpy(&header, recvBuffer_.data(), sizeof(MessageHeader));

        header.magic = ntohl(header.magic);
        header.version = ntohs(header.version);
        header.msgType = ntohs(header.msgType);
        header.bodyLength = ntohl(header.bodyLength);
        header.sequence = ntohl(header.sequence);

        if (!ProtocolParser::validateHeader(header)) {
            qWarning() << "Invalid header, clearing buffer";
            recvBuffer_.clear();
            break;
        }

        int totalLen = static_cast<int>(sizeof(MessageHeader) + header.bodyLength);
        if (recvBuffer_.size() < totalLen) {
            break;
        }

        QByteArray body = recvBuffer_.mid(sizeof(MessageHeader), header.bodyLength);
        processMessage(header, body);
        recvBuffer_.remove(0, totalLen);
    }
}

void TcpClient::onError(QAbstractSocket::SocketError error) {
    if (error == QAbstractSocket::RemoteHostClosedError) {
        return;
    }

    QString errorStr;
    switch (error) {
        case QAbstractSocket::ConnectionRefusedError:
            errorStr = "Connection refused";
            break;
        case QAbstractSocket::HostNotFoundError:
            errorStr = "Host not found";
            break;
        case QAbstractSocket::NetworkError:
            errorStr = "Network error";
            break;
        case QAbstractSocket::SocketTimeoutError:
            errorStr = "Connection timeout";
            break;
        default:
            errorStr = socket_->errorString();
            break;
    }

    qWarning() << "Socket error:" << errorStr;

    emit connectError(errorStr);
}

void TcpClient::onHeartbeatTimeout() {
    sendHeartbeat();
}

void TcpClient::sendData(const QByteArray &data) {
    if (!isConnected()) {
        qWarning() << "Not connected, skip send";
        return;
    }

    qint64 written = socket_->write(data);
    socket_->flush();

    qDebug() << "Sent" << written << "/" << data.size() << "bytes";
}

void TcpClient::processMessage(const MessageHeader &header, const QByteArray &body) {
    qDebug() << "Process msg type" << header.msgType
             << "seq" << header.sequence
             << "len" << header.bodyLength;

    switch (header.msgType) {
        case MSG_HEARTBEAT_RSP:
            qDebug() << "Heartbeat response";
            break;
        case MSG_LOGIN_RSP: {
            LoginResponse rsp;
            if (ProtocolParser::parseLoginResponse(
                    reinterpret_cast<const uint8_t *>(body.data()),
                    static_cast<size_t>(body.size()), rsp)) {
                bool success = (rsp.result == LOGIN_SUCCESS);
                QString message = QString::fromUtf8(rsp.message);

                qDebug() << "Login response" << rsp.result << message;
                emit loginResponse(success, message);
            } else {
                qWarning() << "Failed to parse login response";
                emit loginResponse(false, "Failed to parse response");
            }
            break;
        }
        case MSG_CHAT_MSG: {
            ChatMessage msg;
            if (ProtocolParser::parseChatMessage(
                    reinterpret_cast<const uint8_t *>(body.data()),
                    static_cast<size_t>(body.size()), msg)) {
                bool isPrivate = (msg.chatType == CHAT_PRIVATE);
                QString fromId = QString::fromUtf8(msg.fromId);
                QString fromNick = QString::fromUtf8(msg.fromNick);
                QString toId = QString::fromUtf8(msg.toId);
                QString text = QString::fromUtf8(msg.message);
                emit chatMessageReceived(fromId, fromNick, text, isPrivate, toId, msg.timestamp);
            } else {
                qWarning() << "Failed to parse chat message";
            }
            break;
        }
        case MSG_USER_LIST_RSP: {
            std::vector<UserInfo> users;
            if (ProtocolParser::parseUserListResponse(
                    reinterpret_cast<const uint8_t *>(body.data()),
                    static_cast<size_t>(body.size()), users)) {
                userList_.clear();
                userList_.reserve(static_cast<int>(users.size()));
                for (const auto &user : users) {
                    userList_.push_back(user);
                }
                emit userListUpdated();
            } else {
                qWarning() << "Failed to parse user list";
            }
            break;
        }
        case MSG_FILE_OFFER: {
            FileOffer offer;
            if (ProtocolParser::parseFileOffer(
                    reinterpret_cast<const uint8_t *>(body.data()),
                    static_cast<size_t>(body.size()), offer)) {
                QString fileId = QString::fromUtf8(offer.fileId);
                QString fileName = QString::fromUtf8(offer.fileName);
                QString fromId = QString::fromUtf8(offer.fromId);
                QString fromNick = QString::fromUtf8(offer.fromNick);

                PendingOffer pending;
                pending.fileId = fileId;
                pending.fileName = fileName;
                pending.fileSize = offer.fileSize;
                pending.fromId = fromId;
                pending.fromNick = fromNick;
                pendingOffers_.insert(fileId, pending);

                emit fileOfferReceived(fileId, fileName, offer.fileSize, fromId, fromNick);
            } else {
                qWarning() << "Failed to parse file offer";
            }
            break;
        }
        case MSG_FILE_OFFER_RSP: {
            FileOfferResponse rsp;
            if (ProtocolParser::parseFileOfferResponse(
                    reinterpret_cast<const uint8_t *>(body.data()),
                    static_cast<size_t>(body.size()), rsp)) {
                QString fileId = QString::fromUtf8(rsp.fileId);
                QString message = QString::fromUtf8(rsp.message);
                emit fileOfferResponseReceived(fileId, rsp.result, message);

                if (rsp.result == FILE_OFFER_ACCEPT) {
                    startFileSend(fileId);
                } else {
                    if (sendSessions_.contains(fileId)) {
                        emit fileTransferCompleted(fileId, false, false, message);
                        sendSessions_.remove(fileId);
                    }
                }
            } else {
                qWarning() << "Failed to parse file offer response";
            }
            break;
        }
        case MSG_FILE_DATA: {
            FileDataHeader headerData;
            const uint8_t *payload = nullptr;
            size_t payloadLen = 0;
            if (ProtocolParser::parseFileData(
                    reinterpret_cast<const uint8_t *>(body.data()),
                    static_cast<size_t>(body.size()),
                    headerData,
                    payload,
                    payloadLen)) {
                QString fileId = QString::fromUtf8(headerData.fileId);
                QByteArray dataChunk;
                if (payloadLen > 0) {
                    dataChunk = QByteArray(reinterpret_cast<const char *>(payload),
                                            static_cast<int>(payloadLen));
                }
                handleFileData(fileId, headerData.offset, dataChunk);
            } else {
                qWarning() << "Failed to parse file data";
            }
            break;
        }
        default:
            qWarning() << "Unknown message type" << header.msgType;
            break;
    }
}

void TcpClient::startFileSend(const QString &fileId) {
    if (!sendSessions_.contains(fileId)) {
        return;
    }

    FileSendSession &session = sendSessions_[fileId];
    if (session.started) {
        return;
    }

    session.file = QSharedPointer<QFile>::create(session.filePath);
    if (!session.file->open(QIODevice::ReadOnly)) {
        emit fileTransferCompleted(fileId, false, false, "Cannot open file");
        sendSessions_.remove(fileId);
        return;
    }

    session.started = true;
    session.bytesSent = 0;

    while (!session.file->atEnd()) {
        QByteArray chunk = session.file->read(kFileChunkSize);
        if (chunk.isEmpty() && !session.file->atEnd()) {
            emit fileTransferCompleted(fileId, false, false, "Read error");
            session.file->close();
            sendSessions_.remove(fileId);
            return;
        }

        auto packet = ProtocolParser::packFileData(
            ++sequence_,
            fileId.toStdString(),
            session.bytesSent,
            reinterpret_cast<const uint8_t *>(chunk.data()),
            static_cast<size_t>(chunk.size()));

        sendData(QByteArray(reinterpret_cast<const char *>(packet.data()),
                            static_cast<int>(packet.size())));

        session.bytesSent += static_cast<quint64>(chunk.size());
        emit fileTransferProgress(fileId, session.bytesSent, session.fileSize, false);
    }

    session.file->close();
    emit fileTransferCompleted(fileId, false, true, "Sent");
    sendSessions_.remove(fileId);
}

void TcpClient::handleFileData(const QString &fileId, quint64 offset, const QByteArray &payload) {
    if (!recvSessions_.contains(fileId)) {
        return;
    }

    FileReceiveSession &session = recvSessions_[fileId];
    if (!session.file || !session.file->isOpen()) {
        emit fileTransferCompleted(fileId, true, false, "File not open");
        recvSessions_.remove(fileId);
        return;
    }

    if (offset != session.bytesReceived) {
        if (!session.file->seek(static_cast<qint64>(offset))) {
            emit fileTransferCompleted(fileId, true, false, "Seek failed");
            session.file->close();
            recvSessions_.remove(fileId);
            return;
        }
        session.bytesReceived = offset;
    }

    if (!payload.isEmpty()) {
        qint64 written = session.file->write(payload);
        if (written < 0) {
            emit fileTransferCompleted(fileId, true, false, "Write failed");
            session.file->close();
            recvSessions_.remove(fileId);
            return;
        }
        session.bytesReceived += static_cast<quint64>(written);
    }

    emit fileTransferProgress(fileId, session.bytesReceived, session.fileSize, true);

    if (session.fileSize > 0 && session.bytesReceived >= session.fileSize) {
        session.file->flush();
        session.file->close();
        emit fileTransferCompleted(fileId, true, true, session.savePath);
        recvSessions_.remove(fileId);
    }
}

QString TcpClient::buildDownloadPath(const QString &fileName) const {
    QString baseDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (baseDir.isEmpty()) {
        baseDir = QDir::current().filePath("downloads");
    }

    QDir dir(baseDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QString candidate = dir.filePath(fileName);
    if (!QFile::exists(candidate)) {
        return candidate;
    }

    QFileInfo info(fileName);
    QString baseName = info.completeBaseName();
    QString suffix = info.suffix();

    int index = 1;
    while (true) {
        QString name = suffix.isEmpty()
            ? QString("%1_%2").arg(baseName).arg(index)
            : QString("%1_%2.%3").arg(baseName).arg(index).arg(suffix);
        candidate = dir.filePath(name);
        if (!QFile::exists(candidate)) {
            return candidate;
        }
        ++index;
    }
}

void TcpClient::clearFileSessions() {
    for (auto it = sendSessions_.begin(); it != sendSessions_.end(); ++it) {
        if (it->file && it->file->isOpen()) {
            it->file->close();
        }
    }
    for (auto it = recvSessions_.begin(); it != recvSessions_.end(); ++it) {
        if (it->file && it->file->isOpen()) {
            it->file->close();
        }
    }
    sendSessions_.clear();
    recvSessions_.clear();
    pendingOffers_.clear();
}
