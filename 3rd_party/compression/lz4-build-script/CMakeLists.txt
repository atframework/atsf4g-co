# Origin from https://github.com/microsoft/vcpkg/blob/master/ports/lz4/CMakeLists.txt and https://github.com/lz4/lz4/blob/dev/build/cmake/CMakeLists.txt

cmake_minimum_required(VERSION 3.0)
project(lz4 C)

option(LZ4_BUILD_CLI "Build lz4 program" ON)
option(LZ4_BUILD_LEGACY_LZ4C "Build lz4c progam with legacy argument support" ON)
option(LZ4_POSITION_INDEPENDENT_LIB
       "Use position independent code for static library (if applicable)" ON)
include(CMakeDependentOption)
cmake_dependent_option(BUILD_SHARED_LIBS "Build shared libraries" ON "NOT LZ4_BUNDLED_MODE" OFF)
cmake_dependent_option(BUILD_STATIC_LIBS "Build static libraries" OFF "BUILD_SHARED_LIBS" ON)

# Parse version information
file(STRINGS "${LZ4_TOP_SOURCE_DIR}/lib/lz4.h" LZ4_VERSION_MAJOR
     REGEX "^#define LZ4_VERSION_MAJOR +([0-9]+) +.*$")
string(REGEX REPLACE "^#define LZ4_VERSION_MAJOR +([0-9]+) +.*$" "\\1" LZ4_VERSION_MAJOR
                     "${LZ4_VERSION_MAJOR}")
file(STRINGS "${LZ4_TOP_SOURCE_DIR}/lib/lz4.h" LZ4_VERSION_MINOR
     REGEX "^#define LZ4_VERSION_MINOR +([0-9]+) +.*$")
string(REGEX REPLACE "^#define LZ4_VERSION_MINOR +([0-9]+) +.*$" "\\1" LZ4_VERSION_MINOR
                     "${LZ4_VERSION_MINOR}")
file(STRINGS "${LZ4_TOP_SOURCE_DIR}/lib/lz4.h" LZ4_VERSION_RELEASE
     REGEX "^#define LZ4_VERSION_RELEASE +([0-9]+) +.*$")
string(REGEX REPLACE "^#define LZ4_VERSION_RELEASE +([0-9]+) +.*$" "\\1" LZ4_VERSION_RELEASE
                     "${LZ4_VERSION_RELEASE}")
set(LZ4_VERSION_STRING "${LZ4_VERSION_MAJOR}.${LZ4_VERSION_MINOR}.${LZ4_VERSION_RELEASE}")
mark_as_advanced(LZ4_VERSION_STRING LZ4_VERSION_MAJOR LZ4_VERSION_MINOR LZ4_VERSION_RELEASE)

set(CMAKE_INSTALL_PREFIX
    "${LZ4_TOP_SOURCE_DIR}"
    CACHE PATH "Install prefix")
include(GNUInstallDirs)

file(GLOB LZ4_HEADERS "${LZ4_TOP_SOURCE_DIR}/lib/*.h")

set(LZ4_LIB_SOURCE_DIR "${LZ4_TOP_SOURCE_DIR}/lib")
set(LZ4_PROG_SOURCE_DIR "${LZ4_TOP_SOURCE_DIR}/programs")

# CLI sources
set(LZ4_SOURCES
    "${LZ4_LIB_SOURCE_DIR}/lz4.c"
    "${LZ4_LIB_SOURCE_DIR}/lz4hc.c"
    "${LZ4_LIB_SOURCE_DIR}/lz4.h"
    "${LZ4_LIB_SOURCE_DIR}/lz4hc.h"
    "${LZ4_LIB_SOURCE_DIR}/lz4frame.c"
    "${LZ4_LIB_SOURCE_DIR}/lz4frame.h"
    "${LZ4_LIB_SOURCE_DIR}/xxhash.c")
set(LZ4_CLI_SOURCES "${LZ4_PROG_SOURCE_DIR}/bench.c" "${LZ4_PROG_SOURCE_DIR}/lz4cli.c"
                    "${LZ4_PROG_SOURCE_DIR}/lz4io.c" "${LZ4_PROG_SOURCE_DIR}/datagen.c")

