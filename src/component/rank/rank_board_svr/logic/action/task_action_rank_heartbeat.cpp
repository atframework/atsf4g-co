#include "task_action_rank_heartbeat.h"

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
#include "logic/rank_manager.h"

RANK_SERVICE_API task_action_rank_heartbeat::task_action_rank_heartbeat(dispatcher_start_data_type&& param)
    : base_type(COPP_MACRO_STD_MOVE(param)) {}

RANK_SERVICE_API task_action_rank_heartbeat::~task_action_rank_heartbeat() {}

RANK_SERVICE_API const char* task_action_rank_heartbeat::name() const { return "task_action_rank_heartbeat"; }

RANK_SERVICE_API task_action_rank_heartbeat::result_type task_action_rank_heartbeat::operator()() {
  EXPLICIT_UNUSED_ATTR const rpc_request_type& req_body = get_request_body();
  EXPLICIT_UNUSED_ATTR rpc_response_type& rsp_body = get_response_body();
  if (is_stream_rpc()) {
    disable_response_message();
  }

  auto rank_ptr = rank_manager::me()->get_rank(req_body.rank_key());
  if (!rank_ptr || !rank_ptr->is_main_node()) {
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_RANK_IS_NOT_MAIN);
  }
  if (rank_ptr->get_router_data().router_version() != req_body.router_version()) {
      FWLOGDEBUG("rank heartbeat router version mismatch, rank_key({}:{}:{}:{}) local_version:{} req_version:{}",
                  req_body.rank_key().rank_type(), req_body.rank_key().rank_instance_id(), req_body.rank_key().sub_rank_type(), req_body.rank_key().sub_rank_instance_id(),
                  rank_ptr->get_router_data().router_version(), req_body.router_version());
  }
  rank_ptr->slave_confirm_info(get_shared_context(), get_request_node_id(), req_body.data_version());

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

RANK_SERVICE_API int task_action_rank_heartbeat::on_success() { return get_result(); }

RANK_SERVICE_API int task_action_rank_heartbeat::on_failed() { return get_result(); }
