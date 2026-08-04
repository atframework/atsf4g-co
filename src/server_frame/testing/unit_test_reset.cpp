// Copyright 2026 atframework

#include "testing/unit_test_reset.h"

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS

#  include <dispatcher/cs_msg_dispatcher.h>
#  include <dispatcher/ss_msg_dispatcher.h>

SERVER_FRAME_API void server_frame_unit_test_reset_dispatcher_registrations() {
  ss_msg_dispatcher::me()->reset_registrations_for_unit_test();
  cs_msg_dispatcher::me()->reset_registrations_for_unit_test();
}

#endif
