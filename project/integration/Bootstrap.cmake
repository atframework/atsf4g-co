# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

include_guard(GLOBAL)

if(EXISTS "${PROJECT_SOURCE_DIR}/.vscode")
  include("${CMAKE_CURRENT_LIST_DIR}/vscode/OptimizeVsCodeSettings.cmake")
endif()

if(PROJECT_ENABLE_CODE_ANALYSIS)
  include("${CMAKE_CURRENT_LIST_DIR}/analysis/clang-tidy.cmake")
  include("${CMAKE_CURRENT_LIST_DIR}/analysis/cpplint.cmake")
endif()
