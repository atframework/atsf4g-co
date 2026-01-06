include_guard(GLOBAL)

set(PROJECT_THIRD_PARTY_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}")

# Cleanup old prebuilt
project_git_get_ambiguous_name(ATFRAMEWORK_CMAKE_TOOLSET_VERSION "${ATFRAMEWORK_CMAKE_TOOLSET_DIR}")
string(STRIP "${ATFRAMEWORK_CMAKE_TOOLSET_VERSION}" ATFRAMEWORK_CMAKE_TOOLSET_VERSION)
if(EXISTS "${PROJECT_THIRD_PARTY_INSTALL_DIR}/${CMAKE_INSTALL_INCLUDEDIR}/google/protobuf/message.h")
  if(EXISTS "${PROJECT_THIRD_PARTY_INSTALL_DIR}/.cmake-toolset.version")
    file(READ "${PROJECT_THIRD_PARTY_INSTALL_DIR}/.cmake-toolset.version" ATFRAMEWORK_CMAKE_TOOLSET_PREBUILT_VERSION)
    string(STRIP "${ATFRAMEWORK_CMAKE_TOOLSET_PREBUILT_VERSION}" ATFRAMEWORK_CMAKE_TOOLSET_PREBUILT_VERSION)
    if(NOT ATFRAMEWORK_CMAKE_TOOLSET_PREBUILT_VERSION STREQUAL ATFRAMEWORK_CMAKE_TOOLSET_VERSION)
      set(ATFRAMEWORK_CMAKE_TOOLSET_NEED_UPGRADE TRUE)
    endif()
  endif()
endif()
if(EXISTS "${PROJECT_THIRD_PARTY_HOST_INSTALL_DIR}/${CMAKE_INSTALL_INCLUDEDIR}/google/protobuf/message.h")
  if(EXISTS "${PROJECT_THIRD_PARTY_HOST_INSTALL_DIR}/.cmake-toolset.version")
    file(READ "${PROJECT_THIRD_PARTY_HOST_INSTALL_DIR}/.cmake-toolset.version"
         ATFRAMEWORK_CMAKE_TOOLSET_PREBUILT_VERSION)
    string(STRIP "${ATFRAMEWORK_CMAKE_TOOLSET_PREBUILT_VERSION}" ATFRAMEWORK_CMAKE_TOOLSET_PREBUILT_VERSION)
    if(NOT ATFRAMEWORK_CMAKE_TOOLSET_PREBUILT_VERSION STREQUAL ATFRAMEWORK_CMAKE_TOOLSET_VERSION)
      set(ATFRAMEWORK_CMAKE_TOOLSET_NEED_UPGRADE TRUE)
    endif()
  endif()
