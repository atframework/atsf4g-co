# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

add_subdirectory(service)

if((BUILD_TESTING OR PROJECT_ENABLE_UNITTEST) AND PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS)
  add_subdirectory(test)
endif()
