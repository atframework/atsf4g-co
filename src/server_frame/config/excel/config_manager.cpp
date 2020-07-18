//
// Created by owt50 on 2016/9/23.
//

#include <log/log_wrapper.h>
#include <common/file_system.h>
#include <common/string_oprs.h>

#include <config/logic_config.h>
#include "config_manager.h"
#include "config_set.h"

namespace excel {
    config_manager::config_manager(): read_file_handle_(config_manager::default_loader){
        set_file_pattern("config/%s.bin");
    }

    config_manager::~config_manager() {}

    int config_manager::init(std::string file_pattern) {
        if (!file_pattern.empty()) {
            set_file_pattern(file_pattern);
        }

        return init();
    }

    int config_manager::init() {
        // 过滤掉excel里的空数据
        //config_player_init_items::me()->add_filter([](const config_player_init_items::value_type &v) { return v->id() > 0; });

        //config_player_init_items::me()->init("init_items_cfg",
        //                                        [](config_player_init_items::value_type v) { return config_player_init_items::key_type(v->id()); });

        return 0;
    }

    bool config_manager::load_file_data(std::string& write_to, const std::string& file_path) {
        if (!read_file_handle_) {
            WLOGERROR("invalid file data loader.");
            return false;
        }

        return read_file_handle_(write_to, file_path.c_str());
    }

    int config_manager::reload_all() {
        int ret = 0;
        for (config_set_base *cs : config_set_list_) {
            bool res = cs->reload();
            ret += res ? 1 : 0;
        }

        // index update

        return ret;
    }

    void config_manager::add_config_set(config_set_base* cs) {
        if (NULL == cs) {
            WLOGERROR("add null config set is not allowed.");
            return;
        }

        return config_set_list_.push_back(cs);
    }

    config_manager::read_buffer_func_t config_manager::get_loader() const {
        return read_file_handle_;
    }

    void config_manager::set_loader(read_buffer_func_t fn) {
        read_file_handle_ = fn;
    }

    std::string config_manager::get_file_pattern() const {
        return file_pattern_.substr(3);
    }

    void config_manager::set_file_pattern(const std::string& pattern) {
        file_pattern_.reserve(3 + pattern.size());
        file_pattern_ = "%s";
        file_pattern_ += util::file_system::DIRECTORY_SEPARATOR;
        file_pattern_ += pattern;
    }

    bool config_manager::default_loader(std::string& out, const char* path) {
        if (util::file_system::is_abs_path(path)) {
            return util::file_system::get_file_content(out, path, true);
        } else {
            char full_path[util::file_system::MAX_PATH_LEN] = {0};
            // path to excel configure directories
            UTIL_STRFUNC_SNPRINTF(full_path, sizeof(full_path), me()->file_pattern_.c_str(), 
                logic_config::me()->get_cfg_logic().server_resource_dir.c_str(), path);
            return util::file_system::get_file_content(out, full_path, true);
        }
    }
}