include_guard(GLOBAL)

set(PROJECT_THIRD_PARTY_PYTHON_MODULE_DIR "${PROJECT_THIRD_PARTY_INSTALL_DIR}/.modules")
set(PROJECT_THIRD_PARTY_PYTHON_PIP_SOURCE "-i" "https://mirrors.tencent.com/pypi/simple/")

if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.15")
  cmake_policy(SET CMP0094 NEW)
endif()

# 如果 Python3_EXECUTABLE 有缓存且在venv中，要检查当前venv是否有效
if(Python3_EXECUTABLE AND NOT EXISTS "${Python3_EXECUTABLE}")
  message(STATUS "Cached Python3_EXECUTABLE ${Python3_EXECUTABLE} not found, clearing cache")
  unset(Python3_EXECUTABLE CACHE)
  unset(Python3_EXECUTABLE)
endif()
if(Python3_EXECUTABLE)
  file(TO_CMAKE_PATH "${PROJECT_THIRD_PARTY_HOST_INSTALL_DIR}/python.venv" __normalize_python_venv_path)
  file(TO_CMAKE_PATH "${Python3_EXECUTABLE}" __normalize_python_exec_path)
  string(FIND "${__normalize_python_exec_path}" "${__normalize_python_venv_path}" __python_index)
  if(__python_index EQUAL 0)
    get_filename_component(__python_venv_home "${__normalize_python_exec_path}" DIRECTORY)
    get_filename_component(__python_venv_home "${__python_venv_home}" DIRECTORY)

    # stdlib venv writes executable=/version=, virtualenv writes base-executable=/version_info=; support both.
    set(__python_venv_base_exec "")
    set(__python_venv_version "")
    if(EXISTS "${__python_venv_home}/pyvenv.cfg")
      file(STRINGS "${__python_venv_home}/pyvenv.cfg" __python_venv_base_exec_lines
           REGEX "^[ \t]*(base-)?executable[ \t]*=")
      if(__python_venv_base_exec_lines)
        list(GET __python_venv_base_exec_lines 0 __python_venv_base_exec)
        string(REGEX REPLACE "^[ \t]*(base-)?executable[ \t]*=[ \t]*(.*)$" "\\2" __python_venv_base_exec
                             "${__python_venv_base_exec}")
        string(STRIP "${__python_venv_base_exec}" __python_venv_base_exec)
      endif()
      unset(__python_venv_base_exec_lines)

      file(STRINGS "${__python_venv_home}/pyvenv.cfg" __python_venv_version_lines REGEX "^[ \t]*version(_info)?[ \t]*=")
      if(__python_venv_version_lines)
        list(GET __python_venv_version_lines 0 __python_venv_version)
      endif()
      unset(__python_venv_version_lines)
    endif()

    # Normalize the recorded version to MAJOR.MINOR.PATCH (virtualenv records e.g. 3.13.0.final.0).
    if(__python_venv_version MATCHES "([0-9]+\\.[0-9]+\\.[0-9]+)")
      set(__python_venv_version "${CMAKE_MATCH_1}")
    elseif(__python_venv_version MATCHES "([0-9]+\\.[0-9]+)")
      set(__python_venv_version "${CMAKE_MATCH_1}")
    else()
      set(__python_venv_version "")
    endif()

    if(__python_venv_base_exec AND EXISTS "${__python_venv_base_exec}")
      execute_process(
        COMMAND "${__python_venv_base_exec}" "--version"
        OUTPUT_VARIABLE __python_venv_base_version
        ERROR_VARIABLE __python_venv_base_version_stderr
        OUTPUT_STRIP_TRAILING_WHITESPACE)
      if(NOT __python_venv_base_version)
        set(__python_venv_base_version "${__python_venv_base_version_stderr}")
      endif()
      unset(__python_venv_base_version_stderr)
      if(__python_venv_base_version MATCHES "([0-9]+\\.[0-9]+\\.[0-9]+)")
        set(__python_venv_base_version "${CMAKE_MATCH_1}")
      elseif(__python_venv_base_version MATCHES "([0-9]+\\.[0-9]+)")
        set(__python_venv_base_version "${CMAKE_MATCH_1}")
      else()
        set(__python_venv_base_version "unknown")
      endif()
    else()
      set(__python_venv_base_version "unknown")
    endif()
    if(__python_venv_version AND __python_venv_version STREQUAL __python_venv_base_version)
      message(STATUS "Using cached Python3_EXECUTABLE: ${Python3_EXECUTABLE} (venv: ${__python_venv_home})")
    else()
      message(
        STATUS
          "Cached Python3_EXECUTABLE: ${Python3_EXECUTABLE} (venv: ${__python_venv_home}, version: ${__python_venv_version}) "
          "does not match base python version ${__python_venv_base_version}, clearing cache")
      unset(Python3_EXECUTABLE CACHE)
      unset(Python3_EXECUTABLE)
    endif()
    unset(__python_venv_home)
    unset(__python_venv_version)
    unset(__python_venv_base_exec)
    unset(__python_venv_base_version)
  endif()
  unset(__normalize_python_venv_path)
  unset(__normalize_python_exec_path)
  unset(__python_index)
