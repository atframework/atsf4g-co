// Copyright 2021 atframework
// Created by owent on 2016/9/29.
//

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "config/compile_optimize.h"

#ifdef __cplusplus
extern "C" {
#endif

using libatgw_inner_v1_c_context = void *;

using libatgw_inner_v1_c_on_write_start_fn_t = int32_t (*)(libatgw_inner_v1_c_context, void *, uint64_t, int32_t *);
using libatgw_inner_v1_c_on_message_fn_t = int32_t (*)(libatgw_inner_v1_c_context, const void *, uint64_t);
using libatgw_inner_v1_c_on_init_new_session_fn_t = int32_t (*)(libatgw_inner_v1_c_context, uint64_t *);
using libatgw_inner_v1_c_on_init_reconnect_fn_t = int32_t (*)(libatgw_inner_v1_c_context, uint64_t);
using libatgw_inner_v1_c_on_close_fn_t = int32_t (*)(libatgw_inner_v1_c_context, int32_t);
using libatgw_inner_v1_c_on_handshake_done_fn_t = int32_t (*)(libatgw_inner_v1_c_context, int32_t);
using libatgw_inner_v1_c_on_error_fn_t = int32_t (*)(libatgw_inner_v1_c_context, const char *, int32_t, int32_t,
                                                     const char *);

UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_global_init_algorithms();
UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_global_cleanup_algorithms();

UTIL_SYMBOL_EXPORT uint64_t __cdecl libatgw_inner_v1_c_global_get_crypt_size();
UTIL_SYMBOL_EXPORT const char *__cdecl libatgw_inner_v1_c_global_get_crypt_name(uint64_t idx);

UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_gset_on_write_start_fn(libatgw_inner_v1_c_on_write_start_fn_t fn);
UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_gset_on_message_fn(libatgw_inner_v1_c_on_message_fn_t fn);
UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_gset_on_init_new_session_fn(
    libatgw_inner_v1_c_on_init_new_session_fn_t fn);
UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_gset_on_init_reconnect_fn(
    libatgw_inner_v1_c_on_init_reconnect_fn_t fn);
UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_gset_on_close_fn(libatgw_inner_v1_c_on_close_fn_t fn);
UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_gset_on_handshake_done_fn(
    libatgw_inner_v1_c_on_handshake_done_fn_t fn);
UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_gset_on_handshake_update_fn(
    libatgw_inner_v1_c_on_handshake_done_fn_t fn);
UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_gset_on_error_fn(libatgw_inner_v1_c_on_error_fn_t fn);

UTIL_SYMBOL_EXPORT libatgw_inner_v1_c_context __cdecl libatgw_inner_v1_c_create();
UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_destroy(libatgw_inner_v1_c_context context);

UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_set_recv_buffer_limit(libatgw_inner_v1_c_context context,
                                                                         uint64_t max_size, uint64_t max_number);
UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_set_send_buffer_limit(libatgw_inner_v1_c_context context,
                                                                         uint64_t max_size, uint64_t max_number);

UTIL_SYMBOL_EXPORT int32_t __cdecl libatgw_inner_v1_c_start_session(libatgw_inner_v1_c_context context,
                                                                    const char *crypt_type);
UTIL_SYMBOL_EXPORT int32_t __cdecl libatgw_inner_v1_c_reconnect_session(libatgw_inner_v1_c_context context,
                                                                        uint64_t sessios_id, const char *crypt_type,
                                                                        const unsigned char *secret_buf,
                                                                        uint64_t secret_len);

UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_get_info(libatgw_inner_v1_c_context context, char *info_str,
                                                            uint64_t info_len);

UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_set_private_data(libatgw_inner_v1_c_context context, void *);
UTIL_SYMBOL_EXPORT void *__cdecl libatgw_inner_v1_c_get_private_data(libatgw_inner_v1_c_context context);

UTIL_SYMBOL_EXPORT uint64_t __cdecl libatgw_inner_v1_c_get_session_id(libatgw_inner_v1_c_context context);
UTIL_SYMBOL_EXPORT const char *__cdecl libatgw_inner_v1_c_get_crypt_type(libatgw_inner_v1_c_context context);
UTIL_SYMBOL_EXPORT uint64_t __cdecl libatgw_inner_v1_c_get_crypt_secret_size(libatgw_inner_v1_c_context context);
UTIL_SYMBOL_EXPORT uint64_t __cdecl libatgw_inner_v1_c_copy_crypt_secret(libatgw_inner_v1_c_context context,
                                                                         unsigned char *secret,
                                                                         uint64_t available_size);
UTIL_SYMBOL_EXPORT uint32_t __cdecl libatgw_inner_v1_c_get_crypt_keybits(libatgw_inner_v1_c_context context);

UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_read_alloc(libatgw_inner_v1_c_context context,
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
UTIL_SYMBOL_EXPORT void __cdecl libatgw_inner_v1_c_read(libatgw_inner_v1_c_context context, int32_t ssz,
                                                        const char *buff, uint64_t len, int32_t *errcode);

UTIL_SYMBOL_EXPORT int32_t __cdecl libatgw_inner_v1_c_write_done(libatgw_inner_v1_c_context context, int32_t status);

UTIL_SYMBOL_EXPORT int32_t __cdecl libatgw_inner_v1_c_post_msg(libatgw_inner_v1_c_context context, const void *out_buf,
                                                               uint64_t out_len);
UTIL_SYMBOL_EXPORT int32_t __cdecl libatgw_inner_v1_c_send_ping(libatgw_inner_v1_c_context context);
UTIL_SYMBOL_EXPORT int64_t __cdecl libatgw_inner_v1_c_get_ping_delta(libatgw_inner_v1_c_context context);

UTIL_SYMBOL_EXPORT int32_t __cdecl libatgw_inner_v1_c_close(libatgw_inner_v1_c_context context, int32_t reason);

UTIL_SYMBOL_EXPORT int32_t __cdecl libatgw_inner_v1_c_is_closing(libatgw_inner_v1_c_context context);
UTIL_SYMBOL_EXPORT int32_t __cdecl libatgw_inner_v1_c_is_closed(libatgw_inner_v1_c_context context);
UTIL_SYMBOL_EXPORT int32_t __cdecl libatgw_inner_v1_c_is_handshake_updating(libatgw_inner_v1_c_context context);
UTIL_SYMBOL_EXPORT int32_t __cdecl libatgw_inner_v1_c_is_handshake_done(libatgw_inner_v1_c_context context);
UTIL_SYMBOL_EXPORT int32_t __cdecl libatgw_inner_v1_c_is_writing(libatgw_inner_v1_c_context context);
UTIL_SYMBOL_EXPORT int32_t __cdecl libatgw_inner_v1_c_is_in_callback(libatgw_inner_v1_c_context context);

#ifdef __cplusplus
}
#endif
