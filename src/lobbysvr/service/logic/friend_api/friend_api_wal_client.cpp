// Copyright 2026 atframework
// Created by owent on 2026-09-28

#include "logic/friend_api/friend_api_wal_client.h"

#include <time/time_utility.h>

#include <config/logic_config.h>

#include <rpc/friend_api/friend_algorithm.h>
#include <rpc/rpc_context.h>

#include "data/user.h"
#include "logic/friend_api/user_friend_api_manager.h"

friend_api_wal_client_context::friend_api_wal_client_context(rpc::context& ctx, int32_t& output_result)
    : context(ctx), result_code(output_result) {}

namespace {

struct user_friend_wal_delegate_helper {
  using wal_object_type = user_friend_api_wal_client_type::object_type;
  using wal_publisher_type = user_friend_api_wal_client_type;
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
    user_friend_api_manager* friend_manager = wal.get_private_data();
    if (nullptr == friend_manager) {
      return wal_result_code::kInitlization;
    }

    friend_manager->add_inviter_cache(param.context, log.add_inviter());
    return wal_result_code::kOk;
  }

  static wal_result_code remove_inviter(wal_object_type& wal, wal_object_type::log_type& log,
                                        wal_object_type::callback_param_type param) {
    user_friend_api_manager* friend_manager = wal.get_private_data();
    if (nullptr == friend_manager) {
      return wal_result_code::kInitlization;
    }

    friend_manager->remove_inviter_cache(param.context, log.remove_inviter().from_user());
    return wal_result_code::kOk;
  }

  static wal_result_code add_invitee(wal_object_type& wal, wal_object_type::log_type& log,
                                     wal_object_type::callback_param_type param) {
    user_friend_api_manager* friend_manager = wal.get_private_data();
    if (nullptr == friend_manager) {
      return wal_result_code::kInitlization;
    }

    friend_manager->add_invitee_cache(param.context, log.add_invitee());
    return wal_result_code::kOk;
  }

  static wal_result_code remove_invitee(wal_object_type& wal, wal_object_type::log_type& log,
                                        wal_object_type::callback_param_type param) {
    user_friend_api_manager* friend_manager = wal.get_private_data();
    if (nullptr == friend_manager) {
      return wal_result_code::kInitlization;
    }

    friend_manager->remove_invitee_cache(param.context, log.remove_invitee().to_user());
    return wal_result_code::kOk;
  }

  static wal_result_code add_gift(wal_object_type& wal, wal_object_type::log_type& log,
                                  wal_object_type::callback_param_type param) {
    user_friend_api_manager* friend_manager = wal.get_private_data();
    if (nullptr == friend_manager) {
      return wal_result_code::kInitlization;
    }

    friend_manager->add_gift_cache(param.context, log.add_gift());
    return wal_result_code::kOk;
  }

  static wal_result_code remove_gift(wal_object_type& wal, wal_object_type::log_type& log,
                                     wal_object_type::callback_param_type param) {
    user_friend_api_manager* friend_manager = wal.get_private_data();
    if (nullptr == friend_manager) {
      return wal_result_code::kInitlization;
    }

    friend_manager->remove_gift_cache(param.context, log.remove_gift().gift_id());
    return wal_result_code::kOk;
  }

  static wal_result_code add_friend_data(wal_object_type& wal, wal_object_type::log_type& log,
                                         wal_object_type::callback_param_type param) {
    user_friend_api_manager* friend_manager = wal.get_private_data();
    if (nullptr == friend_manager) {
      return wal_result_code::kInitlization;
    }

    friend_manager->add_friend_cache(param.context, log.add_friend_data());
    return wal_result_code::kOk;
  }

  static wal_result_code remove_friend_data(wal_object_type& wal, wal_object_type::log_type& log,
                                            wal_object_type::callback_param_type param) {
    user_friend_api_manager* friend_manager = wal.get_private_data();
    if (nullptr == friend_manager) {
      return wal_result_code::kInitlization;
    }

    friend_manager->remove_friend_cache(param.context, log.remove_friend_data().user_key());
    return wal_result_code::kOk;
  }

