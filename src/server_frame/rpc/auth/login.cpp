// Copyright 2021 atframework
// Created by owent on 2016/9/28.
//

#include <sstream>

#include <string/string_format.h>
#include <utility/random_engine.h>

#include "login.h"

namespace rpc {
namespace auth {
namespace login {
SERVER_FRAME_API void generate_login_code(char *code, size_t sz) {
  if (sz > 0) {
    for (size_t i = 0; i < sz - 1; ++i) {
      code[i] = atfw::util::random_engine::random_between<char>(33, 127);
    }
    code[sz - 1] = 0;
  }
}

SERVER_FRAME_API std::string make_open_id(uint32_t zone_id, uint32_t account_type, uint32_t channel_id,
                                          gsl::string_view openid) {
  return atfw::util::string::format("z-{}:at-{}:c-{}:{}", zone_id, account_type, channel_id, openid);
}
}  // namespace login
}  // namespace auth
}  // namespace rpc
