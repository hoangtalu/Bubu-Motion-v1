# Protocol Compliance Matrix (Bubu vs 78/xiaozhi-esp32)

## 1) Baseline Lock (Step 1)

- Upstream repository: `https://github.com/78/xiaozhi-esp32`
- Baseline branch: `main`
- Baseline commit: `331d69b83b097595db3d45de6af7c389f545a5e7`
- Commit date (UTC): `2026-05-25T21:42:19Z`
- Commit link: `https://github.com/78/xiaozhi-esp32/commit/331d69b83b097595db3d45de6af7c389f545a5e7`

This matrix treats the baseline commit above as the source-of-truth protocol target.

## 2) Compliance Matrix (Step 2)

Legend:
- `MATCH`: behavior/schema aligned with baseline
- `DIVERGE_SAFE`: intentional extension, wire-compatible
- `DIVERGE_RISK`: may break strict compatibility; should be normalized

| Area | Baseline (78) | Bubu current | Status | Evidence |
|---|---|---|---|---|
| Protocol abstraction (`Protocol` API) | Common API for WS/MQTT (`SendStartListening`, `SendAbortSpeaking`, `SendMcpMessage`, timeout) | Same API and semantics | MATCH | `main/protocols/protocol.h`, `main/protocols/protocol.cc:69-118` |
| Listen start message | `{"session_id","type":"listen","state":"start","mode":"auto/manual/realtime"}` | Same fields and mode mapping | MATCH | `main/protocols/protocol.cc:85-97` |
| Listen stop message | `{"session_id","type":"listen","state":"stop"}` | Same | MATCH | `main/protocols/protocol.cc:99-102` |
| Abort message | `{"session_id","type":"abort","reason":"wake_word_detected"(optional)}` | Same | MATCH | `main/protocols/protocol.cc:69-76` |
| MCP envelope | `{"session_id","type":"mcp","payload":<jsonrpc>}` | Same | MATCH | `main/protocols/protocol.cc:104-107` |
| Timeout policy | channel timeout at 120s | Same timeout logic | MATCH | `main/protocols/protocol.cc:109-118` |
| WS request headers | `Authorization`, `Protocol-Version`, `Device-Id`, `Client-Id` | Same | MATCH | `main/protocols/websocket_protocol.cc:104-113` |
| WS hello request | `type=hello`, `version`, `features(mcp,aec)`, `transport=websocket`, `audio_params(opus,16000,1,frame_duration)` | Same structure | MATCH | `main/protocols/websocket_protocol.cc:206-224` |
| WS server hello validate | require `transport=websocket`, parse `session_id`, `audio_params` | Same | MATCH | `main/protocols/websocket_protocol.cc:231-257` |
| WS binary audio framing | support v1 raw opus, v2 `BinaryProtocol2`, v3 `BinaryProtocol3` | Same | MATCH | `main/protocols/websocket_protocol.cc:115-150` |
| WS hello wait timeout | wait 10s for server hello | Same | MATCH | `main/protocols/websocket_protocol.cc:191-197` |
| WS text parse method | baseline uses `cJSON_ParseWithLength(data,len)` | now uses `cJSON_ParseWithLength(data,len)` | MATCH | `main/protocols/websocket_protocol.cc:152-171` |
| WS malformed `type` logging | baseline logs payload from bounded buffer | now logs bounded `std::string(data,len)` | MATCH | `main/protocols/websocket_protocol.cc:167-168` |
| MQTT hello request | `type=hello`, `transport=udp`, `features(mcp,aec)`, `audio_params` | Same | MATCH | `main/protocols/mqtt_protocol.cc:348-371` |
| MQTT server hello validate | require `transport=udp`, parse `session_id`,`audio_params`,`udp{server,port,key,nonce}` | Same | MATCH | `main/protocols/mqtt_protocol.cc:373-419` |
| MQTT hello wait timeout | wait 10s for server hello | Same | MATCH | `main/protocols/mqtt_protocol.cc:240-246` |
| MQTT audio crypto | AES-CTR with nonce/key from hello | Same | MATCH | `main/protocols/mqtt_protocol.cc:276-292`, `main/protocols/mqtt_protocol.cc:411-413` |
| MQTT packet header format | `|type|flags|payload_len|ssrc|timestamp|sequence|...|` | Same expectation | MATCH | `main/protocols/mqtt_protocol.cc:252-255` |
| MQTT sequence handling | baseline checks progression; simpler path | strict mode available; optional Bubu jitter-reorder extension can be disabled | MATCH (configurable) | `main/protocols/mqtt_protocol.cc`, `main/Kconfig.projbuild:819+` |
| MQTT goodbye handling | client/server close with ping-pong protection | Same behavior | MATCH | `main/protocols/mqtt_protocol.cc:121-129`, `main/protocols/mqtt_protocol.cc:198-221` |
| Wake-word detect payload build | baseline string concatenation | Same string-concatenation payload build | MATCH | `main/protocols/protocol.cc` |

## 3) Immediate Normalization Targets (for Step 3)

- P0 done (2026-05-26): WS JSON parse path normalized to `cJSON_ParseWithLength(data, len)`.
- P0 done (2026-05-26): malformed-type logging normalized to bounded payload logging.
- P1 done (2026-05-26): MQTT jitter/reorder extension is now gated by `CONFIG_MQTT_STRICT_UPSTREAM_SEQUENCE`.
- Strict mode ON (default): behaves like upstream sequence handling.
- Strict mode OFF: keeps Bubu jitter-reorder tolerance.

## 4) Scope Statement

This document only covers protocol-layer compliance:
- `main/protocols/protocol.*`
- `main/protocols/websocket_protocol.*`
- `main/protocols/mqtt_protocol.*`

It does not yet cover higher-layer state logic in `application.cc` beyond message transport/contract handling.
