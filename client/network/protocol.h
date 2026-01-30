#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#pragma pack(push, 1)
struct MessageHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t msgType;
    uint32_t bodyLength;
    uint32_t sequence;
};

struct LoginRequest {
    char clientId[32];
    char nickname[64];
};

struct LoginResponse {
    uint32_t result;
    char message[128];
};

struct ChatMessage {
    uint8_t chatType;
    char fromId[32];
    char fromNick[64];
    char toId[32];
    uint64_t timestamp;
    char message[256];
};

struct UserInfo {
    char clientId[32];
    char nickname[64];
};

struct FileOffer {
    char fileId[37];
    char fromId[32];
    char fromNick[64];
    char toId[32];
    uint64_t fileSize;
    char fileName[256];
};

struct FileOfferResponse {
    char fileId[37];
    uint32_t result;
    char message[64];
};

struct FileDataHeader {
    char fileId[37];
    uint64_t offset;
    uint32_t chunkSize;
};
#pragma pack(pop)

enum MessageType : uint16_t {
    MSG_HEARTBEAT_REQ = 0x0001,
    MSG_HEARTBEAT_RSP = 0x0002,
    MSG_LOGIN_REQ = 0x0101,
    MSG_LOGIN_RSP = 0x0102,
    MSG_LOGOUT_REQ = 0x0103,
    MSG_CHAT_MSG = 0x0201,
    MSG_USER_LIST_REQ = 0x0202,
    MSG_USER_LIST_RSP = 0x0203,
    MSG_FILE_OFFER = 0x0301,
    MSG_FILE_OFFER_RSP = 0x0302,
    MSG_FILE_DATA = 0x0303,
    MSG_FILE_DATA_ACK = 0x0304
};

enum LoginResult : uint32_t {
    LOGIN_SUCCESS = 0,
    LOGIN_INVALID_PARAM = 1,
    LOGIN_SERVER_FULL = 2,
    LOGIN_ALREADY_ONLINE = 3,
    LOGIN_NICKNAME_TAKEN = 4
};

enum ChatScope : uint8_t {
    CHAT_GROUP = 0,
    CHAT_PRIVATE = 1
};

enum FileOfferResult : uint32_t {
    FILE_OFFER_ACCEPT = 0,
    FILE_OFFER_DECLINE = 1,
    FILE_OFFER_BUSY = 2
};

class ProtocolParser {
public:
    static bool validateHeader(const MessageHeader &header);
    static std::vector<uint8_t> packHeartbeatRequest(uint32_t sequence);
    static std::vector<uint8_t> packLoginRequest(uint32_t sequence,
                                                  const std::string &clientId,
                                                  const std::string &nickname);
    static std::vector<uint8_t> packLogoutRequest(uint32_t sequence);
    static std::vector<uint8_t> packChatMessage(uint32_t sequence,
                                                ChatScope scope,
                                                const std::string &fromId,
                                                const std::string &fromNick,
                                                const std::string &toId,
                                                const std::string &message,
                                                uint64_t timestamp);
    static std::vector<uint8_t> packUserListRequest(uint32_t sequence);
    static std::vector<uint8_t> packFileOffer(uint32_t sequence,
                                              const std::string &fileId,
                                              const std::string &fileName,
                                              uint64_t fileSize,
                                              const std::string &fromId,
                                              const std::string &fromNick,
                                              const std::string &toId);
    static std::vector<uint8_t> packFileOfferResponse(uint32_t sequence,
                                                      const std::string &fileId,
                                                      uint32_t result,
                                                      const std::string &message);
    static std::vector<uint8_t> packFileData(uint32_t sequence,
                                             const std::string &fileId,
                                             uint64_t offset,
                                             const uint8_t *data,
                                             size_t dataLen);

    static bool parseLoginResponse(const uint8_t *data, size_t len, LoginResponse &rsp);
    static bool parseChatMessage(const uint8_t *data, size_t len, ChatMessage &msg);
    static bool parseUserListResponse(const uint8_t *data,
                                      size_t len,
                                      std::vector<UserInfo> &users);
    static bool parseFileOffer(const uint8_t *data, size_t len, FileOffer &offer);
    static bool parseFileOfferResponse(const uint8_t *data,
                                       size_t len,
                                       FileOfferResponse &rsp);
    static bool parseFileData(const uint8_t *data,
                              size_t len,
                              FileDataHeader &header,
                              const uint8_t *&payload,
                              size_t &payloadLen);

private:
    static constexpr uint32_t MAGIC_NUMBER = 0x12345678;
    static constexpr uint16_t PROTOCOL_VERSION = 0x0001;
};

static_assert(sizeof(MessageHeader) == 16, "MessageHeader size mismatch");

#endif // PROTOCOL_H
