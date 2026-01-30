# 模块3：通信协议详细设计文档

## 📋 文档概述

**文档用途**: 服务端和客户端必须严格遵守的通信协议规范
**协议版本**: v1.0（`PROTOCOL_VERSION = 0x0001`）
**传输层**: TCP
**编码**: 大端字节序（网络字节序）

> 说明：本文档基于当前项目实现（server/client）整理，并预留后续扩展字段与消息类型。

---

## 🎯 协议设计目标

1. **简单高效**: 二进制格式，解析快速
2. **可扩展**: 支持未来添加新消息类型
3. **可靠传输**: TCP保证数据完整性
4. **易于调试**: 魔数用于快速识别协议数据

---

## 📦 消息格式总览

```
+------------------------------------------+
|          消息头 (16字节，固定)            |
+------------------------------------------+
|          消息体 (变长，0-N字节)           |
+------------------------------------------+
```

### 特点
- **固定头部**: 16字节，便于快速解析
- **变长Body**: 根据实际需要动态变化
- **大端序**: 所有多字节数值使用网络字节序（Big-Endian）

---

## 📋 消息头结构 (MessageHeader)

### 结构定义
```
+--------+--------+----------+------------+----------+
| Offset | Length |  Field   |    Type    |  Value   |
+--------+--------+----------+------------+----------+
|   0    |   4    |  magic   |  uint32_t  | 0x12345678 |
|   4    |   2    | version  |  uint16_t  |  0x0001  |
|   6    |   2    | msgType  |  uint16_t  |  见下表  |
|   8    |   4    |bodyLength|  uint32_t  |  Body长度|
|  12    |   4    | sequence |  uint32_t  |  序列号  |
+--------+--------+----------+------------+----------+
总计: 16字节
```

### C++结构体定义
```cpp
#pragma pack(push, 1)  // 禁用内存对齐
struct MessageHeader {
    uint32_t magic;        // 魔数: 0x12345678
    uint16_t version;      // 协议版本: 0x0001
    uint16_t msgType;      // 消息类型
    uint32_t bodyLength;   // 消息体长度（字节数）
    uint32_t sequence;     // 序列号（请求响应匹配）
};
#pragma pack(pop)
```

### 字段说明

#### 1. magic (魔数)
- **长度**: 4字节
- **值**: `0x12345678`
- **用途**:
  - 识别是否为合法协议数据
  - 快速过滤非法数据
  - 调试时便于定位问题

#### 2. version (协议版本)
- **长度**: 2字节
- **值**: `0x0001` (v1.0版本)
- **用途**:
  - 版本兼容性检查
  - 支持协议演进

#### 3. msgType (消息类型)
- **长度**: 2字节
- **值**: 见下方“消息类型表”
- **用途**:
  - 区分不同业务消息
  - 决定消息体的解析方式

#### 4. bodyLength (消息体长度)
- **长度**: 4字节
- **值**: 消息体的字节数（0表示无Body）
- **用途**:
  - 确定完整消息的长度
  - 处理TCP粘包/半包问题

#### 5. sequence (序列号)
- **长度**: 4字节
- **值**: 递增的整数（从1开始）
- **用途**:
  - 匹配请求和响应
  - 超时检测
  - 并发请求处理

---

## 🔢 消息类型定义 (MessageType)

### 类型枚举表

| 十六进制值 | 十进制值 | 符号名称           | 描述           | 方向          |
|-----------|---------|-------------------|----------------|---------------|
| 0x0001    | 1       | MSG_HEARTBEAT_REQ | 心跳请求       | Client→Server |
| 0x0002    | 2       | MSG_HEARTBEAT_RSP | 心跳响应       | Server→Client |
| 0x0101    | 257     | MSG_LOGIN_REQ     | 登录请求       | Client→Server |
| 0x0102    | 258     | MSG_LOGIN_RSP     | 登录响应       | Server→Client |
| 0x0103    | 259     | MSG_LOGOUT_REQ    | 登出请求       | Client→Server |
| 0x0201    | 513     | MSG_TEXT_MSG      | 文本消息(阶段二)| Client↔Server |
| 0x0301    | 769     | MSG_FILE_REQ      | 文件传输(阶段三)| Client→Server |

> 注：当前实现已支持 0x0001/0x0002/0x0101/0x0102/0x0103，
> 0x0201 与 0x0301 为协议预留，待阶段二/三实现。

### 编号规则
- **0x00xx**: 系统级消息（心跳、握手等）
- **0x01xx**: 认证相关（登录、登出等）
- **0x02xx**: 聊天相关（文本、表情等）
- **0x03xx**: 文件传输相关

