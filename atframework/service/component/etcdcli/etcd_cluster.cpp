#include <assert.h>

#include <std/explicit_declare.h>

#include <common/string_oprs.h>

#include <log/log_wrapper.h>

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#include "etcd_keepalive.h"
#include "etcd_watcher.h"

#include "etcd_cluster.h"

namespace atframe {
    namespace component {
        /**
         * @note APIs just like this
         * @see https://coreos.com/etcd/docs/latest/dev-guide/api_reference_v3.html
         * @see https://coreos.com/etcd/docs/latest/dev-guide/apispec/swagger/rpc.swagger.json
         * @note KeyValue: { "key": "KEY", "create_revision": "number", "mod_revision": "number", "version": "number", "value": "", "lease": "number" }
         *   Get data => curl http://localhost:2379/v3/kv/range -X POST -d '{"key": "KEY", "range_end": ""}'
         *       # Response {"kvs": [{...}], "more": "bool", "count": "COUNT"}
         *   Set data => curl http://localhost:2379/v3/kv/put -X POST -d '{"key": "KEY", "value": "", "lease": "number", "prev_kv": "bool"}'
         *   Renew data => curl http://localhost:2379/v3/kv/put -X POST -d '{"key": "KEY", "value": "", "prev_kv": "bool", "ignore_lease": true}'
         *       # Response {"header":{...}, "prev_kv": {...}}
         *   Delete data => curl http://localhost:2379/v3/kv/deleterange -X POST -d '{"key": "KEY", "range_end": "", "prev_kv": "bool"}'
         *       # Response {"header":{...}, "deleted": "number", "prev_kvs": [{...}]}
         *
         *   Watch => curl http://localhost:2379/v3/watch -XPOST -d '{"create_request":  {"key": "WATCH KEY", "range_end": "", "prev_kv": true} }'
         *       # Response {"header":{...},"watch_id":"ID","created":"bool", "canceled": "bool", "compact_revision": "REVISION", "events": [{"type":
         *                  "PUT=0|DELETE=1", "kv": {...}, prev_kv": {...}"}]}
         *
         *   Allocate Lease => curl http://localhost:2379/v3/lease/grant -XPOST -d '{"TTL": 5, "ID": 0}'
         *       # Response {"header":{...},"ID":"ID","TTL":"5"}
         *   Keepalive Lease => curl http://localhost:2379/v3/lease/keepalive -XPOST -d '{"ID": 0}'
         *       # Response {"header":{...},"ID":"ID","TTL":"5"}
         *   Revoke Lease => curl http://localhost:2379/v3/kv/lease/revoke -XPOST -d '{"ID": 0}'
         *       # Response {"header":{...}}
         *
         *   List members => curl http://localhost:2379/v3/cluster/member/list -XPOST -d '{}'
         *       # Response {"header":{...},"members":[{"ID":"ID","name":"NAME","peerURLs":["peer url"],"clientURLs":["client url"]}]}
         *
         *   Authorization Header => curl -H "Authorization: TOKEN"
         *   Authorization => curl http://localhost:2379/v3/auth/authenticate -XPOST -d '{"name": "username", "password": "pass"}'
         *       # Response {"header":{...}, "token": "TOKEN"}
         *       # Return 401 if auth token invalid
         *       # Return 400 with {"error": "etcdserver: user name is empty", "code": 3} if need TOKEN
         *       # Return 400 with {"error": "etcdserver: authentication failed, ...", "code": 3} if username of password invalid
         *   Authorization Enable:
         *       curl -L http://127.0.0.1:2379/v3/auth/user/add -XPOST -d '{"name": "root", "password": "3d91123233ffd36825bf2aca17808bfe"}'
         *       curl -L http://127.0.0.1:2379/v3/auth/role/add -XPOST -d '{"name": "root"}'
         *       curl -L http://127.0.0.1:2379/v3/auth/user/grant -XPOST -d '{"user": "root", "role": "root"}'
         *       curl -L http://127.0.0.1:2379/v3/auth/enable -XPOST -d '{}'
         */

#define ETCD_API_V3_ERROR_HTTP_CODE_AUTH 401
#define ETCD_API_V3_ERROR_HTTP_INVALID_PARAM 400
#define ETCD_API_V3_ERROR_HTTP_PRECONDITION 412
// @see https://godoc.org/google.golang.org/grpc/codes
#define ETCD_API_V3_ERROR_GRPC_CODE_UNAUTHENTICATED 16

#define ETCD_API_V3_MEMBER_LIST "/v3/cluster/member/list"
#define ETCD_API_V3_AUTH_AUTHENTICATE "/v3/auth/authenticate"

#define ETCD_API_V3_KV_GET "/v3/kv/range"
#define ETCD_API_V3_KV_SET "/v3/kv/put"
#define ETCD_API_V3_KV_DELETE "/v3/kv/deleterange"

#define ETCD_API_V3_WATCH "/v3/watch"

#define ETCD_API_V3_LEASE_GRANT "/v3/lease/grant"
#define ETCD_API_V3_LEASE_KEEPALIVE "/v3/lease/keepalive"
#define ETCD_API_V3_LEASE_REVOKE "/v3/kv/lease/revoke"

        namespace details {
            const std::string &get_default_user_agent() {
                static std::string ret;
                if (!ret.empty()) {
                    return ret;
                }

                char        buffer[256] = {0};
                const char *prefix      = "Mozilla/5.0";
                const char *suffix      = "Atframework Etcdcli/1.0";
#if defined(_WIN32) || defined(__WIN32__)
#if (defined(__MINGW32__) && __MINGW32__)
                const char *sys_env = "Win32; MinGW32";
#elif (defined(__MINGW64__) || __MINGW64__)
                const char *sys_env = "Win64; x64; MinGW64";
#elif defined(__CYGWIN__) || defined(__MSYS__)
#if defined(_WIN64) || defined(__amd64) || defined(__x86_64)
                const char *sys_env = "Win64; x64; POSIX";
#else
                const char *sys_env = "Win32; POSIX";
#endif
#elif defined(_WIN64) || defined(__amd64) || defined(__x86_64)
                const char *sys_env = "Win64; x64";
#else
                const char *sys_env = "Win32";
#endif
#elif defined(__linux__) || defined(__linux)
                const char *sys_env = "Linux";
#elif defined(__APPLE__)
                const char *sys_env = "Darwin";
#elif defined(__DragonFly__) || defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || defined(__OpenBSD__) || defined(__NetBSD__)
                const char *sys_env = "BSD";
#elif defined(__unix__) || defined(__unix)
                const char *sys_env = "Unix";
#else
                const char *sys_env = "Unknown";
#endif

                UTIL_STRFUNC_SNPRINTF(buffer, sizeof(buffer) - 1, "%s (%s) %s", prefix, sys_env, suffix);
                ret = &buffer[0];

                return ret;
            }

