# IM Server (Linux)

A simple TCP server built with C++14, epoll, and pthreads. It supports
multiple concurrent clients, heartbeat timeout, and basic login/heartbeat
messages.

## Build

```bash
mkdir build
cd build
cmake ..
make -j4
```

## Run

```bash
./im_server 8888 0.0.0.0
```

If IP is omitted, it defaults to 0.0.0.0. If port is omitted, it defaults
to 8888.

## Protocol Summary

- MessageHeader is 16 bytes.
- Magic: 0x12345678
- Version: 0x0001
- Heartbeat request/response and login request/response are supported.

## Notes

- Use Ctrl+C to stop the server.
- Heartbeat timeout is 10 seconds.
