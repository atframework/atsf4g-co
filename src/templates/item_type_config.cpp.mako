## -*- coding: utf-8 -*-
<%!
import time
%><%namespace name="pb_loader" module="pb_loader"/>
// Copyright ${time.strftime("%Y")}. All rights reserved.
// @brief Created by ${generator}, please don't edit it
#include "config/excel/item_type_config.h"

#include "config/excel/config_manager.h"

EXCEL_CONFIG_LOADER_API bool ItemAlgorithmTypeOption::operator<(const ItemAlgorithmTypeOption &other) const noexcept {
  if (index_begin_ != other.index_begin_) {
    return index_begin_ < other.index_begin_;
  }
  return index_end_ < other.index_end_;
}

static void InitItemTypeCache(std::vector<ItemAlgorithmTypeOption> &data) {
  data.clear();
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
%                 for range in item_type_option.item_type_range:
<%
                        need_guid = 'false'
                        need_occupy_the_grid = 'false'
                        if item_type_option.need_guid:
                              need_guid = 'true'
                        if item_type_option.need_occupy_the_grid:
                              need_occupy_the_grid = 'true'
%>\
  data.emplace_back(PROJECT_NAMESPACE_ID::${enum_value.get_name()}, ${range.start_index}, ${range.end_index}, ${need_guid}, ${need_occupy_the_grid});
%                 endfor
%           endfor
%     endfor
% endfor
  std::sort(data.begin(), data.end());
}

EXCEL_CONFIG_LOADER_API const ItemAlgorithmTypeOption *ItemAlgorithmTypeOption::GetItemType(int32_t item_id) {
  static std::vector<ItemAlgorithmTypeOption> item_type_cache;
  if ATFW_UTIL_UNLIKELY_CONDITION (item_type_cache.empty()) {
    InitItemTypeCache(item_type_cache);
  }

  auto iter = std::upper_bound(item_type_cache.begin(), item_type_cache.end(), item_id,
                               [](int32_t l, const ItemAlgorithmTypeOption &r) { return l < r.index_end_; });
  if (iter == item_type_cache.end()) {
    return nullptr;
  }

  if (item_id < (*iter).index_begin_) {
    return nullptr;
  }

  return &(*iter);
}

EXCEL_CONFIG_LOADER_API bool ItemAlgorithmTypeOption::IsNeedGuid(int32_t item_id) {
  auto option = GetItemType(item_id);
  if (option == nullptr) {
    return false;
  }
  return option->need_guid;
}

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
EXCEL_CONFIG_LOADER_API bool is_${enum_value.get_name()}_range(int32_t type_id) {
  bool ret = false;
%                 for range in item_type_option.item_type_range:
  ret |= type_id >= ${range.start_index} && type_id < ${range.end_index};
%                 endfor
  return ret;
}
%           endfor
%     endfor
% endfor

EXCEL_CONFIG_LOADER_API bool is_item_type_invalid(int32_t type_id) {
  auto group = excel::config_manager::me()->get_current_config_group();
  if (!group) {
    return false;
  }
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
                item_excel_option = enum_value.get_extension('prx.item_excel_option')
                if item_type_option == None:
                      continue
                if item_excel_option == None:
                      continue
                if len(item_type_option.item_type_range) == 0:
                      continue
                if len(item_excel_option.item_excel_sheet) == 0:
                      continue
                if len(item_excel_option.item_excel_key) == 0:
                      continue
%>\
%     for range in item_type_option.item_type_range:

  if(type_id >= ${range.start_index} && type_id < ${range.end_index}){
    auto cfg = group->${item_excel_option.item_excel_sheet}.get_by_${item_excel_option.item_excel_key}(type_id);
    if(cfg == nullptr){
      return false;
    }
    return true;
  }
%                 endfor
%           endfor
%     endfor
% endfor

  auto cfg = group->ExcelItemType.get_by_type_id(type_id);
  if(cfg != nullptr){
    return true;
  }
  return false;
}

}  // namespace ItemTypeConfig