            static int etcd_cluster_trace_porcess_callback(util::network::http_request &req, const util::network::http_request::progress_t &process) {
                WLOGTRACE("Etcd cluster %p http request %p to %s, process: download %llu/%llu, upload %llu/%llu", req.get_priv_data(), &req,
                          req.get_url().c_str(), static_cast<unsigned long long>(process.dlnow), static_cast<unsigned long long>(process.dltotal),
                          static_cast<unsigned long long>(process.ulnow), static_cast<unsigned long long>(process.ultotal));
                return 0;
            }

            EXPLICIT_UNUSED_ATTR static int etcd_cluster_verbose_callback(util::network::http_request &req, curl_infotype type, char *data, size_t size) {
                if (util::log::log_wrapper::check_level(WDTLOGGETCAT(util::log::log_wrapper::categorize_t::DEFAULT),
                                                        util::log::log_wrapper::level_t::LOG_LW_TRACE)) {
                    const char *verbose_type = "Unknown Action";
                    switch (type) {
                    case CURLINFO_TEXT:
                        verbose_type = "Text";
                        break;
                    case CURLINFO_HEADER_OUT:
                        verbose_type = "Header Send";
                        break;
                    case CURLINFO_DATA_OUT:
                        verbose_type = "Data Send";
                        break;
                    case CURLINFO_SSL_DATA_OUT:
                        verbose_type = "SSL Data Send";
                        break;
                    case CURLINFO_HEADER_IN:
                        verbose_type = "Header Received";
                        break;
                    case CURLINFO_DATA_IN:
                        verbose_type = "Data Received";
                        break;
                    case CURLINFO_SSL_DATA_IN:
                        verbose_type = "SSL Data Received";
                        break;
                    default: /* in case a new one is introduced to shock us */
                        break;
                    }

                    util::log::log_wrapper::caller_info_t caller = WDTLOGFILENF(util::log::log_wrapper::level_t::LOG_LW_TRACE, "Trace");
                    WDTLOGGETCAT(util::log::log_wrapper::categorize_t::DEFAULT)
                        ->log(caller, "Etcd cluster %p http request %p to %s => Verbose: %s", req.get_priv_data(), &req, req.get_url().c_str(), verbose_type);
                    WDTLOGGETCAT(util::log::log_wrapper::categorize_t::DEFAULT)->write_log(caller, data, size);
                }

                return 0;
            }
        } // namespace details

        etcd_cluster::etcd_cluster() : flags_(0) {
            conf_.http_cmd_timeout              = std::chrono::seconds(10);
            conf_.etcd_members_next_update_time = std::chrono::system_clock::from_time_t(0);
            conf_.etcd_members_update_interval  = std::chrono::minutes(5);
            conf_.etcd_members_retry_interval   = std::chrono::minutes(1);

            conf_.lease                      = 0;
            conf_.keepalive_next_update_time = std::chrono::system_clock::from_time_t(0);
            conf_.keepalive_timeout          = std::chrono::seconds(16);
            conf_.keepalive_interval         = std::chrono::seconds(5);

            conf_.ssl_enable_alpn = true;
            conf_.ssl_verify_peer = false;

            conf_.ssl_min_version = ssl_version_t::DISABLED;
            conf_.ssl_client_cert.clear();
            conf_.ssl_client_cert_type.clear();
            conf_.ssl_client_key.clear();
            conf_.ssl_client_key_passwd.clear();
            conf_.ssl_ca_cert.clear();
            conf_.ssl_cipher_list.clear();
            conf_.ssl_cipher_list_tls13.clear();

            memset(&stats_, 0, sizeof(stats_));
        }

        etcd_cluster::~etcd_cluster() { reset(); }

        void etcd_cluster::init(const util::network::http_request::curl_m_bind_ptr_t &curl_mgr) {
            curl_multi_ = curl_mgr;
            random_generator_.init_seed(static_cast<util::random::mt19937::result_type>(util::time::time_utility::get_now()));

            set_flag(flag_t::CLOSING, false);
        }

        util::network::http_request::ptr_t etcd_cluster::close(bool wait) {
            set_flag(flag_t::CLOSING, true);

            if (rpc_keepalive_) {
                rpc_keepalive_->set_on_complete(NULL);
                rpc_keepalive_->stop();
                rpc_keepalive_.reset();
            }

            if (rpc_update_members_) {
                rpc_update_members_->set_on_complete(NULL);
                rpc_update_members_->stop();
                rpc_update_members_.reset();
            }

            if (rpc_authenticate_) {
                rpc_authenticate_->set_on_complete(NULL);
                rpc_authenticate_->stop();
                rpc_authenticate_.reset();
            }

            for (size_t i = 0; i < keepalive_actors_.size(); ++i) {
                if (keepalive_actors_[i]) {
                    keepalive_actors_[i]->close();
                }
            }
            keepalive_actors_.clear();

            for (size_t i = 0; i < watcher_actors_.size(); ++i) {
                if (watcher_actors_[i]) {
                    watcher_actors_[i]->close();
                }
            }
            watcher_actors_.clear();

            util::network::http_request::ptr_t ret;
            if (curl_multi_) {
                if (0 != conf_.lease) {
                    ret = create_request_lease_revoke();

                    // wait to delete content
                    if (ret) {
                        WLOGDEBUG("Etcd start to revoke lease %lld", static_cast<long long>(get_lease()));
                        ret->start(util::network::http_request::method_t::EN_MT_POST, wait);
                    }

                    conf_.lease = 0;
                }
            }

            if (ret && false == ret->is_running()) {
                ret.reset();
            }

            return ret;
        }

