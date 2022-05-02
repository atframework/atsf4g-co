// Copyright 2021 atframework

#include "config/excel_config_wrapper.h"

#include <common/file_system.h>
#include <common/string_oprs.h>

#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <config/compiler/protobuf_prefix.h>

#include <google/protobuf/reflection.h>

#include <config/compiler/protobuf_suffix.h>

#include <list>
#include <unordered_map>

#include "config/server_frame_build_feature.h"

#include "config/excel/config_manager.h"
#include "config/excel_config_const_index.h"
#include "config/logic_config.h"

namespace details {
static std::list<std::function<void(excel::config_group_t&)> > g_excel_on_group_loaded_fns;
static bool g_excel_config_manager_inited = false;
static std::atomic<int64_t> g_excel_reporter_blocker(0);
}  // namespace details

excel_config_block_report_t::excel_config_block_report_t() { ++details::g_excel_reporter_blocker; }

excel_config_block_report_t::~excel_config_block_report_t() { --details::g_excel_reporter_blocker; }

static bool excel_config_callback_get_buffer(std::string& out, const char* path) {
  char file_path[util::file_system::MAX_PATH_LEN + 1];
  int res = UTIL_STRFUNC_SNPRINTF(file_path, sizeof(file_path) - 1, "%s%c%s",
                                  logic_config::me()->get_logic().excel().bindir().c_str(),
                                  util::file_system::DIRECTORY_SEPARATOR, path);
  if (res > 0 && static_cast<size_t>(res) < util::file_system::MAX_PATH_LEN) {
    file_path[res] = 0;
  } else {
    return false;
  }

  if (!util::file_system::is_exist(file_path)) {
    return false;
  }

  return util::file_system::get_file_content(out, file_path, true);
}

static bool excel_config_callback_get_version(std::string& out) {
  char file_path[util::file_system::MAX_PATH_LEN + 1];
  int res = UTIL_STRFUNC_SNPRINTF(file_path, sizeof(file_path) - 1, "%s%c%s",
                                  logic_config::me()->get_logic().excel().bindir().c_str(),
                                  util::file_system::DIRECTORY_SEPARATOR, "version.txt");
  if (res > 0 && static_cast<size_t>(res) < util::file_system::MAX_PATH_LEN) {
    file_path[res] = 0;
  } else {
    return false;
  }

  out = "0.0.0.0";
  if (util::file_system::is_exist(file_path)) {
    std::string buffer;
    if (util::file_system::get_file_content(buffer, file_path, true)) {
      std::pair<const char*, size_t> ver = util::string::trim(buffer.c_str(), buffer.size());
      out.assign(ver.first, ver.second);
    }
  }

  return true;
}

static void excel_config_callback_on_reload_all(excel::config_manager::config_group_ptr_t group) {
  if (!group) {
    FWLOGERROR("excel config group error");
    return;
  }

  // 自定义跨表索引在这之后初始化
  setup_const_config(*group);
}

int excel_config_wrapper_reload_all(bool is_init) {
  if (!details::g_excel_config_manager_inited && !is_init) {
    return 0;
  }

  if (logic_config::me()->get_logic().excel().enable()) {
    if (!details::g_excel_config_manager_inited) {
      int res = ::excel::config_manager::me()->init();
      if (res < 0) {
        FWLOGERROR("excel::config_manager init failed, res: {}", res);
        return res;
      }

      excel::config_manager::me()->set_buffer_loader(excel_config_callback_get_buffer);
      excel::config_manager::me()->set_version_loader(excel_config_callback_get_version);

      excel::config_manager::on_load_func_t origin_reload_callback =
          excel::config_manager::me()->get_on_group_reload_all();
      excel::config_manager::me()->set_on_group_reload_all(
          [origin_reload_callback](excel::config_manager::config_group_ptr_t group) {
            if (origin_reload_callback) {
              origin_reload_callback(group);
            }
            excel_config_callback_on_reload_all(group);

            if (group) {
              for (auto& fn : details::g_excel_on_group_loaded_fns) {
                fn(*group);
              }
            }
          });
      details::g_excel_config_manager_inited = true;
    }

    excel::config_manager::me()->set_override_same_version(
        logic_config::me()->get_logic().excel().override_same_version());
    excel::config_manager::me()->set_group_number(logic_config::me()->get_logic().excel().group_number());
    excel::config_manager::me()->set_on_not_found([](const excel::config_manager::on_not_found_event_data_t& evt_data) {
      if (details::g_excel_reporter_blocker.load() > 0) {
        return;
      }

      // TODO Remote remote(Metrics)
    });

    int ret = excel::config_manager::me()->reload_all();
    ::util::time::time_utility::update();
    return ret;
  }

  return 0;
}

void excel_add_on_group_loaded_callback(std::function<void(excel::config_group_t&)> fn) {
  if (fn) {
    details::g_excel_on_group_loaded_fns.push_back(fn);
  }
}
