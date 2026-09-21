// Copyright 2022 atframework
// Created by owent on 2022-03-01.
//

#include "data/friend_wal_handle.h"

namespace atframework {
namespace friend_api {

DFriendEvent::EventCase friend_wal_publisher_log_action_getter::operator()(
    const DFriendEvent& event_data) const noexcept {
  return event_data.event_case();
}

atfw::util::memory::strong_rc_ptr<friend_wal_publisher_type> create_friend_publisher(rpc::context&, friend_object&) {
  // TODO(owent): ...
  return nullptr;
}

}  // namespace friend_api
}  // namespace atframework
