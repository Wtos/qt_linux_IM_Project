#ifndef REACTOR_H
#define REACTOR_H

#include <cstdint>
#include <memory>
#include <unordered_map>

class EpollWrapper;

constexpr uint32_t EVENT_READ = 1u << 0;
constexpr uint32_t EVENT_WRITE = 1u << 1;
constexpr uint32_t EVENT_ERROR = 1u << 2;
constexpr uint32_t EVENT_HUP = 1u << 3;

class EventHandler {
public:
    virtual ~EventHandler() = default;
    virtual int getHandle() const = 0;
    virtual void handleRead() = 0;
    virtual void handleWrite() = 0;
    virtual void handleError(uint32_t events) = 0;
};

class Reactor {
public:
    explicit Reactor(int maxEvents = 1024);
    ~Reactor();

    bool init();
    bool registerHandler(EventHandler* handler, uint32_t events);
    bool modifyHandler(EventHandler* handler, uint32_t events);
    void removeHandler(int fd);
    void removeHandler(EventHandler* handler);
    EventHandler* findHandler(int fd);

    int poll(int timeoutMs);

private:
    struct HandlerEntry {
        EventHandler* handler;
        uint32_t events;
    };

    std::unique_ptr<EpollWrapper> epoll_;
    std::unordered_map<int, HandlerEntry> handlers_;
};

#endif
