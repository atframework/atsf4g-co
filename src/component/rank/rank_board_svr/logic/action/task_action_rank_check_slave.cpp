#include "task_action_rank_check_slave.h"

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

RANK_SERVICE_API task_action_rank_check_slave::task_action_rank_check_slave(dispatcher_start_data_type&& param)
    : base_type(COPP_MACRO_STD_MOVE(param)) {}

RANK_SERVICE_API task_action_rank_check_slave::~task_action_rank_check_slave() {}

RANK_SERVICE_API const char* task_action_rank_check_slave::name() const { return "task_action_rank_check_slave"; }

RANK_SERVICE_API task_action_rank_check_slave::result_type task_action_rank_check_slave::operator()() {
  EXPLICIT_UNUSED_ATTR const rpc_request_type& req_body = get_request_body();
  EXPLICIT_UNUSED_ATTR rpc_response_type& rsp_body = get_response_body();
  if (is_stream_rpc()) {
    disable_response_message();
  }

  protobuf_copy_message(*rsp_body.mutable_rank_key(), req_body.rank_key());
  auto rank_ptr = rank_manager::me()->get_rank(req_body.rank_key());
  if (rank_ptr && rank_ptr->is_slave_node()) {
    rsp_body.set_is_slave(true);
    rsp_body.set_data_version(rank_ptr->get_data_version());
  } else {
    rsp_body.set_is_slave(false);
  }

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

RANK_SERVICE_API int task_action_rank_check_slave::on_success() { return get_result(); }

RANK_SERVICE_API int task_action_rank_check_slave::on_failed() { return get_result(); }
