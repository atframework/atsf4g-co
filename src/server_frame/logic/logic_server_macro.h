// Copyright 2022 atframework
// Created by owent

#pragma once

#ifdef _MSC_VER

#  define INIT_CALL(MOD_NAME, ...)                                   \
    {                                                                \
      int res = MOD_NAME::me()->init(__VA_ARGS__);                   \
      if (res < 0) {                                                 \
        FWLOGERROR("Initialize {} failed, res: {}", #MOD_NAME, res); \
        return res;                                                  \
      }                                                              \
    }

#  define INIT_CALL_FN(FUNC, ...)                                \
    {                                                            \
      int res = FUNC(__VA_ARGS__);                               \
      if (res < 0) {                                             \
        FWLOGERROR("Initialize {} failed, res: {}", #FUNC, res); \
        return res;                                              \
      }                                                          \
    }

#  define RELOAD_CALL(RET_VAR, MOD_NAME, ...)                    \
    {                                                            \
      int res = MOD_NAME::me()->reload(__VA_ARGS__);             \
      if (res < 0) {                                             \
        FWLOGERROR("Reload {} failed, res: {}", #MOD_NAME, res); \
        RET_VAR = res;                                           \
      }                                                          \
    }

#  define RELOAD_CALL_FN(RET_VAR, FUNC, ...)                 \
    {                                                        \
      int res = FUNC(__VA_ARGS__);                           \
      if (res < 0) {                                         \
        FWLOGERROR("Reload {} failed, res: {}", #FUNC, res); \
        RET_VAR = res;                                       \
      }                                                      \
    }

#else
#  define INIT_CALL(MOD_NAME, args...)                               \
    {                                                                \
      int res = MOD_NAME::me()->init(args);                          \
      if (res < 0) {                                                 \
        FWLOGERROR("Initialize {} failed, res: {}", #MOD_NAME, res); \
        return res;                                                  \
      }                                                              \
    }

#  define INIT_CALL_FN(FUNC, args...)                            \
    {                                                            \
      int res = FUNC(args);                                      \
      if (res < 0) {                                             \
        FWLOGERROR("Initialize {} failed, res: {}", #FUNC, res); \
        return res;                                              \
      }                                                          \
    }

#  define RELOAD_CALL(RET_VAR, MOD_NAME, args...)                \
    {                                                            \
      int res = MOD_NAME::me()->reload(args);                    \
      if (res < 0) {                                             \
        FWLOGERROR("Reload {} failed, res: {}", #MOD_NAME, res); \
        RET_VAR = res;                                           \
      }                                                          \
    }

#  define RELOAD_CALL_FN(RET_VAR, FUNC, args...)             \
    {                                                        \
      int res = FUNC(args);                                  \
      if (res < 0) {                                         \
        FWLOGERROR("Reload {} failed, res: {}", #FUNC, res); \
        RET_VAR = res;                                       \
      }                                                      \
    }

#endif
