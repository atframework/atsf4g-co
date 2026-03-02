# atgateway Protocol v2 — 2-Message ECDH Handshake

## Overview

The atgateway v2 protocol uses a **2-message ECDH key exchange**, inspired by
libatbus's `node_register_req`/`node_register_rsp` flow. This replaces the
previous multi-step SSL-like handshake that required 10+ round-trips.

```
Client                                    Server
  │                                         │
  │  kKeyExchangeReq                        │
  │  ──────────────────────────────────────► │
  │  (client_pubkey, algorithms,            │
  │   access_data[], session_id=seq)        │
  │                                         │
  │                  kKeyExchangeRsp        │
  │  ◄────────────────────────────────────── │
  │  (server_pubkey, algorithms,            │
  │   access_data[], session_id=assigned)   │
  │                                         │
  │  ←── encrypted channel established ──►  │
```

After the exchange, both sides derive a shared secret via ECDH, then use
HKDF-SHA256 to produce symmetric key material for AES-256-GCM (or another
negotiated cipher).

## Key Exchange Algorithms

| Enum value    | DH parameter        |
|---------------|---------------------|
| `kSecp256r1`  | `ecdh:secp256r1`    |
| `kSecp384r1`  | `ecdh:secp384r1`    |
| `kSecp521r1`  | `ecdh:secp521r1`    |
| `kX25519`     | `ecdh:X25519`       |

## Crypto Algorithms

| Enum value          | OpenSSL cipher name |
|---------------------|---------------------|
| `kAes128Gcm`        | `aes-128-gcm`       |
| `kAes256Gcm`        | `aes-256-gcm`       |
| `kAes128Ctr`        | `aes-128-ctr`       |
| `kAes256Ctr`        | `aes-256-ctr`       |
| `kChacha20Poly1305` | `chacha20-poly1305` |

## Authentication

Each handshake message carries one or more `access_data` entries. Each entry
contains:

| Field       | Description                                  |
|-------------|----------------------------------------------|
| `algorithm` | Always `kHmacSha256`                         |
| `timestamp` | Unix epoch seconds (server rejects ±300 s)   |
| `nonce1`    | Random 64-bit value                          |
| `nonce2`    | Random 64-bit value                          |
| `signature` | HMAC-SHA256 over the plaintext below         |

**Plaintext** (used for HMAC input):

```
{timestamp}:{nonce1}-{nonce2}:{session_id}:{key_exchange_int}:{sha256_hex(public_key)}
```

The server verifies the signature against **all configured access tokens** (any
match is accepted). Multiple tokens enable zero-downtime key rotation.

## Configuration (Protobuf)

```protobuf
message atgateway_client_crypt_cfg {
  repeated bytes access_tokens  = 1;
  Duration       update_interval = 2;
  atgateway_key_exchange_type  key_exchange = 3;
  repeated atgateway_crypto_algorithm algorithms = 4;
}
```

## FlatBuffers Schema

See `libatgw_protocol_sdk.fbs` for the full schema. Key types:

- `cs_body_handshake` — handshake request/response body
- `cs_body_post` — encrypted application data
- `client_message` — top-level envelope (union of handshake/post/ping/pong/kickoff/key_syn)

## API Quick Reference

### Global configuration

```cpp
libatgw_protocol_sdk::crypt_conf_t conf;
conf.key_exchange_algorithm = key_exchange_t::kSecp256r1;
conf.supported_algorithms.push_back(crypto_algorithm_t::kAes256Gcm);
conf.access_tokens.push_back({...});
libatgw_protocol_sdk::global_reload(conf);
```

### Per-connection (crypt_session_t)

```cpp
// Key generation (uses global DH shared context)
session.handshake_generate_self_key(peer_sequence_id);

// Exchange public keys
session.handshake_write_self_public_key(out_pubkey);
session.handshake_read_peer_key(peer_pubkey, peer_algs, local_algs);

// Encrypt/decrypt
session.encrypt_data(in, insz, out, outsz);
session.decrypt_data(in, insz, out, outsz);
```

### Direct key setup (testing / bypass DH)

```cpp
session.setup_crypto_with_key(crypto_algorithm_t::kAes256Gcm, key, 32, iv, 12);
```

## Unit Tests

Build and run:

```bash
cmake --build <build_dir> --target atgateway_protocol_unit_test
# On Windows, ensure DLLs are on PATH:
# publish/bin, publish/atframework/atgateway/bin, third_party/install/.../bin
./test/atgateway_protocol_unit_test
```

Test cases:
- `crypt_session_default_state` — no-cipher passthrough
- `global_reload` — configuration initialization
- `setup_crypto_with_key_and_roundtrip` — encrypt/decrypt with explicit key
- `encrypt_decrypt_no_cipher` — passthrough mode
- `access_data_generation_and_verification` — HMAC signature consistency
- `crypt_session_close` — cleanup behavior