  static wal_result_code remove_all_inviter(wal_object_type& wal, const wal_object_type::log_type& log,
                                            wal_object_type::callback_param_type param) {
    auto* manager = wal.get_private_data();
    if (manager == nullptr) {
      return wal_result_code::kInitlization;
    }
    manager->remove_all_inviter_cache(param.context, log.event_id());
    return wal_result_code::kOk;
  }

  static wal_result_code clear_all_data(wal_object_type& wal, const wal_object_type::log_type& log,
                                        wal_object_type::callback_param_type param) {
    user_friend_api_manager* friend_manager = wal.get_private_data();
    if (nullptr == friend_manager) {
      return wal_result_code::kInitlization;
    }

    friend_manager->cleanup_friend_data(param.context, log.event_id());
    if (wal.get_global_ingore_key() == nullptr || *wal.get_global_ingore_key() < log.event_id()) {
      wal.set_global_ingore_key(log.event_id());
    }
    return wal_result_code::kOk;
  }

  static void setup_delegate_actions(user_friend_api_wal_client_type::object_type::callback_log_group_map_t& actions) {
    actions[static_cast<int32_t>(atfw::friend_api::DFriendEvent::kRemoveAllInviter)].action =
        user_friend_wal_delegate_helper::remove_all_inviter;
    actions[static_cast<int32_t>(atfw::friend_api::DFriendEvent::kAddInviter)].patch =
        user_friend_wal_delegate_helper::add_inviter;
    actions[static_cast<int32_t>(atfw::friend_api::DFriendEvent::kRemoveInviter)].patch =
        user_friend_wal_delegate_helper::remove_inviter;
    actions[static_cast<int32_t>(atfw::friend_api::DFriendEvent::kAddInvitee)].patch =
        user_friend_wal_delegate_helper::add_invitee;
    actions[static_cast<int32_t>(atfw::friend_api::DFriendEvent::kRemoveInvitee)].patch =
        user_friend_wal_delegate_helper::remove_invitee;
    actions[static_cast<int32_t>(atfw::friend_api::DFriendEvent::kAddGift)].patch =
        user_friend_wal_delegate_helper::add_gift;
    actions[static_cast<int32_t>(atfw::friend_api::DFriendEvent::kRemoveGift)].patch =
        user_friend_wal_delegate_helper::remove_gift;
    actions[static_cast<int32_t>(atfw::friend_api::DFriendEvent::kAddFriendData)].patch =
        user_friend_wal_delegate_helper::add_friend_data;
    actions[static_cast<int32_t>(atfw::friend_api::DFriendEvent::kRemoveFriendData)].patch =
        user_friend_wal_delegate_helper::remove_friend_data;
    actions[static_cast<int32_t>(atfw::friend_api::DFriendEvent::kDailySendList)].action =
        user_friend_wal_delegate_helper::do_nothing;
    actions[static_cast<int32_t>(atfw::friend_api::DFriendEvent::kDailyReceiveList)].action =
        user_friend_wal_delegate_helper::do_nothing;
    actions[static_cast<int32_t>(atfw::friend_api::DFriendEvent::kClearAllData)].action =
        user_friend_wal_delegate_helper::clear_all_data;
  }
};