set(LZ4_TARGETS_BUILT)

if(BUILD_SHARED_LIBS)
  add_library(lz4_shared SHARED ${LZ4_SOURCES})

  target_include_directories(lz4_shared PUBLIC "$<BUILD_INTERFACE:${LZ4_LIB_SOURCE_DIR}>"
                                               "$<INSTALL_INTERFACE:include>")
  set_target_properties(
    lz4_shared
    PROPERTIES OUTPUT_NAME lz4
               PUBLIC_HEADER ${LZ4_HEADERS}
               SOVERSION "${LZ4_VERSION_MAJOR}"
               VERSION "${LZ4_VERSION_STRING}")

  target_compile_definitions(lz4_shared PRIVATE LZ4_DLL_EXPORT=1)
  list(APPEND LZ4_TARGETS_BUILT lz4_shared)
endif()

if(BUILD_STATIC_LIBS)
  add_library(lz4_static STATIC ${LZ4_SOURCES})

  target_include_directories(lz4_static PUBLIC "$<BUILD_INTERFACE:${LZ4_LIB_SOURCE_DIR}>"
                                               "$<INSTALL_INTERFACE:include>")
  set_target_properties(
    lz4_static
    PROPERTIES OUTPUT_NAME lz4
               PUBLIC_HEADER ${LZ4_HEADERS}
               POSITION_INDEPENDENT_CODE ${LZ4_POSITION_INDEPENDENT_LIB})
  list(APPEND LZ4_TARGETS_BUILT lz4_static)
endif()

# link to shared whenever possible, to static otherwise
if(TARGET lz4_shared)
  set(LZ4_LINK_LIBRARY lz4_shared)
else()
  set(LZ4_LINK_LIBRARY lz4_static)
endif()

# lz4
if(LZ4_BUILD_CLI)
  set(LZ4_PROGRAMS_BUILT lz4cli)
  add_executable(lz4cli ${LZ4_CLI_SOURCES})
  target_include_directories(lz4cli PRIVATE "$<BUILD_INTERFACE:${LZ4_LIB_SOURCE_DIR}>"
                                            "$<INSTALL_INTERFACE:include>")
  set_target_properties(lz4cli PROPERTIES OUTPUT_NAME lz4)
  target_link_libraries(lz4cli ${LZ4_LINK_LIBRARY})

  list(APPEND LZ4_TARGETS_BUILT lz4cli)
endif()

# lz4c
if(LZ4_BUILD_LEGACY_LZ4C)
  list(APPEND LZ4_PROGRAMS_BUILT lz4c)
  add_executable(lz4c ${LZ4_CLI_SOURCES})
  target_include_directories(lz4c PRIVATE "$<BUILD_INTERFACE:${LZ4_LIB_SOURCE_DIR}>"
                                          "$<INSTALL_INTERFACE:include>")
  set_target_properties(lz4c PROPERTIES COMPILE_DEFINITIONS "ENABLE_LZ4C_LEGACY_OPTIONS")
  target_link_libraries(lz4c ${LZ4_LINK_LIBRARY})

  list(APPEND LZ4_TARGETS_BUILT lz4c)
endif()

install(
  TARGETS ${LZ4_TARGETS_BUILT}
  EXPORT lz4Config
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  PRIVATE_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT dev)

file(GLOB lz4h "${LZ4_TOP_SOURCE_DIR}/lib/*.h")
list(REMOVE_ITEM lz4h "${LZ4_TOP_SOURCE_DIR}/lib/xxhash.h")
install(FILES ${lz4h} DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

export(
  EXPORT lz4Config
  FILE "${CMAKE_CURRENT_BINARY_DIR}/lz4-config.cmake"
  NAMESPACE lz4::
  # EXPORT_LINK_INTERFACE_LIBRARIES
)

install(
  EXPORT lz4Config
  FILE lz4-config.cmake
  NAMESPACE lz4::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/lz4")
