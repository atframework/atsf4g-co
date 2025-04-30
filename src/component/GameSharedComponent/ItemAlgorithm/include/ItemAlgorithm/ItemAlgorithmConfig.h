// Copyright 2025 atframework

#pragma once

#include <config/compile_optimize.h>

#if defined(GAME_SHARED_COMPONENT_SERVER_MODE) && GAME_SHARED_COMPONENT_SERVER_MODE
#  include <config/server_frame_build_feature.h>
#else
#  include <config/shared_component_build_feature.h>
#endif

#ifndef PROJECT_NAMESPACE_BEGIN
#  error "PROJECT_NAMESPACE_BEGIN is not defined"
#endif

#ifndef PROJECT_NAMESPACE_END
#  error "PROJECT_NAMESPACE_END is not defined"
#endif

#ifndef PROJECT_NAMESPACE_ID
#  error "PROJECT_NAMESPACE_ID is not defined"
#endif

#ifndef SERVER_FRAME_PROTOCOL_COMMON_API
#  ifdef GSC_PROTOCOL_COMMON_API
#    define SERVER_FRAME_PROTOCOL_COMMON_API GSC_PROTOCOL_COMMON_API
#  else
#    error "SERVER_FRAME_PROTOCOL_COMMON_API is not defined"
#  endif
#endif

#ifndef ITEM_ALGORITHM_NAMESPACE_BEGIN
#  define ITEM_ALGORITHM_NAMESPACE_BEGIN PROJECT_NAMESPACE_BEGIN
#endif

#ifndef ITEM_ALGORITHM_NAMESPACE_END
#  define ITEM_ALGORITHM_NAMESPACE_END PROJECT_NAMESPACE_END
#endif

#ifndef ITEM_ALGORITHM_NAMESPACE_ID
#  define ITEM_ALGORITHM_NAMESPACE_ID PROJECT_NAMESPACE_ID
#endif

#ifndef ITEM_ALGORITHM_API
#  define ITEM_ALGORITHM_API ATFW_UTIL_SYMBOL_VISIBLE
#endif
