// Copyright 2025 Tencent
// Created by owent on 2025-03-21

#include "atgateway/protocols/libatgw_protocol_api.h"

#include <string>

#include "config/atframe_utils_build_feature.h"
#include "std/thread.h"

#ifndef ATFRAMEWORK_ATGATEWAY_TLS_BUFFER_SIZE
#  define ATFRAMEWORK_ATGATEWAY_TLS_BUFFER_SIZE 262144
#endif

#if defined(_REENTRANT)
#  define ATFRAMEWORK_ATGATEWAY_TLS_BUFFER_USE_PTHREAD 1
#elif defined(THREAD_TLS_ENABLED) && THREAD_TLS_ENABLED
#  define ATFRAMEWORK_ATGATEWAY_TLS_BUFFER_USE_THREAD_LOCAL 1
#else
#  define ATFRAMEWORK_ATGATEWAY_TLS_BUFFER_USE_PTHREAD 1
#endif

#if defined(ATFRAMEWORK_ATGATEWAY_TLS_BUFFER_USE_PTHREAD) && ATFRAMEWORK_ATGATEWAY_TLS_BUFFER_USE_PTHREAD
#  include <pthread.h>
namespace atframework {
namespace gateway {
namespace {
static pthread_once_t gt_atgateway_get_msg_buffer_tls_once = PTHREAD_ONCE_INIT;
static pthread_key_t
    gt_atgateway_get_msg_buffer_tls_key[::atframework::gateway::libatgw_protocol_api::tls_buffer_t::EN_TBT_MAX];

static void dtor_pthread_atgateway_get_msg_buffer_tls(void *p) {
  char *res = reinterpret_cast<char *>(p);
  if (nullptr != res) {
    delete[] res;
  }
}

static void init_pthread_atgateway_get_msg_buffer_tls() {
  for (int i = 0; i < ::atframework::gateway::libatgw_protocol_api::tls_buffer_t::EN_TBT_MAX; ++i) {
    (void)pthread_key_create(&gt_atgateway_get_msg_buffer_tls_key[i], dtor_pthread_atgateway_get_msg_buffer_tls);
  }
}

static char *atgateway_get_msg_buffer(::atframework::gateway::libatgw_protocol_api::tls_buffer_t::type t) {
  (void)pthread_once(&gt_atgateway_get_msg_buffer_tls_once, init_pthread_atgateway_get_msg_buffer_tls);
  char *ret = reinterpret_cast<char *>(pthread_getspecific(gt_atgateway_get_msg_buffer_tls_key[t]));
  if (nullptr == ret) {
    ret = new char[ATFRAMEWORK_ATGATEWAY_TLS_BUFFER_SIZE + (2 * sizeof(size_t))];  // in case of padding
    pthread_setspecific(gt_atgateway_get_msg_buffer_tls_key[t], ret);
  }
  return ret;
}
}  // namespace
}  // namespace gateway
}  // namespace atframework
#else
namespace atframework {
namespace gateway {
namespace {
static char *atgateway_get_msg_buffer(::atframework::gateway::libatgw_protocol_api::tls_buffer_t::type t) {
  static THREAD_TLS char ret[::atframework::gateway::libatgw_protocol_api::tls_buffer_t::EN_TBT_MAX]
                            [ATFRAMEWORK_ATGATEWAY_TLS_BUFFER_SIZE + (2 * sizeof(size_t))];  // in case of padding
  return ret[t];
}
}  // namespace
}  // namespace gateway
}  // namespace atframework
#endif

