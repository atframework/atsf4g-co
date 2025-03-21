// Copyright 2023 atframework
// Created by owentou on 2019-01-15.
//

#pragma once

#include <config/server_frame_build_feature.h>

#include <stdint.h>

#include <cstddef>

struct ATFW_UTIL_SYMBOL_VISIBLE tls_buffers_type_t {
  enum type {
    EN_TBT_DEFAULT = 0,
    EN_TBT_MESSAGE,
    EN_TBT_UTILITY,
    EN_TBT_CUSTOM,
    EN_TBT_MAX,
  };
};

/**
 * @biref get thread-local storage buffer limit for message encrypt/decrypt, zip/unzip and etc
 * @param tls_type type, different type has different address
 * @return thread-local storage buffer address
 */
SERVER_FRAME_API void *tls_buffers_get_buffer(tls_buffers_type_t::type tls_type);

/**
 * @biref get thread-local storage buffer length for message encrypt/decrypt, zip/unzip and etc
 * @param tls_type type, different type has different length
 * @return thread-local storage buffer length
 */
SERVER_FRAME_API size_t tls_buffers_get_length(tls_buffers_type_t::type tls_type);
