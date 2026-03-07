// Copyright 2021 atframework
// Created by owent on 2016/9/29.
//

#include "libatgateway_v2_c.h"

#include <atgateway/protocol/v2/libatgw_protocol_sdk.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "nostd/string_view.h"

#define ATGW_CONTEXT(x) (reinterpret_cast<::atframework::gateway::libatgw_protocol_sdk *>(x))
#define ATGW_CONTEXT_IS_NULL(x) (nullptr == (x))

namespace {

struct g_libatgateway_v2_c_callbacks_t {
  ::atframework::gateway::libatgw_protocol_api::proto_callbacks_t callbacks;
  libatgateway_v2_c_on_write_start_fn_t write_start_fn;
  libatgateway_v2_c_on_message_fn_t on_message_fn;
  libatgateway_v2_c_on_init_new_session_fn_t on_init_new_session_fn;
  libatgateway_v2_c_on_init_reconnect_fn_t on_init_reconnect_fn;
  libatgateway_v2_c_on_close_fn_t on_close_fn;
  libatgateway_v2_c_on_handshake_done_fn_t on_handshake_done_fn;
  libatgateway_v2_c_on_handshake_done_fn_t on_handshake_update_fn;
  libatgateway_v2_c_on_error_fn_t on_error_fn;

  g_libatgateway_v2_c_callbacks_t()
      : write_start_fn(nullptr),
        on_message_fn(nullptr),
        on_init_new_session_fn(nullptr),
        on_init_reconnect_fn(nullptr),
        on_close_fn(nullptr),
        on_handshake_done_fn(nullptr),
        on_handshake_update_fn(nullptr),
        on_error_fn(nullptr) {}
};

static g_libatgateway_v2_c_callbacks_t *libatgateway_v2_c_get_c_callbacks() {
  static g_libatgateway_v2_c_callbacks_t cbks;
  return &cbks;
}

static int32_t proto_inner_callback_on_write(::atframework::gateway::libatgw_protocol_api *proto,
                                             gsl::span<unsigned char> buffer, bool *is_done) {
  if (buffer.empty()) {
    if (nullptr != is_done) {
      *is_done = true;
    }
    return static_cast<int32_t>(::atframework::gateway::error_code_t::kParam);
  }

  libatgateway_v2_c_on_write_start_fn_t fn = libatgateway_v2_c_get_c_callbacks()->write_start_fn;
  if (nullptr == fn) {
    if (nullptr != is_done) {
      *is_done = true;
    }
    return static_cast<int32_t>(::atframework::gateway::error_code_t::kMissCallbacks);
  }

  int32_t is_done_i = 0;
  libatgateway_v2_c_context context = proto;
  int32_t ret = fn(context, buffer.data(), static_cast<uint64_t>(buffer.size()), &is_done_i);
  if (nullptr != is_done) {
    *is_done = (is_done_i != 0);
  }

  return ret;
}

static int32_t proto_inner_callback_on_message(::atframework::gateway::libatgw_protocol_api *proto,
                                               gsl::span<const unsigned char> buffer) {
  libatgateway_v2_c_on_message_fn_t fn = libatgateway_v2_c_get_c_callbacks()->on_message_fn;
  if (nullptr != fn) {
    libatgateway_v2_c_context context = proto;
    fn(context, buffer.data(), static_cast<uint64_t>(buffer.size()));
  }

  return 0;
}

// useless
static int32_t proto_inner_callback_on_new_session(::atframework::gateway::libatgw_protocol_api *proto,
                                                   uint64_t &sess_id) {
  libatgateway_v2_c_on_init_new_session_fn_t fn = libatgateway_v2_c_get_c_callbacks()->on_init_new_session_fn;
  if (nullptr != fn) {
    libatgateway_v2_c_context context = proto;
    fn(context, &sess_id);
  }

  return 0;
}

// useless
static int32_t proto_inner_callback_on_reconnect(::atframework::gateway::libatgw_protocol_api *proto,
                                                 uint64_t sess_id) {
  libatgateway_v2_c_on_init_reconnect_fn_t fn = libatgateway_v2_c_get_c_callbacks()->on_init_reconnect_fn;
  if (nullptr != fn) {
    libatgateway_v2_c_context context = proto;
    fn(context, sess_id);
  }

  return 0;
}

static int32_t proto_inner_callback_on_close(::atframework::gateway::libatgw_protocol_api *proto, int32_t reason,
                                             int32_t sub_reason, atfw::util::nostd::string_view message) {
  libatgateway_v2_c_on_close_fn_t fn = libatgateway_v2_c_get_c_callbacks()->on_close_fn;
  if (nullptr != fn) {
    libatgateway_v2_c_context context = proto;
    fn(context, reason, sub_reason, message.data(), static_cast<uint64_t>(message.size()));
  }

  return 0;
}

static int32_t proto_inner_callback_on_handshake(::atframework::gateway::libatgw_protocol_api *proto, int32_t status) {
  libatgateway_v2_c_on_handshake_done_fn_t fn = libatgateway_v2_c_get_c_callbacks()->on_handshake_done_fn;
  if (nullptr != fn) {
    libatgateway_v2_c_context context = proto;
    fn(context, status);
  }

  return 0;
}

static int32_t proto_inner_callback_on_handshake_update(::atframework::gateway::libatgw_protocol_api *proto,
                                                        int32_t status) {
  libatgateway_v2_c_on_handshake_done_fn_t fn = libatgateway_v2_c_get_c_callbacks()->on_handshake_update_fn;
  if (nullptr != fn) {
    libatgateway_v2_c_context context = proto;
    fn(context, status);
  }

  return 0;
}

static int32_t proto_inner_callback_on_error(::atframework::gateway::libatgw_protocol_api *proto, const char *filename,
                                             int32_t line, int32_t errcode, const char *errmsg) {
  libatgateway_v2_c_on_error_fn_t fn = libatgateway_v2_c_get_c_callbacks()->on_error_fn;
  if (nullptr != fn) {
    libatgateway_v2_c_context context = proto;
    fn(context, filename, line, errcode, errmsg);
  }

  return 0;
}

static ::atframework::gateway::libatgw_protocol_api::proto_callbacks_t *libatgateway_v2_c_get_proto_callbacks() {
  g_libatgateway_v2_c_callbacks_t *cbks = libatgateway_v2_c_get_c_callbacks();

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

}  // namespace

#ifdef __cplusplus
extern "C" {
#endif

LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_global_init_algorithms() {
  ::atfw::util::crypto::cipher::init_global_algorithm();
}
LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_global_cleanup_algorithms() {
  ::atfw::util::crypto::cipher::cleanup_global_algorithm();
}

LIBATGATEWAY_V2_C_API uint64_t __cdecl libatgateway_v2_c_global_get_crypto_size() {
  return static_cast<uint64_t>(::atfw::util::crypto::cipher::get_all_cipher_names().size());
}

LIBATGATEWAY_V2_C_API const char *__cdecl libatgateway_v2_c_global_get_crypto_name(uint64_t idx) {
  const std::vector<std::string> &res = ::atfw::util::crypto::cipher::get_all_cipher_names();
  if (idx >= res.size()) {
    return nullptr;
  }

  return res[idx].c_str();
}

LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_gset_on_write_start_fn(libatgateway_v2_c_on_write_start_fn_t fn) {
  libatgateway_v2_c_get_c_callbacks()->write_start_fn = fn;
}

LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_gset_on_message_fn(libatgateway_v2_c_on_message_fn_t fn) {
  libatgateway_v2_c_get_c_callbacks()->on_message_fn = fn;
}

LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_gset_on_init_new_session_fn(
    libatgateway_v2_c_on_init_new_session_fn_t fn) {
  libatgateway_v2_c_get_c_callbacks()->on_init_new_session_fn = fn;
}

LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_gset_on_init_reconnect_fn(
    libatgateway_v2_c_on_init_reconnect_fn_t fn) {
  libatgateway_v2_c_get_c_callbacks()->on_init_reconnect_fn = fn;
}

LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_gset_on_close_fn(libatgateway_v2_c_on_close_fn_t fn) {
  libatgateway_v2_c_get_c_callbacks()->on_close_fn = fn;
}

LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_gset_on_handshake_done_fn(
    libatgateway_v2_c_on_handshake_done_fn_t fn) {
  libatgateway_v2_c_get_c_callbacks()->on_handshake_done_fn = fn;
}

LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_gset_on_handshake_update_fn(
    libatgateway_v2_c_on_handshake_done_fn_t fn) {
  libatgateway_v2_c_get_c_callbacks()->on_handshake_update_fn = fn;
}

LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_gset_on_error_fn(libatgateway_v2_c_on_error_fn_t fn) {
  libatgateway_v2_c_get_c_callbacks()->on_error_fn = fn;
}

// Thread-local storage for the C SDK's crypto configuration
namespace {
static std::shared_ptr<::atframework::gateway::v2::crypto_shared_context_t> &libatgateway_v2_c_get_global_conf() {
  static std::shared_ptr<::atframework::gateway::v2::crypto_shared_context_t> inst;
  return inst;
}
}  // namespace

LIBATGATEWAY_V2_C_API libatgateway_v2_c_context __cdecl libatgateway_v2_c_create() {
  libatgateway_v2_c_context ret = nullptr;
  assert(sizeof(void *) == sizeof(libatgateway_v2_c_context));

  auto global_conf = libatgateway_v2_c_get_global_conf();
  ::atframework::gateway::libatgw_protocol_sdk *res =
      new (std::nothrow)::atframework::gateway::libatgw_protocol_sdk(global_conf);
  if (nullptr != res) {
    res->set_callbacks(libatgateway_v2_c_get_proto_callbacks());
  }

  ret = res;
  return ret;
}

LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_destroy(libatgateway_v2_c_context context) {
  delete ATGW_CONTEXT(context);
}

LIBATGATEWAY_V2_C_API uint64_t __cdecl libatgateway_v2_c_get_session_id(libatgateway_v2_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return 0;
  }

  return ATGW_CONTEXT(context)->get_session_id();
}

LIBATGATEWAY_V2_C_API const unsigned char *__cdecl libatgateway_v2_c_get_session_token(
    libatgateway_v2_c_context context, uint64_t *size) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    if (size != nullptr) {
      *size = 0;
    }
    return nullptr;
  }

  gsl::span<const unsigned char> token = ATGW_CONTEXT(context)->get_session_token();
  if (size != nullptr) {
    *size = static_cast<uint64_t>(token.size());
  }
  return token.data();
}

LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_set_receive_buffer_limit(libatgateway_v2_c_context context,
                                                                              uint64_t max_size, uint64_t max_number) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return;
  }

  ATGW_CONTEXT(context)->set_receive_buffer_limit(static_cast<size_t>(max_size), static_cast<size_t>(max_number));
}

LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_set_send_buffer_limit(libatgateway_v2_c_context context,
                                                                           uint64_t max_size, uint64_t max_number) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return;
  }

  ATGW_CONTEXT(context)->set_send_buffer_limit(static_cast<size_t>(max_size), static_cast<size_t>(max_number));
}

LIBATGATEWAY_V2_C_API int32_t __cdecl libatgateway_v2_c_start_session(libatgateway_v2_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return static_cast<int32_t>(::atframework::gateway::error_code_t::kParam);
  }

  return ATGW_CONTEXT(context)->start_session();
}

LIBATGATEWAY_V2_C_API int32_t __cdecl libatgateway_v2_c_reconnect_session(libatgateway_v2_c_context context,
                                                                          uint64_t sessios_id,
                                                                          const unsigned char *secret_buf,
                                                                          uint64_t secret_len) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return static_cast<int32_t>(::atframework::gateway::error_code_t::kParam);
  }

  std::vector<unsigned char> secret;
  if (nullptr != secret_buf && secret_len > 0) {
    secret.assign(secret_buf, secret_buf + secret_len);
  }
  return ATGW_CONTEXT(context)->reconnect_session(sessios_id, secret);
}

LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_get_info(libatgateway_v2_c_context context, char *info_str,
                                                              uint64_t info_len) {
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
  info_str[len] = 0;
}

LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_set_private_data(libatgateway_v2_c_context context, void *p) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return;
  }

  ATGW_CONTEXT(context)->set_private_data(p);
}

LIBATGATEWAY_V2_C_API void *__cdecl libatgateway_v2_c_get_private_data(libatgateway_v2_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return 0;
  }

  return ATGW_CONTEXT(context)->get_private_data();
}

LIBATGATEWAY_V2_C_API int32_t __cdecl libatgateway_v2_c_get_crypto_algorithm(libatgateway_v2_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return 0;
  }

  auto session = ATGW_CONTEXT(context)->get_crypto_session();
  if (!session) {
    return 0;
  }
  return static_cast<int32_t>(session->selected_algorithm);
}

LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_read_alloc(libatgateway_v2_c_context context,
                                                                uint64_t suggested_size, char **out_buf,
                                                                uint64_t *out_len) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return;
  }

  char *co = nullptr;
  size_t colen = 0;
  if (nullptr == out_buf) {
    out_buf = &co;
  }

  ATGW_CONTEXT(context)->alloc_receive_buffer(suggested_size, *out_buf, colen);

  if (nullptr != out_len) {
    *out_len = colen;
  }
}

LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_read(libatgateway_v2_c_context context, int32_t ssz,
                                                          const char *buff, uint64_t len, int32_t *errcode) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return;
  }

  int32_t ecd = 0;
  gsl::span<const unsigned char> span_buff;
  if (nullptr != buff && len > 0) {
    span_buff = gsl::span<const unsigned char>{reinterpret_cast<const unsigned char *>(buff), static_cast<size_t>(len)};
  }

  ATGW_CONTEXT(context)->read(ssz, span_buff, ecd);

  if (nullptr != errcode) {
    *errcode = ecd;
  }
}

LIBATGATEWAY_V2_C_API int32_t __cdecl libatgateway_v2_c_write_done(libatgateway_v2_c_context context, int32_t status) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return static_cast<int32_t>(::atframework::gateway::error_code_t::kParam);
  }

  return ATGW_CONTEXT(context)->write_done(status);
}

LIBATGATEWAY_V2_C_API int32_t __cdecl libatgateway_v2_c_post_msg(libatgateway_v2_c_context context, const void *out_buf,
                                                                 uint64_t out_len) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return static_cast<int32_t>(::atframework::gateway::error_code_t::kParam);
  }

  gsl::span<const unsigned char> span_buff;
  if (nullptr != out_buf && out_len > 0) {
    span_buff =
        gsl::span<const unsigned char>{reinterpret_cast<const unsigned char *>(out_buf), static_cast<size_t>(out_len)};
  }

  return ATGW_CONTEXT(context)->send_post(span_buff);
}

LIBATGATEWAY_V2_C_API int32_t __cdecl libatgateway_v2_c_send_ping(libatgateway_v2_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return static_cast<int32_t>(::atframework::gateway::error_code_t::kParam);
  }

  return ATGW_CONTEXT(context)->send_ping();
}

LIBATGATEWAY_V2_C_API int64_t __cdecl libatgateway_v2_c_get_ping_delta(libatgateway_v2_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return 0;
  }

  return static_cast<int64_t>(ATGW_CONTEXT(context)->get_last_ping().last_delta);
}

LIBATGATEWAY_V2_C_API int32_t __cdecl libatgateway_v2_c_close(libatgateway_v2_c_context context, int32_t reason) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return static_cast<int32_t>(::atframework::gateway::error_code_t::kParam);
  }

  return ATGW_CONTEXT(context)->close(reason);
}

LIBATGATEWAY_V2_C_API int32_t __cdecl libatgateway_v2_c_is_closing(libatgateway_v2_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return 0;
  }

  return ATGW_CONTEXT(context)->check_flag(::atframework::gateway::libatgw_protocol_api::flag_t::kClosing);
}

LIBATGATEWAY_V2_C_API int32_t __cdecl libatgateway_v2_c_is_closed(libatgateway_v2_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return 0;
  }

  return ATGW_CONTEXT(context)->check_flag(::atframework::gateway::libatgw_protocol_api::flag_t::kClosed);
}

LIBATGATEWAY_V2_C_API int32_t __cdecl libatgateway_v2_c_is_handshake_updating(libatgateway_v2_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return 0;
  }

  return ATGW_CONTEXT(context)->check_flag(::atframework::gateway::libatgw_protocol_api::flag_t::kHandshakeUpdate);
}

LIBATGATEWAY_V2_C_API int32_t __cdecl libatgateway_v2_c_is_handshake_done(libatgateway_v2_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return 0;
  }

  return ATGW_CONTEXT(context)->check_flag(::atframework::gateway::libatgw_protocol_api::flag_t::kHandshakeDone);
}

LIBATGATEWAY_V2_C_API int32_t __cdecl libatgateway_v2_c_is_writing(libatgateway_v2_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return 0;
  }

  return ATGW_CONTEXT(context)->check_flag(::atframework::gateway::libatgw_protocol_api::flag_t::kWriting);
}

LIBATGATEWAY_V2_C_API int32_t __cdecl libatgateway_v2_c_is_in_callback(libatgateway_v2_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return 0;
  }

  return ATGW_CONTEXT(context)->check_flag(::atframework::gateway::libatgw_protocol_api::flag_t::kInCallback);
}

