if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.10")
    include_guard(GLOBAL)
endif()

# =========== 3rdparty libwebsockets ==================
function (PROJECT_3RD_PARTY_LIBWEBSOCKETS_PATCH_IMPORTED_TARGET TARGET_NAME)
    unset(PATCH_REMOVE_RULES)
    unset(PATCH_ADD_TARGETS)
    if (TARGET OpenSSL::SSL OR TARGET OpenSSL::Crypto OR TARGET LibreSSL::TLS OR TARGET mbedtls_static OR TARGET mbedtls)
        list(APPEND PATCH_REMOVE_RULES "(lib)?crypto" "(lib)?ssl")
        list(APPEND PATCH_ADD_TARGETS ${3RD_PARTY_CRYPT_LINK_NAME})
    endif ()

    if (TARGET uv_a OR TARGET uv OR TARGET libuv)
        list(APPEND PATCH_REMOVE_RULES "(lib)?uv(_a)?")
        list(APPEND PATCH_ADD_TARGETS ${3RD_PARTY_LIBUV_LINK_NAME})
    endif ()
    if (PATCH_REMOVE_RULES OR PATCH_ADD_TARGETS)
        project_build_tools_patch_imported_link_interface_libraries(
            ${TARGET_NAME}
            REMOVE_LIBRARIES ${PATCH_REMOVE_RULES}
            ADD_LIBRARIES ${PATCH_ADD_TARGETS}
        )
    endif()
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

if (NOT Libwebsockets_FOUND AND NOT TARGET websockets AND NOT TARGET websockets_shared)
    if (VCPKG_TOOLCHAIN)
        find_package(Libwebsockets QUIET)
        PROJECT_3RD_PARTY_LIBWEBSOCKETS_IMPORT()
    endif ()

    if (NOT Libwebsockets_FOUND AND NOT TARGET websockets AND NOT TARGET websockets_shared)
        # force to use prebuilt when using mingw
        set(3RD_PARTY_LIBWEBSOCKET_BACKUP_FIND_ROOT ${CMAKE_FIND_ROOT_PATH})
        set(3RD_PARTY_LIBWEBSOCKET_BACKUP_PREFIX_PATH ${CMAKE_PREFIX_PATH})
        list(APPEND CMAKE_FIND_ROOT_PATH ${PROJECT_3RD_PARTY_INSTALL_DIR})
        list(APPEND CMAKE_PREFIX_PATH ${PROJECT_3RD_PARTY_INSTALL_DIR})

        find_package(Libwebsockets QUIET)
        PROJECT_3RD_PARTY_LIBWEBSOCKETS_IMPORT()
        if (NOT Libwebsockets_FOUND)
            set (3RD_PARTY_LIBWEBSOCKETS_VERSION "v4.1.2")
            set (3RD_PARTY_LIBWEBSOCKETS_REPO_DIR "${PROJECT_3RD_PARTY_PACKAGE_DIR}/libwebsockets-${3RD_PARTY_LIBWEBSOCKETS_VERSION}")
            set (3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR "${3RD_PARTY_LIBWEBSOCKETS_REPO_DIR}/build_jobs_${PROJECT_PREBUILT_PLATFORM_NAME}")

            project_git_clone_3rd_party(
                URL "https://github.com/warmcat/libwebsockets.git"
                REPO_DIRECTORY ${3RD_PARTY_LIBWEBSOCKETS_REPO_DIR}
                DEPTH 200
                TAG ${3RD_PARTY_LIBWEBSOCKETS_VERSION}
                WORKING_DIRECTORY ${PROJECT_3RD_PARTY_PACKAGE_DIR}
            )

            if (NOT EXISTS ${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR})
                file(MAKE_DIRECTORY ${3RD_PARTY_LIBWEBSOCKETS_BUILD_DIR})
            endif ()

            # 服务器目前不需要适配ARM和android
            set (3RD_PARTY_LIBWEBSOCKETS_BUILD_OPTIONS ${CMAKE_COMMAND} "${3RD_PARTY_LIBWEBSOCKETS_REPO_DIR}"
                "-DCMAKE_INSTALL_PREFIX=${PROJECT_3RD_PARTY_INSTALL_DIR}"
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
        
        set(CMAKE_FIND_ROOT_PATH ${3RD_PARTY_LIBWEBSOCKET_BACKUP_FIND_ROOT})
        set(CMAKE_PREFIX_PATH ${3RD_PARTY_LIBWEBSOCKET_BACKUP_PREFIX_PATH})
        unset(3RD_PARTY_LIBWEBSOCKET_BACKUP_FIND_ROOT)
        unset(3RD_PARTY_LIBWEBSOCKET_BACKUP_PREFIX_PATH)
    endif()
endif()

if (NOT Libwebsockets_FOUND AND NOT TARGET websockets AND NOT TARGET websockets_shared)
    EchoWithColor(COLOR YELLOW "-- Dependency: libwebsockets not found")
endif ()
