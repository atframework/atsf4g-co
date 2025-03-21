// Copyright 2021 atframework
// Created by owent on 2016/9/29.
//

#include "libatgw_v1_c.h"

#include <atgateway/protocols/v1/libatgw_protocol_sdk.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "common/compiler_message.h"
#include "common/string_oprs.h"

#define ATGW_CONTEXT(x) ((::atframework::gateway::libatgw_protocol_sdk *)(x))
#define ATGW_CONTEXT_IS_NULL(x) (nullptr == (x))

struct g_libatgw_v1_c_callbacks_t {
  ::atframework::gateway::libatgw_protocol_api::proto_callbacks_t callbacks;
  libatgw_v1_c_on_write_start_fn_t write_start_fn;
  libatgw_v1_c_on_message_fn_t on_message_fn;
  libatgw_v1_c_on_init_new_session_fn_t on_init_new_session_fn;
  libatgw_v1_c_on_init_reconnect_fn_t on_init_reconnect_fn;
  libatgw_v1_c_on_close_fn_t on_close_fn;
  libatgw_v1_c_on_handshake_done_fn_t on_handshake_done_fn;
  libatgw_v1_c_on_handshake_done_fn_t on_handshake_update_fn;
  libatgw_v1_c_on_error_fn_t on_error_fn;

  g_libatgw_v1_c_callbacks_t()
      : write_start_fn(nullptr),
        on_message_fn(nullptr),
        on_init_new_session_fn(nullptr),
        on_init_reconnect_fn(nullptr),
        on_close_fn(nullptr),
        on_handshake_done_fn(nullptr),
        on_error_fn(nullptr) {}
};

static g_libatgw_v1_c_callbacks_t *libatgw_v1_c_get_c_callbacks() {
  static g_libatgw_v1_c_callbacks_t cbks;
  return &cbks;
}

static int32_t proto_inner_callback_on_write(::atframework::gateway::libatgw_protocol_api *proto, void *buffer,
                                             size_t sz, bool *is_done) {
  if (nullptr == buffer || 0 == sz) {
    if (nullptr != is_done) {
      *is_done = true;
    }
    return ::atframework::gateway::error_code_t::EN_ECT_PARAM;
  }

  libatgw_v1_c_on_write_start_fn_t fn = libatgw_v1_c_get_c_callbacks()->write_start_fn;
  if (nullptr == fn) {
    if (nullptr != is_done) {
      *is_done = true;
    }
    return ::atframework::gateway::error_code_t::EN_ECT_MISS_CALLBACKS;
  }

  int32_t is_done_i = 0;
  libatgw_v1_c_context context;
  context = proto;
  int32_t ret = fn(context, buffer, sz, &is_done_i);
  if (nullptr != is_done) {
    *is_done = !!is_done_i;
  }

  return ret;
}

static int32_t proto_inner_callback_on_message(::atframework::gateway::libatgw_protocol_api *proto, const void *buffer,
                                               size_t sz) {
  libatgw_v1_c_on_message_fn_t fn = libatgw_v1_c_get_c_callbacks()->on_message_fn;
  if (nullptr != fn) {
    libatgw_v1_c_context context;
    context = proto;
    fn(context, buffer, sz);
  }

  return 0;
}

// useless
static int32_t proto_inner_callback_on_new_session(::atframework::gateway::libatgw_protocol_api *proto,
                                                   uint64_t &sess_id) {
  libatgw_v1_c_on_init_new_session_fn_t fn = libatgw_v1_c_get_c_callbacks()->on_init_new_session_fn;
  if (nullptr != fn) {
    libatgw_v1_c_context context;
    context = proto;
    fn(context, &sess_id);
  }

  return 0;
}

// useless
static int32_t proto_inner_callback_on_reconnect(::atframework::gateway::libatgw_protocol_api *proto,
                                                 uint64_t sess_id) {
  libatgw_v1_c_on_init_reconnect_fn_t fn = libatgw_v1_c_get_c_callbacks()->on_init_reconnect_fn;
  if (nullptr != fn) {
    libatgw_v1_c_context context;
    context = proto;
    fn(context, sess_id);
  }

  return 0;
}

static int32_t proto_inner_callback_on_close(::atframework::gateway::libatgw_protocol_api *proto, int32_t reason) {
  libatgw_v1_c_on_close_fn_t fn = libatgw_v1_c_get_c_callbacks()->on_close_fn;
  if (nullptr != fn) {
    libatgw_v1_c_context context;
    context = proto;
    fn(context, reason);
  }

  return 0;
}

