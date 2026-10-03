---
slug: /
title: Introduction
---

# atsf4g-co Documentation

**atsf4g-co** (AT Service Framework for Game - Coroutine) is a coroutine-based game server framework built on
[atframework](https://github.com/atframework) libraries (`atframe_utils`, `libatbus`, `libatapp`, and
`libcopp`). Project code requires **C++14** and supports **Windows, Linux, and macOS**.

## Core Features

- **Asynchronous coroutines**: C++20 coroutines and traditional libcopp stackful coroutines share the framework
  API. Switch backends with `PROJECT_SERVER_FRAME_USE_STD_COROUTINE` without changing business code that uses
  that API. The C++20 backend needs a toolchain with standard coroutine support; C++14 uses the stackful backend.
- **Declarative development**: protobuf declarations drive RPC APIs, handler registration, task action skeletons,
  database APIs, and Excel loaders. Routine extensions use existing declarations plus business logic.
- **Cross-platform infrastructure**: atgateway handles client access and atproxy handles inter-server traffic;
  service discovery, router caches, Redis access, and OpenTelemetry are built in.
- **Reusable components and services**: dtmq, distributed transactions, leaderboards, friends, matchmaking,
  teams, and their SDKs.
- **Orbit**: a Dedicated Server (DS) management solution for Unreal Engine (UE), with controller / agent /
  server / client layers managing process lifecycles, heartbeats, and communication.
- **Deployment tools**: atdtool and values configuration generate resources and scripts for Kubernetes,
  Docker, and bare-metal/local deployments.

Check third-party toolchain requirements separately from the project's C++14 requirement; newer dependency
versions can cause a build to use a higher language standard. See [prerequisites](getting-started/prerequisites)
and [switching coroutine backends](getting-started/build#coroutine-backend).

## Where to Start

For a first run, follow [prerequisites](getting-started/prerequisites) → [build](getting-started/build) →
[run and deploy](getting-started/run-deploy). In a working project, go straight to the
[development quick start](development/overview) and choose your task.

| Section | Content |
| --- | --- |
| [Development Quick Start](development/overview) | Minimal workflows for RPCs, services, components, Excel, server config, protocols, and DB tables |
| [Components](components/overview) | Integration entry points for dtmq, transactions, rank, friends, matchmaking, teams, and Orbit |
| [Services](services/overview) | Service responsibilities and local validation entry points |
| [Architecture](architecture/overview) | Design and implementation details for changes to framework behavior |

## Repository Layout at a Glance

```text
atsf4g-co/
├── atframework/        # Framework libraries and atproxy / atgateway
├── src/
│   ├── server_frame/   # Shared config, protocols, dispatcher, router, RPC, data layer
│   ├── *svr/           # Lobby, authentication, cache, friend, matchmaking, team services
│   ├── component/      # dtmq, distributed_transaction, rank, orbit, shared algorithms
│   ├── templates/      # Code generation templates for framework maintainers
│   ├── tools/          # Code generators and offline RPC test tools
│   └── robot/          # Go stress-test and simulated client
├── install/            # Deployment templates and values
├── resource/           # Excel tables and resource conversion configuration
├── project/            # Build options and tools
└── third_party/        # Third-party dependencies
```