        void etcd_cluster::reset() {
            close(true);

            curl_multi_.reset();
            flags_ = 0;

            conf_.http_cmd_timeout = std::chrono::seconds(10);

            conf_.authorization_header.clear();
            conf_.path_node.clear();
            conf_.etcd_members_next_update_time = std::chrono::system_clock::from_time_t(0);
            conf_.etcd_members_update_interval  = std::chrono::minutes(5);
            conf_.etcd_members_retry_interval   = std::chrono::minutes(1);

            conf_.lease                      = 0;
            conf_.keepalive_next_update_time = std::chrono::system_clock::from_time_t(0);
            conf_.keepalive_timeout          = std::chrono::seconds(16);
            conf_.keepalive_interval         = std::chrono::seconds(5);

            conf_.ssl_enable_alpn = true;
            conf_.ssl_verify_peer = false;

            conf_.ssl_min_version = ssl_version_t::DISABLED;
            conf_.ssl_client_cert.clear();
            conf_.ssl_client_cert_type.clear();
            conf_.ssl_client_key.clear();
            conf_.ssl_client_key_passwd.clear();
            conf_.ssl_ca_cert.clear();
            conf_.ssl_cipher_list.clear();
            conf_.ssl_cipher_list_tls13.clear();
        }

        int etcd_cluster::tick() {
            int ret = 0;

            if (!curl_multi_) {
                return ret;
            }

            if (check_flag(flag_t::CLOSING)) {
                return 0;
            }

            // update members
            if (util::time::time_utility::now() > conf_.etcd_members_next_update_time) {
                ret += create_request_member_update() ? 1 : 0;
            }

            // empty other actions will be delayed
            if (conf_.path_node.empty()) {
                return ret;
            }

            // check or start authorization
            if (!check_authorization()) {
                if (!rpc_authenticate_) {
                    ret += create_request_auth_authenticate() ? 1 : 0;
                }

                return ret;
            }

            // keepalive lease
            if (check_flag(flag_t::ENABLE_LEASE)) {
                if (0 == get_lease()) {
                    ret += create_request_lease_grant() ? 1 : 0;
                } else if (util::time::time_utility::now() > conf_.keepalive_next_update_time) {
                    ret += create_request_lease_keepalive() ? 1 : 0;
                }
            } else if (!check_flag(flag_t::RUNNING)) {
                set_flag(flag_t::RUNNING, true);
            }

            // reactive watcher
            for (size_t i = 0; i < watcher_actors_.size(); ++i) {
                if (watcher_actors_[i]) {
                    watcher_actors_[i]->active();
                }
            }

            return ret;
        }

        bool etcd_cluster::is_available() const {
            if (!curl_multi_) {
                return false;
            }

            if (check_flag(flag_t::CLOSING)) {
                return false;
            }

            // empty other actions will be delayed
            if (conf_.path_node.empty()) {
                return false;
            }

            // check or start authorization
            return check_authorization();
        }

        void etcd_cluster::set_flag(flag_t::type f, bool v) {
            assert(0 == (f & (f - 1)));
            if (v == check_flag(f)) {
                return;
            }

            if (v) {
                flags_ |= f;
            } else {
                flags_ &= ~f;
            }

            switch (f) {
            case flag_t::ENABLE_LEASE: {
                if (v) {
                    create_request_lease_grant();
                } else if (rpc_keepalive_) {
                    rpc_keepalive_->set_on_complete(NULL);
                    rpc_keepalive_->stop();
                    rpc_keepalive_.reset();
                }
                break;
            }
            default: { break; }
            }
        }

        time_t etcd_cluster::get_http_timeout_ms() const {
            time_t ret = static_cast<time_t>(std::chrono::duration_cast<std::chrono::milliseconds>(get_conf_http_timeout()).count());
            if (ret <= 0) {
                ret = 30000; // 30s
            }

            return ret;
        }

        bool etcd_cluster::add_keepalive(const std::shared_ptr<etcd_keepalive> &keepalive) {
            if (!keepalive) {
                return false;
            }

            if (check_flag(flag_t::CLOSING)) {
                return false;
            }

            if (keepalive_actors_.end() != std::find(keepalive_actors_.begin(), keepalive_actors_.end(), keepalive)) {
                return false;
            }

            if (this != &keepalive->get_owner()) {
                return false;
            }

            set_flag(flag_t::ENABLE_LEASE, true);
            keepalive_actors_.push_back(keepalive);

            // auto active if cluster is running
            if (check_flag(flag_t::RUNNING)) {
                keepalive->active();
            }
            return true;
        }

        bool etcd_cluster::add_retry_keepalive(const std::shared_ptr<etcd_keepalive> &keepalive) {
            if (!keepalive) {
                return false;
            }

            if (check_flag(flag_t::CLOSING)) {
                return false;
            }

            if (keepalive_retry_actors_.end() != std::find(keepalive_retry_actors_.begin(), keepalive_retry_actors_.end(), keepalive)) {
                return false;
            }

            if (this != &keepalive->get_owner()) {
                return false;
            }

            set_flag(flag_t::ENABLE_LEASE, true);
            keepalive_retry_actors_.push_back(keepalive);
            return true;
        }

        bool etcd_cluster::remove_keepalive(std::shared_ptr<etcd_keepalive> keepalive) {
            if (!keepalive) {
                return false;
            }

            bool has_data = false;
            for (size_t i = 0; i < keepalive_actors_.size(); ++i) {
                if (keepalive_actors_[i] == keepalive) {
                    if (i != keepalive_actors_.size() - 1) {
                        keepalive_actors_[i].swap(keepalive_actors_[keepalive_actors_.size() - 1]);
                    }

                    keepalive_actors_.pop_back();
                    has_data = true;
                    break;
                }
            }

            for (size_t i = 0; i < keepalive_retry_actors_.size(); ++i) {
                if (keepalive_retry_actors_[i] == keepalive) {
                    if (i != keepalive_retry_actors_.size() - 1) {
                        keepalive_retry_actors_[i].swap(keepalive_retry_actors_[keepalive_retry_actors_.size() - 1]);
                    }

                    keepalive_retry_actors_.pop_back();
                    has_data = true;
                    break;
                }
            }

            if (has_data) {
                keepalive->close();
            }

            return has_data;
        }

        bool etcd_cluster::add_watcher(const std::shared_ptr<etcd_watcher> &watcher) {
            if (!watcher) {
                return false;
            }

            if (check_flag(flag_t::CLOSING)) {
                return false;
            }

            if (watcher_actors_.end() != std::find(watcher_actors_.begin(), watcher_actors_.end(), watcher)) {
                return false;
            }

            if (this != &watcher->get_owner()) {
                return false;
            }

            watcher_actors_.push_back(watcher);
            return true;
        }

