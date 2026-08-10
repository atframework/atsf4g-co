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

EXCEL_CONFIG_LOADER_API void normalize_dtmq_channel_configure(atfw::dtmq::DChannelConfigure& configure) {
  if (configure.gc_expire_duration().seconds() <= 0) {
    configure.mutable_gc_expire_duration()->set_seconds(86400 * 3650);  // 10 years
    configure.mutable_gc_expire_duration()->set_nanos(0);
  }

  if (configure.gc_log_count() <= 0) {
    configure.set_gc_log_count(30);
  }

  if (configure.max_log_count() <= 0) {
    configure.set_max_log_count(300);
  }

  if (configure.heartbeat_interval().seconds() <= 0) {
    configure.mutable_heartbeat_interval()->set_seconds(300);
    configure.mutable_heartbeat_interval()->set_nanos(0);
  }

  if (configure.heartbeat_retry_interval().seconds() <= 0) {
    configure.mutable_heartbeat_retry_interval()->set_seconds(60);
    configure.mutable_heartbeat_retry_interval()->set_nanos(0);
  }

  if (configure.subscriber_timeout().seconds() <= 0) {
    configure.mutable_subscriber_timeout()->set_seconds(configure.heartbeat_interval().seconds() +
                                                        configure.heartbeat_interval().seconds() +
                                                        configure.heartbeat_retry_interval().seconds());
    auto nanos = configure.heartbeat_interval().nanos() + configure.heartbeat_interval().nanos();
    if (nanos >= 1000000000) {
      configure.mutable_subscriber_timeout()->set_seconds(configure.subscriber_timeout().seconds() + 1);
      nanos -= 1000000000;
    }
    nanos += configure.heartbeat_retry_interval().nanos();
    if (nanos >= 1000000000) {
      configure.mutable_subscriber_timeout()->set_seconds(configure.subscriber_timeout().seconds() + 1);
      nanos -= 1000000000;
    }
    configure.mutable_subscriber_timeout()->set_nanos(nanos);
  }

  if (configure.shared_subscriber_gc_timeout().seconds() <= 0) {
    *configure.mutable_shared_subscriber_gc_timeout() = configure.subscriber_timeout();
  }
}

EXCEL_CONFIG_LOADER_API void setup_dtmq_config(config_group_t& group) {
  for (const auto& channel_type : group.ExcelDtmqChannelType.get_all_of_channel_type()) {
    if (!channel_type.second) {
      continue;
    }

    normalize_dtmq_channel_configure(
        *atfw::util::memory::const_pointer_cast<::PROJECT_NAMESPACE_ID::config::ExcelDtmqChannelType>(
             channel_type.second)
             ->mutable_channel_configure());

    auto& logic_config = group.dtmq_channel_type_configure[channel_type.second->channel_type()];
    logic_config = ::excel::excel_config_type_traits::make_shared<atfw::dtmq::DChannelConfigure>(
        channel_type.second->channel_configure());
    logic_config->set_channel_type(channel_type.second->channel_type());
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
