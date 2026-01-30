#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>
#include <vector>
#include <string>
#include <functional>
#include <map>
#include <arpa/inet.h>
#include <cstring>

#include "common/message.h"

using MessageCallback = std::function<void(int fd, const MessageHeader& header,
                                           const uint8_t* body, size_t bodyLen)>;

class ProtocolParser {
public:
    ProtocolParser();
    ~ProtocolParser();

    void parseData(int fd, const uint8_t* data, size_t len, MessageCallback callback);

    static bool validateHeader(const MessageHeader& header);
    static std::vector<uint8_t> packHeartbeatResponse(uint32_t sequence);
    static std::vector<uint8_t> packLoginResponse(uint32_t sequence,
                                                  uint32_t result,
                                                  const std::string& message);
    static std::vector<uint8_t> packChatMessage(uint32_t sequence,
                                                ChatScope scope,
                                                const std::string& fromId,
                                                const std::string& fromNick,
                                                const std::string& toId,
                                                const std::string& message,
                                                uint64_t timestamp);
    static std::vector<uint8_t> packUserListResponse(uint32_t sequence,
                                                     const std::vector<UserInfo>& users);
    static std::vector<uint8_t> packFileOffer(uint32_t sequence,
                                              const std::string& fileId,
                                              const std::string& fileName,
                                              uint64_t fileSize,
                                              const std::string& fromId,
                                              const std::string& fromNick,
                                              const std::string& toId);
    static std::vector<uint8_t> packFileOfferResponse(uint32_t sequence,
                                                      const std::string& fileId,
                                                      uint32_t result,
                                                      const std::string& message);
    static std::vector<uint8_t> packRawMessage(uint16_t msgType,
                                               uint32_t sequence,
                                               const uint8_t* body,
                                               size_t bodyLen);
    static bool parseLoginRequest(const uint8_t* data, size_t len, LoginRequest& req);
    static bool parseChatMessage(const uint8_t* data, size_t len, ChatMessage& msg);
    static bool parseFileOffer(const uint8_t* data, size_t len, FileOffer& offer);
    static bool parseFileOfferResponse(const uint8_t* data, size_t len, FileOfferResponse& rsp);

    void removeClient(int fd);

private:
    static constexpr uint32_t MAGIC_NUMBER = 0x12345678;
    static constexpr uint16_t PROTOCOL_VERSION = 0x0001;

    std::map<int, std::vector<uint8_t>> recvBuffers_;
};

#endif
