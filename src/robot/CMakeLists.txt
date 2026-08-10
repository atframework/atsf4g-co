# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

# CMakeLists.txt for src/robot (Go-based robot/testing framework)
#
# The Go robot is built by go-task at build time through the `robot-build` target, which is intentionally NOT
# part of ALL. Configure only locates go-task and prepares the top-level Go submodules.

cmake_minimum_required(VERSION 3.24.0)

# ============================================================================
# 1. Locate go-task (never fatal; download the latest release as a fallback)
# ============================================================================

find_program(
  TASK_EXECUTABLE
  NAMES task task.exe
  HINTS "${PROJECT_INSTALL_TOOLS_DIR}/go-task"
  DOC "go-task runner")

set(ROBOT_TASK_READY FALSE)
if(TASK_EXECUTABLE AND NOT ROBOT_TASK_READY)
  execute_process(
    COMMAND "${TASK_EXECUTABLE}" --version
    OUTPUT_VARIABLE _ROBOT_TASK_VERSION_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _ROBOT_TASK_VERSION_RESULT)
  if(_ROBOT_TASK_VERSION_RESULT EQUAL 0)
    set(ROBOT_TASK_READY TRUE)
    message(STATUS "Found go-task: ${TASK_EXECUTABLE} (${_ROBOT_TASK_VERSION_OUTPUT})")
  else()
    message(WARNING "go-task found at ${TASK_EXECUTABLE} but failed to get version; trying the download fallback")
    unset(TASK_EXECUTABLE CACHE)
    unset(TASK_EXECUTABLE)
  endif()
endif()

if(NOT ROBOT_TASK_READY)
  # Fallback: query the latest go-task release and download the binary for this host. Trust note: this fetches whatever
  # upstream publishes as "latest"; pin PROJECT_INSTALL_TOOLS_DIR/go-task for a hermetic build.
  if(CMAKE_HOST_WIN32)
    set(ROBOT_TASK_HOST_OS "windows")
    set(ROBOT_TASK_ARCHIVE_EXT "zip")
    set(ROBOT_TASK_BIN_NAME "task.exe")
  elseif(CMAKE_HOST_APPLE)
    set(ROBOT_TASK_HOST_OS "darwin")
    set(ROBOT_TASK_ARCHIVE_EXT "tar.gz")
    set(ROBOT_TASK_BIN_NAME "task")
  else()
    set(ROBOT_TASK_HOST_OS "linux")
    set(ROBOT_TASK_ARCHIVE_EXT "tar.gz")
    set(ROBOT_TASK_BIN_NAME "task")
  endif()
  if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
    set(ROBOT_TASK_HOST_ARCH "arm64")
  else()
    set(ROBOT_TASK_HOST_ARCH "amd64")
  endif()

  set(ROBOT_TASK_DOWNLOAD_WORK_DIR "${CMAKE_BINARY_DIR}/robot/_downloads")
  file(MAKE_DIRECTORY "${ROBOT_TASK_DOWNLOAD_WORK_DIR}")
  set(ROBOT_TASK_API_JSON "${ROBOT_TASK_DOWNLOAD_WORK_DIR}/go-task-latest.json")
  message(STATUS "go-task not found locally, querying the latest release...")
  file(
    DOWNLOAD "https://api.github.com/repos/go-task/task/releases/latest" "${ROBOT_TASK_API_JSON}"
    STATUS ROBOT_TASK_API_STATUS
    TIMEOUT 30)
  list(GET ROBOT_TASK_API_STATUS 0 ROBOT_TASK_API_CODE)
  set(ROBOT_TASK_RELEASE_TAG "")
  if(ROBOT_TASK_API_CODE EQUAL 0)
    file(READ "${ROBOT_TASK_API_JSON}" ROBOT_TASK_API_CONTENT)
    string(REGEX MATCH "\"tag_name\"[ \t]*:[ \t]*\"([^\"]+)\"" ROBOT_TASK_TAG_MATCH "${ROBOT_TASK_API_CONTENT}")
    set(ROBOT_TASK_RELEASE_TAG "${CMAKE_MATCH_1}")
  else()
    list(GET ROBOT_TASK_API_STATUS 1 ROBOT_TASK_API_MESSAGE)
    message(STATUS "Failed to query go-task releases: ${ROBOT_TASK_API_MESSAGE}")
  endif()

  if(ROBOT_TASK_RELEASE_TAG)
    set(ROBOT_TASK_ARCHIVE_NAME "task_${ROBOT_TASK_HOST_OS}_${ROBOT_TASK_HOST_ARCH}.${ROBOT_TASK_ARCHIVE_EXT}")
    set(ROBOT_TASK_ARCHIVE_URL
        "https://github.com/go-task/task/releases/download/${ROBOT_TASK_RELEASE_TAG}/${ROBOT_TASK_ARCHIVE_NAME}")
    set(ROBOT_TASK_ARCHIVE "${ROBOT_TASK_DOWNLOAD_WORK_DIR}/${ROBOT_TASK_RELEASE_TAG}-${ROBOT_TASK_ARCHIVE_NAME}")
    message(STATUS "Downloading ${ROBOT_TASK_ARCHIVE_URL}")
    file(
      DOWNLOAD "${ROBOT_TASK_ARCHIVE_URL}" "${ROBOT_TASK_ARCHIVE}"
      STATUS ROBOT_TASK_ARCHIVE_STATUS
      TIMEOUT 300
      SHOW_PROGRESS)
    list(GET ROBOT_TASK_ARCHIVE_STATUS 0 ROBOT_TASK_ARCHIVE_CODE)
    if(ROBOT_TASK_ARCHIVE_CODE EQUAL 0)
      set(ROBOT_TASK_INSTALL_DIR "${PROJECT_INSTALL_TOOLS_DIR}/go-task")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E make_directory "${ROBOT_TASK_INSTALL_DIR}"
                      RESULT_VARIABLE ROBOT_TASK_MAKE_DIR_RESULT)
      if(ROBOT_TASK_MAKE_DIR_RESULT EQUAL 0)
        # file(ARCHIVE_EXTRACT) reports extraction failures as fatal CMake errors. Use cmake -E tar so a corrupt archive
        # or unwritable destination remains a warning and configure can still create the placeholder target.
        execute_process(
          COMMAND "${CMAKE_COMMAND}" -E tar xvf "${ROBOT_TASK_ARCHIVE}"
          WORKING_DIRECTORY "${ROBOT_TASK_INSTALL_DIR}"
          RESULT_VARIABLE ROBOT_TASK_EXTRACT_RESULT
          OUTPUT_QUIET
          ERROR_VARIABLE ROBOT_TASK_EXTRACT_ERROR)
        if(ROBOT_TASK_EXTRACT_RESULT EQUAL 0)
          set(TASK_EXECUTABLE "${ROBOT_TASK_INSTALL_DIR}/${ROBOT_TASK_BIN_NAME}")
          if(NOT CMAKE_HOST_WIN32 AND EXISTS "${TASK_EXECUTABLE}")
            execute_process(COMMAND chmod +x "${TASK_EXECUTABLE}" RESULT_VARIABLE ROBOT_TASK_CHMOD_RESULT)
            if(NOT ROBOT_TASK_CHMOD_RESULT EQUAL 0)
              message(WARNING "Failed to mark downloaded go-task executable: ${TASK_EXECUTABLE}")
            endif()
          endif()
        else()
          string(STRIP "${ROBOT_TASK_EXTRACT_ERROR}" ROBOT_TASK_EXTRACT_ERROR)
          message(WARNING "Failed to extract go-task: ${ROBOT_TASK_EXTRACT_ERROR}")
        endif()
      else()
        message(WARNING "Failed to create go-task install directory: ${ROBOT_TASK_INSTALL_DIR}")
      endif()
    else()
      list(GET ROBOT_TASK_ARCHIVE_STATUS 1 ROBOT_TASK_ARCHIVE_MESSAGE)
      message(STATUS "Failed to download go-task: ${ROBOT_TASK_ARCHIVE_MESSAGE}")
    endif()
  endif()
