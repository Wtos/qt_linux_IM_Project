#include "server.h"
#include "utils.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>

constexpr int kBacklog = 128;
constexpr int kEpollMaxEvents = 1024;
constexpr int kHeartbeatIntervalSec = 5;
constexpr int kHeartbeatTimeoutSec = 10;
constexpr size_t kMaxOnlineClients = 1024;
constexpr size_t kWriteTrimThreshold = 4096;
constexpr size_t kFileIdSize = 37;

static int sendFlags() {
#ifdef MSG_NOSIGNAL
    return MSG_NOSIGNAL;
#else
    return 0;
#endif
}

uint64_t currentEpochSeconds() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<seconds>(system_clock::now().time_since_epoch()).count());
}

std::string extractFileId(const uint8_t* body, size_t bodyLen) {
    if (!body || bodyLen < kFileIdSize) {
        return {};
    }
    char fileId[kFileIdSize];
    std::memcpy(fileId, body, kFileIdSize);
    fileId[kFileIdSize - 1] = '\0';
    return std::string(fileId, boundedStrnlen(fileId, kFileIdSize));
}

class ListenHandler : public EventHandler {
public:
    ListenHandler(Server* server, int listenFd)
        : server_(server), listenFd_(listenFd) {}

    int getHandle() const override {
        return listenFd_;
    }

    void handleRead() override {
        while (true) {
            sockaddr_in clientAddr;
            socklen_t clientLen = sizeof(clientAddr);

            int clientFd = accept(listenFd_,
                                  reinterpret_cast<sockaddr*>(&clientAddr),
                                  &clientLen);
            if (clientFd < 0) {
                if (errno == EINTR) {
                    continue;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                std::cerr << "accept failed: " << std::strerror(errno) << std::endl;
                break;
            }

            char clientIp[INET_ADDRSTRLEN] = {0};
            if (inet_ntop(AF_INET, &clientAddr.sin_addr, clientIp, sizeof(clientIp)) == nullptr) {
                std::strncpy(clientIp, "unknown", sizeof(clientIp) - 1);
            }
            int clientPort = ntohs(clientAddr.sin_port);

            if (!server_->registerClient(clientFd, clientIp, clientPort)) {
                continue;
            }

            std::cout << "[connect] ip=" << clientIp << ":" << clientPort
                      << " fd=" << clientFd << std::endl;
        }
    }

    void handleWrite() override {}

    void handleError(uint32_t events) override {
        std::cerr << "listen socket error events=" << events << std::endl;
    }

private:
    Server* server_;
    int listenFd_;
};

class ClientHandler : public EventHandler {
public:
    ClientHandler(Server* server, int fd)
        : server_(server), fd_(fd) {}

    int getHandle() const override {
        return fd_;
    }

