// Copyright 2021 atframework

#include "logic/logic_server_setup.h"

#include <common/file_system.h>
#include <time/time_utility.h>

#include <atframe/atapp.h>
#include <cli/cmd_option_phoenix.h>
#include <libatbus_protocol.h>

#include <atframe/modules/etcd_module.h>

#include <config/atframe_service_types.h>
#include <config/extern_service_types.h>
#include <config/server_frame_build_feature.h>

#include <config/logic_config.h>

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <utility/rapid_json_helper.h>

#if defined(SERVER_FRAME_ENABLE_SANITIZER_INTERFACE) && SERVER_FRAME_ENABLE_SANITIZER_INTERFACE
#  include <sanitizer/asan_interface.h>
#endif

#include <config/excel/config_manager.h>
#include <config/extern_log_categorize.h>

#include <router/action/task_action_auto_save_objects.h>

#include <dispatcher/ss_msg_dispatcher.h>
#include <dispatcher/task_manager.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <map>
#include <sstream>
#include <utility>
#include <vector>

#include "logic/action/task_action_reload_remote_server_configure.h"
#include "logic/handle_ss_rpc_logiccommonservice.h"

namespace detail {
static logic_server_common_module *g_last_common_module = NULL;
}

namespace {
static int show_server_time(util::cli::callback_param params) {
  struct tm tt;
  time_t now = ::util::time::time_utility::get_now();
  UTIL_STRFUNC_LOCALTIME_S(&now, &tt);
  char str[64] = {0};
  strftime(str, sizeof(str) - 1, "%Y-%m-%d %H:%M:%S", &tt);
  ::atapp::app::add_custom_command_rsp(params, &str[0]);
  return 0;
}

static int debug_receive_stop_when_running(util::cli::callback_param params) {
  task_action_auto_save_objects::debug_receive_stop_when_running = true;
  return 0;
}

static int show_configure_handler(util::cli::callback_param params) {
  // std::string atapp_configure =
  auto app = atapp::app::get_last_instance();
  if (nullptr != app) {
    std::string app_configure =
        std::string("atapp configure:\n") + protobuf_mini_dumper_get_readable(app->get_origin_configure());
    ::atapp::app::add_custom_command_rsp(params, app_configure);
  }
  std::string logic_configure =
      std::string("logic configure:\n") + protobuf_mini_dumper_get_readable(logic_config::me()->get_server_cfg());
  ::atapp::app::add_custom_command_rsp(params, logic_configure);
  return 0;
}

static int show_battlesvr_by_version(util::cli::callback_param params) {
  logic_server_common_module *mod = logic_server_last_common_module();
  if (nullptr == mod) {
    ::atapp::app::add_custom_command_rsp(params, "logic_server_common_module destroyed.");
    return 0;
  }
  struct tm tt;
  time_t now = ::util::time::time_utility::get_now();
  UTIL_STRFUNC_LOCALTIME_S(&now, &tt);
  char str[64] = {0};
  strftime(str, sizeof(str) - 1, "%Y-%m-%d %H:%M:%S", &tt);
  ::atapp::app::add_custom_command_rsp(params, &str[0]);
  for (auto &battlesvr_by_version : mod->get_battlesvr_set_all_by_version()) {
    ::atapp::app::add_custom_command_rsp(params,
                                         LOG_WRAPPER_FWAPI_FORMAT("battle version: {}", battlesvr_by_version.first));
    for (auto &node : battlesvr_by_version.second) {
      atapp::app::add_custom_command_rsp(
          params, LOG_WRAPPER_FWAPI_FORMAT("  node -> {:#x}({})", node.server_id, node.server_id));
    }
  }
  return 0;
}
}  // namespace

