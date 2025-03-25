// Copyright 2021 atframework
// Created by owent on 2016/9/28.
//

#ifndef RPC_AUTH_LOGIN_H
#define RPC_AUTH_LOGIN_H

#pragma once

#include <config/server_frame_build_feature.h>

#include <gsl/select-gsl.h>

#include <stdint.h>
#include <cstddef>
#include <string>

namespace rpc {
namespace auth {
namespace login {
/**
 * @brief generate logincode
 * @param code where to store code
 * @param sz code length
 */
SERVER_FRAME_API void generate_login_code(char *code, size_t sz);

/**
 * @brief add account and channel prefix into openid
 * @param account_type account type (determine how to login)
 * @param channel_id channel id (what's the publish channel)
 * @param openid raw openid
 * @return final openid
 */
SERVER_FRAME_API std::string make_open_id(uint32_t zone_id, uint32_t account_type, uint32_t channel_id,
                                          gsl::string_view openid);
}  // namespace login
}  // namespace auth
}  // namespace rpc

#endif  //_RPC_AUTH_LOGIN_H