### C++枚举定义
```cpp
enum MessageType : uint16_t {
    // 系统消息
    MSG_HEARTBEAT_REQ = 0x0001,
    MSG_HEARTBEAT_RSP = 0x0002,

    // 认证消息
    MSG_LOGIN_REQ = 0x0101,
    MSG_LOGIN_RSP = 0x0102,
    MSG_LOGOUT_REQ = 0x0103,

    // 聊天消息（阶段二）
    MSG_TEXT_MSG = 0x0201,

    // 文件传输（阶段三）
    MSG_FILE_REQ = 0x0301,
};
```

---

## 📝 消息体结构定义

### 1. 心跳消息

#### MSG_HEARTBEAT_REQ (0x0001)
**消息体**: 空（bodyLength = 0）

**示例数据包**:
```
消息头(16字节):
  magic:      0x12345678
  version:    0x0001
  msgType:    0x0001
  bodyLength: 0x00000000
  sequence:   0x00000001
消息体: 无
```

#### MSG_HEARTBEAT_RSP (0x0002)
**消息体**: 空（bodyLength = 0）

**示例数据包**:
```
消息头(16字节):
  magic:      0x12345678
  version:    0x0001
  msgType:    0x0002
  bodyLength: 0x00000000
  sequence:   0x00000001  (与请求的sequence相同)
消息体: 无
```

---

### 2. 登录请求 MSG_LOGIN_REQ (0x0101)

#### 消息体结构
```
+--------+--------+----------+----------+-----------+
| Offset | Length |  Field   |   Type   |  描述     |
+--------+--------+----------+----------+-----------+
|   0    |  32    | clientId |  char[]  | 客户端ID  |
|  32    |  64    | nickname |  char[]  | 用户昵称  |
+--------+--------+----------+----------+-----------+
总计: 96字节
```

#### C++结构体
```cpp
struct LoginRequest {
    char clientId[32];    // 客户端唯一ID（C字符串）
    char nickname[64];    // 用户昵称（UTF-8编码）
};
```

#### 字段说明
- **clientId**:
  - 长度: 32字节（包含'\0'）
  - 格式: 字符串，如"CLIENT_20260109_143025_1234"
  - 用途: 唯一标识客户端

- **nickname**:
  - 长度: 64字节（包含'\0'）
  - 格式: UTF-8编码的字符串
  - 用途: 显示用户昵称

#### 示例数据包
```
消息头(16字节):
  magic:      0x12345678
  version:    0x0001
  msgType:    0x0101
  bodyLength: 0x00000060  (96字节)
  sequence:   0x00000002

消息体(96字节):
  clientId:   "CLIENT_20260109_143025_1234\0" (32字节)
  nickname:   "张三\0"                        (64字节)
```

#### 客户端发送代码示例
```cpp
std::vector<uint8_t> packLoginRequest(uint32_t sequence,
                                      const std::string& clientId,
                                      const std::string& nickname) {
    std::vector<uint8_t> buffer(sizeof(MessageHeader) + sizeof(LoginRequest));

    // 构建消息头
    MessageHeader header;
    header.magic = htonl(0x12345678);
    header.version = htons(0x0001);
    header.msgType = htons(MSG_LOGIN_REQ);
    header.bodyLength = htonl(sizeof(LoginRequest));
    header.sequence = htonl(sequence);

    // 构建消息体
    LoginRequest req;
    std::memset(&req, 0, sizeof(req));
    std::strncpy(req.clientId, clientId.c_str(), sizeof(req.clientId) - 1);
    std::strncpy(req.nickname, nickname.c_str(), sizeof(req.nickname) - 1);

    // 序列化
    std::memcpy(buffer.data(), &header, sizeof(MessageHeader));
    std::memcpy(buffer.data() + sizeof(MessageHeader), &req, sizeof(LoginRequest));

    return buffer;
}
```

---

### 3. 登录响应 MSG_LOGIN_RSP (0x0102)

#### 消息体结构
```
+--------+--------+----------+----------+-----------+
| Offset | Length |  Field   |   Type   |  描述     |
+--------+--------+----------+----------+-----------+
|   0    |   4    |  result  | uint32_t | 结果码    |
|   4    |  128   | message  |  char[]  | 响应消息  |
+--------+--------+----------+----------+-----------+
总计: 132字节
```

#### C++结构体
```cpp
struct LoginResponse {
    uint32_t result;      // 结果码（见下方枚举）
    char message[128];    // 响应消息（UTF-8编码）
};
```

