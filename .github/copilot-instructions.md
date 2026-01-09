# atsf4g-co - Copilot Instructions

## Project Overview

**atsf4g-co** (AT Service Framework for Game - Coroutine) is a service framework for building game servers using libatbus, libatapp, libcopp and other atframework components. It provides a complete solution for building scalable, high-performance game server architectures.

- **Repository**: https://github.com/atframework/atsf4g-co
- **License**: MIT
- **Languages**: C++ (C++17 required, C++17/C++20/C++23 features used when available)

## Skills (How-to playbooks)

Operational, copy/paste-friendly guides live in `.github/skills/`:

- Entry point: `.github/skills/README.md`

## Build System

This project uses **CMake** (minimum version 3.24.0).

Build steps and common configuration options are documented in:

- `.github/skills/build.md`

## Directory Structure

```
atsf4g-co/
├── atframework/           # Core framework libraries
│   ├── atframe_utils/     # Utility library
│   ├── libatbus/          # Message bus library
│   ├── libatapp/          # Application framework
│   ├── cmake-toolset/     # CMake build tools
│   ├── export/            # Exported client libraries
│   └── service/           # Inner services
│       ├── atproxy/       # Proxy server
│       ├── atgateway/     # Gateway server
│       └── component/     # Common service components
├── src/                   # Application source code
│   ├── server_frame/      # Server common library
│   │   ├── config/        # Server configuration
│   │   ├── data/          # Game data layer
│   │   ├── dispatcher/    # Message dispatchers
│   │   ├── logic/         # Game logic layer
│   │   ├── protocol/      # Protocol definitions
│   │   ├── router/        # Routing logic
│   │   ├── rpc/           # RPC APIs
│   │   └── utility/       # Utility codes
│   ├── component/         # Shared components
│   ├── echosvr/           # Echo server sample
│   ├── gamesvr/           # Game server
│   ├── loginsvr/          # Login server
│   ├── tools/             # Tool projects
│   └── templates/         # Code generation templates
├── project/               # Build scripts and cmake modules
│   ├── cmake/             # CMake modules
│   └── tools/             # Build tools
├── sample/                # Sample applications
├── resource/              # Resources and templates
├── third_party/           # Third-party dependencies
└── install/               # Installation resources
```

## Inner Services

### atproxy

Proxy server for logic servers to communicate with each other.

- Uses [etcd](https://etcd.io/) for service discovery and online detection
- Handles cross-group server communication

### atgateway

Gateway server for client connections to logic servers.

- DH/ECDH key exchange for secure connections
- Traffic control and handshake timeout per client
- Routing switching support

## Unit Testing Framework

This project uses a **private unit testing framework** (same as atframe_utils/libatbus/libatapp).

### Test Framework Macros

```cpp
// Define a test case
CASE_TEST(test_group_name, test_case_name) {
    // Test implementation
}

// Assertions
CASE_EXPECT_TRUE(condition)
CASE_EXPECT_FALSE(condition)
CASE_EXPECT_EQ(expected, actual)
CASE_EXPECT_NE(val1, val2)
CASE_EXPECT_LT(val1, val2)
CASE_EXPECT_LE(val1, val2)
CASE_EXPECT_GT(val1, val2)
CASE_EXPECT_GE(val1, val2)
CASE_EXPECT_ERROR(message)

// Logging during tests
CASE_MSG_INFO() << "Info message";
CASE_MSG_ERROR() << "Error message";

// Test utilities
CASE_THREAD_SLEEP_MS(milliseconds)
CASE_THREAD_YIELD()
```

### Running Tests

See `.github/skills/testing.md` for how to run unit tests (including Windows DLL/PATH notes).

## CMake helper functions

See `.github/skills/service-functions-cmake.md`.

## Server Architecture

### Layers

1. **Protocol Layer** (`protocol/`) - Protobuf message definitions
2. **Data Layer** (`data/`) - Game data management and persistence
3. **RPC Layer** (`rpc/`) - Remote procedure call implementations
4. **Logic Layer** (`logic/`) - Game logic and business rules
5. **Dispatcher Layer** (`dispatcher/`) - Message routing and coroutine task management
6. **Router Layer** (`router/`) - Service routing

### Message Flow

```
Client → atgateway → atproxy → Game Server
                  ↓
              libatbus (message bus)
                  ↓
           dispatcher → logic → data → DB
```

## Deployment configuration

See `.github/skills/deployment-config.md`.

## Code generation

See `.github/skills/code-generation.md`.

## Coding Conventions

1. **Namespaces**:

   - Framework: `atframework::*`
   - Project: Follow project naming

2. **Include guards**: Use `#pragma once`

3. **C++ Standard**: C++17 required

4. **Naming**:

   - Classes/structs: `snake_case`
   - Functions: `snake_case`
   - Constants: `UPPER_SNAKE_CASE`
   - Types: `*_t` suffix for typedefs

5. **Error handling**: Use return codes or error enums

6. **Logging**: Use FWLOG macros
   ```cpp
   FWLOGINFO("Message: {}", value);
   FWLOGERROR("Error: {}", error);
   ```

## Compiler Support

| Compiler | Minimum Version |
| -------- | --------------- |
| GCC      | 7.1+            |
| Clang    | 7+              |
| MSVC     | VS2022+         |

## Dependencies

### Required

- CMake 3.24.0+
- C++17 compiler
- protobuf
- libuv
- OpenSSL or MbedTLS

### Optional

- etcd (for service discovery)
- Redis (for data storage)

### Python Modules

- python-mako (for code generation)
- python-setuptools (optional)
- python-pip (optional)

## Development Tools

- cmake 3.24+
- git
- gdb/lldb/windb (debugging)
- unzip, tar, p7zip
- autoconf, automake
- Python 3.x

## Build Outputs

All compiled resources are placed in:

```
<BUILD_DIR>/publish/
├── bin/           # Executables
├── lib/           # Libraries
├── resource/      # Resources
│   └── pbdesc/    # Protobuf descriptors
└── tools/         # Tools and scripts
    ├── bin/
    └── script/
```

## Subprojects

Each subproject has its own `copilot-instructions.md`:

- [atframe_utils/.github/copilot-instructions.md](atframework/atframe_utils/.github/copilot-instructions.md)
- [libatbus/.github/copilot-instructions.md](atframework/libatbus/.github/copilot-instructions.md)
- [libatapp/.github/copilot-instructions.md](atframework/libatapp/.github/copilot-instructions.md)
