#include "protocol.h"

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

bool ProtocolParser::validateHeader(const MessageHeader &header) {
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

std::vector<uint8_t> ProtocolParser::packHeartbeatRequest(uint32_t sequence) {
    std::vector<uint8_t> buffer(sizeof(MessageHeader));

    MessageHeader header;
    header.magic = htonl(MAGIC_NUMBER);
    header.version = htons(PROTOCOL_VERSION);
    header.msgType = htons(MSG_HEARTBEAT_REQ);
    header.bodyLength = htonl(0);
    header.sequence = htonl(sequence);

    std::memcpy(buffer.data(), &header, sizeof(MessageHeader));
    return buffer;
}

std::vector<uint8_t> ProtocolParser::packLoginRequest(uint32_t sequence,
                                                      const std::string &clientId,
                                                      const std::string &nickname) {
    std::vector<uint8_t> buffer(sizeof(MessageHeader) + sizeof(LoginRequest));

    MessageHeader header;
    header.magic = htonl(MAGIC_NUMBER);
    header.version = htons(PROTOCOL_VERSION);
    header.msgType = htons(MSG_LOGIN_REQ);
    header.bodyLength = htonl(static_cast<uint32_t>(sizeof(LoginRequest)));
    header.sequence = htonl(sequence);

    LoginRequest req;
    std::memset(&req, 0, sizeof(req));
    std::strncpy(req.clientId, clientId.c_str(), sizeof(req.clientId) - 1);
    std::strncpy(req.nickname, nickname.c_str(), sizeof(req.nickname) - 1);

    std::memcpy(buffer.data(), &header, sizeof(MessageHeader));
    std::memcpy(buffer.data() + sizeof(MessageHeader), &req, sizeof(LoginRequest));

    return buffer;
}

std::vector<uint8_t> ProtocolParser::packLogoutRequest(uint32_t sequence) {
    std::vector<uint8_t> buffer(sizeof(MessageHeader));

    MessageHeader header;
    header.magic = htonl(MAGIC_NUMBER);
    header.version = htons(PROTOCOL_VERSION);
    header.msgType = htons(MSG_LOGOUT_REQ);
    header.bodyLength = htonl(0);
    header.sequence = htonl(sequence);

    std::memcpy(buffer.data(), &header, sizeof(MessageHeader));
    return buffer;
}

std::vector<uint8_t> ProtocolParser::packChatMessage(uint32_t sequence,
                                                     ChatScope scope,
                                                     const std::string &fromId,
                                                     const std::string &fromNick,
                                                     const std::string &toId,
                                                     const std::string &message,
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

std::vector<uint8_t> ProtocolParser::packUserListRequest(uint32_t sequence) {
    std::vector<uint8_t> buffer(sizeof(MessageHeader));

    MessageHeader header;
    header.magic = htonl(MAGIC_NUMBER);
    header.version = htons(PROTOCOL_VERSION);
    header.msgType = htons(MSG_USER_LIST_REQ);
    header.bodyLength = htonl(0);
    header.sequence = htonl(sequence);

    std::memcpy(buffer.data(), &header, sizeof(MessageHeader));
    return buffer;
}

std::vector<uint8_t> ProtocolParser::packFileOffer(uint32_t sequence,
                                                   const std::string &fileId,
                                                   const std::string &fileName,
                                                   uint64_t fileSize,
                                                   const std::string &fromId,
                                                   const std::string &fromNick,
                                                   const std::string &toId) {
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
                                                           const std::string &fileId,
                                                           uint32_t result,
                                                           const std::string &message) {
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

std::vector<uint8_t> ProtocolParser::packFileData(uint32_t sequence,
                                                  const std::string &fileId,
                                                  uint64_t offset,
                                                  const uint8_t *data,
                                                  size_t dataLen) {
    std::vector<uint8_t> buffer(sizeof(MessageHeader) + sizeof(FileDataHeader) + dataLen);

    MessageHeader header;
    header.magic = htonl(MAGIC_NUMBER);
    header.version = htons(PROTOCOL_VERSION);
    header.msgType = htons(MSG_FILE_DATA);
    header.bodyLength = htonl(static_cast<uint32_t>(sizeof(FileDataHeader) + dataLen));
    header.sequence = htonl(sequence);

    FileDataHeader fileHeader;
    std::memset(&fileHeader, 0, sizeof(fileHeader));
    std::strncpy(fileHeader.fileId, fileId.c_str(), sizeof(fileHeader.fileId) - 1);
    fileHeader.offset = hostToNetwork64(offset);
    fileHeader.chunkSize = htonl(static_cast<uint32_t>(dataLen));

    std::memcpy(buffer.data(), &header, sizeof(MessageHeader));
    std::memcpy(buffer.data() + sizeof(MessageHeader), &fileHeader, sizeof(FileDataHeader));
    if (dataLen > 0) {
        std::memcpy(buffer.data() + sizeof(MessageHeader) + sizeof(FileDataHeader), data, dataLen);
    }

    return buffer;
}

bool ProtocolParser::parseLoginResponse(const uint8_t *data, size_t len, LoginResponse &rsp) {
    if (len < sizeof(LoginResponse)) {
        return false;
    }

    std::memcpy(&rsp, data, sizeof(LoginResponse));
    rsp.result = ntohl(rsp.result);
    rsp.message[sizeof(rsp.message) - 1] = '\0';

    return true;
}

bool ProtocolParser::parseChatMessage(const uint8_t *data, size_t len, ChatMessage &msg) {
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

bool ProtocolParser::parseUserListResponse(const uint8_t *data,
                                           size_t len,
                                           std::vector<UserInfo> &users) {
    users.clear();
    if (len < sizeof(uint32_t)) {
        return false;
    }

    uint32_t count = 0;
    std::memcpy(&count, data, sizeof(uint32_t));
    count = ntohl(count);

    size_t expected = sizeof(uint32_t) + static_cast<size_t>(count) * sizeof(UserInfo);
    if (len < expected) {
        return false;
    }

    size_t offset = sizeof(uint32_t);
    users.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        UserInfo info;
        std::memcpy(&info, data + offset, sizeof(UserInfo));
        info.clientId[sizeof(info.clientId) - 1] = '\0';
        info.nickname[sizeof(info.nickname) - 1] = '\0';
        users.push_back(info);
        offset += sizeof(UserInfo);
    }

    return true;
}

bool ProtocolParser::parseFileOffer(const uint8_t *data, size_t len, FileOffer &offer) {
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

bool ProtocolParser::parseFileOfferResponse(const uint8_t *data,
                                            size_t len,
                                            FileOfferResponse &rsp) {
    if (len < sizeof(FileOfferResponse)) {
        return false;
    }

    std::memcpy(&rsp, data, sizeof(FileOfferResponse));
    rsp.fileId[sizeof(rsp.fileId) - 1] = '\0';
    rsp.result = ntohl(rsp.result);
    rsp.message[sizeof(rsp.message) - 1] = '\0';

    return true;
}

bool ProtocolParser::parseFileData(const uint8_t *data,
                                   size_t len,
                                   FileDataHeader &header,
                                   const uint8_t *&payload,
                                   size_t &payloadLen) {
    payload = nullptr;
    payloadLen = 0;

    if (len < sizeof(FileDataHeader)) {
        return false;
    }

    std::memcpy(&header, data, sizeof(FileDataHeader));
    header.fileId[sizeof(header.fileId) - 1] = '\0';
    header.offset = networkToHost64(header.offset);
    header.chunkSize = ntohl(header.chunkSize);

    size_t total = sizeof(FileDataHeader) + header.chunkSize;
    if (len < total) {
        return false;
    }

    payload = data + sizeof(FileDataHeader);
    payloadLen = header.chunkSize;
    return true;
}