endif()

find_package(Python3 COMPONENTS Interpreter)

# Patch for python3 binary
if(NOT Python3_Interpreter_FOUND AND UNIX)
  find_program(Python3_EXECUTABLE NAMES python3)
  if(Python3_EXECUTABLE)
    get_filename_component(Python3_BIN_DIR ${Python3_EXECUTABLE} DIRECTORY)
    get_filename_component(Python3_ROOT_DIR ${Python3_BIN_DIR} DIRECTORY CACHE)
    find_package(Python3 COMPONENTS Interpreter)
  endif()
endif()
if(NOT Python3_Interpreter_FOUND)
  message(FATAL_ERROR "Python is required but not found")
endif()

# ===== Try to use a python virtualenv/venv if the `virtualenv/venv` module is available =====
# When available, all subsequent python invocations and dependency installations go through the venv at:
# ${PROJECT_THIRD_PARTY_HOST_INSTALL_DIR}/python.venv/<MAJOR>.<MINOR> Environment variables (VIRTUAL_ENV, PATH,
# PYTHONHOME) are also injected so that any tool spawned from this CMake configure step picks up the same env.
file(TO_CMAKE_PATH
     "${PROJECT_THIRD_PARTY_HOST_INSTALL_DIR}/python.venv/${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}"
     PROJECT_THIRD_PARTY_PYTHON_VENV_DIR)

if(CMAKE_HOST_WIN32)
  set(PROJECT_THIRD_PARTY_PYTHON_VENV_BIN_DIR "${PROJECT_THIRD_PARTY_PYTHON_VENV_DIR}/Scripts")
  set(PROJECT_THIRD_PARTY_PYTHON_VENV_EXECUTABLE "${PROJECT_THIRD_PARTY_PYTHON_VENV_BIN_DIR}/python.exe")
else()
  set(PROJECT_THIRD_PARTY_PYTHON_VENV_BIN_DIR "${PROJECT_THIRD_PARTY_PYTHON_VENV_DIR}/bin")
  set(PROJECT_THIRD_PARTY_PYTHON_VENV_EXECUTABLE "${PROJECT_THIRD_PARTY_PYTHON_VENV_BIN_DIR}/python")
endif()

set(PROJECT_THIRD_PARTY_PYTHON_VENV_AVAILABLE FALSE)
if(EXISTS "${PROJECT_THIRD_PARTY_PYTHON_VENV_EXECUTABLE}")
  set(PROJECT_THIRD_PARTY_PYTHON_VENV_AVAILABLE TRUE)