int logic_server_setup_common(atapp::app &app, const logic_server_common_module_conf_t &conf) {
#if defined(SERVER_FRAME_ENABLE_SANITIZER_INTERFACE) && SERVER_FRAME_ENABLE_SANITIZER_INTERFACE
  // @see
  // https://github.com/gcc-mirror/gcc/blob/releases/gcc-4.8.5/libsanitizer/include/sanitizer/asan_interface.h
  __asan_set_death_callback([]() { FWLOGINFO("[SANITIZE=ADDRESS]: Exit"); });
  __asan_set_error_report_callback([](const char *content) { FWLOGERROR("[SANITIZE=ADDRESS]: Report: {}", content); });
#endif

  // setup options
  util::cli::cmd_option::ptr_type opt_mgr = app.get_option_manager();
  // show help and exit
  opt_mgr
      ->bind_cmd("-env",
                 [&app](util::cli::callback_param params) {
                   if (params.get_params_number() <= 0) {
                     return;
                   }

                   if (params[0]->to_cpp_string().empty()) {
                     return;
                   }

                   (*app.mutable_metadata().mutable_labels())["deployment.environment"] = params[0]->to_cpp_string();
                 })
      ->set_help_msg("-env [text]                            set a env name.");

  util::cli::cmd_option_ci::ptr_type cmd_mgr = app.get_command_manager();
  cmd_mgr->bind_cmd("show-configure", show_configure_handler)
      ->set_help_msg("show-configure                         show service configure");
  cmd_mgr->bind_cmd("debug-stop-when-running-auto-save", debug_receive_stop_when_running)
      ->set_help_msg(
          "debug-stop-when-running-auto-save debug stop when running "
          "task_action_auto_save_objects");
  cmd_mgr->bind_cmd("show-server-time", show_server_time)
      ->set_help_msg("show-server-time                       show server's local time");
  cmd_mgr->bind_cmd("list-battlesvr", show_battlesvr_by_version)
      ->set_help_msg("list-battlesvr                         list all ");

  std::shared_ptr<logic_server_common_module> logic_mod = std::make_shared<logic_server_common_module>(conf);
  if (!logic_mod) {
    fprintf(stderr, "create logic_server_common_module failed\n");
    return -1;
  }
  app.add_module(logic_mod);

  // set VCS version info
  const char *vcs_commit = server_frame_vcs_get_commit();
  const char *vcs_version = server_frame_vcs_get_version();
  if (!(vcs_commit && *vcs_commit)) {
    vcs_commit = server_frame_vcs_get_commit_sha();
  }
  if ((vcs_commit && *vcs_commit) || (vcs_version && *vcs_version)) {
    auto &metadata = app.mutable_metadata();
    std::stringstream ss;
    ss << app.get_build_version() << std::endl;
    if (vcs_commit && *vcs_commit) {
      ss << "VCS Commit    : " << vcs_commit << std::endl;
      (*metadata.mutable_labels())["vcs_commit"] = vcs_commit;
    }

    if (vcs_version && *vcs_version) {
      ss << "VCS Refer     : " << vcs_version << std::endl;
      (*metadata.mutable_labels())["vcs_version"] = vcs_version;
    }

    const char *vcs_server_branch = server_frame_vcs_get_server_branch();

    if (vcs_server_branch && *vcs_server_branch) {
      ss << "Server Branch : " << vcs_server_branch << std::endl;
      (*metadata.mutable_labels())["server_branch"] = vcs_server_branch;
    }

#ifdef __DATE__
    ss << "Module Build Time: " << __DATE__;
#  ifdef __TIME__
    ss << " " << __TIME__;
#  endif
    ss << std::endl;
#endif

    app.set_build_version(ss.str());
  }

  return 0;
}

logic_server_common_module *logic_server_last_common_module() { return detail::g_last_common_module; }

bool logic_server_common_module::battle_service_node_t::operator==(const battle_service_node_t &other) const {
  return server_id == other.server_id;
}

size_t logic_server_common_module::battle_service_node_hash_t::operator()(const battle_service_node_t &in) const {
  return std::hash<uint64_t>()(in.server_id);
}

logic_server_common_module::logic_server_common_module(const logic_server_common_module_conf_t &static_conf)
    : static_conf_(static_conf),
      etcd_event_handle_registered_(false),
      cachesvr_discovery_version_(0),
      server_remote_conf_global_version_(0),
      server_remote_conf_zone_version_(0),
      server_remote_conf_next_update_time_(0) {
  detail::g_last_common_module = this;
}

logic_server_common_module::~logic_server_common_module() {
  if (detail::g_last_common_module == this) {
    detail::g_last_common_module = NULL;
  }
}

