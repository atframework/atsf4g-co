/**
 * etcd_cluster.h
 *
 *  Created on: 2017-11-17
 *      Author: owent
 *
 *  Released under the MIT license
 */

#ifndef ATFRAME_SERVICE_COMPONENT_ETCDCLI_ETCD_CLUSTER_H
#define ATFRAME_SERVICE_COMPONENT_ETCDCLI_ETCD_CLUSTER_H

#pragma once

#include <ctime>
#include <list>
#include <string>
#include <vector>

#include <std/chrono.h>
#include <std/smart_ptr.h>

#include <network/http_request.h>
#include <random/random_generator.h>
#include <time/time_utility.h>

#include <detail/libatbus_config.h>

#include "etcd_packer.h"

namespace atframe {
    namespace component {
        class etcd_keepalive;
        class etcd_watcher;
        class etcd_cluster;

        struct etcd_keepalive_deletor {
            util::network::http_request::ptr_t rpc;
            std::string                        path;
            void *                             keepalive_addr; // maybe already destroyed, just used to write log
            size_t                             retry_times;
            etcd_cluster *                     owner;
        };

        class etcd_cluster {
        public:
            struct flag_t {
                enum type {
                    CLOSING      = 0x0001, // closeing
                    RUNNING      = 0x0002,
                    ENABLE_LEASE = 0x0100, // enable auto get lease
                };
            };

            struct ssl_version_t {
                enum type {
                    DISABLED = 0,
                    SSL3,    // default for curl version < 7.34.0
                    TLS_V10, // TLSv1.0
                    TLS_V11, // TLSv1.1
                    TLS_V12, // TLSv1.2, default for curl version >= 7.34.0
                    TLS_V13, // TLSv1.3
                };
            };

            struct conf_t {
                std::vector<std::string>            conf_hosts;
                std::vector<std::string>            hosts;
                std::string                         authorization;
                std::chrono::system_clock::duration http_cmd_timeout;

                // generated data for cluster members
                std::vector<std::string>              authorization_user_roles;
                std::string                           authorization_header;
                std::chrono::system_clock::time_point authorization_next_update_time;
                std::chrono::system_clock::duration   authorization_retry_interval;
                /**
                 * @note We use /v3/auth/user/get to renew the timeout of authorization token.
                 *       /v3/lease/keepalive do not need authorization and can not used to renew the token.
                 *       auth-token=simple will set timeout to 5 minutes, so we use auth_user_get_retry_interval=2 minutes for default
                 *       If set auth-token=jwt,pub-key=PUBLIC KEY,priv-key=PRIVITE KEY,sign-method=METHOD,ttl=TTL
                 *          please set auth_user_get_retry_interval to any value less than TTL
                 * @see https://etcd.io/docs/v3.4.0/op-guide/configuration/#--auth-token
                 * @see https://github.com/etcd-io/etcd/blob/master/auth/simple_token.go
                 * @see https://github.com/etcd-io/etcd/blob/master/auth/jwt.go
                 */
                std::chrono::system_clock::time_point auth_user_get_next_update_time;
                std::chrono::system_clock::duration   auth_user_get_retry_interval;
                std::string                           path_node;
                std::chrono::system_clock::time_point etcd_members_next_update_time;
                std::chrono::system_clock::duration   etcd_members_update_interval;
                std::chrono::system_clock::duration   etcd_members_retry_interval;

                // generated data for lease
                int64_t                               lease;
                std::chrono::system_clock::time_point keepalive_next_update_time;
                std::chrono::system_clock::duration   keepalive_timeout;
                std::chrono::system_clock::duration   keepalive_interval;

                // SSL configure
                // @see https://github.com/etcd-io/etcd/blob/master/Documentation/op-guide/security.md for detail
                bool ssl_enable_alpn; // curl 7.36.0 CURLOPT_SSL_ENABLE_ALPN
                bool ssl_verify_peer; // CURLOPT_SSL_VERIFYPEER