endif()
if(ATFRAMEWORK_CMAKE_TOOLSET_NEED_UPGRADE)
  if(PROJECT_THIRD_PARTY_HOST_INSTALL_DIR STREQUAL PROJECT_THIRD_PARTY_INSTALL_DIR)
    message(
      FATAL_ERROR
        "cmake-toolset upgraded, please remove these directories/files and run cmake again
    - ${PROJECT_THIRD_PARTY_INSTALL_DIR}
    - ${CMAKE_BINARY_DIR}/CMakeCache.txt")
  else()
    message(
      FATAL_ERROR
        "cmake-toolset upgraded, please remove these directories/files and run cmake again
    - ${PROJECT_THIRD_PARTY_INSTALL_DIR}
    - ${PROJECT_THIRD_PARTY_HOST_INSTALL_DIR}
    - ${CMAKE_BINARY_DIR}/CMakeCache.txt")
  endif()
endif()

# ============ third party mirror ============
if(PROJECT_GIT_USE_MIRROR AND PROJECT_GITHUB_GIT_HTTP_MIRROR)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} config --local --unset-all "url.${PROJECT_GITHUB_GIT_HTTP_MIRROR}.insteadOf"
    COMMAND ${GIT_EXECUTABLE} config --add --local "url.${PROJECT_GITHUB_GIT_HTTP_MIRROR}.insteadOf"
            "https://github.com/"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
else()
  execute_process(COMMAND ${GIT_EXECUTABLE} config --local --unset-all "url.${PROJECT_GITHUB_GIT_HTTP_MIRROR}.insteadOf"
                  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
endif()

# ============ third party - package alias ============
function(project_third_party_make_package_alias_dir)
  file(REAL_PATH "${PROJECT_THIRD_PARTY_PACKAGE_DIR}" _rel_pkg_path)
  file(TO_CMAKE_PATH "${_rel_pkg_path}" _rel_pkg_path)
  file(REAL_PATH "${CMAKE_CURRENT_LIST_DIR}" _alias_pkg_path_base)
  if(_rel_pkg_path STREQUAL "${_alias_pkg_path_base}/packages")
    return()
  endif()
  message(STATUS "Try to creating package directory alias: ${_alias_pkg_path} -> ${_rel_pkg_path}")

  if(EXISTS "${_alias_pkg_path_base}/packages")
    file(REMOVE_RECURSE "${_alias_pkg_path_base}/packages")
  endif()

  if (NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
    file(CREATE_LINK "${_rel_pkg_path}" "${_alias_pkg_path_base}/packages" SYMBOLIC)
  endif()
endfunction()
project_third_party_make_package_alias_dir()

# ============ third party ============
project_third_party_include_port("compression/import.cmake")
if(NOT ANDROID AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
    project_third_party_include_port("malloc/jemalloc.cmake")
  endif()
  project_third_party_include_port("malloc/mimalloc.cmake")
  #[[
  # There is a BUG in gcc 4.6-4.8 and finxed in gcc 4.9
  #   @see https://gcc.gnu.org/bugzilla/show_bug.cgi?id=58016
  #   @see https://gcc.gnu.org/gcc-4.9/changes.html
  #]]
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux"
     AND NOT PROJECT_COMPILER_OPTIONS_TARGET_USE_SANITIZER
     AND (NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "4.9"))
    project_third_party_include_port("libunwind/libunwind.cmake")
  endif()
endif()
project_third_party_include_port("algorithm/xxhash.cmake")
project_third_party_include_port("algorithm/tbb.cmake")
project_third_party_include_port("libuv/libuv.cmake")
project_third_party_include_port("lua/lua.cmake")
project_third_party_include_port("fmtlib/fmtlib.cmake")
project_third_party_include_port("yaml-cpp/yaml-cpp.cmake")
project_third_party_include_port("json/rapidjson.cmake")
project_third_party_include_port("json/nlohmann_json.cmake")
project_third_party_include_port("flatbuffers/flatbuffers.cmake")
project_third_party_include_port("ssl/port.cmake")
project_third_party_include_port("redis/hiredis.cmake")
project_third_party_include_port("cares/c-ares.cmake")
project_third_party_include_port("abseil-cpp/abseil-cpp.cmake")
project_third_party_include_port("re2/re2.cmake")
project_third_party_include_port("ngtcp2/nghttp3.cmake")
project_third_party_include_port("ngtcp2/ngtcp2.cmake")
project_third_party_include_port("nghttp2/nghttp2.cmake")
project_third_party_include_port("libcurl/libcurl.cmake")

# Set stack size to 512KB, the default value is 100K and will overflow with asan
set(ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_CIVETWEB_THREAD_STACK_SIZE "524288")
project_third_party_include_port("web/civetweb.cmake")
# project_third_party_include_port("web/libwebsockets.cmake")

set(ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROTOBUF_ALLOW_SHARED_LIBS
    ON
    CACHE BOOL "Allow build protobuf as dynamic(May cause duplicate symbol[File already exists in database])")
project_third_party_include_port("protobuf/protobuf.cmake")
project_third_party_include_port("grpc/import.cmake")
#
project_third_party_include_port("telemetry/prometheus-cpp.cmake")
if(MSVC AND MSVC_VERSION LESS 1920)
  message(STATUS "Opentelemetry-cpp only support Visual Studio 2019 and upper. Skip it for
MSVC_VERSION=${MSVC_VERSION}")
else()
  project_third_party_include_port("telemetry/opentelemetry-cpp.cmake")
endif()

project_third_party_include_port("libcopp/libcopp.cmake")

# =========== third_party - python_env ===========
include("${CMAKE_CURRENT_LIST_DIR}/python_env/python_env.cmake")

# =========== third_party - xres-code-generator and xresloader ===========
include("${CMAKE_CURRENT_LIST_DIR}/xres-code-generator/xres-code-generator.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/xresloader/xresloader.cmake")

# =========== third_party - cfssl ===========
include("${CMAKE_CURRENT_LIST_DIR}/cfssl/cfssl.cmake")

# =========== third_party - atdtool ===========
include("${CMAKE_CURRENT_LIST_DIR}/atdtool/atdtool.cmake")

# =========== third_party - otel ===========
include("${CMAKE_CURRENT_LIST_DIR}/otel/otelcol-contrib.cmake")

# =========== set dependency variables ===========
# Changes in otel-cpp v1.8.0
if(TARGET opentelemetry-cpp::prometheus_exporter)
  set(OPENTELEMETRY_CPP_PROMETHUS_EXPORTER_NAME opentelemetry-cpp::prometheus_exporter)
else()
  set(OPENTELEMETRY_CPP_PROMETHUS_EXPORTER_NAME opentelemetry-cpp::opentelemetry_exporter_prometheus)
endif()
if(TARGET opentelemetry-cpp::otlp_http_log_record_exporter)
  set(OPENTELEMETRY_CPP_LOG_RECORD_EXPORTER_NAME
      opentelemetry-cpp::otlp_http_log_record_exporter opentelemetry-cpp::otlp_grpc_log_record_exporter
      opentelemetry-cpp::otlp_file_log_record_exporter opentelemetry-cpp::ostream_log_record_exporter)
else()
  set(OPENTELEMETRY_CPP_LOG_RECORD_EXPORTER_NAME
      opentelemetry-cpp::otlp_http_log_exporter opentelemetry-cpp::otlp_grpc_log_exporter
      opentelemetry-cpp::ostream_log_exporter)
endif()

set(PROJECT_THIRD_PARTY_PUBLIC_LINK_NAMES
    opentelemetry-cpp::otlp_http_exporter
    opentelemetry-cpp::otlp_grpc_exporter
    opentelemetry-cpp::otlp_file_exporter
    ${OPENTELEMETRY_CPP_PROMETHUS_EXPORTER_NAME}
    ${OPENTELEMETRY_CPP_LOG_RECORD_EXPORTER_NAME}
    opentelemetry-cpp::otlp_http_metric_exporter
    opentelemetry-cpp::otlp_grpc_metrics_exporter
    opentelemetry-cpp::otlp_file_metric_exporter
    opentelemetry-cpp::ostream_span_exporter
    opentelemetry-cpp::ostream_metrics_exporter
    opentelemetry-cpp::resources
    opentelemetry-cpp::metrics
    opentelemetry-cpp::sdk
    opentelemetry-cpp::api
    ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROMETHEUS_CPP_LINK_NAME}
    ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_GRPC_LINK_NAME}
    ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROTOBUF_LINK_NAME}
    # ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_FLATBUFFERS_LINK_NAME}
    ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_LIBCOPP_LINK_NAME}
    ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_NLOHMANN_JSON_LINK_NAME}
    ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_RAPIDJSON_LINK_NAME}
    ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_YAML_CPP_LINK_NAME}
    ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_FMTLIB_LINK_NAME}
    ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_LIBCURL_LINK_NAME}
    ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_RE2_LINK_NAME}
    ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_HIREDIS_LINK_NAME}
    ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_LIBUV_LINK_NAME}
    ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_CRYPT_LINK_NAME}
    ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_XXHASH_LINK_NAME}
    ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_ZSTD_LINK_NAME}
    ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_GSL_LINK_NAME}
    ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_LZ4_LINK_NAME}
    ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_ZLIB_LINK_NAME})

