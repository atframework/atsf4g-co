include_guard(GLOBAL)

if(EXISTS "${PROJECT_SOURCE_DIR}/.vscode")
  include("${CMAKE_CURRENT_LIST_DIR}/vscode/OptimizeVsCodeSettings.cmake")
endif()
