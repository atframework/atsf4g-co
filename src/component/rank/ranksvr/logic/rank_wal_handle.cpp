#include "logic/rank_wal_handle.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/config/com.const.config.pb.h>
#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.protocol.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <config/excel_config_const_index.h>
#include <config/logic_config.h>

#include <utility/protobuf_mini_dumper.h>

#include <dispatcher/task_manager.h>

#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_utils.h>
#include "rpc/rank/ranksvrservice.h"

#include "logic/rank.h"

rank_wal_publisher_context::rank_wal_publisher_context(rpc::context& ctx, int32_t& output_result)
    : context(std::ref(ctx)), result_code(std::ref(output_result)) {}

static rank_wal_publisher_type::vtable_pointer create_rank_publisher_vtable() {
  using wal_object_type = rank_wal_publisher_type::object_type;
  using wal_publisher_type = rank_wal_publisher_type;
  using wal_result_code = util::distributed_system::wal_result_code;

  static wal_publisher_type::vtable_pointer ret;
  if (ret) {
    return ret;
  }

  ret = rank_wal_publisher_log_operator::make_strong<wal_publisher_type::vtable_type>();
  if (!ret) {
    return ret;
  }

  // ============ callbacks for wal_object ============
  ret->load = [](ATFW_EXPLICIT_UNUSED_ATTR wal_object_type& wal,
                 ATFW_EXPLICIT_UNUSED_ATTR const wal_object_type::storage_type& from,
                 wal_object_type::callback_param_type) -> wal_result_code {
    // Stateless, no need to load
    return wal_result_code::kOk;
  };

  ret->dump = [](ATFW_EXPLICIT_UNUSED_ATTR const wal_object_type& wal,
                 ATFW_EXPLICIT_UNUSED_ATTR ATFW_EXPLICIT_UNUSED_ATTR wal_object_type::storage_type& to,
                 wal_object_type::callback_param_type) -> wal_result_code {
    // Stateless, no need to dump
    return wal_result_code::kOk;
  };

  ret->get_meta = [](const wal_object_type&,
                     const wal_object_type::log_type& log) -> wal_object_type::meta_result_type {
    auto tp = std::chrono::system_clock::from_time_t(log.timepoint().seconds()) +
              std::chrono::nanoseconds{log.timepoint().nanos()};
    auto tp_cast = std::chrono::time_point_cast<std::chrono::system_clock::duration>(tp);
    return wal_object_type::meta_result_type::make_success(tp_cast, log.event_id(), log.event_case());
  };

  ret->set_meta = [](const wal_object_type&, wal_object_type::log_type& log, const wal_object_type::meta_type& meta) {
    // log.event_case = meta.action_case; // event_case will be created by mutable_*
    time_t seconds = std::chrono::system_clock::to_time_t(meta.timepoint);
    int32_t nanos = static_cast<int32_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                             meta.timepoint - std::chrono::system_clock::from_time_t(seconds))
                                             .count());
    log.mutable_timepoint()->set_seconds(seconds);
    log.mutable_timepoint()->set_nanos(nanos);
  };

  ret->merge_log = [](const wal_object_type&, wal_object_type::callback_param_type, wal_object_type::log_type& to,
                      const wal_object_type::log_type& from) {
    FWLOGERROR("Merge rank WAL failed(should not happen) from\n{}to\n{}", from.DebugString(), to.DebugString());
  };

  ret->get_log_key = [](const wal_object_type&, const wal_object_type::log_type& log) -> wal_object_type::log_key_type {
    return log.event_id();
  };

  ret->allocate_log_key = [](wal_object_type& wal, const wal_object_type::log_type& log,
                             wal_object_type::callback_param_type) -> wal_object_type::log_key_result_type {
    if (log.event_id() > 0) {
      return wal_object_type::log_key_result_type::make_success(log.event_id());
    }
    auto private_data = wal.get_private_data();
    FWLOGERROR("log event id must invalid rank_type: {} event_cast:{}",
               private_data ? private_data->get_key().rank_type() : 0, static_cast<int32_t>(log.event_case()));

    return wal_object_type::log_key_result_type::make_error(wal_result_code::kInitlization);
  };
  ret->default_delegate.action = [](wal_object_type&, const wal_object_type::log_type&,
                                    wal_object_type::callback_param_type) -> wal_result_code {
    return wal_result_code::kOk;
  };

  // ============ callbacks for wal_publisher ============
  ret->send_snapshot = [](ATFW_EXPLICIT_UNUSED_ATTR wal_publisher_type& publisher,
                          wal_publisher_type::subscriber_iterator subscriber_begin,
                          wal_publisher_type::subscriber_iterator subscriber_end,
                          ATFW_EXPLICIT_UNUSED_ATTR wal_publisher_type::callback_param_type param) -> wal_result_code {
    std::unordered_map<uint64_t, google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DPlayerIDKey>*>
        notify_servers;

    for (; subscriber_begin != subscriber_end; ++subscriber_begin) {
      auto& user = *subscriber_begin;
      if (!user.second || !user.second->get_private_data()) {
        continue;
      }
      auto& private_data = *user.second->get_private_data();
      FWLOGDEBUG("send rank snapshot event to server:{} rank:({}:{}:{}:{})", private_data.server_id(),
                 private_data.rank_key().rank_type(), private_data.rank_key().rank_instance_id(),
                 private_data.rank_key().sub_rank_type(), private_data.rank_key().sub_rank_instance_id());
    }

    return wal_result_code::kOk;
  };

  ret->send_logs = [](wal_publisher_type& publisher, wal_publisher_type::log_const_iterator log_begin,
                      wal_publisher_type::log_const_iterator log_end,
                      wal_publisher_type::subscriber_iterator subscriber_begin,
                      wal_publisher_type::subscriber_iterator subscriber_end,
                      wal_publisher_type::callback_param_type param) -> wal_result_code {
    auto publish_private_data = publisher.get_private_data();
    if (!publish_private_data) {
      return wal_result_code::kInvalidParam;
    }

    rpc::context::message_holder<PROJECT_NAMESPACE_ID::SSRankEventSync> sync_body(param.context);
    protobuf_copy_message(*sync_body->mutable_rank_key(), publish_private_data->get_key());

    for (; log_begin != log_end; ++log_begin) {
      auto log = sync_body->add_event_logs();
      if (nullptr == log) {
        WLOGERROR("malloc event log failed");
        break;
      }

      if (!(*log_begin)) {
        continue;
      }

      protobuf_copy_message(*log, **log_begin);
    }
    for (; subscriber_begin != subscriber_end; ++subscriber_begin) {
      if (!subscriber_begin->second || !subscriber_begin->second->get_private_data()) {
        continue;
      }
      auto private_data = subscriber_begin->second->get_private_data();
      if (!private_data) {
        continue;
      }

      int32_t res = rpc::rank::rank_event_sync(param.context, private_data->server_id(), *sync_body);
      if (res != 0) {
        FWLOGERROR(
            "send rank event sync to server failed rank_id({}:{}:{}:{}) cur_server_id:{} subcribe_server_id:{} res: "
            "{}({})",
            private_data->rank_key().rank_type(), private_data->rank_key().rank_instance_id(),
            private_data->rank_key().sub_rank_type(), private_data->rank_key().sub_rank_instance_id(),
            logic_config::me()->get_local_server_id(), private_data->server_id(), res,
            protobuf_mini_dumper_get_error_msg(res));
      } else {
        FWLOGDEBUG("send rank event sync to server success rank_id({}:{}:{}:{}) cur_server_id:{} subcribe_server_id:{}",
                   private_data->rank_key().rank_type(), private_data->rank_key().rank_instance_id(),
                   private_data->rank_key().sub_rank_type(), private_data->rank_key().sub_rank_instance_id(),
                   logic_config::me()->get_local_server_id(), private_data->server_id());
      }
    }

    return wal_result_code::kOk;
  };

  ret->on_subscriber_removed =
      [](wal_publisher_type& publisher, const wal_publisher_type::subscriber_pointer& subscribe,
         util::distributed_system::wal_unsubscribe_reason reason, wal_publisher_type::callback_param_type) -> bool {
    FWLOGDEBUG("on_subscriber_removed rank_type: {} cur_server:{} slave_server:{} reason:{}",
               publisher.get_private_data() ? publisher.get_private_data()->get_key().rank_type() : 0,
               logic_config::me()->get_local_server_id(),
               subscribe->get_private_data() ? subscribe->get_private_data()->server_id() : 0,
               static_cast<int>(reason));
    return true;
  };

  return ret;
}