namespace atframework {
namespace gateway {
LIBATGW_PROTOCOL_API libatgw_protocol_api::flag_guard_t::flag_guard_t(int &f, int v) : flags_(&f), v_(0) {
  if (f == (f | v)) {
    flags_ = nullptr;
  } else {
    v_ = (f | v) ^ f;
    f |= v_;
  }
}

LIBATGW_PROTOCOL_API libatgw_protocol_api::flag_guard_t::~flag_guard_t() {
  if (nullptr == flags_) {
    return;
  }

  *flags_ &= ~v_;
}

LIBATGW_PROTOCOL_API libatgw_protocol_api::libatgw_protocol_api()
    : flags_(0), write_header_offset_(0), callbacks_(nullptr), private_data_(nullptr) {}

LIBATGW_PROTOCOL_API libatgw_protocol_api::~libatgw_protocol_api() {
  if (check_flag(flag_t::EN_PFT_HANDSHAKE_UPDATE) || !check_flag(flag_t::EN_PFT_HANDSHAKE_DONE)) {
    inner_handshake_done(error_code_t::EN_ECT_HANDSHAKE);
  }
}

LIBATGW_PROTOCOL_API bool libatgw_protocol_api::check_flag(flag_t::type t) const { return 0 != (flags_ & t); }

LIBATGW_PROTOCOL_API void libatgw_protocol_api::set_flag(flag_t::type t, bool v) {
  if (v) {
    flags_ |= t;
  } else {
    flags_ &= ~t;
  }
}

LIBATGW_PROTOCOL_API gsl::span<unsigned char> libatgw_protocol_api::get_tls_buffer(tls_buffer_t::type tls_type) {
  if (tls_type >= tls_buffer_t::EN_TBT_MAX || tls_type < 0) {
    return {};
  }
  return {reinterpret_cast<unsigned char *>(atgateway_get_msg_buffer(tls_type)), ATFRAMEWORK_ATGATEWAY_TLS_BUFFER_SIZE};
}

LIBATGW_PROTOCOL_API int libatgw_protocol_api::write_done(int /*status*/) {
  if (!check_flag(flag_t::EN_PFT_WRITING)) {
    return 0;
  }
  set_flag(flag_t::EN_PFT_WRITING, false);

  return 0;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_api::close(int reason) {
  if (check_flag(flag_t::EN_PFT_CLOSING)) {
    return 0;
  }
  set_flag(flag_t::EN_PFT_CLOSING, true);
  set_flag(flag_t::EN_PFT_CLOSED, true);

  if (nullptr != callbacks_ && callbacks_->close_fn) {
    return callbacks_->close_fn(this, reason);
  }
  return 0;
}

LIBATGW_PROTOCOL_API bool libatgw_protocol_api::check_reconnect(const libatgw_protocol_api * /*other*/) {
  return false;
}

LIBATGW_PROTOCOL_API void libatgw_protocol_api::set_recv_buffer_limit(size_t, size_t) {}
LIBATGW_PROTOCOL_API void libatgw_protocol_api::set_send_buffer_limit(size_t, size_t) {}

LIBATGW_PROTOCOL_API int libatgw_protocol_api::handshake_done(int status) { return inner_handshake_done(status); }
LIBATGW_PROTOCOL_API int libatgw_protocol_api::inner_handshake_done(int status) {
  bool has_handshake_done = check_flag(flag_t::EN_PFT_HANDSHAKE_DONE);
  if (has_handshake_done && !check_flag(flag_t::EN_PFT_HANDSHAKE_UPDATE)) {
    return error_code_t::EN_ECT_HANDSHAKE;
  }
  set_flag(flag_t::EN_PFT_HANDSHAKE_DONE, true);
  set_flag(flag_t::EN_PFT_HANDSHAKE_UPDATE, false);

  // on_handshake_done_fn only active when handshake done
  if (!has_handshake_done && nullptr != callbacks_ && callbacks_->on_handshake_done_fn) {
    callbacks_->on_handshake_done_fn(this, status);
  } else if (nullptr != callbacks_ && callbacks_->on_handshake_update_fn) {
    callbacks_->on_handshake_update_fn(this, status);
  }

  return 0;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_api::handshake_update() {
  if (check_flag(flag_t::EN_PFT_HANDSHAKE_UPDATE)) {
    return error_code_t::EN_ECT_HANDSHAKE;
  }

  set_flag(flag_t::EN_PFT_HANDSHAKE_UPDATE, true);

  return 0;
}

LIBATGW_PROTOCOL_API std::string libatgw_protocol_api::get_info() const { return std::string(""); }
}  // namespace gateway
}  // namespace atframework