#### 结果码枚举
```cpp
enum LoginResult : uint32_t {
    LOGIN_SUCCESS = 0,          // 登录成功
    LOGIN_INVALID_PARAM = 1,    // 参数错误
    LOGIN_SERVER_FULL = 2,      // 服务器已满
    LOGIN_ALREADY_ONLINE = 3,   // 客户端已在线
    LOGIN_NICKNAME_TAKEN = 4,   // 昵称已被占用(预留)
};
```

> 注：当前实现中 LoginResult 已使用 0~3，
> `LOGIN_NICKNAME_TAKEN` 为可选预留值，待业务引入后启用。

#### 示例数据包 - 成功
```
消息头(16字节):
  magic:      0x12345678
  version:    0x0001
  msgType:    0x0102
  bodyLength: 0x00000084  (132字节)
  sequence:   0x00000002  (与请求相同)

消息体(132字节):
  result:     0x00000000  (LOGIN_SUCCESS)
  message:    "登录成功！欢迎 张三\0"
```

#### 示例数据包 - 失败
```
消息头(16字节):
  magic:      0x12345678
  version:    0x0001
  msgType:    0x0102
  bodyLength: 0x00000084  (132字节)
  sequence:   0x00000002

消息体(132字节):
  result:     0x00000001  (LOGIN_INVALID_PARAM)
  message:    "昵称不能为空\0"
```

#### 服务端发送代码示例
```cpp
std::vector<uint8_t> packLoginResponse(uint32_t sequence,
                                       uint32_t result,
                                       const std::string& message) {
    std::vector<uint8_t> buffer(sizeof(MessageHeader) + sizeof(LoginResponse));

    // 构建消息头
    MessageHeader header;
    header.magic = htonl(0x12345678);
    header.version = htons(0x0001);
    header.msgType = htons(MSG_LOGIN_RSP);
    header.bodyLength = htonl(sizeof(LoginResponse));
    header.sequence = htonl(sequence);

    // 构建消息体
    LoginResponse rsp;
    rsp.result = htonl(result);
    std::memset(rsp.message, 0, sizeof(rsp.message));
    std::strncpy(rsp.message, message.c_str(), sizeof(rsp.message) - 1);

    // 序列化
    std::memcpy(buffer.data(), &header, sizeof(MessageHeader));
    std::memcpy(buffer.data() + sizeof(MessageHeader), &rsp, sizeof(LoginResponse));

    return buffer;
}
```

---

## 🔄 通信流程时序图

### 完整登录流程

```
客户端                                           服务端
  |                                               |
  |------------ TCP Connect (三次握手) ---------->|
  |                                               |
  |<----------- TCP Connected --------------------|
  |                                               |
  |------------ MSG_LOGIN_REQ ------------------->|
  |  Header:                                      |
  |    sequence: 1                                |
  |  Body:                                        |  - 验证魔数和版本
  |    clientId: "CLIENT_..."                     |  - 解析LoginRequest
  |    nickname: "张三"                           |  - 更新客户端身份
  |                                               |
  |<----------- MSG_LOGIN_RSP --------------------|
  |  Header:                                      |
  |    sequence: 1 (相同)                         |
  |  Body:                                        |
  |    result: 0 (成功)                           |
  |    message: "登录成功"                        |
  |                                               |
  |------------ MSG_HEARTBEAT_REQ --------------->|
  |  (每5秒)                                      |
  |                                               |  - 更新lastHeartbeat
  |<----------- MSG_HEARTBEAT_RSP ----------------|
  |                                               |
  |            ...正常通信中...                   |
  |                                               |
  |                                               |  (10秒无心跳)
  |                                               |  - 心跳超时检测
  |<----------- TCP Close (主动断开) -------------|
  |                                               |  - 移除客户端
  |                                               |
```

### 错误处理流程（参数错误）

```
客户端                                           服务端
  |                                               |
  |------------ MSG_LOGIN_REQ ------------------->|
  |  Body:                                        |
  |    clientId: "CLIENT_..."                     |  - 检测到参数无效
  |    nickname: "" (空字符串)                    |
  |                                               |
  |<----------- MSG_LOGIN_RSP --------------------|
  |  Body:                                        |
  |    result: 1 (LOGIN_INVALID_PARAM)            |
  |    message: "昵称不能为空"                    |
  |                                               |
  | [显示错误提示，允许重新登录]                   |
  |                                               |
```

> 注：当前实现仅校验 `LoginRequest` 长度，昵称非空校验需在业务侧补充。

---

## 🛠️ 协议解析实现