        bool etcd_cluster::remove_watcher(std::shared_ptr<etcd_watcher> watcher) {
            if (!watcher) {
                return false;
            }

            bool has_data = false;
            for (size_t i = 0; i < watcher_actors_.size(); ++i) {
                if (watcher_actors_[i] == watcher) {
                    if (i != watcher_actors_.size() - 1) {
                        watcher_actors_[i].swap(watcher_actors_[watcher_actors_.size() - 1]);
                    }

                    watcher_actors_.pop_back();
                    has_data = true;
                    break;
                }
            }

            if (has_data) {
                watcher->close();
            }

            return has_data;
        }

        void etcd_cluster::set_lease(int64_t v, bool force_active_keepalives) {
            int64_t old_v = get_lease();
            conf_.lease   = v;

            if (old_v == v && false == force_active_keepalives) {
                // 仅重试失败项目
                for (size_t i = 0; i < keepalive_retry_actors_.size(); ++i) {
                    if (keepalive_retry_actors_[i]) {
                        keepalive_retry_actors_[i]->reset_value_changed();
                        keepalive_retry_actors_[i]->active();
                    }
                }

                keepalive_retry_actors_.clear();
                return;
            }

            if (0 != v) {
                // all keepalive object start a update/set request
                for (size_t i = 0; i < keepalive_actors_.size(); ++i) {
                    if (keepalive_actors_[i]) {
                        keepalive_actors_[i]->reset_value_changed();
                        keepalive_actors_[i]->active();
                    }
                }
            }

            keepalive_retry_actors_.clear();
        }

        bool etcd_cluster::create_request_auth_authenticate() {
            if (!curl_multi_) {
                return false;
            }

            if (check_flag(flag_t::CLOSING)) {
                return false;
            }

            if (conf_.path_node.empty()) {
                return false;
            }

            if (conf_.authorization.empty()) {
                conf_.authorization_header.clear();
                return false;
            }

            if (rpc_authenticate_) {
                return false;
            }

            std::stringstream ss;
            ss << conf_.path_node << ETCD_API_V3_AUTH_AUTHENTICATE;
            util::network::http_request::ptr_t req = util::network::http_request::create(curl_multi_.get(), ss.str());

            std::string::size_type username_sep = conf_.authorization.find(':');
            std::string            username, password;
            if (username_sep == std::string::npos) {
                username = conf_.authorization;
            } else {
                username = conf_.authorization.substr(0, username_sep);
                if (username_sep + 1 < conf_.authorization.size()) {
                    password = conf_.authorization.substr(username_sep + 1);
                }
            }

            if (req) {
                add_stats_create_request();

                rapidjson::Document doc;
                doc.SetObject();
                doc.AddMember("name", rapidjson::StringRef(username.c_str(), username.size()), doc.GetAllocator());
                doc.AddMember("password", rapidjson::StringRef(password.c_str(), password.size()), doc.GetAllocator());

                setup_http_request(req, doc, get_http_timeout_ms());
                req->set_priv_data(this);
                req->set_on_complete(libcurl_callback_on_auth_authenticate);

                // req->set_on_verbose(details::etcd_cluster_verbose_callback);
                int res = req->start(util::network::http_request::method_t::EN_MT_POST, false);
                if (res != 0) {
                    req->set_on_complete(NULL);
                    WLOGERROR("Etcd start authenticate request for user %s to %s failed, res: %d", username.c_str(), req->get_url().c_str(), res);
                    add_stats_error_request();
                    return false;
                }

                WLOGINFO("Etcd start authenticate request for user %s to %s", username.c_str(), req->get_url().c_str());
                rpc_authenticate_ = req;
            } else {
                add_stats_error_request();
            }

            return !!rpc_authenticate_;
        }

        int etcd_cluster::libcurl_callback_on_auth_authenticate(util::network::http_request &req) {
            etcd_cluster *self = reinterpret_cast<etcd_cluster *>(req.get_priv_data());
            if (NULL == self) {
                WLOGERROR("Etcd authenticate shouldn't has request without private data");
                return 0;
            }

            util::network::http_request::ptr_t keep_rpc = self->rpc_authenticate_;
            self->rpc_authenticate_.reset();

            // 服务器错误则忽略
            if (0 != req.get_error_code() ||
                util::network::http_request::status_code_t::EN_ECG_SUCCESS != util::network::http_request::get_status_code_group(req.get_response_code())) {

                // only network error will trigger a etcd member update
                if (0 != req.get_error_code()) {
                    self->retry_request_member_update();
                }
                self->add_stats_error_request();

                WLOGERROR("Etcd authenticate failed, error code: %d, http code: %d\n%s", req.get_error_code(), req.get_response_code(), req.get_error_msg());
                self->check_authorization_expired(req.get_response_code(), req.get_response_stream().str());
                return 0;
            }

            std::string http_content;
            req.get_response_stream().str().swap(http_content);
            WLOGTRACE("Etcd cluster got http response: %s", http_content.c_str());

            do {
                // 如果lease不存在（没有TTL）则启动创建流程
                // 忽略空数据
                rapidjson::Document doc;
                if (false == ::atframe::component::etcd_packer::parse_object(doc, http_content.c_str())) {
                    break;
                }

                rapidjson::Value root = doc.GetObject();
                std::string      token;
                if (false == etcd_packer::unpack_string(root, "token", token)) {
                    WLOGERROR("Etcd authenticate failed, token not found.(%s)", http_content.c_str());
                    self->add_stats_error_request();
                    return 0;
                }

                self->conf_.authorization_header = "Authorization: " + token;
                WLOGDEBUG("Etcd cluster got authenticate token: %s", token.c_str());

                self->add_stats_success_request();
            } while (false);

            return 0;
        }

        bool etcd_cluster::retry_request_member_update() {
            if (util::time::time_utility::now() + conf_.etcd_members_retry_interval < conf_.etcd_members_next_update_time) {
                conf_.etcd_members_next_update_time = util::time::time_utility::now() + conf_.etcd_members_retry_interval;
                return false;
            }

            if (util::time::time_utility::now() <= conf_.etcd_members_next_update_time) {
                return false;
            }

            return create_request_member_update();
        }