                ssl_version_t::type ssl_min_version;       // CURLOPT_SSLVERSION @see ssl_version_t, SSLv3/TLSv1/TLSv1.1/TLSv1.2/TLSv1.3
                std::string         ssl_client_cert;       // CURLOPT_SSLCERT
                std::string         ssl_client_cert_type;  // curl 7.9.3 CURLOPT_SSLCERTTYPE , PEM for default
                std::string         ssl_client_key;        // CURLOPT_SSLKEY
                std::string         ssl_client_key_passwd; // curl 7.16.4 CURLOPT_SSLKEYPASSWD , curl 7.9.2 CURLOPT_SSLCERTPASSWD
                std::string         ssl_ca_cert;           // CURLOPT_CAINFO
                std::string
                    ssl_cipher_list; // CURLOPT_SSL_CIPHER_LIST, default:
                                     // ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:DHE-RSA-AES128-GCM-SHA256:DHE-RSA-AES256-GCM-SHA384
                                     // ,@see https://curl.haxx.se/docs/ssl-ciphers.html
                std::string ssl_cipher_list_tls13; // curl 7.61.0, openssl 1.1.1 CURLOPT_TLS13_CIPHERS, default:
                                                   //   TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256
            };

            struct stats_t {
                size_t sum_error_requests;
                size_t continue_error_requests;
                size_t sum_success_requests;
                size_t continue_success_requests;

                size_t sum_create_requests;
            };

        public:
            etcd_cluster();
            ~etcd_cluster();

            void init(const util::network::http_request::curl_m_bind_ptr_t &curl_mgr);

            util::network::http_request::ptr_t close(bool wait = false);
            void                               reset();
            int                                tick();
            bool                               is_available() const;

            inline bool check_flag(uint32_t f) const { return 0 != (flags_ & f); };
            void        set_flag(flag_t::type f, bool v);

            inline const stats_t &get_stats() const { return stats_; };
            // ====================== apis for configure ==================
            inline const std::vector<std::string> &get_available_hosts() const { return conf_.hosts; }
            inline const std::string &             get_selected_host() const { return conf_.path_node; }

            inline void               set_conf_authorization(const std::string &authorization) { conf_.authorization = authorization; }
            inline const std::string &get_conf_authorization() const { return conf_.authorization; }
            void                      pick_conf_authorization(std::string &username, std::string *password);

            inline int64_t get_keepalive_lease() const { return get_lease(); }

            inline void                            set_conf_hosts(const std::vector<std::string> &hosts) { conf_.conf_hosts = hosts; }
            inline const std::vector<std::string> &get_conf_hosts() const { return conf_.conf_hosts; }

            inline void                                       set_conf_http_timeout(std::chrono::system_clock::duration v) { conf_.http_cmd_timeout = v; }
            inline void                                       set_conf_http_timeout_sec(time_t v) { set_conf_http_timeout(std::chrono::seconds(v)); }
            inline const std::chrono::system_clock::duration &get_conf_http_timeout() const { return conf_.http_cmd_timeout; }
            time_t                                            get_http_timeout_ms() const;

            inline void set_conf_etcd_members_update_interval(std::chrono::system_clock::duration v) { conf_.etcd_members_update_interval = v; }
            inline void set_conf_etcd_members_update_interval_min(time_t v) { set_conf_etcd_members_update_interval(std::chrono::minutes(v)); }
            inline const std::chrono::system_clock::duration &get_conf_etcd_members_update_interval() const { return conf_.etcd_members_update_interval; }

            inline void set_conf_etcd_members_retry_interval(std::chrono::system_clock::duration v) { conf_.etcd_members_retry_interval = v; }
            inline void set_conf_etcd_members_retry_interval_min(time_t v) { set_conf_etcd_members_retry_interval(std::chrono::minutes(v)); }
            inline const std::chrono::system_clock::duration &get_conf_etcd_members_retry_interval() const { return conf_.etcd_members_retry_interval; }

            inline void                                       set_conf_keepalive_timeout(std::chrono::system_clock::duration v) { conf_.keepalive_timeout = v; }
            inline void                                       set_conf_keepalive_timeout_sec(time_t v) { set_conf_keepalive_timeout(std::chrono::seconds(v)); }
            inline const std::chrono::system_clock::duration &get_conf_keepalive_timeout() const { return conf_.keepalive_timeout; }

            inline void set_conf_keepalive_interval(std::chrono::system_clock::duration v) { conf_.keepalive_interval = v; }
            inline void set_conf_keepalive_interval_sec(time_t v) { set_conf_keepalive_interval(std::chrono::seconds(v)); }
            inline const std::chrono::system_clock::duration &get_conf_keepalive_interval() const { return conf_.keepalive_interval; }

            inline void set_conf_ssl_enable_alpn(bool v) { conf_.ssl_enable_alpn = v; }
            inline bool get_conf_ssl_enable_alpn() const { return conf_.ssl_enable_alpn; }