// ========== Algorithm listing APIs ==========

LIBATGATEWAY_V2_C_API uint64_t __cdecl libatgateway_v2_c_get_key_exchange_algorithm_count() {
  return static_cast<uint64_t>(
      ::atframework::gateway::libatgw_protocol_sdk::get_all_key_exchange_algorithm_names().size());
}

LIBATGATEWAY_V2_C_API const char *__cdecl libatgateway_v2_c_get_key_exchange_algorithm_name(uint64_t idx) {
  const auto &names = ::atframework::gateway::libatgw_protocol_sdk::get_all_key_exchange_algorithm_names();
  if (idx >= names.size()) {
    return nullptr;
  }
  return names[idx];
}

LIBATGATEWAY_V2_C_API uint64_t __cdecl libatgateway_v2_c_get_crypto_algorithm_count() {
  return static_cast<uint64_t>(::atframework::gateway::libatgw_protocol_sdk::get_all_crypto_algorithm_names().size());
}

LIBATGATEWAY_V2_C_API const char *__cdecl libatgateway_v2_c_get_crypto_algorithm_name(uint64_t idx) {
  const auto &names = ::atframework::gateway::libatgw_protocol_sdk::get_all_crypto_algorithm_names();
  if (idx >= names.size()) {
    return nullptr;
  }
  return names[idx];
}

LIBATGATEWAY_V2_C_API uint64_t __cdecl libatgateway_v2_c_get_compression_algorithm_count() {
  return static_cast<uint64_t>(
      ::atframework::gateway::libatgw_protocol_sdk::get_all_compression_algorithm_names().size());
}

LIBATGATEWAY_V2_C_API const char *__cdecl libatgateway_v2_c_get_compression_algorithm_name(uint64_t idx) {
  const auto &names = ::atframework::gateway::libatgw_protocol_sdk::get_all_compression_algorithm_names();
  if (idx >= names.size()) {
    return nullptr;
  }
  return names[idx];
}

LIBATGATEWAY_V2_C_API int32_t __cdecl libatgateway_v2_c_set_crypto_algorithm(libatgateway_v2_c_context /*context*/,
                                                                             const char *const *crypto_algorithm_names,
                                                                             uint64_t crypto_algorithms_count) {
  using sdk_t = ::atframework::gateway::libatgw_protocol_sdk;

  auto &global_conf = libatgateway_v2_c_get_global_conf();
  if (!global_conf) {
    // Create new default config, then set crypto fields
    sdk_t::crypto_conf_t conf;
    global_conf = sdk_t::create_shared_context(conf);
  }

  if (!global_conf) {
    return static_cast<int32_t>(::atframework::gateway::error_code_t::kCryptNotSupported);
  }

  std::vector<sdk_t::crypto_algorithm_type> algorithms;
  if (nullptr != crypto_algorithm_names && crypto_algorithms_count > 0) {
    for (uint64_t i = 0; i < crypto_algorithms_count; ++i) {
      auto alg = sdk_t::crypto_algorithm_from_name(crypto_algorithm_names[i]);
      if (alg != ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::crypto_algorithm_t, kNone)) {
        algorithms.push_back(alg);
      }
    }
  }

  return sdk_t::set_shared_context_crypto_algorithm(global_conf,
                                                    gsl::span<const sdk_t::crypto_algorithm_type>(algorithms));
}

LIBATGATEWAY_V2_C_API int32_t __cdecl libatgateway_v2_c_set_key_exchange_algorithm(
    libatgateway_v2_c_context /*context*/, const char *key_exchange_algorithm) {
  using sdk_t = ::atframework::gateway::libatgw_protocol_sdk;

  auto &global_conf = libatgateway_v2_c_get_global_conf();
  if (!global_conf) {
    // Create new default config, then set crypto fields
    sdk_t::crypto_conf_t conf;
    global_conf = sdk_t::create_shared_context(conf);
  }

  auto alg = sdk_t::key_exchange_algorithm_from_name(key_exchange_algorithm);
  return sdk_t::set_shared_context_key_exchange_algorithm(global_conf, alg);
}