int logic_server_common_module::init() {
  FWLOGINFO("[Server startup]: {}\n{}", get_app()->get_app_version(), get_app()->get_build_version());

  // 注册路由系统的内部事件
  int ret = handle::logic::register_handles_for_logiccommonservice();
  if (ret < 0) {
    FWLOGERROR("Setup LogicCommonService failed, result: {}({})", ret, protobuf_mini_dumper_get_error_msg(ret));
  }

  ret = setup_battle_service_watcher();
  setup_etcd_event_handle();
  return ret;
}

void logic_server_common_module::ready() {}

int logic_server_common_module::reload() {
  int ret = 0;

  if (get_app() && get_app()->is_running()) {
    ret = setup_battle_service_watcher();
    setup_etcd_event_handle();
  }

  return ret;
}

int logic_server_common_module::stop() {
  // can not use this module after stop
  if (detail::g_last_common_module == this) {
    detail::g_last_common_module = nullptr;
  }

  return 0;
}

int logic_server_common_module::timeout() {
  // can not use this module after stop
  if (detail::g_last_common_module == this) {
    detail::g_last_common_module = nullptr;
  }
  return 0;
}

const char *logic_server_common_module::name() const { return "logic_server_common_module"; }

int logic_server_common_module::tick() {
  int ret = 0;

  ret += tick_update_remote_configures();
  return ret;
}

int logic_server_common_module::debug_stop_app() { return get_app()->stop(); }

bool logic_server_common_module::is_closing() const noexcept { return get_app()->is_closing(); }

logic_server_common_module::etcd_keepalive_ptr_t logic_server_common_module::add_keepalive(const std::string &path,
                                                                                           std::string &value) {
  std::shared_ptr<atapp::etcd_module> etcd_mod;

  if (NULL != get_app()) {
    etcd_mod = get_app()->get_etcd_module();
  }

  if (!etcd_mod || path.empty()) {
    return nullptr;
  }

  etcd_keepalive_ptr_t ret = etcd_mod->add_keepalive_actor(value, path);
  if (!ret) {
    FWLOGERROR("add keepalive {}={} failed", path, value);
  }

  return ret;
}

atapp::etcd_cluster *logic_server_common_module::get_etcd_cluster() {
  std::shared_ptr< ::atapp::etcd_module> etcd_mod = get_etcd_module();
  if (!etcd_mod) {
    return nullptr;
  }

  return &etcd_mod->get_raw_etcd_ctx();
}

std::shared_ptr< ::atapp::etcd_module> logic_server_common_module::get_etcd_module() {
  atapp::app *app = get_app();
  if (nullptr == app) {
    return nullptr;
  }

  return app->get_etcd_module();
}

static void logic_server_common_module_on_watch_battlesvr_callback(atapp::etcd_module::watcher_sender_one_t &evt) {
  logic_server_common_module *current_mod = logic_server_last_common_module();
  const char *method_name;
  if (evt.event.get().evt_type == atapp::etcd_watch_event::EN_WEVT_PUT) {
    method_name = "PUT";
  } else {
    method_name = "DELETE";
  }

  logic_server_common_module::battle_service_node_t node;
  if (!logic_server_common_module::parse_battle_etcd_version_path(evt.event.get().kv.key, node.version,
                                                                  node.server_id)) {
    node.server_id = evt.node.get().node_discovery.id();  // TODO maybe using name instead
  }

  if (nullptr == current_mod) {
    FWLOGERROR("Got battlesvr event {} for {}:{} after logic_server_common_module closed", method_name, node.server_id,
               node.version.c_str());
    return;
  }

  if (evt.event.get().evt_type == atapp::etcd_watch_event::EN_WEVT_PUT) {
    current_mod->add_battlesvr_index(node);
  } else {
    current_mod->remove_battlesvr_index(node.server_id);
  }
}

