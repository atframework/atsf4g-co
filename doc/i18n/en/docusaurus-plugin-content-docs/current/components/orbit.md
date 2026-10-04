---
title: Orbit
---

# Orbit

Orbit is a Dedicated Server (DS) management solution for Unreal Engine (UE).
Its controller / agent / server / client layers cover scheduling, node-level process management,
business-service integration, and managed-process communication.
An Orbit client is a managed process role, usable by a UE DS; it is distinct from a player's game client.

Location: `src/component/orbit/`.

## Quick Start

1. Enable `orbit-controller`, `orbit-agent`, and `orbit-server` in values.
   The `orbit-server` chart uses the `src/orbitsvr/` example.
2. Configure/export process templates in `resource/ExcelTables/OrbitClient.xlsx`.
   Set `orbit_agent.client_path` and `orbit_agent.client_command_line` in the values file
   `orbit-agent.yaml`; the generated configuration type is `orbit_agent_cfg`.
3. Follow `src/orbitsvr/` for business server SDK dependencies and registration.
   Integrate UE DS with the client runtime under
   `src/component/GameSharedComponent/Orbit/include/Orbit/`, using `OrbitClientRuntime.h`,
   `OrbitEasyApi.h`, and the example `orbit_config.yaml`.
4. Verify startup, heartbeats, message echo, and exit with a runtime-enabled process before using the actual
   UE DS. Agent and managed process identity/communication settings must agree.
   Existing process paths, arguments, and template data can be changed through configuration.

## Customization and Design

### Composition

| Part | Description |
| --- | --- |
| `controller/` | Controller service: global scheduling decisions |
| `agent/` | Agent service: node-level agent |
| `protocol/` | Three proto groups: client / common / server |
| `sdk/` | Four SDKs: agent / client / controller / server; the server SDK ships its own dedicated Mako templates |

### Code Generation

The orbit server SDK uses its own templates (`orbit/sdk/server/template/`):
`handle_orbit_rpc` / `task_action_orbit_rpc` / `rpc_call_api_for_orbit`, generating `handle_orbit_rpc_*`,
`task_action_orbit_rpc`, and other code.

### Example Service

`src/orbitsvr/` demonstrates the usage of the orbit component:
`handle_orbit_rpc_orbitserverrpcservice` (orbit server RPC handler) and `task_action_echo`.

### Client-Side Runtime

`src/component/GameSharedComponent/Orbit/` provides the client-side (e.g., UE) Orbit runtime SDK;
`resource/UeSource*` stores the UE protocol source data.
