if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.10")
  include_guard(GLOBAL)
endif()

# =========== 3rdparty openssl ==================

macro(PROJECT_3RD_PARTY_OPENSSL_IMPORT)
  if(OPENSSL_FOUND)
    echowithcolor(COLOR GREEN "-- Dependency: openssl found.(${OPENSSL_VERSION})")
    if(TARGET OpenSSL::SSL OR TARGET OpenSSL::Crypto)
      if(TARGET OpenSSL::Crypto)
        list(APPEND 3RD_PARTY_CRYPT_LINK_NAME OpenSSL::Crypto)
        list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES OpenSSL::Crypto)

        if(TARGET Threads::Threads)
          project_build_tools_patch_imported_link_interface_libraries(
            OpenSSL::Crypto ADD_LIBRARIES Threads::Threads ${CMAKE_DL_LIBS})
        elseif(CMAKE_DL_LIBS)
          project_build_tools_patch_imported_link_interface_libraries(OpenSSL::Crypto ADD_LIBRARIES
                                                                      ${CMAKE_DL_LIBS})
        endif()
      endif()
      if(TARGET OpenSSL::SSL)
        list(APPEND 3RD_PARTY_CRYPT_LINK_NAME OpenSSL::SSL)
        list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES OpenSSL::SSL)

        if(TARGET Threads::Threads)
          project_build_tools_patch_imported_link_interface_libraries(
            OpenSSL::SSL ADD_LIBRARIES Threads::Threads ${CMAKE_DL_LIBS})
        elseif(CMAKE_DL_LIBS)
          project_build_tools_patch_imported_link_interface_libraries(OpenSSL::SSL ADD_LIBRARIES
                                                                      ${CMAKE_DL_LIBS})
        endif()
      endif()
    else()
      if(OPENSSL_INCLUDE_DIR)
        list(APPEND 3RD_PARTY_PUBLIC_INCLUDE_DIRS ${OPENSSL_INCLUDE_DIR})
      endif()

      if(NOT OPENSSL_LIBRARIES)
        set(OPENSSL_LIBRARIES
            ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY}
            CACHE INTERNAL "Fix cmake module path for openssl" FORCE)
      endif()
      if(OPENSSL_LIBRARIES)
        list(APPEND 3RD_PARTY_CRYPT_LINK_NAME ${OPENSSL_LIBRARIES})
        list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES ${OPENSSL_LIBRARIES})
      endif()
    endif()

    find_program(
      OPENSSL_EXECUTABLE
      NAMES openssl openssl.exe
      PATHS "${OPENSSL_INCLUDE_DIR}/../bin" "${OPENSSL_INCLUDE_DIR}/../" ${OPENSSL_INCLUDE_DIR}
      NO_SYSTEM_ENVIRONMENT_PATH NO_CMAKE_SYSTEM_PATH)
    if(OPENSSL_EXECUTABLE AND EXISTS ${OPENSSL_EXECUTABLE})
      file(
        COPY ${OPENSSL_EXECUTABLE}
        DESTINATION "${PROJECT_INSTALL_TOOLS_DIR}/bin"
        USE_SOURCE_PERMISSIONS)
    endif()

    if(NOT CRYPTO_USE_OPENSSL)
      set(CRYPTO_USE_OPENSSL
          TRUE
          CACHE BOOL "Cache ssl selector and directly use openssl next time")
    endif()
  endif()
endmacro()

