// Copyright 2026 atframework

#include "logic/matching/matching_wal_handle.h"

#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <rpc/matching/matchsvrnotifyservice.atfw.gen.h>
#include <rpc/rpc_context.h>
#include <rpc/rpc_shared_message.h>

#include <utility/protobuf_mini_dumper.h>

#include <algorithm>
#include <chrono>
#include <map>
#include <utility>
#include <vector>

#include "logic/matching/matching_room.h"

matching_wal_context::matching_wal_context(rpc::context& ctx, int32_t& output_result)
    : context(std::ref(ctx)), result_code(std::ref(output_result)) {}

namespace {
using wal_result_code = atfw::util::distributed_system::wal_result_code;

using subscriber_group_key = std::pair<uint64_t, uint64_t>;
using subscriber_group = std::map<subscriber_group_key, std::vector<matching_wal_publisher::subscriber_pointer>>;

subscriber_group collect_subscribers(matching_wal_publisher::subscriber_iterator begin,
                                     matching_wal_publisher::subscriber_iterator end) {
  subscriber_group result;
  for (; begin != end; ++begin) {
    auto subscriber = begin->second;
    if (!subscriber || !subscriber->get_private_data() || subscriber->get_private_data()->server_id() == 0 ||
        subscriber->is_offline(atfw::util::time::time_utility::now())) {
      continue;
    }
    if (subscriber->get_private_data()->unit_id() == 0) {
      continue;
    }
    result[{subscriber->get_private_data()->server_id(), subscriber->get_private_data()->unit_id()}].emplace_back(
        std::move(subscriber));
  }
  return result;
}

void send_to_subscribers(matching_wal_publisher& publisher, const subscriber_group& groups,
                         const PROJECT_NAMESPACE_ID::SSMatchingEventSync& message, matching_wal_context param,
                         int64_t last_event_id) {
  auto* room = publisher.get_private_data();
  if (!room) {
    return;
  }
  for (const auto& group : groups) {
    rpc::context::message_holder<PROJECT_NAMESPACE_ID::SSMatchingEventSync> group_message(param.context.get());
    group_message->set_matching_id(message.matching_id());
    if (!room->dump_player_view(group.first.second, *group_message->mutable_player_view())) {
      bool found_removed_unit = false;
      for (const auto& event_log : message.event_logs()) {
        if (event_log.event_case() == PROJECT_NAMESPACE_ID::DMatchingEventLog::kRemoveUnit &&
            event_log.remove_unit().unit().unit_id() == group.first.second) {
          room->dump_player_view(event_log.remove_unit().unit(), *group_message->mutable_player_view());
          found_removed_unit = true;
          break;
        }
      }
      if (!found_removed_unit) {
        continue;
      }
    }
    for (const auto& subscriber : group.second) {
      if (!subscriber || !subscriber->get_private_data()) {
        continue;
      }
      protobuf_copy_message(*group_message->add_user_keys(), subscriber->get_key());
      subscriber->get_private_data()->set_last_send_event_id(
          std::max(last_event_id, subscriber->get_private_data()->last_send_event_id()));
    }
    for (const auto& event_log : message.event_logs()) {
      if (event_log.event_case() == PROJECT_NAMESPACE_ID::DMatchingEventLog::kAddUnit &&
          event_log.add_unit().unit_id() != group.first.second) {
        continue;
      }
      if (event_log.event_case() == PROJECT_NAMESPACE_ID::DMatchingEventLog::kRemoveUnit &&
          event_log.remove_unit().unit().unit_id() != group.first.second) {
        continue;
      }
      auto* output_event = group_message->add_event_logs();
      protobuf_copy_message(*output_event, event_log);
      if (output_event->event_case() == PROJECT_NAMESPACE_ID::DMatchingEventLog::kMatched) {
        // 成局状态和 Orbit 信息由 player_view 表达，事件本身不再携带完整房间快照。
        output_event->mutable_matched()->Clear();
      }
    }
    const int32_t result = rpc::matching::matching_event_sync(param.context.get(), group.first.first, *group_message).unwrap();
    if (result != 0) {
      FCTXLOGERROR(param.context.get(), "matching {} send WAL to lobbysvr {:#x} failed, result: {}({})",
                   room->get_matching_id(), group.first.first, result, protobuf_mini_dumper_get_error_msg(result));
      param.result_code.get() = result;
    } else {
      FCTXLOGDEBUG(param.context.get(),
                   "matching {} send WAL to lobbysvr {:#x} finish, users={}, view={}, event_count={}, "
                   "last_event_id={}",
                   room->get_matching_id(), group.first.first, group_message->user_keys_size(),
                   group_message->has_player_view(), group_message->event_logs_size(), last_event_id);
    }
  }
}

matching_wal_publisher::vtable_pointer create_vtable() {
  using wal_object = matching_wal_publisher::object_type;
  static matching_wal_publisher::vtable_pointer result;
  if (result) {
    return result;
  }
  result = matching_wal_log_operator::make_strong<matching_wal_publisher::vtable_type>();
  if (!result) {
    return result;
  }

  result->load = [](wal_object&, const wal_object::storage_type&, wal_object::callback_param_type) {
    return wal_result_code::kOk;
  };
  result->dump = [](const wal_object&, wal_object::storage_type&, wal_object::callback_param_type) {
    return wal_result_code::kOk;
  };
  result->get_meta = [](const wal_object&, const wal_object::log_type& log) {
    auto timepoint = std::chrono::system_clock::from_time_t(log.timepoint().seconds()) +
                     std::chrono::nanoseconds{log.timepoint().nanos()};
    auto cast_timepoint = std::chrono::time_point_cast<std::chrono::system_clock::duration>(timepoint);
    return wal_object::meta_result_type::make_success(cast_timepoint, log.event_id(), log.event_case());
  };
  result->set_meta = [](const wal_object&, wal_object::log_type& log, const wal_object::meta_type& meta) {
    const time_t seconds = std::chrono::system_clock::to_time_t(meta.timepoint);
    const int32_t nanos = static_cast<int32_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                   meta.timepoint - std::chrono::system_clock::from_time_t(seconds))
                                                   .count());
    log.mutable_timepoint()->set_seconds(seconds);
    log.mutable_timepoint()->set_nanos(nanos);
    log.set_event_id(meta.log_key);
  };
  result->merge_log = [](const wal_object&, wal_object::callback_param_type, wal_object::log_type& to,
                         const wal_object::log_type& from) {
    FWLOGERROR("matching WAL logs must not merge, from:\n{}to:\n{}", from.DebugString(), to.DebugString());
  };
  result->get_log_key = [](const wal_object&, const wal_object::log_type& log) { return log.event_id(); };
  result->allocate_log_key = [](wal_object& wal, const wal_object::log_type& log, wal_object::callback_param_type) {
    if (log.event_id() > 0) {
      return wal_object::log_key_result_type::make_success(log.event_id());
    }
    auto* room = wal.get_private_data();
    return room ? wal_object::log_key_result_type::make_success(room->get_last_event_id() + 1)
                : wal_object::log_key_result_type::make_error(wal_result_code::kInitlization);
  };
  result->default_delegate.action = [](wal_object&, const wal_object::log_type&, wal_object::callback_param_type) {
    return wal_result_code::kOk;
  };

  result->send_snapshot = [](matching_wal_publisher& publisher, matching_wal_publisher::subscriber_iterator begin,
                             matching_wal_publisher::subscriber_iterator end,
                             matching_wal_publisher::callback_param_type param) {
    auto* room = publisher.get_private_data();
    if (!room || begin == end) {
      return wal_result_code::kOk;
    }
    rpc::context::message_holder<PROJECT_NAMESPACE_ID::SSMatchingEventSync> message(param.context.get());
    message->set_matching_id(room->get_matching_id());
    send_to_subscribers(publisher, collect_subscribers(begin, end), *message, param, room->get_last_event_id());
    return wal_result_code::kOk;
  };
  result->send_logs = [](matching_wal_publisher& publisher, matching_wal_publisher::log_const_iterator log_begin,
                         matching_wal_publisher::log_const_iterator log_end,
                         matching_wal_publisher::subscriber_iterator subscriber_begin,
                         matching_wal_publisher::subscriber_iterator subscriber_end,
                         matching_wal_publisher::callback_param_type param) {
    auto* room = publisher.get_private_data();
    if (!room || log_begin == log_end || subscriber_begin == subscriber_end) {
      return wal_result_code::kOk;
    }
    rpc::context::message_holder<PROJECT_NAMESPACE_ID::SSMatchingEventSync> message(param.context.get());
    message->set_matching_id(room->get_matching_id());
    int64_t last_event_id = 0;
    for (; log_begin != log_end; ++log_begin) {
      if (!*log_begin) {
        continue;
      }
      protobuf_copy_message(*message->add_event_logs(), **log_begin);
      last_event_id = std::max(last_event_id, (*log_begin)->event_id());
    }
    send_to_subscribers(publisher, collect_subscribers(subscriber_begin, subscriber_end), *message, param,
                        last_event_id);
    return wal_result_code::kOk;
  };
  result->check_subscriber = [](matching_wal_publisher&, const matching_wal_publisher::subscriber_pointer&,
                                matching_wal_publisher::callback_param_type) { return true; };
  result->subscriber_force_sync_snapshot =
      [](matching_wal_publisher&, const matching_wal_publisher::subscriber_pointer& subscriber, int64_t log_key,
         const matching_wal_publisher::hash_code_type*, matching_wal_publisher::callback_param_type) {
        return subscriber && subscriber->get_private_data() &&
               log_key < subscriber->get_private_data()->valid_event_id_bound();
      };
  result->on_subscriber_removed = [](matching_wal_publisher&, const matching_wal_publisher::subscriber_pointer&,
                                     atfw::util::distributed_system::wal_unsubscribe_reason,
                                     matching_wal_publisher::callback_param_type) { return true; };
  return result;
}

matching_wal_publisher::configure_pointer create_configure() {
  auto result = matching_wal_publisher::make_configure();
  if (!result) {
    return result;
  }
  result->enable_last_broadcast_for_removed_subscriber = false;
  result->gc_expire_duration = std::chrono::hours{1};
  result->gc_log_size = 30;
  result->max_log_size = 300;
  result->subscriber_timeout = std::chrono::minutes{3};
  return result;
}
}  // namespace

matching_wal_log_operator::strong_ptr<matching_wal_publisher> create_matching_wal_publisher(matching_room& room) {
  return matching_wal_publisher::create(create_vtable(), create_configure(), &room);
}
