// Copyright 2021 atframework

#pragma once

#include <design_pattern/nomovable.h>
#include <design_pattern/noncopyable.h>

#include <config/server_frame_build_feature.h>

#include <functional>

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
#  include <string>
#endif

namespace excel {
struct config_group_t;
}

struct excel_config_block_report_t {
  EXCEL_CONFIG_LOADER_API excel_config_block_report_t();
  EXCEL_CONFIG_LOADER_API ~excel_config_block_report_t();

  UTIL_DESIGN_PATTERN_NOCOPYABLE(excel_config_block_report_t)
  UTIL_DESIGN_PATTERN_NOMOVABLE(excel_config_block_report_t)
};

EXCEL_CONFIG_LOADER_API int excel_config_wrapper_reload_all(bool is_init);

/**
 * @brief 设置配置组加载完后的回调，请在init流程中excel_config_wrapper_reload_all(true)前调用
 */
EXCEL_CONFIG_LOADER_API void excel_add_on_group_loaded_callback(std::function<void(excel::config_group_t&)> fn);

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
// Scoped resource provider seam for unit tests. When installed, the excel config buffer/version callbacks consult
// this provider first and never touch the filesystem loader. Installing and clearing is fully reversible; teardown
// must call clear_excel_resource_provider_for_unit_test() so the next fixture sees no leftover bytes/version.
struct excel_resource_provider_for_unit_test_t {
  std::function<bool(std::string& out, const char* path)> get_buffer;
  std::function<bool(std::string& out)> get_version;
};

EXCEL_CONFIG_LOADER_API void set_excel_resource_provider_for_unit_test(
    excel_resource_provider_for_unit_test_t provider);
EXCEL_CONFIG_LOADER_API void clear_excel_resource_provider_for_unit_test();
#endif
