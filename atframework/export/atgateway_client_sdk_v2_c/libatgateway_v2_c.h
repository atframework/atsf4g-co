// Copyright 2026 atframework
// Created by owent on 2016/9/29.
//

#pragma once

#include <cstddef>
#include <cstdint>

#include "config/compile_optimize.h"

#ifndef LIBATGATEWAY_V2_C_API
#  define LIBATGATEWAY_V2_C_API ATFW_UTIL_SYMBOL_VISIBLE
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum libatgateway_v2_c_error_code_t {
  LIBATGATEWAY_V2_C_EN_ERROR_CODE_SUCCESS = 0,
  LIBATGATEWAY_V2_C_EN_ERROR_CODE_FIRST_IDEL = -1001,
  LIBATGATEWAY_V2_C_EN_ERROR_CODE_HANDSHAKE = -1002,
  LIBATGATEWAY_V2_C_EN_ERROR_CODE_BUSY = -1003,
  LIBATGATEWAY_V2_C_EN_ERROR_CODE_SESSION_EXPIRED = -1004,
  LIBATGATEWAY_V2_C_EN_ERROR_CODE_REFUSE_RECONNECT = -1005,
  LIBATGATEWAY_V2_C_EN_ERROR_CODE_MISS_CALLBACKS = -1006,
  LIBATGATEWAY_V2_C_EN_ERROR_CODE_INVALID_ROUTER = -1007,
  LIBATGATEWAY_V2_C_EN_ERROR_CODE_INVALID_ADDRESS = -1008,
  LIBATGATEWAY_V2_C_EN_ERROR_CODE_NETWORK = -1009,
  LIBATGATEWAY_V2_C_EN_ERROR_CODE_BAD_PROTOCOL = -1010,
  LIBATGATEWAY_V2_C_EN_ERROR_CODE_CLOSING = -1011,
  LIBATGATEWAY_V2_C_EN_ERROR_CODE_LOST_MANAGER = -1012,
  LIBATGATEWAY_V2_C_EN_ERROR_CODE_MSG_TOO_LARGE = -1013,
  LIBATGATEWAY_V2_C_EN_ERROR_CODE_HANDLE_NOT_FOUND = -1014,
  LIBATGATEWAY_V2_C_EN_ERROR_CODE_ALREADY_HAS_FD = -1015,
  LIBATGATEWAY_V2_C_EN_ERROR_CODE_SESSION_NOT_FOUND = -1016,
  LIBATGATEWAY_V2_C_EN_ERROR_CODE_SESSION_ALREADY_EXIST = -1017,
  LIBATGATEWAY_V2_C_EN_ERROR_CODE_NOT_WRITING = -1018,
  LIBATGATEWAY_V2_C_EN_ERROR_CODE_CRYPT_NOT_SUPPORTED = -1019,
  LIBATGATEWAY_V2_C_EN_ERROR_CODE_PARAM = -1020,
  LIBATGATEWAY_V2_C_EN_ERROR_CODE_BAD_DATA = -1021,
  LIBATGATEWAY_V2_C_EN_ERROR_CODE_INVALID_SIZE = -1022,
  LIBATGATEWAY_V2_C_EN_ERROR_CODE_NO_DATA = -1023,
  LIBATGATEWAY_V2_C_EN_ERROR_CODE_MALLOC = -1024,
  LIBATGATEWAY_V2_C_EN_ERROR_CODE_CRYPT_ALREADY_INITED = -1101,
  LIBATGATEWAY_V2_C_EN_ERROR_CODE_CRYPT_VERIFY = -1102,
  LIBATGATEWAY_V2_C_EN_ERROR_CODE_CRYPT_OPERATION = -1103,
  LIBATGATEWAY_V2_C_EN_ERROR_CODE_CRYPT_READ_DHPARAM_FILE = -1211,
  LIBATGATEWAY_V2_C_EN_ERROR_CODE_CRYPT_INIT_DHPARAM = -1212,
  LIBATGATEWAY_V2_C_EN_ERROR_CODE_CRYPT_READ_RSA_PUBKEY = -1221,
  LIBATGATEWAY_V2_C_EN_ERROR_CODE_CRYPT_READ_RSA_PRIKEY = -1222,
};

