---
title: Adding a Service
---

# Adding a Service

Use `echosvr` as the minimal template (just a single `app/echosvr_main.cpp`); refer to `lobbysvr` for a
standard service.

## Steps

1. **Define the protocol**: add services and messages in `src/server_frame/protocol/` (or the component's
   own `protocol/`); annotate `module_name` / `api_name` / `allow_no_wait` with
   `atframework.service_options` / `rpc_options`.
2. **Declare generation rules**: in the service's `CMakeLists.txt`, use `generate_for_pb_add_ss_service` (SS RPC) /
   `generate_for_pb_add_cs_service` (CS RPC) (see `src/tools/generate_for_pb_utility.cmake`) to declare: proto
   files + templates (`handle_ss_rpc` / `task_action_ss_rpc` / `rpc_call_api_for_ss` / CS variants) + output paths.
3. **Write main**: follow `src/lobbysvr/service/app/lobbysvr_main.cpp`:

```cpp
int main(int argc, char *argv[]) {
  atfw::atapp::app app;
  // logic_config injects this service's configuration loading callbacks
  project::logic_server_setup_common(app);
  // Register the generated RPC handlers
  register_handles_for_<yourservice>();
  // Mount dispatchers and business modules
  app.add_module(atfw::cs_msg_dispatcher::me());
  app.add_module(atfw::ss_msg_dispatcher::me());
  app.add_module(atfw::db_msg_dispatcher::me());
  app.add_module(<business module>);
  return app.run(uv_default_loop(), argc, argv, nullptr);
}
```

4. **Fill in task actions**: the generator produces `logic/action/task_action_*` skeletons; write business
   logic in `operator()`.
5. **Add to the build**: `add_subdirectory(<name>svr)` in `src/CMakeLists.txt`.
6. **Deployment**: add the service chart under `install/cloud-native/charts/` (you can copy an existing
   service), and supplement parameters in each profile under `values/`; for local runs, supplement the
   `publish/tools/script/config.conf` template.

## Notes

- Generated artifacts (`*.atfw.gen.*`) must not be edited manually; regeneration is triggered by
  proto/template changes.
- Module assembly order follows the conventions of `logic_server_setup_common`; mount custom modules after
  the dispatcher when they depend on it.
- New services are included in all builds by default, with no separate switch.