static rank_wal_publisher_type::configure_pointer create_rank_publisher_congigure() {
  rank_wal_publisher_type::configure_pointer ret = rank_wal_publisher_type::make_configure();
  if (!ret) {
    return ret;
  }
  ret->enable_last_broadcast_for_removed_subscriber = false;
  ret->gc_expire_duration =
      std::chrono::seconds{logic_config::me()
                               ->get_custom_config<PROJECT_NAMESPACE_ID::config::ranksvr_cfg>()
                               .pushlisher_congihure()
                               .gc_expire_duration()};
  ret->gc_log_size = logic_config::me()
                         ->get_custom_config<PROJECT_NAMESPACE_ID::config::ranksvr_cfg>()
                         .pushlisher_congihure()
                         .gc_log_size();
  ret->max_log_size = logic_config::me()
                          ->get_custom_config<PROJECT_NAMESPACE_ID::config::ranksvr_cfg>()
                          .pushlisher_congihure()
                          .max_log_size();

  // ret->subscriber_timeout = std::chrono::seconds{10};

  return ret;
}

rank_wal_publisher_log_operator::strong_ptr<rank_wal_publisher_type> create_rank_publisher(rank& rank_obj) {
  return rank_wal_publisher_type::create(create_rank_publisher_vtable(), create_rank_publisher_congigure(), &rank_obj);
}
