// Copyright 2021 atframework

#include "config/excel_config_const_index.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/duration.pb.h>
#include <google/protobuf/message.h>
#include <google/protobuf/timestamp.pb.h>
#include <protocol/config/com.const.config.pb.h>
#include <protocol/extension/v3/xresloader.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <std/explicit_declare.h>

#include <common/string_oprs.h>
#include <log/log_wrapper.h>
#include <memory/rc_ptr.h>
#include <time/time_utility.h>

#include <cli/cmd_option.h>

#include <algorithm>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "config/excel/config_manager.h"

namespace excel {

SERVER_FRAME_CONFIG_API void setup_const_config(config_group_t& group) {
	auto& all_key = group.ExcelConstConfig.get_all_of_fake_key();
	if (all_key.size() != 1)
	{
		FWLOGERROR("[EXCEL] setup_const_config key not 1");
		return;
	}

	for (auto& key : all_key)
	{
		group.const_settings = *key.second;
	}
}

SERVER_FRAME_CONFIG_API const ::PROJECT_NAMESPACE_ID::config::ExcelConstConfig& get_const_config() {
  auto group = config_manager::me()->get_current_config_group();
  if (!group) {
    return ::PROJECT_NAMESPACE_ID::config::ExcelConstConfig::default_instance();
  }

  return group->const_settings;
}

}  // namespace excel
