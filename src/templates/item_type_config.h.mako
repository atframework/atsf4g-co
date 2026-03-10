#pragma once

#include <config/compiler_features.h>
#include <config/excel_type_trait_setting.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/common/com.struct.item_type.common.pb.h>

#include <config/compiler/protobuf_suffix.h>

struct ItemAlgorithmTypeOption {
 public:
  PROJECT_NAMESPACE_ID::EnItemType item_type;
  bool need_guid;
  bool need_occupy_the_grid;

  EXCEL_CONFIG_LOADER_API ItemAlgorithmTypeOption(PROJECT_NAMESPACE_ID::EnItemType type, int32_t begin, int32_t end, bool guid, bool occupy) {
    item_type = type;
    index_begin_ = begin;
    index_end_ = end;
    need_guid = guid;
    need_occupy_the_grid = occupy;
  }

  EXCEL_CONFIG_LOADER_API static const ItemAlgorithmTypeOption *GetItemType(int32_t item_id);
  EXCEL_CONFIG_LOADER_API static bool IsNeedGuid(int32_t item_id);

  EXCEL_CONFIG_LOADER_API bool operator<(const ItemAlgorithmTypeOption &other) const noexcept;
  int32_t index_begin_;
  int32_t index_end_;
};

namespace ItemTypeConfig {
% for raw_file_path in database.raw_files:
<%
      raw_file = database.raw_files[raw_file_path]
%>\
%     for enum_desc_proto in raw_file.enum_type:
<%
            if raw_file.package == '':
                  full_name = enum_desc_proto.name
            else:
                  full_name = raw_file.package + '.' + enum_desc_proto.name
            enum = database.get_enum(full_name)
            if enum == None:
                  continue
%>\
%           for enum_value in enum.values:
<%
                  item_type_option = enum_value.get_extension('prx.item_type_option')
                  if item_type_option == None:
                        continue
                  if len(item_type_option.item_type_range) == 0:
                        continue
%>\

EXCEL_CONFIG_LOADER_API bool is_${enum_value.get_name()}_range(int32_t type_id);
%           endfor
%     endfor
% endfor

EXCEL_CONFIG_LOADER_API bool is_item_type_invalid(int32_t type_id);

}  // namespace ItemTypeConfig