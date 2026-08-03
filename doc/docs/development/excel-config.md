---
title: Excel 配置开发
---

# Excel 配置开发

策划数值配置走 Excel → xresloader → 二进制 → 生成的 config manager 的链路。

## 步骤

1. **定义配置 proto**：在 `src/server_frame/protocol/public/protocol/config/`（或 private 侧）的
   `com.struct.*.config.proto` 中添加 message，标注 `xrescode.loader` 选项（loader 名、表名、索引等）。
2. **编辑 Excel 与转换配置**：
   - Excel 放 `resource/ExcelTables/`；
   - `resource/excel_xml/xresconv.xml.in` 注册新表的转换规则。
3. **声明生成规则**：CMake（`generate_config_codes.cmake` / 各服务 CMakeLists）声明
   `config_manager` / `config_set` / `config_easy_api` 模板与自定义字段注入
   （`custom_group_fields.h.mako`、`custom_config_*_include_fields.h.mako`）。
4. **写索引类**（如需按 key 快速查询）：参照
   `src/server_frame/config/src/excel_config_dtmq_index.cpp`（dtmq 的 `DChannelConfigure`）。
5. **加载与热更**：服务启动时 `excel_config_wrapper_reload_all`（
   `src/server_frame/config/src/excel_config_wrapper.cpp`）按 loader/version 分组加载；reload 触发后
   按 version 分组回调热更。
6. **读取**：用生成的 `config_easy_api` 便捷接口或 config manager 反射查询。

## 参考实例

dtmq 频道类型配置：`com.struct.dtmq.config.proto` 的 `ExcelDtmqChannelType`（`channel_type` /
`show_max_log_count` / `readonly_replicate_count`）→ `resource/ExcelTables/DtMq.xlsx` →
`excel_config_dtmq_index.cpp`。

## 注意

- 转换工具 xresloader 在 `third_party/xresloader/`，由构建流程自动调用；
- 配置 reload 与 `logic_config` 的 YAML reload 是两条独立通道，注意区分；
- 部署侧 Excel 配置路径在实例配置的 `logic.excel` 段指定。
