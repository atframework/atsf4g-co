---
name: atgateway-protocol
description: "Use when: changing atgateway v2 wire schema, handshake/ECDH, crypto, compression, reconnection, or protocol tests. Do not use for ordinary service routing through atgateway."
---

# atgateway Protocol SDK

Use the live schema, implementation, protocol document, and tests as the source of truth. This Skill routes the work; it
does not restate wire behavior that can drift.

## Source routing

Use `<PROTOCOL_ROOT>=atframework/service/atgateway/protocol` and
`<SDK_DIR>=<PROTOCOL_ROOT>/atgateway/protocol` below.

| Task | Read first |
| --- | --- |
| Wire messages or compatibility | `<SDK_DIR>/v2/libatgw_protocol_sdk.fbs` and `<PROTOCOL_ROOT>/PROTOCOL.md` |
| SDK state, handshake, crypto, compression, reconnect | `<SDK_DIR>/v2/libatgw_protocol_sdk.{h,cpp}` and `<SDK_DIR>/libatgw_protocol_api.{h,cpp}` |
| Gateway-to-logic configuration/protocol | `<SDK_DIR>/libatgw_server_config.proto` and `<SDK_DIR>/libatgw_server_protocol.proto` |
| Unit-test behavior and helpers | `<PROTOCOL_ROOT>/test/case/libatgw_protocol_sdk_test.cpp` and `<PROTOCOL_ROOT>/test/CMakeLists.txt` |

## Change workflow

1. Identify the exact frame, state transition, callback, or configuration field and trace both peers through current
   code before proposing behavior.
2. For schema or wire changes, record compatibility with existing clients/servers and whether unknown or old messages
   remain parseable. Regenerate FlatBuffers/protobuf outputs instead of hand-editing generated files.
3. For crypto or authentication changes, verify negotiation failure, downgrade behavior, key/session lifetime,
   transition-state reads, reconnect, and malformed input from current code and tests. Do not infer security behavior
   from names or this Skill.
4. Extend the existing loopback tests with the smallest case that proves the changed transition and its failure path.
   Reuse current helpers from the test file rather than copying a stale example into this Skill.
5. Load `engineering-guidelines` for C++/schema conventions, `testing` for the private test runner, and `build` only when
   configuring/building or diagnosing the build environment.

## Validation

- Build the target resolved from the current protocol test `CMakeLists.txt`; do not rely on a hardcoded target name here.
- Run focused protocol cases, then the protocol test executable's full suite when practical.
- Report any algorithm/backend, platform DLL, or dependency variant that was not exercised.
