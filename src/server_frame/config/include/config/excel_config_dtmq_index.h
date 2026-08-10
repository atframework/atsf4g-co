// Copyright 2026 atframework

#pragma once

#include <config/server_frame_build_feature.h>

#include <gsl/select-gsl.h>

#include "config/excel_type_trait_setting.h"

namespace atframework {
namespace dtmq {
class DChannelConfigure;
}  // namespace dtmq
}  // namespace atframework

namespace excel {
struct config_group_t;
EXCEL_CONFIG_LOADER_API void setup_dtmq_config(config_group_t& group);

EXCEL_CONFIG_LOADER_API void normalize_dtmq_channel_configure(atfw::dtmq::DChannelConfigure& configure);

EXCEL_CONFIG_LOADER_API ::excel::excel_config_type_traits::shared_ptr<atfw::dtmq::DChannelConfigure>
get_dtmq_channel_configure(uint32_t channel_type);
}  // namespace excel
