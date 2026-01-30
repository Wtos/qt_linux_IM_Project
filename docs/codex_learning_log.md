# Codex 学习记录（项目疏通文档）

> 目标：在 3 天内吃透项目（server → client → 端到端整合）。
> 本文记录已完成的梳理、关键链路、以及后续学习路线，便于在另一台电脑继续使用 Codex 学习。

## 一、项目结构概览
- server：C++14 + epoll + pthreads 的 IM 服务端
- client：Qt 客户端（登录、聊天、文件传输）
- docs：协议设计文档

## 二、三天学习计划（总纲）
- Day1：Server 核心（入口、Reactor、协议、登录/心跳/聊天/文件、断连）
- Day2：Client 核心（Qt 入口、网络层封装、UI→网络、回包→UI）
- Day3：端到端整合（运行、协议一致性、边界/异常、性能风险）

## 三、Server 8 个关键点（学习骨架）
1) 入口与生命周期
- main：`server/src/main.cpp`
- start/run/stop：`server/src/server.cpp`

2) 主数据流（accept → send）
- accept：`ListenHandler::handleRead`（`server/src/server.cpp`）
- 收包：`ClientHandler::handleRead` → `Server::onClientData`
- 解包：`ProtocolParser::parseData`（`server/src/protocol.cpp`）
- 分发：`Server::handleMessage`
- 回包：`Server::sendResponse` → `ClientHandler::queueSend/flushOut`

3) Reactor/epoll 模型
- EPOLLET 边沿触发：`server/src/reactor.cpp`
- 必须循环读/写直到 EAGAIN：`ClientHandler::handleRead/flushOut`

4) 粘包/半包处理
- 每 fd 维护 buffer：`ProtocolParser::parseData`
- 验证 header → 判断完整包 → 回调

5) 协议布局与字节序
- `#pragma pack(push, 1)` 保证结构体无 padding：`server/include/common/message.h`
- 统一使用 htonl/ntohl 等：`server/src/protocol.cpp`

6) 消息分发与业务逻辑
- `Server::handleMessage` 覆盖所有 msgType（登录/心跳/聊天/用户列表/文件）

7) 客户端状态机
- 连接 `addClient` → 登录 `setClientIdentity` → 心跳 `updateHeartbeat`
- 超时 `checkTimeout` → 断连 `handleClientDisconnect`

8) 并发与断连收敛
- 主线程 epoll 循环 + 心跳线程
- 断连走 `queueDisconnect` → `processPendingDisconnects` 统一处理

## 四、已深入讲解的核心链路
### 1) 登录链路（Login）
- `MSG_LOGIN_REQ` → parse → 参数校验 → 唯一性校验 → `setClientIdentity`
- 回包 `MSG_LOGIN_RSP` → 广播用户列表
- 关键文件：
  - `server/src/server.cpp`（login 分支）
  - `server/src/protocol.cpp`（pack/parse）

### 2) 心跳链路（Heartbeat）
- 收到 `MSG_HEARTBEAT_REQ` 更新心跳并回包
- 心跳线程每 5s 检查超时（10s）并断连
- 关键文件：
  - `server/src/server.cpp`（heartbeatLoop）
  - `server/src/client_manager.cpp`（checkTimeout）

## 五、登录界面 → 聊天界面的跳转路径（Client）
**完整链路：**
1. 登录按钮点击 → `LoginWindow::onConnectClicked`（`client/ui/loginwindow.cpp`）
2. 连接成功 → `onConnected` → `tcpClient_->sendLoginRequest`
3. 服务端返回 `MSG_LOGIN_RSP`
4. 客户端解析 → `TcpClient::processMessage` 发射 `loginResponse`
5. `LoginWindow::onLoginResponse` 收到 success → 创建并 show `ChatWindow`，hide 登录窗

关键文件：
- `client/ui/loginwindow.cpp`
- `client/network/tcpclient.cpp`

## 六、GitHub 推送记录（实际操作过程）
- 初始化仓库、添加 `.gitignore`、首次提交
- 远端地址：`https://github.com/Wtos/qt_linux_IM_Project.git`
- 推送成功输出：
  - `main -> main`
  - `分支 'main' 设置为跟踪来自 'origin' 的远程分支 'main'`

**已提交的 .gitignore 规则：**
- build/bin/CMakeFiles 等构建产物
- Qt 生成文件与 obj/lib/so

> 注意：PAT 不应在对话中暴露，已提醒撤销并重新生成。

## 七、后续学习建议（继续路线）
- 继续讲第 2 点：主数据流（accept → send）
- 接着讲第 3 点：Reactor/epoll
- 再拆解聊天、用户列表、文件传输三条业务链路
- 最后做端到端运行验证与日志对照

## 八、建议下次继续的入口
你可以直接告诉 Codex：
- “继续第 2 点：主数据流”
- 或 “继续讲 MSG_CHAT_MSG / MSG_USER_LIST_REQ / FILE_*”

