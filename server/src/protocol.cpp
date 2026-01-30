#include "protocol.h"

#include <vector>

namespace {
uint64_t swap64(uint64_t value) {
    return (static_cast<uint64_t>(htonl(static_cast<uint32_t>(value & 0xFFFFFFFFULL))) << 32)
        | htonl(static_cast<uint32_t>(value >> 32));
}

uint64_t hostToNetwork64(uint64_t value) {
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    return value;
#else
    return swap64(value);
#endif
}

uint64_t networkToHost64(uint64_t value) {
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    return value;
#else
    return swap64(value);
#endif
}
} // namespace

ProtocolParser::ProtocolParser() = default;
ProtocolParser::~ProtocolParser() = default;

void ProtocolParser::parseData(int fd, const uint8_t* data, size_t len, MessageCallback callback) {
    auto& buffer = recvBuffers_[fd];
    buffer.insert(buffer.end(), data, data + len);

    while (buffer.size() >= sizeof(MessageHeader)) {
        MessageHeader header;
        std::memcpy(&header, buffer.data(), sizeof(MessageHeader));

        header.magic = ntohl(header.magic);
        header.version = ntohs(header.version);
        header.msgType = ntohs(header.msgType);
        header.bodyLength = ntohl(header.bodyLength);
        header.sequence = ntohl(header.sequence);

        if (!validateHeader(header)) {
            buffer.clear();
            break;
        }

        size_t totalLen = sizeof(MessageHeader) + static_cast<size_t>(header.bodyLength);
        if (buffer.size() < totalLen) {
            break;
        }

        const uint8_t* body = buffer.data() + sizeof(MessageHeader);
        callback(fd, header, body, static_cast<size_t>(header.bodyLength));

        buffer.erase(buffer.begin(),
                     buffer.begin() + static_cast<std::vector<uint8_t>::difference_type>(totalLen));
    }
}

bool ProtocolParser::validateHeader(const MessageHeader& header) {
    if (header.magic != MAGIC_NUMBER) {
        return false;
    }
    if (header.version != PROTOCOL_VERSION) {
        return false;
    }
    if (header.bodyLength > 1024 * 1024) {
        return false;
    }
    return true;
}

std::vector<uint8_t> ProtocolParser::packHeartbeatResponse(uint32_t sequence) {
    std::vector<uint8_t> buffer(sizeof(MessageHeader));

    MessageHeader header;
    header.magic = htonl(MAGIC_NUMBER);
    header.version = htons(PROTOCOL_VERSION);
    header.msgType = htons(MSG_HEARTBEAT_RSP);
    header.bodyLength = htonl(0);
    header.sequence = htonl(sequence);

    std::memcpy(buffer.data(), &header, sizeof(MessageHeader));
    return buffer;
}

std::vector<uint8_t> ProtocolParser::packLoginResponse(uint32_t sequence,
                                                       uint32_t result,
                                                       const std::string& message) {
    std::vector<uint8_t> buffer(sizeof(MessageHeader) + sizeof(LoginResponse));

    MessageHeader header;
    header.magic = htonl(MAGIC_NUMBER);
    header.version = htons(PROTOCOL_VERSION);
    header.msgType = htons(MSG_LOGIN_RSP);
    header.bodyLength = htonl(static_cast<uint32_t>(sizeof(LoginResponse)));
    header.sequence = htonl(sequence);

    LoginResponse rsp;
    rsp.result = htonl(result);
    std::memset(rsp.message, 0, sizeof(rsp.message));
    std::strncpy(rsp.message, message.c_str(), sizeof(rsp.message) - 1);

    std::memcpy(buffer.data(), &header, sizeof(MessageHeader));
    std::memcpy(buffer.data() + sizeof(MessageHeader), &rsp, sizeof(LoginResponse));

    return buffer;
}

