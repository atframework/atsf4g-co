/**
 * etcd_keepalive.h
 *
 *  Created on: 2017-12-26
 *      Author: owent
 *
 *  Released under the MIT license
 */

#ifndef ATFRAME_SERVICE_COMPONENT_ETCDCLI_ETCD_KEEPALIVE_H
#define ATFRAME_SERVICE_COMPONENT_ETCDCLI_ETCD_KEEPALIVE_H

#pragma once

#include <string>

#include <std/functional.h>
#include <std/smart_ptr.h>

#include <config/compiler_features.h>

#include <network/http_request.h>


namespace atframe {
    namespace component {
        class etcd_cluster;

        class etcd_keepalive : public std::enable_shared_from_this<etcd_keepalive> {
        public:
            typedef std::function<bool(const std::string &)> checker_fn_t; // the parameter will be base64 of the value
            typedef std::shared_ptr<etcd_keepalive>          ptr_t;

            struct default_checker_t {
                default_checker_t(const std::string &checked);

                bool operator()(const std::string &checked) const;

                std::string data;
            };

        private:
            struct constrict_helper_t {};

        public:
            etcd_keepalive(etcd_cluster &owner, const std::string &path, constrict_helper_t &helper);
            ~etcd_keepalive();
            static ptr_t create(etcd_cluster &owner, const std::string &path);

            void close(bool reset_has_data_flag);

            void set_checker(const std::string &checked_str);
            void set_checker(checker_fn_t fn);

            void set_value(const std::string &str);
            void reset_value_changed();

            inline const std::string &get_value() const { return value_; }

            const std::string &get_path() const;

            void active();

            etcd_cluster &      get_owner() { return *owner_; }
            const etcd_cluster &get_owner() const { return *owner_; }

            inline bool   is_check_run() const { return checker_.is_check_run; }
            inline bool   is_check_passed() const { return checker_.is_check_passed; }
            inline size_t get_check_times() const { return checker_.retry_times; }
            inline bool   has_data() const { return rpc_.has_data; }

        private:
            void process();

        private:
            static int libcurl_callback_on_get_data(util::network::http_request &req);
            static int libcurl_callback_on_set_data(util::network::http_request &req);

        private:
            etcd_cluster *owner_;
            std::string   path_;
            std::string   value_;
            typedef struct {
                util::network::http_request::ptr_t rpc_opr_;
                bool                               is_actived;
                bool                               is_value_changed;
                bool                               has_data;
            } rpc_data_t;
            rpc_data_t rpc_;

            typedef struct {
                checker_fn_t fn;
                bool         is_check_run;
                bool         is_check_passed;
                size_t       retry_times;
            } checker_t;
            checker_t checker_;
        };
    } // namespace component
} // namespace atframe

#endif