        bool etcd_cluster::create_request_member_update() {
            if (!curl_multi_) {
                return false;
            }

            if (check_flag(flag_t::CLOSING)) {
                return false;
            }

            if (rpc_update_members_) {
                return false;
            }

            if (conf_.conf_hosts.empty() && conf_.hosts.empty()) {
                return false;
            }

            if (std::chrono::system_clock::duration::zero() >= conf_.etcd_members_update_interval) {
                conf_.etcd_members_next_update_time = util::time::time_utility::now() + std::chrono::seconds(1);
            } else {
                conf_.etcd_members_next_update_time = util::time::time_utility::now() + conf_.etcd_members_update_interval;
            }

            std::string *selected_host;
            if (!conf_.hosts.empty()) {
                selected_host = &conf_.hosts[random_generator_.random_between<size_t>(0, conf_.hosts.size())];
            } else {
                selected_host = &conf_.conf_hosts[random_generator_.random_between<size_t>(0, conf_.conf_hosts.size())];
            }

            std::stringstream ss;
            ss << (*selected_host) << ETCD_API_V3_MEMBER_LIST;
            util::network::http_request::ptr_t req = util::network::http_request::create(curl_multi_.get(), ss.str());

            if (req) {
                add_stats_create_request();

                rapidjson::Document doc;
                doc.SetObject();

                setup_http_request(req, doc, get_http_timeout_ms());
                req->set_priv_data(this);
                req->set_on_complete(libcurl_callback_on_member_update);

                int res = req->start(util::network::http_request::method_t::EN_MT_POST, false);
                if (res != 0) {
                    req->set_on_complete(NULL);
                    WLOGERROR("Etcd start update member %lld request to %s failed, res: %d", static_cast<long long>(get_lease()), req->get_url().c_str(), res);

                    add_stats_error_request();
                    return false;
                }

                WLOGTRACE("Etcd start update member %lld request to %s", static_cast<long long>(get_lease()), req->get_url().c_str());
                rpc_update_members_ = req;
            } else {
                add_stats_error_request();
            }

            return true;
        }

        int etcd_cluster::libcurl_callback_on_member_update(util::network::http_request &req) {
            etcd_cluster *self = reinterpret_cast<etcd_cluster *>(req.get_priv_data());
            if (NULL == self) {
                WLOGERROR("Etcd member list shouldn't has request without private data");
                return 0;
            }

            util::network::http_request::ptr_t keep_rpc = self->rpc_update_members_;
            self->rpc_update_members_.reset();

            // 服务器错误则忽略
            if (0 != req.get_error_code() ||
                util::network::http_request::status_code_t::EN_ECG_SUCCESS != util::network::http_request::get_status_code_group(req.get_response_code())) {

                // only network error will trigger a etcd member update
                if (0 != req.get_error_code()) {
                    self->retry_request_member_update();
                }
                WLOGERROR("Etcd member list failed, error code: %d, http code: %d\n%s", req.get_error_code(), req.get_response_code(), req.get_error_msg());
                self->add_stats_error_request();

                // 出错则从host里移除无效数据
                for (size_t i = 0; i < self->conf_.hosts.size(); ++i) {
                    if (0 == UTIL_STRFUNC_STRNCASE_CMP(self->conf_.hosts[i].c_str(), req.get_url().c_str(), self->conf_.hosts[i].size())) {
                        if (i != self->conf_.hosts.size() - 1) {
                            self->conf_.hosts[self->conf_.hosts.size() - 1].swap(self->conf_.hosts[i]);
                        }

                        self->conf_.hosts.pop_back();
                        break;
                    }
                }
                return 0;
            }

            std::string http_content;
            req.get_response_stream().str().swap(http_content);
            WLOGTRACE("Etcd cluster got http response: %s", http_content.c_str());

            do {
                // ignore empty data
                rapidjson::Document doc;
                if (false == ::atframe::component::etcd_packer::parse_object(doc, http_content.c_str())) {
                    break;
                }

                rapidjson::Value                    root    = doc.GetObject();
                rapidjson::Document::MemberIterator members = root.FindMember("members");
                if (root.MemberEnd() == members) {
                    WLOGERROR("Etcd members not found");
                    self->add_stats_error_request();
                    return 0;
                }

                self->conf_.hosts.clear();
                bool                       need_select_node = true;
                rapidjson::Document::Array all_members      = members->value.GetArray();
                for (rapidjson::Document::Array::ValueIterator iter = all_members.Begin(); iter != all_members.End(); ++iter) {
                    rapidjson::Document::MemberIterator client_urls = iter->FindMember("clientURLs");
                    if (client_urls == iter->MemberEnd()) {
                        continue;
                    }

                    rapidjson::Document::Array all_client_urls = client_urls->value.GetArray();
                    for (rapidjson::Document::Array::ValueIterator cli_url_iter = all_client_urls.Begin(); cli_url_iter != all_client_urls.End();
                         ++cli_url_iter) {
                        if (cli_url_iter->GetStringLength() > 0) {
                            self->conf_.hosts.push_back(cli_url_iter->GetString());

                            if (self->conf_.path_node == self->conf_.hosts.back()) {
                                need_select_node = false;
                            }
                        }
                    }
                }

                if (!self->conf_.hosts.empty() && need_select_node) {
                    self->conf_.path_node = self->conf_.hosts[self->random_generator_.random_between<size_t>(0, self->conf_.hosts.size())];
                    WLOGINFO("Etcd cluster using node %s", self->conf_.path_node.c_str());
                }

                self->add_stats_success_request();

                // 触发一次tick
                self->tick();
            } while (false);

            return 0;
        }

