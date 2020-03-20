CMAKE_MINIMUM_REQUIRED(VERSION 3.4.0)
INCLUDE(GNUInstallDirs)
PROJECT(hiredis)

OPTION(ENABLE_SSL "Build hiredis_ssl for SSL support" OFF)
# OPTION(DISABLE_TESTS "If tests should be compiled or not" OFF)

MACRO(getVersionBit name)
  SET(VERSION_REGEX "^#define ${name} (.+)$")
  FILE(STRINGS "${3RD_PARTY_REDIS_HIREDIS_DIR}/hiredis.h"
    VERSION_BIT REGEX ${VERSION_REGEX})
  STRING(REGEX REPLACE ${VERSION_REGEX} "\\1" ${name} "${VERSION_BIT}")
ENDMACRO(getVersionBit)

getVersionBit(HIREDIS_MAJOR)
getVersionBit(HIREDIS_MINOR)
getVersionBit(HIREDIS_PATCH)
getVersionBit(HIREDIS_SONAME)
SET(VERSION "${HIREDIS_MAJOR}.${HIREDIS_MINOR}.${HIREDIS_PATCH}")
MESSAGE("Detected version: ${VERSION}")

PROJECT(hiredis VERSION "${VERSION}")

# SET(ENABLE_EXAMPLES OFF CACHE BOOL "Enable building hiredis examples")

SET(hiredis_check_sources
    "${3RD_PARTY_REDIS_HIREDIS_DIR}/async.c"
    "${3RD_PARTY_REDIS_HIREDIS_DIR}/dict.c"
    "${3RD_PARTY_REDIS_HIREDIS_DIR}/hiredis.c"
    "${3RD_PARTY_REDIS_HIREDIS_DIR}/net.c"
    "${3RD_PARTY_REDIS_HIREDIS_DIR}/read.c"
    "${3RD_PARTY_REDIS_HIREDIS_DIR}/sds.c"
    "${3RD_PARTY_REDIS_HIREDIS_DIR}/sockcompat.c"
    "${3RD_PARTY_REDIS_HIREDIS_DIR}/alloc.c")

foreach(SRC_FILE ${hiredis_check_sources})
    if (EXISTS ${SRC_FILE})
        list(APPEND hiredis_sources ${SRC_FILE})
    endif()
endforeach()

IF(WIN32)
    list(APPEND hiredis_sources
        "${3RD_PARTY_REDIS_HIREDIS_DIR}/hiredis.def"
    )
ENDIF()

ADD_LIBRARY(hiredis SHARED ${hiredis_sources})

SET_TARGET_PROPERTIES(hiredis
    PROPERTIES
    VERSION "${HIREDIS_SONAME}")
IF(WIN32 OR MINGW)
    TARGET_LINK_LIBRARIES(hiredis PUBLIC ws2_32)
ENDIF()
TARGET_INCLUDE_DIRECTORIES(hiredis
PUBLIC 
    "$<BUILD_INTERFACE:${3RD_PARTY_REDIS_HIREDIS_DIR}>"
    "$<INSTALL_INTERFACE:include>"
)

if (EXISTS "${3RD_PARTY_REDIS_HIREDIS_DIR}/hiredis.pc.in")
    CONFIGURE_FILE("${3RD_PARTY_REDIS_HIREDIS_DIR}/hiredis.pc.in" "${CMAKE_CURRENT_BINARY_DIR}/hiredis.pc" @ONLY)
endif()

#INSTALL(TARGETS hiredis
#    DESTINATION "${CMAKE_INSTALL_LIBDIR}")

install(TARGETS hiredis
    EXPORT hiredis-target
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

INSTALL(FILES 
    "${3RD_PARTY_REDIS_HIREDIS_DIR}/hiredis.h" 
    "${3RD_PARTY_REDIS_HIREDIS_DIR}/read.h"
    "${3RD_PARTY_REDIS_HIREDIS_DIR}/sds.h"
    "${3RD_PARTY_REDIS_HIREDIS_DIR}/async.h"
    "${3RD_PARTY_REDIS_HIREDIS_DIR}/alloc.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/hiredis)
    
INSTALL(DIRECTORY "${3RD_PARTY_REDIS_HIREDIS_DIR}/adapters"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/hiredis)

if (EXISTS "${3RD_PARTY_REDIS_HIREDIS_DIR}/hiredis.pc.in")
    INSTALL(FILES "${CMAKE_CURRENT_BINARY_DIR}/hiredis.pc"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
endif()

IF(ENABLE_SSL AND EXISTS "${3RD_PARTY_REDIS_HIREDIS_DIR}/hiredis_ssl.h")
    IF (NOT OPENSSL_ROOT_DIR)
        IF (APPLE)
            SET(OPENSSL_ROOT_DIR "/usr/local/opt/openssl")
        ENDIF()
    ENDIF()
    FIND_PACKAGE(OpenSSL REQUIRED)
    ADD_LIBRARY(hiredis_ssl SHARED
        "${3RD_PARTY_REDIS_HIREDIS_DIR}/ssl.c")
    TARGET_INCLUDE_DIRECTORIES(hiredis_ssl PRIVATE "$<BUILD_INTERFACE:OPENSSL_INCLUDE_DIR}>")
    TARGET_LINK_LIBRARIES(hiredis_ssl PRIVATE ${OPENSSL_LIBRARIES})

    if (EXISTS "${3RD_PARTY_REDIS_HIREDIS_DIR}/hiredis_ssl.pc.in")
        CONFIGURE_FILE("${3RD_PARTY_REDIS_HIREDIS_DIR}/hiredis_ssl.pc.in" "${CMAKE_CURRENT_BINARY_DIR}/hiredis_ssl.pc" @ONLY)
    endif ()

    # INSTALL(TARGETS hiredis_ssl
    #     DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    install(TARGETS hiredis_ssl
        EXPORT hiredis-target
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )

    INSTALL(FILES "${3RD_PARTY_REDIS_HIREDIS_DIR}/hiredis_ssl.h"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/hiredis)
    
    if (EXISTS "${3RD_PARTY_REDIS_HIREDIS_DIR}/hiredis_ssl.pc.in")
        INSTALL(FILES "${CMAKE_CURRENT_BINARY_DIR}/hiredis_ssl.pc"
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
    endif()
ENDIF()

# IF(NOT (DISABLE_TESTS OR (WIN32 OR MINGW)))
#     ENABLE_TESTING()
#     ADD_EXECUTABLE(hiredis-test test.c)
#     TARGET_LINK_LIBRARIES(hiredis-test hiredis)
#     ADD_TEST(NAME hiredis-test
#         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test.sh)
# ENDIF()

# Add examples
# IF(ENABLE_EXAMPLES)
#   ADD_SUBDIRECTORY(examples)
# ENDIF(ENABLE_EXAMPLES)

export(TARGETS hiredis
    FILE "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/hiredis-target.cmake"
    NAMESPACE hiredis::
)

install(
    EXPORT hiredis-target
    NAMESPACE "hiredis::"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake"
)
