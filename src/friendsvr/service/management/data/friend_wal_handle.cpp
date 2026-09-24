// Copyright 2026 atframework
// Created by owent on 2026-09-22.
//

#include "data/friend_wal_handle.h"

#include <memory/object_allocator.h>

#include <utility/protobuf_mini_dumper.h>

#include <config/logic_config.h>

#include <rpc/rpc_context.h>

#include "data/friend_object.h"

namespace atframework {
namespace friend_api {

namespace {

struct friend_wal_delegate_helper {
  using wal_object_type = friend_wal_publisher_type::object_type;
  using wal_publisher_type = friend_wal_publisher_type;
  using wal_result_code = atfw::util::distributed_system::wal_result_code;
  using log_const_iterator = wal_object_type::log_const_iterator;
  using log_iterator = wal_object_type::log_iterator;
  using log_key_type = wal_object_type::log_key_type;
  using log_type = wal_object_type::log_type;

  static wal_result_code do_nothing(wal_object_type&, const wal_object_type::log_type&,
                                    wal_object_type::callback_param_type) {
    return wal_result_code::kOk;
  }

  static wal_result_code add_inviter(wal_object_type& wal, wal_object_type::log_type& log,
                                     wal_object_type::callback_param_type param) {
    friend_object* friend_obj = wal.get_private_data();
    if (nullptr == friend_obj) {
      return wal_result_code::kInitlization;
    }

    if (!friend_obj->add_inviter(param.context, log.event_id(), *log.mutable_add_inviter())) {
      return wal_result_code::kIgnore;
    }
    return wal_result_code::kOk;
  }

  static wal_result_code remove_inviter(wal_object_type& wal, wal_object_type::log_type& log,
                                        wal_object_type::callback_param_type param) {
    friend_object* friend_obj = wal.get_private_data();
    if (nullptr == friend_obj) {
      return wal_result_code::kInitlization;
    }

    if (!friend_obj->remove_inviter(param.context, log.event_id(), *log.mutable_remove_inviter())) {
      return wal_result_code::kIgnore;
    }
    return wal_result_code::kOk;
  }

  static wal_result_code remove_all_inviter(wal_object_type& wal, wal_object_type::log_type& log,
                                            wal_object_type::callback_param_type param) {
    friend_object* friend_obj = wal.get_private_data();
    if (nullptr == friend_obj) {
      return wal_result_code::kInitlization;
    }

    if (!friend_obj->remove_all_inviters(param.context, log.event_id())) {
      return wal_result_code::kIgnore;
    }
    return wal_result_code::kOk;
  }

  static wal_result_code add_invitee(wal_object_type& wal, wal_object_type::log_type& log,
                                     wal_object_type::callback_param_type param) {
    friend_object* friend_obj = wal.get_private_data();
    if (nullptr == friend_obj) {
      return wal_result_code::kInitlization;
    }

    if (!friend_obj->add_invitee(param.context, log.event_id(), *log.mutable_add_invitee())) {
      return wal_result_code::kIgnore;
    }
    return wal_result_code::kOk;
  }

  static wal_result_code remove_invitee(wal_object_type& wal, wal_object_type::log_type& log,
                                        wal_object_type::callback_param_type param) {
    friend_object* friend_obj = wal.get_private_data();
    if (nullptr == friend_obj) {
      return wal_result_code::kInitlization;
    }

    if (!friend_obj->remove_invitee(param.context, log.event_id(), *log.mutable_remove_invitee())) {
      return wal_result_code::kIgnore;
    }
    return wal_result_code::kOk;
  }

  static wal_result_code add_gift(wal_object_type& wal, wal_object_type::log_type& log,
                                  wal_object_type::callback_param_type param) {
    friend_object* friend_obj = wal.get_private_data();
    if (nullptr == friend_obj) {
      return wal_result_code::kInitlization;
    }

    if (!friend_obj->add_gift(param.context, log.event_id(), *log.mutable_add_gift())) {
      return wal_result_code::kIgnore;
    }
    return wal_result_code::kOk;
  }

  static wal_result_code remove_gift(wal_object_type& wal, wal_object_type::log_type& log,
                                     wal_object_type::callback_param_type param) {
    friend_object* friend_obj = wal.get_private_data();
    if (nullptr == friend_obj) {
      return wal_result_code::kInitlization;
    }

    if (!friend_obj->remove_gift(param.context, log.event_id(), *log.mutable_remove_gift())) {
      return wal_result_code::kIgnore;
    }
    return wal_result_code::kOk;
  }

  static wal_result_code add_friend_data(wal_object_type& wal, wal_object_type::log_type& log,
                                         wal_object_type::callback_param_type param) {
    friend_object* friend_obj = wal.get_private_data();
    if (nullptr == friend_obj) {
      return wal_result_code::kInitlization;
    }

    if (!friend_obj->add_friend(param.context, log.event_id(), *log.mutable_add_friend_data())) {
      return wal_result_code::kIgnore;
    }
    return wal_result_code::kOk;
  }

