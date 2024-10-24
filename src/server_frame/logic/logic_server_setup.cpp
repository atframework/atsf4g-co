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

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <opentelemetry/sdk/resource/semantic_conventions.h>

#include <utility/rapid_json_helper.h>

#if defined(SERVER_FRAME_ENABLE_SANITIZER_ASAN_INTERFACE) && SERVER_FRAME_ENABLE_SANITIZER_ASAN_INTERFACE
#  include <sanitizer/asan_interface.h>
#elif defined(SERVER_FRAME_ENABLE_SANITIZER_TSAN_INTERFACE) && SERVER_FRAME_ENABLE_SANITIZER_TSAN_INTERFACE
#  include <sanitizer/tsan_interface.h>
#elif defined(SERVER_FRAME_ENABLE_SANITIZER_LSAN_INTERFACE) && SERVER_FRAME_ENABLE_SANITIZER_LSAN_INTERFACE
#  include <sanitizer/lsan_interface.h>
#elif defined(SERVER_FRAME_ENABLE_SANITIZER_UBSAN_INTERFACE) && SERVER_FRAME_ENABLE_SANITIZER_UBSAN_INTERFACE
#  include <sanitizer/ubsan_interface.h>
#elif defined(SERVER_FRAME_ENABLE_SANITIZER_HWASAN_INTERFACE_TEST) && \
    SERVER_FRAME_ENABLE_SANITIZER_HWASAN_INTERFACE_TEST
#  include <sanitizer/hwasan_interface.h>
#endif

#include <config/excel/config_manager.h>
#include <config/excel_config_wrapper.h>
#include <config/extern_log_categorize.h>

#include <router/action/task_action_auto_save_objects.h>
#include <router/router_manager_set.h>

#include <dispatcher/cs_msg_dispatcher.h>
#include <dispatcher/ss_msg_dispatcher.h>
#include <dispatcher/task_manager.h>
#include <logic/session_manager.h>

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
#include "logic/logic_server_macro.h"
#include "rpc/rpc_utils.h"
#include "rpc/telemetry/opentelemetry_utility.h"
#include "rpc/telemetry/rpc_global_service.h"

namespace detail {
static logic_server_common_module *g_last_common_module = NULL;
static std::shared_ptr<logic_server_common_module::stats_data_t> g_last_common_module_stats;
}  // namespace detail

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

static int send_notification(util::cli::callback_param params) {
  if (params.get_params_number() < 3) {
    ::atapp::app::add_custom_command_rsp(params,
                                         "send-notification <level> <event name> <event message>    send notification "
                                         "message(level: crirical,error,warn,notice");
    return 0;
  }

  rpc::telemetry::notification_domain domain = rpc::telemetry::notification_domain::kNotice;
  if (0 == UTIL_STRFUNC_STRNCASE_CMP("crirical", params[0]->to_string(), 8)) {
    domain = rpc::telemetry::notification_domain::kCritical;
  } else if (0 == UTIL_STRFUNC_STRNCASE_CMP("error", params[0]->to_string(), 5)) {
    domain = rpc::telemetry::notification_domain::kError;
  } else if (0 == UTIL_STRFUNC_STRNCASE_CMP("warn", params[0]->to_string(), 4)) {
    domain = rpc::telemetry::notification_domain::kWarning;
  }

  rpc::context ctx{rpc::context::create_without_task()};
  rpc::telemetry::opentelemetry_utility::send_notification_event(ctx, domain, params[1]->to_cpp_string(),
                                                                 params[2]->to_cpp_string(), {{"source", "command"}});
  ::atapp::app::add_custom_command_rsp(params, "success");
  return 0;
}

static int debug_receive_stop_when_running(util::cli::callback_param) {
  task_action_auto_save_objects::debug_receive_stop_when_running = true;
  return 0;
}

static int show_configure_handler(util::cli::callback_param params) {
  // std::string atapp_configure =
  auto app = atapp::app::get_last_instance();
  if (nullptr != app) {
    std::string app_configure =
        std::string("atapp configure:\n") +
        static_cast<std::string>(protobuf_mini_dumper_get_readable(app->get_origin_configure()));
    ::atapp::app::add_custom_command_rsp(params, app_configure);
  }
  std::string logic_configure =
      std::string("logic configure:\n") +
      static_cast<std::string>(protobuf_mini_dumper_get_readable(logic_config::me()->get_server_cfg()));
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

static int app_default_handle_on_receive_request(atapp::app &, const atapp::app::message_sender_t &source,
                                                 const atapp::app::message_t &msg) {
  if (0 == source.id) {
    FWLOGERROR("receive a message from unknown source or invalid body case");
    return PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
  }

  int ret = 0;
  switch (msg.type) {
    case ::atframe::component::service_type::EN_ATST_GATEWAY: {
      ret = cs_msg_dispatcher::me()->dispatch(source, msg);
      break;
    }

    case ::atframe::component::message_type::EN_ATST_SS_MSG: {
      ret = ss_msg_dispatcher::me()->dispatch(source, msg);
      break;
    }

    default: {
      FWLOGERROR("receive a message of invalid type: {}", msg.type);
      break;
    }
  }

  return ret;
}

static int app_default_handle_on_forward_response(atapp::app &app, const atapp::app::message_sender_t &source,
                                                  const atapp::app::message_t &msg, int32_t error_code) {
  if (error_code < 0) {
    FWLOGERROR("send data from {:#x}({}) to {:#x}({}) failed, sequence: {}, code: {}", app.get_id(), app.get_app_name(),
               source.id, source.name, msg.message_sequence, error_code);
  } else {
    FWLOGINFO("send data from {:#x}({}) to {:#x}({}) got response, sequence: {}, ", app.get_id(), app.get_app_name(),
              source.id, source.name, msg.message_sequence);
  }

  int ret = 0;
  switch (msg.type) {
    case ::atframe::component::message_type::EN_ATST_SS_MSG: {
      ret = ss_msg_dispatcher::me()->on_receive_send_data_response(source, msg, error_code);
      break;
    }

    default: {
      break;
    }
  }

  return ret;
}

static int app_default_handle_on_connected(atapp::app &, atbus::endpoint &ep, int status) {
  FWLOGINFO("endpoint {:#x} connected, status: {}", ep.get_id(), status);
  return 0;
}

static int app_default_handle_on_disconnected(atapp::app &, atbus::endpoint &ep, int status) {
  FWLOGINFO("endpoint {:#x} disconnected, status: {}", ep.get_id(), status);
  return 0;
}

}  // namespace

