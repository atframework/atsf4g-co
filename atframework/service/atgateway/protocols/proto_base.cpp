#include <string>

#include "config/atframe_utils_build_feature.h"
#include "proto_base.h"
#include "std/thread.h"

#ifndef ATBUS_MACRO_MSG_LIMIT
#define ATBUS_MACRO_MSG_LIMIT 262144
#endif

#if !(defined(THREAD_TLS_USE_PTHREAD) && THREAD_TLS_USE_PTHREAD) && defined(THREAD_TLS_ENABLED) && THREAD_TLS_ENABLED
namespace atframe {
    namespace gateway {
        namespace detail {
            static char *atgateway_get_msg_buffer(::atframe::gateway::proto_base::tls_buffer_t::type t) {
                static THREAD_TLS char ret[::atframe::gateway::proto_base::tls_buffer_t::EN_TBT_MAX]
                                          [ATBUS_MACRO_MSG_LIMIT + 2 * sizeof(size_t)]; // in case of padding
                return ret[t];
            }
        } // namespace detail
    }     // namespace gateway
} // namespace atframe
#else

#include <pthread.h>
namespace atframe {
    namespace gateway {
        namespace detail {
            static pthread_once_t gt_atgateway_get_msg_buffer_tls_once = PTHREAD_ONCE_INIT;
            static pthread_key_t  gt_atgateway_get_msg_buffer_tls_key[::atframe::gateway::proto_base::tls_buffer_t::EN_TBT_MAX];

            static void dtor_pthread_atgateway_get_msg_buffer_tls(void *p) {
                char *res = reinterpret_cast<char *>(p);
                if (NULL != res) {
                    delete[] res;
                }
            }

            static void init_pthread_atgateway_get_msg_buffer_tls() {
                for (int i = 0; i < ::atframe::gateway::proto_base::tls_buffer_t::EN_TBT_MAX; ++i) {
                    (void)pthread_key_create(&gt_atgateway_get_msg_buffer_tls_key[i], dtor_pthread_atgateway_get_msg_buffer_tls);
                }
            }

            static char *atgateway_get_msg_buffer(::atframe::gateway::proto_base::tls_buffer_t::type t) {
                (void)pthread_once(&gt_atgateway_get_msg_buffer_tls_once, init_pthread_atgateway_get_msg_buffer_tls);
                char *ret = reinterpret_cast<char *>(pthread_getspecific(gt_atgateway_get_msg_buffer_tls_key[t]));
                if (NULL == ret) {
                    ret = new char[ATBUS_MACRO_MSG_LIMIT + 2 * sizeof(size_t)]; // in case of padding
                    pthread_setspecific(gt_atgateway_get_msg_buffer_tls_key[t], ret);
                }
                return ret;
            }
        } // namespace detail
    }     // namespace gateway
} // namespace atframe

#endif

namespace atframe {
    namespace gateway {
        proto_base::flag_guard_t::flag_guard_t(int &f, int v) : flags_(&f), v_(0) {
            if (f == (f | v)) {
                flags_ = NULL;
            } else {
                v_ = (f | v) ^ f;
                f |= v_;
            }
        }

        proto_base::flag_guard_t::~flag_guard_t() {
            if (NULL == flags_) {
                return;
            }

            *flags_ &= ~v_;
        }

        proto_base::proto_base() : flags_(0), write_header_offset_(0), callbacks_(NULL), private_data_(NULL) {}
        proto_base::~proto_base() {
            if (check_flag(flag_t::EN_PFT_HANDSHAKE_UPDATE) || !check_flag(flag_t::EN_PFT_HANDSHAKE_DONE)) {
                inner_handshake_done(error_code_t::EN_ECT_HANDSHAKE);
            }
        };

        bool proto_base::check_flag(flag_t::type t) const { return 0 != (flags_ & t); }

        void proto_base::set_flag(flag_t::type t, bool v) {
            if (v) {
                flags_ |= t;
            } else {
                flags_ &= ~t;
            }
        }

        void *proto_base::get_tls_buffer(tls_buffer_t::type tls_type) {
            if (tls_type >= tls_buffer_t::EN_TBT_MAX || tls_type < 0) {
                return NULL;
            }
            return ::atframe::gateway::detail::atgateway_get_msg_buffer(tls_type);
        }

        size_t proto_base::get_tls_length(tls_buffer_t::type tls_type) {
            if (tls_type >= tls_buffer_t::EN_TBT_MAX || tls_type < 0) {
                return 0;
            }
            return ATBUS_MACRO_MSG_LIMIT;
        }

        int proto_base::write_done(int /*status*/) {
            if (!check_flag(flag_t::EN_PFT_WRITING)) {
                return 0;
            }
            set_flag(flag_t::EN_PFT_WRITING, false);

            return 0;
        }

        int proto_base::close(int reason) {
            if (check_flag(flag_t::EN_PFT_CLOSING)) {
                return 0;
            }
            set_flag(flag_t::EN_PFT_CLOSING, true);
            set_flag(flag_t::EN_PFT_CLOSED, true);

            if (NULL != callbacks_ && callbacks_->close_fn) {
                return callbacks_->close_fn(this, reason);
            }
            return 0;
        }

        bool proto_base::check_reconnect(const proto_base * /*other*/) { return false; }

        void proto_base::set_recv_buffer_limit(size_t, size_t) {}
        void proto_base::set_send_buffer_limit(size_t, size_t) {}

        int proto_base::handshake_done(int status) { return inner_handshake_done(status); }
        int proto_base::inner_handshake_done(int status);
            bool has_handshake_done = check_flag(flag_t::EN_PFT_HANDSHAKE_DONE);
            if (has_handshake_done && !check_flag(flag_t::EN_PFT_HANDSHAKE_UPDATE)) {
                return error_code_t::EN_ECT_HANDSHAKE;
            }
            set_flag(flag_t::EN_PFT_HANDSHAKE_DONE, true);
            set_flag(flag_t::EN_PFT_HANDSHAKE_UPDATE, false);

            // on_handshake_done_fn only active when handshake done
            if (!has_handshake_done && NULL != callbacks_ && callbacks_->on_handshake_done_fn) {
                callbacks_->on_handshake_done_fn(this, status);
            } else if (NULL != callbacks_ && callbacks_->on_handshake_update_fn) {
                callbacks_->on_handshake_update_fn(this, status);
            }

            return 0;
        }

        int proto_base::handshake_update() {
            if (check_flag(flag_t::EN_PFT_HANDSHAKE_UPDATE)) {
                return error_code_t::EN_ECT_HANDSHAKE;
            }

            set_flag(flag_t::EN_PFT_HANDSHAKE_UPDATE, true);

            return 0;
        }

        std::string proto_base::get_info() const { return std::string(""); }
    } // namespace gateway
} // namespace atframe