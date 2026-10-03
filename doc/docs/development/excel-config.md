---
title: Excel 配置开发
---

# Excel 配置开发

Excel → xresloader → 二进制资源 → 配置加载器和读取 API。常规表格、字段和索引扩展只需声明配置。

## 快速上手：修改已有表格

1. 编辑 `resource/ExcelTables/` 中的表格，保留表头、字段名和数据起始行约定。
2. 使用已有构建目录导出资源：

```bash
cmake --build <BUILD_DIR> --config Debug --target resource-config
```

3. 确认转换成功，检查 `<BUILD_DIR>/publish/resource/excel/` 的对应资源。
4. 发布新资源；按实例配置的 `logic.excel` 资源路径加载，执行实例 reload 并验证业务读取值。
   资源 reload 会更新配置分组；具体业务的派生缓存还需遵循该模块的 reload 逻辑。

只改数值不需要修改 proto、CMake 或模板。

## 快速上手：增加表格或字段

1. Excel 放入 `resource/ExcelTables/`。参照 `Item.xlsx` 的“道具描述总表”：
   第 2 行字段名，第 3 行开始数据。
2. 在 `src/server_frame/protocol/public/protocol/config/` 新增配置 proto，
   沿用相邻文件的 package 与 import。下面是一个完整的最小声明：

```protobuf
syntax = "proto3";
package atframework.shared.config;
import "protocol/extension/xrescode_extensions_v3.proto";

message ExcelExample {
  option (xrescode.loader) = {
    file_path: "example.bytes"
    indexes: { fields: "id" index_type: EN_INDEX_KV }
    tags: "all"
  };
  uint32 id = 1;
  string name = 2;
}
```

3. 在 **`src/server_frame/protocol/public/xresconv.xml`** 的 `<list>` 中添加转换项：

```xml
<item name="Example" cat="misc" class="all">
  <scheme name="DataSource">Example.xlsx|Sheet1|3,1</scheme>
  <scheme name="ProtoName">atframework.shared.config.ExcelExample</scheme>
  <scheme name="OutputFile">Both/example.bytes</scheme>
</item>
```

   `DataSource` 填实际工作簿、工作表与数据起始位置；`OutputFile` 沿用现有 `Both/` 布局，
   loader 的 `file_path` 填文件名。全局输出目录和工具路径由已有配置提供，无需修改
   `resource/excel_xml/xresconv.xml.in`。
4. 按[统一构建步骤](overview)重新配置并构建，自动生成加载器、索引与读取接口。
   新文件要重新配置；已有表新增字段则同时更新 proto 和 Excel 表头。
5. 包含生成的 `config/excel/config_easy_api.h`，按其中的新接口读取配置，再按前一节发布并验证。

## 索引与热更新

普通按键查询在 `xrescode.loader.indexes` 中声明即可，不需要手写索引类。
表格主键的唯一性、关联字段和验证器可参照
`com.struct.item.config.proto` 与 `src/server_frame/protocol/public/validator.yaml`。

配置由 `excel_config_wrapper_reload_all(true)` 首次加载，reload 调用
`excel_config_wrapper_reload_all(false)`。YAML 和 Excel 是两类配置输入，服务公共 reload 流程会处理两者。

## 定制与详细设计

需要额外派生数据、跨表预处理或专用查询时，再参照
`src/server_frame/config/src/excel_config_dtmq_index.cpp` 和
`src/templates/custom_*fields.h.mako` 的对应文件。
加载与版本分组见[配置系统](../architecture/configuration)；
生成机制见 `src/server_frame/generate_config_codes.cmake`。