logic_server_common_module_configure::logic_server_common_module_configure() : enable_watch_battlesvr(false) {}

int logic_server_setup_common(atapp::app &app, const logic_server_common_module_configure &conf) {
#if defined(SERVER_FRAME_ENABLE_SANITIZER_ASAN_INTERFACE) && SERVER_FRAME_ENABLE_SANITIZER_ASAN_INTERFACE
  // @see
  // https://github.com/gcc-mirror/gcc/blob/releases/gcc-4.8.5/libsanitizer/include/sanitizer/asan_interface.h
  __sanitizer_set_death_callback([]() { FWLOGINFO("[SANITIZE=ADDRESS]: Exit"); });
  __asan_set_error_report_callback([](const char *content) {
    // Sanitizer report
    FWLOGWARNING("[SANITIZE=ADDRESS]: Report: {}", content);
  });
#elif defined(SERVER_FRAME_ENABLE_SANITIZER_TSAN_INTERFACE) && SERVER_FRAME_ENABLE_SANITIZER_TSAN_INTERFACE
  __sanitizer_set_death_callback([]() { FWLOGINFO("[SANITIZE=THREAD]: Exit"); });
#elif defined(SERVER_FRAME_ENABLE_SANITIZER_LSAN_INTERFACE) && SERVER_FRAME_ENABLE_SANITIZER_LSAN_INTERFACE
  __sanitizer_set_death_callback([]() { FWLOGINFO("[SANITIZE=LEAK]: Exit"); });
#elif defined(SERVER_FRAME_ENABLE_SANITIZER_UBSAN_INTERFACE) && SERVER_FRAME_ENABLE_SANITIZER_UBSAN_INTERFACE
  __sanitizer_set_death_callback([]() { FWLOGINFO("[SANITIZE=UB]: Exit"); });
#elif defined(SERVER_FRAME_ENABLE_SANITIZER_HWASAN_INTERFACE) && SERVER_FRAME_ENABLE_SANITIZER_HWASAN_INTERFACE
  __sanitizer_set_death_callback([]() { FWLOGINFO("[SANITIZE=HWADDRESS]: Exit"); });
  __hwasan_set_error_report_callback([](const char *content) {
    // Sanitizer report
    FWLOGWARNING("[SANITIZE=HWADDRESS]: Report: {}", content);
  });
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

                   app.set_metadata_label(opentelemetry::sdk::resource::SemanticConventions::kDeploymentEnvironmentName,
                                          params[0]->to_cpp_string());
                 })
      ->set_help_msg("-env [text]                                               set a env name.");

  util::cli::cmd_option_ci::ptr_type cmd_mgr = app.get_command_manager();
  cmd_mgr->bind_cmd("show-configure", show_configure_handler)
      ->set_help_msg("show-configure                                            show service configure");
  cmd_mgr->bind_cmd("debug-stop-when-running-auto-save", debug_receive_stop_when_running)
      ->set_help_msg("debug-stop-when-running-auto-save debug stop when running task_action_auto_save_objects");
  cmd_mgr->bind_cmd("show-server-time", show_server_time)
      ->set_help_msg("show-server-time                                          show server's local time");
  cmd_mgr->bind_cmd("send-notification", send_notification)
      ->set_help_msg(
          "send-notification <level> <event name> <event message>    send notification message(level: "
          "crirical,error,warn,notice)");
  cmd_mgr->bind_cmd("list-battlesvr", show_battlesvr_by_version)
      ->set_help_msg("list-battlesvr                                            list all ");

  std::shared_ptr<logic_server_common_module> logic_mod =
      atfw::memory::stl::make_shared<logic_server_common_module>(conf);
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
    std::stringstream ss;
    ss << app.get_build_version() << std::endl;
    if (vcs_commit && *vcs_commit) {
      ss << "VCS Commit       : " << vcs_commit << std::endl;
      app.set_metadata_label("vcs_commit", vcs_commit);
    }

    if (vcs_version && *vcs_version) {
      ss << "VCS Refer        : " << vcs_version << std::endl;
      app.set_metadata_label("vcs_version", vcs_version);
    }

    const char *vcs_server_branch = server_frame_vcs_get_server_branch();
    const char *package_version = server_frame_project_get_version();
    const char *bussiness_version = server_frame_get_user_bussiness_version();

    if (vcs_server_branch && *vcs_server_branch) {
      ss << "Server Branch    : " << vcs_server_branch << std::endl;
      app.set_metadata_label("server_branch", vcs_server_branch);
    }

    if (package_version && *package_version) {
      ss << "Package Version  : " << package_version << std::endl;
    }

    if (bussiness_version && *bussiness_version) {
      ss << "Bussness Version : " << bussiness_version << std::endl;
    }

