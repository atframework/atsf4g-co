---
title: Excel Configuration Development
---

# Excel Configuration Development

Game-design numeric configuration flows through the pipeline: Excel → xresloader → binary → generated config
manager.

## Steps

1. **Define the config proto**: add a message in `com.struct.*.config.proto` under
   `src/server_frame/protocol/public/protocol/config/` (or on the private side), and annotate the
   `xrescode.loader` options (loader name, table name, indexes, etc.).
2. **Edit the Excel and conversion configuration**:
   - Excel files go in `resource/ExcelTables/`;
   - register the conversion rules for new tables in `resource/excel_xml/xresconv.xml.in`.
3. **Declare generation rules**: CMake (`generate_config_codes.cmake` / each service's CMakeLists) declares
   the `config_manager` / `config_set` / `config_easy_api` templates and custom field injection
   (`custom_group_fields.h.mako`, `custom_config_*_include_fields.h.mako`).
4. **Write an index class** (if fast lookup by key is needed): refer to
   `src/server_frame/config/src/excel_config_dtmq_index.cpp` (dtmq's `DChannelConfigure`).
5. **Loading and hot-reload**: at service startup, `excel_config_wrapper_reload_all`
   (`src/server_frame/config/src/excel_config_wrapper.cpp`) loads data grouped by loader/version; after a
   reload is triggered, hot-reload callbacks are invoked grouped by version.
6. **Reading**: use the convenient generated `config_easy_api` interfaces or reflective queries on the
   config manager.

## Reference Example

dtmq channel type configuration: `ExcelDtmqChannelType` in `com.struct.dtmq.config.proto`
(`channel_type` / `show_max_log_count` / `readonly_replicate_count`) → `resource/ExcelTables/DtMq.xlsx` →
`excel_config_dtmq_index.cpp`.

## Notes

- The conversion tool xresloader lives in `third_party/xresloader/` and is invoked automatically by the
  build process;
- Configuration reload and the YAML reload of `logic_config` are two independent channels—do not confuse
  them;
- On the deployment side, the Excel configuration path is specified in the `logic.excel` section of the
  instance configuration.
