//
// Created by owt50 on 2016/9/29.
//

#include <ctime>
#include "random_engine.h"

namespace util {

    random_engine::random_engine() {}

    random_engine::~random_engine() {}

    ::util::random::mt19937_64& random_engine::_get_common_generator() {
        static ::util::random::mt19937_64 ret(time(NULL));
        return ret;
    }

    ::util::random::taus88& random_engine::_get_fast_generator() {
        static ::util::random::taus88 ret(static_cast<uint32_t>(time(NULL)));
        return ret;
    }

    uint32_t random_engine::random() {
        return static_cast<uint32_t>(_get_common_generator().random());
    }

    uint32_t random_engine::fast_random() {
        return _get_fast_generator().random();
    }
}