//
// Created by owt50 on 2016/9/23.
//

#include <common/string_oprs.h>
#include <sstream>
#include <std/foreach.h>
#include <time/time_utility.h>

#include <atframe/atapp.h>

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

logic_config::logic_config() : server_id_(0), const_settings_(nullptr), atframe_settings_(nullptr) {}
logic_config::~logic_config() {}


int logic_config::init(uint64_t server_id, const std::string &server_name) {
    server_id_   = server_id;
    server_name_ = server_name;
    return 0;
}

int logic_config::reload(atapp::app &app) {
    util::config::ini_loader &cfg_set = app.get_configure_loader();
    const_settings_                   = nullptr;
    atframe_settings_                 = nullptr;

    _load_server_cfg(app);

    const util::config::ini_value::node_type &children = cfg_set.get_root_node().get_children();
    if (children.find("logic") != children.end()) {
        _load_logic(cfg_set);
    }

    _load_db();

    if (children.find("loginsvr") != children.end()) {
        _load_loginsvr(cfg_set);
    }

    return 0;
}

uint64_t logic_config::get_self_bus_id() const { return server_id_; }

const std::string &logic_config::get_self_name() const { return server_name_; }

void logic_config::_load_logic(util::config::ini_loader &loader) {
    cfg_logic_.zone_id   = 0;
    cfg_logic_.zone_step = 256;
    loader.dump_to("logic.zone.id", cfg_logic_.zone_id);
    loader.dump_to("logic.zone.step", cfg_logic_.zone_step);

    cfg_logic_.server_maintenance_mode = false;
    cfg_logic_.server_open_time        = util::time::time_utility::get_now();
    cfg_logic_.server_resource_dir     = "../../resource";

    loader.dump_to("logic.server.open_service_time", cfg_logic_.server_open_time);
    loader.dump_to("logic.server.maintenance_mode", cfg_logic_.server_maintenance_mode);
    loader.dump_to("logic.server.resource.dir", cfg_logic_.server_resource_dir);

    // player
    cfg_logic_.player_max_online_number = 10000;
    cfg_logic_.player_default_openid    = "gm://system";

    loader.dump_to("logic.user.max_online", cfg_logic_.player_max_online_number);
    loader.dump_to("logic.user.default_openid", cfg_logic_.player_default_openid);

    cfg_logic_.session_login_code_protect   = 1200;  // 20m for expired of bad token protect
    cfg_logic_.session_login_code_valid_sec = 600;   // 10m for expired of token
    cfg_logic_.session_login_ban_time       = 10800; // 3 hours when ban by anti cheating
    cfg_logic_.session_tick_sec             = 60;    // session event tick interval(for example: online number)
    loader.dump_to("logic.session.login_code_protect", cfg_logic_.session_login_code_protect);
    loader.dump_to("logic.session.login_code_valid_sec", cfg_logic_.session_login_code_valid_sec);
    loader.dump_to("logic.session.login_ban_time", cfg_logic_.session_login_ban_time);
    loader.dump_to("logic.session.tick_sec", cfg_logic_.session_tick_sec);

    cfg_logic_.task_stack_size            = 1024 * 1024; // default for 1MB
    cfg_logic_.task_csmsg_timeout         = 5;           // 5s
    cfg_logic_.task_nomsg_timeout         = 1800;        // 1800s for auto task
    cfg_logic_.task_paymsg_timeout        = 300;         // 300s for pay task
    cfg_logic_.task_stats_interval        = 60;          // 60s for stats interval
    cfg_logic_.task_stack_gc_once_number  = 10;
    cfg_logic_.task_stack_mmap_count      = 60000; // check sys mmap configure(linux: only, >=task_stack_busy_count*2+task_stack_keep_count)
    cfg_logic_.task_stack_pool_max_count  = 25000;
    cfg_logic_.task_stack_busy_count      = 20000;
    cfg_logic_.task_stack_keep_count      = 10000;
    cfg_logic_.task_stack_busy_warn_count = 15000;
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
    cfg_logic_.router.cache_free_timeout    = 600;
    cfg_logic_.router.object_free_timeout   = 1500;
    cfg_logic_.router.retry_max_ttl         = 3;
    loader.dump_to("logic.router.cache_update_interval", cfg_logic_.router.cache_update_interval, false);
    loader.dump_to("logic.router.cache_free_timeout", cfg_logic_.router.cache_free_timeout, false);
    loader.dump_to("logic.router.object_free_timeout", cfg_logic_.router.object_free_timeout, false);
    loader.dump_to("logic.router.retry_max_ttl", cfg_logic_.router.retry_max_ttl, false);
    load_int_compare(loader, "logic.router.object_save_interval", cfg_logic_.router.object_save_interval, 600, 60);
    load_int_compare(loader, "logic.router.detault_timer_interval", cfg_logic_.router.default_timer_interval, 300, 5);
    load_int_compare(loader, "logic.router.fast_timer_interval", cfg_logic_.router.fast_timer_interval, 8, 1);
}

