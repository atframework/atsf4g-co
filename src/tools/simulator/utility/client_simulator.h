// Copyright 2021 atframework
// Created by owent on 2016/10/11.
//

#ifndef ATFRAMEWORK_LIBSIMULATOR_UTILITY_CLIENT_SIMULATOR_H
#define ATFRAMEWORK_LIBSIMULATOR_UTILITY_CLIENT_SIMULATOR_H

#pragma once

#include <simulator_active.h>
#include <simulator_base.h>

#include <config/server_frame_build_feature.h>

#include <string>

#include "utility/client_player.h"

class client_simulator : public simulator_msg_base<client_player, PROJECT_NAMESPACE_ID::CSMsg> {
 public:
  using self_type = client_simulator;
  using base_type = simulator_msg_base<client_player, PROJECT_NAMESPACE_ID::CSMsg>;
  using player_t = typename base_type::player_t;
  using player_ptr_t = typename base_type::player_ptr_t;
  using msg_t = typename base_type::msg_t;
  using cmd_sender_t = typename base_type::cmd_sender_t;

 public:
  client_simulator();
  virtual ~client_simulator();

  uint32_t pick_message_id(const msg_t &msg) const override;
  std::string pick_message_name(const msg_t &msg) const override;
  std::string dump_message(const msg_t &msg) override;

  int pack_message(const msg_t &msg, void *buffer, size_t &sz) const override;
  int unpack_message(msg_t &msg, const void *buffer, size_t sz) const override;

  int tick() override;

  static const PROJECT_NAMESPACE_ID::DConstSettingsType &get_const_settings();
  static const atframework::ConstSettingsType &get_atframework_settings();

  static client_simulator *cast(simulator_base *b);
  static cmd_sender_t &get_cmd_sender(util::cli::callback_param params);
  static msg_t &add_req(cmd_sender_t &sender);
  static msg_t &add_req(util::cli::callback_param params);
};

#define SIMULATOR_CHECK_PLAYER_PARAMNUM(PARAM, N)                                             \
  if (!client_simulator::get_cmd_sender(PARAM).player) {                                      \
    util::cli::shell_stream(std::cerr)() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED \
                                         << "this command require a player." << std::endl;    \
    SIMULATOR_PRINT_PARAM_HELPER(PARAM, std::cerr);                                           \
    return;                                                                                   \
  }                                                                                           \
  SIMULATOR_CHECK_PARAMNUM(PARAM, N)

#endif  // ATFRAMEWORK_LIBSIMULATOR_UTILITY_CLIENT_SIMULATOR_H
