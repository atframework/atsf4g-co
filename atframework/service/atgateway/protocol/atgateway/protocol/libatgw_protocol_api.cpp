// Copyright 2025 Tencent
// Created by owent on 2025-03-21

#include "atgateway/protocol/libatgw_protocol_api.h"

#include <cstddef>
#include <string>

#include "config/atframe_utils_build_feature.h"  // IWYU pragma: keep
#include "std/thread.h"

#ifndef ATFRAMEWORK_ATGATEWAY_TLS_BUFFER_SIZE
#  define ATFRAMEWORK_ATGATEWAY_TLS_BUFFER_SIZE 262144
#endif

#ifdef _REENTRANT
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
  gt_atgateway_get_msg_buffer_tls_key[static_cast<size_t>(
    ::atframework::gateway::libatgw_protocol_api::tls_buffer_t::kMax)];

static void dtor_pthread_atgateway_get_msg_buffer_tls(void *p) {
  char *res = reinterpret_cast<char *>(p);
  if (nullptr != res) {
    delete[] res;
  }
}

static void init_pthread_atgateway_get_msg_buffer_tls() {
  for (size_t i = 0;
       i < static_cast<size_t>(::atframework::gateway::libatgw_protocol_api::tls_buffer_t::kMax); ++i) {
    (void)pthread_key_create(&gt_atgateway_get_msg_buffer_tls_key[i], dtor_pthread_atgateway_get_msg_buffer_tls);
  }
}

static char *atgateway_get_msg_buffer(::atframework::gateway::libatgw_protocol_api::tls_buffer_t t) {
  (void)pthread_once(&gt_atgateway_get_msg_buffer_tls_once, init_pthread_atgateway_get_msg_buffer_tls);
  const size_t tls_index = static_cast<size_t>(t);
  char *ret = reinterpret_cast<char *>(pthread_getspecific(gt_atgateway_get_msg_buffer_tls_key[tls_index]));
  if (nullptr == ret) {
    ret = new char[ATFRAMEWORK_ATGATEWAY_TLS_BUFFER_SIZE + (2 * sizeof(size_t))];  // in case of padding
    pthread_setspecific(gt_atgateway_get_msg_buffer_tls_key[tls_index], ret);
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
static char *atgateway_get_msg_buffer(::atframework::gateway::libatgw_protocol_api::tls_buffer_t t) {
  static THREAD_TLS char ret[static_cast<size_t>(::atframework::gateway::libatgw_protocol_api::tls_buffer_t::kMax)]
                            [ATFRAMEWORK_ATGATEWAY_TLS_BUFFER_SIZE + (2 * sizeof(size_t))];  // in case of padding
  return ret[static_cast<size_t>(t)];
}
}  // namespace
}  // namespace gateway
}  // namespace atframework
#endif

namespace atframework {
namespace gateway {
LIBATGW_PROTOCOL_API libatgw_protocol_api::flag_guard_t::flag_guard_t(uint32_t &f, flag_t v) : flags_(&f), v_(0) {
  const uint32_t flag_value = static_cast<uint32_t>(v);
  if (f == (f | flag_value)) {
    flags_ = nullptr;
  } else {
    v_ = (f | flag_value) ^ f;
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
  if (check_flag(flag_t::kHandshakeUpdate) || !check_flag(flag_t::kHandshakeDone)) {
    inner_handshake_done(static_cast<int>(error_code_t::kHandshake));
  }
}

LIBATGW_PROTOCOL_API bool libatgw_protocol_api::check_flag(flag_t t) const {
  return 0 != (flags_ & static_cast<uint32_t>(t));
}

LIBATGW_PROTOCOL_API void libatgw_protocol_api::set_flag(flag_t t, bool v) {
  const uint32_t flag_value = static_cast<uint32_t>(t);
  if (v) {
    flags_ |= flag_value;
  } else {
    flags_ &= ~flag_value;
  }
}

LIBATGW_PROTOCOL_API gsl::span<unsigned char> libatgw_protocol_api::get_tls_buffer(tls_buffer_t tls_type) {
  if (tls_type >= tls_buffer_t::kMax) {
    return {};
  }
  return {reinterpret_cast<unsigned char *>(atgateway_get_msg_buffer(tls_type)), ATFRAMEWORK_ATGATEWAY_TLS_BUFFER_SIZE};
}

LIBATGW_PROTOCOL_API int libatgw_protocol_api::write_done(int /*status*/) {
  if (!check_flag(flag_t::kWriting)) {
    return 0;
  }
  set_flag(flag_t::kWriting, false);

  return 0;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_api::close(int reason) {
  if (check_flag(flag_t::kClosing)) {
    return 0;
  }
  set_flag(flag_t::kClosing, true);
  set_flag(flag_t::kClosed, true);

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
  bool has_handshake_done = check_flag(flag_t::kHandshakeDone);
  if (has_handshake_done && !check_flag(flag_t::kHandshakeUpdate)) {
    return static_cast<int>(error_code_t::kHandshake);
  }
  set_flag(flag_t::kHandshakeDone, true);
  set_flag(flag_t::kHandshakeUpdate, false);

  // on_handshake_done_fn only active when handshake done
  if (!has_handshake_done && nullptr != callbacks_ && callbacks_->on_handshake_done_fn) {
    callbacks_->on_handshake_done_fn(this, status);
  } else if (nullptr != callbacks_ && callbacks_->on_handshake_update_fn) {
    callbacks_->on_handshake_update_fn(this, status);
  }

  return 0;
}

LIBATGW_PROTOCOL_API int libatgw_protocol_api::handshake_update() {
  if (check_flag(flag_t::kHandshakeUpdate)) {
    return static_cast<int>(error_code_t::kHandshake);
  }

  set_flag(flag_t::kHandshakeUpdate, true);

  return 0;
}

LIBATGW_PROTOCOL_API std::string libatgw_protocol_api::get_info() const { return {}; }
}  // namespace gateway
}  // namespace atframework