int logic_server_common_module::setup_battle_service_watcher() {
  std::shared_ptr< ::atapp::etcd_module> etcd_mod = get_etcd_module();

  if (!static_conf_.enable_watch_battlesvr) {
    if (battle_service_watcher_) {
      if (etcd_mod) {
        if (etcd_mod->get_raw_etcd_ctx().remove_watcher(battle_service_watcher_)) {
          FWLOGINFO("Remove battlesvr watcher to {}", battle_service_watcher_->get_path());
        }
      }
      battle_service_watcher_.reset();
    }

    return 0;
  }

  if (!etcd_mod) {
    FWLOGERROR("Etcd module is required");
    return -1;
  }

  const std::string &battle_version_prefix = logic_config::me()->get_logic().battle().etcd_version_path();
  std::string watch_path;
  watch_path.reserve(etcd_mod->get_configure().path().size() + battle_version_prefix.size() + 1);
  watch_path = etcd_mod->get_configure().path();
  if (watch_path.empty() || watch_path[watch_path.size() - 1] != '/') {
    watch_path += '/';
  }

  if (battle_version_prefix.empty()) {
    watch_path += "by_battle_version";
  } else if (battle_version_prefix[0] == '/') {
    watch_path += battle_version_prefix.c_str() + 1;
  } else {
    watch_path += battle_version_prefix;
  }

  while (!watch_path.empty() && watch_path[watch_path.size() - 1] == '/') {
    watch_path.pop_back();
  }

  // check watch path change
  if (battle_service_watcher_ && battle_service_watcher_->get_path() == watch_path) {
    return 0;
  }

  if (battle_service_watcher_) {
    if (etcd_mod) {
      if (etcd_mod->get_raw_etcd_ctx().remove_watcher(battle_service_watcher_)) {
        FWLOGINFO("Remove battlesvr watcher to {}", battle_service_watcher_->get_path());
      }
    }
    battle_service_watcher_.reset();
  }

  battle_service_watcher_ =
      etcd_mod->add_watcher_by_custom_path(watch_path, logic_server_common_module_on_watch_battlesvr_callback);
  if (!battle_service_watcher_) {
    FWLOGERROR("Etcd create battlesvr watcher to {} failed.", watch_path);
    return -1;
  }

  return 0;
}

int logic_server_common_module::setup_etcd_event_handle() {
  if (etcd_event_handle_registered_) {
    return 0;
  }

  atapp::etcd_cluster *raw_ctx = get_etcd_cluster();
  if (NULL == raw_ctx) {
    return 0;
  }

  raw_ctx->add_on_event_up([this](atapp::etcd_cluster &ctx) { setup_battle_service_watcher(); }, true);

  raw_ctx->add_on_event_down(
      [this](atapp::etcd_cluster &ctx) {
        // clear cache, then watcher will be setup again when etcd is enabled in the future.
        battle_service_watcher_.reset();
      },
      true);

  etcd_event_handle_registered_ = true;
  return 0;
}

int logic_server_common_module::tick_update_remote_configures() {
  if (nullptr == get_app()) {
    return 0;
  }

  if (false == get_app()->is_running()) {
    return 0;
  }

  time_t sys_now = util::time::time_utility::get_sys_now();
  if (sys_now <= server_remote_conf_next_update_time_) {
    return 0;
  }

  server_remote_conf_next_update_time_ =
      sys_now + logic_config::me()->get_server_cfg().logic().remote_configure_update_interval().seconds();
  if (server_remote_conf_next_update_time_ <= sys_now) {
    server_remote_conf_next_update_time_ = sys_now + 300;
  }

  task_action_reload_remote_server_configure::ctor_param_t params;

  task_manager::id_t task_id;
  int res = task_manager::me()->create_task<task_action_reload_remote_server_configure>(task_id, std::move(params));
  if (0 != res) {
    FWLOGERROR("create task_action_reload_remote_server_configure failed, res: {}({})", res,
               protobuf_mini_dumper_get_error_msg(res));
    return 0;
  }

  task_manager::start_data_t start_data = dispatcher_make_default<dispatcher_start_data_t>();
  res = task_manager::me()->start_task(task_id, start_data);
  if (0 != res) {
    FWLOGERROR("start task_action_reload_remote_server_configure {} failed, res: {}({})", task_id, res,
               protobuf_mini_dumper_get_error_msg(res));
    return 0;
  }

  return 1;
}

