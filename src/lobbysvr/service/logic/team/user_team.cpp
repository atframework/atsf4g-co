// Copyright 2026 atframework

#include "logic/team/user_team.h"

#include "logic/chat/user_chat_manager.h"

struct user_team::ctor_guard_t {};

// NOLINTNEXTLINE(modernize-pass-by-value)
user_team::user_team(ctor_guard_t&, user& owner, const atfw::team::DTeamKey& team_key,
                     const atfw::dtmq::DChannelIdKey& channel_key)
    : team_key_(team_key) {
  rpc::dtmq::client_subscriber::subscriber_options options{owner.get_user_chat_manager().get_subscriber_key()};
  options.auto_create_channel = true;
  // options.event_callback_set =
  channel_subscriber_ = rpc::dtmq::client_subscriber::create(channel_key, options);
}

user_team::~user_team() {}
