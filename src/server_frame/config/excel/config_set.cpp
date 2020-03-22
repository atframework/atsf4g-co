//
// Created by owt50 on 2016/9/23.
//

#include "config_set.h"
#include "config_manager.h"

namespace excel {
    config_set_base::config_set_base() {
        config_manager::me()->add_config_set(this);
    }

    config_set_base::~config_set_base() {}

    bool config_set_base::load_file_data(std::string& write_to, const std::string& file_path) {
        return config_manager::me()->load_file_data(write_to, file_path);
    }
}