enum libatgateway_v2_c_close_reason_t {
  LIBATGATEWAY_V2_C_EN_CLOSE_REASON_UNKNOWN = 0x0000,
  LIBATGATEWAY_V2_C_EN_CLOSE_REASON_EAGAIN = 0x0001,  // resource temporary unavailable
  LIBATGATEWAY_V2_C_EN_CLOSE_REASON_TRAFIC_EXTENDED = 0x0002,
  LIBATGATEWAY_V2_C_EN_CLOSE_REASON_INVALID_DATA = 0x0003,
  LIBATGATEWAY_V2_C_EN_CLOSE_REASON_RESET = 0x0004,
  LIBATGATEWAY_V2_C_EN_CLOSE_REASON_RECONNECT_INNER_BOUND = 0x0100,
  LIBATGATEWAY_V2_C_EN_CLOSE_REASON_RECONNECT_BOUND = 0x10000,
  LIBATGATEWAY_V2_C_EN_CLOSE_REASON_FIRST_IDLE = 0x10001,
  LIBATGATEWAY_V2_C_EN_CLOSE_REASON_SERVER_CLOSED = 0x10002,
  LIBATGATEWAY_V2_C_EN_CLOSE_REASON_SERVER_BUSY = 0x10003,
  LIBATGATEWAY_V2_C_EN_CLOSE_REASON_KICKOFF = 0x10004,
  LIBATGATEWAY_V2_C_EN_CLOSE_REASON_HANDSHAKE = 0x10005,
  LIBATGATEWAY_V2_C_EN_CLOSE_REASON_LOGOUT = 0x10006,
  LIBATGATEWAY_V2_C_EN_CLOSE_REASON_ADMINISTRATOR = 0x10007,  // kickoff by administrator
  LIBATGATEWAY_V2_C_EN_CLOSE_REASON_MAINTENANCE = 0x10008,    // closed to maintenance
  LIBATGATEWAY_V2_C_EN_CLOSE_REASON_EOF = 0x10009,  // EOF means everything is finished and no more need this connection
  LIBATGATEWAY_V2_C_EN_CLOSE_REASON_NO_RECONNECT_INNER_BOUND = 0x10100,
};

using libatgateway_v2_c_context = void *;

using libatgateway_v2_c_on_write_start_fn_t = int32_t (*)(libatgateway_v2_c_context, void *, uint64_t, int32_t *);
using libatgateway_v2_c_on_message_fn_t = int32_t (*)(libatgateway_v2_c_context, const void *, uint64_t);
using libatgateway_v2_c_on_init_new_session_fn_t = int32_t (*)(libatgateway_v2_c_context, uint64_t *);
using libatgateway_v2_c_on_init_reconnect_fn_t = int32_t (*)(libatgateway_v2_c_context, uint64_t);
using libatgateway_v2_c_on_close_fn_t = int32_t (*)(libatgateway_v2_c_context, int32_t);
using libatgateway_v2_c_on_handshake_done_fn_t = int32_t (*)(libatgateway_v2_c_context, int32_t);
using libatgateway_v2_c_on_error_fn_t = int32_t (*)(libatgateway_v2_c_context, const char *, int32_t, int32_t,
                                                    const char *);

LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_global_init_algorithms();
LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_global_cleanup_algorithms();

LIBATGATEWAY_V2_C_API uint64_t __cdecl libatgateway_v2_c_global_get_crypt_size();
LIBATGATEWAY_V2_C_API const char *__cdecl libatgateway_v2_c_global_get_crypt_name(uint64_t idx);

LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_gset_on_write_start_fn(libatgateway_v2_c_on_write_start_fn_t fn);
LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_gset_on_message_fn(libatgateway_v2_c_on_message_fn_t fn);
LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_gset_on_init_new_session_fn(
    libatgateway_v2_c_on_init_new_session_fn_t fn);
LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_gset_on_init_reconnect_fn(
    libatgateway_v2_c_on_init_reconnect_fn_t fn);
LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_gset_on_close_fn(libatgateway_v2_c_on_close_fn_t fn);
LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_gset_on_handshake_done_fn(
    libatgateway_v2_c_on_handshake_done_fn_t fn);
LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_gset_on_handshake_update_fn(
    libatgateway_v2_c_on_handshake_done_fn_t fn);
LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_gset_on_error_fn(libatgateway_v2_c_on_error_fn_t fn);

LIBATGATEWAY_V2_C_API libatgateway_v2_c_context __cdecl libatgateway_v2_c_create();
LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_destroy(libatgateway_v2_c_context context);

LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_set_recv_buffer_limit(libatgateway_v2_c_context context,
                                                                           uint64_t max_size, uint64_t max_number);
LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_set_send_buffer_limit(libatgateway_v2_c_context context,
                                                                           uint64_t max_size, uint64_t max_number);

LIBATGATEWAY_V2_C_API int32_t __cdecl libatgateway_v2_c_start_session(libatgateway_v2_c_context context,
                                                                      const char *crypt_type);
LIBATGATEWAY_V2_C_API int32_t __cdecl libatgateway_v2_c_reconnect_session(libatgateway_v2_c_context context,
                                                                          uint64_t sessios_id, const char *crypt_type,
                                                                          const unsigned char *secret_buf,
                                                                          uint64_t secret_len);

LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_get_info(libatgateway_v2_c_context context, char *info_str,
                                                              uint64_t info_len);

LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_set_private_data(libatgateway_v2_c_context context, void *);
LIBATGATEWAY_V2_C_API void *__cdecl libatgateway_v2_c_get_private_data(libatgateway_v2_c_context context);

LIBATGATEWAY_V2_C_API uint64_t __cdecl libatgateway_v2_c_get_session_id(libatgateway_v2_c_context context);
LIBATGATEWAY_V2_C_API const char *__cdecl libatgateway_v2_c_get_crypt_type(libatgateway_v2_c_context context);
LIBATGATEWAY_V2_C_API uint64_t __cdecl libatgateway_v2_c_get_crypt_secret_size(libatgateway_v2_c_context context);
LIBATGATEWAY_V2_C_API uint64_t __cdecl libatgateway_v2_c_copy_crypt_secret(libatgateway_v2_c_context context,
                                                                           unsigned char *secret,
                                                                           uint64_t available_size);
LIBATGATEWAY_V2_C_API uint32_t __cdecl libatgateway_v2_c_get_crypt_keybits(libatgateway_v2_c_context context);

LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_read_alloc(libatgateway_v2_c_context context,
                                                                uint64_t suggested_size, char **out_buf,
                                                                uint64_t *out_len);

/**
 * @brief mark how much data already copied into read buffer.
 * @param ssz context protocol context
 * @param ssz nread, error code or data length. useless
 * @param buff start address of read data. useless
 * @param len lengtn of read data. read buffer manager will cost len bytes and try to dispatch message
 * @param errcode where to receive error code
 */
LIBATGATEWAY_V2_C_API void __cdecl libatgateway_v2_c_read(libatgateway_v2_c_context context, int32_t ssz,
                                                          const char *buff, uint64_t len, int32_t *errcode);

LIBATGATEWAY_V2_C_API int32_t __cdecl libatgateway_v2_c_write_done(libatgateway_v2_c_context context, int32_t status);

LIBATGATEWAY_V2_C_API int32_t __cdecl libatgateway_v2_c_post_msg(libatgateway_v2_c_context context, const void *out_buf,
                                                                 uint64_t out_len);
LIBATGATEWAY_V2_C_API int32_t __cdecl libatgateway_v2_c_send_ping(libatgateway_v2_c_context context);
LIBATGATEWAY_V2_C_API int64_t __cdecl libatgateway_v2_c_get_ping_delta(libatgateway_v2_c_context context);

LIBATGATEWAY_V2_C_API int32_t __cdecl libatgateway_v2_c_close(libatgateway_v2_c_context context, int32_t reason);

LIBATGATEWAY_V2_C_API int32_t __cdecl libatgateway_v2_c_is_closing(libatgateway_v2_c_context context);
LIBATGATEWAY_V2_C_API int32_t __cdecl libatgateway_v2_c_is_closed(libatgateway_v2_c_context context);
LIBATGATEWAY_V2_C_API int32_t __cdecl libatgateway_v2_c_is_handshake_updating(libatgateway_v2_c_context context);
LIBATGATEWAY_V2_C_API int32_t __cdecl libatgateway_v2_c_is_handshake_done(libatgateway_v2_c_context context);
LIBATGATEWAY_V2_C_API int32_t __cdecl libatgateway_v2_c_is_writing(libatgateway_v2_c_context context);
LIBATGATEWAY_V2_C_API int32_t __cdecl libatgateway_v2_c_is_in_callback(libatgateway_v2_c_context context);

#ifdef __cplusplus
}
#endif
