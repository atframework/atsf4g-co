#include "libatgw_inner_v1_c.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "common/compiler_message.h"
#include "common/string_oprs.h"

#include <inner_v1/libatgw_proto_inner.h>

#define ATGW_CONTEXT(x) ((::atframe::gateway::libatgw_proto_inner_v1 *)(x))
#define ATGW_CONTEXT_IS_NULL(x) (NULL == (x))

struct g_libatgw_inner_v1_c_callbacks_t {
  ::atframe::gateway::proto_base::proto_callbacks_t callbacks;
  libatgw_inner_v1_c_on_write_start_fn_t write_start_fn;
  libatgw_inner_v1_c_on_message_fn_t on_message_fn;
  libatgw_inner_v1_c_on_init_new_session_fn_t on_init_new_session_fn;
  libatgw_inner_v1_c_on_init_reconnect_fn_t on_init_reconnect_fn;
  libatgw_inner_v1_c_on_close_fn_t on_close_fn;
  libatgw_inner_v1_c_on_handshake_done_fn_t on_handshake_done_fn;
  libatgw_inner_v1_c_on_handshake_done_fn_t on_handshake_update_fn;
  libatgw_inner_v1_c_on_error_fn_t on_error_fn;

  g_libatgw_inner_v1_c_callbacks_t()
      : write_start_fn(NULL),
        on_message_fn(NULL),
        on_init_new_session_fn(NULL),
        on_init_reconnect_fn(NULL),
        on_close_fn(NULL),
        on_handshake_done_fn(NULL),
        on_error_fn(NULL) {}
};

static g_libatgw_inner_v1_c_callbacks_t *libatgw_inner_v1_c_get_c_callbacks() {
  static g_libatgw_inner_v1_c_callbacks_t cbks;
  return &cbks;
}

static int32_t proto_inner_callback_on_write(::atframe::gateway::proto_base *proto, void *buffer, size_t sz,
                                             bool *is_done) {
  if (NULL == buffer || 0 == sz) {
    if (NULL != is_done) {
      *is_done = true;
    }
    return ::atframe::gateway::error_code_t::EN_ECT_PARAM;
  }

  libatgw_inner_v1_c_on_write_start_fn_t fn = libatgw_inner_v1_c_get_c_callbacks()->write_start_fn;
  if (NULL == fn) {
    if (NULL != is_done) {
      *is_done = true;
    }
    return ::atframe::gateway::error_code_t::EN_ECT_MISS_CALLBACKS;
  }

  int32_t is_done_i = 0;
  libatgw_inner_v1_c_context context;
  context = proto;
  int32_t ret = fn(context, buffer, sz, &is_done_i);
  if (NULL != is_done) {
    *is_done = !!is_done_i;
  }

  return ret;
}

static int32_t proto_inner_callback_on_message(::atframe::gateway::proto_base *proto, const void *buffer, size_t sz) {
  libatgw_inner_v1_c_on_message_fn_t fn = libatgw_inner_v1_c_get_c_callbacks()->on_message_fn;
  if (NULL != fn) {
    libatgw_inner_v1_c_context context;
    context = proto;
    fn(context, buffer, sz);
  }

  return 0;
}

// useless
static int32_t proto_inner_callback_on_new_session(::atframe::gateway::proto_base *proto, uint64_t &sess_id) {
  libatgw_inner_v1_c_on_init_new_session_fn_t fn = libatgw_inner_v1_c_get_c_callbacks()->on_init_new_session_fn;
  if (NULL != fn) {
    libatgw_inner_v1_c_context context;
    context = proto;
    fn(context, &sess_id);
  }

  return 0;
}

// useless
static int32_t proto_inner_callback_on_reconnect(::atframe::gateway::proto_base *proto, uint64_t sess_id) {
  libatgw_inner_v1_c_on_init_reconnect_fn_t fn = libatgw_inner_v1_c_get_c_callbacks()->on_init_reconnect_fn;
  if (NULL != fn) {
    libatgw_inner_v1_c_context context;
    context = proto;
    fn(context, sess_id);
  }

  return 0;
}

