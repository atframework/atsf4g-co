//
// Created by owent on 2016/9/23.
//

#ifndef CONFIG_EXCEL_CONFIG_SET_H
#define CONFIG_EXCEL_CONFIG_SET_H

#pragma once

#include <design_pattern/singleton.h>
#include <log/log_wrapper.h>

namespace excel {
    class config_set_base {
    protected:
        config_set_base();
        virtual ~config_set_base() = 0;

        bool load_file_data(std::string& write_to, const std::string& file_path);

    public:
        virtual bool reload() = 0;
    };

    template<typename TResLoader>
    class config_set_base_type: public TResLoader, public config_set_base {
    public:
        typedef TResLoader base_type;
        typedef typename base_type::key_type key_type;
        typedef typename base_type::value_type value_type;
        typedef typename base_type::proto_type proto_type;

        typedef typename base_type::func_type func_type;
        typedef typename base_type::filter_func_type filter_func_type;

    protected:
        config_set_base_type() {}
        ~config_set_base_type() {}

    public:
        int init(const std::string& file_path, func_type fn) {
            conf_file_path_ = file_path;
            base_type::set_key_handle(fn);

            return 0;
        }

        virtual bool reload() {
            std::string data;
            bool res = load_file_data(data, conf_file_path_);
            if (false == res) {
                WLOGERROR("load configure file %s failed.", conf_file_path_.c_str());
                return false;
            }

            base_type::clear();
            res = base_type::load(data.data(), data.size());

            if (res) {
                WLOGINFO("load configure file %s done,(%llu items)", conf_file_path_.c_str(), static_cast<unsigned long long>(base_type::size()));
            } else {
                WLOGERROR("load configure file %s failed.", conf_file_path_.c_str());
            }
            return res;
        }

    private:
        std::string conf_file_path_;
    };

    template<typename TResLoader, int INDEX>
    class config_set : public config_set_base_type<TResLoader>, public util::design_pattern::singleton< config_set<TResLoader, INDEX> > {
    public:
        typedef config_set_base_type<TResLoader> base_type;
        typedef typename base_type::key_type key_type;
        typedef typename base_type::value_type value_type;
        typedef typename base_type::proto_type proto_type;

        typedef typename base_type::func_type func_type;
        typedef typename base_type::filter_func_type filter_func_type;
    };
}
#endif //ATF4G_CO_CONFIG_SET_H
