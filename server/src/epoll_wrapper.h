#ifndef EPOLL_WRAPPER_H
#define EPOLL_WRAPPER_H

#include <sys/epoll.h>
#include <vector>
#include <unistd.h>

class EpollWrapper {
public:
    explicit EpollWrapper(int maxEvents = 1024);
    ~EpollWrapper();

    bool create();
    bool addFd(int fd, uint32_t events = EPOLLIN | EPOLLET);
    bool modifyFd(int fd, uint32_t events);
    bool removeFd(int fd);
    int wait(int timeoutMs = -1);

    const epoll_event* getEvents() const {
        return events_.data();
    }

private:
    int epollFd_;
    int maxEvents_;
    std::vector<epoll_event> events_;
};

#endif
