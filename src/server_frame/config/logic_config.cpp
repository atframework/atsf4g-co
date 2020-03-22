//
// Created by owt50 on 2016/9/23.
//

#include <common/string_oprs.h>
#include <sstream>
#include <std/foreach.h>
#include <time/time_utility.h>


#include "logic_config.h"

template <typename TINT, typename TVAL>
static void load_int_compare(util::config::ini_loader &loader, const char *key, TINT &v, TVAL default_val, TVAL cmp_val = 0) {
    v = static_cast<TINT>(default_val);
    loader.dump_to(key, v, false);
    if (v <= static_cast<TINT>(cmp_val)) {
        v = static_cast<TINT>(default_val);
    }
}

template <typename TINT, typename TVAL, size_t ARRSZ>
static void load_int_compare(util::config::ini_loader &loader, const char key[ARRSZ], TINT &v, TVAL default_val, TVAL cmp_val = 0) {
    load_int_compare(loader, (const char *)key, v, default_val, cmp_val);
}

logic_config::logic_config() : bus_id_(0) {}
logic_config::~logic_config() {}


int logic_config::init(uint64_t bus_id) {
    bus_id_ = bus_id;
    return 0;
}

int logic_config::reload(util::config::ini_loader &cfg_set) {
    const util::config::ini_value::node_type &children = cfg_set.get_root_node().get_children();
    if (children.find("logic") != children.end()) {
        _load_logic(cfg_set);
    }

    if (children.find("db") != children.end()) {
        _load_db(cfg_set);
    }

    if (children.find("gamesvr") != children.end()) {
        _load_gamesvr(cfg_set);
    }

    if (children.find("loginsvr") != children.end()) {
        _load_loginsvr(cfg_set);
    }

    return 0;
}

uint64_t logic_config::get_self_bus_id() const { return bus_id_; }

