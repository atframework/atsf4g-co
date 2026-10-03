---
title: Excel Configuration
---

# Excel Configuration

Excel → xresloader → binary resources → loaders and read APIs. Ordinary table, field, and index extensions use declarations.

## Quick Start: Change an Existing Workbook

1. Edit the workbook under `resource/ExcelTables/`, preserving headers, field names, and data start rows.
2. Export resources using the existing build directory:

```bash
cmake --build <BUILD_DIR> --config Debug --target resource-config
```

3. Check conversion success and the corresponding files under `<BUILD_DIR>/publish/resource/excel/`.
4. Publish the resources, load them from the instance's `logic.excel` resource path, reload the instance, and
   verify business reads. Resource reload updates config groups; derived business caches also follow their module's reload logic.

Value-only edits need no proto, CMake, or template changes.

## Quick Start: Add a Workbook or Field

1. Put the workbook under `resource/ExcelTables/`. Follow the “道具描述总表” sheet in `Item.xlsx`:
   field names in row 2 and data from row 3.
2. Add a config proto under `src/server_frame/protocol/public/protocol/config/`, following neighboring
   packages and imports. A complete minimal declaration is:

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

3. Add a conversion entry inside `<list>` in **`src/server_frame/protocol/public/xresconv.xml`**:

```xml
<item name="Example" cat="misc" class="all">
  <scheme name="DataSource">Example.xlsx|Sheet1|3,1</scheme>
  <scheme name="ProtoName">atframework.shared.config.ExcelExample</scheme>
  <scheme name="OutputFile">Both/example.bytes</scheme>
</item>
```

   Set `DataSource` to the actual workbook, sheet, and starting cell. Keep the existing `Both/` output layout,
   with just the filename in the loader's `file_path`. The existing global configuration supplies tool paths and
   the output directory; no edit to `resource/excel_xml/xresconv.xml.in` is needed.
4. [Reconfigure and build](overview) to generate loaders, indexes, and read APIs.
   Reconfigure for new files; when adding a field to an existing table, update both proto and Excel headers.
5. Include the generated `config/excel/config_easy_api.h`, use its new read API, then publish and validate as above.

## Indexes and Reload

Declare ordinary key lookups in `xrescode.loader.indexes`; handwritten index classes are not required.
For uniqueness, related fields, and validators, follow `com.struct.item.config.proto` and
`src/server_frame/protocol/public/validator.yaml`.

Initial loading calls `excel_config_wrapper_reload_all(true)`; reload calls
`excel_config_wrapper_reload_all(false)`. YAML and Excel are separate inputs, both handled by the shared service reload flow.

## Customization and Design

For derived data, cross-table preprocessing, or specialized queries, consult
`src/server_frame/config/src/excel_config_dtmq_index.cpp` and the corresponding
`src/templates/custom_*fields.h.mako` files.
See [configuration](../architecture/configuration) for loading and version groups, and
`src/server_frame/generate_config_codes.cmake` for generation internals.
