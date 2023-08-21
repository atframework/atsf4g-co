// Copyright 2023 atframework
// Created by owentou on 2019-01-15.

#include "utility/tls_buffers.h"

#include <std/thread.h>

#if TCONNAPI_MAX_BUFF > (1 << 20)
#  define TLS_BUFFER_BLOCK_SIZE TCONNAPI_MAX_BUFF
#else
#  define TLS_BUFFER_BLOCK_SIZE (1 << 20)
#endif

#if defined(_REENTRANT)
#  define ATFRAMEWORK_ULILITY_TLS_BUFFER_USE_PTHREAD 1
#elif defined(THREAD_TLS_ENABLED) && THREAD_TLS_ENABLED
#  define ATFRAMEWORK_ULILITY_TLS_BUFFER_USE_THREAD_LOCAL 1
#else
#  define ATFRAMEWORK_ULILITY_TLS_BUFFER_USE_PTHREAD 1
#endif

#if defined(ATFRAMEWORK_ULILITY_TLS_BUFFER_USE_PTHREAD) && ATFRAMEWORK_ULILITY_TLS_BUFFER_USE_PTHREAD
#  include <pthread.h>
namespace detail {
static pthread_once_t gt_atgateway_get_msg_buffer_tls_once = PTHREAD_ONCE_INIT;
static pthread_key_t gt_atgateway_get_msg_buffer_tls_key[::tls_buffers_type_t::EN_TBT_MAX];

static void dtor_pthread_atgateway_get_msg_buffer_tls(void *p) {
  char *res = reinterpret_cast<char *>(p);
  if (NULL != res) {
    delete[] res;
  }
}

static void init_pthread_atgateway_get_msg_buffer_tls() {
  for (int i = 0; i < ::tls_buffers_type_t::EN_TBT_MAX; ++i) {
    (void)pthread_key_create(&gt_atgateway_get_msg_buffer_tls_key[i], dtor_pthread_atgateway_get_msg_buffer_tls);
  }
}

static char *atgateway_get_msg_buffer(::tls_buffers_type_t::type t) {
  (void)pthread_once(&gt_atgateway_get_msg_buffer_tls_once, init_pthread_atgateway_get_msg_buffer_tls);
  char *ret = reinterpret_cast<char *>(pthread_getspecific(gt_atgateway_get_msg_buffer_tls_key[t]));
  if (NULL == ret) {
    ret = new char[TLS_BUFFER_BLOCK_SIZE + 2 * sizeof(size_t)];  // in case of padding
    pthread_setspecific(gt_atgateway_get_msg_buffer_tls_key[t], ret);
  }
  return ret;
}
}  // namespace detail
#else
namespace detail {
static char *atgateway_get_msg_buffer(::tls_buffers_type_t::type t) {
  static THREAD_TLS char ret[::tls_buffers_type_t::EN_TBT_MAX]
                            [TLS_BUFFER_BLOCK_SIZE + 2 * sizeof(size_t)];  // in case of padding
  return ret[t];
}
}  // namespace detail
#endif

SERVER_FRAME_API void *tls_buffers_get_buffer(tls_buffers_type_t::type tls_type) {
  if (tls_type >= tls_buffers_type_t::EN_TBT_MAX || tls_type < 0) {
    return NULL;
  }
  return ::detail::atgateway_get_msg_buffer(tls_type);
}

SERVER_FRAME_API size_t tls_buffers_get_length(tls_buffers_type_t::type tls_type) {
  if (tls_type >= tls_buffers_type_t::EN_TBT_MAX || tls_type < 0) {
    return 0;
  }
  return TLS_BUFFER_BLOCK_SIZE;
}