#ifdef __DATE__
    ss << "Module Build Time: " << __DATE__;
#  ifdef __TIME__
    ss << " " << __TIME__;
#  endif
    ss << std::endl;
#endif

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
    ss << "Coroutine mode   : C++20 coroutine";
#  if defined(__cpp_impl_coroutine) || defined(__cpp_lib_coroutine)
    ss << "(";
#  endif
#  if defined(__cpp_impl_coroutine)
    ss << "Language/" << __cpp_impl_coroutine;
#  endif
#  if defined(__cpp_lib_coroutine)
    ss << ", Library/" << __cpp_lib_coroutine;
#  endif
#  if defined(__cpp_impl_coroutine) || defined(__cpp_lib_coroutine)
    ss << ")";
#  endif
    ss << std::endl;
#else
    ss << "Coroutine mode   : stackful";
#  ifdef LIBCOPP_MACRO_SYS_POSIX
    ss << "(mmap+pool)";
#  elif defined(LIBCOPP_MACRO_SYS_WIN)
    ss << "(VirtualAlloc+pool)";
#  else
    ss << "(pool)";
#  endif
    ss << std::endl;
#endif

    {
      auto sanitizer_name = server_frame_get_sanitizer_name();
      if (nullptr != sanitizer_name && *sanitizer_name) {
        ss << "Sanitizer        : " << sanitizer_name;
      }
    }

    app.set_build_version(ss.str());
  }

  // setup default message handle
  app.set_evt_on_forward_request(app_default_handle_on_receive_request);
  app.set_evt_on_forward_response(app_default_handle_on_forward_response);
  app.set_evt_on_app_connected(app_default_handle_on_connected);
  app.set_evt_on_app_disconnected(app_default_handle_on_disconnected);

  return 0;
}

logic_server_common_module *logic_server_last_common_module() { return detail::g_last_common_module; }

bool logic_server_common_module::battle_service_node_t::operator==(const battle_service_node_t &other) const {
  return server_id == other.server_id;
}

size_t logic_server_common_module::battle_service_node_hash_t::operator()(const battle_service_node_t &in) const {
  return std::hash<uint64_t>()(in.server_id);
}

logic_server_common_module::logic_server_common_module(const logic_server_common_module_configure &static_conf)
    : static_conf_(static_conf),
      stop_log_timepoint_(0),
      etcd_event_handle_registered_(false),
      cachesvr_discovery_version_(0),
      server_remote_conf_global_version_(0),
      server_remote_conf_zone_version_(0),
      server_remote_conf_next_update_time_(0) {
  stats_ = atfw::memory::stl::make_shared<stats_data_t>();
  memset(&stats_->last_checkpoint_usage, 0, sizeof(stats_->last_checkpoint_usage));
  stats_->collect_sequence.store(0, std::memory_order_release);

  stats_->last_update_usage_timepoint = 0;
  stats_->last_collect_sequence = 0;
  stats_->last_checkpoint = util::time::time_utility::sys_now();
  stats_->previous_tick_checkpoint = util::time::time_utility::sys_now();

  detail::g_last_common_module = this;
  detail::g_last_common_module_stats = stats_;
}

logic_server_common_module::~logic_server_common_module() {
  if (detail::g_last_common_module == this) {
    detail::g_last_common_module = nullptr;
    detail::g_last_common_module_stats.reset();
  }
}

int logic_server_common_module::init() {
  FWLOGINFO("============ Server initialize ============");
  FWLOGINFO("[Server startup]: {}\n{}", get_app()->get_app_version(), get_app()->get_build_version());

  INIT_CALL(logic_config, get_app()->get_id(), get_app()->get_app_name());

  // 内部模块暂不支持热开关
  shared_component_ = logic_config::me()->get_logic().server().shared_component();

  if (shared_component_.excel_config()) {
    INIT_CALL_FN(excel_config_wrapper_reload_all, true);
  }
  if (shared_component_.task_manager()) {
    INIT_CALL(task_manager);
  }
  if (shared_component_.session_manager()) {
    INIT_CALL(session_manager);
  }
  if (shared_component_.router_manager_set()) {
    INIT_CALL(router_manager_set);
  }

  // 注册路由系统的内部事件
  int ret = handle::logic::register_handles_for_logiccommonservice();
  if (ret < 0) {
    FWLOGERROR("Setup LogicCommonService failed, result: {}({})", ret, protobuf_mini_dumper_get_error_msg(ret));
  }

  ret = setup_battle_service_watcher();
  setup_etcd_event_handle();
  return ret;
}

