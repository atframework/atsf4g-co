// Copyright 2021 atframework

#pragma once

#include <design_pattern/nomovable.h>
#include <design_pattern/noncopyable.h>

#include <functional>

namespace excel {
struct config_group_t;
}

struct excel_config_block_report_t {
  excel_config_block_report_t();
  ~excel_config_block_report_t();

  UTIL_DESIGN_PATTERN_NOCOPYABLE(excel_config_block_report_t)
  UTIL_DESIGN_PATTERN_NOMOVABLE(excel_config_block_report_t)
};

int excel_config_wrapper_reload_all(bool is_init);

/**
 * @brief 设置配置组加载完后的回调，请在init流程中excel_config_wrapper_reload_all(true)前调用
 */
void excel_add_on_group_loaded_callback(std::function<void(excel::config_group_t&)> fn);
