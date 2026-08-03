---
title: Orbit
---

# Orbit

Orbit is a scheduling framework (four layers: controller / agent / server / client) for managing dynamically
created logical instances.

Location: `src/component/orbit/`.

## Composition

| Part | Description |
| --- | --- |
| `controller/` | Controller service: global scheduling decisions |
| `agent/` | Agent service: node-level agent |
| `protocol/` | Three proto groups: client / common / server |
| `sdk/` | Four SDKs: agent / client / controller / server; the server SDK ships its own dedicated Mako templates |

## Code Generation

The orbit server SDK uses its own templates (`orbit/sdk/server/template/`):
`handle_orbit_rpc` / `task_action_orbit_rpc` / `rpc_call_api_for_orbit`, generating `handle_orbit_rpc_*`,
`task_action_orbit_rpc`, and other code.

## Example Service

`src/orbitsvr/` demonstrates the usage of the orbit component:
`handle_orbit_rpc_orbitserverrpcservice` (orbit server RPC handler) and `task_action_echo`.

## Client-Side Runtime

`src/component/GameSharedComponent/Orbit/` provides the client-side (e.g., UE) Orbit runtime SDK;
`resource/UeSource*` stores the UE protocol source data.
