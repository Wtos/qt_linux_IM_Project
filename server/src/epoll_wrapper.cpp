#include "epoll_wrapper.h"

#include <cerrno>

EpollWrapper::EpollWrapper(int maxEvents)
    : epollFd_(-1), maxEvents_(maxEvents), events_(static_cast<size_t>(maxEvents)) {}

EpollWrapper::~EpollWrapper() {
    if (epollFd_ >= 0) {
        close(epollFd_);
    }
}

bool EpollWrapper::create() {
    epollFd_ = epoll_create1(0);
    return epollFd_ >= 0;
}

bool EpollWrapper::addFd(int fd, uint32_t events) {
    if (epollFd_ < 0) {
        return false;
    }

    epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;

    return epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev) == 0;
}

bool EpollWrapper::modifyFd(int fd, uint32_t events) {
    if (epollFd_ < 0) {
        return false;
    }

    epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;

    return epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev) == 0;
}

bool EpollWrapper::removeFd(int fd) {
    if (epollFd_ < 0) {
        return false;
    }

    return epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr) == 0;
}

int EpollWrapper::wait(int timeoutMs) {
    if (epollFd_ < 0) {
        errno = EBADF;
        return -1;
    }

    return epoll_wait(epollFd_, events_.data(), maxEvents_, timeoutMs);
}