            inline void set_conf_ssl_verify_peer(bool v) { conf_.ssl_verify_peer = v; }
            inline bool get_conf_ssl_verify_peer() const { return conf_.ssl_verify_peer; }

            inline void                set_conf_ssl_min_version(ssl_version_t::type v) { conf_.ssl_min_version = v; }
            inline ssl_version_t::type get_conf_ssl_min_version() const { return conf_.ssl_min_version; }

            inline void               set_conf_ssl_client_cert(const std::string &v) { conf_.ssl_client_cert = v; }
            inline const std::string &get_conf_ssl_client_cert() const { return conf_.ssl_client_cert; }

            inline void               set_conf_ssl_client_cert_type(const std::string &v) { conf_.ssl_client_cert_type = v; }
            inline const std::string &get_conf_ssl_client_cert_type() const { return conf_.ssl_client_cert_type; }

            inline void               set_conf_ssl_client_key(const std::string &v) { conf_.ssl_client_key = v; }
            inline const std::string &get_conf_ssl_client_key() const { return conf_.ssl_client_key; }

            inline void               set_conf_ssl_client_key_passwd(const std::string &v) { conf_.ssl_client_key_passwd = v; }
            inline const std::string &get_conf_ssl_client_key_passwd() const { return conf_.ssl_client_key_passwd; }

            inline void               set_conf_ssl_ca_cert(const std::string &v) { conf_.ssl_ca_cert = v; }
            inline const std::string &get_conf_ssl_ca_cert() const { return conf_.ssl_ca_cert; }

            inline void               set_conf_ssl_cipher_list(const std::string &v) { conf_.ssl_cipher_list = v; }
            inline const std::string &get_conf_ssl_cipher_list() const { return conf_.ssl_cipher_list; }

            inline void               set_conf_ssl_cipher_list_tls13(const std::string &v) { conf_.ssl_cipher_list_tls13 = v; }
            inline const std::string &get_conf_ssl_cipher_list_tls13() const { return conf_.ssl_cipher_list_tls13; }

            // ================== apis for sub-services ==================
            bool add_keepalive(const std::shared_ptr<etcd_keepalive> &keepalive);
            bool add_retry_keepalive(const std::shared_ptr<etcd_keepalive> &keepalive);
            bool remove_keepalive(std::shared_ptr<etcd_keepalive> keepalive);
            bool add_watcher(const std::shared_ptr<etcd_watcher> &watcher);
            bool remove_watcher(std::shared_ptr<etcd_watcher> watcher);

            // ================== apis of create request for key-value operation ==================
        public:
            /**
             * @brief               create request for range get key-value data
             * @param key	        key is the first key for the range. If range_end is not given, the request only looks up key.
             * @param range_end	    range_end is the upper bound on the requested range [key, range_end). just like etcd_packer::pack_key_range
             * @param limit	        limit is a limit on the number of keys returned for the request. When limit is set to 0, it is treated as no limit.
             * @param revision	    revision is the point-in-time of the key-value store to use for the range. If revision is less or equal to zero, the range
             *                      is over the newest key-value store. If the revision has been compacted, ErrCompacted is returned as a response.
             * @return http request
             */
            util::network::http_request::ptr_t create_request_kv_get(const std::string &key, const std::string &range_end = "", int64_t limit = 0,
                                                                     int64_t revision = 0);

            /**
             * @brief               create request for set key-value data
             * @param key	        key is the key, in bytes, to put into the key-value store.
             * @param value	        value is the value, in bytes, to associate with the key in the key-value store.
             * @param assign_lease	if add lease ID to associate with the key in the key-value store. A lease value of 0 indicates no lease.
             * @param prev_kv	    If prev_kv is set, etcd gets the previous key-value pair before changing it. The previous key-value pair will be returned in
             *                      the put response.
             * @param ignore_value	If ignore_value is set, etcd updates the key using its current value. Returns an error if the key does not exist.
             * @param ignore_lease	If ignore_lease is set, etcd updates the key using its current lease. Returns an error if the key does not exist.
             * @return http request
             */
            util::network::http_request::ptr_t create_request_kv_set(const std::string &key, const std::string &value, bool assign_lease = false,
                                                                     bool prev_kv = false, bool ignore_value = false, bool ignore_lease = false);

