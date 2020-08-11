# =========== 3rdparty mbedtls ==================
if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.10")
    include_guard(GLOBAL)
endif()

if (NOT 3RD_PARTY_MBEDTLS_FOUND)
    if (NOT 3RD_PARTY_MBEDTLS_BASE_DIR)
        set (3RD_PARTY_MBEDTLS_BASE_DIR ${CMAKE_CURRENT_LIST_DIR})
    endif()

    set (3RD_PARTY_MBEDTLS_PKG_DIR "${3RD_PARTY_MBEDTLS_BASE_DIR}/pkg")

    set (3RD_PARTY_MBEDTLS_DEFAULT_VERSION "2.23.0")
    set (3RD_PARTY_MBEDTLS_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/prebuilt/${PROJECT_PREBUILT_PLATFORM_NAME}")

    macro(PROJECT_3RD_PARTY_MBEDTLS_IMPORT)
        if(NOT 3RD_PARTY_MBEDTLS_FOUND)
            if (TARGET mbedtls_static)
                list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES mbedtls_static)
                set(3RD_PARTY_MBEDTLS_FOUND TRUE)
            elseif (TARGET mbedtls)
                list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES mbedtls)
                set(3RD_PARTY_MBEDTLS_FOUND TRUE)
            elseif (mbedTLS_FOUND OR MbedTLS_FOUND)
                if (MBEDTLS_INCLUDE_DIR)
                    list(APPEND 3RD_PARTY_PUBLIC_INCLUDE_DIRS ${MBEDTLS_INCLUDE_DIR})
                elseif(MbedTLS_INCLUDE_DIRS)
                    list(APPEND 3RD_PARTY_PUBLIC_INCLUDE_DIRS ${MbedTLS_INCLUDE_DIRS})
                endif()

                if (MBEDTLS_TLS_LIBRARY)
                    list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES ${MBEDTLS_TLS_LIBRARY})
                elseif(MbedTLS_TLS_LIBRARIES)
                    list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES ${MbedTLS_TLS_LIBRARIES})
                endif()

                set(3RD_PARTY_MBEDTLS_FOUND TRUE)
            endif()
        endif()
    endmacro()

    if (VCPKG_TOOLCHAIN)
        find_package(mbedtls)
        PROJECT_3RD_PARTY_MBEDTLS_IMPORT()
    endif ()

    if (NOT 3RD_PARTY_MBEDTLS_FOUND)
        if(NOT EXISTS ${3RD_PARTY_MBEDTLS_PKG_DIR})
            file(MAKE_DIRECTORY ${3RD_PARTY_MBEDTLS_PKG_DIR})
        endif()

        set (MbedTLS_ROOT ${3RD_PARTY_MBEDTLS_ROOT_DIR})
        set(3RD_PARTY_MBEDTLS_BACKUP_FIND_ROOT ${CMAKE_FIND_ROOT_PATH})
        list(APPEND CMAKE_FIND_ROOT_PATH ${3RD_PARTY_MBEDTLS_ROOT_DIR})

        set(3RD_PARTY_MBEDTLS_CMAKE_OPTIONS 
            "-DENABLE_TESTING=OFF" "-DUSE_STATIC_MBEDTLS_LIBRARY=ON" "-DUSE_SHARED_MBEDTLS_LIBRARY=OFF" "-DENABLE_PROGRAMS=OFF"
        )
        if (ZLIB_FOUND AND ZLIB_ROOT)
            list(APPEND 3RD_PARTY_MBEDTLS_CMAKE_OPTIONS "-DZLIB_ROOT=${ZLIB_ROOT}" "-DENABLE_ZLIB_SUPPORT=ON")
        endif ()

        EchoWithColor(COLOR GREEN "-- Try to configure and use mbedtls")
        FindConfigurePackage(
            PACKAGE MbedTLS
            BUILD_WITH_CMAKE
            CMAKE_FLAGS ${3RD_PARTY_MBEDTLS_CMAKE_OPTIONS}
            WORKING_DIRECTORY "${3RD_PARTY_MBEDTLS_PKG_DIR}"
            SRC_DIRECTORY_NAME "mbedtls-${3RD_PARTY_MBEDTLS_DEFAULT_VERSION}"
            GIT_BRANCH "mbedtls-${3RD_PARTY_MBEDTLS_DEFAULT_VERSION}"
            BUILD_DIRECTORY "${3RD_PARTY_MBEDTLS_PKG_DIR}/mbedtls-${3RD_PARTY_MBEDTLS_DEFAULT_VERSION}/build_jobs_${PROJECT_PREBUILT_PLATFORM_NAME}"
            PREFIX_DIRECTORY ${3RD_PARTY_MBEDTLS_ROOT_DIR}
            GIT_URL "https://github.com/ARMmbed/mbedtls.git"
            CMAKE_INHIRT_BUILD_ENV
        )

        PROJECT_3RD_PARTY_MBEDTLS_IMPORT()
    endif()


    if (3RD_PARTY_MBEDTLS_BACKUP_FIND_ROOT)
        set(CMAKE_FIND_ROOT_PATH ${3RD_PARTY_MBEDTLS_BACKUP_FIND_ROOT})
        unset(3RD_PARTY_MBEDTLS_BACKUP_FIND_ROOT)
    endif ()
endif()
