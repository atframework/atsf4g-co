#include "task_action_rank_del_one_user.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/rank_board_service.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>
#include <logic/rank_manager.h>

RANK_SERVICE_API task_action_rank_del_one_user::task_action_rank_del_one_user(dispatcher_start_data_type&& param)
    : base_type(COPP_MACRO_STD_MOVE(param)) {}

RANK_SERVICE_API task_action_rank_del_one_user::~task_action_rank_del_one_user() {}

RANK_SERVICE_API const char* task_action_rank_del_one_user::name() const { return "task_action_rank_del_one_user"; }

RANK_SERVICE_API task_action_rank_del_one_user::result_type task_action_rank_del_one_user::operator()() {
  EXPLICIT_UNUSED_ATTR const rpc_request_type& req_body = get_request_body();
  EXPLICIT_UNUSED_ATTR rpc_response_type& rsp_body = get_response_body();
  if (is_stream_rpc()) {
    disable_response_message();
  }

  rank_ptr_type rank_ptr = nullptr;
  auto ret =
      RPC_AWAIT_TYPE_RESULT(rank_manager::me()->mutable_main_rank(get_shared_context(), req_body.rank_key(), rank_ptr));
  if (ret != 0) {
    TASK_ACTION_RETURN_CODE(ret);
  }
  if (!rank_ptr) {
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_RANK_NOT_EXIST);
  }
  if (!rank_ptr->is_main_node()) {
    bool ok = false;
    ret = RPC_AWAIT_CODE_RESULT(forward_rpc(rank_ptr->get_router_data().main_server_id(), false, ok));
    if (ret != 0 || !ok) {
      FWLOGERROR("forward rank:({}:{}:{}:{}) message to dest server {} failed! ret:{} ok:{}", req_body.rank_key().rank_type(),
                 req_body.rank_key().rank_instance_id(), req_body.rank_key().sub_rank_type(), req_body.rank_key().sub_rank_instance_id(), rank_ptr->get_router_data().main_server_id(), ret, ok ? 1 : 0);
      TASK_ACTION_RETURN_CODE(ret != 0 ? ret : PROJECT_NAMESPACE_ID::EN_ERR_UNKNOWN);
    }
    FWLOGDEBUG("forward rank:({}:{}:{}:{}) message to dest server {} success", req_body.rank_key().rank_type(),
               req_body.rank_key().rank_instance_id(), req_body.rank_key().sub_rank_type(), req_body.rank_key().sub_rank_instance_id(), rank_ptr->get_router_data().main_server_id());
    TASK_ACTION_RETURN_CODE(0);
  }

  auto old_version = rank_ptr->get_data_version();

  ret = rank_ptr->del_one_user(req_body.data().user_key());

  if (rank_ptr->get_data_version() > old_version) {
    PROJECT_NAMESPACE_ID::DRankEventLog log;
    log.mutable_remove_score()->CopyFrom(req_body.data());
    rank_ptr->broadcast_events(get_shared_context(), std::move(log));
  }
  FWLOGDEBUG(" rank:({}:{}) del one user finish user:{}{} err:{}", req_body.rank_key().rank_type(),
             req_body.rank_key().rank_instance_id(), req_body.data().user_key().user_id(),
             req_body.data().user_key().zone_id(), ret);
  rsp_body.set_result(ret);
  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

RANK_SERVICE_API int task_action_rank_del_one_user::on_success() { return get_result(); }

RANK_SERVICE_API int task_action_rank_del_one_user::on_failed() { return get_result(); }