static int32_t proto_inner_callback_on_close(::atframe::gateway::proto_base *proto, int32_t reason) {
  libatgw_inner_v1_c_on_close_fn_t fn = libatgw_inner_v1_c_get_c_callbacks()->on_close_fn;
  if (NULL != fn) {
    libatgw_inner_v1_c_context context;
    context = proto;
    fn(context, reason);
  }

  return 0;
}

static int32_t proto_inner_callback_on_handshake(::atframe::gateway::proto_base *proto, int32_t status) {
  libatgw_inner_v1_c_on_handshake_done_fn_t fn = libatgw_inner_v1_c_get_c_callbacks()->on_handshake_done_fn;
  if (NULL != fn) {
    libatgw_inner_v1_c_context context;
    context = proto;
    fn(context, status);
  }

  return 0;
}

static int32_t proto_inner_callback_on_handshake_update(::atframe::gateway::proto_base *proto, int32_t status) {
  libatgw_inner_v1_c_on_handshake_done_fn_t fn = libatgw_inner_v1_c_get_c_callbacks()->on_handshake_update_fn;
  if (NULL != fn) {
    libatgw_inner_v1_c_context context;
    context = proto;
    fn(context, status);
  }

  return 0;
}

static int32_t proto_inner_callback_on_error(::atframe::gateway::proto_base *proto, const char *filename, int32_t line,
                                             int32_t errcode, const char *errmsg) {
  libatgw_inner_v1_c_on_error_fn_t fn = libatgw_inner_v1_c_get_c_callbacks()->on_error_fn;
  if (NULL != fn) {
    libatgw_inner_v1_c_context context;
    context = proto;
    fn(context, filename, line, errcode, errmsg);
  }

  return 0;
}

static ::atframe::gateway::proto_base::proto_callbacks_t *libatgw_inner_v1_c_get_proto_callbacks() {
  g_libatgw_inner_v1_c_callbacks_t *cbks = libatgw_inner_v1_c_get_c_callbacks();

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

UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_global_init_algorithms() {
  util::crypto::cipher::init_global_algorithm();
}
UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_global_cleanup_algorithms() {
  util::crypto::cipher::cleanup_global_algorithm();
}

UTIL_SYMBOL_EXPORT uint64_t __cdecl libatgw_inner_v1_c_global_get_crypt_size() {
  return static_cast<uint64_t>(util::crypto::cipher::get_all_cipher_names().size());
}

UTIL_SYMBOL_EXPORT const char *__cdecl libatgw_inner_v1_c_global_get_crypt_name(uint64_t idx) {
  const std::vector<std::string> &res = util::crypto::cipher::get_all_cipher_names();
  if (idx >= res.size()) {
    return NULL;
  }

  return res[idx].c_str();
}

UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_gset_on_write_start_fn(libatgw_inner_v1_c_on_write_start_fn_t fn) {
  libatgw_inner_v1_c_get_c_callbacks()->write_start_fn = fn;
}

UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_gset_on_message_fn(libatgw_inner_v1_c_on_message_fn_t fn) {
  libatgw_inner_v1_c_get_c_callbacks()->on_message_fn = fn;
}

UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_gset_on_init_new_session_fn(
    libatgw_inner_v1_c_on_init_new_session_fn_t fn) {
  libatgw_inner_v1_c_get_c_callbacks()->on_init_new_session_fn = fn;
}

UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_gset_on_init_reconnect_fn(
    libatgw_inner_v1_c_on_init_reconnect_fn_t fn) {
  libatgw_inner_v1_c_get_c_callbacks()->on_init_reconnect_fn = fn;
}

UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_gset_on_close_fn(libatgw_inner_v1_c_on_close_fn_t fn) {
  libatgw_inner_v1_c_get_c_callbacks()->on_close_fn = fn;
}

UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_gset_on_handshake_done_fn(
    libatgw_inner_v1_c_on_handshake_done_fn_t fn) {
  libatgw_inner_v1_c_get_c_callbacks()->on_handshake_done_fn = fn;
}

UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_gset_on_handshake_update_fn(
    libatgw_inner_v1_c_on_handshake_done_fn_t fn) {
  libatgw_inner_v1_c_get_c_callbacks()->on_handshake_update_fn = fn;
}

UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_gset_on_error_fn(libatgw_inner_v1_c_on_error_fn_t fn) {
  libatgw_inner_v1_c_get_c_callbacks()->on_error_fn = fn;
}

UTIL_SYMBOL_EXPORT libatgw_inner_v1_c_context __cdecl libatgw_inner_v1_c_create() {
  libatgw_inner_v1_c_context ret;
  assert(sizeof(void *) == sizeof(libatgw_inner_v1_c_context));

  atframe::gateway::libatgw_proto_inner_v1 *res = new (std::nothrow) atframe::gateway::libatgw_proto_inner_v1();
  if (NULL != res) {
    res->set_callbacks(libatgw_inner_v1_c_get_proto_callbacks());
  }

  ret = res;
  return ret;
}

UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_destroy(libatgw_inner_v1_c_context context) {
  delete ATGW_CONTEXT(context);
}

UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_set_recv_buffer_limit(libatgw_inner_v1_c_context context,
                                                                         uint64_t max_size, uint64_t max_number) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return;
  }

  ATGW_CONTEXT(context)->set_recv_buffer_limit((size_t)max_size, (size_t)max_number);
}

UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_set_send_buffer_limit(libatgw_inner_v1_c_context context,
                                                                         uint64_t max_size, uint64_t max_number) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return;
  }

  ATGW_CONTEXT(context)->set_send_buffer_limit((size_t)max_size, (size_t)max_number);
}

UTIL_SYMBOL_EXPORT int32_t __cdecl libatgw_inner_v1_c_start_session(libatgw_inner_v1_c_context context,
                                                                    const char *crypt_type) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return ::atframe::gateway::error_code_t::EN_ECT_PARAM;
  }

  return ATGW_CONTEXT(context)->start_session(crypt_type);
}

UTIL_SYMBOL_EXPORT int32_t __cdecl libatgw_inner_v1_c_reconnect_session(libatgw_inner_v1_c_context context,
                                                                        uint64_t sessios_id, const char *crypt_type,
                                                                        const unsigned char *secret_buf,
                                                                        uint64_t secret_len) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return ::atframe::gateway::error_code_t::EN_ECT_PARAM;
  }

  std::vector<unsigned char> secret;
  secret.assign(secret_buf, secret_buf + secret_len);
  return ATGW_CONTEXT(context)->reconnect_session(sessios_id, crypt_type, secret);
}

UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_get_info(libatgw_inner_v1_c_context context, char *info_str,
                                                            uint64_t info_len) {
  if (NULL == info_str || 0 == info_len) {
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

UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_set_private_data(libatgw_inner_v1_c_context context, void *p) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return;
  }

  return ATGW_CONTEXT(context)->set_private_data(p);
}

UTIL_SYMBOL_EXPORT void *__cdecl libatgw_inner_v1_c_get_private_data(libatgw_inner_v1_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return NULL;
  }

  return ATGW_CONTEXT(context)->get_private_data();
}

UTIL_SYMBOL_EXPORT uint64_t __cdecl libatgw_inner_v1_c_get_session_id(libatgw_inner_v1_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return 0;
  }

  return ATGW_CONTEXT(context)->get_session_id();
}

UTIL_SYMBOL_EXPORT const char *__cdecl libatgw_inner_v1_c_get_crypt_type(libatgw_inner_v1_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return 0;
  }

  return ATGW_CONTEXT(context)->get_crypt_handshake()->type.c_str();
}

UTIL_SYMBOL_EXPORT uint64_t __cdecl libatgw_inner_v1_c_get_crypt_secret_size(libatgw_inner_v1_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return 0;
  }

  return (uint64_t)(ATGW_CONTEXT(context)->get_crypt_handshake()->secret.size());
}

UTIL_SYMBOL_EXPORT uint64_t __cdecl libatgw_inner_v1_c_copy_crypt_secret(libatgw_inner_v1_c_context context,
                                                                         unsigned char *secret,
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

UTIL_SYMBOL_EXPORT uint32_t __cdecl libatgw_inner_v1_c_get_crypt_keybits(libatgw_inner_v1_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return 0;
  }

  return ATGW_CONTEXT(context)->get_crypt_handshake()->cipher.get_key_bits();
}

UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_read_alloc(libatgw_inner_v1_c_context context,
                                                              uint64_t suggested_size, char **out_buf,
                                                              uint64_t *out_len) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return;
  }

  char *co = NULL;
  size_t colen = 0;
  if (NULL == out_buf) {
    out_buf = &co;
  }

  ATGW_CONTEXT(context)->alloc_recv_buffer(suggested_size, *out_buf, colen);

  if (NULL != out_len) {
    *out_len = colen;
  }
}

UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_read(libatgw_inner_v1_c_context context, int32_t ssz,
                                                        const char *buff, uint64_t len, int32_t *errcode) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return;
  }

  int32_t ecd = 0;

  ATGW_CONTEXT(context)->read(ssz, buff, (size_t)len, ecd);

  if (NULL != errcode) {
    *errcode = ecd;
  }
}

UTIL_SYMBOL_EXPORT int32_t __cdecl libatgw_inner_v1_c_write_done(libatgw_inner_v1_c_context context, int32_t status) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return ::atframe::gateway::error_code_t::EN_ECT_PARAM;
  }

  return ATGW_CONTEXT(context)->write_done(status);
}

UTIL_SYMBOL_EXPORT int32_t __cdecl libatgw_inner_v1_c_post_msg(libatgw_inner_v1_c_context context, const void *out_buf,
                                                               uint64_t out_len) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return ::atframe::gateway::error_code_t::EN_ECT_PARAM;
  }

  return ATGW_CONTEXT(context)->send_post(out_buf, out_len);
}

UTIL_SYMBOL_EXPORT int32_t __cdecl libatgw_inner_v1_c_send_ping(libatgw_inner_v1_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return ::atframe::gateway::error_code_t::EN_ECT_PARAM;
  }

  return ATGW_CONTEXT(context)->send_ping();
}

UTIL_SYMBOL_EXPORT int64_t __cdecl libatgw_inner_v1_c_get_ping_delta(libatgw_inner_v1_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return 0;
  }

  return (int64_t)(ATGW_CONTEXT(context)->get_last_ping().last_delta);
}

UTIL_SYMBOL_EXPORT int32_t __cdecl libatgw_inner_v1_c_close(libatgw_inner_v1_c_context context, int32_t reason) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return ::atframe::gateway::error_code_t::EN_ECT_PARAM;
  }

  return ATGW_CONTEXT(context)->close(reason);
}

UTIL_SYMBOL_EXPORT int32_t __cdecl libatgw_inner_v1_c_is_closing(libatgw_inner_v1_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return 0;
  }

  return ATGW_CONTEXT(context)->check_flag(::atframe::gateway::proto_base::flag_t::EN_PFT_CLOSING);
}

UTIL_SYMBOL_EXPORT int32_t __cdecl libatgw_inner_v1_c_is_closed(libatgw_inner_v1_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return 0;
  }

  return ATGW_CONTEXT(context)->check_flag(::atframe::gateway::proto_base::flag_t::EN_PFT_CLOSED);
}

UTIL_SYMBOL_EXPORT int32_t __cdecl libatgw_inner_v1_c_is_handshake_updating(libatgw_inner_v1_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return 0;
  }

  return ATGW_CONTEXT(context)->check_flag(::atframe::gateway::proto_base::flag_t::EN_PFT_HANDSHAKE_UPDATE);
}

UTIL_SYMBOL_EXPORT int32_t __cdecl libatgw_inner_v1_c_is_handshake_done(libatgw_inner_v1_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return 0;
  }

  return ATGW_CONTEXT(context)->check_flag(::atframe::gateway::proto_base::flag_t::EN_PFT_HANDSHAKE_DONE);
}

UTIL_SYMBOL_EXPORT int32_t __cdecl libatgw_inner_v1_c_is_writing(libatgw_inner_v1_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return 0;
  }

  return ATGW_CONTEXT(context)->check_flag(::atframe::gateway::proto_base::flag_t::EN_PFT_WRITING);
}

UTIL_SYMBOL_EXPORT int32_t __cdecl libatgw_inner_v1_c_is_in_callback(libatgw_inner_v1_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return 0;
  }

  return ATGW_CONTEXT(context)->check_flag(::atframe::gateway::proto_base::flag_t::EN_PFT_IN_CALLBACK);
}

#ifdef __cplusplus
}
#endif