static user_friend_api_wal_client_type::vtable_pointer create_user_friend_api_client_vtable() {
  using wal_object_type = user_friend_api_wal_client_type::object_type;
  using wal_client_type = user_friend_api_wal_client_type;
  using atfw::util::distributed_system::wal_result_code;
  using snapshot_type = user_friend_api_wal_client_type::snapshot_type;

  static wal_client_type::vtable_pointer ret;
  if (ret) {
    return ret;
  }

  ret = atfw::memory::stl::make_strong_rc<wal_client_type::vtable_type>();
  if (!ret) {
    return ret;
  }

  rpc::friend_api::setup_common_vtable<wal_object_type>(*ret);

  // callbacks for wal_object
  ret->load = [](wal_object_type& wal, const wal_object_type::storage_type& from,
                 wal_object_type::callback_param_type param) -> wal_result_code {
    if (nullptr == wal.get_private_data()) {
      return wal_result_code::kInitlization;
    }

    user_friend_api_manager* friend_manager = wal.get_private_data();

    // 加载远端快照
    friend_manager->load_storage(param.context, from);
    return wal_result_code::kOk;
  };

  ret->dump = [](const wal_object_type& wal, wal_object_type::storage_type& to,
                 wal_object_type::callback_param_type param) -> wal_result_code {
    if (nullptr == wal.get_private_data()) {
      return wal_result_code::kInitlization;
    }

    user_friend_api_manager* friend_manager = wal.get_private_data();
    friend_manager->dump_storage(param.context, to);
    return wal_result_code::kOk;
  };

  ret->merge_log = [](const wal_object_type&, wal_object_type::callback_param_type, wal_object_type::log_type& to,
                      const wal_object_type::log_type& from) {
    FWLOGERROR("Merge friend WAL failed(should not happen) from\n{}to\n{}", from.DebugString(), to.DebugString());
  };

  ret->allocate_log_key = [](wal_object_type& /*wal*/, const wal_object_type::log_type& log,
                             wal_object_type::callback_param_type) -> wal_object_type::log_key_result_type {
    if (log.event_id() > 0) {
      return wal_object_type::log_key_result_type::make_success(log.event_id());
    }

    // client不允许分配log的event id，由friendsvr分配
    return wal_object_type::log_key_result_type::make_error(wal_result_code::kCallbackError);
  };

  user_friend_wal_delegate_helper::setup_delegate_actions(ret->log_action_delegate);

  // ============ callbacks for wal_client ============
  ret->on_receive_snapshot = [](wal_client_type& wal, const snapshot_type& snapshot_data,
                                wal_client_type::callback_param_type param) -> wal_result_code {
    if (nullptr == wal.get_private_data()) {
      return wal_result_code::kInitlization;
    }

    user_friend_api_manager* friend_manager = wal.get_private_data();
    friend_manager->load_snapshot(param.context, snapshot_data);
    return wal_result_code::kOk;
  };

  ret->on_receive_subscribe_response = [](wal_client_type&, wal_client_type::callback_param_type) -> wal_result_code {
    return wal_result_code::kOk;
  };

  ret->subscribe_request = [](wal_client_type& wal, wal_client_type::callback_param_type param) -> wal_result_code {
    if (nullptr == wal.get_private_data()) {
      return wal_result_code::kInitlization;
    }

    user_friend_api_manager* friend_manager = wal.get_private_data();
    friend_manager->set_need_send_wal_heartbeat(param.context);
    return wal_result_code::kOk;
  };

  return ret;
}

static user_friend_api_wal_client_type::configure_pointer create_user_friend_api_client_congigure() {
  user_friend_api_wal_client_type::configure_pointer ret = user_friend_api_wal_client_type::make_configure();
  if (!ret) {
    return ret;
  }

  ret->require_snapshot = true;
  ret->subscriber_heartbeat_interval =
      protobuf_to_system_clock(logic_config::me()->get_logic_cfg().friend_api().wal_subscriber_heartbeat());
  ret->subscriber_heartbeat_retry_interval =
      protobuf_to_system_clock(logic_config::me()->get_logic_cfg().friend_api().wal_subscriber_retry_interval());
  ret->gc_expire_duration =
      protobuf_to_system_clock(logic_config::me()->get_logic_cfg().friend_api().wal_gc_expire_duration());
  ret->gc_log_size = logic_config::me()->get_logic_cfg().friend_api().wal_gc_log_size();
  ret->max_log_size = logic_config::me()->get_logic_cfg().friend_api().wal_max_log_size();

  return ret;
}
}  // namespace

atfw::util::memory::strong_rc_ptr<user_friend_api_wal_client_type> user_friend_api_create_wal_client(
    user_friend_api_manager& friend_mgr) {
  return user_friend_api_wal_client_type::create(atfw::util::time::time_utility::now(),
                                                 create_user_friend_api_client_vtable(),
                                                 create_user_friend_api_client_congigure(), &friend_mgr);
}
