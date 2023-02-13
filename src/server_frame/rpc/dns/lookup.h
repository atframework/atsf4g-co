// Copyright 2023 atframework
// Created by owent on 2023/01/05.
//

#pragma once

#include <gsl/select-gsl.h>

#include <stdint.h>
#include <cstddef>
#include <string>
#include <vector>

#include "rpc/rpc_common_types.h"

namespace rpc {
class context;

namespace dns {
enum class address_type : int8_t {
  kUnknown = 0,
  kA = 1,
  kAAAA = 2,
};

struct address_record {
  address_type type;
  std::string address;
};

namespace details {
using callback_data_type = std::vector<address_record>;
}

EXPLICIT_NODISCARD_ATTR rpc::result_code_type lookup(rpc::context& ctx, gsl::string_view domain,
                                                                         std::vector<address_record>& output);

}  // namespace dns
}  // namespace rpc
