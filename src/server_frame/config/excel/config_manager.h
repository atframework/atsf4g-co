//
// Created by owent on 2016/9/23.
//

#ifndef CONFIG_EXCEL_CONFIG_MANAGER_H
#define CONFIG_EXCEL_CONFIG_MANAGER_H

#pragma once

#include <stdint.h>
#include <cstddef>
#include <string>
#include <std/functional.h>
#include <list>

#include <design_pattern/singleton.h>

namespace excel {
    class config_set_base;

    class config_manager : public util::design_pattern::singleton<config_manager> {
    public:
        typedef std::function<bool(std::string&, const char* path)> read_buffer_func_t;

    protected:
        config_manager();
        ~config_manager();

    public:
        int init(std::string file_pattern);
        int init();

        bool load_file_data(std::string& write_to, const std::string& file_path);

        int reload_all();

        void add_config_set(config_set_base* config_set);

        read_buffer_func_t get_loader() const;
        void set_loader(read_buffer_func_t fn);

        std::string get_file_pattern() const;
        void set_file_pattern(const std::string& pattern);
    private:
        static bool default_loader(std::string&, const char* path);

    private:
        read_buffer_func_t read_file_handle_;
        std::list<config_set_base*> config_set_list_;
        std::string file_pattern_;
    };

}
#endif //ATF4G_CO_CONFIG_MANAGER_H
