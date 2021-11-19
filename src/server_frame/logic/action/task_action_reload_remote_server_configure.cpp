// Copyright 2021 atframework
// @brief Created by owent on 2021-05-21 15:19:58

#include "logic/action/task_action_reload_remote_server_configure.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <logic/logic_server_setup.h>

#include <string>

task_action_reload_remote_server_configure::task_action_reload_remote_server_configure(ctor_param_t&& param)
    : task_action_no_req_base(param), param_(param) {}
task_action_reload_remote_server_configure::~task_action_reload_remote_server_configure() {}

const char* task_action_reload_remote_server_configure::name() const {
  return "task_action_reload_remote_server_configure";
}

task_action_reload_remote_server_configure::result_type task_action_reload_remote_server_configure::operator()() {
  std::string global_conf;
  int32_t global_version = 0;

  std::string local_conf;
  int32_t local_version = 0;

#if 0  // DB implementation
  int res = rpc::db::TABLE_SERVICE_CONFIGURE_DEF::get(get_shared_context(), 0, global_conf, &global_version);
  if (PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND == res) {
    rpc::db::TABLE_SERVICE_CONFIGURE_DEF::add(get_shared_context(), global_conf);
    res = rpc::db::TABLE_SERVICE_CONFIGURE_DEF::get(get_shared_context(), 0, global_conf, &global_version);
  }

  if (0 != res) {
    FWLOGERROR("call rpc::db::TABLE_SERVICE_CONFIGURE_DEF::get(zone=0) failed, res: {}({})", res,
               protobuf_mini_dumper_get_error_msg(res));
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SUCCESS;
  }

  res = rpc::db::TABLE_SERVICE_CONFIGURE_DEF::get(get_shared_context(), logic_config::me()->get_local_zone_id(),
                                                  local_conf, &local_version);
  if (PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND == res) {
    local_conf.set_zone_id(logic_config::me()->get_local_zone_id());
    rpc::db::TABLE_SERVICE_CONFIGURE_DEF::add(get_shared_context(), local_conf);
    res = rpc::db::TABLE_SERVICE_CONFIGURE_DEF::get(get_shared_context(), logic_config::me()->get_local_zone_id(),
                                                    local_conf, &local_version);
  }

  if (0 != res) {
    FWLOGERROR("call rpc::db::TABLE_SERVICE_CONFIGURE_DEF::get(zone={}) failed, res: {}({})",
               logic_config::me()->get_local_zone_id(), res, protobuf_mini_dumper_get_error_msg(res));
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SUCCESS;
  }
#endif

  logic_server_common_module* mod = logic_server_last_common_module();
  if (nullptr == mod) {
    return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SUCCESS;
  }
  mod->update_remote_server_configure(global_conf, global_version, local_conf, local_version);

  return PROJECT_SERVER_FRAME_NAMESPACE_ID::err::EN_SUCCESS;
}

int task_action_reload_remote_server_configure::on_success() { return get_result(); }

int task_action_reload_remote_server_configure::on_failed() { return get_result(); }