        bool etcd_cluster::create_request_lease_grant() {
            if (!curl_multi_ || conf_.path_node.empty()) {
                return false;
            }

            if (check_flag(flag_t::CLOSING) || !check_flag(flag_t::ENABLE_LEASE)) {
                return false;
            }

            if (rpc_keepalive_) {
                return false;
            }

            if (std::chrono::system_clock::duration::zero() >= conf_.keepalive_interval) {
                conf_.keepalive_next_update_time = util::time::time_utility::now() + std::chrono::seconds(1);
            } else {
                conf_.keepalive_next_update_time = util::time::time_utility::now() + conf_.keepalive_interval;
            }

            std::stringstream ss;
            ss << conf_.path_node << ETCD_API_V3_LEASE_GRANT;
            util::network::http_request::ptr_t req = util::network::http_request::create(curl_multi_.get(), ss.str());

            if (req) {
                add_stats_create_request();

                rapidjson::Document doc;
                doc.SetObject();
                doc.AddMember("ID", get_lease(), doc.GetAllocator());
                doc.AddMember("TTL", static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(conf_.keepalive_timeout).count()),
                              doc.GetAllocator());

                setup_http_request(req, doc, get_http_timeout_ms());
                req->set_priv_data(this);
                req->set_on_complete(libcurl_callback_on_lease_keepalive);

                // req->set_on_verbose(details::etcd_cluster_verbose_callback);
                int res = req->start(util::network::http_request::method_t::EN_MT_POST, false);
                if (res != 0) {
                    req->set_on_complete(NULL);
                    WLOGERROR("Etcd start keepalive lease %lld request to %s failed, res: %d", static_cast<long long>(get_lease()), req->get_url().c_str(),
                              res);
                    add_stats_error_request();
                    return false;
                }

                WLOGDEBUG("Etcd start keepalive lease %lld request to %s", static_cast<long long>(get_lease()), req->get_url().c_str());
                rpc_keepalive_ = req;
            } else {
                add_stats_error_request();
            }

            return true;
        }

        bool etcd_cluster::create_request_lease_keepalive() {
            if (!curl_multi_ || 0 == get_lease() || conf_.path_node.empty()) {
                return false;
            }

            if (check_flag(flag_t::CLOSING) || !check_flag(flag_t::ENABLE_LEASE)) {
                return false;
            }

            if (rpc_keepalive_) {
                return false;
            }

            if (util::time::time_utility::now() <= conf_.keepalive_next_update_time) {
                return false;
            }

            if (std::chrono::system_clock::duration::zero() >= conf_.keepalive_interval) {
                conf_.keepalive_next_update_time = util::time::time_utility::now() + std::chrono::seconds(1);
            } else {
                conf_.keepalive_next_update_time = util::time::time_utility::now() + conf_.keepalive_interval;
            }

            std::stringstream ss;
            ss << conf_.path_node << ETCD_API_V3_LEASE_KEEPALIVE;
            util::network::http_request::ptr_t req = util::network::http_request::create(curl_multi_.get(), ss.str());

            if (req) {
                add_stats_create_request();

                rapidjson::Document doc;
                doc.SetObject();
                doc.AddMember("ID", get_lease(), doc.GetAllocator());

                setup_http_request(req, doc, get_http_timeout_ms());
                req->set_priv_data(this);
                req->set_on_complete(libcurl_callback_on_lease_keepalive);

                int res = req->start(util::network::http_request::method_t::EN_MT_POST, false);
                if (res != 0) {
                    req->set_on_complete(NULL);
                    WLOGERROR("Etcd start keepalive lease %lld request to %s failed, res: %d", static_cast<long long>(get_lease()), req->get_url().c_str(),
                              res);
                    add_stats_error_request();
                    return false;
                }

                WLOGTRACE("Etcd start keepalive lease %lld request to %s", static_cast<long long>(get_lease()), req->get_url().c_str());
                rpc_keepalive_ = req;
            } else {
                add_stats_error_request();
            }

            return true;
        }

        int etcd_cluster::libcurl_callback_on_lease_keepalive(util::network::http_request &req) {
            etcd_cluster *self = reinterpret_cast<etcd_cluster *>(req.get_priv_data());
            if (NULL == self) {
                WLOGERROR("Etcd lease keepalive shouldn't has request without private data");
                return 0;
            }

            util::network::http_request::ptr_t keep_rpc = self->rpc_keepalive_;
            self->rpc_keepalive_.reset();

            // 服务器错误则忽略
            if (0 != req.get_error_code() ||
                util::network::http_request::status_code_t::EN_ECG_SUCCESS != util::network::http_request::get_status_code_group(req.get_response_code())) {

                // only network error will trigger a etcd member update
                if (0 != req.get_error_code()) {
                    self->retry_request_member_update();
                }
                self->add_stats_error_request();

                WLOGERROR("Etcd lease keepalive failed, error code: %d, http code: %d\n%s", req.get_error_code(), req.get_response_code(), req.get_error_msg());
                self->check_authorization_expired(req.get_response_code(), req.get_response_stream().str());
                return 0;
            }

            std::string http_content;
            req.get_response_stream().str().swap(http_content);
            WLOGTRACE("Etcd cluster got http response: %s", http_content.c_str());

            do {
                // 如果lease不存在（没有TTL）则启动创建流程
                // 忽略空数据
                rapidjson::Document doc;
                if (false == ::atframe::component::etcd_packer::parse_object(doc, http_content.c_str())) {
                    break;
                }

                bool                             is_grant = false;
                rapidjson::Value                 root     = doc.GetObject();
                rapidjson::Value::MemberIterator result   = root.FindMember("result");
                if (result == root.MemberEnd()) {
                    is_grant = true;
                } else {
                    root = result->value;
                }


                if (root.MemberEnd() == root.FindMember("TTL")) {
                    if (is_grant) {
                        WLOGERROR("Etcd lease grant failed");
                    } else {
                        WLOGERROR("Etcd lease keepalive failed because not found, try to grant one");
                        self->create_request_lease_grant();
                    }

                    self->add_stats_error_request();
                    return 0;
                }

                // 更新lease
                int64_t new_lease = 0;
                etcd_packer::unpack_int(root, "ID", new_lease);

                if (0 == new_lease) {
                    WLOGERROR("Etcd cluster got a error http response for grant or keepalive lease: %s", http_content.c_str());
                    self->add_stats_error_request();
                    break;
                }

                if (is_grant) {
                    WLOGDEBUG("Etcd lease %lld granted", static_cast<long long>(new_lease));
                } else {
                    WLOGDEBUG("Etcd lease %lld keepalive successed", static_cast<long long>(new_lease));
                }

                self->add_stats_success_request();
                if (!self->check_flag(flag_t::RUNNING)) {
                    self->set_flag(flag_t::RUNNING, true);
                }

                // 这里会触发重试失败的keepalive
                self->set_lease(new_lease, is_grant);
            } while (false);

            return 0;
        }

