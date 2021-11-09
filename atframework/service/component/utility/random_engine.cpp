// Copyright 2021 atframework
// Created by owent on 2016/9/29.
//

#include "utility/random_engine.h"

#include <ctime>

namespace util {

random_engine::random_engine() {}

random_engine::~random_engine() {}

ATFRAME_SERVICE_COMPONENT_MACRO_API ::util::random::mt19937_64 &random_engine::_get_common_generator() {
  static ::util::random::mt19937_64 ret(time(nullptr));
  return ret;
}

ATFRAME_SERVICE_COMPONENT_MACRO_API ::util::random::taus88 &random_engine::_get_fast_generator() {
  static ::util::random::taus88 ret(static_cast<uint32_t>(time(nullptr)));
  return ret;
}

ATFRAME_SERVICE_COMPONENT_MACRO_API uint32_t random_engine::random() {
  return static_cast<uint32_t>(_get_common_generator().random());
}

ATFRAME_SERVICE_COMPONENT_MACRO_API uint32_t random_engine::fast_random() { return _get_fast_generator().random(); }
}  // namespace util