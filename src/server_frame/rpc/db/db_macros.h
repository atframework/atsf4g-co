// Copyright 2022 atframework
// Created by owent on 2016/9/28.
//

#pragma once

#include <common/string_oprs.h>
#include <string/string_format.h>

#include <gsl/select-gsl.h>

#include <log/log_wrapper.h>

#include <inttypes.h>
#include <stdint.h>
#include <cstddef>
#include <utility>

namespace rpc {
namespace db {
using table_key_type = char[256];

template <class TTable, class TID, class TZone>
ATFW_UTIL_SYMBOL_VISIBLE inline size_t format_user_key(table_key_type& key, TTable&& table, TID&& id, TZone&& zone_id) {
  size_t keylen = sizeof(key) - 1;
  auto result = atfw::util::string::format_to_n(key, keylen, "{}:{}:{}", std::forward<TTable>(table),
                                                std::forward<TID>(id), std::forward<TZone>(zone_id));

  key[result.size] = '\0';
  return keylen;
}

}  // namespace db
}  // namespace rpc