endif()

if(TASK_EXECUTABLE AND NOT ROBOT_TASK_READY)
  execute_process(
    COMMAND "${TASK_EXECUTABLE}" --version
    OUTPUT_VARIABLE _ROBOT_TASK_VERSION_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _ROBOT_TASK_VERSION_RESULT)
  if(_ROBOT_TASK_VERSION_RESULT EQUAL 0)
    set(ROBOT_TASK_READY TRUE)
    set(TASK_EXECUTABLE
        "${TASK_EXECUTABLE}"
        CACHE FILEPATH "go-task runner" FORCE)
    message(STATUS "Found go-task: ${TASK_EXECUTABLE} (${_ROBOT_TASK_VERSION_OUTPUT})")
  else()
    message(WARNING "go-task found at ${TASK_EXECUTABLE} but failed to get version")
    set(ROBOT_TASK_READY FALSE)
    unset(TASK_EXECUTABLE CACHE)
    unset(TASK_EXECUTABLE)
  endif()
  unset(_ROBOT_TASK_VERSION_OUTPUT)
  unset(_ROBOT_TASK_VERSION_RESULT)
endif()
string(CONCAT ROBOT_TASK_MISSING_MESSAGE "go-task (task) not found. The robot-build target is unavailable. "
              "Install go-task (https://taskfile.dev/) or set PROJECT_INSTALL_TOOLS_DIR, then re-run CMake.")
if(NOT ROBOT_TASK_READY)
  unset(TASK_EXECUTABLE CACHE)
  unset(TASK_EXECUTABLE)
  message(WARNING "go-task not found and could not be downloaded. ${ROBOT_TASK_MISSING_MESSAGE}")
