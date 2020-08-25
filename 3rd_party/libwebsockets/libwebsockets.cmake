if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.10")
    include_guard(GLOBAL)
endif()

# =========== 3rdparty libwebsockets ==================
if (NOT 3RD_PARTY_LIBWEBSOCKETS_BASE_DIR)
    set (3RD_PARTY_LIBWEBSOCKETS_BASE_DIR ${CMAKE_CURRENT_LIST_DIR})
endif()

set (3RD_PARTY_LIBWEBSOCKETS_ROOT_DIR "${3RD_PARTY_LIBWEBSOCKETS_BASE_DIR}/prebuilt/${PROJECT_PREBUILT_PLATFORM_NAME}")
set (3RD_PARTY_LIBWEBSOCKETS_VERSION "v4.0.20")


function (PROJECT_3RD_PARTY_LIBWEBSOCKETS_PATCH_IMPORTED_TARGET TARGET_NAME)
    unset(PATCH_INNER_LIBS)
    unset(PATCH_SSL_LIBS)
    unset(PATCH_LIBUV_LIBS)
    if (TARGET OpenSSL::SSL OR TARGET OpenSSL::Crypto OR TARGET LibreSSL::TLS OR TARGET mbedtls_static OR TARGET mbedtls)
        set(PATCH_SSL_LIBS ON)
        list(APPEND PATCH_INNER_LIBS ${3RD_PARTY_CRYPT_LINK_NAME})
    endif ()

    if (TARGET uv_a OR TARGET uv OR TARGET libuv)
        set(PATCH_LIBUV_LIBS ON)
        list(APPEND PATCH_INNER_LIBS ${3RD_PARTY_LIBUV_LINK_NAME})
    endif ()

    get_target_property(OLD_LINK_LIBRARIES ${TARGET_NAME} INTERFACE_LINK_LIBRARIES)
    if (OLD_LINK_LIBRARIES)
        set(PROPERTY_NAME "INTERFACE_LINK_LIBRARIES")
    endif ()
    if (NOT PROPERTY_NAME)
        get_target_property(OLD_LINK_LIBRARIES ${TARGET_NAME} IMPORTED_LINK_INTERFACE_LIBRARIES)
        if (OLD_LINK_LIBRARIES)
            set(PROPERTY_NAME "IMPORTED_LINK_INTERFACE_LIBRARIES")
        endif ()
    endif ()
    if (NOT PROPERTY_NAME)
        get_target_property(OLD_IMPORTED_CONFIGURATIONS ${TARGET_NAME} IMPORTED_CONFIGURATIONS)
        get_target_property(OLD_LINK_LIBRARIES ${TARGET_NAME} "IMPORTED_LINK_INTERFACE_LIBRARIES_${OLD_IMPORTED_CONFIGURATIONS}")
        if (OLD_LINK_LIBRARIES)
            set(PROPERTY_NAME "IMPORTED_LINK_INTERFACE_LIBRARIES_${OLD_IMPORTED_CONFIGURATIONS}")
        endif ()
    endif ()
    if (PROPERTY_NAME)
        foreach(DEP_PATH IN LISTS OLD_LINK_LIBRARIES)
            get_filename_component(DEP_NAME ${DEP_PATH} NAME_WE)
            if (PATCH_SSL_LIBS AND (DEP_NAME MATCHES "(lib)?crypto" OR DEP_NAME MATCHES "(lib)?ssl"))
                continue()
            elseif (PATCH_LIBUV_LIBS AND (DEP_NAME MATCHES "(lib)?uv(_a)?"))
                continue()
            else()
                list(APPEND PATCH_INNER_LIBS ${DEP_PATH})
            endif ()
        endforeach()

        list(REMOVE_DUPLICATES PATCH_INNER_LIBS)
        set_target_properties(${TARGET_NAME} PROPERTIES ${PROPERTY_NAME} "${PATCH_INNER_LIBS}")
        message(STATUS "Dependency: libwebsockets using new ${PROPERTY_NAME}: ${PATCH_INNER_LIBS}")
    endif ()
endfunction()

