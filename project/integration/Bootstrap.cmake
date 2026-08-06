include_guard(GLOBAL)

if(EXISTS "${PROJECT_SOURCE_DIR}/.vscode")
  message(STATUS "Install VSCode Integration.")
  include("${CMAKE_CURRENT_LIST_DIR}/vscode/OptimizeVsCodeSettings.cmake")
endif()
