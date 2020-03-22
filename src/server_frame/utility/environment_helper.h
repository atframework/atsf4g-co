#ifndef UTILITY_ENVIRONMENT_HELPER_H
#define UTILITY_ENVIRONMENT_HELPER_H

#pragma once

#if (defined(__cplusplus) && __cplusplus >= 201103L) || (defined(_MSC_VER) && _MSC_VER >= 1600)

#include <unordered_map>
#include <unordered_set>
#define UTIL_ENV_AUTO_MAP(...) std::unordered_map<__VA_ARGS__>
#define UTIL_ENV_AUTO_SET(...) std::unordered_set<__VA_ARGS__>
#define UTIL_ENV_AUTO_UNORDERED 1
#else

#include <map>
#include <set>
#define UTIL_ENV_AUTO_MAP(...) std::map<__VA_ARGS__>
#define UTIL_ENV_AUTO_SET(...) std::set<__VA_ARGS__>

#endif

#endif