macro(PROJECT_3RD_PARTY_LIBWEBSOCKETS_IMPORT)
    if(TARGET websockets)
        EchoWithColor(COLOR GREEN "-- Dependency: libwebsockets found.(TARGET: websockets)")
        list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES websockets)
        PROJECT_3RD_PARTY_LIBWEBSOCKETS_PATCH_IMPORTED_TARGET(websockets)
    elseif(TARGET websockets_shared)
        EchoWithColor(COLOR GREEN "-- Dependency: libwebsockets found.(TARGET: websockets_shared)")
        list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES websockets_shared)
        PROJECT_3RD_PARTY_LIBWEBSOCKETS_PATCH_IMPORTED_TARGET(websockets_shared)
    elseif(Libwebsockets_FOUND)
        EchoWithColor(COLOR RED "-- Dependency: libwebsockets found")
        list(APPEND 3RD_PARTY_PUBLIC_INCLUDE_DIRS ${LIBWEBSOCKETS_INCLUDE_DIRS})
        list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES ${LIBWEBSOCKETS_LIBRARIES})
    endif()
endmacro()

if (VCPKG_TOOLCHAIN)
    find_package(Libwebsockets)
    PROJECT_3RD_PARTY_LIBWEBSOCKETS_IMPORT()
endif ()

