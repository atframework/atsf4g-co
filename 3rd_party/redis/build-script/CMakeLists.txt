cmake_minimum_required(VERSION 3.4.0)
include(GNUInstallDirs)
project(hiredis)

option(ENABLE_SSL "Build hiredis_ssl for SSL support" OFF)
# OPTION(DISABLE_TESTS "If tests should be compiled or not" OFF)

macro(getVersionBit name)
  set(VERSION_REGEX "^#define ${name} (.+)$")
  file(STRINGS "${3RD_PARTY_REDIS_HIREDIS_DIR}/hiredis.h" VERSION_BIT REGEX ${VERSION_REGEX})
  string(REGEX REPLACE ${VERSION_REGEX} "\\1" ${name} "${VERSION_BIT}")
endmacro(getVersionBit)

getversionbit(HIREDIS_MAJOR)
getversionbit(HIREDIS_MINOR)
getversionbit(HIREDIS_PATCH)
getversionbit(HIREDIS_SONAME)
set(VERSION "${HIREDIS_MAJOR}.${HIREDIS_MINOR}.${HIREDIS_PATCH}")
message("Detected version: ${VERSION}")

project(hiredis VERSION "${VERSION}")

# SET(ENABLE_EXAMPLES OFF CACHE BOOL "Enable building hiredis examples")

set(hiredis_check_sources
    "${3RD_PARTY_REDIS_HIREDIS_DIR}/async.c"
    "${3RD_PARTY_REDIS_HIREDIS_DIR}/dict.c"
    "${3RD_PARTY_REDIS_HIREDIS_DIR}/hiredis.c"
    "${3RD_PARTY_REDIS_HIREDIS_DIR}/net.c"
    "${3RD_PARTY_REDIS_HIREDIS_DIR}/read.c"
    "${3RD_PARTY_REDIS_HIREDIS_DIR}/sds.c"
    "${3RD_PARTY_REDIS_HIREDIS_DIR}/sockcompat.c"
    "${3RD_PARTY_REDIS_HIREDIS_DIR}/alloc.c")

foreach(SRC_FILE ${hiredis_check_sources})
  if(EXISTS ${SRC_FILE})
    list(APPEND hiredis_sources ${SRC_FILE})
  endif()
endforeach()

if(WIN32 AND EXISTS "${3RD_PARTY_REDIS_HIREDIS_DIR}/hiredis.def")
  list(APPEND hiredis_sources "${3RD_PARTY_REDIS_HIREDIS_DIR}/hiredis.def")
endif()

add_library(hiredis SHARED ${hiredis_sources})

set_target_properties(hiredis PROPERTIES VERSION "${HIREDIS_SONAME}")
if(WIN32 OR MINGW)
  target_link_libraries(hiredis PUBLIC ws2_32)
endif()
target_include_directories(hiredis PUBLIC "$<BUILD_INTERFACE:${3RD_PARTY_REDIS_HIREDIS_DIR}>"
                                          "$<INSTALL_INTERFACE:include>")

if(EXISTS "${3RD_PARTY_REDIS_HIREDIS_DIR}/hiredis.pc.in")
  configure_file("${3RD_PARTY_REDIS_HIREDIS_DIR}/hiredis.pc.in"
                 "${CMAKE_CURRENT_BINARY_DIR}/hiredis.pc" @ONLY)
endif()

# INSTALL(TARGETS hiredis DESTINATION "${CMAKE_INSTALL_LIBDIR}")

install(
  TARGETS hiredis
  EXPORT hiredis-target
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(
  FILES "${3RD_PARTY_REDIS_HIREDIS_DIR}/hiredis.h" "${3RD_PARTY_REDIS_HIREDIS_DIR}/read.h"
        "${3RD_PARTY_REDIS_HIREDIS_DIR}/sds.h" "${3RD_PARTY_REDIS_HIREDIS_DIR}/async.h"
        "${3RD_PARTY_REDIS_HIREDIS_DIR}/alloc.h" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/hiredis)

install(DIRECTORY "${3RD_PARTY_REDIS_HIREDIS_DIR}/adapters"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/hiredis)

if(EXISTS "${3RD_PARTY_REDIS_HIREDIS_DIR}/hiredis.pc.in")
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hiredis.pc"
          DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
endif()

if(ENABLE_SSL AND EXISTS "${3RD_PARTY_REDIS_HIREDIS_DIR}/hiredis_ssl.h")
  if(NOT OPENSSL_ROOT_DIR)
    if(APPLE)
      set(OPENSSL_ROOT_DIR "/usr/local/opt/openssl")
    endif()
  endif()
  find_package(OpenSSL REQUIRED)
  add_library(hiredis_ssl SHARED "${3RD_PARTY_REDIS_HIREDIS_DIR}/ssl.c")
  target_include_directories(hiredis_ssl PRIVATE "$<BUILD_INTERFACE:OPENSSL_INCLUDE_DIR}>")
  target_link_libraries(hiredis_ssl PRIVATE ${OPENSSL_LIBRARIES})

  if(EXISTS "${3RD_PARTY_REDIS_HIREDIS_DIR}/hiredis_ssl.pc.in")
    configure_file("${3RD_PARTY_REDIS_HIREDIS_DIR}/hiredis_ssl.pc.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/hiredis_ssl.pc" @ONLY)
  endif()

  # INSTALL(TARGETS hiredis_ssl DESTINATION "${CMAKE_INSTALL_LIBDIR}")
  install(
    TARGETS hiredis_ssl
    EXPORT hiredis-target
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

  install(FILES "${3RD_PARTY_REDIS_HIREDIS_DIR}/hiredis_ssl.h"
          DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/hiredis)

  if(EXISTS "${3RD_PARTY_REDIS_HIREDIS_DIR}/hiredis_ssl.pc.in")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/hiredis_ssl.pc"
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
  endif()
endif()

# IF(NOT (DISABLE_TESTS OR (WIN32 OR MINGW))) ENABLE_TESTING() ADD_EXECUTABLE(hiredis-test test.c)
# TARGET_LINK_LIBRARIES(hiredis-test hiredis) ADD_TEST(NAME hiredis-test COMMAND
# ${CMAKE_CURRENT_SOURCE_DIR}/test.sh) ENDIF()

# Add examples IF(ENABLE_EXAMPLES) ADD_SUBDIRECTORY(examples) ENDIF(ENABLE_EXAMPLES)

export(
  TARGETS hiredis
  FILE "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/hiredis-target.cmake"
  NAMESPACE hiredis::)

install(
  EXPORT hiredis-target
  NAMESPACE "hiredis::"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake")
