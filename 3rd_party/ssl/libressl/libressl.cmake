# =========== 3rdparty libressl ==================
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.10")
  include_guard(GLOBAL)
endif()

macro(PROJECT_3RD_PARTY_LIBRESSL_IMPORT)
  if(LIBRESSL_FOUND)
    set(OPENSSL_FOUND
        ${LIBRESSL_FOUND}
        CACHE BOOL "using libressl for erplacement of openssl" FORCE)
    set(OPENSSL_INCLUDE_DIR
        ${LIBRESSL_INCLUDE_DIR}
        CACHE PATH "libressl include dir" FORCE)
    set(OPENSSL_CRYPTO_LIBRARY
        ${LIBRESSL_CRYPTO_LIBRARY}
        CACHE STRING "libressl crypto libs" FORCE)
    set(OPENSSL_CRYPTO_LIBRARIES
        ${LIBRESSL_CRYPTO_LIBRARY}
        CACHE STRING "libressl crypto libs" FORCE)
    set(OPENSSL_SSL_LIBRARY
        ${LIBRESSL_SSL_LIBRARY}
        CACHE STRING "libressl ssl libs" FORCE)
    set(OPENSSL_SSL_LIBRARIES
        ${LIBRESSL_SSL_LIBRARY}
        CACHE STRING "libressl ssl libs" FORCE)
    set(OPENSSL_LIBRARIES
        ${LIBRESSL_LIBRARIES}
        CACHE STRING "libressl all libs" FORCE)
    set(OPENSSL_VERSION
        "1.1.0"
        CACHE STRING "openssl version of libressl" FORCE)

    add_library(OpenSSL::Crypto ALIAS LibreSSL::Crypto)
    add_library(OpenSSL::SSL ALIAS LibreSSL::SSL)

    if(TARGET LibreSSL::TLS)
      list(APPEND 3RD_PARTY_CRYPT_LINK_NAME LibreSSL::TLS)
      list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES LibreSSL::TLS)

      if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        project_build_tools_patch_imported_link_interface_libraries(LibreSSL::Crypto ADD_LIBRARIES
                                                                    Bcrypt)
        project_build_tools_patch_imported_link_interface_libraries(LibreSSL::TLS ADD_LIBRARIES
                                                                    Bcrypt)
      endif()
    else()
      if(LIBRESSL_INCLUDE_DIR)
        list(APPEND 3RD_PARTY_PUBLIC_INCLUDE_DIRS ${LIBRESSL_INCLUDE_DIR})
      endif()

      if(LIBRESSL_LIBRARIES)
        list(APPEND 3RD_PARTY_CRYPT_LINK_NAME ${LIBRESSL_LIBRARIES})
        list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES ${LIBRESSL_LIBRARIES})
      endif()
    endif()

    if(NOT CRYPTO_USE_LIBRESSL)
      set(CRYPTO_USE_LIBRESSL
          TRUE
          CACHE BOOL "Cache ssl selector and directly use libressl next time")
    endif()
  endif()
endmacro()