std::vector<uint8_t> ProtocolParser::packChatMessage(uint32_t sequence,
                                                     ChatScope scope,
                                                     const std::string& fromId,
                                                     const std::string& fromNick,
                                                     const std::string& toId,
                                                     const std::string& message,
                                                     uint64_t timestamp) {
    std::vector<uint8_t> buffer(sizeof(MessageHeader) + sizeof(ChatMessage));

    MessageHeader header;
    header.magic = htonl(MAGIC_NUMBER);
    header.version = htons(PROTOCOL_VERSION);
    header.msgType = htons(MSG_CHAT_MSG);
    header.bodyLength = htonl(static_cast<uint32_t>(sizeof(ChatMessage)));
    header.sequence = htonl(sequence);

    ChatMessage msg;
    std::memset(&msg, 0, sizeof(msg));
    msg.chatType = static_cast<uint8_t>(scope);
    std::strncpy(msg.fromId, fromId.c_str(), sizeof(msg.fromId) - 1);
    std::strncpy(msg.fromNick, fromNick.c_str(), sizeof(msg.fromNick) - 1);
    std::strncpy(msg.toId, toId.c_str(), sizeof(msg.toId) - 1);
    msg.timestamp = hostToNetwork64(timestamp);
    std::strncpy(msg.message, message.c_str(), sizeof(msg.message) - 1);

    std::memcpy(buffer.data(), &header, sizeof(MessageHeader));
    std::memcpy(buffer.data() + sizeof(MessageHeader), &msg, sizeof(ChatMessage));

    return buffer;
}

std::vector<uint8_t> ProtocolParser::packUserListResponse(uint32_t sequence,
                                                          const std::vector<UserInfo>& users) {
    uint32_t count = static_cast<uint32_t>(users.size());
    size_t bodyLen = sizeof(uint32_t) + users.size() * sizeof(UserInfo);

    std::vector<uint8_t> buffer(sizeof(MessageHeader) + bodyLen);

    MessageHeader header;
    header.magic = htonl(MAGIC_NUMBER);
    header.version = htons(PROTOCOL_VERSION);
    header.msgType = htons(MSG_USER_LIST_RSP);
    header.bodyLength = htonl(static_cast<uint32_t>(bodyLen));
    header.sequence = htonl(sequence);

    uint32_t countNet = htonl(count);
    std::memcpy(buffer.data(), &header, sizeof(MessageHeader));
    std::memcpy(buffer.data() + sizeof(MessageHeader), &countNet, sizeof(uint32_t));

    size_t offset = sizeof(MessageHeader) + sizeof(uint32_t);
    for (const auto& user : users) {
        std::memcpy(buffer.data() + offset, &user, sizeof(UserInfo));
        offset += sizeof(UserInfo);
    }

    return buffer;
}

std::vector<uint8_t> ProtocolParser::packFileOffer(uint32_t sequence,
                                                   const std::string& fileId,
                                                   const std::string& fileName,
                                                   uint64_t fileSize,
                                                   const std::string& fromId,
                                                   const std::string& fromNick,
                                                   const std::string& toId) {
    std::vector<uint8_t> buffer(sizeof(MessageHeader) + sizeof(FileOffer));

    MessageHeader header;
    header.magic = htonl(MAGIC_NUMBER);
    header.version = htons(PROTOCOL_VERSION);
    header.msgType = htons(MSG_FILE_OFFER);
    header.bodyLength = htonl(static_cast<uint32_t>(sizeof(FileOffer)));
    header.sequence = htonl(sequence);

    FileOffer offer;
    std::memset(&offer, 0, sizeof(offer));
    std::strncpy(offer.fileId, fileId.c_str(), sizeof(offer.fileId) - 1);
    std::strncpy(offer.fromId, fromId.c_str(), sizeof(offer.fromId) - 1);
    std::strncpy(offer.fromNick, fromNick.c_str(), sizeof(offer.fromNick) - 1);
    std::strncpy(offer.toId, toId.c_str(), sizeof(offer.toId) - 1);
    offer.fileSize = hostToNetwork64(fileSize);
    std::strncpy(offer.fileName, fileName.c_str(), sizeof(offer.fileName) - 1);

    std::memcpy(buffer.data(), &header, sizeof(MessageHeader));
    std::memcpy(buffer.data() + sizeof(MessageHeader), &offer, sizeof(FileOffer));

    return buffer;
}