### 1. 处理TCP粘包和半包

#### 问题说明
TCP是**流式协议**，可能出现:
- **粘包**: 多个消息一起到达
- **半包**: 一个消息分多次到达

#### 解决方案
使用**接收缓冲区** + **消息头长度字段**

#### 服务端解析代码（节选）
```cpp
void parseData(int fd, const uint8_t* data, size_t len,
               MessageCallback callback) {
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

        size_t totalLen = sizeof(MessageHeader) + header.bodyLength;
        if (buffer.size() < totalLen) {
            break;
        }

        const uint8_t* body = buffer.data() + sizeof(MessageHeader);
        callback(fd, header, body, header.bodyLength);

        buffer.erase(buffer.begin(), buffer.begin() + totalLen);
    }
}
```

#### 客户端解析代码（Qt版本，节选）
```cpp
void onReadyRead() {
    QByteArray newData = socket_->readAll();
    recvBuffer_.append(newData);

    while (recvBuffer_.size() >= (int)sizeof(MessageHeader)) {
        MessageHeader header;
        std::memcpy(&header, recvBuffer_.data(), sizeof(MessageHeader));

        header.magic = ntohl(header.magic);
        header.version = ntohs(header.version);
        header.msgType = ntohs(header.msgType);
        header.bodyLength = ntohl(header.bodyLength);
        header.sequence = ntohl(header.sequence);

        if (!ProtocolParser::validateHeader(header)) {
            recvBuffer_.clear();
            break;
        }

        int totalLen = sizeof(MessageHeader) + header.bodyLength;
        if (recvBuffer_.size() < totalLen) {
            break;
        }

        QByteArray body = recvBuffer_.mid(sizeof(MessageHeader), header.bodyLength);
        processMessage(header, body);
        recvBuffer_.remove(0, totalLen);
    }
}
```

#### 头部校验规则
```cpp
bool validateHeader(const MessageHeader& header) {
    if (header.magic != 0x12345678) return false;
    if (header.version != 0x0001) return false;
    if (header.bodyLength > 1024 * 1024) return false;  // 最大1MB
    return true;
}
```

---

### 2. 字节序转换

#### 为什么需要字节序转换？
- **大端序（Big-Endian）**: 高位字节在前，网络字节序
- **小端序（Little-Endian）**: 低位字节在前，x86架构

#### 示例
数值 `0x12345678` 在内存中:
- **大端**: `12 34 56 78`
- **小端**: `78 56 34 12`

#### 转换函数
```cpp
#include <arpa/inet.h>  // Linux
// #include <winsock2.h>   // Windows

// 主机序 → 网络序
uint32_t htonl(uint32_t hostlong);   // 32位
uint16_t htons(uint16_t hostshort);  // 16位

// 网络序 → 主机序
uint32_t ntohl(uint32_t netlong);
uint16_t ntohs(uint16_t netshort);
```

#### 使用规则
```cpp
// 发送时: 主机序 → 网络序
header.magic = htonl(0x12345678);
header.msgType = htons(MSG_LOGIN_REQ);

// 接收时: 网络序 → 主机序
header.magic = ntohl(header.magic);
header.msgType = ntohs(header.msgType);
```

---

## 📊 协议测试用例

### 测试用例1: 正常登录
```
步骤:
1. 客户端发送 MSG_LOGIN_REQ
   - clientId: "CLIENT_TEST_001"
   - nickname: "测试用户"
2. 服务端响应 MSG_LOGIN_RSP
   - result: 0 (成功)
   - message: "登录成功"

预期结果:
- 服务端日志显示新用户登录
- 客户端显示"登录成功"
```

### 测试用例2: 参数错误
```
步骤:
1. 客户端发送 MSG_LOGIN_REQ
   - clientId: "CLIENT_TEST_002"
   - nickname: "" (空)
2. 服务端响应 MSG_LOGIN_RSP
   - result: 1 (LOGIN_INVALID_PARAM)
   - message: "昵称不能为空"

预期结果:
- 客户端显示错误提示
- 服务端不添加该客户端到列表
```

### 测试用例3: 心跳正常
```
步骤:
1. 客户端每5秒发送 MSG_HEARTBEAT_REQ
2. 服务端响应 MSG_HEARTBEAT_RSP

预期结果:
- 服务端lastHeartbeat时间更新
- 客户端收到响应（无需显示）
```

### 测试用例4: 心跳超时
```
步骤:
1. 客户端暂停发送心跳（模拟网络中断）
2. 等待15秒

预期结果:
- 服务端检测到超时（10秒阈值）
- 服务端主动断开连接
- 客户端收到disconnected信号
```

