# =========== 3rdparty libressl ==================
if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.10")
    include_guard(GLOBAL)
endif()

if (NOT 3RD_PARTY_CRYPT_LINK_NAME)
    if (NOT 3RD_PARTY_LIBRESSL_BASE_DIR)
        set (3RD_PARTY_LIBRESSL_BASE_DIR ${CMAKE_CURRENT_LIST_DIR})
    endif()

    set (3RD_PARTY_LIBRESSL_PKG_DIR "${3RD_PARTY_LIBRESSL_BASE_DIR}/pkg")

    set (3RD_PARTY_LIBRESSL_DEFAULT_VERSION "3.2.0")
    set (3RD_PARTY_LIBRESSL_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/prebuilt/${PROJECT_PREBUILT_PLATFORM_NAME}")

    macro(PROJECT_3RD_PARTY_LIBRESSL_IMPORT)
        if(LIBRESSL_FOUND)
            set (OPENSSL_FOUND ${LIBRESSL_FOUND} CACHE BOOL "using libressl for erplacement of openssl" FORCE)
            set (OPENSSL_INCLUDE_DIR ${LIBRESSL_INCLUDE_DIR} CACHE PATH "libressl include dir" FORCE)
            set (OPENSSL_CRYPTO_LIBRARY ${LIBRESSL_CRYPTO_LIBRARY} CACHE STRING "libressl crypto libs" FORCE)
            set (OPENSSL_CRYPTO_LIBRARIES ${LIBRESSL_CRYPTO_LIBRARY} CACHE STRING "libressl crypto libs" FORCE)
            set (OPENSSL_SSL_LIBRARY ${LIBRESSL_SSL_LIBRARY} CACHE STRING "libressl ssl libs" FORCE)
            set (OPENSSL_SSL_LIBRARIES ${LIBRESSL_SSL_LIBRARY} CACHE STRING "libressl ssl libs" FORCE)
            set (OPENSSL_LIBRARIES ${LIBRESSL_LIBRARIES} CACHE STRING "libressl all libs" FORCE)
            set (OPENSSL_VERSION "1.1.0" CACHE STRING "openssl version of libressl" FORCE)

            set (OpenSSL::Crypto LibreSSL::Crypto)
            set (OpenSSL::SSL LibreSSL::SSL)

            if (TARGET LibreSSL::TLS)
                list(APPEND 3RD_PARTY_CRYPT_LINK_NAME LibreSSL::TLS)
                list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES LibreSSL::TLS)
            else()
                if (LIBRESSL_INCLUDE_DIR)
                    list(APPEND 3RD_PARTY_PUBLIC_INCLUDE_DIRS ${LIBRESSL_INCLUDE_DIR})
                endif ()

                if(LIBRESSL_LIBRARIES)
                    list(APPEND 3RD_PARTY_CRYPT_LINK_NAME ${LIBRESSL_LIBRARIES})
                    list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES ${LIBRESSL_LIBRARIES})
                endif()
            endif ()
        endif ()
    endmacro()

    if (VCPKG_TOOLCHAIN)
        find_package(LibreSSL)
        PROJECT_3RD_PARTY_LIBRESSL_IMPORT()
    endif ()

    if (NOT LIBRESSL_FOUND)
        if(NOT EXISTS ${3RD_PARTY_LIBRESSL_PKG_DIR})
            file(MAKE_DIRECTORY ${3RD_PARTY_LIBRESSL_PKG_DIR})
        endif()

        set (OPENSSL_ROOT_DIR ${3RD_PARTY_LIBRESSL_ROOT_DIR})
        set (LIBRESSL_ROOT_DIR ${3RD_PARTY_LIBRESSL_ROOT_DIR})
        set(LibreSSL_ROOT ${3RD_PARTY_LIBRESSL_ROOT_DIR})

        set(3RD_PARTY_LIBRESSL_BACKUP_FIND_ROOT ${CMAKE_FIND_ROOT_PATH})
        list(APPEND CMAKE_FIND_ROOT_PATH ${3RD_PARTY_LIBRESSL_ROOT_DIR})

        find_library(3RD_PARTY_LIBRESSL_FIND_LIB_CRYPTO NAMES crypto libcrypto PATHS "${3RD_PARTY_LIBRESSL_ROOT_DIR}/lib" "${3RD_PARTY_LIBRESSL_ROOT_DIR}/lib64" NO_DEFAULT_PATH)
        find_library(3RD_PARTY_LIBRESSL_FIND_LIB_SSL NAMES ssl libssl PATHS "${3RD_PARTY_LIBRESSL_ROOT_DIR}/lib" "${3RD_PARTY_LIBRESSL_ROOT_DIR}/lib64" NO_DEFAULT_PATH)
        find_library(3RD_PARTY_LIBRESSL_FIND_LIB_TLS NAMES tls libtls PATHS "${3RD_PARTY_LIBRESSL_ROOT_DIR}/lib" "${3RD_PARTY_LIBRESSL_ROOT_DIR}/lib64" NO_DEFAULT_PATH)
        
        if (3RD_PARTY_LIBRESSL_FIND_LIB_CRYPTO AND 3RD_PARTY_LIBRESSL_FIND_LIB_SSL AND 3RD_PARTY_LIBRESSL_FIND_LIB_TLS)
            find_package(LibreSSL)
        endif ()
        unset(3RD_PARTY_LIBRESSL_FIND_LIB_CRYPTO)
        unset(3RD_PARTY_LIBRESSL_FIND_LIB_SSL)
        unset(3RD_PARTY_LIBRESSL_FIND_LIB_TLS)

        PROJECT_3RD_PARTY_LIBRESSL_IMPORT()
    endif()

    if (NOT LIBRESSL_FOUND)
        EchoWithColor(COLOR GREEN "-- Try to configure and use libressl")
        unset(OPENSSL_FOUND CACHE)
        unset(OPENSSL_INCLUDE_DIR CACHE)
        unset(OPENSSL_CRYPTO_LIBRARY CACHE)
        unset(OPENSSL_CRYPTO_LIBRARIES CACHE)
        unset(OPENSSL_SSL_LIBRARY CACHE)
        unset(OPENSSL_SSL_LIBRARIES CACHE)
        unset(OPENSSL_LIBRARIES CACHE)
        unset(OPENSSL_VERSION CACHE)
        # patch for old build script
        if (EXISTS "${3RD_PARTY_LIBRESSL_PKG_DIR}/libressl-${3RD_PARTY_LIBRESSL_DEFAULT_VERSION}/CMakeCache.txt")
            execute_process(COMMAND ${CMAKE_COMMAND} -E remove_directory "${3RD_PARTY_LIBRESSL_PKG_DIR}/libressl-${3RD_PARTY_LIBRESSL_DEFAULT_VERSION}"
                WORKING_DIRECTORY ${3RD_PARTY_LIBRESSL_PKG_DIR}
            )
        endif ()

        if (NOT EXISTS "${3RD_PARTY_LIBRESSL_PKG_DIR}/libressl-${3RD_PARTY_LIBRESSL_DEFAULT_VERSION}")
            if (NOT EXISTS "${3RD_PARTY_LIBRESSL_PKG_DIR}/libressl-${3RD_PARTY_LIBRESSL_DEFAULT_VERSION}.tar.gz")
                FindConfigurePackageDownloadFile("https://ftp.openbsd.org/pub/OpenBSD/LibreSSL/libressl-${3RD_PARTY_LIBRESSL_DEFAULT_VERSION}.tar.gz" "${3RD_PARTY_LIBRESSL_PKG_DIR}/libressl-${3RD_PARTY_LIBRESSL_DEFAULT_VERSION}.tar.gz")
            endif ()

            execute_process(COMMAND ${CMAKE_COMMAND} -E tar xvf "${3RD_PARTY_LIBRESSL_PKG_DIR}/libressl-${3RD_PARTY_LIBRESSL_DEFAULT_VERSION}.tar.gz"
                WORKING_DIRECTORY ${3RD_PARTY_LIBRESSL_PKG_DIR}
            )
        endif ()

        if (NOT EXISTS "${3RD_PARTY_LIBRESSL_PKG_DIR}/libressl-${3RD_PARTY_LIBRESSL_DEFAULT_VERSION}")
            EchoWithColor(COLOR RED "-- Dependency: Build libressl failed")
            message(FATAL_ERROR "Dependency: LibreSSL is required")
        endif ()

        unset(3RD_PARTY_LIBRESSL_BUILD_FLAGS)
        list(APPEND 3RD_PARTY_LIBRESSL_BUILD_FLAGS 
            ${CMAKE_COMMAND} "${3RD_PARTY_LIBRESSL_PKG_DIR}/libressl-${3RD_PARTY_LIBRESSL_DEFAULT_VERSION}"
            "-DCMAKE_INSTALL_PREFIX=${3RD_PARTY_LIBRESSL_ROOT_DIR}" "-DLIBRESSL_TESTS=OFF" "-DBUILD_SHARED_LIBS=NO"
        )
        project_build_tools_append_cmake_options_for_lib(3RD_PARTY_LIBRESSL_BUILD_FLAGS)

        set (3RD_PARTY_LIBRESSL_BUILD_DIR "${3RD_PARTY_LIBRESSL_PKG_DIR}/libressl-${3RD_PARTY_LIBRESSL_DEFAULT_VERSION}/build_jobs_dir_${PROJECT_PREBUILT_PLATFORM_NAME}")
        if (NOT EXISTS ${3RD_PARTY_LIBRESSL_BUILD_DIR})
            file(MAKE_DIRECTORY ${3RD_PARTY_LIBRESSL_BUILD_DIR})
        endif()

        if (CMAKE_HOST_UNIX OR MSYS OR CYGWIN)
            file(WRITE "${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-cmake.sh" "#!/bin/bash${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
            file(WRITE "${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-build-release.sh" "#!/bin/bash${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
            file(APPEND "${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-cmake.sh" "export PATH=\"${3RD_PARTY_LIBRESSL_BUILD_DIR}:\$PATH\"${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
            file(APPEND "${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-build-release.sh" "export PATH=\"${3RD_PARTY_LIBRESSL_BUILD_DIR}:\$PATH\"${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
            project_make_executable("${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-cmake.sh")
            project_make_executable("${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-build-release.sh")
            project_expand_list_for_command_line_to_file("${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-cmake.sh"
                ${3RD_PARTY_LIBRESSL_BUILD_FLAGS}
            )
            project_expand_list_for_command_line_to_file("${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-build-release.sh"
                ${CMAKE_COMMAND} --build . --target install --config Release "-j"
            )

            # build & install
            message(STATUS "@${3RD_PARTY_LIBRESSL_BUILD_DIR} Run: ./run-cmake.sh")
            message(STATUS "@${3RD_PARTY_LIBRESSL_BUILD_DIR} Run: ./run-build-release.sh")
            execute_process(
                COMMAND "${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-cmake.sh"
                WORKING_DIRECTORY ${3RD_PARTY_LIBRESSL_BUILD_DIR}
            )

            execute_process(
                COMMAND "${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-build-release.sh"
                WORKING_DIRECTORY ${3RD_PARTY_LIBRESSL_BUILD_DIR}
            )

        else ()
            file(WRITE "${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-cmake.bat" "@echo off${PROJECT_THIRD_PARTY_BUILDTOOLS_EOL}")
            file(WRITE "${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-build-debug.bat" "@echo off${PROJECT_THIRD_PARTY_BUILDTOOLS_EOL}")
            file(WRITE "${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-build-release.bat" "@echo off${PROJECT_THIRD_PARTY_BUILDTOOLS_EOL}")
            file(APPEND "${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-cmake.bat" "set PATH=${ATFRAME_THIRD_PARTY_ENV_PATH};%PATH%${PROJECT_THIRD_PARTY_BUILDTOOLS_EOL}")
            file(APPEND "${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-build-debug.bat" "set PATH=${ATFRAME_THIRD_PARTY_ENV_PATH};%PATH%${PROJECT_THIRD_PARTY_BUILDTOOLS_EOL}")
            file(APPEND "${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-build-release.bat" "set PATH=${ATFRAME_THIRD_PARTY_ENV_PATH};%PATH%${PROJECT_THIRD_PARTY_BUILDTOOLS_EOL}")
            project_make_executable("${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-cmake.bat")
            project_make_executable("${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-build-debug.bat")
            project_make_executable("${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-build-release.bat")
            project_expand_list_for_command_line_to_file("${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-cmake.bat"
                ${3RD_PARTY_LIBRESSL_BUILD_FLAGS}
            )
            project_expand_list_for_command_line_to_file("${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-build-debug.bat"
                ${CMAKE_COMMAND} --build . --target install --config Debug "-j"
            )
            project_expand_list_for_command_line_to_file("${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-build-release.bat"
                ${CMAKE_COMMAND} --build . --target install --config Release "-j"
            )

            # build & install
            message(STATUS "@${3RD_PARTY_LIBRESSL_BUILD_DIR} Run: ${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-cmake.bat")
            message(STATUS "@${3RD_PARTY_LIBRESSL_BUILD_DIR} Run: ${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-build-release.bat")
            execute_process(
                COMMAND "${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-cmake.bat"
                WORKING_DIRECTORY ${3RD_PARTY_LIBRESSL_BUILD_DIR}
            )
            # install debug target
            if (MSVC)
                message(STATUS "@${3RD_PARTY_LIBRESSL_BUILD_DIR} Run: ${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-build-debug.bat")
                execute_process(
                    COMMAND "${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-build-debug.bat"
                    WORKING_DIRECTORY ${3RD_PARTY_LIBRESSL_BUILD_DIR}
                )
            endif ()

            execute_process(
                COMMAND "${3RD_PARTY_LIBRESSL_BUILD_DIR}/run-build-release.bat"
                WORKING_DIRECTORY ${3RD_PARTY_LIBRESSL_BUILD_DIR}
            )
        endif ()

        unset(LIBRESSL_FOUND CACHE)
        unset(LIBRESSL_INCLUDE_DIR CACHE)
        unset(LIBRESSL_CRYPTO_LIBRARY CACHE)
        unset(LIBRESSL_SSL_LIBRARY CACHE)
        unset(LIBRESSL_TLS_LIBRARY CACHE)
        unset(LIBRESSL_LIBRARIES CACHE)
        unset(LIBRESSL_VERSION CACHE)
        find_package(LibreSSL)
        PROJECT_3RD_PARTY_LIBRESSL_IMPORT()
    endif ()

    if(LIBRESSL_FOUND)
        EchoWithColor(COLOR GREEN "-- Dependency: Libressl found.(${LIBRESSL_VERSION})")
    else()
        EchoWithColor(COLOR RED "-- Dependency: Libressl is required")
        message(FATAL_ERROR "must at least have one of openssl,libressl or mbedtls")
    endif()


    if (3RD_PARTY_LIBRESSL_BACKUP_FIND_ROOT)
        set(CMAKE_FIND_ROOT_PATH ${3RD_PARTY_LIBRESSL_BACKUP_FIND_ROOT})
        unset(3RD_PARTY_LIBRESSL_BACKUP_FIND_ROOT)
    endif ()
endif()