std::vector<uint8_t> ProtocolParser::packFileOfferResponse(uint32_t sequence,
                                                           const std::string& fileId,
                                                           uint32_t result,
                                                           const std::string& message) {
    std::vector<uint8_t> buffer(sizeof(MessageHeader) + sizeof(FileOfferResponse));

    MessageHeader header;
    header.magic = htonl(MAGIC_NUMBER);
    header.version = htons(PROTOCOL_VERSION);
    header.msgType = htons(MSG_FILE_OFFER_RSP);
    header.bodyLength = htonl(static_cast<uint32_t>(sizeof(FileOfferResponse)));
    header.sequence = htonl(sequence);

    FileOfferResponse rsp;
    std::memset(&rsp, 0, sizeof(rsp));
    std::strncpy(rsp.fileId, fileId.c_str(), sizeof(rsp.fileId) - 1);
    rsp.result = htonl(result);
    std::strncpy(rsp.message, message.c_str(), sizeof(rsp.message) - 1);

    std::memcpy(buffer.data(), &header, sizeof(MessageHeader));
    std::memcpy(buffer.data() + sizeof(MessageHeader), &rsp, sizeof(FileOfferResponse));

    return buffer;
}

std::vector<uint8_t> ProtocolParser::packRawMessage(uint16_t msgType,
                                                    uint32_t sequence,
                                                    const uint8_t* body,
                                                    size_t bodyLen) {
    std::vector<uint8_t> buffer(sizeof(MessageHeader) + bodyLen);

    MessageHeader header;
    header.magic = htonl(MAGIC_NUMBER);
    header.version = htons(PROTOCOL_VERSION);
    header.msgType = htons(msgType);
    header.bodyLength = htonl(static_cast<uint32_t>(bodyLen));
    header.sequence = htonl(sequence);

    std::memcpy(buffer.data(), &header, sizeof(MessageHeader));
    if (bodyLen > 0 && body) {
        std::memcpy(buffer.data() + sizeof(MessageHeader), body, bodyLen);
    }

    return buffer;
}

bool ProtocolParser::parseLoginRequest(const uint8_t* data, size_t len, LoginRequest& req) {
    if (len < sizeof(LoginRequest)) {
        return false;
    }

    std::memcpy(&req, data, sizeof(LoginRequest));
    req.clientId[sizeof(req.clientId) - 1] = '\0';
    req.nickname[sizeof(req.nickname) - 1] = '\0';
    return true;
}

bool ProtocolParser::parseChatMessage(const uint8_t* data, size_t len, ChatMessage& msg) {
    if (len < sizeof(ChatMessage)) {
        return false;
    }

    std::memcpy(&msg, data, sizeof(ChatMessage));
    msg.fromId[sizeof(msg.fromId) - 1] = '\0';
    msg.fromNick[sizeof(msg.fromNick) - 1] = '\0';
    msg.toId[sizeof(msg.toId) - 1] = '\0';
    msg.message[sizeof(msg.message) - 1] = '\0';
    msg.timestamp = networkToHost64(msg.timestamp);

    return true;
}

bool ProtocolParser::parseFileOffer(const uint8_t* data, size_t len, FileOffer& offer) {
    if (len < sizeof(FileOffer)) {
        return false;
    }

    std::memcpy(&offer, data, sizeof(FileOffer));
    offer.fileId[sizeof(offer.fileId) - 1] = '\0';
    offer.fromId[sizeof(offer.fromId) - 1] = '\0';
    offer.fromNick[sizeof(offer.fromNick) - 1] = '\0';
    offer.toId[sizeof(offer.toId) - 1] = '\0';
    offer.fileName[sizeof(offer.fileName) - 1] = '\0';
    offer.fileSize = networkToHost64(offer.fileSize);

    return true;
}

bool ProtocolParser::parseFileOfferResponse(const uint8_t* data, size_t len, FileOfferResponse& rsp) {
    if (len < sizeof(FileOfferResponse)) {
        return false;
    }

    std::memcpy(&rsp, data, sizeof(FileOfferResponse));
    rsp.fileId[sizeof(rsp.fileId) - 1] = '\0';
    rsp.result = ntohl(rsp.result);
    rsp.message[sizeof(rsp.message) - 1] = '\0';

    return true;
}

void ProtocolParser::removeClient(int fd) {
    recvBuffers_.erase(fd);
}