endif()

# ============================================================================
# Configure paths and top-level Go submodules
# ============================================================================

set(ROBOT_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(ROBOT_INSTALL_PATH "${PROJECT_INSTALL_BAS_DIR}/robot")
set(ROBOT_BUILD_DIR "${CMAKE_BINARY_DIR}/robot")

message(STATUS "Robot source dir: ${ROBOT_SOURCE_DIR}")
message(STATUS "Robot install path: ${ROBOT_INSTALL_PATH}")
message(STATUS "Robot build dir: ${ROBOT_BUILD_DIR}")

maybe_populate_submodule(ROBOT_DEPENDENCY_ATFRAME_UTILS_GO "src/robot/atframework/atframe-utils-go"
                         "${PROJECT_SOURCE_DIR}/src/robot/atframework/atframe-utils-go")

maybe_populate_submodule(ROBOT_DEPENDENCY_ROBOT_GO "src/robot/atframework/robot-go"
                         "${PROJECT_SOURCE_DIR}/src/robot/atframework/robot-go")

# ============================================================================
# robot-build target (excluded from ALL; runs `task all` when selected)
# ============================================================================

# Compose environment for the go-task run. All injected keys are absent from build-tools.env, so environment injection
# is not overridden by dotenv. ENV_BUILD_TARGET_DIR/ENV_BUILD_TOOLS_DOWNLOAD_DIR must stay relative to the robot source
# dir: downstream tasks join them with PROJECT_ROOT.
file(RELATIVE_PATH ROBOT_BUILD_DIR_RELATIVE "${ROBOT_SOURCE_DIR}" "${ROBOT_BUILD_DIR}")
set(ROBOT_TASK_ENV
    "INSTALL_PATH=${ROBOT_INSTALL_PATH}" "ROBOT_BUILD_DIR=${ROBOT_BUILD_DIR}"
    "PYTHON_VENV_PATH=${ROBOT_BUILD_DIR}/venv" "ENV_BUILD_TARGET_DIR=${ROBOT_BUILD_DIR_RELATIVE}"
    "ENV_BUILD_TOOLS_DOWNLOAD_DIR=${ROBOT_BUILD_DIR_RELATIVE}/tools")

# Inject the parent template engine, interpreter, server pid/port files and python package prefix as one group so the go
# subproject attaches to the same persistent generate-for-pb server as the parent project (client and server must share
# one python interpreter, otherwise the pid-file process-image check rejects the running server).
if(GENERATE_FOR_PB_MAKO_PY
   AND Python3_EXECUTABLE
   AND GENERATE_FOR_PB_SERVER_PID_FILE
   AND GENERATE_FOR_PB_SERVER_PORT_FILE)
  list(
    APPEND
    ROBOT_TASK_ENV
    "MAKO_GENERATOR_PY=${GENERATE_FOR_PB_MAKO_PY}"
    "MAKO_GENERATOR_PYTHON_BIN=${Python3_EXECUTABLE}"
    "MAKO_GENERATOR_SERVER_PID_FILE=${GENERATE_FOR_PB_SERVER_PID_FILE}"
    "MAKO_GENERATOR_SERVER_PORT_FILE=${GENERATE_FOR_PB_SERVER_PORT_FILE}")
  if(PROJECT_THIRD_PARTY_PYTHON_MODULE_DIR)
    list(APPEND ROBOT_TASK_ENV "MAKO_GENERATOR_PACKAGE_PREFIX=${PROJECT_THIRD_PARTY_PYTHON_MODULE_DIR}")
  endif()
elseif(
  GENERATE_FOR_PB_MAKO_PY
  OR Python3_EXECUTABLE
  OR GENERATE_FOR_PB_SERVER_PID_FILE
  OR GENERATE_FOR_PB_SERVER_PORT_FILE)
  message(WARNING "Parent generate-for-pb settings are incomplete; robot-build will use its local generator server")
endif()

if(ROBOT_TASK_READY)
  add_custom_target(
    robot-build
    COMMAND "${CMAKE_COMMAND}" -E env ${ROBOT_TASK_ENV} "${TASK_EXECUTABLE}" all
    WORKING_DIRECTORY "${ROBOT_SOURCE_DIR}"
    USES_TERMINAL VERBATIM
    COMMENT "Build robot (go-task all)")
else()
  # Placeholder with the same name: fails with an actionable message only when explicitly selected.
  add_custom_target(
    robot-build
    COMMAND "${CMAKE_COMMAND}" -E echo "${ROBOT_TASK_MISSING_MESSAGE}"
    COMMAND "${CMAKE_COMMAND}" -E false
    WORKING_DIRECTORY "${ROBOT_SOURCE_DIR}"
    USES_TERMINAL VERBATIM
    COMMENT "Build robot (go-task missing)")
endif()
set_property(TARGET robot-build PROPERTY FOLDER "${PROJECT_NAME}/robot")
