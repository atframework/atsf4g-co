// Copyright 2021 atframework
// Created by owent on 2016/9/29.
//

#include "utility/random_engine.h"

#include <config/compile_optimize.h>

#include <common/platform_compat.h>

#include <libatbus.h>

#if defined(ATFRAMEWORK_UTILS_THREAD_TLS_USE_PTHREAD) && ATFRAMEWORK_UTILS_THREAD_TLS_USE_PTHREAD
#  include <pthread.h>
#  include <sys/time.h>
#else
#  ifndef LEAN_AND_MEAN
#    define LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif

#  include <Windows.h>
#  include <Winsock2.h>
#endif

#include <cstdint>

ATFRAMEWORK_SERVICE_COMPONENT_NAMESPACE_BEGIN

namespace {

#ifdef _WIN32
static void gettimeofday(struct timeval *tv, void *) {
  FILETIME ftime;
  int64_t n = 0;

  GetSystemTimeAsFileTime(&ftime);
  n = ((static_cast<int64_t>(ftime.dwHighDateTime) << 32) + static_cast<int64_t>(ftime.dwLowDateTime));
  if (n) {
    n /= 10;
    n -= (((369 * 365) + 89) * static_cast<int64_t>(86400)) * 1000000;
  }

  tv->tv_sec = static_cast<decltype(tv->tv_sec)>(n) / 1000000;
  tv->tv_usec = static_cast<decltype(tv->tv_usec)>(n % 1000000);
}

static int getuid() { return 1; }
#endif

template <class SeedType>
SeedType build_random_seed() {
  struct timeval tv{};
  gettimeofday(&tv, nullptr);

  SeedType seed = static_cast<SeedType>(atfw::atbus::node::get_pid() << 16) ^ static_cast<SeedType>(getuid()) ^
                  static_cast<SeedType>(tv.tv_sec) ^ static_cast<SeedType>(tv.tv_usec);

  return seed;
}

struct ATFW_UTIL_SYMBOL_LOCAL random_engine_atfork_guard_t {
  random_engine_atfork_guard_t() {
    atfw::util::platform::atfork(nullptr, nullptr, []() { random_engine::reset_all_seeds(); });
  }
};
static void random_engine_setup_atfork() {
  static random_engine_atfork_guard_t guard;
  (void)guard;
}

static atfw::util::random::mt19937_64 build_common_generator() {
  random_engine_setup_atfork();
  return {build_random_seed<atfw::util::random::mt19937_64::result_type>()};
}

static atfw::util::random::xoshiro256_starstar build_fast_generator() {
  random_engine_setup_atfork();
  return {build_random_seed<atfw::util::random::xoshiro256_starstar::result_type>()};
}
}  // namespace

random_engine::random_engine() {}

random_engine::~random_engine() {}

ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API atfw::util::random::mt19937_64 &random_engine::_get_common_generator() {
  static atfw::util::random::mt19937_64 ret = build_common_generator();
  return ret;
}

ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API atfw::util::random::xoshiro256_starstar &random_engine::_get_fast_generator() {
  static atfw::util::random::xoshiro256_starstar ret = build_fast_generator();
  return ret;
}

ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API void random_engine::reset_all_seeds() {
  _get_common_generator() = build_common_generator();
  _get_fast_generator() = build_fast_generator();
}

ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API uint64_t random_engine::random() {
  static_assert(sizeof(atfw::util::random::mt19937_64::result_type) >= sizeof(uint64_t), "random range checking");
  return static_cast<uint64_t>(_get_common_generator().random());
}

ATFRAMEWORK_SERVICE_COMPONENT_MACRO_API uint64_t random_engine::fast_random() {
  static_assert(sizeof(atfw::util::random::xoshiro256_starstar::result_type) >= sizeof(uint64_t),
                "random range checking");
  return _get_fast_generator().random();
}
ATFRAMEWORK_SERVICE_COMPONENT_NAMESPACE_END
