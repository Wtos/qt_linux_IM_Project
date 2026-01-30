# Issue Log

- ID: IM-SRV-001
  Title: Parser drops valid message after invalid header in same recv
  Component: server/src/protocol.cpp (ProtocolParser::parseData)
  Repro: In one TCP connection, send invalid header(s) (bad magic/version/bodyLength) and then a valid login request immediately; login response never arrives (timeout).
  Suspected cause: On invalid header, the parser clears the buffer and breaks, so coalesced valid bytes are discarded with no resync.
  Impact: Stream can stall after transient corruption or mixed frames.
  Status: Open
  Found in: extended stress test (invalid_headers_recovery) on 127.0.0.1

- ID: IM-SRV-002
  Title: Heartbeat timeout disconnect not observed within expected window
  Component: server/src/server.cpp (heartbeatLoop/processPendingDisconnects)
  Repro: Login then idle; after >10s heartbeat timeout, client connection does not close within ~6s observation window.
  Suspected cause: Pending disconnect processing is delayed or requires additional event loop cycles; timing-sensitive behavior.
  Impact: Idle clients may stay connected longer than configured timeout.
  Status: Open
  Found in: full scan test (heartbeat_timeout_disconnect) on 127.0.0.1
