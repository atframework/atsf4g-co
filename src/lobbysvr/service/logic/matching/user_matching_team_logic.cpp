// Copyright 2026 atframework

#include "logic/matching/user_matching_team_logic.h"

#include <logic/team/user_team_manager.h>

#include "data/user.h"

user_matching_team_logic::user_matching_team_logic(user& owner) noexcept : owner_(&owner) {}

bool user_matching_team_logic::is_in_team() const noexcept {
  return !!owner_->get_user_team_manager().get_team_by_team_type(PROJECT_NAMESPACE_ID::EN_TEAM_TYPE_NORMAL);
}

void user_matching_team_logic::register_start_matching_check_function(start_matching_check_function_t function) {
  start_matching_check_function_ = std::move(function);
}

void user_matching_team_logic::register_matching_finish_function(matching_finish_function_t function) {
  matching_finish_function_ = std::move(function);
}

void user_matching_team_logic::register_start_matching_finish_function(start_matching_finish_function_t function) {
  start_matching_finish_function_ = std::move(function);
}

void user_matching_team_logic::start_matching_check(rpc::context& ctx) {
  if (!start_matching_check_function_) {
    FWLOGERROR("Start matching check function is not registered");
    return;
  }
  start_matching_check_function_(ctx);
}

void user_matching_team_logic::matching_finish(rpc::context& ctx) {
  if (!matching_finish_function_) {
    FWLOGERROR("Matching finish notify function is not registered");
    return;
  }
  matching_finish_function_(ctx);
}

void user_matching_team_logic::start_matching_finish(rpc::context& ctx) {
  if (!start_matching_finish_function_) {
    FWLOGERROR("Start matching finish function is not registered");
    return;
  }
  start_matching_finish_function_(ctx);
}