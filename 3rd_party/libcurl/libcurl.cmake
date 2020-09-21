if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.10")
    include_guard(GLOBAL)
endif()
# =========== 3rdparty libcurl ==================
if (NOT CURL_EXECUTABLE)
    set (3RD_PARTY_LIBCURL_VERSION "7.71.1")
    set (3RD_PARTY_LIBCURL_PKG_NAME "curl-${3RD_PARTY_LIBCURL_VERSION}")
    set (3RD_PARTY_LIBCURL_SRC_URL_PREFIX "http://curl.haxx.se/download")

    macro(PROJECT_3RD_PARTY_LIBCURL_IMPORT)
        if (CURL_FOUND)
            if (TARGET CURL::libcurl)
                list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES CURL::libcurl)
                if (LIBRESSL_FOUND AND TARGET LibreSSL::Crypto AND TARGET LibreSSL::SSL)
                    project_build_tools_patch_imported_link_interface_libraries(
                        CURL::libcurl
                        REMOVE_LIBRARIES "OpenSSL::SSL;OpenSSL::Crypto"
                        ADD_LIBRARIES "LibreSSL::SSL;LibreSSL::Crypto"
                    )
                endif()
            else()
                if (CURL_INCLUDE_DIRS)
                    list(APPEND 3RD_PARTY_PUBLIC_INCLUDE_DIRS ${CURL_INCLUDE_DIRS})
                endif ()

                if(3RD_PARTY_LIBCURL_STATIC_LINK_NAMES)
                    list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES ${3RD_PARTY_LIBCURL_STATIC_LINK_NAMES})
                endif()
            endif ()

            if (TARGET CURL::curl)
                get_target_property(CURL_EXECUTABLE CURL::curl IMPORTED_LOCATION)
                if (CURL_EXECUTABLE AND EXISTS ${CURL_EXECUTABLE})
                    file(COPY ${CURL_EXECUTABLE} DESTINATION "${PROJECT_INSTALL_TOOLS_DIR}/bin" USE_SOURCE_PERMISSIONS)
                else()
                    get_target_property(CURL_EXECUTABLE CURL::curl IMPORTED_LOCATION_NOCONFIG)
                    if (CURL_EXECUTABLE AND EXISTS ${CURL_EXECUTABLE})
                        file(COPY ${CURL_EXECUTABLE} DESTINATION "${PROJECT_INSTALL_TOOLS_DIR}/bin" USE_SOURCE_PERMISSIONS)
                    endif ()
                endif ()
            else ()
                find_program(CURL_EXECUTABLE NAMES curl curl.exe PATHS 
                    "${CURL_INCLUDE_DIRS}/../bin" "${CURL_INCLUDE_DIRS}/../" ${CURL_INCLUDE_DIRS}
                    NO_SYSTEM_ENVIRONMENT_PATH NO_CMAKE_SYSTEM_PATH)
                if (CURL_EXECUTABLE AND EXISTS ${CURL_EXECUTABLE})
                    file(COPY ${CURL_EXECUTABLE} DESTINATION "${PROJECT_INSTALL_TOOLS_DIR}/bin" USE_SOURCE_PERMISSIONS)
                endif ()
            endif ()
        endif()
    endmacro()

    if (VCPKG_TOOLCHAIN)
        find_package(CURL)
        PROJECT_3RD_PARTY_LIBCURL_IMPORT()
    endif ()

    if (NOT CURL_FOUND)
        if (CURL_ROOT)
            set(Libcurl_ROOT ${CURL_ROOT})
            set(LIBCURL_ROOT ${CURL_ROOT})
        elseif (Libcurl_ROOT)
            set(LIBCURL_ROOT ${Libcurl_ROOT})
            set(CURL_ROOT ${Libcurl_ROOT})
        elseif (LIBCURL_ROOT)
            set(Libcurl_ROOT ${LIBCURL_ROOT})
            set(CURL_ROOT ${LIBCURL_ROOT})
        endif()

        set(Libcurl_ROOT ${PROJECT_3RD_PARTY_INSTALL_DIR})
        set(LIBCURL_ROOT ${PROJECT_3RD_PARTY_INSTALL_DIR})

        set(CURL_ROOT ${LIBCURL_ROOT})
        set(3RD_PARTY_LIBCURL_BACKUP_FIND_ROOT ${CMAKE_FIND_ROOT_PATH})
        list(APPEND CMAKE_FIND_ROOT_PATH ${CURL_ROOT})

        unset(3RD_PARTY_LIBCURL_SSL_BACKEND)
        if (OPENSSL_FOUND)
            set(3RD_PARTY_LIBCURL_SSL_BACKEND "-DCMAKE_USE_OPENSSL=YES" )
            if (OPENSSL_ROOT_DIR)
                list(APPEND 3RD_PARTY_LIBCURL_SSL_BACKEND "-DOPENSSL_ROOT_DIR=${OPENSSL_ROOT_DIR}")
            endif()
            if (OPENSSL_USE_STATIC_LIBS)
                list(APPEND 3RD_PARTY_LIBCURL_SSL_BACKEND "-DOPENSSL_USE_STATIC_LIBS=${OPENSSL_USE_STATIC_LIBS}")
            endif ()
        elseif (3RD_PARTY_MBEDTLS_FOUND)
            set(3RD_PARTY_LIBCURL_SSL_BACKEND "-DCMAKE_USE_MBEDTLS=YES" )
            if (MbedTLS_ROOT)
                list(APPEND 3RD_PARTY_LIBCURL_SSL_BACKEND "-DMbedTLS_ROOT=${MbedTLS_ROOT}")
            endif ()
        endif ()

        FindConfigurePackage(
            PACKAGE CURL
            BUILD_WITH_CMAKE
            CMAKE_FLAGS "-DBUILD_SHARED_LIBS=NO" "-DCMAKE_POSITION_INDEPENDENT_CODE=YES" ${3RD_PARTY_LIBCURL_SSL_BACKEND}
            WORKING_DIRECTORY "${PROJECT_3RD_PARTY_PACKAGE_DIR}"
            BUILD_DIRECTORY "${PROJECT_3RD_PARTY_PACKAGE_DIR}/${3RD_PARTY_LIBCURL_PKG_NAME}/build_jobs_${PROJECT_PREBUILT_PLATFORM_NAME}"
            PREFIX_DIRECTORY "${PROJECT_3RD_PARTY_INSTALL_DIR}"
            TAR_URL "${3RD_PARTY_LIBCURL_SRC_URL_PREFIX}/${3RD_PARTY_LIBCURL_PKG_NAME}.tar.gz"
            ZIP_URL "${3RD_PARTY_LIBCURL_SRC_URL_PREFIX}/${3RD_PARTY_LIBCURL_PKG_NAME}.zip"
        )

        if(CURL_FOUND)
            EchoWithColor(COLOR GREEN "-- Dependency: libcurl found.(${CURL_INCLUDE_DIRS}|${CURL_LIBRARIES})")
        else()
            EchoWithColor(COLOR RED "-- Dependency: libcurl is required")
            message(FATAL_ERROR "libcurl not found")
        endif()

        unset(3RD_PARTY_LIBCURL_STATIC_LINK_NAMES)
        if (TARGET CURL::libcurl)
            EchoWithColor(COLOR GREEN "-- Libcurl: use target CURL::libcurl")
        else()
            set(3RD_PARTY_LIBCURL_TEST_SRC "#include <curl/curl.h>
            #include <stdio.h>

            int main () {
                curl_global_init(CURL_GLOBAL_ALL)\;
                printf(\"libcurl version: %s\", LIBCURL_VERSION)\;
                return 0\; 
            }")

            file(WRITE "${CMAKE_BINARY_DIR}/try_run_libcurl_test.c" ${3RD_PARTY_LIBCURL_TEST_SRC})

            if(MSVC)
                try_compile(3RD_PARTY_LIBCURL_TRY_COMPILE_RESULT
                    ${CMAKE_BINARY_DIR} "${CMAKE_BINARY_DIR}/try_run_libcurl_test.c"
                    CMAKE_FLAGS -DINCLUDE_DIRECTORIES=${CURL_INCLUDE_DIRS}
                    LINK_LIBRARIES ${CURL_LIBRARIES}
                    OUTPUT_VARIABLE 3RD_PARTY_LIBCURL_TRY_COMPILE_DYN_MSG
                )
            else()
                try_run(3RD_PARTY_LIBCURL_TRY_RUN_RESULT 3RD_PARTY_LIBCURL_TRY_COMPILE_RESULT
                    ${CMAKE_BINARY_DIR} "${CMAKE_BINARY_DIR}/try_run_libcurl_test.c"
                    CMAKE_FLAGS -DINCLUDE_DIRECTORIES=${CURL_INCLUDE_DIRS}
                    LINK_LIBRARIES ${CURL_LIBRARIES}
                    COMPILE_OUTPUT_VARIABLE 3RD_PARTY_LIBCURL_TRY_COMPILE_DYN_MSG
                    RUN_OUTPUT_VARIABLE 3RD_PARTY_LIBCURL_TRY_RUN_OUT
                )
            endif()

            if (NOT 3RD_PARTY_LIBCURL_TRY_COMPILE_RESULT)
                EchoWithColor(COLOR YELLOW "-- Libcurl: Dynamic symbol test in ${CURL_LIBRARIES} failed, try static symbols")
                if(MSVC)
                    if (ZLIB_FOUND)
                        list(APPEND 3RD_PARTY_LIBCURL_STATIC_LINK_NAMES ${ZLIB_LIBRARIES})
                    endif()

                    try_compile(3RD_PARTY_LIBCURL_TRY_COMPILE_RESULT
                        ${CMAKE_BINARY_DIR} "${CMAKE_BINARY_DIR}/try_run_libcurl_test.c"
                        CMAKE_FLAGS -DINCLUDE_DIRECTORIES=${CURL_INCLUDE_DIRS}
                        COMPILE_DEFINITIONS /D CURL_STATICLIB
                        LINK_LIBRARIES ${CURL_LIBRARIES} ${3RD_PARTY_LIBCURL_STATIC_LINK_NAMES}
                        OUTPUT_VARIABLE 3RD_PARTY_LIBCURL_TRY_COMPILE_STA_MSG
                    )
                else()
                    get_filename_component(3RD_PARTY_LIBCURL_LIBDIR ${CURL_LIBRARIES} DIRECTORY)
                    find_package(PkgConfig)
                    if (PKG_CONFIG_FOUND AND EXISTS "${3RD_PARTY_LIBCURL_LIBDIR}/pkgconfig/libcurl.pc")
                        pkg_check_modules(LIBCURL "${3RD_PARTY_LIBCURL_LIBDIR}/pkgconfig/libcurl.pc")
                        list(APPEND 3RD_PARTY_LIBCURL_STATIC_LINK_NAMES ${LIBCURL_STATIC_LIBRARIES})
                        list(REMOVE_ITEM 3RD_PARTY_LIBCURL_STATIC_LINK_NAMES curl)
                        message(STATUS "Libcurl use static link with ${3RD_PARTY_LIBCURL_STATIC_LINK_NAMES} in ${3RD_PARTY_LIBCURL_LIBDIR}")
                    else()
                        if (OPENSSL_FOUND)
                            list(APPEND 3RD_PARTY_LIBCURL_STATIC_LINK_NAMES ${OPENSSL_LIBRARIES})
                        else ()
                            list(APPEND 3RD_PARTY_LIBCURL_STATIC_LINK_NAMES ssl crypto)
                        endif()
                        if (ZLIB_FOUND)
                            list(APPEND 3RD_PARTY_LIBCURL_STATIC_LINK_NAMES ${ZLIB_LIBRARIES})
                        else ()
                            list(APPEND 3RD_PARTY_LIBCURL_STATIC_LINK_NAMES z)
                        endif()
                    endif ()
                    
                    try_run(3RD_PARTY_LIBCURL_TRY_RUN_RESULT 3RD_PARTY_LIBCURL_TRY_COMPILE_RESULT
                        ${CMAKE_BINARY_DIR} "${CMAKE_BINARY_DIR}/try_run_libcurl_test.c"
                        CMAKE_FLAGS -DCMAKE_INCLUDE_DIRECTORIES_BEFORE=${CURL_INCLUDE_DIRS}
                        COMPILE_DEFINITIONS -DCURL_STATICLIB
                        LINK_LIBRARIES ${CURL_LIBRARIES} ${3RD_PARTY_LIBCURL_STATIC_LINK_NAMES} -lpthread
                        COMPILE_OUTPUT_VARIABLE 3RD_PARTY_LIBCURL_TRY_COMPILE_STA_MSG
                        RUN_OUTPUT_VARIABLE 3RD_PARTY_LIBCURL_TRY_RUN_OUT
                    )
                    list(APPEND 3RD_PARTY_LIBCURL_STATIC_LINK_NAMES pthread)
                endif()
                if (NOT 3RD_PARTY_LIBCURL_TRY_COMPILE_RESULT)
                    message(STATUS ${3RD_PARTY_LIBCURL_TRY_COMPILE_DYN_MSG})
                    message(STATUS ${3RD_PARTY_LIBCURL_TRY_COMPILE_STA_MSG})
                    message(FATAL_ERROR "Libcurl: try compile with ${CURL_LIBRARIES} failed")
                else()
                    EchoWithColor(COLOR GREEN "-- Libcurl: use static symbols")
                    if(3RD_PARTY_LIBCURL_TRY_RUN_OUT)
                        message(STATUS ${3RD_PARTY_LIBCURL_TRY_RUN_OUT})
                    endif()
                    list(APPEND 3RD_PARTY_PUBLIC_DEFINITIONS CURL_STATICLIB=1)
                endif()
            else()
                EchoWithColor(COLOR GREEN "-- Libcurl: use dynamic symbols")
                if(3RD_PARTY_LIBCURL_TRY_RUN_OUT)
                    message(STATUS ${3RD_PARTY_LIBCURL_TRY_RUN_OUT})
                endif()
            endif()
        endif()
        unset(3RD_PARTY_LIBCURL_TRY_RUN_OUT)

        PROJECT_3RD_PARTY_LIBCURL_IMPORT()
    endif()

    if (3RD_PARTY_LIBCURL_BACKUP_FIND_ROOT)
        set(CMAKE_FIND_ROOT_PATH ${3RD_PARTY_LIBCURL_BACKUP_FIND_ROOT})
        unset(3RD_PARTY_LIBCURL_BACKUP_FIND_ROOT)
    endif ()
endif()