void logic_server_common_module::ready() {
  FWLOGINFO("============ Server ready ============");

  memset(&stats_->last_checkpoint_usage, 0, sizeof(stats_->last_checkpoint_usage));
  stats_->collect_sequence.store(0, std::memory_order_release);
  stats_->last_update_usage_timepoint = 0;
  stats_->last_collect_sequence = 0;

  stats_->last_checkpoint = util::time::time_utility::sys_now();
  stats_->previous_tick_checkpoint = util::time::time_utility::sys_now();

  // Setup metrics
  setup_metrics();
}

int logic_server_common_module::reload() {
  FWLOGINFO("============ Server reload ============");
  int ret = 0;

  RELOAD_CALL(ret, logic_config, *get_app());

  // Update rpc caller context data
  rpc::context::set_current_service(*get_app(), logic_config::me()->get_logic().telemetry());

  if (get_app() && get_app()->is_running()) {
    ret = setup_battle_service_watcher();
    setup_etcd_event_handle();
  }

  if (shared_component_.excel_config()) {
    RELOAD_CALL_FN(ret, excel_config_wrapper_reload_all, false);
  }
  if (shared_component_.task_manager()) {
    RELOAD_CALL(ret, task_manager);
  }

  return ret;
}

int logic_server_common_module::stop() {
  time_t now = util::time::time_utility::get_sys_now();
  if (now != stop_log_timepoint_) {
    stop_log_timepoint_ = now;
    FWLOGINFO("============ Server stop ============");
  }

  int ret = 0;
  if (shared_component_.router_manager_set()) {
    ret = router_manager_set::me()->stop();
    if (ret < 0) {
      FWLOGERROR("router_manager_set stop failed, res: {}", ret);
      return ret;
    }

    // 保存任务未完成，需要继续等待
    if (!router_manager_set::me()->is_closed()) {
      ret = 1;
    }
  }

  // can not use this module after stop
  if (0 == ret) {
    if (detail::g_last_common_module == this) {
      detail::g_last_common_module_stats.reset();
      detail::g_last_common_module = nullptr;
    }
  }

  return ret;
}

int logic_server_common_module::timeout() {
  FWLOGINFO("============ Server module timeout ============");
  if (shared_component_.router_manager_set()) {
    router_manager_set::me()->force_close();
  }

  // can not use this module after stop
  if (detail::g_last_common_module == this) {
    detail::g_last_common_module_stats.reset();
    detail::g_last_common_module = nullptr;
  }
  return 0;
}

void logic_server_common_module::cleanup() {
  task_manager::me()->kill_all();

  if (!service_index_handle_) {
    std::shared_ptr<atapp::etcd_module> etcd_mod = get_etcd_module();
    if (etcd_mod) {
      etcd_mod->remove_on_node_event(*service_index_handle_);
    }
    service_index_handle_.reset();
  }
}

const char *logic_server_common_module::name() const { return "logic_server_common_module"; }