static int32_t proto_inner_callback_on_handshake(::atframework::gateway::libatgw_protocol_api *proto, int32_t status) {
  libatgw_v1_c_on_handshake_done_fn_t fn = libatgw_v1_c_get_c_callbacks()->on_handshake_done_fn;
  if (nullptr != fn) {
    libatgw_v1_c_context context;
    context = proto;
    fn(context, status);
  }

  return 0;
}

static int32_t proto_inner_callback_on_handshake_update(::atframework::gateway::libatgw_protocol_api *proto,
                                                        int32_t status) {
  libatgw_v1_c_on_handshake_done_fn_t fn = libatgw_v1_c_get_c_callbacks()->on_handshake_update_fn;
  if (nullptr != fn) {
    libatgw_v1_c_context context;
    context = proto;
    fn(context, status);
  }

  return 0;
}

static int32_t proto_inner_callback_on_error(::atframework::gateway::libatgw_protocol_api *proto, const char *filename,
                                             int32_t line, int32_t errcode, const char *errmsg) {
  libatgw_v1_c_on_error_fn_t fn = libatgw_v1_c_get_c_callbacks()->on_error_fn;
  if (nullptr != fn) {
    libatgw_v1_c_context context;
    context = proto;
    fn(context, filename, line, errcode, errmsg);
  }

  return 0;
}

static ::atframework::gateway::libatgw_protocol_api::proto_callbacks_t *libatgw_v1_c_get_proto_callbacks() {
  g_libatgw_v1_c_callbacks_t *cbks = libatgw_v1_c_get_c_callbacks();

  // init
  if (!cbks->callbacks.write_fn) {
    cbks->callbacks.write_fn = proto_inner_callback_on_write;
    cbks->callbacks.message_fn = proto_inner_callback_on_message;
    cbks->callbacks.new_session_fn = proto_inner_callback_on_new_session;
    cbks->callbacks.reconnect_fn = proto_inner_callback_on_reconnect;
    cbks->callbacks.close_fn = proto_inner_callback_on_close;
    cbks->callbacks.on_handshake_done_fn = proto_inner_callback_on_handshake;
    cbks->callbacks.on_handshake_update_fn = proto_inner_callback_on_handshake_update;
    cbks->callbacks.on_error_fn = proto_inner_callback_on_error;
  }
  return &cbks->callbacks;
}

#ifdef __cplusplus
extern "C" {
#endif

LIBATGW_V1_C_API void __cdecl libatgw_v1_c_global_init_algorithms() { util::crypto::cipher::init_global_algorithm(); }
LIBATGW_V1_C_API void __cdecl libatgw_v1_c_global_cleanup_algorithms() {
  util::crypto::cipher::cleanup_global_algorithm();
}

LIBATGW_V1_C_API uint64_t __cdecl libatgw_v1_c_global_get_crypt_size() {
  return static_cast<uint64_t>(util::crypto::cipher::get_all_cipher_names().size());
}

LIBATGW_V1_C_API const char *__cdecl libatgw_v1_c_global_get_crypt_name(uint64_t idx) {
  const std::vector<std::string> &res = util::crypto::cipher::get_all_cipher_names();
  if (idx >= res.size()) {
    return nullptr;
  }

  return res[idx].c_str();
}

LIBATGW_V1_C_API void __cdecl libatgw_v1_c_gset_on_write_start_fn(libatgw_v1_c_on_write_start_fn_t fn) {
  libatgw_v1_c_get_c_callbacks()->write_start_fn = fn;
}

LIBATGW_V1_C_API void __cdecl libatgw_v1_c_gset_on_message_fn(libatgw_v1_c_on_message_fn_t fn) {
  libatgw_v1_c_get_c_callbacks()->on_message_fn = fn;
}

LIBATGW_V1_C_API void __cdecl libatgw_v1_c_gset_on_init_new_session_fn(libatgw_v1_c_on_init_new_session_fn_t fn) {
  libatgw_v1_c_get_c_callbacks()->on_init_new_session_fn = fn;
}

LIBATGW_V1_C_API void __cdecl libatgw_v1_c_gset_on_init_reconnect_fn(libatgw_v1_c_on_init_reconnect_fn_t fn) {
  libatgw_v1_c_get_c_callbacks()->on_init_reconnect_fn = fn;
}

LIBATGW_V1_C_API void __cdecl libatgw_v1_c_gset_on_close_fn(libatgw_v1_c_on_close_fn_t fn) {
  libatgw_v1_c_get_c_callbacks()->on_close_fn = fn;
}

LIBATGW_V1_C_API void __cdecl libatgw_v1_c_gset_on_handshake_done_fn(libatgw_v1_c_on_handshake_done_fn_t fn) {
  libatgw_v1_c_get_c_callbacks()->on_handshake_done_fn = fn;
}

LIBATGW_V1_C_API void __cdecl libatgw_v1_c_gset_on_handshake_update_fn(libatgw_v1_c_on_handshake_done_fn_t fn) {
  libatgw_v1_c_get_c_callbacks()->on_handshake_update_fn = fn;
}

LIBATGW_V1_C_API void __cdecl libatgw_v1_c_gset_on_error_fn(libatgw_v1_c_on_error_fn_t fn) {
  libatgw_v1_c_get_c_callbacks()->on_error_fn = fn;
}

LIBATGW_V1_C_API libatgw_v1_c_context __cdecl libatgw_v1_c_create() {
  libatgw_v1_c_context ret;
  assert(sizeof(void *) == sizeof(libatgw_v1_c_context));

  ::atframework::gateway::libatgw_protocol_sdk *res = new (std::nothrow)::atframework::gateway::libatgw_protocol_sdk();
  if (nullptr != res) {
    res->set_callbacks(libatgw_v1_c_get_proto_callbacks());
  }

  ret = res;
  return ret;
}

LIBATGW_V1_C_API void __cdecl libatgw_v1_c_destroy(libatgw_v1_c_context context) { delete ATGW_CONTEXT(context); }

LIBATGW_V1_C_API void __cdecl libatgw_v1_c_set_recv_buffer_limit(libatgw_v1_c_context context, uint64_t max_size,
                                                                 uint64_t max_number) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return;
  }

  ATGW_CONTEXT(context)->set_recv_buffer_limit((size_t)max_size, (size_t)max_number);
}

