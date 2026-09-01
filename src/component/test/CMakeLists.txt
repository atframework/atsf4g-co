# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

add_subdirectory(api)
if(BUILD_TESTING OR PROJECT_ENABLE_UNITTEST)
  add_subdirectory(ItemAlgorithmTest)
endif()
