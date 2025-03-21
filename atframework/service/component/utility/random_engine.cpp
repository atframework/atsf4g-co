// Copyright 2021 atframework
// Created by owent on 2016/9/29.
//

#include "utility/random_engine.h"

#include <ctime>

namespace atframework {
namespace util {

random_engine::random_engine() {}

random_engine::~random_engine() {}

ATFRAME_SERVICE_COMPONENT_MACRO_API atfw::util::random::mt19937_64 &random_engine::_get_common_generator() {
  static atfw::util::random::mt19937_64 ret(static_cast<atfw::util::random::mt19937_64::result_type>(time(nullptr)));
  return ret;
}

ATFRAME_SERVICE_COMPONENT_MACRO_API atfw::util::random::xoshiro256_starstar &random_engine::_get_fast_generator() {
  static atfw::util::random::xoshiro256_starstar ret(
      static_cast<atfw::util::random::xoshiro256_starstar::result_type>(time(nullptr)));
  return ret;
}

ATFRAME_SERVICE_COMPONENT_MACRO_API uint64_t random_engine::random() {
  static_assert(sizeof(atfw::util::random::mt19937_64::result_type) >= sizeof(uint64_t), "random range checking");
  return static_cast<uint64_t>(_get_common_generator().random());
}

ATFRAME_SERVICE_COMPONENT_MACRO_API uint64_t random_engine::fast_random() {
  static_assert(sizeof(atfw::util::random::xoshiro256_starstar::result_type) >= sizeof(uint64_t),
                "random range checking");
  return _get_fast_generator().random();
}
}  // namespace util
}  // namespace atframework