LIBATGW_V1_C_API void __cdecl libatgw_v1_c_set_send_buffer_limit(libatgw_v1_c_context context, uint64_t max_size,
                                                                 uint64_t max_number) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return;
  }

  ATGW_CONTEXT(context)->set_send_buffer_limit((size_t)max_size, (size_t)max_number);
}

LIBATGW_V1_C_API int32_t __cdecl libatgw_v1_c_start_session(libatgw_v1_c_context context, const char *crypt_type) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return ::atframework::gateway::error_code_t::EN_ECT_PARAM;
  }

  return ATGW_CONTEXT(context)->start_session(crypt_type);
}

LIBATGW_V1_C_API int32_t __cdecl libatgw_v1_c_reconnect_session(libatgw_v1_c_context context, uint64_t sessios_id,
                                                                const char *crypt_type, const unsigned char *secret_buf,
                                                                uint64_t secret_len) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return ::atframework::gateway::error_code_t::EN_ECT_PARAM;
  }

  std::vector<unsigned char> secret;
  secret.assign(secret_buf, secret_buf + secret_len);
  return ATGW_CONTEXT(context)->reconnect_session(sessios_id, crypt_type, secret);
}

LIBATGW_V1_C_API void __cdecl libatgw_v1_c_get_info(libatgw_v1_c_context context, char *info_str, uint64_t info_len) {
  if (nullptr == info_str || 0 == info_len) {
    return;
  }

  if (ATGW_CONTEXT_IS_NULL(context)) {
    info_str[0] = 0;
    return;
  }

  std::string msg = ATGW_CONTEXT(context)->get_info();
  size_t len = msg.size();
  if (len >= info_len) {
    len = info_len - 1;
  }

  memcpy(info_str, msg.c_str(), len);
  info_str[len - 1] = 0;
}

LIBATGW_V1_C_API void __cdecl libatgw_v1_c_set_private_data(libatgw_v1_c_context context, void *p) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return;
  }

  return ATGW_CONTEXT(context)->set_private_data(p);
}

LIBATGW_V1_C_API void *__cdecl libatgw_v1_c_get_private_data(libatgw_v1_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return nullptr;
  }

  return ATGW_CONTEXT(context)->get_private_data();
}

LIBATGW_V1_C_API uint64_t __cdecl libatgw_v1_c_get_session_id(libatgw_v1_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return 0;
  }

  return ATGW_CONTEXT(context)->get_session_id();
}

LIBATGW_V1_C_API const char *__cdecl libatgw_v1_c_get_crypt_type(libatgw_v1_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return 0;
  }

  return ATGW_CONTEXT(context)->get_crypt_handshake()->type.c_str();
}

LIBATGW_V1_C_API uint64_t __cdecl libatgw_v1_c_get_crypt_secret_size(libatgw_v1_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return 0;
  }

  return (uint64_t)(ATGW_CONTEXT(context)->get_crypt_handshake()->secret.size());
}

LIBATGW_V1_C_API uint64_t __cdecl libatgw_v1_c_copy_crypt_secret(libatgw_v1_c_context context, unsigned char *secret,
                                                                 uint64_t available_size) {
  if (ATGW_CONTEXT_IS_NULL(context) || 0 == available_size) {
    return 0;
  }

  size_t len = ATGW_CONTEXT(context)->get_crypt_handshake()->secret.size();
  if (len >= available_size) {
    len = (size_t)available_size;
  }

  memcpy(secret, ATGW_CONTEXT(context)->get_crypt_handshake()->secret.data(), len);
  return len;
}