endif()
if(NOT PROJECT_THIRD_PARTY_PYTHON_VENV_AVAILABLE)
  set(PROJECT_THIRD_PARTY_PYTHON_VIRTUALENV_MODULE_NAME "virtualenv")
  execute_process(
    COMMAND "${Python3_EXECUTABLE}" "-m" "${PROJECT_THIRD_PARTY_PYTHON_VIRTUALENV_MODULE_NAME}" "--version"
    RESULT_VARIABLE PROJECT_THIRD_PARTY_PYTHON_VIRTUALENV_CHECK_RESULT
    OUTPUT_QUIET ERROR_QUIET)
  if(NOT PROJECT_THIRD_PARTY_PYTHON_VIRTUALENV_CHECK_RESULT EQUAL 0)
    set(PROJECT_THIRD_PARTY_PYTHON_VIRTUALENV_MODULE_NAME "venv")
    execute_process(
      COMMAND "${Python3_EXECUTABLE}" "-m" "${PROJECT_THIRD_PARTY_PYTHON_VIRTUALENV_MODULE_NAME}" "--help"
      RESULT_VARIABLE PROJECT_THIRD_PARTY_PYTHON_VIRTUALENV_CHECK_RESULT
      OUTPUT_QUIET ERROR_QUIET)
  endif()
  if(PROJECT_THIRD_PARTY_PYTHON_VIRTUALENV_CHECK_RESULT EQUAL 0)
    if(NOT EXISTS "${PROJECT_THIRD_PARTY_PYTHON_VENV_DIR}")
      file(MAKE_DIRECTORY "${PROJECT_THIRD_PARTY_PYTHON_VENV_DIR}")
    endif()
    message(
      STATUS
        "Creating python ${PROJECT_THIRD_PARTY_PYTHON_VIRTUALENV_MODULE_NAME} at ${PROJECT_THIRD_PARTY_PYTHON_VENV_DIR} (host python: ${Python3_EXECUTABLE})"
    )
    execute_process(
      COMMAND "${Python3_EXECUTABLE}" "-m" "${PROJECT_THIRD_PARTY_PYTHON_VIRTUALENV_MODULE_NAME}"
              "--system-site-packages" "${PROJECT_THIRD_PARTY_PYTHON_VENV_DIR}"
      RESULT_VARIABLE PROJECT_THIRD_PARTY_PYTHON_VIRTUALENV_CREATE_RESULT COMMAND_ECHO STDOUT)
    if(NOT PROJECT_THIRD_PARTY_PYTHON_VIRTUALENV_CREATE_RESULT EQUAL 0)
      message(
        WARNING
          "Failed to create python ${PROJECT_THIRD_PARTY_PYTHON_VIRTUALENV_MODULE_NAME} at ${PROJECT_THIRD_PARTY_PYTHON_VENV_DIR}, fall back to host python"
      )
    endif()
    if(EXISTS "${PROJECT_THIRD_PARTY_PYTHON_VENV_EXECUTABLE}")
      set(PROJECT_THIRD_PARTY_PYTHON_VENV_AVAILABLE TRUE)
    endif()
    unset(PROJECT_THIRD_PARTY_PYTHON_VIRTUALENV_CREATE_RESULT)
  else()
    message(
      FATAL_ERROR
        "Python `virtualenv/venv` module not available on ${Python3_EXECUTABLE}, fall back to user-site install")
  endif()
  unset(PROJECT_THIRD_PARTY_PYTHON_VIRTUALENV_CHECK_RESULT)
endif()