void logic_config::_load_logic(util::config::ini_loader &loader) {
    cfg_logic_.zone_id = 0;
    cfg_logic_.zone_step = 256;
    loader.dump_to("logic.zone.id", cfg_logic_.zone_id);
    loader.dump_to("logic.zone.step", cfg_logic_.zone_step);

    cfg_logic_.server_maintenance_mode = false;
    cfg_logic_.server_open_time = util::time::time_utility::get_now();
    cfg_logic_.server_resource_dir = "../../resource";

    loader.dump_to("logic.server.open_service_time", cfg_logic_.server_open_time);
    loader.dump_to("logic.server.maintenance_mode", cfg_logic_.server_maintenance_mode);
    loader.dump_to("logic.server.resource.dir", cfg_logic_.server_resource_dir);

    // player
    cfg_logic_.player_max_online_number = 10000;
    cfg_logic_.player_default_openid    = "gm://system";

    loader.dump_to("logic.player.max_online", cfg_logic_.player_max_online_number);
    loader.dump_to("logic.player.default_openid", cfg_logic_.player_default_openid);

    cfg_logic_.session_login_code_protect   = 1200;  // 20m for expired of bad token protect
    cfg_logic_.session_login_code_valid_sec = 600;   // 10m for expired of token
    cfg_logic_.session_login_ban_time       = 10800; // 3 hours when ban by anti cheating
    cfg_logic_.session_tick_sec             = 60;    // session event tick interval(for example: online number)
    loader.dump_to("logic.session.login_code_protect", cfg_logic_.session_login_code_protect);
    loader.dump_to("logic.session.login_code_valid_sec", cfg_logic_.session_login_code_valid_sec);
    loader.dump_to("logic.session.login_ban_time", cfg_logic_.session_login_ban_time);
    loader.dump_to("logic.session.tick_sec", cfg_logic_.session_tick_sec);

    cfg_logic_.task_stack_size           = 1024 * 1024; // default for 1MB
    cfg_logic_.task_csmsg_timeout        = 5;           // 5s
    cfg_logic_.task_nomsg_timeout        = 1800;        // 1800s for auto task
    cfg_logic_.task_paymsg_timeout       = 300;         // 300s for pay task
    cfg_logic_.task_stats_interval       = 60;          // 60s for stats interval
    cfg_logic_.task_stack_gc_once_number = 10;
    cfg_logic_.task_stack_mmap_count     = 60000;       // check sys mmap configure(linux: only, >=task_stack_busy_count*2+task_stack_keep_count)
    cfg_logic_.task_stack_pool_max_count = 25000;
    cfg_logic_.task_stack_busy_count     = 20000;
    cfg_logic_.task_stack_keep_count     = 10000;
    cfg_logic_.task_stack_busy_warn_count= 15000;
    loader.dump_to("logic.task.stack.size", cfg_logic_.task_stack_size);
    loader.dump_to("logic.task.csmsg.timeout", cfg_logic_.task_csmsg_timeout);
    loader.dump_to("logic.task.nomsg.timeout", cfg_logic_.task_nomsg_timeout);
    loader.dump_to("logic.task.stats.interval", cfg_logic_.task_paymsg_timeout);
    load_int_compare(loader, "logic.task.stack.gc_once_number", cfg_logic_.task_stack_gc_once_number, 10, 0);
    load_int_compare(loader, "logic.task.stack.pool_max_count", cfg_logic_.task_stack_pool_max_count, 25000, 0);
    load_int_compare(loader, "logic.task.stack.mmap_count", cfg_logic_.task_stack_mmap_count, 60000, 0);
    load_int_compare(loader, "logic.task.stack.busy_count", cfg_logic_.task_stack_busy_count, 20000, 0);
    load_int_compare(loader, "logic.task.stack.keep_count", cfg_logic_.task_stack_keep_count, 10000, 100);
    load_int_compare(loader, "logic.task.stack.busy_warn_count", cfg_logic_.task_stack_busy_warn_count, 15000, 0);
    

    cfg_logic_.heartbeat_interval        = 120;   // 120s for every ping/pong
    cfg_logic_.heartbeat_tolerance       = 20;    // 20s for network latency tolerance
    cfg_logic_.heartbeat_error_times     = 4;     // how much times of continue error will cause a kickoff
    cfg_logic_.heartbeat_ban_error_times = 3;     // how much times of continue kickoff will ban account
    cfg_logic_.heartbeat_ban_time_bound  = 10800; // 3 hours of ban time
    loader.dump_to("logic.heartbeat.interval", cfg_logic_.heartbeat_interval);
    loader.dump_to("logic.heartbeat.tolerance", cfg_logic_.heartbeat_tolerance);
    loader.dump_to("logic.heartbeat.error_times", cfg_logic_.heartbeat_error_times);
    loader.dump_to("logic.heartbeat.ban_error_times", cfg_logic_.heartbeat_ban_error_times);
    loader.dump_to("logic.heartbeat.ban_time_bound", cfg_logic_.heartbeat_ban_time_bound);

    // router
    cfg_logic_.router.cache_update_interval = 1800;
    cfg_logic_.router.cache_free_timeout = 600;
    cfg_logic_.router.object_free_timeout = 1500;
    cfg_logic_.router.retry_max_ttl = 3;
    loader.dump_to("logic.router.cache_update_interval", cfg_logic_.router.cache_update_interval, false);
    loader.dump_to("logic.router.cache_free_timeout", cfg_logic_.router.cache_free_timeout, false);
    loader.dump_to("logic.router.object_free_timeout", cfg_logic_.router.object_free_timeout, false);
    loader.dump_to("logic.router.retry_max_ttl", cfg_logic_.router.retry_max_ttl, false);
    load_int_compare(loader, "logic.router.object_save_interval", cfg_logic_.router.object_save_interval, 600, 60);
    load_int_compare(loader, "logic.router.detault_timer_interval", cfg_logic_.router.default_timer_interval, 300, 5);
    load_int_compare(loader, "logic.router.fast_timer_interval", cfg_logic_.router.fast_timer_interval, 8, 1);
}

