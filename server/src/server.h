#ifndef SERVER_H
#define SERVER_H

#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <unordered_map>

#include "reactor.h"
#include "client_manager.h"
#include "protocol.h"

class ListenHandler;
class ClientHandler;

class Server {
public:
    Server(const std::string& ip, int port);
    ~Server();

    bool start();
    void stop();
    void run();

private:
    friend class ListenHandler;
    friend class ClientHandler;

    bool initListenSocket();
    bool setNonBlocking(int fd);
    bool registerClient(int clientFd, const std::string& ip, int port);
    void onClientData(int clientFd, const uint8_t* data, size_t len);
    void handleClientDisconnect(int clientFd);
    void handleMessage(int clientFd, const MessageHeader& header,
                       const uint8_t* body, size_t bodyLen);
    void handleChatMessage(int clientFd, const MessageHeader& header,
                           const uint8_t* body, size_t bodyLen);
    void handleUserListRequest(int clientFd, const MessageHeader& header);
    void handleFileOffer(int clientFd, const MessageHeader& header,
                         const uint8_t* body, size_t bodyLen);
    void handleFileOfferResponse(int clientFd, const MessageHeader& header,
                                 const uint8_t* body, size_t bodyLen);
    void handleFileData(int clientFd, const MessageHeader& header,
                        const uint8_t* body, size_t bodyLen);
    void broadcastUserList();
    void sendUserList(int clientFd, uint32_t sequence);
    void heartbeatLoop();
    void queueDisconnect(int clientFd);
    void processPendingDisconnects();
    void cleanupAllClients();
    bool sendResponse(int clientFd, const std::vector<uint8_t>& data);
    void cleanupFileSessionsForFd(int clientFd);

private:
    struct FileSession {
        int senderFd = -1;
        int receiverFd = -1;
    };

    std::string ip_;
    int port_;
    int listenFd_;
    std::atomic<bool> running_;

    std::unique_ptr<Reactor> reactor_;
    std::unique_ptr<ClientManager> clientMgr_;
    std::unique_ptr<ProtocolParser> protocol_;
    std::unique_ptr<ListenHandler> listenHandler_;
    std::unordered_map<int, std::unique_ptr<ClientHandler>> clientHandlers_;

    std::thread heartbeatThread_;
    std::mutex pendingMutex_;
    std::vector<int> pendingDisconnects_;
    std::mutex fileMutex_;
    std::unordered_map<std::string, FileSession> fileSessions_;
};

#endif
