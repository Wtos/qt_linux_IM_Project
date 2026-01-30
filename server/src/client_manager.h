#ifndef CLIENT_MANAGER_H
#define CLIENT_MANAGER_H

#include <map>
#include <vector>
#include <mutex>
#include <string>
#include <chrono>

struct ClientInfo {
    int fd;
    std::string clientId;
    std::string nickname;
    std::string ip;
    int port;
    std::chrono::steady_clock::time_point lastHeartbeat;
    bool isOnline;

    ClientInfo() : fd(-1), port(0), isOnline(false) {}
};

class ClientManager {
public:
    ClientManager();
    ~ClientManager();

    bool addClient(int fd, const std::string& ip, int port);
    void removeClient(int fd);
    void updateHeartbeat(int fd);
    std::vector<int> checkTimeout(int timeoutSeconds);
    ClientInfo* getClient(int fd);
    bool getClientInfo(int fd, ClientInfo& out) const;
    bool setClientIdentity(int fd, const std::string& clientId, const std::string& nickname);
    bool isClientIdOnline(const std::string& clientId, int excludeFd) const;
    bool isNicknameOnline(const std::string& nickname, int excludeFd) const;
    int getFdByClientId(const std::string& clientId) const;
    std::vector<ClientInfo> getOnlineClients() const;
    bool hasClient(int fd) const;
    bool isTimedOut(int fd, int timeoutSeconds) const;
    std::vector<int> getAllFds() const;
    size_t getOnlineCount() const;
    void printClients() const;

private:
    std::map<int, ClientInfo> clients_;
    mutable std::mutex mutex_;
};

#endif