int logic_server_common_module::tick() {
  int ret = 0;

  ret += tick_update_remote_configures();
  if (shared_component_.task_manager()) {
    ret += task_manager::me()->tick(util::time::time_utility::get_sys_now(),
                                    static_cast<int>(1000 * util::time::time_utility::get_now_usec()));
  }
  if (shared_component_.session_manager()) {
    ret += session_manager::me()->proc();
  }
  if (shared_component_.router_manager_set()) {
    ret += router_manager_set::me()->tick();
  }

  if (!task_timer_.empty()) {
    auto now = util::time::time_utility::sys_now();
    int left_timer_per_tick = 4096;
    while (!task_timer_.empty() && left_timer_per_tick-- > 0) {
      if (task_timer_.top().timeout >= now) {
        break;
      }

      auto timer_data = task_timer_.top();
      task_timer_.pop();

      auto callback_data = dispatcher_make_default<dispatcher_resume_data_type>();
      callback_data.sequence = timer_data.sequence;
      callback_data.message.message_type = timer_data.message_type;

      rpc::custom_resume(timer_data.task_id, callback_data);

      ++ret;
    }
  }

  tick_stats();
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

bool logic_server_common_module::is_runtime_active() const noexcept {
  std::shared_ptr<atapp::etcd_module> etcd_mod;

  if (nullptr != get_app()) {
    etcd_mod = get_app()->get_etcd_module();
  }

  if (!etcd_mod) {
    return false;
  }

  auto node_ptr = etcd_mod->get_global_discovery().get_node_by_id(get_app_id());
  if (!node_ptr) {
    return false;
  }

  auto &labels = node_ptr->get_discovery_info().metadata().labels();
  auto active_iter = labels.find("runtime_active");
  if (active_iter == labels.end()) {
    return true;
  }

  return !active_iter->second.empty() && active_iter->second != "0" &&
         0 != UTIL_STRFUNC_STRNCASE_CMP(active_iter->second.c_str(), "no", 2) &&
         0 != UTIL_STRFUNC_STRNCASE_CMP(active_iter->second.c_str(), "false", 5);
}

atapp::etcd_cluster *logic_server_common_module::get_etcd_cluster() {
  std::shared_ptr<::atapp::etcd_module> etcd_mod = get_etcd_module();
  if (!etcd_mod) {
    return nullptr;
  }

  return &etcd_mod->get_raw_etcd_ctx();
}

std::shared_ptr<::atapp::etcd_module> logic_server_common_module::get_etcd_module() {
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
  std::shared_ptr<::atapp::etcd_module> etcd_mod = get_etcd_module();

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

atapp::etcd_discovery_set::ptr_t logic_server_common_module::get_discovery_index_by_type(uint64_t type_id) const {
  auto iter = service_type_id_index_.find(type_id);
  if (iter == service_type_id_index_.end()) {
    return nullptr;
  }

  return iter->second.all_index;
}

atapp::etcd_discovery_set::ptr_t logic_server_common_module::get_discovery_index_by_type(
    const std::string &type_name) const {
  auto iter = service_type_name_index_.find(type_name);
  if (iter == service_type_name_index_.end()) {
    return nullptr;
  }

  return iter->second.all_index;
}

atapp::etcd_discovery_set::ptr_t logic_server_common_module::get_discovery_index_by_type_zone(uint64_t type_id,
                                                                                              uint64_t zone_id) const {
  auto type_iter = service_type_id_index_.find(type_id);
  if (type_iter == service_type_id_index_.end()) {
    return nullptr;
  }

  auto zone_iter = type_iter->second.zone_index.find(zone_id);
  if (zone_iter == type_iter->second.zone_index.end()) {
    return nullptr;
  }

  return zone_iter->second;
}

atapp::etcd_discovery_set::ptr_t logic_server_common_module::get_discovery_index_by_type_zone(
    const std::string &type_name, uint64_t zone_id) const {
  auto type_iter = service_type_name_index_.find(type_name);
  if (type_iter == service_type_name_index_.end()) {
    return nullptr;
  }

  auto zone_iter = type_iter->second.zone_index.find(zone_id);
  if (zone_iter == type_iter->second.zone_index.end()) {
    return nullptr;
  }

  return zone_iter->second;
}

atapp::etcd_discovery_set::ptr_t logic_server_common_module::get_discovery_index_by_zone(uint64_t zone_id) const {
  auto iter = service_zone_index_.find(zone_id);
  if (iter == service_zone_index_.end()) {
    return nullptr;
  }

  return iter->second;
}

util::memory::strong_rc_ptr<atapp::etcd_discovery_node> logic_server_common_module::get_discovery_by_id(
    uint64_t id) const {
  if (nullptr == get_app()) {
    return nullptr;
  }

  return get_app()->get_global_discovery().get_node_by_id(id);
}

util::memory::strong_rc_ptr<atapp::etcd_discovery_node> logic_server_common_module::get_discovery_by_name(
    const std::string &name) const {
  if (nullptr == get_app()) {
    return nullptr;
  }

  return get_app()->get_global_discovery().get_node_by_name(name);
}

int logic_server_common_module::setup_etcd_event_handle() {
  if (etcd_event_handle_registered_) {
    return 0;
  }

  std::shared_ptr<::atapp::etcd_module> etcd_mod = get_etcd_module();
  if (!etcd_mod) {
    return 0;
  }
  if (service_index_handle_) {
    etcd_mod->remove_on_node_event(*service_index_handle_);
  } else {
    service_index_handle_ = std::unique_ptr<atapp::etcd_module::node_event_callback_handle_t>(
        new atapp::etcd_module::node_event_callback_handle_t());
  }
  if (service_index_handle_) {
    *service_index_handle_ = etcd_mod->add_on_node_discovery_event(
        [this](atapp::etcd_module::node_action_t::type action_type, const atapp::etcd_discovery_node::ptr_t &node) {
          if (!node) {
            return;
          }

          switch (action_type) {
            case atapp::etcd_module::node_action_t::EN_NAT_PUT: {
              if (0 != node->get_discovery_info().type_id()) {
                add_service_type_id_index(node);
              }
              if (!node->get_discovery_info().type_name().empty()) {
                add_service_type_name_index(node);
              }
              if (0 != node->get_discovery_info().area().zone_id()) {
                add_service_zone_index(node);
              }
              break;
            }
            case atapp::etcd_module::node_action_t::EN_NAT_DELETE: {
              if (0 != node->get_discovery_info().type_id()) {
                remove_service_type_id_index(node);
              }
              if (!node->get_discovery_info().type_name().empty()) {
                remove_service_type_name_index(node);
              }
              if (0 != node->get_discovery_info().area().zone_id()) {
                remove_service_zone_index(node);
              }
              break;
            }
            default:
              break;
          }
        });
  }

  atapp::etcd_cluster &raw_ctx = etcd_mod->get_raw_etcd_ctx();
  raw_ctx.add_on_event_up([this](atapp::etcd_cluster &) { setup_battle_service_watcher(); }, true);

  raw_ctx.add_on_event_down(
      [this](atapp::etcd_cluster &) {
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

  task_type_trait::task_type task_inst;
  int res = task_manager::me()->create_task<task_action_reload_remote_server_configure>(task_inst, std::move(params));
  if (0 != res) {
    FWLOGERROR("create task_action_reload_remote_server_configure failed, res: {}({})", res,
               protobuf_mini_dumper_get_error_msg(res));
    return 0;
  }

  dispatcher_start_data_type start_data = dispatcher_make_default<dispatcher_start_data_type>();
  res = task_manager::me()->start_task(task_inst, start_data);
  if (0 != res) {
    FWLOGERROR("start task_action_reload_remote_server_configure {} failed, res: {}({})",
               task_type_trait::get_task_id(task_inst), res, protobuf_mini_dumper_get_error_msg(res));
    return 0;
  }

  return 1;
}

void logic_server_common_module::tick_stats() {
  if (nullptr == get_app()) {
    return;
  }

  if (!get_app()->is_running()) {
    return;
  }

  auto sys_now = util::time::time_utility::sys_now();
  // Tick interval
  {
    auto tick_interval = sys_now - stats_->previous_tick_checkpoint;
    int64_t max_tick_interval_us =
        static_cast<int64_t>(std::chrono::duration_cast<std::chrono::microseconds>(tick_interval).count());
    int64_t collect_max_tick_interval_us = stats_->collect_max_tick_interval_us.load(std::memory_order_acquire);
    if (max_tick_interval_us > collect_max_tick_interval_us) {
      stats_->collect_max_tick_interval_us.compare_exchange_strong(collect_max_tick_interval_us, max_tick_interval_us,
                                                                   std::memory_order_acq_rel);
    }
    stats_->previous_tick_checkpoint = sys_now;
  }

  if (stats_->last_update_usage_timepoint == util::time::time_utility::get_sys_now()) {
    return;
  }
  stats_->last_update_usage_timepoint = util::time::time_utility::get_sys_now();

  do {
    uv_rusage_t last_usage;
    if (0 != uv_getrusage(&last_usage)) {
      break;
    }

    // 首次tick，初始化
    if (0 == stats_->last_checkpoint_usage.ru_utime.tv_sec || 0 == stats_->last_checkpoint_usage.ru_stime.tv_sec) {
      stats_->last_checkpoint_usage = last_usage;
      stats_->last_checkpoint = sys_now;
      stats_->last_collect_sequence = stats_->collect_sequence.load(std::memory_order_acquire);
      stats_->collect_max_tick_interval_us.store(0, std::memory_order_release);
      break;
    }

    opentelemetry::context::Context telemetry_context;

    auto offset_usr = last_usage.ru_utime.tv_sec - stats_->last_checkpoint_usage.ru_utime.tv_sec;
    auto offset_sys = last_usage.ru_stime.tv_sec - stats_->last_checkpoint_usage.ru_stime.tv_sec;
    offset_usr *= 1000000;
    offset_sys *= 1000000;
    offset_usr += last_usage.ru_utime.tv_usec - stats_->last_checkpoint_usage.ru_utime.tv_usec;
    offset_sys += last_usage.ru_stime.tv_usec - stats_->last_checkpoint_usage.ru_stime.tv_usec;

    auto checkpoint_offset =
        std::chrono::duration_cast<std::chrono::microseconds>(sys_now - stats_->last_checkpoint).count();
    if (checkpoint_offset <= 0) {
      break;
    }

    stats_->collect_cpu_sys.store(offset_sys * 1000000 / checkpoint_offset, std::memory_order_release);
    stats_->collect_cpu_user.store(offset_usr * 1000000 / checkpoint_offset, std::memory_order_release);
    stats_->collect_memory_max_rss.store(last_usage.ru_maxrss, std::memory_order_release);
    size_t memory_rss = 0;
    if (0 == uv_resident_set_memory(&memory_rss)) {
      stats_->collect_memory_rss.store(memory_rss, std::memory_order_release);
    }

    // 拉取过则重置周期性数据
    uint64_t collect_sequence = stats_->collect_sequence.load(std::memory_order_acquire);
    if (collect_sequence != stats_->last_collect_sequence) {
      stats_->last_collect_sequence = collect_sequence;

      stats_->collect_max_tick_interval_us.store(0, std::memory_order_release);
      stats_->last_checkpoint_usage = last_usage;
      stats_->last_checkpoint = sys_now;
    }
  } while (false);
}

void logic_server_common_module::setup_metrics() {
  rpc::telemetry::opentelemetry_utility::add_global_metics_observable_int64(
      rpc::telemetry::metrics_observable_type::kGauge, "service_rusage", {"service_tick", "", "us"},
      [](rpc::telemetry::opentelemetry_utility::metrics_observer &result) {
        std::shared_ptr<logic_server_common_module::stats_data_t> stats = detail::g_last_common_module_stats;
        if (!stats) {
          return;
        }

        rpc::telemetry::opentelemetry_utility::global_metics_observe_record(
            result, stats->collect_max_tick_interval_us.load(std::memory_order_acquire));

        ++stats->collect_sequence;
      });

  rpc::telemetry::opentelemetry_utility::add_global_metics_observable_double(
      rpc::telemetry::metrics_observable_type::kGauge, "service_rusage", {"service_rusage_cpu_sys", "", "percent"},
      [](rpc::telemetry::opentelemetry_utility::metrics_observer &result) {
        std::shared_ptr<logic_server_common_module::stats_data_t> stats = detail::g_last_common_module_stats;
        if (!stats) {
          return;
        }

        rpc::telemetry::opentelemetry_utility::global_metics_observe_record(
            result, static_cast<double>(stats->collect_cpu_sys.load(std::memory_order_acquire)) / 10000.0);

        ++stats->collect_sequence;
      });

  rpc::telemetry::opentelemetry_utility::add_global_metics_observable_double(
      rpc::telemetry::metrics_observable_type::kGauge, "service_rusage", {"service_rusage_cpu_user", "", "percent"},
      [](rpc::telemetry::opentelemetry_utility::metrics_observer &result) {
        std::shared_ptr<logic_server_common_module::stats_data_t> stats = detail::g_last_common_module_stats;
        if (!stats) {
          return;
        }

        rpc::telemetry::opentelemetry_utility::global_metics_observe_record(
            result, static_cast<double>(stats->collect_cpu_user.load(std::memory_order_acquire)) / 10000.0);

        ++stats->collect_sequence;
      });

  rpc::telemetry::opentelemetry_utility::add_global_metics_observable_double(
      rpc::telemetry::metrics_observable_type::kGauge, "service_rusage", {"service_rusage_cpu_all", "", "percent"},
      [](rpc::telemetry::opentelemetry_utility::metrics_observer &result) {
        std::shared_ptr<logic_server_common_module::stats_data_t> stats = detail::g_last_common_module_stats;
        if (!stats) {
          return;
        }

        rpc::telemetry::opentelemetry_utility::global_metics_observe_record(
            result, static_cast<double>(stats->collect_cpu_sys.load(std::memory_order_acquire) +
                                        stats->collect_cpu_user.load(std::memory_order_acquire)) /
                        10000.0);

        ++stats->collect_sequence;
      });

  rpc::telemetry::opentelemetry_utility::add_global_metics_observable_int64(
      rpc::telemetry::metrics_observable_type::kGauge, "service_rusage", {"service_rusage_memory_maxrss", "", ""},
      [](rpc::telemetry::opentelemetry_utility::metrics_observer &result) {
        std::shared_ptr<logic_server_common_module::stats_data_t> stats = detail::g_last_common_module_stats;
        if (!stats) {
          return;
        }

        rpc::telemetry::opentelemetry_utility::global_metics_observe_record(
            result, static_cast<int64_t>(stats->collect_memory_max_rss.load(std::memory_order_acquire)));

        ++stats->collect_sequence;
      });

  rpc::telemetry::opentelemetry_utility::add_global_metics_observable_int64(
      rpc::telemetry::metrics_observable_type::kGauge, "service_rusage", {"service_rusage_memory_rss", "", ""},
      [](rpc::telemetry::opentelemetry_utility::metrics_observer &result) {
        std::shared_ptr<logic_server_common_module::stats_data_t> stats = detail::g_last_common_module_stats;
        if (!stats) {
          return;
        }

        rpc::telemetry::opentelemetry_utility::global_metics_observe_record(
            result, static_cast<int64_t>(stats->collect_memory_rss.load(std::memory_order_acquire)));

        ++stats->collect_sequence;
      });
}

void logic_server_common_module::add_service_type_id_index(const atapp::etcd_discovery_node::ptr_t &node) {
  uint64_t zone_id = node->get_discovery_info().area().zone_id();
  uint64_t type_id = node->get_discovery_info().type_id();
  logic_server_type_discovery_set_t &type_index = service_type_id_index_[type_id];
  if (!type_index.all_index) {
    type_index.all_index = atfw::memory::stl::make_strong_rc<atapp::etcd_discovery_set>();
  }

  if (!type_index.all_index) {
    // Bad data
    service_type_id_index_.erase(type_id);
    return;
  }

  type_index.all_index->add_node(node);

  if (0 == zone_id) {
    return;
  }

  atapp::etcd_discovery_set::ptr_t &zone_index = type_index.zone_index[zone_id];
  if (!zone_index) {
    zone_index = atfw::memory::stl::make_strong_rc<atapp::etcd_discovery_set>();
  }

  if (!zone_index) {
    type_index.zone_index.erase(zone_id);
    return;
  }

  zone_index->add_node(node);
}

void logic_server_common_module::remove_service_type_id_index(const atapp::etcd_discovery_node::ptr_t &node) {
  uint64_t zone_id = node->get_discovery_info().area().zone_id();
  uint64_t type_id = node->get_discovery_info().type_id();
  auto type_iter = service_type_id_index_.find(type_id);
  if (type_iter == service_type_id_index_.end()) {
    return;
  }

  if (!type_iter->second.all_index) {
    service_type_id_index_.erase(type_iter);
    return;
  }

  type_iter->second.all_index->remove_node(node);

  if (0 == zone_id) {
    if (type_iter->second.all_index->empty() && type_iter->second.zone_index.empty()) {
      service_type_id_index_.erase(type_iter);
    }
    return;
  }
  auto zone_iter = type_iter->second.zone_index.find(zone_id);
  if (zone_iter == type_iter->second.zone_index.end()) {
    if (type_iter->second.all_index->empty() && type_iter->second.zone_index.empty()) {
      service_type_id_index_.erase(type_iter);
    }
    return;
  }

  if (!zone_iter->second) {
    type_iter->second.zone_index.erase(zone_iter);

    if (type_iter->second.all_index->empty() && type_iter->second.zone_index.empty()) {
      service_type_id_index_.erase(type_iter);
    }
    return;
  }

  zone_iter->second->remove_node(node);
  if (zone_iter->second->empty()) {
    type_iter->second.zone_index.erase(zone_iter);
  }

  if (type_iter->second.all_index->empty() && type_iter->second.zone_index.empty()) {
    service_type_id_index_.erase(type_iter);
  }
}

void logic_server_common_module::add_service_type_name_index(const atapp::etcd_discovery_node::ptr_t &node) {
  uint64_t zone_id = node->get_discovery_info().area().zone_id();
  const std::string &type_name = node->get_discovery_info().type_name();
  logic_server_type_discovery_set_t &type_index = service_type_name_index_[type_name];
  if (!type_index.all_index) {
    type_index.all_index = atfw::memory::stl::make_strong_rc<atapp::etcd_discovery_set>();
  }

  if (!type_index.all_index) {
    // Bad data
    service_type_name_index_.erase(type_name);
    return;
  }

  type_index.all_index->add_node(node);

  if (0 == zone_id) {
    return;
  }

  atapp::etcd_discovery_set::ptr_t &zone_index = type_index.zone_index[zone_id];
  if (!zone_index) {
    zone_index = atfw::memory::stl::make_strong_rc<atapp::etcd_discovery_set>();
  }

  if (!zone_index) {
    type_index.zone_index.erase(zone_id);
    return;
  }

  zone_index->add_node(node);
}

void logic_server_common_module::remove_service_type_name_index(const atapp::etcd_discovery_node::ptr_t &node) {
  uint64_t zone_id = node->get_discovery_info().area().zone_id();
  const std::string &type_name = node->get_discovery_info().type_name();
  auto type_iter = service_type_name_index_.find(type_name);
  if (type_iter == service_type_name_index_.end()) {
    return;
  }

  if (!type_iter->second.all_index) {
    service_type_name_index_.erase(type_iter);
    return;
  }

  type_iter->second.all_index->remove_node(node);

  if (0 == zone_id) {
    if (type_iter->second.all_index->empty() && type_iter->second.zone_index.empty()) {
      service_type_name_index_.erase(type_iter);
    }
    return;
  }
  auto zone_iter = type_iter->second.zone_index.find(zone_id);
  if (zone_iter == type_iter->second.zone_index.end()) {
    if (type_iter->second.all_index->empty() && type_iter->second.zone_index.empty()) {
      service_type_name_index_.erase(type_iter);
    }
    return;
  }

  if (!zone_iter->second) {
    type_iter->second.zone_index.erase(zone_iter);

    if (type_iter->second.all_index->empty() && type_iter->second.zone_index.empty()) {
      service_type_name_index_.erase(type_iter);
    }
    return;
  }

  zone_iter->second->remove_node(node);
  if (zone_iter->second->empty()) {
    type_iter->second.zone_index.erase(zone_iter);
  }

  if (type_iter->second.all_index->empty() && type_iter->second.zone_index.empty()) {
    service_type_name_index_.erase(type_iter);
  }
}

void logic_server_common_module::add_service_zone_index(const atapp::etcd_discovery_node::ptr_t &node) {
  uint64_t zone_id = node->get_discovery_info().area().zone_id();
  atapp::etcd_discovery_set::ptr_t &zone_index = service_zone_index_[zone_id];
  if (!zone_index) {
    zone_index = atfw::memory::stl::make_strong_rc<atapp::etcd_discovery_set>();
  }

  if (!zone_index) {
    // Bad data
    service_zone_index_.erase(zone_id);
    return;
  }
  zone_index->add_node(node);

  FWLOGINFO("service discovery {}({}, {}) indexed, type name={}, zone_id={}",
            node->get_discovery_info().area().district(), node->get_discovery_info().id(),
            node->get_discovery_info().name(), node->get_discovery_info().type_name(),
            node->get_discovery_info().area().zone_id());
}

void logic_server_common_module::remove_service_zone_index(const atapp::etcd_discovery_node::ptr_t &node) {
  uint64_t zone_id = node->get_discovery_info().area().zone_id();

  auto zone_iter = service_zone_index_.find(zone_id);
  if (zone_iter == service_zone_index_.end()) {
    return;
  }

  if (!zone_iter->second) {
    service_zone_index_.erase(zone_iter);
    return;
  }

  zone_iter->second->remove_node(node);

  if (zone_iter->second->empty()) {
    service_zone_index_.erase(zone_iter);
  }

  FWLOGINFO("service discovery {}({}, {}) unindexed, type name={}, zone_id={}",
            node->get_discovery_info().area().district(), node->get_discovery_info().id(),
            node->get_discovery_info().name(), node->get_discovery_info().type_name(),
            node->get_discovery_info().area().zone_id());
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

  rapidjson_helper_dump_options default_dump_options;
  PROJECT_NAMESPACE_ID::table_service_configure_data new_conf;
  rapidjson_helper_parse(new_conf, global_conf, default_dump_options);
  rapidjson_helper_parse(new_conf, zone_conf, default_dump_options);

  server_remote_conf_.Swap(&new_conf);

  // TODO(owent): 服务器配置数据变化事件
}

void logic_server_common_module::insert_timer(uint64_t task_id, std::chrono::system_clock::duration timeout_conf,
                                              logic_server_timer &output) {
  output.task_id = task_id;
  output.message_type = reinterpret_cast<uintptr_t>(&task_timer_);
  output.sequence = ss_msg_dispatcher::me()->allocate_sequence();
  output.timeout = util::time::time_utility::sys_now() + timeout_conf;

  task_timer_.push(output);
}