void logic_config::_load_db() {
    _load_db_hosts(*server_cfg_.mutable_db()->mutable_cluster(), "cluster");
    _load_db_hosts(*server_cfg_.mutable_db()->mutable_raw(), "raw");
}

void logic_config::_load_db_hosts(hello::config::db_group_cfg &out, const char *group_name) {
    for (int i = 0; i < out.host_size(); ++i) {
        const std::string &host = out.host(i);
        out.clear_gateways();

        std::string::size_type fn = host.find_last_of(":");
        if (std::string::npos == fn) {
            hello::config::db_group_gateway_cfg *db_gateway = out.add_gateways();
            if (NULL != db_gateway) {
                db_gateway->set_port(6379);
                db_gateway->set_host(host);
                db_gateway->set_url(LOG_WRAPPER_FWAPI_FORMAT("{}:{}", db_gateway->host(), db_gateway->port()));
            }
        } else {
            // check if it's IP:port-port mode
            std::string::size_type minu_pos = host.find('-', fn + 1);
            if (std::string::npos == minu_pos) {
                // IP:port
                hello::config::db_group_gateway_cfg *db_gateway = out.add_gateways();
                if (NULL != db_gateway) {
                    db_gateway->set_port(util::string::to_int<int32_t>(host.c_str() + fn + 1));
                    db_gateway->set_host(host.substr(0, fn));
                    db_gateway->set_url(LOG_WRAPPER_FWAPI_FORMAT("{}:{}", db_gateway->host(), db_gateway->port()));
                }
            } else {
                // IP:begin_port-end_port
                int32_t begin_port = 0, end_port = 0;
                util::string::str2int(begin_port, &host[fn + 1]);
                util::string::str2int(end_port, &host[minu_pos + 1]);

                for (int32_t port = begin_port; port < end_port; ++port) {
                    hello::config::db_group_gateway_cfg *db_gateway = out.add_gateways();
                    if (NULL != db_gateway) {
                        db_gateway->set_port(port);
                        db_gateway->set_host(host.substr(0, fn));
                        db_gateway->set_url(LOG_WRAPPER_FWAPI_FORMAT("{}:{}", db_gateway->host(), db_gateway->port()));
                    }
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

const hello::DConstSettingsType &logic_config::get_const_settings() {
    if (likely(nullptr != const_settings_)) {
        return *const_settings_;
    }
    auto desc = ::google::protobuf::DescriptorPool::generated_pool()->FindFileByName("com.const.proto");
    if (nullptr != desc && desc->options().HasExtension(hello::CONST_SETTINGS)) {
        const_settings_ = &desc->options().GetExtension(hello::CONST_SETTINGS);
    }

    if (nullptr == const_settings_) {
        return hello::DConstSettingsType::default_instance();
    }

    return *const_settings_;
}

const atframework::ConstSettingsType &logic_config::get_atframework_settings() {
    if (likely(nullptr != atframe_settings_)) {
        return *atframe_settings_;
    }
    auto desc = ::google::protobuf::DescriptorPool::generated_pool()->FindFileByName("atframework.proto");
    if (nullptr != desc && desc->options().HasExtension(atframework::CONST_SETTINGS)) {
        atframe_settings_ = &desc->options().GetExtension(atframework::CONST_SETTINGS);
    }

    if (nullptr == atframe_settings_) {
        return atframework::ConstSettingsType::default_instance();
    }

    return *atframe_settings_;
}

void logic_config::_load_server_cfg(atapp::app &app) {
    server_cfg_.Clear();
    app.parse_configures_into(server_cfg_, std::string());
}