    void handleRead() override {
        if (closing_) {
            return;
        }

        uint8_t buffer[4096];
        while (true) {
            ssize_t n = recv(fd_, buffer, sizeof(buffer), 0);
            if (n > 0) {
                server_->onClientData(fd_, buffer, static_cast<size_t>(n));
                continue;
            }
            if (n == 0) {
                requestClose("peer closed");
                return;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            std::cerr << "recv failed: " << std::strerror(errno) << std::endl;
            requestClose("recv error");
            return;
        }
    }

    void handleWrite() override {
        if (closing_) {
            return;
        }
        if (!flushOut()) {
            return;
        }
        if (!closing_ && pending() == 0) {
            server_->reactor_->modifyHandler(this, EVENT_READ);
        }
    }

    void handleError(uint32_t events) override {
        if (closing_) {
            return;
        }
        std::cerr << "socket error events=" << events << " fd=" << fd_ << std::endl;
        requestClose("socket error");
    }

    bool queueSend(const std::vector<uint8_t>& data) {
        if (closing_ || fd_ < 0) {
            return false;
        }
        if (data.empty()) {
            return true;
        }

        if (pending() == 0) {
            size_t offset = 0;
            while (offset < data.size()) {
                ssize_t n = send(fd_,
                                 data.data() + offset,
                                 data.size() - offset,
                                 sendFlags());
                if (n > 0) {
                    offset += static_cast<size_t>(n);
                    continue;
                }
                if (n == 0) {
                    requestClose("peer closed");
                    return false;
                }
                if (errno == EINTR) {
                    continue;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                std::cerr << "send failed: " << std::strerror(errno) << std::endl;
                requestClose("send error");
                return false;
            }

            if (offset == data.size()) {
                return true;
            }
            appendOut(data.data() + offset, data.size() - offset);
        } else {
            appendOut(data.data(), data.size());
        }

        server_->reactor_->modifyHandler(this, EVENT_READ | EVENT_WRITE);
        return true;
    }

private:
    size_t pending() const {
        return outbuf_.size() - outOffset_;
    }

    void appendOut(const uint8_t* data, size_t len) {
        if (len == 0) {
            return;
        }
        if (pending() == 0) {
            outbuf_.clear();
            outOffset_ = 0;
        }
        outbuf_.insert(outbuf_.end(), data, data + len);
    }

    bool flushOut() {
        while (pending() > 0) {
            ssize_t n = send(fd_,
                             outbuf_.data() + outOffset_,
                             pending(),
                             sendFlags());
            if (n > 0) {
                outOffset_ += static_cast<size_t>(n);
                if (pending() == 0) {
                    outbuf_.clear();
                    outOffset_ = 0;
                } else if (outOffset_ >= kWriteTrimThreshold) {
                    outbuf_.erase(outbuf_.begin(),
                                  outbuf_.begin()
                                      + static_cast<std::vector<uint8_t>::difference_type>(outOffset_));
                    outOffset_ = 0;
                }
                continue;
            }
            if (n == 0) {
                requestClose("peer closed");
                return false;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return true;
            }
            std::cerr << "send failed: " << std::strerror(errno) << std::endl;
            requestClose("send error");
            return false;
        }
        return true;
    }

    void requestClose(const char* reason) {
        if (closing_) {
            return;
        }
        closing_ = true;
        std::cout << "[disconnect] fd=" << fd_ << " reason=" << reason << std::endl;
        server_->queueDisconnect(fd_);
    }

    Server* server_;
    int fd_;
    bool closing_ = false;
    std::vector<uint8_t> outbuf_;
    size_t outOffset_ = 0;
};

Server::Server(const std::string& ip, int port)
    : ip_(ip),
      port_(port),
      listenFd_(-1),
      running_(false) {}

Server::~Server() {
    stop();
    if (heartbeatThread_.joinable()) {
        heartbeatThread_.join();
    }
    cleanupAllClients();
    if (listenFd_ >= 0) {
        close(listenFd_);
        listenFd_ = -1;
    }
}

bool Server::start() {
    if (!initListenSocket()) {
        return false;
    }

    reactor_ = std::make_unique<Reactor>(kEpollMaxEvents);
    if (!reactor_->init()) {
        std::cerr << "epoll_create1 failed: " << std::strerror(errno) << std::endl;
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    clientMgr_ = std::make_unique<ClientManager>();
    protocol_ = std::make_unique<ProtocolParser>();
    listenHandler_ = std::make_unique<ListenHandler>(this, listenFd_);
    if (!reactor_->registerHandler(listenHandler_.get(), EVENT_READ)) {
        std::cerr << "epoll_ctl add listen failed: " << std::strerror(errno) << std::endl;
        listenHandler_.reset();
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }
    return true;
}

void Server::stop() {
    running_ = false;
}

void Server::run() {
    if (!reactor_ || !clientMgr_ || !protocol_) {
        std::cerr << "server not initialized" << std::endl;
        return;
    }

    running_ = true;
    heartbeatThread_ = std::thread(&Server::heartbeatLoop, this);

    while (running_) {
        processPendingDisconnects();
        int nReady = reactor_->poll(1000);
        if (nReady < 0) {
            std::cerr << "epoll_wait error: " << std::strerror(errno) << std::endl;
            break;
        }
        processPendingDisconnects();
    }

    running_ = false;
    if (heartbeatThread_.joinable()) {
        heartbeatThread_.join();
    }
    cleanupAllClients();
    if (listenFd_ >= 0) {
        close(listenFd_);
        listenFd_ = -1;
    }
    listenHandler_.reset();
    reactor_.reset();
}

bool Server::initListenSocket() {
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        std::cerr << "socket failed: " << std::strerror(errno) << std::endl;
        return false;
    }

    int reuse = 1;
    if (setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "setsockopt SO_REUSEADDR failed: " << std::strerror(errno) << std::endl;
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (ip_ == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, ip_.c_str(), &addr.sin_addr) <= 0) {
            std::cerr << "inet_pton failed for ip: " << ip_ << std::endl;
            close(listenFd_);
            listenFd_ = -1;
            return false;
        }
    }

    if (bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "bind failed: " << std::strerror(errno) << std::endl;
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    if (listen(listenFd_, kBacklog) < 0) {
        std::cerr << "listen failed: " << std::strerror(errno) << std::endl;
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    if (!setNonBlocking(listenFd_)) {
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    std::cout << "listening on " << ip_ << ":" << port_ << std::endl;
    return true;
}

bool Server::setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        std::cerr << "fcntl F_GETFL failed: " << std::strerror(errno) << std::endl;
        return false;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        std::cerr << "fcntl F_SETFL failed: " << std::strerror(errno) << std::endl;
        return false;
    }

    return true;
}

bool Server::registerClient(int clientFd, const std::string& ip, int port) {
    if (!reactor_ || !clientMgr_) {
        close(clientFd);
        return false;
    }

    if (!setNonBlocking(clientFd)) {
        close(clientFd);
        return false;
    }

    auto handler = std::make_unique<ClientHandler>(this, clientFd);
    if (!reactor_->registerHandler(handler.get(), EVENT_READ)) {
        std::cerr << "epoll_ctl add client failed: " << std::strerror(errno) << std::endl;
        close(clientFd);
        return false;
    }

    clientMgr_->addClient(clientFd, ip, port);
    clientHandlers_.emplace(clientFd, std::move(handler));
    return true;
}

void Server::onClientData(int clientFd, const uint8_t* data, size_t len) {
    protocol_->parseData(
        clientFd,
        data,
        len,
        [this](int fd, const MessageHeader& header, const uint8_t* body, size_t bodyLen) {
            this->handleMessage(fd, header, body, bodyLen);
        });
}

void Server::handleMessage(int clientFd, const MessageHeader& header,
                           const uint8_t* body, size_t bodyLen) {
    switch (header.msgType) {
        case MSG_HEARTBEAT_REQ: {
            if (bodyLen != 0) {
                std::cerr << "invalid heartbeat body length=" << bodyLen
                          << " fd=" << clientFd << std::endl;
                break;
            }
            clientMgr_->updateHeartbeat(clientFd);
            auto response = ProtocolParser::packHeartbeatResponse(header.sequence);
            if (!sendResponse(clientFd, response)) {
                std::cerr << "send heartbeat response failed for fd=" << clientFd << std::endl;
            }
            break;
        }
        case MSG_LOGIN_REQ: {
            LoginRequest req;
            if (!ProtocolParser::parseLoginRequest(body, bodyLen, req)) {
                auto response = ProtocolParser::packLoginResponse(
                    header.sequence, LOGIN_INVALID_PARAM, "Invalid parameters");
                sendResponse(clientFd, response);
                break;
            }

            std::string clientId(req.clientId,
                                 boundedStrnlen(req.clientId, sizeof(req.clientId)));
            std::string nickname(req.nickname,
                                 boundedStrnlen(req.nickname, sizeof(req.nickname)));

            if (clientId.empty() || nickname.empty()) {
                auto response = ProtocolParser::packLoginResponse(
                    header.sequence, LOGIN_INVALID_PARAM, "Invalid parameters");
                sendResponse(clientFd, response);
                break;
            }

            if (clientMgr_->isClientIdOnline(clientId, clientFd)) {
                auto response = ProtocolParser::packLoginResponse(
                    header.sequence, LOGIN_ALREADY_ONLINE, "Client already online");
                sendResponse(clientFd, response);
                break;
            }

            if (clientMgr_->isNicknameOnline(nickname, clientFd)) {
                auto response = ProtocolParser::packLoginResponse(
                    header.sequence, LOGIN_NICKNAME_TAKEN, "Nickname taken");
                sendResponse(clientFd, response);
                break;
            }

            if (clientMgr_->getOnlineCount() >= kMaxOnlineClients) {
                auto response = ProtocolParser::packLoginResponse(
                    header.sequence, LOGIN_SERVER_FULL, "Server full");
                sendResponse(clientFd, response);
                break;
            }

            if (!clientMgr_->setClientIdentity(clientFd, clientId, nickname)) {
                auto response = ProtocolParser::packLoginResponse(
                    header.sequence, LOGIN_INVALID_PARAM, "Invalid parameters");
                sendResponse(clientFd, response);
                break;
            }

            auto response = ProtocolParser::packLoginResponse(
                header.sequence, LOGIN_SUCCESS, "OK");
            if (!sendResponse(clientFd, response)) {
                std::cerr << "send login response failed for fd=" << clientFd << std::endl;
            }

            std::cout << "[login] fd=" << clientFd
                      << " clientId=" << clientId
                      << " nickname=" << nickname << std::endl;
            broadcastUserList();
            break;
        }
        case MSG_LOGOUT_REQ: {
            if (bodyLen != 0) {
                std::cerr << "invalid logout body length=" << bodyLen
                          << " fd=" << clientFd << std::endl;
            }
            queueDisconnect(clientFd);
            break;
        }
        case MSG_CHAT_MSG:
            handleChatMessage(clientFd, header, body, bodyLen);
            break;
        case MSG_USER_LIST_REQ:
            handleUserListRequest(clientFd, header);
            break;
        case MSG_FILE_OFFER:
            handleFileOffer(clientFd, header, body, bodyLen);
            break;
        case MSG_FILE_OFFER_RSP:
            handleFileOfferResponse(clientFd, header, body, bodyLen);
            break;
        case MSG_FILE_DATA:
        case MSG_FILE_DATA_ACK:
            handleFileData(clientFd, header, body, bodyLen);
            break;
        default:
            std::cout << "[unknown] msgType=" << header.msgType
                      << " fd=" << clientFd << std::endl;
            break;
    }
}

void Server::handleChatMessage(int clientFd, const MessageHeader& header,
                               const uint8_t* body, size_t bodyLen) {
    ChatMessage msg;
    if (!ProtocolParser::parseChatMessage(body, bodyLen, msg)) {
        std::cerr << "invalid chat message length=" << bodyLen
                  << " fd=" << clientFd << std::endl;
        return;
    }

    ClientInfo sender;
    if (!clientMgr_ || !clientMgr_->getClientInfo(clientFd, sender) || !sender.isOnline) {
        std::cerr << "chat from unknown client fd=" << clientFd << std::endl;
        return;
    }

    std::string toId(msg.toId, boundedStrnlen(msg.toId, sizeof(msg.toId)));
    std::string text(msg.message, boundedStrnlen(msg.message, sizeof(msg.message)));
    uint64_t timestamp = msg.timestamp == 0 ? currentEpochSeconds() : msg.timestamp;

    ChatScope scope = (msg.chatType == CHAT_PRIVATE) ? CHAT_PRIVATE : CHAT_GROUP;

    auto packet = ProtocolParser::packChatMessage(
        header.sequence,
        scope,
        sender.clientId,
        sender.nickname,
        toId,
        text,
        timestamp);

    if (scope == CHAT_GROUP) {
        auto targets = clientMgr_->getOnlineClients();
        for (const auto& target : targets) {
            if (target.fd == clientFd) {
                continue;
            }
            sendResponse(target.fd, packet);
        }
        return;
    }

    if (toId.empty()) {
        std::cerr << "private chat missing target fd=" << clientFd << std::endl;
        return;
    }

    int targetFd = clientMgr_->getFdByClientId(toId);
    if (targetFd < 0) {
        std::cerr << "private chat target offline id=" << toId
                  << " fd=" << clientFd << std::endl;
        return;
    }

    sendResponse(targetFd, packet);
}

void Server::handleUserListRequest(int clientFd, const MessageHeader& header) {
    if (!clientMgr_) {
        return;
    }

    ClientInfo info;
    if (!clientMgr_->getClientInfo(clientFd, info) || !info.isOnline) {
        return;
    }

    sendUserList(clientFd, header.sequence);
}

void Server::handleFileOffer(int clientFd, const MessageHeader& header,
                             const uint8_t* body, size_t bodyLen) {
    FileOffer offer;
    if (!ProtocolParser::parseFileOffer(body, bodyLen, offer)) {
        std::cerr << "invalid file offer length=" << bodyLen
                  << " fd=" << clientFd << std::endl;
        return;
    }

    ClientInfo sender;
    if (!clientMgr_ || !clientMgr_->getClientInfo(clientFd, sender) || !sender.isOnline) {
        std::cerr << "file offer from unknown client fd=" << clientFd << std::endl;
        return;
    }

    std::string fileId(offer.fileId, boundedStrnlen(offer.fileId, sizeof(offer.fileId)));
    std::string fileName(offer.fileName, boundedStrnlen(offer.fileName, sizeof(offer.fileName)));
    std::string toId(offer.toId, boundedStrnlen(offer.toId, sizeof(offer.toId)));

    if (fileId.empty()) {
        auto response = ProtocolParser::packFileOfferResponse(
            header.sequence, "", FILE_OFFER_DECLINE, "Invalid file id");
        sendResponse(clientFd, response);
        return;
    }

    if (toId.empty()) {
        auto response = ProtocolParser::packFileOfferResponse(
            header.sequence, fileId, FILE_OFFER_DECLINE, "Target required");
        sendResponse(clientFd, response);
        return;
    }

    int targetFd = -1;
    if (!toId.empty()) {
        targetFd = clientMgr_->getFdByClientId(toId);
        if (targetFd < 0) {
            auto response = ProtocolParser::packFileOfferResponse(
                header.sequence, fileId, FILE_OFFER_BUSY, "Target offline");
            sendResponse(clientFd, response);
            return;
        }
    }

    auto packet = ProtocolParser::packFileOffer(
        header.sequence,
        fileId,
        fileName,
        offer.fileSize,
        sender.clientId,
        sender.nickname,
        toId);

    if (targetFd >= 0) {
        sendResponse(targetFd, packet);
    } else {
        auto targets = clientMgr_->getOnlineClients();
        bool sent = false;
        for (const auto& target : targets) {
            if (target.fd == clientFd) {
                continue;
            }
            if (sendResponse(target.fd, packet)) {
                sent = true;
            }
        }
        if (!sent) {
            auto response = ProtocolParser::packFileOfferResponse(
                header.sequence, fileId, FILE_OFFER_BUSY, "No recipients online");
            sendResponse(clientFd, response);
            return;
        }
    }

    {
        std::lock_guard<std::mutex> lock(fileMutex_);
        fileSessions_[fileId] = FileSession{clientFd, targetFd};
    }
}

void Server::handleFileOfferResponse(int clientFd, const MessageHeader& header,
                                     const uint8_t* body, size_t bodyLen) {
    FileOfferResponse rsp;
    if (!ProtocolParser::parseFileOfferResponse(body, bodyLen, rsp)) {
        std::cerr << "invalid file offer response length=" << bodyLen
                  << " fd=" << clientFd << std::endl;
        return;
    }

    std::string fileId(rsp.fileId, boundedStrnlen(rsp.fileId, sizeof(rsp.fileId)));
    if (fileId.empty()) {
        std::cerr << "file offer response missing fileId fd=" << clientFd << std::endl;
        return;
    }

    FileSession session;
    bool eraseSession = false;
    {
        std::lock_guard<std::mutex> lock(fileMutex_);
        auto it = fileSessions_.find(fileId);
        if (it == fileSessions_.end()) {
            std::cerr << "file offer response for unknown fileId=" << fileId << std::endl;
            return;
        }
        session = it->second;

        if (session.receiverFd != -1 && session.receiverFd != clientFd) {
            std::cerr << "file offer response from unexpected fd=" << clientFd
                      << " fileId=" << fileId << std::endl;
            return;
        }

        if (session.receiverFd == -1 && rsp.result == FILE_OFFER_ACCEPT) {
            it->second.receiverFd = clientFd;
            session.receiverFd = clientFd;
        }

        if (rsp.result != FILE_OFFER_ACCEPT) {
            if (session.receiverFd == -1 || session.receiverFd == clientFd) {
                eraseSession = true;
            }
        }

        if (eraseSession) {
            fileSessions_.erase(it);
        }
    }

    if (session.senderFd < 0) {
        return;
    }

    auto response = ProtocolParser::packFileOfferResponse(
        header.sequence, fileId, rsp.result, rsp.message);
    sendResponse(session.senderFd, response);
}

void Server::handleFileData(int clientFd, const MessageHeader& header,
                            const uint8_t* body, size_t bodyLen) {
    std::string fileId = extractFileId(body, bodyLen);
    if (fileId.empty()) {
        std::cerr << "file data missing fileId fd=" << clientFd << std::endl;
        return;
    }

    int targetFd = -1;
    {
        std::lock_guard<std::mutex> lock(fileMutex_);
        auto it = fileSessions_.find(fileId);
        if (it == fileSessions_.end()) {
            std::cerr << "file data unknown fileId=" << fileId << std::endl;
            return;
        }
        if (clientFd == it->second.senderFd) {
            targetFd = it->second.receiverFd;
        } else if (clientFd == it->second.receiverFd) {
            targetFd = it->second.senderFd;
        } else {
            std::cerr << "file data fd mismatch fileId=" << fileId << std::endl;
            return;
        }
    }

    if (targetFd < 0) {
        std::cerr << "file data target not ready fileId=" << fileId << std::endl;
        return;
    }

    auto packet = ProtocolParser::packRawMessage(
        static_cast<uint16_t>(header.msgType),
        header.sequence,
        body,
        bodyLen);
    sendResponse(targetFd, packet);
}

void Server::handleClientDisconnect(int clientFd) {
    if (clientFd < 0) {
        return;
    }

    if (reactor_) {
        reactor_->removeHandler(clientFd);
    }

    auto handlerIt = clientHandlers_.find(clientFd);
    if (handlerIt != clientHandlers_.end()) {
        clientHandlers_.erase(handlerIt);
    }
    if (clientMgr_) {
        clientMgr_->removeClient(clientFd);
    }
    if (protocol_) {
        protocol_->removeClient(clientFd);
    }
    cleanupFileSessionsForFd(clientFd);
    close(clientFd);

    if (running_) {
        broadcastUserList();
    }
}

void Server::queueDisconnect(int clientFd) {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    pendingDisconnects_.push_back(clientFd);
}

void Server::processPendingDisconnects() {
    std::vector<int> pending;
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        if (pendingDisconnects_.empty()) {
            return;
        }
        pending.swap(pendingDisconnects_);
    }

    std::sort(pending.begin(), pending.end());
    pending.erase(std::unique(pending.begin(), pending.end()), pending.end());

    for (int fd : pending) {
        handleClientDisconnect(fd);
    }
}

void Server::cleanupAllClients() {
    std::vector<int> fds;
    if (clientMgr_) {
        fds = clientMgr_->getAllFds();
    } else {
        for (const auto& pair : clientHandlers_) {
            fds.push_back(pair.first);
        }
    }

    for (int fd : fds) {
        handleClientDisconnect(fd);
    }
    clientHandlers_.clear();
}

void Server::sendUserList(int clientFd, uint32_t sequence) {
    if (!clientMgr_) {
        return;
    }

    auto clients = clientMgr_->getOnlineClients();
    std::vector<UserInfo> users;
    users.reserve(clients.size());

    for (const auto& info : clients) {
        UserInfo user;
        std::memset(&user, 0, sizeof(user));
        std::strncpy(user.clientId, info.clientId.c_str(), sizeof(user.clientId) - 1);
        std::strncpy(user.nickname, info.nickname.c_str(), sizeof(user.nickname) - 1);
        users.push_back(user);
    }

    auto packet = ProtocolParser::packUserListResponse(sequence, users);
    sendResponse(clientFd, packet);
}

void Server::broadcastUserList() {
    if (!clientMgr_) {
        return;
    }

    auto clients = clientMgr_->getOnlineClients();
    if (clients.empty()) {
        return;
    }

    std::vector<UserInfo> users;
    users.reserve(clients.size());

    for (const auto& info : clients) {
        UserInfo user;
        std::memset(&user, 0, sizeof(user));
        std::strncpy(user.clientId, info.clientId.c_str(), sizeof(user.clientId) - 1);
        std::strncpy(user.nickname, info.nickname.c_str(), sizeof(user.nickname) - 1);
        users.push_back(user);
    }

    auto packet = ProtocolParser::packUserListResponse(0, users);
    for (const auto& info : clients) {
        sendResponse(info.fd, packet);
    }
}

bool Server::sendResponse(int clientFd, const std::vector<uint8_t>& data) {
    auto it = clientHandlers_.find(clientFd);
    if (it == clientHandlers_.end()) {
        return false;
    }

    if (!it->second->queueSend(data)) {
        queueDisconnect(clientFd);
        return false;
    }

    return true;
}

void Server::cleanupFileSessionsForFd(int clientFd) {
    std::lock_guard<std::mutex> lock(fileMutex_);
    for (auto it = fileSessions_.begin(); it != fileSessions_.end();) {
        if (it->second.senderFd == clientFd || it->second.receiverFd == clientFd) {
            it = fileSessions_.erase(it);
        } else {
            ++it;
        }
    }
}

void Server::heartbeatLoop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(kHeartbeatIntervalSec));
        if (!running_) {
            break;
        }

        auto timeoutClients = clientMgr_->checkTimeout(kHeartbeatTimeoutSec);
        for (int fd : timeoutClients) {
            std::cout << "[heartbeat timeout] fd=" << fd << std::endl;
            queueDisconnect(fd);
        }

        std::cout << "[status] online clients: " << clientMgr_->getOnlineCount() << std::endl;
    }
}