if(TARGET absl::abseil_dll)
  list(APPEND PROJECT_THIRD_PARTY_PUBLIC_LINK_NAMES absl::abseil_dll)
else()
  foreach(PROJECT_THIRD_PARTY_PUBLIC_LINK_ABSL_NAME "absl::string_view" "absl::strings" "absl::str_format")
    if(TARGET ${PROJECT_THIRD_PARTY_PUBLIC_LINK_ABSL_NAME})
      list(APPEND PROJECT_THIRD_PARTY_PUBLIC_LINK_NAMES ${PROJECT_THIRD_PARTY_PUBLIC_LINK_ABSL_NAME})
    endif()
  endforeach()
endif()

set(PROJECT_THIRD_PARTY_PUBLIC_INCLUDE_DIRS)

# Write version
file(WRITE "${PROJECT_THIRD_PARTY_INSTALL_DIR}/.cmake-toolset.version" "${ATFRAMEWORK_CMAKE_TOOLSET_VERSION}")
if(NOT PROJECT_THIRD_PARTY_HOST_INSTALL_DIR STREQUAL PROJECT_THIRD_PARTY_INSTALL_DIR)
  file(WRITE "${PROJECT_THIRD_PARTY_HOST_INSTALL_DIR}/.cmake-toolset.version" "${ATFRAMEWORK_CMAKE_TOOLSET_VERSION}")
endif()