        util::network::http_request::ptr_t etcd_cluster::create_request_lease_revoke() {
            if (!curl_multi_ || 0 == get_lease() || conf_.path_node.empty()) {
                return util::network::http_request::ptr_t();
            }

            std::stringstream ss;
            ss << conf_.path_node << ETCD_API_V3_LEASE_REVOKE;
            util::network::http_request::ptr_t ret = util::network::http_request::create(curl_multi_.get(), ss.str());

            if (ret) {
                add_stats_create_request();

                rapidjson::Document doc;
                doc.SetObject();
                doc.AddMember("ID", get_lease(), doc.GetAllocator());

                setup_http_request(ret, doc, get_http_timeout_ms());
            } else {
                add_stats_error_request();
            }

            return ret;
        }

        util::network::http_request::ptr_t etcd_cluster::create_request_kv_get(const std::string &key, const std::string &range_end, int64_t limit,
                                                                               int64_t revision) {
            if (!curl_multi_ || conf_.path_node.empty() || check_flag(flag_t::CLOSING)) {
                return util::network::http_request::ptr_t();
            }

            std::stringstream ss;
            ss << conf_.path_node << ETCD_API_V3_KV_GET;
            util::network::http_request::ptr_t ret = util::network::http_request::create(curl_multi_.get(), ss.str());

            if (ret) {
                add_stats_create_request();

                rapidjson::Document doc;
                rapidjson::Value &  root = doc.SetObject();

                etcd_packer::pack_key_range(root, key, range_end, doc);
                doc.AddMember("limit", limit, doc.GetAllocator());
                doc.AddMember("revision", revision, doc.GetAllocator());

                setup_http_request(ret, doc, get_http_timeout_ms());
            } else {
                add_stats_error_request();
            }

            return ret;
        }

        util::network::http_request::ptr_t etcd_cluster::create_request_kv_set(const std::string &key, const std::string &value, bool assign_lease,
                                                                               bool prev_kv, bool ignore_value, bool ignore_lease) {
            if (!curl_multi_ || conf_.path_node.empty() || check_flag(flag_t::CLOSING)) {
                return util::network::http_request::ptr_t();
            }

            if (assign_lease && 0 == get_lease()) {
                return util::network::http_request::ptr_t();
            }

            std::stringstream ss;
            ss << conf_.path_node << ETCD_API_V3_KV_SET;
            util::network::http_request::ptr_t ret = util::network::http_request::create(curl_multi_.get(), ss.str());

            if (ret) {
                add_stats_create_request();

                rapidjson::Document doc;
                rapidjson::Value &  root = doc.SetObject();

                etcd_packer::pack_base64(root, "key", key, doc);
                etcd_packer::pack_base64(root, "value", value, doc);
                if (assign_lease) {
                    doc.AddMember("lease", get_lease(), doc.GetAllocator());
                }

                doc.AddMember("prev_kv", prev_kv, doc.GetAllocator());
                doc.AddMember("ignore_value", ignore_value, doc.GetAllocator());
                doc.AddMember("ignore_lease", ignore_lease, doc.GetAllocator());

                setup_http_request(ret, doc, get_http_timeout_ms());
            } else {
                add_stats_error_request();
            }

            return ret;
        }

        util::network::http_request::ptr_t etcd_cluster::create_request_kv_del(const std::string &key, const std::string &range_end, bool prev_kv) {
            if (!curl_multi_ || conf_.path_node.empty() || check_flag(flag_t::CLOSING)) {
                return util::network::http_request::ptr_t();
            }

            std::stringstream ss;
            ss << conf_.path_node << ETCD_API_V3_KV_DELETE;
            util::network::http_request::ptr_t ret = util::network::http_request::create(curl_multi_.get(), ss.str());

            if (ret) {
                add_stats_create_request();

                rapidjson::Document doc;
                rapidjson::Value &  root = doc.SetObject();

                etcd_packer::pack_key_range(root, key, range_end, doc);
                doc.AddMember("prev_kv", prev_kv, doc.GetAllocator());

                setup_http_request(ret, doc, get_http_timeout_ms());
            } else {
                add_stats_error_request();
            }

            return ret;
        }

        util::network::http_request::ptr_t etcd_cluster::create_request_watch(const std::string &key, const std::string &range_end, int64_t start_revision,
                                                                              bool prev_kv, bool progress_notify) {
            if (!curl_multi_ || conf_.path_node.empty() || check_flag(flag_t::CLOSING)) {
                return util::network::http_request::ptr_t();
            }

            std::stringstream ss;
            ss << conf_.path_node << ETCD_API_V3_WATCH;
            util::network::http_request::ptr_t ret = util::network::http_request::create(curl_multi_.get(), ss.str());

            if (ret) {
                add_stats_create_request();

                rapidjson::Document doc;
                rapidjson::Value &  root = doc.SetObject();

                rapidjson::Value create_request(rapidjson::kObjectType);


                etcd_packer::pack_key_range(create_request, key, range_end, doc);
                if (prev_kv) {
                    create_request.AddMember("prev_kv", prev_kv, doc.GetAllocator());
                }

                if (progress_notify) {
                    create_request.AddMember("progress_notify", progress_notify, doc.GetAllocator());
                }

                if (0 != start_revision) {
                    create_request.AddMember("start_revision", start_revision, doc.GetAllocator());
                }

                root.AddMember("create_request", create_request, doc.GetAllocator());

                setup_http_request(ret, doc, get_http_timeout_ms());
                ret->set_opt_keepalive(75, 150);
                // 不能共享socket
                ret->set_opt_reuse_connection(false);
            } else {
                add_stats_error_request();
            }

            return ret;
        }

        void etcd_cluster::add_stats_error_request() {
            ++stats_.sum_error_requests;
            ++stats_.continue_error_requests;
            stats_.continue_success_requests = 0;
        }

        void etcd_cluster::add_stats_success_request() {
            ++stats_.sum_success_requests;
            ++stats_.continue_success_requests;
            stats_.continue_error_requests = 0;
        }

        void etcd_cluster::add_stats_create_request() { stats_.sum_create_requests = 0; }

        bool etcd_cluster::check_authorization() const {
            if (conf_.authorization.empty()) {
                return true;
            }

            if (!conf_.authorization_header.empty()) {
                return true;
            }

            return false;
        }

