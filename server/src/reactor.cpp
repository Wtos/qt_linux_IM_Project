#include "reactor.h"

#include "epoll_wrapper.h"

#include <sys/epoll.h>

#include <cerrno>

namespace {
constexpr bool kUseEdgeTriggered = true;

uint32_t toEpollEvents(uint32_t events) {
    uint32_t ep = 0;
    if (events & EVENT_READ) {
        ep |= EPOLLIN;
#ifdef EPOLLRDHUP
        ep |= EPOLLRDHUP;
#endif
    }
    if (events & EVENT_WRITE) {
        ep |= EPOLLOUT;
    }
    if (events & EVENT_ERROR) {
        ep |= EPOLLERR;
    }
    if (events & EVENT_HUP) {
        ep |= EPOLLHUP;
    }
    if (kUseEdgeTriggered) {
        ep |= EPOLLET;
    }
    return ep;
}

uint32_t fromEpollEvents(uint32_t events) {
    uint32_t ev = 0;
    if (events & EPOLLIN) {
        ev |= EVENT_READ;
    }
    if (events & EPOLLOUT) {
        ev |= EVENT_WRITE;
    }
    if (events & EPOLLERR) {
        ev |= EVENT_ERROR;
    }
    if (events & EPOLLHUP) {
        ev |= EVENT_HUP;
    }
#ifdef EPOLLRDHUP
    if (events & EPOLLRDHUP) {
        ev |= EVENT_HUP;
    }
#endif
    return ev;
}
}  // namespace

Reactor::Reactor(int maxEvents)
    : epoll_(std::make_unique<EpollWrapper>(maxEvents)) {}

Reactor::~Reactor() = default;

bool Reactor::init() {
    return epoll_ && epoll_->create();
}

bool Reactor::registerHandler(EventHandler* handler, uint32_t events) {
    if (!handler || !epoll_) {
        return false;
    }

    int fd = handler->getHandle();
    auto it = handlers_.find(fd);
    if (it != handlers_.end()) {
        it->second.handler = handler;
        it->second.events = events;
        return epoll_->modifyFd(fd, toEpollEvents(events));
    }

    if (!epoll_->addFd(fd, toEpollEvents(events))) {
        return false;
    }

    handlers_.emplace(fd, HandlerEntry{handler, events});
    return true;
}

bool Reactor::modifyHandler(EventHandler* handler, uint32_t events) {
    if (!handler || !epoll_) {
        return false;
    }

    int fd = handler->getHandle();
    auto it = handlers_.find(fd);
    if (it == handlers_.end()) {
        return false;
    }

    it->second.events = events;
    return epoll_->modifyFd(fd, toEpollEvents(events));
}

void Reactor::removeHandler(int fd) {
    if (!epoll_) {
        return;
    }

    auto it = handlers_.find(fd);
    if (it == handlers_.end()) {
        return;
    }

    epoll_->removeFd(fd);
    handlers_.erase(it);
}

void Reactor::removeHandler(EventHandler* handler) {
    if (!handler) {
        return;
    }
    removeHandler(handler->getHandle());
}

EventHandler* Reactor::findHandler(int fd) {
    auto it = handlers_.find(fd);
    if (it == handlers_.end()) {
        return nullptr;
    }
    return it->second.handler;
}

int Reactor::poll(int timeoutMs) {
    if (!epoll_) {
        errno = EBADF;
        return -1;
    }

    while (true) {
        int nReady = epoll_->wait(timeoutMs);
        if (nReady < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (nReady == 0) {
            return 0;
        }

        const epoll_event* events = epoll_->getEvents();
        for (int i = 0; i < nReady; ++i) {
            int fd = events[i].data.fd;
            uint32_t ev = fromEpollEvents(events[i].events);

            EventHandler* handler = findHandler(fd);
            if (!handler) {
                continue;
            }

            if (ev & (EVENT_ERROR | EVENT_HUP)) {
                handler->handleError(ev);
                continue;
            }

            if (ev & EVENT_READ) {
                handler->handleRead();
            }
            if (ev & EVENT_WRITE) {
                EventHandler* afterRead = findHandler(fd);
                if (afterRead) {
                    afterRead->handleWrite();
                }
            }
        }
        return nReady;
    }
}