            /**
             * @brief               create request for range delete key-value data
             * @param key	        key is the first key for the range. If range_end is not given, the request only looks up key.
             * @param range_end	    range_end is the upper bound on the requested range [key, range_end). just like etcd_packer::pack_key_range
             * @param prev_kv	    If prev_kv is set, etcd gets the previous key-value pairs before deleting it. The previous key-value pairs will be
             *                      returned in the delete response.
             * @return http request
             */
            util::network::http_request::ptr_t create_request_kv_del(const std::string &key, const std::string &range_end = "", bool prev_kv = false);

            /**
             * @brief                   create request for watch
             * @param key	            key is the first key for the range. If range_end is not given, the request only looks up key.
             * @param range_end	        range_end is the upper bound on the requested range [key, range_end). just like etcd_packer::pack_key_range
             * @param start_revision	start_revision is an optional revision to watch from (inclusive). No start_revision or 0 is "now".
             * @param prev_kv	        If prev_kv is set, created watcher gets the previous KV before the event happens. If the previous KV is already
             *                          compacted, nothing will be returned.
             * @param progress_notify   progress_notify is set so that the etcd server will periodically send a WatchResponse with no events to the new watcher
             *                          if there are no recent events. It is useful when clients wish to recover a disconnected watcher starting from a recent
             *                          known revision. The etcd server may decide how often it will send notifications based on current load.
             * @return http request
             */
            util::network::http_request::ptr_t create_request_watch(const std::string &key, const std::string &range_end = "", int64_t start_revision = 0,
                                                                    bool prev_kv = false, bool progress_notify = true);

        private:
#if defined(UTIL_CONFIG_COMPILER_CXX_RVALUE_REFERENCES) && UTIL_CONFIG_COMPILER_CXX_RVALUE_REFERENCES
            void remove_keepalive_path(etcd_keepalive_deletor *keepalive_deletor, bool delay_delete);
#else
            void remove_keepalive_path(etcd_keepalive_deletor *&&keepalive_deletor, bool delay_delete);
#endif
            static int libcurl_callback_on_remove_keepalive_path(util::network::http_request &req);

            void           retry_pending_actions();
            void           set_lease(int64_t v, bool force_active_keepalives);
            inline int64_t get_lease() const { return conf_.lease; }

            bool       create_request_auth_authenticate();
            static int libcurl_callback_on_auth_authenticate(util::network::http_request &req);
            bool       create_request_auth_user_get();
            static int libcurl_callback_on_auth_user_get(util::network::http_request &req);

            bool       retry_request_member_update();
            bool       create_request_member_update();
            static int libcurl_callback_on_member_update(util::network::http_request &req);

            bool                               create_request_lease_grant();
            bool                               create_request_lease_keepalive();
            static int                         libcurl_callback_on_lease_keepalive(util::network::http_request &req);
            util::network::http_request::ptr_t create_request_lease_revoke();

            void add_stats_error_request();
            void add_stats_success_request();
            void add_stats_create_request();

            bool check_authorization() const;

            static void delete_keepalive_deletor(etcd_keepalive_deletor *in, bool close_rpc);
            void        cleanup_keepalive_deletors();

        public:
            /**
             * @see https://github.com/grpc-ecosystem/grpc-gateway/blob/master/runtime/errors.go
             * @see https://golang.org/pkg/net/http/
             */
            void check_authorization_expired(int http_code, const std::string &content);

            void setup_http_request(util::network::http_request::ptr_t &req, rapidjson::Document &doc, time_t timeout);

        private:
            typedef ATBUS_ADVANCE_TYPE_MAP(std::string, etcd_keepalive_deletor *) etcd_keepalive_deletor_map_t;
            uint32_t                                       flags_;
            util::random::mt19937                          random_generator_;
            conf_t                                         conf_;
            stats_t                                        stats_;
            util::network::http_request::curl_m_bind_ptr_t curl_multi_;
            util::network::http_request::ptr_t             rpc_authenticate_;
            util::network::http_request::ptr_t             rpc_update_members_;
            util::network::http_request::ptr_t             rpc_keepalive_;
            std::vector<std::shared_ptr<etcd_keepalive> >  keepalive_actors_;
            std::vector<std::shared_ptr<etcd_keepalive> >  keepalive_retry_actors_;
            etcd_keepalive_deletor_map_t                   keepalive_deletors_;
            std::vector<std::shared_ptr<etcd_watcher> >    watcher_actors_;
        };
    } // namespace component
} // namespace atframe

#endif