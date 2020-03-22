//
// Created by owt50 on 2016/9/23.
//

#ifndef CONFIG_LOGIC_CONFIG_H
#define CONFIG_LOGIC_CONFIG_H

#pragma once

#include <design_pattern/singleton.h>
#include <config/ini_loader.h>

#include <protocol/pbdesc/com.const.pb.h>

class logic_config : public util::design_pattern::singleton<logic_config> {
public:
    struct LC_DBCONN {
        std::string url;
        std::string host;
        uint16_t    port;
    };

    struct LC_DBCONF {
        std::vector<LC_DBCONN> cluster_default;
        std::vector<LC_DBCONN> raw_default;
        std::string db_script_file[hello::EnDBScriptShaType_ARRAYSIZE];
        time_t time_retry_sec;
        time_t time_retry_usec;
        time_t timeout;
        uint64_t proc;
    };

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
        size_t task_stack_mmap_count;     // check sys mmap configure(linux: only, >=max(task_stack_busy_count, task_stack_pool_max_count)*2+task_stack_keep_count)
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

    struct LC_GAMESVR {};

protected:
    logic_config();
    ~logic_config();

public:
    int init(uint64_t bus_id);

    int reload(util::config::ini_loader &cfg_set);

    uint64_t get_self_bus_id() const;

    inline const LC_LOGIC &get_cfg_logic() const { return cfg_logic_; }
    inline const LC_DBCONF &get_cfg_db() const { return cfg_db_; }

    inline const LC_LOGINSVR &get_cfg_svr_login() const { return cfg_loginsvr_; }
    inline const LC_GAMESVR & get_cfg_svr_game() const { return cfg_gamesvr_; }

private:
    void _load_logic(util::config::ini_loader &loader);

    void _load_db(util::config::ini_loader &loader);
    void _load_db_hosts(std::vector<LC_DBCONN> &out, const char *group_name, util::config::ini_loader &loader);

    void _load_loginsvr(util::config::ini_loader &loader);
    void _load_gamesvr(util::config::ini_loader &loader);

private:
    uint64_t bus_id_;
    LC_LOGIC cfg_logic_;
    LC_DBCONF cfg_db_;

    LC_LOGINSVR cfg_loginsvr_;
    LC_GAMESVR  cfg_gamesvr_;
};


#endif // ATF4G_CO_LOGIC_CONFIG_H