        void etcd_cluster::check_authorization_expired(int http_code, const std::string &content) {
            if (ETCD_API_V3_ERROR_HTTP_CODE_AUTH == http_code) {
                conf_.authorization_header.clear();
                return;
            }

            rapidjson::Document doc;
            if (::atframe::component::etcd_packer::parse_object(doc, content.c_str())) {
                int64_t error_code = 0;
                ::atframe::component::etcd_packer::unpack_int(doc, "code", error_code);
                if (ETCD_API_V3_ERROR_GRPC_CODE_UNAUTHENTICATED == error_code) {
                    conf_.authorization_header.clear();
                    return;
                }
            }

            if (ETCD_API_V3_ERROR_HTTP_INVALID_PARAM == http_code || ETCD_API_V3_ERROR_HTTP_PRECONDITION == http_code) {
                if (std::string::npos != content.find("authenticat")) {
                    conf_.authorization_header.clear();
                }
            }
        }

        void etcd_cluster::setup_http_request(util::network::http_request::ptr_t &req, rapidjson::Document &doc, time_t timeout) {
            if (!req) {
                return;
            }

            if (timeout <= 0) {
                timeout = get_http_timeout_ms();
            }

            req->set_opt_follow_location(true);
            req->set_opt_ssl_verify_peer(conf_.ssl_verify_peer);
            req->set_opt_accept_encoding("");
            req->set_opt_http_content_decoding(true);
            req->set_opt_timeout(timeout);
            req->set_user_agent(details::get_default_user_agent());
            // req->set_opt_reuse_connection(false); // just enable connection reuse for all but watch request
            req->set_opt_long(CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
            req->set_opt_no_signal(true);
            if (!conf_.authorization_header.empty()) {
                req->append_http_header(conf_.authorization_header.c_str());
            }

            // Setup ssl
            if (ssl_version_t::DISABLED != conf_.ssl_min_version) {
#if LIBCURL_VERSION_NUM >= 0x072400
                req->set_opt_bool(CURLOPT_SSL_ENABLE_ALPN, conf_.ssl_enable_alpn);
#endif
                switch (conf_.ssl_min_version) {
#if LIBCURL_VERSION_NUM >= 0x073400
                case ssl_version_t::TLS_V13:
                    req->set_opt_long(CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_3);
                    break;
#endif
#if LIBCURL_VERSION_NUM >= 0x072200
                case ssl_version_t::TLS_V12:
                    req->set_opt_long(CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
                    break;
                case ssl_version_t::TLS_V11:
                    req->set_opt_long(CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_1);
                    break;
                case ssl_version_t::TLS_V10:
                    req->set_opt_long(CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1);
                    break;
#endif
                case ssl_version_t::SSL3:
                    req->set_opt_long(CURLOPT_SSLVERSION, CURL_SSLVERSION_SSLv3);
                    break;
                default:
#if LIBCURL_VERSION_NUM >= 0x072200
                    conf_.ssl_min_version = ssl_version_t::TLS_V12;
                    req->set_opt_long(CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
#else
                    conf_.ssl_min_version = ssl_version_t::SSL3;
                    req->set_opt_long(CURLOPT_SSLVERSION, CURL_SSLVERSION_SSLv3);
#endif
                    break;
                }
                req->set_opt_bool(CURLOPT_SSL_ENABLE_ALPN, conf_.ssl_enable_alpn);
                if (!conf_.ssl_client_cert.empty()) {
                    req->set_opt_string(CURLOPT_SSLCERT, &conf_.ssl_client_cert[0]);
                }
#if LIBCURL_VERSION_NUM >= 0x070903
                if (!conf_.ssl_client_cert_type.empty()) {
                    req->set_opt_string(CURLOPT_SSLCERTTYPE, &conf_.ssl_client_cert_type[0]);
                }
#endif
                if (!conf_.ssl_client_key.empty()) {
                    req->set_opt_string(CURLOPT_SSLKEY, &conf_.ssl_client_key[0]);
                }
#if LIBCURL_VERSION_NUM >= 0x071004
                if (!conf_.ssl_client_key_passwd.empty()) {
                    req->set_opt_string(CURLOPT_SSLKEYPASSWD, &conf_.ssl_client_key_passwd[0]);
                }
#elif LIBCURL_VERSION_NUM >= 0x070902
                if (!conf_.ssl_client_key_passwd.empty()) {
                    req->set_opt_string(CURLOPT_SSLCERTPASSWD, &conf_.ssl_client_key_passwd[0]);
                }
#endif
                if (!conf_.ssl_ca_cert.empty()) {
                    req->set_opt_string(CURLOPT_CAINFO, &conf_.ssl_ca_cert[0]);
                }

                if (!conf_.ssl_cipher_list.empty()) {
                    req->set_opt_string(CURLOPT_SSL_CIPHER_LIST, &conf_.ssl_cipher_list[0]);
                } else if (ssl_version_t::TLS_V12 == conf_.ssl_min_version || ssl_version_t::TLS_V13 == conf_.ssl_min_version) {
                    char ciphers[] = "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:"
                                     "ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:DHE-RSA-AES128-GCM-SHA256:DHE-RSA-AES256-GCM-SHA384";
                    req->set_opt_string(CURLOPT_SSL_CIPHER_LIST, ciphers);
                }

#if LIBCURL_VERSION_NUM >= 0x073d00
                if (!conf_.ssl_cipher_list_tls13.empty()) {
                    req->set_opt_string(CURLOPT_TLS13_CIPHERS, &conf_.ssl_cipher_list_tls13[0]);
                } else if (ssl_version_t::TLS_V12 == conf_.ssl_min_version) {
                    char ciphers[] = "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256";
                    req->set_opt_string(CURLOPT_TLS13_CIPHERS, ciphers);
                }
#endif
            }

            // req->set_on_verbose(details::etcd_cluster_verbose_callback);

            if (util::log::log_wrapper::check_level(WDTLOGGETCAT(util::log::log_wrapper::categorize_t::DEFAULT),
                                                    util::log::log_wrapper::level_t::LOG_LW_TRACE)) {
                req->set_on_progress(details::etcd_cluster_trace_porcess_callback);
            }

            // Stringify the DOM
            rapidjson::StringBuffer                    buffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            doc.Accept(writer);

            req->post_data().assign(buffer.GetString(), buffer.GetSize());
            WLOGTRACE("Etcd cluster setup request %p to %s, post data: %s", req.get(), req->get_url().c_str(), req->post_data().c_str());
        }

    } // namespace component
} // namespace atframe