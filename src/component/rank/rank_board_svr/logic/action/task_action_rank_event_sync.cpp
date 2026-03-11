#include "task_action_rank_event_sync.h"

#include <curl/curl.h>
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
#include "config/server_frame_build_feature.h"
#include "protocol/pbdesc/com.struct.rank.pb.h"
#include "rpc/rpc_common_types.h"

RANK_SERVICE_API task_action_rank_event_sync::task_action_rank_event_sync(dispatcher_start_data_type&& param)
    : base_type(COPP_MACRO_STD_MOVE(param)) {}

RANK_SERVICE_API task_action_rank_event_sync::~task_action_rank_event_sync() {}

RANK_SERVICE_API const char* task_action_rank_event_sync::name() const { return "task_action_rank_event_sync"; }

RANK_SERVICE_API task_action_rank_event_sync::result_type task_action_rank_event_sync::operator()() {
  EXPLICIT_UNUSED_ATTR const rpc_request_type& req_body = get_request_body();
  // Stream request or stream response, just ignore auto response
  disable_response_message();

  auto rank = rank_manager::me()->get_rank(req_body.rank_key());
  if (!rank || !rank->is_slave_node()) {
    FWLOGERROR("current node is not slave node rank({}:{}) node:{}", req_body.rank_key().rank_type(),
               req_body.rank_key().rank_instance_id(), logic_config::me()->get_local_server_id());
    TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_RANK_IS_NOT_SLAVE);
  }
  // TODO 比较数据版本号

  for (auto& event : req_body.event_logs()) {
    if (event.event_id() <= rank->get_data_version()) {
      continue;
    }
    if (event.event_id() != rank->get_data_version() + 1) {
      FWLOGDEBUG("event logs is break!!!! rank({}:{}) log_event_id:{} cur_event_id:{}", req_body.rank_key().rank_type(),
                 req_body.rank_key().rank_instance_id(), event.event_id(), rank->get_data_version());
      break;
    }
    int ret = 0;
    switch (event.event_case()) {
      case PROJECT_NAMESPACE_ID::DRankEventLog::kUpdateScore:
        rank->update_score(event.update_score());
        break;
      case PROJECT_NAMESPACE_ID::DRankEventLog::kRemoveScore:
        rank->del_one_user(event.remove_score().user_key());
        break;
      case PROJECT_NAMESPACE_ID::DRankEventLog::kRankClear:
        rank->clear_rank();
        break;
      default:
        FWLOGERROR("unsupport event type:{}", static_cast<int>(event.event_case()));
        break;
    }
    if (ret != 0) {
      FWLOGERROR("sync rank event failed rank({}:{}:{}:{}) event_id:{} event_case:{} ret:{}", req_body.rank_key().rank_type(),
                 req_body.rank_key().rank_instance_id(), req_body.rank_key().sub_rank_type(), req_body.rank_key().sub_rank_instance_id(), event.event_id(), static_cast<int>(event.event_case()), ret);
    } else {
      FWLOGDEBUG("sync rank event success rank({}:{}:{}:{}) event_id:{} event_case:{}", req_body.rank_key().rank_type(),
                 req_body.rank_key().rank_instance_id(), req_body.rank_key().sub_rank_type(), req_body.rank_key().sub_rank_instance_id(), event.event_id(), static_cast<int>(event.event_case()));
    }
  }

  TASK_ACTION_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
}

RANK_SERVICE_API int task_action_rank_event_sync::on_success() { return get_result(); }

RANK_SERVICE_API int task_action_rank_event_sync::on_failed() { return get_result(); }
