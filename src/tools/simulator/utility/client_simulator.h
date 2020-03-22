//
// Created by owt50 on 2016/10/11.
//

#ifndef ATFRAMEWORK_LIBSIMULATOR_UTILITY_CLIENT_SIMULATOR_H
#define ATFRAMEWORK_LIBSIMULATOR_UTILITY_CLIENT_SIMULATOR_H

#pragma once

#include <simulator_active.h>
#include <simulator_base.h>

#include "client_player.h"

class client_simulator : public simulator_msg_base<client_player, hello::CSMsg> {
public:
    typedef client_simulator                                self_type;
    typedef simulator_msg_base<client_player, hello::CSMsg> base_type;
    typedef typename base_type::player_t                    player_t;
    typedef typename base_type::player_ptr_t                player_ptr_t;
    typedef typename base_type::msg_t                       msg_t;
    typedef typename base_type::cmd_sender_t                cmd_sender_t;

public:
    virtual ~client_simulator();

    virtual uint32_t    pick_message_id(const msg_t &msg) const UTIL_CONFIG_OVERRIDE;
    virtual std::string pick_message_name(const msg_t &msg) const UTIL_CONFIG_OVERRIDE;
    virtual std::string dump_message(const msg_t &msg) UTIL_CONFIG_OVERRIDE;

    virtual int pack_message(const msg_t &msg, void *buffer, size_t &sz) const UTIL_CONFIG_OVERRIDE;
    virtual int unpack_message(msg_t &msg, const void *buffer, size_t sz) const UTIL_CONFIG_OVERRIDE;

    virtual int tick() UTIL_CONFIG_OVERRIDE;

    static client_simulator *cast(simulator_base *b);
    static cmd_sender_t &    get_cmd_sender(util::cli::callback_param params);
    static msg_t &           add_req(cmd_sender_t &sender);
    static msg_t &           add_req(util::cli::callback_param params);
};

#define SIMULATOR_CHECK_PLAYER_PARAMNUM(PARAM, N)                                                                                                   \
    if (!client_simulator::get_cmd_sender(PARAM).player) {                                                                                          \
        util::cli::shell_stream(std::cerr)() << util::cli::shell_font_style::SHELL_FONT_COLOR_RED << "this command require a player." << std::endl; \
        SIMULATOR_PRINT_PARAM_HELPER(PARAM, std::cerr);                                                                                             \
        return;                                                                                                                                     \
    }                                                                                                                                               \
    SIMULATOR_CHECK_PARAMNUM(PARAM, N)

#endif // ATFRAMEWORK_LIBSIMULATOR_UTILITY_CLIENT_SIMULATOR_H
