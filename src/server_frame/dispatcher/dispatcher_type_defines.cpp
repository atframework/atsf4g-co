//
// Created by owt50 on 2018/04/06.
//

#include <config/compiler_features.h>

#include "dispatcher_type_defines.h"

#if defined(UTIL_CONFIG_COMPILER_CXX_STATIC_ASSERT) && UTIL_CONFIG_COMPILER_CXX_STATIC_ASSERT
#include <type_traits>
static_assert(std::is_trivial<dispatcher_resume_data_t>::value, "resume_data_t must be a trivial.");
static_assert(std::is_trivial<dispatcher_start_data_t>::value, "start_data_t must be a trivial.");
#endif