if (NOT Libwebsockets_FOUND AND NOT TARGET websockets AND NOT TARGET websockets_shared)
    # force to use prebuilt when using mingw
    set(Libwebsockets_ROOT ${3RD_PARTY_LIBWEBSOCKETS_ROOT_DIR})
    set(Libwebsockets_DIR ${3RD_PARTY_LIBWEBSOCKETS_ROOT_DIR})

    if (EXISTS ${Libwebsockets_ROOT})
        find_package(Libwebsockets)
        PROJECT_3RD_PARTY_LIBWEBSOCKETS_IMPORT()
    endif ()
    if (NOT Libwebsockets_FOUND)
        set (3RD_PARTY_LIBWEBSOCKETS_REPO_DIR "${3RD_PARTY_LIBWEBSOCKETS_BASE_DIR}/repo")
        set (3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR "${3RD_PARTY_LIBWEBSOCKETS_REPO_DIR}/build_jobs_${PROJECT_PREBUILT_PLATFORM_NAME}")

        project_git_clone_3rd_party(
            URL "https://github.com/warmcat/libwebsockets.git"
            REPO_DIRECTORY ${3RD_PARTY_LIBWEBSOCKETS_REPO_DIR}
            DEPTH 200
            TAG ${3RD_PARTY_LIBWEBSOCKETS_VERSION}
            WORKING_DIRECTORY ${3RD_PARTY_LIBWEBSOCKETS_BASE_DIR}
        )

        if (NOT EXISTS ${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR})
            file(MAKE_DIRECTORY ${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR})
        endif ()

        # 服务器目前不需要适配ARM和android
        set (3RD_PARTY_LIBWEBSOCKETS_BUILD_OPTIONS ${CMAKE_COMMAND} "${3RD_PARTY_LIBWEBSOCKETS_REPO_DIR}"
            "-DCMAKE_INSTALL_PREFIX=${Libwebsockets_ROOT}"
            "-DLWS_WITH_LIBUV=ON" "-DLWS_LIBUV_LIBRARIES=${Libuv_LIBRARIES}" "-DLWS_LIBUV_INCLUDE_DIRS=${Libuv_INCLUDE_DIRS}"
            "-DLWS_WITH_SHARED=OFF" "-DLWS_STATIC_PIC=ON" "-DLWS_LINK_TESTAPPS_DYNAMIC=OFF" "-DLWS_WITHOUT_CLIENT=ON"
            "-DLWS_WITHOUT_DAEMONIZE=ON" "-DLWS_WITHOUT_TESTAPPS=ON" "-DLWS_WITHOUT_TEST_CLIENT=ON"
            "-DLWS_WITHOUT_TEST_PING=ON" "-DLWS_WITHOUT_TEST_SERVER=ON" "-DLWS_WITHOUT_TEST_SERVER_EXTPOLL=ON"
            "-DLWS_WITH_PLUGINS=ON" "-DLWS_WITHOUT_EXTENSIONS=OFF"
            "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
        )

        if (NOT WIN32 AND NOT CYGWIN AND NOT MINGW) 
            list(APPEND 3RD_PARTY_LIBWEBSOCKETS_BUILD_OPTIONS
                "-DLWS_UNIX_SOCK=ON"
            )
        endif()

        if (ZLIB_INCLUDE_DIRS AND ZLIB_LIBRARIES)
            list(APPEND 3RD_PARTY_LIBWEBSOCKETS_BUILD_OPTIONS 
                "-DLWS_WITH_ZLIB=ON" "-DLWS_ZLIB_LIBRARIES=${ZLIB_LIBRARIES}" "-DLWS_ZLIB_INCLUDE_DIRS=${ZLIB_INCLUDE_DIRS}"
            )
        endif ()
        if (OPENSSL_FOUND AND NOT LIBRESSL_FOUND)
            # list(APPEND 3RD_PARTY_LIBWEBSOCKETS_BUILD_OPTIONS 
            #     # "-DOPENSSL_LIBRARIES=${OPENSSL_LIBRARIES}" "-DOPENSSL_INCLUDE_DIR=${OPENSSL_INCLUDE_DIR}"
            #     # "-DOPENSSL_CRYPTO_LIBRARY=${OPENSSL_CRYPTO_LIBRARY}" "-DOPENSSL_SSL_LIBRARY=${OPENSSL_SSL_LIBRARY}" "-DOPENSSL_VERSION=${OPENSSL_VERSION}" # "-DLWS_WITH_BORINGSSL=ON"
            # )
            if (OPENSSL_ROOT_DIR)
                list(APPEND 3RD_PARTY_LIBWEBSOCKETS_BUILD_OPTIONS "-DOPENSSL_ROOT_DIR=${OPENSSL_ROOT_DIR}")
            endif ()
            if (NOT MSVC)
                # libwebsockets的检测脚本写得不是特别健壮，会导致MSVC环境下很多链接问题，所以还是用动态库
                # 其他环境为了适配兼容性一律使用静态库
                list(APPEND 3RD_PARTY_LIBWEBSOCKETS_BUILD_OPTIONS "-DOPENSSL_USE_STATIC_LIBS=YES")
            endif ()
        endif ()
        if (NOT MSVC)
            file(WRITE "${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR}/run-config.sh" "#!/bin/bash${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
            file(WRITE "${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR}/run-build-release.sh" "#!/bin/bash${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
            file(APPEND "${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR}/run-config.sh" "export PATH=\"${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR}:\$PATH\"${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
            file(APPEND "${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR}/run-build-release.sh" "export PATH=\"${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR}:\$PATH\"${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
            project_make_executable("${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR}/run-config.sh")
            project_make_executable("${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR}/run-build-release.sh")

            list(APPEND 3RD_PARTY_LIBWEBSOCKETS_BUILD_OPTIONS "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}")
            if (CMAKE_AR)
                list(APPEND 3RD_PARTY_LIBWEBSOCKETS_BUILD_OPTIONS "-DCMAKE_AR=${CMAKE_AR}")
            endif ()

            file(APPEND "${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR}/run-config.sh" 
                "export CFLAGS=\"\$CFLAGS -I${OPENSSL_INCLUDE_DIR}\"${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
            )
            find_package(Threads)
            if (CMAKE_USE_PTHREADS_INIT)
                file(APPEND "${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR}/run-config.sh" 
                    "export LDFLAGS=\"\$LDFLAGS -L${3RD_PARTY_OPENSSL_LIBRARY_DIR} -ldl -pthread\"${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
                )
            else ()
                file(APPEND "${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR}/run-config.sh" 
                    "export LDFLAGS=\"\$LDFLAGS -L${3RD_PARTY_OPENSSL_LIBRARY_DIR} -ldl\"${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}"
                )
            endif ()

            list(APPEND 3RD_PARTY_LIBWEBSOCKETS_BUILD_OPTIONS "-DCMAKE_C_FLAGS=${CMAKE_C_FLAGS}")
            list(APPEND 3RD_PARTY_LIBWEBSOCKETS_BUILD_OPTIONS "-DCMAKE_C_FLAGS_DEBUG=${CMAKE_C_FLAGS_DEBUG}")
            list(APPEND 3RD_PARTY_LIBWEBSOCKETS_BUILD_OPTIONS "-DCMAKE_C_FLAGS_RELWITHDEBINFO=${CMAKE_C_FLAGS_RELWITHDEBINFO}")
            list(APPEND 3RD_PARTY_LIBWEBSOCKETS_BUILD_OPTIONS "-DCMAKE_C_FLAGS_RELEASE=${CMAKE_C_FLAGS_RELEASE}")

            if (CMAKE_EXE_LINKER_FLAGS)
                list(APPEND 3RD_PARTY_LIBWEBSOCKETS_BUILD_OPTIONS "-DCMAKE_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}")
            endif ()

            if (CMAKE_RANLIB)
                list(APPEND 3RD_PARTY_LIBWEBSOCKETS_BUILD_OPTIONS "-DCMAKE_RANLIB=${CMAKE_RANLIB}")
            endif ()

            if (CMAKE_STATIC_LINKER_FLAGS)
                list(APPEND 3RD_PARTY_LIBWEBSOCKETS_BUILD_OPTIONS "-DCMAKE_STATIC_LINKER_FLAGS=${CMAKE_STATIC_LINKER_FLAGS}")
            endif ()

            project_expand_list_for_command_line_to_file("${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR}/run-config.sh"
                ${3RD_PARTY_LIBWEBSOCKETS_BUILD_OPTIONS}
            )
            project_expand_list_for_command_line_to_file("${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR}/run-build-release.sh"
                ${CMAKE_COMMAND} "--build" "." "-j"
            )
            project_expand_list_for_command_line_to_file("${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR}/run-build-release.sh"
                ${CMAKE_COMMAND} "--build" "." "--" "install"
            )

            # build & install
            message(STATUS "@${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR} Run: ./run-config.sh")
            message(STATUS "@${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR} Run: ./run-build-release.sh")
            execute_process(
                COMMAND "${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR}/run-config.sh"
                WORKING_DIRECTORY ${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR}
            )

            execute_process(
                COMMAND "${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR}/run-build-release.sh"
                WORKING_DIRECTORY ${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR}
            )

        else ()
            file(WRITE "${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR}/run-config.bat" "@echo off${PROJECT_THIRD_PARTY_BUILDTOOLS_EOL}")
            file(WRITE "${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR}/run-build-release.bat" "@echo off${PROJECT_THIRD_PARTY_BUILDTOOLS_EOL}")
            file(APPEND "${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR}/run-config.bat" "set PATH=${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR};%PATH%${PROJECT_THIRD_PARTY_BUILDTOOLS_EOL}")
            file(APPEND "${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR}/run-build-release.bat" "set PATH=${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR};%PATH%${PROJECT_THIRD_PARTY_BUILDTOOLS_EOL}")
            project_make_executable("${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR}/run-config.bat")
            project_make_executable("${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR}/run-build-release.bat")

            project_expand_list_for_command_line_to_file("${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR}/run-config.bat"
                ${3RD_PARTY_LIBWEBSOCKETS_BUILD_OPTIONS}
            )
            project_expand_list_for_command_line_to_file("${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR}/run-build-release.bat"
                ${CMAKE_COMMAND} "--build" "." "-j"
            )
            project_expand_list_for_command_line_to_file("${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR}/run-build-release.bat"
                ${CMAKE_COMMAND} "--build" "." "--target" "INSTALL"
            )

            # build & install
            message(STATUS "@${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR} Run: ${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR}/run-config.bat")
            message(STATUS "@${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR} Run: ${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR}/run-build-release.bat")
            execute_process(
                COMMAND "${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR}/run-config.bat"
                WORKING_DIRECTORY ${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR}
            )

            execute_process(
                COMMAND "${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR}/run-build-release.bat"
                WORKING_DIRECTORY ${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR}
            )
        endif ()

        find_package(Libwebsockets)
        PROJECT_3RD_PARTY_LIBWEBSOCKETS_IMPORT()
    endif ()
    # FindConfigurePackage(
    #     PACKAGE Libwebsockets
    #     BUILD_WITH_CMAKE
    #     CMAKE_FLAGS "-DLWS_WITH_LIBUV=ON" "-DLWS_LIBUV_LIBRARIES=${Libuv_LIBRARIES}" "-DLWS_LIBUV_INCLUDE_DIRS=${Libuv_INCLUDE_DIRS}"
    #                  "-DLWS_WITH_SHARED=OFF" "-DLWS_STATIC_PIC=ON" "-DLWS_LINK_TESTAPPS_DYNAMIC=OFF" "-DLWS_UNIX_SOCK=ON" "-DLWS_WITHOUT_CLIENT=ON"
    #                  "-DLWS_WITHOUT_DAEMONIZE=ON" "-DLWS_WITHOUT_TESTAPPS=ON" "-DLWS_WITHOUT_TEST_CLIENT=ON"
    #                  "-DLWS_WITHOUT_TEST_PING=ON" "-DLWS_WITHOUT_TEST_SERVER=ON" "-DLWS_WITHOUT_TEST_SERVER_EXTPOLL=ON"
    #                  "-DLWS_WITH_PLUGINS=ON" "-DLWS_WITHOUT_EXTENSIONS=OFF"
    #                  "-DOPENSSL_LIBRARIES=${OPENSSL_LIBRARIES}" "-DOPENSSL_INCLUDE_DIR=${OPENSSL_INCLUDE_DIR}"
    #                  "-DOPENSSL_CRYPTO_LIBRARY=${OPENSSL_CRYPTO_LIBRARY}" "-DOPENSSL_SSL_LIBRARY=${OPENSSL_SSL_LIBRARY}" "-DOPENSSL_VERSION=${OPENSSL_VERSION}" # "-DLWS_WITH_BORINGSSL=ON"
    #                  "-DOPENSSL_ROOT_DIR=${3RD_PARTY_OPENSSL_ROOT_DIR}" "-DOPENSSL_USE_STATIC_LIBS=YES"
    #                  "-DLWS_WITH_ZLIB=ON" "-DLWS_ZLIB_LIBRARIES=${ZLIB_LIBRARIES}" "-DLWS_ZLIB_INCLUDE_DIRS=${ZLIB_INCLUDE_DIRS}"
    #                  "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
    #                  # 下面和openssl版本相关，自动检测会有问题，我们根据使用的openssl强行设置
    #                  # "-DLWS_HAVE_SSL_CTX_set1_param=1" "-DLWS_HAVE_SSL_SET_INFO_CALLBACK=1" "-DLWS_HAVE_X509_VERIFY_PARAM_set1_host=1" "-DLWS_HAVE_RSA_SET0_KEY=1"
    #                  # "-DLWS_HAVE_X509_get_key_usage=1" "-DLWS_HAVE_SSL_CTX_get0_certificate=1" "-DLWS_HAVE_SSL_get0_alpn_selected=1" "-DLWS_HAVE_SSL_set_alpn_protos=1"
    #                  # "-DLWS_HAVE_SSL_CTX_set_ciphersuites=1" "-DLWS_HAVE_TLS_CLIENT_METHOD=1" "-DLWS_HAVE_TLSV1_2_CLIENT_METHOD=1" "-DLWS_HAVE_HMAC_CTX_new=1"
    #     MAKE_FLAGS "-j8"
    #     WORKING_DIRECTORY ${3RD_PARTY_LIBWEBSOCKETS_BASE_DIR}
    #     BUILD_DIRECTORY "${3RD_PARTY_LIBWEBSOCKETS_REPO_DIR}/build_jobs_${PROJECT_PREBUILT_PLATFORM_NAME}"
    #     PREFIX_DIRECTORY "${3RD_PARTY_LIBWEBSOCKETS_ROOT_DIR}"
    #     SRC_DIRECTORY_NAME "repo"
    #     GIT_URL "https://github.com/warmcat/libwebsockets.git"
    #     GIT_BRANCH ${3RD_PARTY_LIBWEBSOCKETS_VERSION}
    # )
endif()

if (NOT Libwebsockets_FOUND AND NOT TARGET websockets AND NOT TARGET websockets_shared)
    EchoWithColor(COLOR YELLOW "-- Dependency: libwebsockets not found")
endif ()

if (LIBWEBSOCKETS_INCLUDE_DIRS)
    list(APPEND 3RD_PARTY_PUBLIC_INCLUDE_DIRS ${LIBWEBSOCKETS_INCLUDE_DIRS})
endif ()
