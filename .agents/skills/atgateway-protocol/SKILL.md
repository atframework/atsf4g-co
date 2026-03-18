---
name: atgateway-protocol
description: atgateway v2 client protocol SDK — ECDH handshake, encryption, compression, reconnection, and testing. Use when working with gateway protocol code, crypto sessions, FlatBuffers schema, or writing gateway unit tests.
---

# atgateway Protocol SDK

The atgateway protocol provides secure client-gateway communication via a 2-message ECDH key exchange.

## Key Files

- `atframework/service/atgateway/protocol/atgateway/protocol/v2/libatgw_protocol_sdk.h` — Main SDK header
- `atframework/service/atgateway/protocol/atgateway/protocol/v2/libatgw_protocol_sdk.fbs` — FlatBuffers wire schema
- `atframework/service/atgateway/protocol/atgateway/protocol/libatgw_protocol_api.h` — Base protocol API
- `atframework/service/atgateway/protocol/atgateway/protocol/libatgw_server_config.proto` — Server config (Protobuf)
- `atframework/service/atgateway/protocol/atgateway/protocol/libatgw_server_protocol.proto` — Gateway↔logic server protocol
- `atframework/service/atgateway/protocol/PROTOCOL.md` — Full protocol documentation

## Architecture

```
Game Client  ←─ FlatBuffers (v2) ─→  atgateway  ←─ Protobuf ─→  Logic Server
                (libatgw_protocol_sdk)              (libatgw_server_protocol)
```

## Handshake Flow

1. **Client** sends `kKeyExchangeReq` with ECDH public key + supported algorithms + access_data
2. **Server** processes, sends `kKeyExchangeRsp` with its public key + selected algorithm + session_id + session_token
3. **Client** sends `kConfirm` to confirm cipher switch
4. Both sides derive shared secret via ECDH → HKDF-SHA256 → symmetric key + IV

## Core Types

- `libatgw_protocol_sdk` — per-connection protocol handler (inherits `libatgw_protocol_api`)
- `crypto_conf_t` — configuration: key exchange, algorithms, access tokens, thresholds
- `crypto_session_t` — per-connection crypto state: ciphers, DH context, compression
- `crypto_shared_context_t` — shared global config (created via `create_shared_context`)
- `proto_callbacks_t` — callback structure: write_fn, message_fn, new_session_fn, close_fn, etc.

## Writing Unit Tests

Tests use a loopback simulation framework. Key helpers:

```cpp
// Setup server-client pair wired back-to-back
sim_peer_t server, client;
libatgw_protocol_api::proto_callbacks_t server_cbs = {}, client_cbs = {};
setup_sim_pair(server, client, server_conf, client_conf, server_cbs, client_cbs);

// Client initiates handshake
int ret = client.sdk->start_session();
CASE_EXPECT_EQ(0, ret);
CASE_EXPECT_EQ(0, server.handshake_status);

// Send encrypted message
client.sdk->send_post(data_span);
CASE_EXPECT_EQ(1, server.received_messages.size());
```

## Building and Running Tests

```bash
cmake --build <build_dir> --target atgateway_protocol_unit_test
```

On Windows, add DLLs to PATH:

- `publish/bin`
- `publish/atframework/atgateway/bin`
- `third_party/install/.../bin`

## Common Patterns

### Algorithm negotiation

Client sends all supported algorithms; server picks the first mutually supported one. If no overlap, falls back to `kNone` (plaintext).

### Key refresh (handshake_update)

Re-runs ECDH exchange on live session. Session ID preserved. Server uses `handshaking_receive_cipher` during transition to handle both old and new keys until client confirms.

### Reconnect

Uses `kReconnectReq`/`kReconnectRsp`. Client sends old session_id + session_token. Server's `reconnect_fn` callback decides acceptance. Full ECDH re-exchange inline. Session token rotated on success.

### Access token authentication

HMAC-SHA256 over `"{timestamp}:{nonce1}-{nonce2}:{session_id}:{key_exchange}:{sha256(pubkey)}"`. Server verifies against all configured tokens. Multiple tokens support zero-downtime rotation.