std::string logic_server_common_module::make_battle_etcd_version_path(const std::string &version) const {
  std::string ret;
  std::shared_ptr<atapp::etcd_module> etcd_mod;
  if (NULL != get_app()) {
    etcd_mod = get_app()->get_etcd_module();
  }
  if (!etcd_mod) {
    return ret;
  }

  const std::string &battle_version_prefix = logic_config::me()->get_logic().battle().etcd_version_path();
  ret.reserve(etcd_mod->get_configure().path().size() + battle_version_prefix.size() + version.size() + 32);
  ret = etcd_mod->get_configure().path();
  if (ret.empty() || ret[ret.size() - 1] != '/') {
    ret += '/';
  }

  if (battle_version_prefix.empty()) {
    ret += "by_battle_version/";
  } else if (battle_version_prefix[0] == '/') {
    ret += battle_version_prefix.c_str() + 1;
  } else {
    ret += battle_version_prefix;
  }

  if (ret[ret.size() - 1] != '/') {
    ret += '/';
  }

  ret += version;

  if (ret[ret.size() - 1] != '/') {
    ret += '/';
  }

  char server_id_str[24] = {0};
  util::string::int2str(server_id_str, 23, get_app_id());
  ret += &server_id_str[0];

  return ret;
}

bool logic_server_common_module::parse_battle_etcd_version_path(const std::string &path, std::string &version,
                                                                uint64_t &svr_id) {
  std::vector<std::string> segments;
  if (false == util::file_system::split_path(segments, path.c_str(), true)) {
    return false;
  }

  if (segments.size() < 2) {
    return false;
  }

  svr_id = 0;
  util::string::str2int(svr_id, segments[segments.size() - 1].c_str());
  version.swap(segments[segments.size() - 2]);

  return svr_id != 0 && !version.empty();
}

void logic_server_common_module::add_battlesvr_index(const battle_service_node_t &node) {
  if (0 == node.server_id || node.version.empty()) {
    return;
  }

  // Closing
  if (nullptr == get_app()) {
    return;
  }

  auto id_iter = battle_service_id_.find(node.server_id);
  if (id_iter != battle_service_id_.end() && id_iter->second.version == node.version) {
    return;
  }

  remove_battlesvr_index(node.server_id);

  battle_service_id_[node.server_id] = node;
  battle_service_version_map_[node.version].insert(node);

  FWLOGINFO("Etcd event: add battlesvr {:#x}({}), version: {}", node.server_id,
            get_app()->convert_app_id_to_string(node.server_id), node.version);
}

void logic_server_common_module::remove_battlesvr_index(uint64_t server_id) {
  auto id_iter = battle_service_id_.find(server_id);
  if (id_iter == battle_service_id_.end()) {
    return;
  }

  // Closing
  if (nullptr == get_app()) {
    return;
  }

  FWLOGINFO("Etcd event: remove battlesvr {:#x}({}), version: %s", server_id,
            get_app()->convert_app_id_to_string(server_id), id_iter->second.version);

  {
    auto ver_iter = battle_service_version_map_.find(id_iter->second.version);
    if (ver_iter != battle_service_version_map_.end()) {
      ver_iter->second.erase(id_iter->second);

      if (ver_iter->second.empty()) {
        battle_service_version_map_.erase(ver_iter);
      }
    }
  }

  battle_service_id_.erase(id_iter);
}

const logic_server_common_module::battle_service_set_t *logic_server_common_module::get_battlesvr_set_by_version(
    const std::string &version) const {
  auto ver_iter = battle_service_version_map_.find(version);
  if (ver_iter != battle_service_version_map_.end()) {
    return &ver_iter->second;
  }

  return nullptr;
}

void logic_server_common_module::update_remote_server_configure(const std::string &global_conf, int32_t global_version,
                                                                const std::string &zone_conf, int32_t zone_version) {
  if (server_remote_conf_global_version_ == global_version && server_remote_conf_zone_version_ == zone_version) {
    return;
  }

  rapidsjon_helper_dump_options default_dump_options;
  hello::table_service_configure_data new_conf;
  rapidsjon_helper_parse(new_conf, global_conf, default_dump_options);
  rapidsjon_helper_parse(new_conf, zone_conf, default_dump_options);

  server_remote_conf_.Swap(&new_conf);

  // TODO(owentou): 服务器配置数据变化事件
}
