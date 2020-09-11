//
// Created by owt50 on 2016/9/23.
//

#ifndef CONFIG_LOGIC_CONFIG_H
#define CONFIG_LOGIC_CONFIG_H

#pragma once

#include <config/ini_loader.h>
#include <design_pattern/singleton.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/config/svr.protocol.config.pb.h>

#include <protocol/pbdesc/atframework.pb.h>
#include <protocol/pbdesc/com.const.pb.h>

#include <config/compiler/protobuf_suffix.h>

namespace atapp {
    class app;
}

class logic_config : public util::design_pattern::singleton<logic_config> {
public:
    struct LC_ROUTER {
        time_t cache_update_interval;
        time_t cache_free_timeout;
        time_t object_free_timeout;
        time_t object_save_interval;
        size_t retry_max_ttl;
        time_t default_timer_interval;
        time_t fast_timer_interval;
    };

    struct LC_LOGIC {
        uint32_t zone_id;
        uint32_t zone_step;

        time_t      server_open_time;
        std::string server_resource_dir; // ../../resource for default
        bool        server_maintenance_mode;

        time_t task_nomsg_timeout;
        time_t task_csmsg_timeout;
        time_t task_paymsg_timeout;
        time_t task_stats_interval;
        size_t task_stack_size;
        size_t task_stack_gc_once_number;
        size_t task_stack_mmap_count; // check sys mmap configure(linux: only, >=max(task_stack_busy_count, task_stack_pool_max_count)*2+task_stack_keep_count)
        size_t task_stack_pool_max_count;
        size_t task_stack_busy_count;
        size_t task_stack_keep_count;
        size_t task_stack_busy_warn_count;

        size_t      player_max_online_number;
        std::string player_default_openid;

        // 登入码的有效期
        time_t session_login_code_protect;
        time_t session_login_code_valid_sec;
        time_t session_login_ban_time;
        time_t session_tick_sec;

        // 心跳
        time_t heartbeat_interval;
        time_t heartbeat_tolerance;
        size_t heartbeat_error_times;
        size_t heartbeat_ban_error_times;
        time_t heartbeat_ban_time_bound;

        // 路由系统
        LC_ROUTER router;
    };

    // ================== server configures =====================
    struct LC_LOGINSVR {
        time_t      gmsvr_timeout_sec;
        std::string version_cfg_file;
        std::string strategy_cfg_file;
        std::string cdn_url;

        std::vector<std::string> gamesvr_list;
        time_t                   start_time;
        time_t                   end_time;

        time_t relogin_expired_time;

        std::vector<std::string> white_openid_list;
        uint32_t                 debug_platform_mode;
        uint32_t                 reload_version;
    };

protected:
    logic_config();
    ~logic_config();

public:
    int init(uint64_t server_id, const std::string &server_name);

    int reload(atapp::app &app);

    uint64_t           get_self_bus_id() const;
    const std::string &get_self_name() const;

    inline const LC_LOGIC &                     get_cfg_logic() const { return cfg_logic_; }
    inline const hello::config::db_section_cfg &get_cfg_db() const { return server_cfg_.db(); }

    inline const LC_LOGINSVR &get_cfg_svr_login() const { return cfg_loginsvr_; }

    const hello::DConstSettingsType &     get_const_settings();
    const atframework::ConstSettingsType &get_atframework_settings();

private:
    void _load_logic(util::config::ini_loader &loader);

    void _load_db();
    void _load_db_hosts(hello::config::db_group_cfg &out, const char *group_name);

    void _load_loginsvr(util::config::ini_loader &loader);

    void _load_server_cfg(atapp::app &app);

private:
    uint64_t                              server_id_;
    std::string                           server_name_;
    const hello::DConstSettingsType *     const_settings_;
    const atframework::ConstSettingsType *atframe_settings_;
    LC_LOGIC                              cfg_logic_;

    LC_LOGINSVR cfg_loginsvr_;

    hello::config::server_cfg server_cfg_;
};


#endif // ATF4G_CO_LOGIC_CONFIG_H
