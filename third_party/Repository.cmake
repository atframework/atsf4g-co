include_guard(GLOBAL)

include("${ATFRAMEWORK_CMAKE_TOOLSET_DIR}/ports/compression/import.cmake")
if(NOT ANDROID AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  include("${ATFRAMEWORK_CMAKE_TOOLSET_DIR}/ports/jemalloc/jemalloc.cmake")
  #[[
  # There is a BUG in gcc 4.6-4.8 and finxed in gcc 4.9
  #   @see https://gcc.gnu.org/bugzilla/show_bug.cgi?id=58016
  #   @see https://gcc.gnu.org/gcc-4.9/changes.html
  #]]
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND (NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_VERSION
                                                                                         VERSION_GREATER_EQUAL "4.9"))
    include("${ATFRAMEWORK_CMAKE_TOOLSET_DIR}/ports/libunwind/libunwind.cmake")
  endif()
endif()
include("${ATFRAMEWORK_CMAKE_TOOLSET_DIR}/ports/algorithm/xxhash.cmake")
include("${ATFRAMEWORK_CMAKE_TOOLSET_DIR}/ports/libuv/libuv.cmake")
include("${ATFRAMEWORK_CMAKE_TOOLSET_DIR}/ports/lua/lua.cmake")
include("${ATFRAMEWORK_CMAKE_TOOLSET_DIR}/ports/fmtlib/fmtlib.cmake")
include("${ATFRAMEWORK_CMAKE_TOOLSET_DIR}/ports/yaml-cpp/yaml-cpp.cmake")
include("${ATFRAMEWORK_CMAKE_TOOLSET_DIR}/ports/json/rapidjson.cmake")
include("${ATFRAMEWORK_CMAKE_TOOLSET_DIR}/ports/json/nlohmann_json.cmake")
include("${ATFRAMEWORK_CMAKE_TOOLSET_DIR}/ports/flatbuffers/flatbuffers.cmake")
include("${ATFRAMEWORK_CMAKE_TOOLSET_DIR}/ports/ssl/port.cmake")
include("${ATFRAMEWORK_CMAKE_TOOLSET_DIR}/ports/redis/hiredis.cmake")
include("${ATFRAMEWORK_CMAKE_TOOLSET_DIR}/ports/cares/c-ares.cmake")
include("${ATFRAMEWORK_CMAKE_TOOLSET_DIR}/ports/re2/re2.cmake")
include("${ATFRAMEWORK_CMAKE_TOOLSET_DIR}/ports/libcurl/libcurl.cmake")
include("${ATFRAMEWORK_CMAKE_TOOLSET_DIR}/ports/web/civetweb.cmake")
include("${ATFRAMEWORK_CMAKE_TOOLSET_DIR}/ports/web/libwebsockets.cmake")
if(VCPKG_TOOLCHAIN
   AND MSVC
   AND MSVC_VERSION GREATER_EQUAL 1929)
  message(WARNING "Current protobuf in vcpkg is too old to support MSVC 1929(VS 16.10)")
else()
  include("${ATFRAMEWORK_CMAKE_TOOLSET_DIR}/ports/protobuf/protobuf.cmake")
  include("${ATFRAMEWORK_CMAKE_TOOLSET_DIR}/ports/grpc/import.cmake")
  #
  include("${ATFRAMEWORK_CMAKE_TOOLSET_DIR}/ports/telemetry/prometheus-cpp.cmake")
  if(MSVC AND MSVC_VERSION LESS 1920)
    message(STATUS "Opentelemetry-cpp only support Visual Studio 2019 and upper. Skip it for
  MSVC_VERSION=${MSVC_VERSION}")
  else()
    include("${ATFRAMEWORK_CMAKE_TOOLSET_DIR}/ports/telemetry/opentelemetry-cpp.cmake")
  endif()
endif()

include("${ATFRAMEWORK_CMAKE_TOOLSET_DIR}/ports/libcopp/libcopp.cmake")

# =========== third_party - python_env ===========
include("${CMAKE_CURRENT_LIST_DIR}/python_env/python_env.cmake")

# =========== third_party - xres-code-generator and xresloader ===========
include("${CMAKE_CURRENT_LIST_DIR}/xres-code-generator/xres-code-generator.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/xresloader/xresloader.cmake")

# =========== set dependency variables ===========
set(PROJECT_THIRD_PARTY_PUBLIC_LINK_NAMES
    opentelemetry-cpp::otlp_http_exporter
    opentelemetry-cpp::otlp_grpc_exporter
    opentelemetry-cpp::prometheus_exporter
    opentelemetry-cpp::otlp_http_metric_exporter
    opentelemetry-cpp::otlp_grpc_metrics_exporter
    opentelemetry-cpp::otlp_http_log_exporter
    opentelemetry-cpp::otlp_grpc_log_exporter
    opentelemetry-cpp::ostream_log_exporter
    opentelemetry-cpp::ostream_span_exporter
    opentelemetry-cpp::ostream_metrics_exporter
    opentelemetry-cpp::resources
    opentelemetry-cpp::metrics
    opentelemetry-cpp::sdk
    opentelemetry-cpp::api
    ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROMETHEUS_CPP_LINK_NAME}
    ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_GRPC_LINK_NAME}
    ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_PROTOBUF_LINK_NAME}
    ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_FLATBUFFERS_LINK_NAME}
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
    ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_LZ4_LINK_NAME}
    ${ATFRAMEWORK_CMAKE_TOOLSET_THIRD_PARTY_ZLIB_LINK_NAME})
set(PROJECT_THIRD_PARTY_PUBLIC_INCLUDE_DIRS)
