// Copyright 2026 atframework

#include "task_action_login_auth.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/authsvr_config.pb.h>
#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <rpc/db/local_db_interface.atfw.gen.h>
#include <rpc/db/uuid.h>

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

#include <dispatcher/cs_msg_dispatcher.h>

#include "rpc/rpc_common_types.h"
#include "rpc/rpc_context.h"

#include "data/session.h"

#include "app/authsvr_helper.h"

task_action_login_auth::task_action_login_auth(dispatcher_start_data_type&& param) : base_type(std::move(param)) {}
task_action_login_auth::~task_action_login_auth() {}

const char* task_action_login_auth::name() const { return "task_action_login_auth"; }

task_action_login_auth::result_type task_action_login_auth::operator()() {
  const rpc_request_type& req_body = get_request_body();
  rpc_response_type& rsp_body = get_response_body();

  rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_auth> login_auth_tb{get_shared_context()};
  uint64_t login_auth_cas_version = 0;

  auto sess = get_session();
  session::key_t session_key{};
  if (sess) {
    session_key = sess->get_key();
  }

  // 拉取鉴权表
  int res = RPC_AWAIT_CODE_RESULT(
      rpc::db::login_auth::get_all(get_shared_context(), req_body.open_id(), *login_auth_tb, login_auth_cas_version));
  if (res < 0 && res != PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
    FCTXLOGERROR(get_shared_context(), "session {}:{}, user {} try to get login auth table failed, error code: {}({})",
                 session_key.node_id, session_key.session_id, req_body.open_id(), res,
                 protobuf_mini_dumper_get_error_msg(res));
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_OPENID_NOT_FOUND);
    RPC_RETURN_CODE(res);
  }
  if (login_auth_tb->open_id().empty()) {
    login_auth_tb->set_open_id(req_body.open_id());
  }

  // TODO: 鉴权实现

  // 如果是新用户，需要创建user_id
  if (login_auth_tb->user_id() == 0) {
    int64_t new_user_id = RPC_AWAIT_CODE_RESULT(rpc::db::uuid::generate_global_unique_id(
        get_shared_context(), PROJECT_NAMESPACE_ID::EN_GLOBAL_UUID_MAT_USER_ID, 0, 0));
    if (new_user_id <= 0) {
      FCTXLOGERROR(get_shared_context(), "session {}:{}, user {} try to allocate user_id failed, error code: {}({})",
                   session_key.node_id, session_key.session_id, req_body.open_id(), new_user_id,
                   protobuf_mini_dumper_get_error_msg(static_cast<int>(new_user_id)));
      set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM);
      res = static_cast<int>(new_user_id);
      RPC_RETURN_CODE(res);
    }

    login_auth_tb->set_user_id(static_cast<uint64_t>(new_user_id));

    res =
        RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::insert(get_shared_context(), login_auth_tb, &login_auth_cas_version));
    if (res < 0) {
      FCTXLOGERROR(get_shared_context(), "session {}:{}, user {} try to save user_id failed, error code: {}({})",
                   session_key.node_id, session_key.session_id, req_body.open_id(), res,
                   protobuf_mini_dumper_get_error_msg(res));
      set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM);
      RPC_RETURN_CODE(res);
    }
  }

  rsp_body.set_open_id(login_auth_tb->open_id());
  rsp_body.set_user_id(login_auth_tb->user_id());

  rpc::shared_message<PROJECT_NAMESPACE_ID::table_login_lock> login_lock_tb{get_shared_context()};
  uint64_t login_lock_cas_version = 0;
  res = RPC_AWAIT_CODE_RESULT(rpc::db::login_lock::get_all(get_shared_context(), login_auth_tb->user_id(),
                                                           *login_lock_tb, login_lock_cas_version));
  if (res < 0 && res != PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND) {
    FCTXLOGERROR(get_shared_context(),
                 "session {}:{}, user {}(user_id={}) try to get login lock table failed, error code: {}({})",
                 session_key.node_id, session_key.session_id, login_auth_tb->open_id(), login_auth_tb->user_id(), res,
                 protobuf_mini_dumper_get_error_msg(res));
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_OPENID_NOT_FOUND);
    RPC_RETURN_CODE(res);
  }

  // 封禁检查
  if (login_lock_tb->ban_time() > atfw::util::time::time_utility::get_now()) {
    FCTXLOGINFO(get_shared_context(), "session {}:{}, user {}(user_id={}) login is banned until {}, login rejected",
                session_key.node_id, session_key.session_id, login_auth_tb->open_id(), login_auth_tb->user_id(),
                login_lock_tb->ban_time());
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_LOGIN_BAN);

    rsp_body.set_ban_time(login_lock_tb->ban_time());
    RPC_RETURN_CODE(0);
  }

  // 选取路由服务，尽量复用老的节点，增加缓存命中率和降低时序风险
  uint64_t router_server_id = login_lock_tb->router_server_id();
  std::string router_server_name;
  if (router_server_id != 0 &&
      protobuf_to_system_clock(login_lock_tb->access_token_expired()) < atfw::util::time::time_utility::sys_now()) {
    // 如果登录锁里token过期了，说明之前的session也过期了，可以清理掉路由信息
    router_server_id = 0;
  }

  res = RPC_AWAIT_CODE_RESULT(select_router_server_id(login_auth_tb->open_id(), login_auth_tb->user_id(),
                                                      router_server_id, router_server_name));
  if (res < 0) {
    FCTXLOGERROR(get_shared_context(),
                 "session {}:{}, user {}(user_id={}) try to get router server failed, error code: {}({})",
                 session_key.node_id, session_key.session_id, login_auth_tb->open_id(), login_auth_tb->user_id(), res,
                 protobuf_mini_dumper_get_error_msg(res));
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_CONNECT_FAILED);
    RPC_RETURN_CODE(res);
  }

  if (router_server_id == 0) {
    FCTXLOGINFO(get_shared_context(),
                "session {}:{}, user {}(user_id={}) can not login because there is no available backend server",
                session_key.node_id, session_key.session_id, login_auth_tb->open_id(), login_auth_tb->user_id());
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_CONNECT_FAILED);
    RPC_RETURN_CODE(res);
  }

  // 发送设置路由到atgateway
  if (sess) {
    res = cs_msg_dispatcher::me()->send_set_router(session_key.node_id, session_key.session_id, router_server_id,
                                                   router_server_name);
    if (res < 0) {
      FCTXLOGERROR(get_shared_context(),
                   "session {}:{}, user {}(user_id={}) try to set router server to {},{} failed, error code: {}({})",
                   session_key.node_id, session_key.session_id, login_auth_tb->open_id(), login_auth_tb->user_id(),
                   router_server_id, router_server_name, res, protobuf_mini_dumper_get_error_msg(res));
      set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_CONNECT_FAILED);
      RPC_RETURN_CODE(res);
    }
  }

  // 创建新的access_token
  login_auth_tb->set_access_token_code(rpc::db::uuid::generate_short_uuid());

  auto valid_duration =
      protobuf_to_chrono_duration<>(logic_config::me()->get_logic_cfg().session().access_token_code_valid_duration());
  auto token_timeout = atfw::util::time::time_utility::now() + valid_duration;
  protobuf_copy_message(*login_auth_tb->mutable_access_token_expired(), protobuf_from_system_clock(token_timeout));
  res =
      RPC_AWAIT_CODE_RESULT(rpc::db::login_auth::replace(get_shared_context(), login_auth_tb, login_auth_cas_version));
  if (res < 0) {
    FCTXLOGERROR(get_shared_context(),
                 "session {}:{}, user {}(user_id={}) try to save access_token failed, error code: {}({})",
                 session_key.node_id, session_key.session_id, login_auth_tb->open_id(), login_auth_tb->user_id(), res,
                 protobuf_mini_dumper_get_error_msg(res));
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM);
    RPC_RETURN_CODE(res);
  }

  rsp_body.set_access_token_code(login_auth_tb->access_token_code());

  rsp_body.set_version_type(PROJECT_NAMESPACE_ID::EN_VERSION_DEFAULT);

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