if(NOT 3RD_PARTY_CRYPT_LINK_NAME)
  set(3RD_PARTY_OPENSSL_DEFAULT_VERSION "1.1.1k")
  set(3RD_PARTY_OPENSSL_GITHUB_TAG "OpenSSL_1_1_1k")
  # "no-hw" and "no-engine" is recommanded by openssl only for mobile devices @see
  # https://wiki.openssl.org/index.php/Compilation_and_Installation
  set(3RD_PARTY_OPENSSL_BUILD_OPTIONS
      "--prefix=${PROJECT_3RD_PARTY_INSTALL_DIR}"
      "--openssldir=${PROJECT_3RD_PARTY_INSTALL_DIR}/ssl"
      "--release"
      # "--api=1.1.1" # libwebsockets and atframe_utils has warnings of using deprecated APIs, maybe
      # it can be remove later "no-deprecated" # libcurl and gRPC requires openssl's API of 1.1.0
      # and 1.0.2, so we can not disable deprecated APIS here
      "no-dso"
      "no-tests"
      "no-external-tests"
      "no-shared"
      "no-idea"
      "no-md4"
      "no-mdc2"
      "no-rc2"
      "no-ssl2"
      "no-ssl3"
      "no-weak-ssl-ciphers"
      "enable-static-engine")
  if(NOT MSVC)
    list(APPEND 3RD_PARTY_OPENSSL_BUILD_OPTIONS "enable-ec_nistp_64_gcc_128")
  endif()

  if(VCPKG_TOOLCHAIN)
    find_package(OpenSSL QUIET)
    project_3rd_party_openssl_import()
  endif()

  if(NOT OPENSSL_FOUND)
    set(OPENSSL_ROOT_DIR ${PROJECT_3RD_PARTY_INSTALL_DIR})
    set(OPENSSL_USE_STATIC_LIBS TRUE)

    find_library(
      3RD_PARTY_OPENSSL_FIND_LIB_CRYPTO
      NAMES crypto libcrypto
      PATHS "${PROJECT_3RD_PARTY_INSTALL_DIR}/lib" "${PROJECT_3RD_PARTY_INSTALL_DIR}/lib64"
      NO_DEFAULT_PATH)
    find_library(
      3RD_PARTY_OPENSSL_FIND_LIB_SSL
      NAMES ssl libssl
      PATHS "${PROJECT_3RD_PARTY_INSTALL_DIR}/lib" "${PROJECT_3RD_PARTY_INSTALL_DIR}/lib64"
      NO_DEFAULT_PATH)

    if(3RD_PARTY_OPENSSL_FIND_LIB_CRYPTO AND 3RD_PARTY_OPENSSL_FIND_LIB_SSL)
      find_package(OpenSSL QUIET)
    else()
      message(STATUS "3RD_PARTY_OPENSSL_FIND_LIB_CRYPTO -- ${3RD_PARTY_OPENSSL_FIND_LIB_CRYPTO}")
      message(STATUS "3RD_PARTY_OPENSSL_FIND_LIB_SSL    -- ${3RD_PARTY_OPENSSL_FIND_LIB_SSL}")
    endif()

    unset(3RD_PARTY_OPENSSL_FIND_LIB_CRYPTO CACHE)
    unset(3RD_PARTY_OPENSSL_FIND_LIB_SSL CACHE)
    project_3rd_party_openssl_import()
  endif()

  if(NOT OPENSSL_FOUND)
    echowithcolor(COLOR GREEN "-- Try to configure and use openssl")
    unset(OPENSSL_FOUND CACHE)
    unset(OPENSSL_EXECUTABLE CACHE)
    unset(OPENSSL_INCLUDE_DIR CACHE)
    unset(OPENSSL_CRYPTO_LIBRARY CACHE)
    unset(OPENSSL_CRYPTO_LIBRARIES CACHE)
    unset(OPENSSL_SSL_LIBRARY CACHE)
    unset(OPENSSL_SSL_LIBRARIES CACHE)
    unset(OPENSSL_LIBRARIES CACHE)
    unset(OPENSSL_VERSION CACHE)

    project_git_clone_3rd_party(
      URL
      "https://github.com/openssl/openssl.git"
      REPO_DIRECTORY
      "${PROJECT_3RD_PARTY_PACKAGE_DIR}/openssl-${3RD_PARTY_OPENSSL_DEFAULT_VERSION}"
      DEPTH
      200
      TAG
      ${3RD_PARTY_OPENSSL_GITHUB_TAG}
      WORKING_DIRECTORY
      ${PROJECT_3RD_PARTY_PACKAGE_DIR})

    if(NOT EXISTS "${PROJECT_3RD_PARTY_PACKAGE_DIR}/openssl-${3RD_PARTY_OPENSSL_DEFAULT_VERSION}")
      echowithcolor(COLOR RED "-- Dependency: Build openssl failed")
      message(FATAL_ERROR "Dependency: openssl is required")
    endif()

    set(3RD_PARTY_OPENSSL_BUILD_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/deps/openssl-${3RD_PARTY_OPENSSL_DEFAULT_VERSION}/build_jobs_${PROJECT_PREBUILT_PLATFORM_NAME}"
    )
    if(NOT EXISTS ${3RD_PARTY_OPENSSL_BUILD_DIR})
      file(MAKE_DIRECTORY ${3RD_PARTY_OPENSSL_BUILD_DIR})
    endif()

    if(MSVC)
      set(3RD_PARTY_OPENSSL_BUILD_MULTI_CORE "/MP")
    else()
      include(ProcessorCount)
      ProcessorCount(CPU_CORE_NUM)
      set(3RD_PARTY_OPENSSL_BUILD_MULTI_CORE "-j${CPU_CORE_NUM}")
      unset(CPU_CORE_NUM)
    endif()
    # 服务器目前不需要适配ARM和android
    if(NOT MSVC)
      file(WRITE "${3RD_PARTY_OPENSSL_BUILD_DIR}/run-config.sh"
           "#!/bin/bash${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
      file(WRITE "${3RD_PARTY_OPENSSL_BUILD_DIR}/run-build-release.sh"
           "#!/bin/bash${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
      file(
        APPEND "${3RD_PARTY_OPENSSL_BUILD_DIR}/run-config.sh"
        "export PATH=\"${3RD_PARTY_OPENSSL_BUILD_DIR}:\$PATH\"${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
      )
      file(
        APPEND "${3RD_PARTY_OPENSSL_BUILD_DIR}/run-build-release.sh"
        "export PATH=\"${3RD_PARTY_OPENSSL_BUILD_DIR}:\$PATH\"${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
      )
      project_make_executable("${3RD_PARTY_OPENSSL_BUILD_DIR}/run-config.sh")
      project_make_executable("${3RD_PARTY_OPENSSL_BUILD_DIR}/run-build-release.sh")

      list(APPEND 3RD_PARTY_OPENSSL_BUILD_OPTIONS "CC=${CMAKE_C_COMPILER}")
      list(APPEND 3RD_PARTY_OPENSSL_BUILD_OPTIONS "CXX=${CMAKE_CXX_COMPILER}")
      if(CMAKE_AR)
        list(APPEND 3RD_PARTY_OPENSSL_BUILD_OPTIONS "AR=${CMAKE_AR}")
      endif()

      if(CMAKE_C_FLAGS OR CMAKE_C_FLAGS_RELEASE)
        list(APPEND 3RD_PARTY_OPENSSL_BUILD_OPTIONS
             "CFLAGS=${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_RELEASE}")
      endif()

      if(CMAKE_CXX_FLAGS OR CMAKE_CXX_FLAGS_RELEASE)
        list(APPEND 3RD_PARTY_OPENSSL_BUILD_OPTIONS
             "CXXFLAGS=${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_RELEASE}")
      endif()

      if(CMAKE_ASM_FLAGS OR CMAKE_ASM_FLAGS_RELEASE)
        list(APPEND 3RD_PARTY_OPENSSL_BUILD_OPTIONS
             "ASFLAGS=${CMAKE_ASM_FLAGS} ${CMAKE_ASM_FLAGS_RELEASE}")
      endif()

      if(CMAKE_EXE_LINKER_FLAGS)
        list(APPEND 3RD_PARTY_OPENSSL_BUILD_OPTIONS "LDFLAGS=${CMAKE_EXE_LINKER_FLAGS}")
      endif()

      if(CMAKE_RANLIB)
        list(APPEND 3RD_PARTY_OPENSSL_BUILD_OPTIONS "RANLIB=${CMAKE_RANLIB}")
      endif()

      if(CMAKE_STATIC_LINKER_FLAGS)
        list(APPEND 3RD_PARTY_OPENSSL_BUILD_OPTIONS "LDFLAGS=${CMAKE_STATIC_LINKER_FLAGS}")
      endif()

      list(APPEND 3RD_PARTY_OPENSSL_BUILD_OPTIONS)
      project_expand_list_for_command_line_to_file("${3RD_PARTY_OPENSSL_BUILD_DIR}/run-config.sh"
                                                   "../config" ${3RD_PARTY_OPENSSL_BUILD_OPTIONS})
      project_expand_list_for_command_line_to_file(
        "${3RD_PARTY_OPENSSL_BUILD_DIR}/run-build-release.sh" "make"
        ${3RD_PARTY_OPENSSL_BUILD_MULTI_CORE})
      project_expand_list_for_command_line_to_file(
        "${3RD_PARTY_OPENSSL_BUILD_DIR}/run-build-release.sh" "make" "install_sw" "install_ssldirs")

      # build & install
      message(STATUS "@${3RD_PARTY_OPENSSL_BUILD_DIR} Run: ./run-config.sh")
      message(STATUS "@${3RD_PARTY_OPENSSL_BUILD_DIR} Run: ./run-build-release.sh")
      execute_process(COMMAND "${3RD_PARTY_OPENSSL_BUILD_DIR}/run-config.sh"
                      WORKING_DIRECTORY ${3RD_PARTY_OPENSSL_BUILD_DIR})

      execute_process(COMMAND "${3RD_PARTY_OPENSSL_BUILD_DIR}/run-build-release.sh"
                      WORKING_DIRECTORY ${3RD_PARTY_OPENSSL_BUILD_DIR})

    else()
      file(WRITE "${3RD_PARTY_OPENSSL_BUILD_DIR}/run-config.bat"
           "@echo off${PROJECT_THIRD_PARTY_BUILDTOOLS_EOL}")
      file(WRITE "${3RD_PARTY_OPENSSL_BUILD_DIR}/run-build-release.bat"
           "@echo off${PROJECT_THIRD_PARTY_BUILDTOOLS_EOL}")
      file(APPEND "${3RD_PARTY_OPENSSL_BUILD_DIR}/run-config.bat"
           "set PATH=${3RD_PARTY_OPENSSL_BUILD_DIR};%PATH%${PROJECT_THIRD_PARTY_BUILDTOOLS_EOL}")
      file(APPEND "${3RD_PARTY_OPENSSL_BUILD_DIR}/run-build-release.bat"
           "set PATH=${3RD_PARTY_OPENSSL_BUILD_DIR};%PATH%${PROJECT_THIRD_PARTY_BUILDTOOLS_EOL}")
      project_make_executable("${3RD_PARTY_OPENSSL_BUILD_DIR}/run-config.bat")
      project_make_executable("${3RD_PARTY_OPENSSL_BUILD_DIR}/run-build-release.bat")

      list(APPEND 3RD_PARTY_OPENSSL_BUILD_OPTIONS "no-makedepend" "-utf-8"
           "no-capieng" # "enable-capieng"  # 有些第三方库没有加入对这个模块检测的支持，比如 libwebsockets
      )

      if(CMAKE_SIZEOF_VOID_P MATCHES 8)
        list(APPEND 3RD_PARTY_OPENSSL_BUILD_OPTIONS "VC-WIN64A-masm")
      else()
        list(APPEND 3RD_PARTY_OPENSSL_BUILD_OPTIONS "VC-WIN32" "no-asm")
      endif()

      project_expand_list_for_command_line_to_file(
        "${3RD_PARTY_OPENSSL_BUILD_DIR}/run-config.bat" perl "../Configure"
        ${3RD_PARTY_OPENSSL_BUILD_OPTIONS})
      file(APPEND "${3RD_PARTY_OPENSSL_BUILD_DIR}/run-build-release.bat"
           "set CL=${3RD_PARTY_OPENSSL_BUILD_MULTI_CORE}${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
      project_expand_list_for_command_line_to_file(
        "${3RD_PARTY_OPENSSL_BUILD_DIR}/run-build-release.bat" "nmake" "build_all_generated")
      project_expand_list_for_command_line_to_file(
        "${3RD_PARTY_OPENSSL_BUILD_DIR}/run-build-release.bat" "nmake" "PERL=no-perl")
      project_expand_list_for_command_line_to_file(
        "${3RD_PARTY_OPENSSL_BUILD_DIR}/run-build-release.bat" "nmake" "install_sw"
        "install_ssldirs" # "DESTDIR=${PROJECT_3RD_PARTY_INSTALL_DIR}"
      )

      # build & install
      message(
        STATUS "@${3RD_PARTY_OPENSSL_BUILD_DIR} Run: ${3RD_PARTY_OPENSSL_BUILD_DIR}/run-config.bat")
      message(
        STATUS
          "@${3RD_PARTY_OPENSSL_BUILD_DIR} Run: ${3RD_PARTY_OPENSSL_BUILD_DIR}/run-build-release.bat"
      )
      execute_process(COMMAND "${3RD_PARTY_OPENSSL_BUILD_DIR}/run-config.bat"
                      WORKING_DIRECTORY ${3RD_PARTY_OPENSSL_BUILD_DIR})

      execute_process(COMMAND "${3RD_PARTY_OPENSSL_BUILD_DIR}/run-build-release.bat"
                      WORKING_DIRECTORY ${3RD_PARTY_OPENSSL_BUILD_DIR})
    endif()
    unset(3RD_PARTY_OPENSSL_BUILD_MULTI_CORE)

    find_package(OpenSSL)
    project_3rd_party_openssl_import()
  endif()

  if(OPENSSL_FOUND AND WIN32)
    find_library(3RD_PARTY_OPENSSL_FIND_CRYPT32 Crypt32)
    if(3RD_PARTY_OPENSSL_FIND_CRYPT32)
      list(APPEND 3RD_PARTY_CRYPT_LINK_NAME Crypt32)
      list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES Crypt32)
    endif()
  endif()
else()
  project_3rd_party_openssl_import()
endif()
