#include <algorithm/base64.h>

#include <log/log_wrapper.h>

#include "etcd_cluster.h"
#include "etcd_keepalive.h"


namespace atframe {
    namespace component {
        namespace details {
            struct etcd_keepalive_deletor {
                std::string                        keepalive_path;
                etcd_keepalive *                   deleted_keepalive;
                util::network::http_request::ptr_t rpc;
            };

            static int etcd_keepalive_deletor_fn(util::network::http_request &req) {
                etcd_keepalive_deletor *self = reinterpret_cast<etcd_keepalive_deletor *>(req.get_priv_data());
                assert(self && self->deleted_keepalive && self->rpc);
                do {
                    if (NULL == self || !self->rpc) {
                        WLOGERROR("Etcd keepalive delete request shouldn't has request without private data");
                        break;
                    }

                    WLOGINFO("Etcd keepalive %p delete %s finished, res: %d, http code: %d\n%s", self->deleted_keepalive, self->keepalive_path.c_str(),
                             self->rpc->get_error_code(), self->rpc->get_response_code(), self->rpc->get_error_msg());
                } while (false);

                delete self;
                return 0;
            }
        } // namespace details

        etcd_keepalive::default_checker_t::default_checker_t(const std::string &checked) : data(checked) {}

        bool etcd_keepalive::default_checker_t::operator()(const std::string &checked) const { return checked.empty() || data == checked; }

        etcd_keepalive::etcd_keepalive(etcd_cluster &owner, const std::string &path, constrict_helper_t &) : owner_(&owner), path_(path) {
            checker_.is_check_run    = false;
            checker_.is_check_passed = false;
            checker_.retry_times     = 0;
            rpc_.is_actived          = false;
            rpc_.is_value_changed    = true;
            rpc_.has_data            = false;
        }

        etcd_keepalive::~etcd_keepalive() { close(); }

        etcd_keepalive::ptr_t etcd_keepalive::create(etcd_cluster &owner, const std::string &path) {
            constrict_helper_t h;
            return std::make_shared<etcd_keepalive>(owner, path, h);
        }

        void etcd_keepalive::close() {
            if (rpc_.rpc_opr_) {
                WLOGDEBUG("Etcd watcher %p cancel http request.", this);
                rpc_.rpc_opr_->set_on_complete(NULL);
                rpc_.rpc_opr_->set_priv_data(NULL);
                rpc_.rpc_opr_->stop();
                rpc_.rpc_opr_.reset();
            }

            remove_etcd_path();
            rpc_.is_actived       = false;
            rpc_.is_value_changed = true;

            checker_.is_check_run    = false;
            checker_.is_check_passed = false;
            checker_.fn              = NULL;
            checker_.retry_times     = 0;
        }

        void etcd_keepalive::set_checker(const std::string &checked_str) { checker_.fn = default_checker_t(checked_str); }

        void etcd_keepalive::set_checker(checker_fn_t fn) { checker_.fn = fn; }

        void etcd_keepalive::set_value(const std::string &str) {
            if (value_ != str) {
                value_                = str;
                rpc_.is_value_changed = true;

                active();
            }
        }

        void etcd_keepalive::reset_value_changed() { rpc_.is_value_changed = true; }

        const std::string &etcd_keepalive::get_path() const { return path_; }

        void etcd_keepalive::active() {
            rpc_.is_actived = true;
            process();
        }

        void etcd_keepalive::process() {
            if (rpc_.rpc_opr_) {
                return;
            }

            rpc_.is_actived = false;

            // if has checker and has not check date yet, send a check request
            if (!checker_.fn) {
                checker_.is_check_run    = true;
                checker_.is_check_passed = true;
                ++checker_.retry_times;
            }

            bool need_retry = false;
            do {
                if (false == checker_.is_check_run) {
                    // create a check rpc
                    rpc_.rpc_opr_ = owner_->create_request_kv_get(path_);
                    if (!rpc_.rpc_opr_) {
                        need_retry = true;
                        ++checker_.retry_times;
                        WLOGERROR("Etcd keepalive %p create get data request to %s failed", this, path_.c_str());
                        break;
                    }

                    rpc_.rpc_opr_->set_priv_data(this);
                    rpc_.rpc_opr_->set_on_complete(libcurl_callback_on_get_data);
                    break;
                }

                // if check passed, set data
                if (checker_.is_check_run && checker_.is_check_passed && rpc_.is_value_changed) {
                    // create set data rpc
                    rpc_.rpc_opr_ = owner_->create_request_kv_set(path_, value_, true);
                    if (!rpc_.rpc_opr_) {
                        need_retry = true;
                        WLOGERROR("Etcd keepalive %p create set data request to %s failed", this, path_.c_str());
                        break;
                    }

                    rpc_.is_value_changed = false;
                    rpc_.rpc_opr_->set_priv_data(this);
                    rpc_.rpc_opr_->set_on_complete(libcurl_callback_on_set_data);
                }
            } while (false);

            if (rpc_.rpc_opr_) {
                int res = rpc_.rpc_opr_->start(util::network::http_request::method_t::EN_MT_POST, false);
                if (res != 0) {
                    need_retry = true;
                    rpc_.rpc_opr_->set_priv_data(NULL);
                    rpc_.rpc_opr_->set_on_complete(NULL);
                    WLOGERROR("Etcd keepalive %p start request to %s failed, res: %d", this, rpc_.rpc_opr_->get_url().c_str(), res);
                    rpc_.rpc_opr_.reset();
                } else {
                    WLOGDEBUG("Etcd keepalive %p start request to %s success.", this, rpc_.rpc_opr_->get_url().c_str());
                }
            }

            if (need_retry) {
                owner_->add_retry_keepalive(shared_from_this());
            }
        }

