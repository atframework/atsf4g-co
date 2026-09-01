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
#include <utility>

#include "logic/matching/matching_unit.h"

matching_wal_context::matching_wal_context(rpc::context& ctx, int32_t& output_result)
    : context(std::ref(ctx)), result_code(std::ref(output_result)) {}

namespace matching_wal_detail {
matching_wal_subscriber_group collect_subscribers(matching_wal_publisher::subscriber_iterator begin,
                                                  matching_wal_publisher::subscriber_iterator end) {
  matching_wal_subscriber_group result;
  for (; begin != end; ++begin) {
    auto subscriber = begin->second;
    if (!subscriber || !subscriber->get_private_data() || subscriber->get_private_data()->server_id() == 0 ||
        subscriber->is_offline(atfw::util::time::time_utility::now())) {
      continue;
    }
    result[subscriber->get_private_data()->server_id()].emplace_back(std::move(subscriber));
  }
  return result;
}

void send_to_subscribers(matching_wal_publisher& publisher, const matching_wal_subscriber_group& groups,
                         const PROJECT_NAMESPACE_ID::SSMatchingEventSync& message, matching_wal_context param,
                         int64_t last_event_id) {
  auto* unit = publisher.get_private_data();
  if (!unit) {
    return;
  }
  for (const auto& group : groups) {
    rpc::context::message_holder<PROJECT_NAMESPACE_ID::SSMatchingEventSync> group_message(param.context.get());
    group_message->set_unit_id(unit->get_unit_id());
    protobuf_copy_message(*group_message->mutable_unit_view(), unit->get_view());
    for (const auto& subscriber : group.second) {
      if (subscriber && subscriber->get_private_data()) {
        protobuf_copy_message(*group_message->add_user_keys(), subscriber->get_key());
      }
    }
    for (const auto& event_log : message.event_logs()) {
      protobuf_copy_message(*group_message->add_event_logs(), event_log);
    }
    const int32_t result = rpc::matching::matching_event_sync(param.context.get(), group.first, *group_message).unwrap();
    if (result != 0) {
      FCTXLOGERROR(param.context.get(), "matching Unit {} send WAL to lobbysvr {:#x} failed, result: {}({})",
                   unit->get_unit_id(), group.first, result, protobuf_mini_dumper_get_error_msg(result));
      param.result_code.get() = result;
      continue;
    }
    for (const auto& subscriber : group.second) {
      if (subscriber && subscriber->get_private_data()) {
        subscriber->get_private_data()->set_last_send_event_id(
            std::max(last_event_id, subscriber->get_private_data()->last_send_event_id()));
      }
    }
  }
}

matching_wal_publisher::vtable_pointer create_vtable() {
  static matching_wal_publisher::vtable_pointer result;
  if (result) {
    return result;
  }
  result = matching_wal_log_operator::make_strong<matching_wal_publisher::vtable_type>();
  if (!result) {
    return result;
  }
  result->load = [](matching_wal_object&, const matching_wal_object::storage_type&,
                    matching_wal_object::callback_param_type) { return matching_wal_result_code::kOk; };
  result->dump = [](const matching_wal_object&, matching_wal_object::storage_type&,
                    matching_wal_object::callback_param_type) { return matching_wal_result_code::kOk; };
  result->get_meta = [](const matching_wal_object&, const matching_wal_object::log_type& log) {
    auto timepoint = std::chrono::system_clock::from_time_t(log.timepoint().seconds()) +
                     std::chrono::nanoseconds{log.timepoint().nanos()};
    return matching_wal_object::meta_result_type::make_success(
        std::chrono::time_point_cast<std::chrono::system_clock::duration>(timepoint), log.event_id(),
        log.event_type());
  };
  result->set_meta = [](const matching_wal_object&, matching_wal_object::log_type& log,
                        const matching_wal_object::meta_type& meta) {
    const time_t seconds = std::chrono::system_clock::to_time_t(meta.timepoint);
    log.mutable_timepoint()->set_seconds(seconds);
    log.mutable_timepoint()->set_nanos(static_cast<int32_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                                meta.timepoint -
                                                                std::chrono::system_clock::from_time_t(seconds))
                                                                .count()));
    log.set_event_id(meta.log_key);
  };
  result->merge_log = [](const matching_wal_object&, matching_wal_object::callback_param_type,
                         matching_wal_object::log_type&, const matching_wal_object::log_type&) {
    FWLOGERROR("matching Unit WAL logs must not merge");
  };
  result->get_log_key = [](const matching_wal_object&, const matching_wal_object::log_type& log) {
    return log.event_id();
  };
  result->allocate_log_key = [](matching_wal_object& wal, const matching_wal_object::log_type& log,
                                matching_wal_object::callback_param_type) {
    if (log.event_id() > 0) {
      return matching_wal_object::log_key_result_type::make_success(log.event_id());
    }
    auto* unit = wal.get_private_data();
    return unit ? matching_wal_object::log_key_result_type::make_success(unit->get_last_event_id() + 1)
                : matching_wal_object::log_key_result_type::make_error(matching_wal_result_code::kInitlization);
  };
  result->default_delegate.action = [](matching_wal_object&, const matching_wal_object::log_type&,
                                       matching_wal_object::callback_param_type) {
    return matching_wal_result_code::kOk;
  };
  result->send_snapshot = [](matching_wal_publisher& publisher, matching_wal_publisher::subscriber_iterator begin,
                             matching_wal_publisher::subscriber_iterator end,
                             matching_wal_publisher::callback_param_type param) {
    auto* unit = publisher.get_private_data();
    if (!unit || begin == end) {
      return matching_wal_result_code::kOk;
    }
    rpc::context::message_holder<PROJECT_NAMESPACE_ID::SSMatchingEventSync> message(param.context.get());
    message->set_unit_id(unit->get_unit_id());
    send_to_subscribers(publisher, collect_subscribers(begin, end), *message, param, unit->get_last_event_id());
    return matching_wal_result_code::kOk;
  };
  result->send_logs = [](matching_wal_publisher& publisher, matching_wal_publisher::log_const_iterator log_begin,
                         matching_wal_publisher::log_const_iterator log_end,
                         matching_wal_publisher::subscriber_iterator subscriber_begin,
                         matching_wal_publisher::subscriber_iterator subscriber_end,
                         matching_wal_publisher::callback_param_type param) {
    if (!publisher.get_private_data() || log_begin == log_end || subscriber_begin == subscriber_end) {
      return matching_wal_result_code::kOk;
    }
    rpc::context::message_holder<PROJECT_NAMESPACE_ID::SSMatchingEventSync> message(param.context.get());
    int64_t last_event_id = 0;
    for (; log_begin != log_end; ++log_begin) {
      if (*log_begin) {
        protobuf_copy_message(*message->add_event_logs(), **log_begin);
        last_event_id = std::max(last_event_id, (*log_begin)->event_id());
      }
    }
    send_to_subscribers(publisher, collect_subscribers(subscriber_begin, subscriber_end), *message, param,
                        last_event_id);
    return matching_wal_result_code::kOk;
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
}  // namespace matching_wal_detail

matching_wal_log_operator::strong_ptr<matching_wal_publisher> create_matching_wal_publisher(matching_unit& unit) {
  return matching_wal_publisher::create(matching_wal_detail::create_vtable(), matching_wal_detail::create_configure(),
                                        &unit);
}
