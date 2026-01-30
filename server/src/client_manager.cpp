#include "client_manager.h"

#include <iostream>

ClientManager::ClientManager() = default;
ClientManager::~ClientManager() = default;

bool ClientManager::addClient(int fd, const std::string& ip, int port) {
    std::lock_guard<std::mutex> lock(mutex_);

    ClientInfo info;
    info.fd = fd;
    info.ip = ip;
    info.port = port;
    info.lastHeartbeat = std::chrono::steady_clock::now();
    info.isOnline = false;

    clients_[fd] = info;
    return true;
}

void ClientManager::removeClient(int fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    clients_.erase(fd);
}

void ClientManager::updateHeartbeat(int fd) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = clients_.find(fd);
    if (it != clients_.end()) {
        it->second.lastHeartbeat = std::chrono::steady_clock::now();
    }
}

std::vector<int> ClientManager::checkTimeout(int timeoutSeconds) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<int> timeoutFds;
    auto now = std::chrono::steady_clock::now();

    for (const auto& pair : clients_) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - pair.second.lastHeartbeat).count();

        if (elapsed > timeoutSeconds) {
            timeoutFds.push_back(pair.first);
        }
    }

    return timeoutFds;
}

ClientInfo* ClientManager::getClient(int fd) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = clients_.find(fd);
    if (it != clients_.end()) {
        return &it->second;
    }
    return nullptr;
}

bool ClientManager::getClientInfo(int fd, ClientInfo& out) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = clients_.find(fd);
    if (it == clients_.end()) {
        return false;
    }
    out = it->second;
    return true;
}

bool ClientManager::setClientIdentity(int fd, const std::string& clientId,
                                      const std::string& nickname) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = clients_.find(fd);
    if (it == clients_.end()) {
        return false;
    }
    it->second.clientId = clientId;
    it->second.nickname = nickname;
    it->second.isOnline = true;
    return true;
}

bool ClientManager::isClientIdOnline(const std::string& clientId, int excludeFd) const {
    if (clientId.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& pair : clients_) {
        if (pair.first == excludeFd) {
            continue;
        }
        const ClientInfo& info = pair.second;
        if (!info.isOnline) {
            continue;
        }
        if (info.clientId == clientId) {
            return true;
        }
    }
    return false;
}

bool ClientManager::isNicknameOnline(const std::string& nickname, int excludeFd) const {
    if (nickname.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& pair : clients_) {
        if (pair.first == excludeFd) {
            continue;
        }
        const ClientInfo& info = pair.second;
        if (!info.isOnline) {
            continue;
        }
        if (info.nickname == nickname) {
            return true;
        }
    }
    return false;
}

int ClientManager::getFdByClientId(const std::string& clientId) const {
    if (clientId.empty()) {
        return -1;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& pair : clients_) {
        const ClientInfo& info = pair.second;
        if (!info.isOnline) {
            continue;
        }
        if (info.clientId == clientId) {
            return pair.first;
        }
    }
    return -1;
}

std::vector<ClientInfo> ClientManager::getOnlineClients() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<ClientInfo> clients;
    for (const auto& pair : clients_) {
        const ClientInfo& info = pair.second;
        if (info.isOnline) {
            clients.push_back(info);
        }
    }
    return clients;
}

bool ClientManager::hasClient(int fd) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return clients_.find(fd) != clients_.end();
}

bool ClientManager::isTimedOut(int fd, int timeoutSeconds) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = clients_.find(fd);
    if (it == clients_.end()) {
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - it->second.lastHeartbeat).count();
    return elapsed > timeoutSeconds;
}

std::vector<int> ClientManager::getAllFds() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<int> fds;
    fds.reserve(clients_.size());
    for (const auto& pair : clients_) {
        fds.push_back(pair.first);
    }
    return fds;
}

size_t ClientManager::getOnlineCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t count = 0;
    for (const auto& pair : clients_) {
        if (pair.second.isOnline) {
            ++count;
        }
    }
    return count;
}

void ClientManager::printClients() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::cout << "[clients] total=" << clients_.size() << std::endl;
    for (const auto& pair : clients_) {
        const ClientInfo& info = pair.second;
        std::cout << "  fd=" << info.fd << " ip=" << info.ip << ":" << info.port
                  << " id=" << info.clientId << " nick=" << info.nickname << std::endl;
    }
}
