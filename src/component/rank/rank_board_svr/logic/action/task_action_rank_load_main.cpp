#include "task_action_rank_load_main.h"

#include <log/log_wrapper.h>
#include <std/explicit_declare.h>
#include <time/time_utility.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/rank_board_service.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include "protocol/pbdesc/com.const.pb.h"

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <utility/protobuf_mini_dumper.h>

#include <config/extern_service_types.h>

#include <logic/rank_manager.h>
#include "config/server_frame_build_feature.h"
#include "dispatcher/task_action_base.h"
#include "logic/rank.h"

#include "rpc/rpc_common_types.h"

#include <atframe/etcdcli/etcd_discovery.h>
#include <logic/hpa/logic_hpa_easy_api.h>

#include <utility/random_engine.h>

RANK_SERVICE_API task_action_rank_load_main::task_action_rank_load_main(dispatcher_start_data_type&& param)
    : base_type(COPP_MACRO_STD_MOVE(param)) {}

RANK_SERVICE_API task_action_rank_load_main::~task_action_rank_load_main() {}

RANK_SERVICE_API const char* task_action_rank_load_main::name() const { return "task_action_rank_load_main"; }

RANK_SERVICE_API task_action_rank_load_main::result_type task_action_rank_load_main::operator()() {
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
  protobuf_copy_message(*rsp_body.mutable_rank_key(), req_body.rank_key());
  protobuf_copy_message(*rsp_body.mutable_router_data(), rank_ptr->get_router_data());

  TASK_ACTION_RETURN_CODE(0);
}

RANK_SERVICE_API int task_action_rank_load_main::on_success() { return get_result(); }

RANK_SERVICE_API int task_action_rank_load_main::on_failed() { return get_result(); }