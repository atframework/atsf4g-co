// Copyright 2026 atframework

#include "config/excel_config_mail_index.h"

#include <common/string_oprs.h>
#include <log/log_wrapper.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/duration.pb.h>
#include <google/protobuf/message.h>
#include <google/protobuf/timestamp.pb.h>

#include <protocol/common/com.struct.mail.common.pb.h>
#include <protocol/config/com.struct.mail.config.pb.h>
#include <protocol/pbdesc/com.struct.mail.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>

#include <utility>

#include "config/excel/config_manager.h"

namespace excel {

namespace {
static void insert_mail_major_type(std::unordered_set<int32_t>& output, gsl::string_view input) {
  if (input.empty()) {
    return;
  }

  if (input[0] == 0) {
    return;
  }

  const auto* major_type_descriptor = ::PROJECT_NAMESPACE_ID::EnMailMajorType_descriptor();
  if (nullptr == major_type_descriptor) {
    return;
  }

  if (input[0] >= '0' && input[0] <= '9') {
    int32_t value = atfw::util::string::to_int<int32_t>(input);
    if (value > 0) {
      output.insert(value);
    } else {
      FWLOGERROR("[CONFIG] {} is not a valid mail major type", input);
    }
  } else {
    const auto* enum_value_desc = major_type_descriptor->FindValueByName(static_cast<std::string>(input));
    if (nullptr == enum_value_desc) {
      FWLOGERROR("[CONFIG] {} is not a valid mail major type", input);
      return;
    }

    if (enum_value_desc->number() > 0) {
      output.insert(enum_value_desc->number());
    }
  }
}
}  // namespace

EXCEL_CONFIG_LOADER_API void setup_mail_config(config_group_t& group) {
  const auto* major_type_descriptor = ::PROJECT_NAMESPACE_ID::EnMailMajorType_descriptor();
  if (nullptr != major_type_descriptor) {
    for (int i = 0; i < major_type_descriptor->value_count(); ++i) {
      if (major_type_descriptor->value(i)->number() <= 0) {
        continue;
      }
      group.mail_user_major_types.insert(major_type_descriptor->value(i)->number());
    }
  }

  for (const auto& custom_major_type : logic_config::me()->get_logic_cfg().mail().user_mail_major_types()) {
    auto type_name_or_number = atfw::util::string::trim(custom_major_type.c_str(), custom_major_type.size());
    insert_mail_major_type(group.mail_user_major_types,
                           gsl::string_view{type_name_or_number.first, type_name_or_number.second});
  }

  for (const auto& custom_major_type : logic_config::me()->get_logic_cfg().mail().global_mail_major_types()) {
    auto type_name_or_number = atfw::util::string::trim(custom_major_type.c_str(), custom_major_type.size());
    insert_mail_major_type(group.mail_global_major_types,
                           gsl::string_view{type_name_or_number.first, type_name_or_number.second});
  }

  // Fallback empty configure
  if (group.mail_global_major_types.empty()) {
    group.mail_global_major_types.insert(::PROJECT_NAMESPACE_ID::EN_MAIL_MT_SYSTEM_ANNOUNCEMENT);
    group.mail_global_major_types.insert(::PROJECT_NAMESPACE_ID::EN_MAIL_MT_SYSTEM_BUSINESS);
    group.mail_global_major_types.insert(::PROJECT_NAMESPACE_ID::EN_MAIL_MT_SYSTEM_ACTIVITY);
    group.mail_global_major_types.insert(::PROJECT_NAMESPACE_ID::EN_MAIL_MT_SYSTEM_COMPENSATION);
  }

  // Copy available global mail types into user mail types
  for (auto custom_major_type : group.mail_global_major_types) {
    group.mail_user_major_types.insert(custom_major_type);
  }
}

namespace mail {

EXCEL_CONFIG_LOADER_API bool is_valid_user_mail(const excel_config_type_traits::shared_ptr<config_group_t>& group,
                                                int32_t major_type) noexcept {
  if (!group) {
    return false;
  }

  if (major_type <= 0) {
    return false;
  }

  return group->mail_user_major_types.end() != group->mail_user_major_types.find(major_type);
}

EXCEL_CONFIG_LOADER_API bool is_valid_user_mail(int32_t major_type) noexcept {
  auto group = config_manager::me()->get_current_config_group();
  if (!group) {
    return false;
  }

  return is_valid_user_mail(group, major_type);
}

EXCEL_CONFIG_LOADER_API
std::pair<excel_config_type_traits::shared_ptr<config_group_t>, std::reference_wrapper<std::unordered_set<int32_t>>>
get_all_user_mail_major_types() noexcept {
  auto group = config_manager::me()->get_current_config_group();
  if (!group) {
    static std::unordered_set<int32_t> empty;
    return {nullptr, empty};
  }

  return {group, group->mail_user_major_types};
}

EXCEL_CONFIG_LOADER_API bool is_valid_global_mail(const excel_config_type_traits::shared_ptr<config_group_t>& group,
                                                  int32_t major_type) noexcept {
  if (!group) {
    return false;
  }

  if (major_type <= 0) {
    return false;
  }

  return group->mail_global_major_types.end() != group->mail_global_major_types.find(major_type);
}

EXCEL_CONFIG_LOADER_API bool is_valid_global_mail(int32_t major_type) noexcept {
  auto group = config_manager::me()->get_current_config_group();
  if (!group) {
    return false;
  }

  return is_valid_global_mail(group, major_type);
}

EXCEL_CONFIG_LOADER_API
std::pair<excel_config_type_traits::shared_ptr<config_group_t>, std::reference_wrapper<std::unordered_set<int32_t>>>
get_all_global_mail_major_types() noexcept {
  auto group = config_manager::me()->get_current_config_group();
  if (!group) {
    static std::unordered_set<int32_t> empty;
    return {nullptr, empty};
  }

  return {group, group->mail_global_major_types};
}

}  // namespace mail

}  // namespace excel
