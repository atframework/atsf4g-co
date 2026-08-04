// Copyright 2026 atframework

#pragma once

#include <config/server_frame_build_feature.h>

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS

/**
 * @brief Reset registered RPC task actions/services/methods of the process-lifetime ss/cs dispatcher singletons.
 * @note Only available when PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS is on. Used by the unit test fixture
 *       lifecycle (src/tools/rpc-unit-test) so a consecutive fixture in the same process can re-register
 *       handles. It does not touch running tasks and must only be called when no task is running.
 */
SERVER_FRAME_API void server_frame_unit_test_reset_dispatcher_registrations();

#endif
