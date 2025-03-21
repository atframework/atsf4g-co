// Copyright 2023 atframework
// Created by owent on 2018-04-06.
//

#include "dispatcher_type_defines.h"

#include <config/compile_optimize.h>
#include <config/compiler_features.h>

#include <config/logic_config.h>

#include <ctime>

std::chrono::system_clock::duration dispatcher_await_options::get_default_timeout() noexcept {
  auto& timeout = logic_config::me()->get_logic().task().csmsg().timeout();

  if ATFW_UTIL_LIKELY_CONDITION (timeout.seconds() > 0 || timeout.nanos() > 0) {
    return std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::seconds{timeout.seconds()}) +
           std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::nanoseconds{timeout.nanos()});
  }

  return std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::milliseconds{6000});
}