### 测试用例5: 粘包处理
```
步骤:
1. 客户端快速连续发送2条消息:
   - MSG_LOGIN_REQ
   - MSG_HEARTBEAT_REQ
2. 服务端可能一次性收到两条消息

预期结果:
- 服务端正确解析出2条独立消息
- 分别处理登录和心跳
```

### 测试用例6: 半包处理
```
步骤:
1. 模拟网络慢速，消息分多次到达
   - 第一次收到: 前10字节
   - 第二次收到: 剩余字节
2. 服务端需要缓存并拼接

预期结果:
- 服务端正确缓存不完整数据
- 完整后正确解析消息
```

---

## 🔍 调试工具

### 1. Wireshark抓包
```
1. 启动Wireshark
2. 选择loopback接口（本地测试）
3. 过滤器: tcp.port == 8888
4. 分析TCP流: 右键 → Follow → TCP Stream
```

### 2. hexdump查看二进制数据
```bash
# 导出数据到文件
echo "binary data" > packet.bin

# 查看十六进制
hexdump -C packet.bin

# 或使用xxd
xxd packet.bin
```

### 3. 自制协议调试工具
```cpp
void printMessageHeader(const MessageHeader& header) {
    printf("=== Message Header ===\n");
    printf("magic:      0x%08X\n", header.magic);
    printf("version:    0x%04X\n", header.version);
    printf("msgType:    0x%04X (%d)\n", header.msgType, header.msgType);
    printf("bodyLength: %u\n", header.bodyLength);
    printf("sequence:   %u\n", header.sequence);
    printf("======================\n");
}

void printHex(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        printf("%02X ", data[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");
}
```

---

## ⚠️ 常见问题

### Q1: 为什么要用魔数？
**A**: 快速识别合法数据，防止误解析其他协议的数据。

### Q2: bodyLength为什么要包含在消息头中？
**A**: 用于判断是否收到完整消息，解决TCP粘包/半包问题。

### Q3: sequence有什么用？
**A**:
1. 匹配请求和响应
2. 检测消息丢失
3. 并发请求处理

### Q4: 为什么用固定长度字符串而不是变长字符串？
**A**:
1. 解析简单，性能高
2. 避免动态内存分配
3. 结构体可以直接memcpy

### Q5: 如何扩展协议？
**A**:
1. 增加新的msgType
2. 定义新的消息体结构
3. 保持向后兼容（版本号检查）

---

## 📚 协议演进计划

### 阶段二：文本聊天
新增消息类型:
```cpp
MSG_TEXT_MSG = 0x0201,        // 文本消息
MSG_USER_LIST_REQ = 0x0202,   // 请求用户列表
MSG_USER_LIST_RSP = 0x0203,   // 用户列表响应
```

### 阶段三：文件传输
新增消息类型:
```cpp
MSG_FILE_REQ = 0x0301,        // 文件传输请求
MSG_FILE_RSP = 0x0302,        // 文件传输响应
MSG_FILE_DATA = 0x0303,       // 文件数据块
MSG_FILE_ACK = 0x0304,        // 文件数据确认
```

---

## ✅ 协议实现检查清单

### 基础功能
- [ ] 消息头结构体定义正确（16字节）
- [ ] 字节序转换正确（htonl/ntohl）
- [ ] 魔数和版本验证正确
- [ ] 消息类型枚举定义完整

### 打包功能
- [ ] packHeartbeatRequest实现
- [ ] packHeartbeatResponse实现
- [ ] packLoginRequest实现
- [ ] packLoginResponse实现

### 解析功能
- [ ] 接收缓冲区正确管理
- [ ] 粘包处理正确
- [ ] 半包处理正确
- [ ] parseLoginRequest实现
- [ ] parseLoginResponse实现

### 错误处理
- [ ] 无效魔数处理
- [ ] 版本不匹配处理
- [ ] bodyLength过大处理
- [ ] 缓冲区溢出保护

---

## 📖 参考资料

- **TCP/IP详解 卷1**: W. Richard Stevens
- **网络字节序**: RFC 1700
- **Protobuf**: Google的协议缓冲区（参考设计思想）

---

**协议版本**: v1.0  \
**最后更新**: 2026-01-09  \
**维护者**: IM项目开发组

---

## 🎉 使用提示

此协议文档应该：
1. ✅ 服务端和客户端开发人员共同遵守
2. ✅ 作为接口规范，严格执行
3. ✅ 任何修改需要同步更新文档
4. ✅ 新增消息类型需要评审后添加