LIBATGATEWAY_V2_C_API int32_t __cdecl libatgateway_v2_c_set_crypto_update_interval(
    libatgateway_v2_c_context /*context*/, int64_t update_interval) {
  using sdk_t = ::atframework::gateway::libatgw_protocol_sdk;

  auto &global_conf = libatgateway_v2_c_get_global_conf();
  if (!global_conf) {
    // Create new default config, then set crypto fields
    sdk_t::crypto_conf_t conf;
    global_conf = sdk_t::create_shared_context(conf);
  }

  auto *conf = sdk_t::get_shared_context_mutable_conf(global_conf);
  if (nullptr == conf) {
    return static_cast<int32_t>(::atframework::gateway::error_code_t::kParam);
  }

  conf->update_interval = static_cast<time_t>(update_interval);
  return 0;
}

LIBATGATEWAY_V2_C_API int32_t __cdecl libatgateway_v2_c_set_access_tokens(libatgateway_v2_c_context /*context*/,
                                                                          const unsigned char *const *tokens,
                                                                          const uint64_t *token_sizes,
                                                                          uint64_t token_count) {
  using sdk_t = ::atframework::gateway::libatgw_protocol_sdk;
  auto &global_conf = libatgateway_v2_c_get_global_conf();
  if (!global_conf) {
    // Create default config
    sdk_t::crypto_conf_t conf;
    conf.client_mode = true;
    global_conf = sdk_t::create_shared_context(conf);
    if (!global_conf) {
      return static_cast<int32_t>(::atframework::gateway::error_code_t::kParam);
    }
  }
  auto *conf = sdk_t::get_shared_context_mutable_conf(global_conf);
  if (nullptr == conf) {
    return static_cast<int32_t>(::atframework::gateway::error_code_t::kParam);
  }
  conf->access_tokens.clear();
  if (nullptr != tokens && nullptr != token_sizes) {
    for (uint64_t i = 0; i < token_count; ++i) {
      if (tokens[i] != nullptr && token_sizes[i] > 0) {
        conf->access_tokens.emplace_back(tokens[i], tokens[i] + token_sizes[i]);
      }
    }
  }
  return 0;
}

LIBATGATEWAY_V2_C_API int32_t __cdecl libatgateway_v2_c_set_compression_algorithms(
    libatgateway_v2_c_context /*context*/, const char *const *compression_algorithm_names, uint64_t count) {
  using sdk_t = ::atframework::gateway::libatgw_protocol_sdk;

  auto &global_conf = libatgateway_v2_c_get_global_conf();
  if (!global_conf) {
    // Create new default config, then set crypto fields
    sdk_t::crypto_conf_t conf;
    global_conf = sdk_t::create_shared_context(conf);
  }

  if (!global_conf) {
    return static_cast<int32_t>(::atframework::gateway::error_code_t::kCryptNotSupported);
  }

  std::vector<sdk_t::compression_algorithm_type> algorithms;
  if (nullptr != compression_algorithm_names && count > 0) {
    for (uint64_t i = 0; i < count; ++i) {
      auto alg = sdk_t::compression_algorithm_from_name(compression_algorithm_names[i]);
      if (alg != ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(::atframework::gateway::v2::compression_algorithm_t, kNone)) {
        algorithms.push_back(alg);
      }
    }
  }

  return sdk_t::set_shared_context_compression_algorithm(
      global_conf, gsl::span<const sdk_t::compression_algorithm_type>(algorithms));
}

LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_set_max_post_message_size(libatgateway_v2_c_context /*context*/,
                                                                               uint64_t max_size) {
  using sdk_t = ::atframework::gateway::libatgw_protocol_sdk;
  auto &global_conf = libatgateway_v2_c_get_global_conf();
  if (!global_conf) {
    // Create default config
    sdk_t::crypto_conf_t conf;
    conf.client_mode = true;
    global_conf = sdk_t::create_shared_context(conf);
    if (!global_conf) {
      return;
    }
  }
  auto *conf = sdk_t::get_shared_context_mutable_conf(global_conf);
  if (nullptr == conf) {
    return;
  }
  conf->max_post_message_size = max_size;
}

LIBATGATEWAY_V2_C_API uint64_t __cdecl libatgateway_v2_c_get_write_header_offset(libatgateway_v2_c_context context) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return 0;
  }

  return static_cast<uint64_t>(ATGW_CONTEXT(context)->get_write_header_offset());
}

LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_set_write_header_offset(libatgateway_v2_c_context context,
                                                                             uint64_t offset) {
  if (ATGW_CONTEXT_IS_NULL(context)) {
    return;
  }

  ATGW_CONTEXT(context)->set_write_header_offset(static_cast<size_t>(offset));
}

#ifdef __cplusplus
}
#endif
