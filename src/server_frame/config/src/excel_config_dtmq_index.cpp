// Copyright 2026 atframework

#include "config/excel_config_dtmq_index.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/duration.pb.h>

#include <protocol/config/com.struct.dtmq.config.pb.h>
#include <protocol/pbdesc/com.struct.dtmq.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include "config/excel/config_manager.h"

namespace excel {

EXCEL_CONFIG_LOADER_API void setup_dtmq_config(config_group_t& group) {
  for (const auto& channel_type : group.ExcelDtmqChannelType.get_all_of_channel_type()) {
    if (!channel_type.second) {
      continue;
    }

    auto& logic_config = group.dtmq_channel_type_configure[channel_type.second->channel_type()];
    logic_config = ::excel::excel_config_type_traits::make_shared<atfw::dtmq::DChannelConfigure>();
    logic_config->set_channel_type(channel_type.second->channel_type());
    logic_config->set_max_log_count(channel_type.second->max_log_count());
    logic_config->set_gc_log_count(channel_type.second->gc_log_count());
    *logic_config->mutable_gc_expire_duration() = channel_type.second->gc_expire_duration();
    logic_config->set_memory_only(channel_type.second->memory_only());

    logic_config->set_memory_only(channel_type.second->memory_only());
    logic_config->set_word_count_limit(channel_type.second->word_count_limit());
  }
}

EXCEL_CONFIG_LOADER_API ::excel::excel_config_type_traits::shared_ptr<atfw::dtmq::DChannelConfigure>
get_dtmq_channel_configure(uint32_t channel_type) {
  auto group = config_manager::me()->get_current_config_group();
  if (!group) {
    return nullptr;
  }

  auto iter = group->dtmq_channel_type_configure.find(channel_type);
  if (iter == group->dtmq_channel_type_configure.end()) {
    return nullptr;
  }

  return iter->second;
}

}  // namespace excel