if(PROJECT_THIRD_PARTY_PYTHON_VENV_AVAILABLE)
  # Switch the cached Python3_EXECUTABLE to the venv's interpreter so that any subsequent find_package(Python3) /
  # Python3_EXECUTABLE consumers use it too.
  if(NOT Python3_EXECUTABLE STREQUAL PROJECT_THIRD_PARTY_PYTHON_VENV_EXECUTABLE)
    # Update the normal variable as well, otherwise it shadows the forced cache value in the current scope and all
    # subsequent execute_process/pip/codegen steps would still use the host interpreter.
    set(Python3_EXECUTABLE "${PROJECT_THIRD_PARTY_PYTHON_VENV_EXECUTABLE}")
    set(Python3_EXECUTABLE
        "${PROJECT_THIRD_PARTY_PYTHON_VENV_EXECUTABLE}"
        CACHE FILEPATH "Path to a python3 executable (atsf4g-co ${PROJECT_THIRD_PARTY_PYTHON_VIRTUALENV_MODULE_NAME})"
              FORCE)
  endif()

  # Inject environment variables so that tools spawned from this configure step (and any execute_process below) behave
  # as if the venv were activated.
  set(ENV{VIRTUAL_ENV} "${PROJECT_THIRD_PARTY_PYTHON_VENV_DIR}")
  unset(ENV{PYTHONHOME})
  if(CMAKE_HOST_WIN32)
    set(ENV{PATH} "${PROJECT_THIRD_PARTY_PYTHON_VENV_BIN_DIR};$ENV{PATH}")
  else()
    set(ENV{PATH} "${PROJECT_THIRD_PARTY_PYTHON_VENV_BIN_DIR}:$ENV{PATH}")
  endif()

  # PROJECT_THIRD_PARTY_PYTHON_MODULE_DIR is consumed downstream as the "package prefix" passed via --add-package-prefix
  # to extend sys.path. The virtualenv/venv root has the standard prefix layout (bin|Scripts and
  # lib/pythonX.Y/site-packages or Lib/site-packages) so it works as a prefix.
  set(PROJECT_THIRD_PARTY_PYTHON_MODULE_DIR "${PROJECT_THIRD_PARTY_PYTHON_VENV_DIR}")
else()
  file(TO_CMAKE_PATH
       "${PROJECT_THIRD_PARTY_HOST_INSTALL_DIR}/python.modules/${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}"
       PROJECT_THIRD_PARTY_PYTHON_MODULE_DIR)
endif()

foreach(__python3_rebind_target_location Python3::Interpreter Python3::InterpreterDebug Python::Interpreter
                                         Python::InterpreterDebug)
  if(TARGET ${__python3_rebind_target_location})
    set_target_properties(
      ${__python3_rebind_target_location}
      PROPERTIES IMPORTED_LOCATION "${Python3_EXECUTABLE}"
                 IMPORTED_LOCATION_RELEASE "${Python3_EXECUTABLE}"
                 IMPORTED_LOCATION_DEBUG "${Python3_EXECUTABLE}"
                 IMPORTED_LOCATION_RELWITHDEBINFO "${Python3_EXECUTABLE}"
                 IMPORTED_LOCATION_MINSIZEREL "${Python3_EXECUTABLE}")
  endif()
endforeach()
unset(__python3_rebind_target_location)

if(NOT EXISTS "${PROJECT_THIRD_PARTY_PYTHON_MODULE_DIR}")
  file(MAKE_DIRECTORY "${PROJECT_THIRD_PARTY_PYTHON_MODULE_DIR}")
endif()
message(STATUS "Install dependency python(${Python3_EXECUTABLE}) modules into ${PROJECT_THIRD_PARTY_PYTHON_MODULE_DIR}")

string(TIMESTAMP PROJECT_THIRD_PARTY_PYTHON_ENV_CURRENT_TIMESTAMP "%Y-%m-%d")
if(EXISTS "${PROJECT_THIRD_PARTY_PYTHON_MODULE_DIR}/update-time.txt")
  file(READ "${PROJECT_THIRD_PARTY_PYTHON_MODULE_DIR}/update-time.txt"
       PROJECT_THIRD_PARTY_PYTHON_ENV_PREVIOUS_TIMESTAMP)
  if(PROJECT_THIRD_PARTY_PYTHON_ENV_PREVIOUS_TIMESTAMP STREQUAL PROJECT_THIRD_PARTY_PYTHON_ENV_CURRENT_TIMESTAMP)
    set(PROJECT_THIRD_PARTY_PYTHON_ENV_NEED_UPDATE FALSE)
  else()
    set(PROJECT_THIRD_PARTY_PYTHON_ENV_NEED_UPDATE TRUE)
  endif()
