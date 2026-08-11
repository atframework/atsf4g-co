// Copyright 2021 atframework

#include "config/excel_config_wrapper.h"

#include <common/file_system.h>
#include <common/string_oprs.h>

#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <config/compiler/protobuf_prefix.h>

#include <google/protobuf/reflection.h>

#include <config/compiler/protobuf_suffix.h>

#include <atomic>
#include <list>
#include <string>
#include <utility>

#include "config/server_frame_build_feature.h"

#include "config/excel_config_dtmq_index.h"
#include "config/excel_config_rank_index.h"

#include "config/excel/config_manager.h"
#include "config/excel_config_const_index.h"
#include "config/logic_config.h"

namespace {
static std::list<std::function<void(excel::config_group_t&)> >& get_excel_on_group_loaded_fns() {
  static std::list<std::function<void(excel::config_group_t&)> > ret;
  return ret;
}

static bool& get_excel_config_manager_inited() {
  static bool ret = false;
  return ret;
}

static std::atomic<int64_t>& get_excel_reporter_blocker() {
  static std::atomic<int64_t> ret{0};
  return ret;
}

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
static excel_resource_provider_for_unit_test_t& get_excel_resource_provider_for_unit_test() {
  static excel_resource_provider_for_unit_test_t ret;
  return ret;
}
#endif

static bool excel_config_callback_get_buffer(std::string& out, const char* path) {
#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
  if (get_excel_resource_provider_for_unit_test().get_buffer) {
    return get_excel_resource_provider_for_unit_test().get_buffer(out, path);
  }
#endif
  char file_path[atfw::util::file_system::MAX_PATH_LEN + 1];
  for (const std::string& bindir : logic_config::me()->get_logic_cfg().excel().bindir()) {
    int res = UTIL_STRFUNC_SNPRINTF(file_path, sizeof(file_path) - 1, "%s%c%s", bindir.c_str(),
                                    atfw::util::file_system::DIRECTORY_SEPARATOR, path);
    if (res > 0 && static_cast<size_t>(res) < atfw::util::file_system::MAX_PATH_LEN) {
      file_path[res] = 0;
    } else {
      return false;
    }

    if (atfw::util::file_system::is_exist(file_path)) {
      return atfw::util::file_system::get_file_content(out, file_path, true);
    }
  }

  return false;
}

static bool excel_config_callback_get_version(std::string& out) {
#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
  if (get_excel_resource_provider_for_unit_test().get_version) {
    return get_excel_resource_provider_for_unit_test().get_version(out);
  }
#endif
  char file_path[atfw::util::file_system::MAX_PATH_LEN + 1];
  out = "0.0.0.0";
  for (const std::string& bindir : logic_config::me()->get_logic_cfg().excel().bindir()) {
    int res = UTIL_STRFUNC_SNPRINTF(file_path, sizeof(file_path) - 1, "%s%c%s", bindir.c_str(),
                                    atfw::util::file_system::DIRECTORY_SEPARATOR, "version.txt");
    if (res > 0 && static_cast<size_t>(res) < atfw::util::file_system::MAX_PATH_LEN) {
      file_path[res] = 0;
    } else {
      return false;
    }

    if (atfw::util::file_system::is_exist(file_path)) {
      std::string buffer;
      if (atfw::util::file_system::get_file_content(buffer, file_path, true)) {
        std::pair<const char*, size_t> ver = atfw::util::string::trim(buffer.c_str(), buffer.size());
        out.assign(ver.first, ver.second);
        break;
      }
    }
  }

  return true;
}

// NOLINTNEXTLINE(performance-unnecessary-value-param)
static void excel_config_callback_on_reload_all(excel::config_manager::config_group_ptr_t group) {
  if (!group) {
    FWLOGERROR("excel config group error");
    return;
  }

  setup_rank_config(*group);
  setup_dtmq_config(*group);

  // 自定义跨表索引在这之后初始化
  setup_const_config(*group);
}

static void excel_config_callback_logger(const excel::config_manager::log_caller_info_t& caller, const char* content) {
  // switch(caller.)
  atfw::util::log::log_wrapper::caller_info_t log_caller;
  log_caller.file_path = caller.file_path;
  log_caller.func_name = caller.func_name;
  log_caller.level_name = caller.level_name;
  log_caller.line_number = caller.line_number;
  log_caller.rotate_index = 0;
  switch (caller.level_id) {
    case excel::config_manager::log_level_t::LOG_LW_DISABLED: {
      log_caller.level_id = atfw::util::log::log_level::kDisabled;
      break;
    }
    case excel::config_manager::log_level_t::LOG_LW_ERROR: {
      log_caller.level_id = atfw::util::log::log_level::kError;
      break;
    }
    case excel::config_manager::log_level_t::LOG_LW_WARNING: {
      log_caller.level_id = atfw::util::log::log_level::kWarning;
      break;
    }
    case excel::config_manager::log_level_t::LOG_LW_INFO: {
      log_caller.level_id = atfw::util::log::log_level::kInfo;
      break;
    }
    default: {
      log_caller.level_id = atfw::util::log::log_level::kDebug;
      break;
    }
  }

  if (util::log::log_wrapper::check_level(WDTLOGGETCAT(util::log::log_wrapper::categorize_t::DEFAULT),
                                          log_caller.level_id)) {
    WDTLOGGETCAT(util::log::log_wrapper::categorize_t::DEFAULT)->format_log(log_caller, "{}", content);
  }
}
// Install the bindir-aware buffer/version loaders, the log callback and the on-group-reload hook chain
// exactly once per process. excel_config_wrapper_reload_all installs them even when excel is disabled,
// because code may still lazily load tables through the excel bindir (e.g. unit tests that expose the
// real excel bindir without the resource feature), and the generated default file loader does not
// search the bindir on its own.
static void excel_config_wrapper_setup_loader_and_hooks_once() {
  if (get_excel_config_manager_inited()) {
    return;
  }

  int res = ::excel::config_manager::me()->init(false);
  if (res < 0) {
    FWLOGERROR("excel::config_manager init failed, res: {}", res);
    return;
  }

  excel::config_manager::me()->set_buffer_loader(excel_config_callback_get_buffer);
  excel::config_manager::me()->set_version_loader(excel_config_callback_get_version);
  excel::config_manager::me()->set_on_log(excel_config_callback_logger);

  excel::config_manager::on_load_func_t origin_reload_callback =
      excel::config_manager::me()->get_on_group_reload_all();
  excel::config_manager::me()->set_on_group_reload_all(
      // NOLINTNEXTLINE(performance-unnecessary-value-param)
      [origin_reload_callback](excel::config_manager::config_group_ptr_t group) {
        if (origin_reload_callback) {
          origin_reload_callback(group);
        }
        excel_config_callback_on_reload_all(group);

        if (group) {
          for (auto& fn : get_excel_on_group_loaded_fns()) {
            fn(*group);
          }
        }
      });
  get_excel_config_manager_inited() = true;
}
}  // namespace