int task_action_login_auth::on_success() { return get_result(); }

int task_action_login_auth::on_failed() { return get_result(); }

task_action_login_auth::result_type task_action_login_auth::select_router_server_id(
    atfw::util::nostd::string_view openid, uint64_t user_id, uint64_t& router_server_id,
    std::string& router_server_name) {
  atfw::component::service_discovery_index::ptr_t discovery_index = authsvr_get_service_discovery_index();
  if (!discovery_index) {
    FCTXLOGINFO(get_shared_context(), "user {}(user_id={}) login is canceled because app instance is shutdown", openid,
                user_id);
    set_response_code(PROJECT_NAMESPACE_ID::EN_ERR_MAINTENANCE);
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_SERVER_SHUTDOWN);
  }

  // 如果节点已经下线了，也清理掉路由信息
  atfw::component::service_discovery_index::discovery_node_ptr_t select_node;
  if (router_server_id != 0) {
    select_node = discovery_index->get_discovery_by_id(router_server_id);
  }

  if (!select_node && !router_server_name.empty()) {
    select_node = discovery_index->get_discovery_by_name(router_server_name);
  }

  if (select_node) {
    // 优先复用老节点
    router_server_id = select_node->get_discovery_info().id();
    router_server_name = std::string{select_node->get_discovery_info().name()};
    RPC_RETURN_CODE(0);
  }

  const auto& server_cfg = logic_config::me()->get_server_instance_config<PROJECT_NAMESPACE_ID::config::authsvr_cfg>();
  bool by_setting = true;
  const atfw::atapp::protocol::atapp_metadata* policy_selector = nullptr;
  if (server_cfg.backend().router().has_policy_selector()) {
    policy_selector = &server_cfg.backend().router().policy_selector();
  }

  atfw::atapp::etcd_discovery_set::ptr_t select_set;
  if (discovery_index) {
    if (server_cfg.backend().router().type_id() != 0) {
      select_set = discovery_index->get_discovery_index_by_type(server_cfg.backend().router().type_id());
    } else if (!server_cfg.backend().router().type_name().empty()) {
      select_set = discovery_index->get_discovery_index_by_type(server_cfg.backend().router().type_name());
    }
  }

  switch (server_cfg.backend().router().policy()) {
    case PROJECT_NAMESPACE_ID::config::EN_AUTHSVR_ROUTER_POLICY_RANDOM: {
      if (!select_set) {
        break;
      }

      select_node = select_set->get_node_by_random(policy_selector);
      if (select_node) {
        router_server_id = select_node->get_discovery_info().id();
        router_server_name = std::string{select_node->get_discovery_info().name()};
        by_setting = false;
      }
      break;
    }
    case PROJECT_NAMESPACE_ID::config::EN_AUTHSVR_ROUTER_POLICY_ROUND_ROBIN: {
      if (!select_set) {
        break;
      }

      select_node = select_set->get_node_by_round_robin(policy_selector);
      if (select_node) {
        router_server_id = select_node->get_discovery_info().id();
        router_server_name = std::string{select_node->get_discovery_info().name()};
        by_setting = false;
      }
      break;
    }
    case PROJECT_NAMESPACE_ID::config::EN_AUTHSVR_ROUTER_POLICY_HASH: {
      if (!select_set) {
        break;
      }

      select_node = select_set->get_node_hash_by_consistent_hash(user_id, policy_selector).node;
      if (select_node) {
        router_server_id = select_node->get_discovery_info().id();
        router_server_name = std::string{select_node->get_discovery_info().name()};
        by_setting = false;
      }
      break;
    }
    default:
      break;
  }

  if (by_setting && discovery_index) {
    if (server_cfg.backend().router().node_id() != 0) {
      auto find_node = discovery_index->get_discovery_by_id(server_cfg.backend().router().node_id());
      if (find_node) {
        router_server_id = find_node->get_discovery_info().id();
        router_server_name = std::string{find_node->get_discovery_info().name()};
        by_setting = false;
      }
    }

    if (by_setting && !server_cfg.backend().router().node_name().empty()) {
      auto find_node = discovery_index->get_discovery_by_name(server_cfg.backend().router().node_name());
      if (find_node) {
        router_server_id = find_node->get_discovery_info().id();
        router_server_name = std::string{find_node->get_discovery_info().name()};
        by_setting = false;
      }
    }
  }

  if (router_server_id == 0 && router_server_name.empty()) {
    FCTXLOGWARNING(get_shared_context(),
                   "user {}(user_id={}) login select router server failed, no available backend server", openid,
                   user_id);
  } else {
    FWLOGINFO("user {}(user_id={}) login set router to {}:{}", openid, user_id, router_server_id, router_server_name);
  }

  RPC_RETURN_CODE(0);
}
