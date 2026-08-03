---
title: Prerequisites
---

# Prerequisites

## Toolchain

| Tool | Requirement |
| --- | --- |
| CMake | ≥ 3.24 |
| Compiler | MSVC 2022 (Windows) / GCC 10+ / Clang 12+ (Linux/macOS) |
| Python | 3.x (with mako: `pip install mako`, required for code generation) |
| vcpkg | Provides prebuilt dependencies such as openssl/mbedtls, libcurl, and libuv on Windows |
| git | Submodule initialization (handled automatically by `atframework/Repository.cmake`) |

Third-party dependencies are downloaded and built via the cmake-toolset ports mechanism (see
`third_party/Repository.cmake`); GitHub mirrors are supported.

## Runtime Dependencies

### etcd (required)

atproxy uses etcd for service discovery and online detection. For local testing, a one-shot script can start a
temporary instance:

```bash
# Linux / macOS (port 12379)
bash atframework/libatapp/ci/etcd/setup-etcd.sh start
export ATAPP_UNIT_TEST_ETCD_HOST="http://127.0.0.1:12379"
```

```powershell
# Windows
.\atframework\libatapp\ci\etcd\setup-etcd.ps1 -Command start
$env:ATAPP_UNIT_TEST_ETCD_HOST = "http://127.0.0.1:12379"
```

### Redis (optional, as needed)

The data layer (`db_msg_dispatcher`, router object cache, dtmq channel records, etc.) uses Redis (supporting both
cluster and sentinel/raw connection modes). Services that use the DB must have it prepared in advance.

## Directory Conventions

- All build artifacts are published to `<BUILD_DIR>/publish/`.
- On Windows, the recommended build directory is `build_jobs_cmake_tools` (consistent with the clangd
  configuration in `.vscode/settings.json`).
- On Linux, `cmake_dev.sh` automatically creates `build_jobs_$(uname -s)`.
