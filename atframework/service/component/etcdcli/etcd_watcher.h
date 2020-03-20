/**
 * etcd_watcher.h
 *
 *  Created on: 2017-12-26
 *      Author: owent
 *
 *  Released under the MIT license
 */

#ifndef ATFRAME_SERVICE_COMPONENT_ETCDCLI_ETCD_WATCHER_H
#define ATFRAME_SERVICE_COMPONENT_ETCDCLI_ETCD_WATCHER_H

#pragma once

#include <sstream>
#include <string>
#include <vector>


#include <std/chrono.h>
#include <std/functional.h>
#include <std/smart_ptr.h>


#include <config/compiler_features.h>

#include <network/http_request.h>

#include "etcd_def.h"

namespace atframe {
    namespace component {
        class etcd_cluster;

        class etcd_watcher {
        public:
            typedef std::shared_ptr<etcd_watcher> ptr_t;

            struct event_t {
                etcd_watch_event::type evt_type;
                etcd_key_value         kv;
                etcd_key_value         prev_kv;
            };

            struct response_t {
                int64_t              watch_id;
                bool                 created;
                bool                 canceled;
                int64_t              compact_revision;
                std::vector<event_t> events;
            };

            typedef std::function<void(const etcd_response_header &header, const response_t &evt_data)> watch_event_fn_t;

        private:
            struct constrict_helper_t {};

        public:
            etcd_watcher(etcd_cluster &owner, const std::string &path, const std::string &range_end, constrict_helper_t &helper);
            ~etcd_watcher();
            static ptr_t create(etcd_cluster &owner, const std::string &path, const std::string &range_end = "+1");

            void close();

            const std::string &get_path() const;

            void active();

            etcd_cluster &      get_owner() { return *owner_; }
            const etcd_cluster &get_owner() const { return *owner_; }

            // ====================== apis for configure ==================

            inline bool is_progress_notify_enabled() const { return rpc_.enable_progress_notify; }
            inline void set_progress_notify_enabled(bool v) { rpc_.enable_progress_notify = v; }

            inline bool is_prev_kv_enabled() const { return rpc_.enable_prev_kv; }
            inline void set_prev_kv_enabled(bool v) { rpc_.enable_prev_kv = v; }

            inline void                                       set_conf_retry_interval(std::chrono::system_clock::duration v) { rpc_.retry_interval = v; }
            inline void                                       set_conf_retry_interval_sec(time_t v) { set_conf_retry_interval(std::chrono::seconds(v)); }
            inline const std::chrono::system_clock::duration &get_conf_retry_interval() const { return rpc_.retry_interval; }

            inline void                                       set_conf_request_timeout(std::chrono::system_clock::duration v) { rpc_.request_timeout = v; }
            inline void                                       set_conf_request_timeout_sec(time_t v) { set_conf_request_timeout(std::chrono::seconds(v)); }
            inline void                                       set_conf_request_timeout_min(time_t v) { set_conf_request_timeout(std::chrono::minutes(v)); }
            inline void                                       set_conf_request_timeout_hour(time_t v) { set_conf_request_timeout(std::chrono::hours(v)); }
            inline const std::chrono::system_clock::duration &get_conf_request_timeout() const { return rpc_.request_timeout; }

            // ====================== apis for events ==================
#if defined(UTIL_CONFIG_COMPILER_CXX_RVALUE_REFERENCES) && UTIL_CONFIG_COMPILER_CXX_RVALUE_REFERENCES
            inline void set_evt_handle(watch_event_fn_t &&fn) { evt_handle_ = std::move(fn); }
#else
            inline void set_evt_handle(const watch_event_fn_t &fn) { evt_handle_ = fn; }
#endif

        private:
            void process();

        private:
            static int libcurl_callback_on_range_completed(util::network::http_request &req);

            static int libcurl_callback_on_watch_completed(util::network::http_request &req);
            static int libcurl_callback_on_watch_write(util::network::http_request &req, const char *inbuf, size_t inbufsz, const char *&outbuf,
                                                       size_t &outbufsz);

        private:
            etcd_cluster *    owner_;
            std::string       path_;
            std::string       range_end_;
            std::stringstream rpc_data_stream_;
            int64_t           rpc_data_brackets_;
            struct rpc_data_t {
                util::network::http_request::ptr_t    rpc_opr_;
                bool                                  is_actived;
                bool                                  is_retry_mode;
                bool                                  enable_progress_notify;
                bool                                  enable_prev_kv;
                int64_t                               last_revision;
                std::chrono::system_clock::time_point watcher_next_request_time;
                std::chrono::system_clock::duration   retry_interval;
                std::chrono::system_clock::duration   request_timeout;
            };
            rpc_data_t rpc_;

            watch_event_fn_t evt_handle_;
        };
    } // namespace component
} // namespace atframe

#endif