LIBATGW_V1_C_API uint32_t __cdecl libatgw_v1_c_get_crypt_keybits(libatgw_v1_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return 0;
  }

  return ATGW_CONTEXT(context)->get_crypt_handshake()->cipher.get_key_bits();
}

LIBATGW_V1_C_API void __cdecl libatgw_v1_c_read_alloc(libatgw_v1_c_context context, uint64_t suggested_size,
                                                      char **out_buf, uint64_t *out_len) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return;
  }

  char *co = nullptr;
  size_t colen = 0;
  if (nullptr == out_buf) {
    out_buf = &co;
  }

  ATGW_CONTEXT(context)->alloc_recv_buffer(suggested_size, *out_buf, colen);

  if (nullptr != out_len) {
    *out_len = colen;
  }
}

LIBATGW_V1_C_API void __cdecl libatgw_v1_c_read(libatgw_v1_c_context context, int32_t ssz, const char *buff,
                                                uint64_t len, int32_t *errcode) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return;
  }

  int32_t ecd = 0;

  ATGW_CONTEXT(context)->read(ssz, buff, (size_t)len, ecd);

  if (nullptr != errcode) {
    *errcode = ecd;
  }
}

LIBATGW_V1_C_API int32_t __cdecl libatgw_v1_c_write_done(libatgw_v1_c_context context, int32_t status) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return ::atframework::gateway::error_code_t::EN_ECT_PARAM;
  }

  return ATGW_CONTEXT(context)->write_done(status);
}

LIBATGW_V1_C_API int32_t __cdecl libatgw_v1_c_post_msg(libatgw_v1_c_context context, const void *out_buf,
                                                       uint64_t out_len) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return ::atframework::gateway::error_code_t::EN_ECT_PARAM;
  }

  return ATGW_CONTEXT(context)->send_post(out_buf, out_len);
}

LIBATGW_V1_C_API int32_t __cdecl libatgw_v1_c_send_ping(libatgw_v1_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return ::atframework::gateway::error_code_t::EN_ECT_PARAM;
  }

  return ATGW_CONTEXT(context)->send_ping();
}

LIBATGW_V1_C_API int64_t __cdecl libatgw_v1_c_get_ping_delta(libatgw_v1_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return 0;
  }

  return (int64_t)(ATGW_CONTEXT(context)->get_last_ping().last_delta);
}

LIBATGW_V1_C_API int32_t __cdecl libatgw_v1_c_close(libatgw_v1_c_context context, int32_t reason) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return ::atframework::gateway::error_code_t::EN_ECT_PARAM;
  }

  return ATGW_CONTEXT(context)->close(reason);
}

LIBATGW_V1_C_API int32_t __cdecl libatgw_v1_c_is_closing(libatgw_v1_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return 0;
  }

  return ATGW_CONTEXT(context)->check_flag(::atframework::gateway::libatgw_protocol_api::flag_t::EN_PFT_CLOSING);
}

LIBATGW_V1_C_API int32_t __cdecl libatgw_v1_c_is_closed(libatgw_v1_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return 0;
  }

  return ATGW_CONTEXT(context)->check_flag(::atframework::gateway::libatgw_protocol_api::flag_t::EN_PFT_CLOSED);
}

LIBATGW_V1_C_API int32_t __cdecl libatgw_v1_c_is_handshake_updating(libatgw_v1_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return 0;
  }

  return ATGW_CONTEXT(context)->check_flag(
      ::atframework::gateway::libatgw_protocol_api::flag_t::EN_PFT_HANDSHAKE_UPDATE);
}

LIBATGW_V1_C_API int32_t __cdecl libatgw_v1_c_is_handshake_done(libatgw_v1_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return 0;
  }

  return ATGW_CONTEXT(context)->check_flag(::atframework::gateway::libatgw_protocol_api::flag_t::EN_PFT_HANDSHAKE_DONE);
}

LIBATGW_V1_C_API int32_t __cdecl libatgw_v1_c_is_writing(libatgw_v1_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return 0;
  }

  return ATGW_CONTEXT(context)->check_flag(::atframework::gateway::libatgw_protocol_api::flag_t::EN_PFT_WRITING);
}

LIBATGW_V1_C_API int32_t __cdecl libatgw_v1_c_is_in_callback(libatgw_v1_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return 0;
  }

  return ATGW_CONTEXT(context)->check_flag(::atframework::gateway::libatgw_protocol_api::flag_t::EN_PFT_IN_CALLBACK);
}

#ifdef __cplusplus
}
#endif
