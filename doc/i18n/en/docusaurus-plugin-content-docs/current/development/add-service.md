---
title: Adding a Service
---

# Adding a Service

## Quick Start: Copy a Standard Service

Use `src/authsvr/` as a small CS RPC service template. For SS RPCs and a separate SDK, follow
`src/lobbysvr/protocol/CMakeLists.txt`. Copy declarations without writing generation templates.
`echosvr` demonstrates raw message echoing and does not include full business RPC setup.

1. Copy `src/authsvr/` to `src/examplesvr/`, keeping `protocol/`, `service/app/`, and business directories.
   Rename the service, main file, config message/header, config target, and export macro. Do not copy
   `*.atfw.gen.*` or the original service's business actions.
2. Declare `ExamplesvrClientService` and its messages in the shared `com.protocol.proto`,
   following the [RPC quick start](add-rpc-task). Change `AuthsvrClientService` to
   `ExamplesvrClientService` in the copied generation declaration.
3. Update the copied main's config section, environment prefix, generated header, and
   `register_handles_for_examplesvrclientservice()`. Keep its shared module/dispatcher initialization
   flow; attach new business modules afterward.
4. Add `add_subdirectory(examplesvr)` to `src/CMakeLists.txt`, reconfigure/build, implement the generated
   actions, and build again.
5. Copy `install/cloud-native/charts/authsvr/` for the new chart and rename its service/config section.
   Assign a unique `type_id` and set the corresponding `proc_name` and `type_name` in the new chart's `values.yaml`.
   If the service requires discovery by type, add an alias matching `type_name` in
   `src/server_frame/protocol/private/protocol/config/svr.hpa.config.proto`.
   Add instances to the active values' `non_cloud_native/deploy.yaml`.
6. [Generate configuration and start](../getting-started/run-deploy) the instance; check discovery registration and one normal RPC.

Use distinct service and config names. Keep values, discovery aliases, and process layout consistent.

## Quick Start: Declare a New SS RPC Service

For a new SS RPC group in an existing standard service, reuse its protocol target and fill the service name and
output locations in the existing helper. For the lobby, append the proto to `lobbysvr-protocol`'s
`PROTOCOLS` and declare:

```cmake
generate_for_pb_add_ss_service(
  "${PROJECT_NAMESPACE}.ExampleService"
  "${LOBBYSVR_ROOT_DIR}/service"
  TASK_PATH_PREFIX "logic"
  HANDLE_PATH_PREFIX "app"
  PROJECT_NAMESPACE "${PROJECT_NAMESPACE_ID}"
  RPC_ROOT_DIR "${LOBBYSVR_ROOT_DIR}/sdk"
  RPC_DLLEXPORT_DECL LOBBY_RPC_API
  EXTERNAL_SERVICE_PROTOCOLS "lobbysvr-protocol"
  INCLUDE_HEADERS "protocol/pbdesc/example_service.pb.h")
```

Add the same service full name to the service and SDK's existing `GENERATED_FLOW_NAMES`, and register
`register_handles_for_exampleservice()` in main. Subsequent methods need only proto and business action edits.
Extend the existing SDK header/source lists for the new API files.

## Trade Service Group

The index, matchmaking, order, price, and search processes under `src/tradesvr/` each use the SS RPC and config
schemas in `protocol/<role>/`. The `sdk::trade-sdk-<role>` target exposes the generated client API in `rpc::trade_<role>`.
Each SDK is declared in the `CMakeLists.txt` of its own subdirectory under `src/tradesvr/sdk/`.
Each role SDK links its RPC protocol and depends on `sdk::trade-common-sdk`. The common SDK links the shared trade
protocol, and its algorithms live under `src/tradesvr/sdk/common/trade/`.
The direct SDK dependencies for each process are declared in `src/tradesvr/service/CMakeLists.txt`.
Each process defines its own `main_module` in `service/<role>/app/trade_<role>_main.cpp` for handler registration
and process-specific lifecycle hooks. Request/response RPCs currently return `EN_ERR_NOT_IMPLEMENTED`.
One-way RPCs retain their no-response behavior. Add business logic in the role's `service/<role>/logic/action/` directory.

Each process has a chart under `install/cloud-native/charts/tradesvr-<role>/`. Group 9 in the default local deployment
defines the instances for each role. The type IDs match the `logic_discovery_selector_cfg` field numbers in
`svr.hpa.config.proto`. Order, matchmaking, index, search, and price use 51, 52, 53, 54, and 55, respectively.

Trade settings live in `install/cloud-native/values/default/modules/trade.yaml`. Charts that enable the trade module
render `trade.logic` as `logic.trade`, which is read as `logic_trade_cfg`. `trade.service.<role>` supplies each
role's settings, and `trade.service.common` is written to each role's `shared_setting`.
`trade.hpa.<role>` holds per-role HPA settings. Each process reads the rendered
`tradesvr_<role>` section and the `ATAPP_TRADESVR_<ROLE>` environment variable prefix.

The order service's custom rules are configured under `trade.hpa.order.rule.custom`.
`tgf_hpa_rule_search_cpu_rate` retains scale-up and scale-down thresholds of 300 and 250 and requires a matching
time series from the monitoring backend. `product_sales_order` defines the grouped order count aggregation,
with sample collection to be added in the order service implementation.

## Customization and Design

Shared service setup is declared in `src/server_frame/logic/logic_server_setup.h`;
see [RPC and code generation](../architecture/rpc-codegen) for generation rules.
Helpers organize service code into an executable and a private static library, with `app/` as the default
entry-point directory. Use project test helpers; see [RPC tests](rpc-unit-test) for service RPC paths.

For custom build, generation, or deployment behavior, read `src/service-functions.cmake`,
`src/tools/generate_for_pb_utility.cmake`, and `install/cloud-native/charts/libapp/`.
