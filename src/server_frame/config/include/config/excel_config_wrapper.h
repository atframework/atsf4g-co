// Copyright 2021 atframework

#pragma once

#include <design_pattern/nomovable.h>
#include <design_pattern/noncopyable.h>

#include <config/server_frame_build_feature.h>

#include <functional>

namespace excel {
struct config_group_t;
}

struct excel_config_block_report_t {
  SERVER_FRAME_CONFIG_API excel_config_block_report_t();
  SERVER_FRAME_CONFIG_API ~excel_config_block_report_t();

  UTIL_DESIGN_PATTERN_NOCOPYABLE(excel_config_block_report_t)
  UTIL_DESIGN_PATTERN_NOMOVABLE(excel_config_block_report_t)
};

SERVER_FRAME_CONFIG_API int excel_config_wrapper_reload_all(bool is_init);

SERVER_FRAME_CONFIG_API int excel_config_wrapper_reload_all(bool is_init);

/**
 * @brief 设置配置组加载完后的回调，请在init流程中excel_config_wrapper_reload_all(true)前调用
 */
SERVER_FRAME_CONFIG_API void excel_add_on_group_loaded_callback(std::function<void(excel::config_group_t&)> fn);