  static wal_result_code remove_friend_data(wal_object_type& wal, wal_object_type::log_type& log,
                                            wal_object_type::callback_param_type param) {
    friend_object* friend_obj = wal.get_private_data();
    if (nullptr == friend_obj) {
      return wal_result_code::kInitlization;
    }

    if (!friend_obj->remove_friend(param.context, log.event_id(), *log.mutable_remove_friend_data())) {
      return wal_result_code::kIgnore;
    }
    return wal_result_code::kOk;
  }

  static wal_result_code clear_all_data(wal_object_type& wal, const wal_object_type::log_type& log,
                                        wal_object_type::callback_param_type param) {
    friend_object* friend_obj = wal.get_private_data();
    if (nullptr == friend_obj) {
      return wal_result_code::kInitlization;
    }

    if (!friend_obj->clear_all_data(param.context, log.event_id())) {
      return wal_result_code::kIgnore;
    }
    return wal_result_code::kOk;
  }

  static void setup_delegate_actions(friend_wal_publisher_type::object_type::callback_log_group_map_t& actions) {
    actions[DFriendEvent::kAddInviter].patch = friend_wal_delegate_helper::add_inviter;
    actions[DFriendEvent::kRemoveInviter].patch = friend_wal_delegate_helper::remove_inviter;
    actions[DFriendEvent::kRemoveAllInviter].patch = friend_wal_delegate_helper::remove_all_inviter;
    actions[DFriendEvent::kAddInvitee].patch = friend_wal_delegate_helper::add_invitee;
    actions[DFriendEvent::kRemoveInvitee].patch = friend_wal_delegate_helper::remove_invitee;
    actions[DFriendEvent::kAddGift].patch = friend_wal_delegate_helper::add_gift;
    actions[DFriendEvent::kRemoveGift].patch = friend_wal_delegate_helper::remove_gift;
    actions[DFriendEvent::kAddFriendData].patch = friend_wal_delegate_helper::add_friend_data;
    actions[DFriendEvent::kRemoveFriendData].patch = friend_wal_delegate_helper::remove_friend_data;
    actions[DFriendEvent::kDailySendList].action = friend_wal_delegate_helper::do_nothing;
    actions[DFriendEvent::kDailyReceiveList].action = friend_wal_delegate_helper::do_nothing;
    actions[DFriendEvent::kClearAllData].action = friend_wal_delegate_helper::clear_all_data;
  }
};

static friend_wal_publisher_type::vtable_pointer create_friend_publisher_vtable() {
  using wal_object_type = friend_wal_publisher_type::object_type;
  using wal_publisher_type = friend_wal_publisher_type;
  using atfw::util::distributed_system::wal_result_code;

  static wal_publisher_type::vtable_pointer ret;
  if (ret) {
    return ret;
  }

  ret = atfw::memory::stl::make_strong_rc<wal_publisher_type::vtable_type>();
  if (!ret) {
    return ret;
  }

  // callbacks for wal_object
  ret->load = [](wal_object_type& wal, const wal_object_type::storage_type& from,
                 wal_object_type::callback_param_type) -> wal_result_code {
    const table_friend_blob_data* blob_data = from.get_const();
    if (nullptr == blob_data) {
      return wal_result_code::kInvalidParam;
    }

    if (nullptr == wal.get_private_data()) {
      return wal_result_code::kInitlization;
    }

    // 好友系统的WAL log为纯内存数据，不需要从数据库读取
    // Load global ignore
    if (blob_data->global_finished_event_id() > 0) {
      wal.set_global_ingore_key(blob_data->global_finished_event_id());
    }

    if (blob_data->wal_removed_event_id() > 0) {
      wal.set_last_removed_key(blob_data->wal_removed_event_id());
    }
    return wal_result_code::kOk;
  };

  ret->dump = [](const wal_object_type& wal, wal_object_type::storage_type& to,
                 wal_object_type::callback_param_type) -> wal_result_code {
    table_friend_blob_data* blob_data = to.get_mutable();
    if (nullptr == blob_data) {
      return wal_result_code::kInvalidParam;
    }

    if (nullptr == wal.get_private_data()) {
      return wal_result_code::kInitlization;
    }

    // 好友系统的WAL log为纯内存数据，不需要保存到数据库
    // Dump global ignore
    if (wal.get_global_ingore_key() != nullptr) {
      blob_data->set_global_finished_event_id(*wal.get_global_ingore_key());
    }

    if (wal.get_last_removed_key() != nullptr) {
      blob_data->set_wal_removed_event_id(*wal.get_last_removed_key());
    }
    return wal_result_code::kOk;
  };

  ret->get_meta = [](const wal_object_type&,
                     const wal_object_type::log_type& log) -> wal_object_type::meta_result_type {
    return wal_object_type::meta_result_type::make_success(protobuf_to_system_clock(log.create_timepoint()),
                                                           log.event_id(), log.event_case());
  };

  ret->set_meta = [](const wal_object_type&, wal_object_type::log_type& log, const wal_object_type::meta_type& meta) {
    // log.event_case = meta.action_case; // event_case will be created by mutable_*
    protobuf_from_system_clock(*log.mutable_create_timepoint(), meta.timepoint);
    log.set_event_id(meta.log_key);
  };

  ret->merge_log = [](const wal_object_type&, wal_object_type::callback_param_type, wal_object_type::log_type& to,
                      const wal_object_type::log_type& from) {
    FWLOGDEBUG("Ignore repeated friend WAL event {}, existing event {}", from.event_id(), to.event_id());
  };

  ret->get_log_key = [](const wal_object_type&, const wal_object_type::log_type& log) -> wal_object_type::log_key_type {
    return log.event_id();
  };

  ret->allocate_log_key = [](wal_object_type& wal, const wal_object_type::log_type& log,
                             wal_object_type::callback_param_type) -> wal_object_type::log_key_result_type {
    if (log.event_id() > 0) {
      return wal_object_type::log_key_result_type::make_success(log.event_id());
    }
    if (nullptr != wal.get_private_data()) {
      return wal_object_type::log_key_result_type::make_success(wal.get_private_data()->allocate_event_id());
    }

    return wal_object_type::log_key_result_type::make_error(wal_result_code::kInitlization);
  };

  friend_wal_delegate_helper::setup_delegate_actions(ret->log_action_delegate);

  // ============ callbacks for wal_publisher ============
  // NOLINTBEGIN(performance-unnecessary-value-param)
  ret->send_snapshot = [](wal_publisher_type& wal_publisher, wal_publisher_type::subscriber_iterator begin_iter,
                          wal_publisher_type::subscriber_iterator end_iter,
                          wal_publisher_type::callback_param_type param) -> wal_result_code {
    auto* friend_obj = wal_publisher.get_private_data();
    if (friend_obj == nullptr) {
      size_t subscriber_count = 0;
      while (begin_iter != end_iter) {
        ++subscriber_count;
        ++begin_iter;
      }
      FCTXLOGERROR(param.context.get(), "Friend object is null in send_snapshot callback, subscriber count: {}",
                   subscriber_count);
      return wal_result_code::kOk;
    }

    while (begin_iter != end_iter) {
      friend_obj->append_notification_snapshot(param.context, begin_iter->first);
      ++begin_iter;
    }

    return wal_result_code::kOk;
  };

  ret->send_logs = [](wal_publisher_type& publisher, wal_publisher_type::log_const_iterator begin_log,
                      wal_publisher_type::log_const_iterator end_log,
                      wal_publisher_type::subscriber_iterator begin_subscriber,
                      wal_publisher_type::subscriber_iterator end_subscriber,
                      wal_publisher_type::callback_param_type parameter) -> wal_result_code {
    auto* object = publisher.get_private_data();
    if (object == nullptr) {
      return wal_result_code::kInitlization;
    }
    for (; begin_subscriber != end_subscriber; ++begin_subscriber) {
      for (auto log = begin_log; log != end_log; ++log) {
        object->append_notification_event(parameter.context, begin_subscriber->first, **log);
      }
    }
    return wal_result_code::kOk;
  };
  // NOLINTEND(performance-unnecessary-value-param)

  ret->check_subscriber = [](wal_publisher_type&, const wal_publisher_type::subscriber_pointer& subscriber,
                             wal_publisher_type::callback_param_type) -> bool {
    if (!subscriber) {
      return false;
    }

    return true;
  };

  return ret;
}

static friend_wal_publisher_type::configure_pointer create_friend_publisher_congigure() {
  friend_wal_publisher_type::configure_pointer ret = friend_wal_publisher_type::make_configure();
  if (!ret) {
    return ret;
  }
  // ret->enable_last_broadcast_for_removed_subscriber = true;
  // Independent prepared transactions may commit in a different order from their assigned event IDs.
  ret->enable_hole_log = true;
  const auto& cfg = logic_config::me()->get_logic_cfg().friend_api();
  ret->gc_expire_duration = protobuf_to_system_clock(cfg.wal_gc_expire_duration());
  ret->gc_log_size = cfg.wal_gc_log_size() > 0 ? cfg.wal_gc_log_size() : 8;
  ret->max_log_size = cfg.wal_max_log_size() > 0 ? cfg.wal_max_log_size() : 64;

  ret->subscriber_timeout = protobuf_to_system_clock(cfg.wal_subscriber_timeout());
  return ret;
}
}  // namespace

friend_wal_publisher_context::friend_wal_publisher_context(rpc::context& ctx, int32_t& output_result)
    : context(std::ref(ctx)), result_code(std::ref(output_result)) {}

DFriendEvent::EventCase friend_wal_publisher_log_action_getter::operator()(
    const DFriendEvent& event_data) const noexcept {
  return event_data.event_case();
}

atfw::util::memory::strong_rc_ptr<friend_wal_publisher_type> create_friend_publisher(rpc::context&) {
  return friend_wal_publisher_type::create(create_friend_publisher_vtable(), create_friend_publisher_congigure(),
                                           nullptr);
}

}  // namespace friend_api
}  // namespace atframework