else()
  set(PROJECT_THIRD_PARTY_PYTHON_ENV_NEED_UPDATE TRUE)
endif()

if(PROJECT_THIRD_PARTY_PYTHON_ENV_NEED_UPDATE)
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/_deps/python_env/${PROJECT_PREBUILT_PLATFORM_NAME}")
  if(PROJECT_THIRD_PARTY_PYTHON_VENV_AVAILABLE)
    # Inside the venv: upgrade pip in-place, then install requirements directly.
    execute_process(
      COMMAND "${Python3_EXECUTABLE}" "-m" "pip" "install" ${PROJECT_THIRD_PARTY_PYTHON_PIP_SOURCE} "--upgrade" "pip"
      WORKING_DIRECTORY "${PROJECT_THIRD_PARTY_PYTHON_MODULE_DIR}" COMMAND_ECHO STDOUT)
    execute_process(
      COMMAND "${Python3_EXECUTABLE}" "-m" "pip" "install" ${PROJECT_THIRD_PARTY_PYTHON_PIP_SOURCE} "--upgrade" "-r"
              "${CMAKE_CURRENT_LIST_DIR}/requirements.txt"
      WORKING_DIRECTORY "${PROJECT_THIRD_PARTY_PYTHON_MODULE_DIR}"
      RESULT_VARIABLE PROJECT_THIRD_PARTY_PYTHON_ENV_UPDATE_RESULT COMMAND_ECHO STDOUT)
  else()
    execute_process(
      COMMAND ${Python3_EXECUTABLE} "-m" "pip" "install" ${PROJECT_THIRD_PARTY_PYTHON_PIP_SOURCE} "--upgrade" "--user"
              "pip" WORKING_DIRECTORY "${PROJECT_THIRD_PARTY_PYTHON_MODULE_DIR}" COMMAND_ECHO STDOUT)

    if(CMAKE_HOST_WIN32)
      set(PROJECT_THIRD_PARTY_PYTHON_MODULE_INSTALL_DIR ".")
    else()
      set(PROJECT_THIRD_PARTY_PYTHON_MODULE_INSTALL_DIR "${PROJECT_THIRD_PARTY_PYTHON_MODULE_DIR}")
    endif()
    execute_process(
      COMMAND
        "${Python3_EXECUTABLE}" "-m" "pip" "install" ${PROJECT_THIRD_PARTY_PYTHON_PIP_SOURCE} "--prefix"
        "${PROJECT_THIRD_PARTY_PYTHON_MODULE_INSTALL_DIR}" "--ignore-installed" "-r"
        "${CMAKE_CURRENT_LIST_DIR}/requirements.txt"
      WORKING_DIRECTORY "${PROJECT_THIRD_PARTY_PYTHON_MODULE_DIR}"
      RESULT_VARIABLE PROJECT_THIRD_PARTY_PYTHON_ENV_UPDATE_RESULT COMMAND_ECHO STDOUT)
  endif()

  if(PROJECT_THIRD_PARTY_PYTHON_ENV_UPDATE_RESULT EQUAL 0)
    file(WRITE "${PROJECT_THIRD_PARTY_PYTHON_MODULE_DIR}/update-time.txt"
         "${PROJECT_THIRD_PARTY_PYTHON_ENV_CURRENT_TIMESTAMP}")
  endif()
endif()

unset(PROJECT_THIRD_PARTY_PYTHON_ENV_NEED_UPDATE)