SERVER_FRAME_CONFIG_API excel_config_block_report_t::excel_config_block_report_t() { ++get_excel_reporter_blocker(); }

SERVER_FRAME_CONFIG_API excel_config_block_report_t::~excel_config_block_report_t() { --get_excel_reporter_blocker(); }

SERVER_FRAME_CONFIG_API int excel_config_wrapper_reload_all(bool is_init) {
  if (!get_excel_config_manager_inited() && !is_init) {
    return 0;
  }

  // Install the bindir-aware loaders and the group-loaded hook chain whenever initialization is
  // requested, even if excel is disabled: code may still lazily load tables through the excel bindir
  // (e.g. unit tests that expose the real excel bindir without the resource feature), and the
  // generated default file loader does not search the bindir on its own.
  excel_config_wrapper_setup_loader_and_hooks_once();
  if (!get_excel_config_manager_inited()) {
    FWLOGERROR("excel::config_manager init failed");
    return -1;
  }

  if (logic_config::me()->get_logic_cfg().excel().enable()) {
    excel::config_manager::me()->set_override_same_version(
        logic_config::me()->get_logic_cfg().excel().override_same_version());
    excel::config_manager::me()->set_group_number(logic_config::me()->get_logic_cfg().excel().group_number());
    excel::config_manager::me()->set_on_not_found(
        [](const excel::config_manager::on_not_found_event_data_t& /*evt_data*/) {
          if (get_excel_reporter_blocker().load() > 0) {
            return;
          }
        });

    int ret = excel::config_manager::me()->reload_all(true);
    atfw::util::time::time_utility::update();
    return ret;
  }

  return 0;
}

// NOLINTNEXTLINE(performance-unnecessary-value-param)
SERVER_FRAME_CONFIG_API void excel_add_on_group_loaded_callback(std::function<void(excel::config_group_t&)> fn) {
  if (fn) {
    get_excel_on_group_loaded_fns().push_back(fn);
  }
}

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
// NOLINTNEXTLINE(performance-unnecessary-value-param)
SERVER_FRAME_CONFIG_API void set_excel_resource_provider_for_unit_test(
    excel_resource_provider_for_unit_test_t provider) {
  get_excel_resource_provider_for_unit_test() = provider;
}

SERVER_FRAME_CONFIG_API void clear_excel_resource_provider_for_unit_test() {
  get_excel_resource_provider_for_unit_test() = excel_resource_provider_for_unit_test_t{};
}
#endif