        void etcd_keepalive::remove_etcd_path() {
            if (owner_ == NULL || !rpc_.has_data) {
                return;
            }

            // 会随lease的释放而释放，不需要额外删除
            if (owner_->check_flag(etcd_cluster::flag_t::CLOSING) && owner_->check_flag(etcd_cluster::flag_t::ENABLE_LEASE)) {
                return;
            }

            if (path_.empty()) {
                return;
            }

            util::network::http_request::ptr_t rpc = owner_->create_request_kv_del(path_, "+1");
            if (!rpc) {
                WLOGERROR("Etcd keepalive %p create delete data request to %s failed", this, path_.c_str());
                return;
            }

            details::etcd_keepalive_deletor *deletor = new details::etcd_keepalive_deletor();
            if (nullptr == deletor) {
                WLOGERROR("Etcd keepalive %p create etcd_keepalive_deletor request to %s failed", this, path_.c_str());
                return;
            }

            deletor->rpc               = rpc;
            deletor->deleted_keepalive = this;
            deletor->keepalive_path    = get_path();

            rpc->set_on_complete(details::etcd_keepalive_deletor_fn);
            rpc->set_priv_data(deletor);

            int res = rpc->start(util::network::http_request::method_t::EN_MT_POST, false);
            if (res != 0) {
                WLOGERROR("Etcd keepalive %p start delete request to %s failed, res: %d", this, rpc->get_url().c_str(), res);
                delete deletor;
                rpc->set_on_complete(NULL);
                rpc->set_priv_data(NULL);
            } else {
                WLOGDEBUG("Etcd keepalive %p start delete request to %s success", this, rpc->get_url().c_str());
                rpc_.has_data = false;
            }
        }

        int etcd_keepalive::libcurl_callback_on_get_data(util::network::http_request &req) {
            etcd_keepalive *self = reinterpret_cast<etcd_keepalive *>(req.get_priv_data());
            if (NULL == self) {
                WLOGERROR("Etcd keepalive get request shouldn't has request without private data");
                return 0;
            }
            util::network::http_request::ptr_t keep_rpc = self->rpc_.rpc_opr_;

            self->rpc_.rpc_opr_.reset();
            ++self->checker_.retry_times;

            // 服务器错误则重试，预检查请求的404是正常的
            if (0 != req.get_error_code() ||
                util::network::http_request::status_code_t::EN_ECG_SUCCESS != util::network::http_request::get_status_code_group(req.get_response_code())) {
                WLOGERROR("Etcd keepalive %p get request failed, error code: %d, http code: %d\n%s", self, req.get_error_code(), req.get_response_code(),
                          req.get_error_msg());

                self->owner_->add_retry_keepalive(self->shared_from_this());
                self->owner_->check_authorization_expired(req.get_response_code(), req.get_response_stream().str());
                return 0;
            }

            std::string http_content;
            std::string value_content;

            req.get_response_stream().str().swap(http_content);
            WLOGTRACE("Etcd keepalive %p got http response: %s", self, http_content.c_str());

            // 如果lease不存在（没有TTL）则启动创建流程
            rapidjson::Document doc;

            if (::atframe::component::etcd_packer::parse_object(doc, http_content.c_str())) {
                rapidjson::Value root = doc.GetObject();

                // Run check function
                int64_t count = 0;

                etcd_packer::unpack_int(root, "count", count);
                if (count > 0) {
                    rapidjson::Document::MemberIterator kvs = root.FindMember("kvs");
                    if (root.MemberEnd() == kvs) {
                        WLOGERROR("Etcd keepalive %p get data count=%lld, but kvs not found", self, static_cast<long long>(count));
                        self->owner_->add_retry_keepalive(self->shared_from_this());
                        return 0;
                    }

                    rapidjson::Document::Array all_kvs = kvs->value.GetArray();
                    for (rapidjson::Document::Array::ValueIterator iter = all_kvs.Begin(); iter != all_kvs.End(); ++iter) {
                        etcd_key_value kv;
                        etcd_packer::unpack(kv, *iter);
                        value_content.swap(kv.value);
                        break;
                    }
                }
            }

            self->checker_.is_check_run = true;
            if (!self->checker_.fn) {
                self->checker_.is_check_passed = true;
            } else {
                self->checker_.is_check_passed = self->checker_.fn(value_content);
            }
            WLOGDEBUG("Etcd keepalive %p check data %s", self, self->checker_.is_check_passed ? "passed" : "failed");

            self->active();
            return 0;
        }

        int etcd_keepalive::libcurl_callback_on_set_data(util::network::http_request &req) {
            etcd_keepalive *self = reinterpret_cast<etcd_keepalive *>(req.get_priv_data());
            if (NULL == self) {
                WLOGERROR("Etcd keepalive set request shouldn't has request without private data");
                return 0;
            }

            util::network::http_request::ptr_t keep_rpc = self->rpc_.rpc_opr_;
            self->rpc_.rpc_opr_.reset();

            // 服务器错误则忽略
            if (0 != req.get_error_code() ||
                util::network::http_request::status_code_t::EN_ECG_SUCCESS != util::network::http_request::get_status_code_group(req.get_response_code())) {
                WLOGERROR("Etcd keepalive %p set request failed, error code: %d, http code: %d\n%s", self, req.get_error_code(), req.get_response_code(),
                          req.get_error_msg());

                self->rpc_.is_value_changed = true;
                self->owner_->add_retry_keepalive(self->shared_from_this());
                self->owner_->check_authorization_expired(req.get_response_code(), req.get_response_stream().str());
                return 0;
            }

            self->rpc_.has_data = true;
            WLOGTRACE("Etcd keepalive %p set data http response: %s", self, req.get_response_stream().str().c_str());
            self->active();
            return 0;
        }

    } // namespace component
} // namespace atframe
