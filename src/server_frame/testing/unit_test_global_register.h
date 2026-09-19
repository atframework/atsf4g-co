// Copyright 2026 atframework

#pragma once

#include <std/explicit_declare.h>

#include <config/server_frame_build_feature.h>

#include <cstddef>

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS

// Include this header at global scope (a top-of-file include block), never inside a namespace: the entry
// points below must keep their global linkage, otherwise their dllimport references no longer match the
// symbols exported by the server frame DLL and MSVC reports LNK2019.

#  ifndef ATFW_SERVER_FRAME_TESTING_SETUP
#    define ATFW_SERVER_FRAME_TESTING_SETUP_ACTION_NAME(__EVENT_NAME) \
      atframework_server_frame_testing_event_on_setup_action_##__EVENT_NAME##_
#    define ATFW_SERVER_FRAME_TESTING_SETUP_INIT_NAME(__EVENT_NAME) \
      atframework_server_frame_testing_event_on_setup_init_##__EVENT_NAME##_

#    if defined(_MSC_VER) && !defined(__clang__)
#      define ATFW_SERVER_FRAME_TESTING_SETUP_INIT_NAME_PTR(__EVENT_NAME) \
        ATFW_SERVER_FRAME_TESTING_SETUP_INIT_NAME(__EVENT_NAME##_ptr)
#      define ATFW_SERVER_FRAME_TESTING_SETUP(__EVENT_NAME)                                                          \
        static void ATFW_SERVER_FRAME_TESTING_SETUP_ACTION_NAME(__EVENT_NAME)();                                     \
        ATFW_EXPLICIT_UNUSED_ATTR static void __cdecl ATFW_SERVER_FRAME_TESTING_SETUP_INIT_NAME(__EVENT_NAME)();     \
        __pragma(section(".CRT$XCU", read)) __declspec(allocate(".CRT$XCU")) static void(                            \
            __cdecl * ATFW_SERVER_FRAME_TESTING_SETUP_INIT_NAME_PTR(__EVENT_NAME))() =                               \
            &ATFW_SERVER_FRAME_TESTING_SETUP_INIT_NAME(__EVENT_NAME);                                                \
        static void __cdecl ATFW_SERVER_FRAME_TESTING_SETUP_INIT_NAME(__EVENT_NAME)() {                              \
          ::server_frame_unit_test_register_setup_action(ATFW_SERVER_FRAME_TESTING_SETUP_ACTION_NAME(__EVENT_NAME)); \
        }                                                                                                            \
        static void ATFW_SERVER_FRAME_TESTING_SETUP_ACTION_NAME(__EVENT_NAME)()
#    elif defined(__GNUC__) || defined(__clang__)
#      define ATFW_SERVER_FRAME_TESTING_SETUP(__EVENT_NAME)                                                          \
        static void ATFW_SERVER_FRAME_TESTING_SETUP_ACTION_NAME(__EVENT_NAME)();                                     \
        __attribute__((constructor, used)) static void ATFW_SERVER_FRAME_TESTING_SETUP_INIT_NAME(__EVENT_NAME)() {   \
          ::server_frame_unit_test_register_setup_action(ATFW_SERVER_FRAME_TESTING_SETUP_ACTION_NAME(__EVENT_NAME)); \
        }                                                                                                            \
        static void ATFW_SERVER_FRAME_TESTING_SETUP_ACTION_NAME(__EVENT_NAME)()
#    else
#      define ATFW_SERVER_FRAME_TESTING_SETUP(__EVENT_NAME)                                                          \
        static void ATFW_SERVER_FRAME_TESTING_SETUP_ACTION_NAME(__EVENT_NAME)();                                     \
        ATFW_EXPLICIT_UNUSED_ATTR static const bool ATFW_SERVER_FRAME_TESTING_SETUP_INIT_NAME(__EVENT_NAME) = [] {   \
          ::server_frame_unit_test_register_setup_action(ATFW_SERVER_FRAME_TESTING_SETUP_ACTION_NAME(__EVENT_NAME)); \
          return true;                                                                                               \
        }();                                                                                                         \
        static void ATFW_SERVER_FRAME_TESTING_SETUP_ACTION_NAME(__EVENT_NAME)()
#    endif
#  endif

using server_frame_unit_test_setup_action_function = void (*)();

SERVER_FRAME_API void server_frame_unit_test_register_setup_action(server_frame_unit_test_setup_action_function fn);

SERVER_FRAME_API void server_frame_unit_test_run_setup_action();

SERVER_FRAME_API size_t server_frame_unit_test_get_setup_action_count();

SERVER_FRAME_API bool server_frame_unit_test_is_setup_action_already_run();

#endif
