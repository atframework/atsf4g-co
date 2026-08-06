include_guard(GLOBAL)

if(EXISTS "${PROJECT_SOURCE_DIR}/.vscode")
  message(STATUS "Install VSCode Integration.")
  include("vscode/OptimizeVsCodeSettings.cmake")
endif()