void logic_config::_load_db(util::config::ini_loader &loader) {
    loader.dump_to("db.script.login", cfg_db_.db_script_file[hello::EN_DBSST_LOGIN]);
    loader.dump_to("db.script.player", cfg_db_.db_script_file[hello::EN_DBSST_PLAYER]);

    cfg_db_.time_retry_sec = 0;
    cfg_db_.time_retry_usec = 100000;
    cfg_db_.timeout = 75;
    cfg_db_.proc = 100;

    loader.dump_to("db.time.retry.sec", cfg_db_.time_retry_sec);
    loader.dump_to("db.time.retry.usec", cfg_db_.time_retry_usec);
    loader.dump_to("db.time.timeout", cfg_db_.timeout);
    loader.dump_to("db.time.proc", cfg_db_.proc);

    cfg_db_.cluster_default.clear();
    _load_db_hosts(cfg_db_.cluster_default, "cluster.default", loader);

    cfg_db_.raw_default.clear();
    _load_db_hosts(cfg_db_.raw_default, "raw.default", loader);
}

void logic_config::_load_db_hosts(std::vector<LC_DBCONN> &out, const char *group_name, util::config::ini_loader &loader) {
    std::stringstream ss;
    ss << "db." << group_name << ".host";
    std::string              path = ss.str();
    std::vector<std::string> urls;
    loader.dump_to(path, urls);

    owent_foreach(std::string & url, urls) {
        LC_DBCONN db_conn;
        db_conn.url               = url;
        std::string::size_type fn = db_conn.url.find_last_of(":");
        if (std::string::npos == fn) {
            db_conn.host = url;
            db_conn.port = 6379;
            out.push_back(db_conn);
        } else {
            db_conn.host = url.substr(0, fn);

            // check if it's IP:port-port mode
            std::string::size_type minu_pos = url.find('-', fn + 1);
            if (std::string::npos == minu_pos) {
                // IP:port
                util::string::str2int(db_conn.port, url.substr(fn + 1).c_str());
                out.push_back(db_conn);
            } else {
                // IP:begin_port-end_port
                uint16_t begin_port = 0, end_port = 0;
                util::string::str2int(begin_port, &url[fn + 1]);
                util::string::str2int(end_port, &url[minu_pos + 1]);

                for (db_conn.port = begin_port; db_conn.port < end_port; ++db_conn.port) {
                    ss.clear();
                    ss << db_conn.host << ":" << db_conn.port;
                    db_conn.url = ss.str();

                    out.push_back(db_conn);
                }
            }
        }
    }
}

void logic_config::_load_loginsvr(util::config::ini_loader &loader) {
    cfg_loginsvr_.version_cfg_file  = "../cfg/cfg_version.xml";
    cfg_loginsvr_.strategy_cfg_file = "../cfg/cfg_strategy.xml";
    cfg_loginsvr_.reload_version    = static_cast<uint32_t>(util::time::time_utility::get_now());

    loader.dump_to("loginsvr.gmsvr.timeout.sec", cfg_loginsvr_.gmsvr_timeout_sec);
    loader.dump_to("loginsvr.version_conf", cfg_loginsvr_.version_cfg_file, false);
    loader.dump_to("loginsvr.strategy_conf", cfg_loginsvr_.strategy_cfg_file, false);
    loader.dump_to("loginsvr.cdn.url", cfg_loginsvr_.cdn_url);

    cfg_loginsvr_.gamesvr_list.clear();
    loader.dump_to("loginsvr.gamesvr.addr", cfg_loginsvr_.gamesvr_list);

    cfg_loginsvr_.start_time = cfg_loginsvr_.end_time = 0;
    loader.dump_to("loginsvr.start_time", cfg_loginsvr_.start_time);
    loader.dump_to("loginsvr.end_time", cfg_loginsvr_.end_time);
    cfg_loginsvr_.relogin_expired_time = 3600;
    loader.dump_to("loginsvr.gamesvr.relogin_expire", cfg_loginsvr_.relogin_expired_time);

    cfg_loginsvr_.white_openid_list.clear();
    cfg_loginsvr_.debug_platform_mode = 0;
    loader.dump_to("loginsvr.white.openid", cfg_loginsvr_.white_openid_list);
    loader.dump_to("loginsvr.debug_platform", cfg_loginsvr_.debug_platform_mode);
}

void logic_config::_load_gamesvr(util::config::ini_loader &loader) {}