if(NOT 3RD_PARTY_CRYPT_LINK_NAME)
  set(3RD_PARTY_LIBRESSL_DEFAULT_VERSION "3.3.1")

  if(VCPKG_TOOLCHAIN)
    find_package(LibreSSL QUIET)
    project_3rd_party_libressl_import()
  endif()

  if(NOT 3RD_PARTY_LIBRESSL_INCLUDE_MODULE_DIR)
    set(3RD_PARTY_LIBRESSL_INCLUDE_MODULE_DIR TRUE)
    list(APPEND CMAKE_MODULE_PATH
         "${PROJECT_3RD_PARTY_PACKAGE_DIR}/libressl-${3RD_PARTY_LIBRESSL_DEFAULT_VERSION}")
  endif()

  if(NOT LIBRESSL_FOUND)
    set(OPENSSL_ROOT_DIR ${PROJECT_3RD_PARTY_INSTALL_DIR})
    set(LIBRESSL_ROOT_DIR ${PROJECT_3RD_PARTY_INSTALL_DIR})
    set(LibreSSL_ROOT ${PROJECT_3RD_PARTY_INSTALL_DIR})

    if(EXISTS "${PROJECT_3RD_PARTY_PACKAGE_DIR}/libressl-${3RD_PARTY_LIBRESSL_DEFAULT_VERSION}")
      find_package(LibreSSL QUIET)
      project_3rd_party_libressl_import()
    endif()
  endif()

  if(NOT LIBRESSL_FOUND)
    echowithcolor(COLOR GREEN "-- Try to configure and use libressl")
    unset(OPENSSL_FOUND CACHE)
    unset(OPENSSL_INCLUDE_DIR CACHE)
    unset(OPENSSL_CRYPTO_LIBRARY CACHE)
    unset(OPENSSL_CRYPTO_LIBRARIES CACHE)
    unset(OPENSSL_SSL_LIBRARY CACHE)
    unset(OPENSSL_SSL_LIBRARIES CACHE)
    unset(OPENSSL_LIBRARIES CACHE)
    unset(OPENSSL_VERSION CACHE)

    # patch for old build script
    if(EXISTS
       "${PROJECT_3RD_PARTY_PACKAGE_DIR}/libressl-${3RD_PARTY_LIBRESSL_DEFAULT_VERSION}/CMakeCache.txt"
    )
      execute_process(
        COMMAND ${CMAKE_COMMAND} -E remove_directory
                "${PROJECT_3RD_PARTY_PACKAGE_DIR}/libressl-${3RD_PARTY_LIBRESSL_DEFAULT_VERSION}"
        WORKING_DIRECTORY ${PROJECT_3RD_PARTY_PACKAGE_DIR})
    endif()

    if(NOT EXISTS "${PROJECT_3RD_PARTY_PACKAGE_DIR}/libressl-${3RD_PARTY_LIBRESSL_DEFAULT_VERSION}")
      if(NOT EXISTS
         "${PROJECT_3RD_PARTY_PACKAGE_DIR}/libressl-${3RD_PARTY_LIBRESSL_DEFAULT_VERSION}.tar.gz")
        findconfigurepackagedownloadfile(
          "https://ftp.openbsd.org/pub/OpenBSD/LibreSSL/libressl-${3RD_PARTY_LIBRESSL_DEFAULT_VERSION}.tar.gz"
          "${PROJECT_3RD_PARTY_PACKAGE_DIR}/libressl-${3RD_PARTY_LIBRESSL_DEFAULT_VERSION}.tar.gz")
      endif()

      execute_process(
        COMMAND
          ${CMAKE_COMMAND} -E tar xvf
          "${PROJECT_3RD_PARTY_PACKAGE_DIR}/libressl-${3RD_PARTY_LIBRESSL_DEFAULT_VERSION}.tar.gz"
        WORKING_DIRECTORY ${PROJECT_3RD_PARTY_PACKAGE_DIR})
    endif()

    if(NOT EXISTS "${PROJECT_3RD_PARTY_PACKAGE_DIR}/libressl-${3RD_PARTY_LIBRESSL_DEFAULT_VERSION}")
      echowithcolor(COLOR RED "-- Dependency: Build libressl failed")
      message(FATAL_ERROR "Dependency: LibreSSL is required")
    endif()

    unset(3RD_PARTY_LIBRESSL_BUILD_FLAGS)
    list(
      APPEND
      3RD_PARTY_LIBRESSL_BUILD_FLAGS
      ${CMAKE_COMMAND}
      "${PROJECT_3RD_PARTY_PACKAGE_DIR}/libressl-${3RD_PARTY_LIBRESSL_DEFAULT_VERSION}"
      "-DCMAKE_INSTALL_PREFIX=${PROJECT_3RD_PARTY_INSTALL_DIR}"
      "-DLIBRESSL_TESTS=OFF"
      "-DBUILD_SHARED_LIBS=NO")
    project_build_tools_append_cmake_options_for_lib(3RD_PARTY_LIBRESSL_BUILD_FLAGS)

    set(3RD_PARTY_LIBRESSL_BUILD_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/deps/libressl-${3RD_PARTY_LIBRESSL_DEFAULT_VERSION}/build_jobs_dir_${PROJECT_PREBUILT_PLATFORM_NAME}"
    )
    if(NOT EXISTS ${3RD_PARTY_LIBRESSL_BUILD_DIR})
      file(MAKE_DIRECTORY ${3RD_PARTY_LIBRESSL_BUILD_DIR})
    endif()

    if(CMAKE_HOST_UNIX
       OR MSYS
       OR CYGWIN)
      file(WRITE "${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-cmake.sh"
           "#!/bin/bash${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
      file(WRITE "${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-build-release.sh"
           "#!/bin/bash${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
      file(
        APPEND "${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-cmake.sh"
        "export PATH=\"${3RD_PARTY_LIBRESSL_BUILD_DIR}:\$PATH\"${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
      )
      file(
        APPEND "${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-build-release.sh"
        "export PATH=\"${3RD_PARTY_LIBRESSL_BUILD_DIR}:\$PATH\"${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
      )
      project_make_executable("${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-cmake.sh")
      project_make_executable("${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-build-release.sh")
      project_expand_list_for_command_line_to_file("${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-cmake.sh"
                                                   ${3RD_PARTY_LIBRESSL_BUILD_FLAGS})
      project_expand_list_for_command_line_to_file(
        "${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-build-release.sh"
        ${CMAKE_COMMAND}
        --build
        .
        --target
        install
        --config
        Release
        "-j")

      # build & install
      message(STATUS "@${3RD_PARTY_LIBRESSL_BUILD_DIR} Run: ./run-cmake.sh")
      message(STATUS "@${3RD_PARTY_LIBRESSL_BUILD_DIR} Run: ./run-build-release.sh")
      execute_process(COMMAND "${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-cmake.sh"
                      WORKING_DIRECTORY ${3RD_PARTY_LIBRESSL_BUILD_DIR})

      execute_process(COMMAND "${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-build-release.sh"
                      WORKING_DIRECTORY ${3RD_PARTY_LIBRESSL_BUILD_DIR})

    else()
      file(WRITE "${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-cmake.bat"
           "@echo off${PROJECT_THIRD_PARTY_BUILDTOOLS_EOL}")
      file(WRITE "${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-build-debug.bat"
           "@echo off${PROJECT_THIRD_PARTY_BUILDTOOLS_EOL}")
      file(WRITE "${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-build-release.bat"
           "@echo off${PROJECT_THIRD_PARTY_BUILDTOOLS_EOL}")
      file(APPEND "${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-cmake.bat"
           "set PATH=${ATFRAME_THIRD_PARTY_ENV_PATH};%PATH%${PROJECT_THIRD_PARTY_BUILDTOOLS_EOL}")
      file(APPEND "${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-build-debug.bat"
           "set PATH=${ATFRAME_THIRD_PARTY_ENV_PATH};%PATH%${PROJECT_THIRD_PARTY_BUILDTOOLS_EOL}")
      file(APPEND "${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-build-release.bat"
           "set PATH=${ATFRAME_THIRD_PARTY_ENV_PATH};%PATH%${PROJECT_THIRD_PARTY_BUILDTOOLS_EOL}")
      project_make_executable("${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-cmake.bat")
      project_make_executable("${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-build-debug.bat")
      project_make_executable("${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-build-release.bat")
      project_expand_list_for_command_line_to_file("${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-cmake.bat"
                                                   ${3RD_PARTY_LIBRESSL_BUILD_FLAGS})
      project_expand_list_for_command_line_to_file(
        "${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-build-release.bat"
        ${CMAKE_COMMAND}
        --build
        .
        --target
        install
        --config
        Release
        "-j")

      # build & install
      message(
        STATUS "@${3RD_PARTY_LIBRESSL_BUILD_DIR} Run: ${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-cmake.bat"
      )
      message(
        STATUS
          "@${3RD_PARTY_LIBRESSL_BUILD_DIR} Run: ${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-build-release.bat"
      )
      execute_process(COMMAND "${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-cmake.bat"
                      WORKING_DIRECTORY ${3RD_PARTY_LIBRESSL_BUILD_DIR})
      # install debug target
      if(MSVC)
        message(
          STATUS
            "@${3RD_PARTY_LIBRESSL_BUILD_DIR} Run: ${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-build-debug.bat"
        )
        execute_process(COMMAND "${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-build-debug.bat"
                        WORKING_DIRECTORY ${3RD_PARTY_LIBRESSL_BUILD_DIR})
      endif()

      execute_process(COMMAND "${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-build-release.bat"
                      WORKING_DIRECTORY ${3RD_PARTY_LIBRESSL_BUILD_DIR})
    endif()

    unset(LIBRESSL_FOUND CACHE)
    unset(LIBRESSL_INCLUDE_DIR CACHE)
    unset(LIBRESSL_CRYPTO_LIBRARY CACHE)
    unset(LIBRESSL_SSL_LIBRARY CACHE)
    unset(LIBRESSL_TLS_LIBRARY CACHE)
    unset(LIBRESSL_LIBRARIES CACHE)
    unset(LIBRESSL_LIBRARIES)
    unset(LIBRESSL_VERSION CACHE)

    if(WIN32)
      # Patch and remove postfix file
      file(GLOB LIBRESSL_PATCH_WIN32_FILE_NAMES "${PROJECT_3RD_PARTY_INSTALL_DIR}/lib/crypto-*.lib")
      if(LIBRESSL_PATCH_WIN32_FILE_NAMES)
        list(GET LIBRESSL_PATCH_WIN32_FILE_NAMES 0 LIBRESSL_PATCH_WIN32_FILE_NAME)
        file(RENAME ${LIBRESSL_PATCH_WIN32_FILE_NAME}
             "${PROJECT_3RD_PARTY_INSTALL_DIR}/lib/crypto.lib")
      endif()
      unset(LIBRESSL_PATCH_WIN32_FILE_NAMES)
      file(GLOB LIBRESSL_PATCH_WIN32_FILE_NAMES "${PROJECT_3RD_PARTY_INSTALL_DIR}/bin/crypto-*.dll")
      if(LIBRESSL_PATCH_WIN32_FILE_NAMES)
        list(GET LIBRESSL_PATCH_WIN32_FILE_NAMES 0 LIBRESSL_PATCH_WIN32_FILE_NAME)
        file(RENAME ${LIBRESSL_PATCH_WIN32_FILE_NAME}
             "${PROJECT_3RD_PARTY_INSTALL_DIR}/bin/crypto.dll")
      endif()
      unset(LIBRESSL_PATCH_WIN32_FILE_NAMES)

      file(GLOB LIBRESSL_PATCH_WIN32_FILE_NAMES "${PROJECT_3RD_PARTY_INSTALL_DIR}/lib/ssl-*.lib")
      if(LIBRESSL_PATCH_WIN32_FILE_NAMES)
        list(GET LIBRESSL_PATCH_WIN32_FILE_NAMES 0 LIBRESSL_PATCH_WIN32_FILE_NAME)
        file(RENAME ${LIBRESSL_PATCH_WIN32_FILE_NAME}
             "${PROJECT_3RD_PARTY_INSTALL_DIR}/lib/ssl.lib")
      endif()
      unset(LIBRESSL_PATCH_WIN32_FILE_NAMES)
      file(GLOB LIBRESSL_PATCH_WIN32_FILE_NAMES "${PROJECT_3RD_PARTY_INSTALL_DIR}/bin/ssl-*.dll")
      if(LIBRESSL_PATCH_WIN32_FILE_NAMES)
        list(GET LIBRESSL_PATCH_WIN32_FILE_NAMES 0 LIBRESSL_PATCH_WIN32_FILE_NAME)
        file(RENAME ${LIBRESSL_PATCH_WIN32_FILE_NAME}
             "${PROJECT_3RD_PARTY_INSTALL_DIR}/bin/ssl.dll")
      endif()
      unset(LIBRESSL_PATCH_WIN32_FILE_NAMES)

      file(GLOB LIBRESSL_PATCH_WIN32_FILE_NAMES "${PROJECT_3RD_PARTY_INSTALL_DIR}/lib/tls-*.lib")
      if(LIBRESSL_PATCH_WIN32_FILE_NAMES)
        list(GET LIBRESSL_PATCH_WIN32_FILE_NAMES 0 LIBRESSL_PATCH_WIN32_FILE_NAME)
        file(RENAME ${LIBRESSL_PATCH_WIN32_FILE_NAME}
             "${PROJECT_3RD_PARTY_INSTALL_DIR}/lib/tls.lib")
      endif()
      unset(LIBRESSL_PATCH_WIN32_FILE_NAMES)
      file(GLOB LIBRESSL_PATCH_WIN32_FILE_NAMES "${PROJECT_3RD_PARTY_INSTALL_DIR}/bin/tls-*.dll")
      if(LIBRESSL_PATCH_WIN32_FILE_NAMES)
        list(GET LIBRESSL_PATCH_WIN32_FILE_NAMES 0 LIBRESSL_PATCH_WIN32_FILE_NAME)
        file(RENAME ${LIBRESSL_PATCH_WIN32_FILE_NAME}
             "${PROJECT_3RD_PARTY_INSTALL_DIR}/bin/tls.dll")
      endif()
      unset(LIBRESSL_PATCH_WIN32_FILE_NAMES)

      unset(LIBRESSL_PATCH_WIN32_FILE_NAME)
    endif()
    find_package(LibreSSL)
    project_3rd_party_libressl_import()
  endif()

  if(LIBRESSL_FOUND)
    echowithcolor(COLOR GREEN "-- Dependency: Libressl found.(${LIBRESSL_VERSION})")
  else()
    echowithcolor(COLOR RED "-- Dependency: Libressl is required")
  endif()
else()
  project_3rd_party_libressl_import()
endif()
