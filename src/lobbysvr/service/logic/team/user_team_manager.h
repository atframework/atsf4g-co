// Copyright 2026 atframework

#pragma once

#include <gsl/select-gsl.h>

class user;

class user_team_manager {
 public:
  explicit user_team_manager(user& owner);
  ~user_team_manager();

  ATFW_EXPLICIT_NODISCARD_ATTR int32_t login_init(rpc::context&);

  void refresh_feature_limit_second(rpc::context&);

  inline user& get_owner() { return *owner_; }
  inline const user& get_owner() const { return *owner_; }

 private:
  user* ATFW_UTIL_MACRO_NONNULL owner_;
};
