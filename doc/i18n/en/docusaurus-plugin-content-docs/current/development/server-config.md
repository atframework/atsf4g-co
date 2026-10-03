---
title: Server Configuration
---

# Server Configuration

## Quick Start: Change Existing Configuration

1. Place personal overrides in `install/cloud-native/values/personal/`, following the existing file layout.
   `default` provides base values, `dev` provides development overrides, and local scripts load `personal` last.
2. Use `proc_desc` in `non_cloud_native/deploy.yaml` for instance counts and process layout, and the
   corresponding files under `modules/` for shared configuration such as etcd and Redis.
3. Build to refresh `<BUILD_DIR>/publish/cloud-native/`, then run
   `publish/tools/script/generate_config.sh` or `generate_config.ps1`. You can also edit published values
   temporarily and render them directly, but a subsequent build may overwrite those edits.
4. Inspect the generated `<server>/cfg/*_<bus_id>.yaml` for addresses, ports, service types, and resource paths.
5. For reloadable fields, run the instance's `reload_<bus_id>.sh/.ps1`. Restart after changing startup settings
   such as process identity or listener addresses. Do not assume every field supports hot reload.

See [run and deploy](../getting-started/run-deploy) for startup commands.

## Quick Start: Add a Configuration Field

1. Shared logic configuration lives under `src/server_frame/protocol/private/protocol/config/`.
   For service-specific configuration, use its existing config proto, for example
   `src/authsvr/protocol/protocol/config/authsvr_config.proto`.
2. Add a field to the existing message, following neighboring `atapp` annotations, defaults, units, and
   validation ranges, then rebuild.
3. Forward the values parameter in the corresponding chart's `cfg/*.yaml.tpl` and add its default in values.
   Existing configuration keys need no template changes.
4. Read the configuration in business code. An existing service loader already parses its configuration type;
   adjust the loading callback only for a new section or service.
5. Render YAML and verify defaults and overrides. If reload is required, verify the business reload behavior too.

## Customization and Design

See [configuration](../architecture/configuration) for loading and the distinction between Excel and YAML.
For custom deployment output, use the shared templates in `install/cloud-native/charts/libapp/`.
Expression annotations are defined in `atframework/libatapp/include/atframe/atapp_conf.proto`.
