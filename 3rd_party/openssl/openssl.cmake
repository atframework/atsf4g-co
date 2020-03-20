if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.10")
    include_guard(GLOBAL)
endif()

# =========== 3rdparty openssl ==================
if (NOT 3RD_PARTY_OPENSSL_BASE_DIR)
    set (3RD_PARTY_OPENSSL_BASE_DIR ${CMAKE_CURRENT_LIST_DIR})
endif()

set (3RD_PARTY_OPENSSL_PKG_DIR "${3RD_PARTY_OPENSSL_BASE_DIR}/pkg")

set (3RD_PARTY_OPENSSL_DEFAULT_VERSION "1.1.1d")
set (3RD_PARTY_OPENSSL_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/prebuilt/${PROJECT_PREBUILT_PLATFORM_NAME}")
# "no-hw"
set (3RD_PARTY_OPENSSL_BUILD_OPTIONS "--prefix=${3RD_PARTY_OPENSSL_ROOT_DIR}" "--openssldir=${3RD_PARTY_OPENSSL_ROOT_DIR}/ssl"
    "--release" "no-deprecated" "no-dso" "no-shared" "no-tests" "no-external-tests" "no-external-tests" 
    "no-aria" "no-bf" "no-blake2" "no-camellia" "no-cast" "no-idea" "no-md2" "no-md4" "no-mdc2" "no-rc2" "no-rc4" "no-rc5" "no-ssl3"
)

if (NOT OPENSSL_FOUND)
    if(NOT EXISTS ${3RD_PARTY_OPENSSL_PKG_DIR})
        file(MAKE_DIRECTORY ${3RD_PARTY_OPENSSL_PKG_DIR})
    endif()

    set (OPENSSL_ROOT_DIR ${3RD_PARTY_OPENSSL_ROOT_DIR})
    set(OPENSSL_USE_STATIC_LIBS TRUE)

    set(3RD_PARTY_OPENSSL_BACKUP_FIND_ROOT ${CMAKE_FIND_ROOT_PATH})
    list(APPEND CMAKE_FIND_ROOT_PATH ${OPENSSL_ROOT_DIR})

    find_library(3RD_PARTY_OPENSSL_FIND_LIB_CRYPTO NAMES crypto PATHS "${3RD_PARTY_OPENSSL_ROOT_DIR}/lib" "${3RD_PARTY_OPENSSL_ROOT_DIR}/lib64" NO_DEFAULT_PATH)
    find_library(3RD_PARTY_OPENSSL_FIND_LIB_SSL NAMES ssl PATHS "${3RD_PARTY_OPENSSL_ROOT_DIR}/lib" "${3RD_PARTY_OPENSSL_ROOT_DIR}/lib64" NO_DEFAULT_PATH)

    if (3RD_PARTY_OPENSSL_FIND_LIB_CRYPTO AND 3RD_PARTY_OPENSSL_FIND_LIB_SSL)
        find_package(OpenSSL)
    endif ()

    unset(3RD_PARTY_OPENSSL_FIND_LIB_CRYPTO)
    unset(3RD_PARTY_OPENSSL_FIND_LIB_SSL)
endif()

if (NOT OPENSSL_FOUND)
    EchoWithColor(COLOR GREEN "-- Try to configure and use openssl")
    unset(OPENSSL_EXECUTABLE CACHE)
    unset(OPENSSL_INCLUDE_DIR CACHE)
    unset(OPENSSL_CRYPTO_LIBRARY CACHE)
    unset(OPENSSL_CRYPTO_LIBRARIES CACHE)
    unset(OPENSSL_SSL_LIBRARY CACHE)
    unset(OPENSSL_SSL_LIBRARIES CACHE)
    unset(OPENSSL_LIBRARIES CACHE)
    unset(OPENSSL_VERSION CACHE)
    if (NOT EXISTS "${3RD_PARTY_OPENSSL_PKG_DIR}/openssl-${3RD_PARTY_OPENSSL_DEFAULT_VERSION}")
        if (NOT EXISTS "${3RD_PARTY_OPENSSL_PKG_DIR}/openssl-${3RD_PARTY_OPENSSL_DEFAULT_VERSION}.tar.gz")
            FindConfigurePackageDownloadFile("https://www.openssl.org/source/openssl-${3RD_PARTY_OPENSSL_DEFAULT_VERSION}.tar.gz" "${3RD_PARTY_OPENSSL_PKG_DIR}/openssl-${3RD_PARTY_OPENSSL_DEFAULT_VERSION}.tar.gz")
        endif ()

        execute_process(COMMAND ${CMAKE_COMMAND} -E tar xvf "${3RD_PARTY_OPENSSL_PKG_DIR}/openssl-${3RD_PARTY_OPENSSL_DEFAULT_VERSION}.tar.gz"
            WORKING_DIRECTORY ${3RD_PARTY_OPENSSL_PKG_DIR}
        )
    endif ()

    if (NOT EXISTS "${3RD_PARTY_OPENSSL_PKG_DIR}/openssl-${3RD_PARTY_OPENSSL_DEFAULT_VERSION}")
        EchoWithColor(COLOR RED "-- Dependency: Build openssl failed")
        message(FATAL_ERROR "Dependency: openssl is required")
    endif ()

    set (3RD_PARTY_OPENSSL_BUILD_DIR "${3RD_PARTY_OPENSSL_PKG_DIR}/openssl-${3RD_PARTY_OPENSSL_DEFAULT_VERSION}/build-obj-dir")
    if (NOT EXISTS ${3RD_PARTY_OPENSSL_BUILD_DIR})
        file(MAKE_DIRECTORY ${3RD_PARTY_OPENSSL_BUILD_DIR})
    endif()

    if (MSVC)
        set(3RD_PARTY_OPENSSL_BUILD_MULTI_CORE "/MP")
    else ()
        include(ProcessorCount)
        ProcessorCount(CPU_CORE_NUM)
        set(3RD_PARTY_OPENSSL_BUILD_MULTI_CORE "-j${CPU_CORE_NUM}")
        unset(CPU_CORE_NUM)
    endif ()
    # 服务器目前不需要适配ARM和android
    if (NOT MSVC)
        file(WRITE "${3RD_PARTY_OPENSSL_BUILD_DIR}/run-config.sh" "#!/bin/bash${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
        file(WRITE "${3RD_PARTY_OPENSSL_BUILD_DIR}/run-build-release.sh" "#!/bin/bash${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
        file(APPEND "${3RD_PARTY_OPENSSL_BUILD_DIR}/run-config.sh" "export PATH=\"${3RD_PARTY_OPENSSL_BUILD_DIR}:\$PATH\"${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
        file(APPEND "${3RD_PARTY_OPENSSL_BUILD_DIR}/run-build-release.sh" "export PATH=\"${3RD_PARTY_OPENSSL_BUILD_DIR}:\$PATH\"${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
        project_make_executable("${3RD_PARTY_OPENSSL_BUILD_DIR}/run-config.sh")
        project_make_executable("${3RD_PARTY_OPENSSL_BUILD_DIR}/run-build-release.sh")

        list(APPEND 3RD_PARTY_OPENSSL_BUILD_OPTIONS "CC=${CMAKE_C_COMPILER}")
        list(APPEND 3RD_PARTY_OPENSSL_BUILD_OPTIONS "CXX=${CMAKE_CXX_COMPILER}")
        if (CMAKE_AR)
            list(APPEND 3RD_PARTY_OPENSSL_BUILD_OPTIONS "AR=${CMAKE_AR}")
        endif ()

        if (CMAKE_C_FLAGS OR CMAKE_C_FLAGS_RELEASE)
            list(APPEND 3RD_PARTY_OPENSSL_BUILD_OPTIONS "CFLAGS=${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_RELEASE}")
        endif ()

        if (CMAKE_CXX_FLAGS OR CMAKE_CXX_FLAGS_RELEASE)
            list(APPEND 3RD_PARTY_OPENSSL_BUILD_OPTIONS "CXXFLAGS=${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_RELEASE}")
        endif ()

        if (CMAKE_ASM_FLAGS OR CMAKE_ASM_FLAGS_RELEASE)
            list(APPEND 3RD_PARTY_OPENSSL_BUILD_OPTIONS "ASFLAGS=${CMAKE_ASM_FLAGS} ${CMAKE_ASM_FLAGS_RELEASE}")
        endif ()

        if (CMAKE_EXE_LINKER_FLAGS)
            list(APPEND 3RD_PARTY_OPENSSL_BUILD_OPTIONS "LDFLAGS=${CMAKE_EXE_LINKER_FLAGS}")
        endif ()

        if (CMAKE_RANLIB)
            list(APPEND 3RD_PARTY_OPENSSL_BUILD_OPTIONS "RANLIB=${CMAKE_RANLIB}")
        endif ()

        if (CMAKE_STATIC_LINKER_FLAGS)
            list(APPEND 3RD_PARTY_OPENSSL_BUILD_OPTIONS "LDFLAGS=${CMAKE_STATIC_LINKER_FLAGS}")
        endif ()

        list(APPEND 3RD_PARTY_OPENSSL_BUILD_OPTIONS)
        project_expand_list_for_command_line_to_file("${3RD_PARTY_OPENSSL_BUILD_DIR}/run-config.sh"
            "../config" ${3RD_PARTY_OPENSSL_BUILD_OPTIONS}
        )
        project_expand_list_for_command_line_to_file("${3RD_PARTY_OPENSSL_BUILD_DIR}/run-build-release.sh"
            "make" ${3RD_PARTY_OPENSSL_BUILD_MULTI_CORE}
        )
        project_expand_list_for_command_line_to_file("${3RD_PARTY_OPENSSL_BUILD_DIR}/run-build-release.sh"
            "make" "install"
        )

        # build & install
        message(STATUS "@${3RD_PARTY_OPENSSL_BUILD_DIR} Run: ./run-config.sh")
        message(STATUS "@${3RD_PARTY_OPENSSL_BUILD_DIR} Run: ./run-build-release.sh")
        execute_process(
            COMMAND "${3RD_PARTY_OPENSSL_BUILD_DIR}/run-config.sh"
            WORKING_DIRECTORY ${3RD_PARTY_OPENSSL_BUILD_DIR}
        )

        execute_process(
            COMMAND "${3RD_PARTY_OPENSSL_BUILD_DIR}/run-build-release.sh"
            WORKING_DIRECTORY ${3RD_PARTY_OPENSSL_BUILD_DIR}
        )

    else ()
        file(WRITE "${3RD_PARTY_OPENSSL_BUILD_DIR}/run-config.bat" "@echo off${PROJECT_THIRD_PARTY_BUILDTOOLS_EOL}")
        file(WRITE "${3RD_PARTY_OPENSSL_BUILD_DIR}/run-build-release.bat" "@echo off${PROJECT_THIRD_PARTY_BUILDTOOLS_EOL}")
        file(APPEND "${3RD_PARTY_OPENSSL_BUILD_DIR}/run-config.bat" "set PATH=${3RD_PARTY_OPENSSL_BUILD_DIR};%PATH%${PROJECT_THIRD_PARTY_BUILDTOOLS_EOL}")
        file(APPEND "${3RD_PARTY_OPENSSL_BUILD_DIR}/run-build-release.bat" "set PATH=${3RD_PARTY_OPENSSL_BUILD_DIR};%PATH%${PROJECT_THIRD_PARTY_BUILDTOOLS_EOL}")
        project_make_executable("${3RD_PARTY_OPENSSL_BUILD_DIR}/run-config.bat")
        project_make_executable("${3RD_PARTY_OPENSSL_BUILD_DIR}/run-build-release.bat")

        list(APPEND 3RD_PARTY_OPENSSL_BUILD_OPTIONS "no-makedepend")
        if(CMAKE_SIZEOF_VOID_P MATCHES 8) 
            list(APPEND 3RD_PARTY_OPENSSL_BUILD_OPTIONS "VC-WIN64A-masm")
        else ()
            list(APPEND 3RD_PARTY_OPENSSL_BUILD_OPTIONS "VC-WIN32" "no-asm")
        endif ()
        
        project_expand_list_for_command_line_to_file("${3RD_PARTY_OPENSSL_BUILD_DIR}/run-config.bat"
            perl "../Configure" ${3RD_PARTY_OPENSSL_BUILD_OPTIONS}
        )
        project_expand_list_for_command_line_to_file("${3RD_PARTY_OPENSSL_BUILD_DIR}/run-build-release.bat"
            "set" "CL=${3RD_PARTY_OPENSSL_BUILD_MULTI_CORE}"
        )
        project_expand_list_for_command_line_to_file("${3RD_PARTY_OPENSSL_BUILD_DIR}/run-build-release.bat"
            "nmake" "build_all_generated" ${3RD_PARTY_OPENSSL_BUILD_MULTI_CORE} "2>&1"
        )
        project_expand_list_for_command_line_to_file("${3RD_PARTY_OPENSSL_BUILD_DIR}/run-build-release.bat"
            "nmake" "PERL=no-perl" ${3RD_PARTY_OPENSSL_BUILD_MULTI_CORE} "2>&1"
        )
        project_expand_list_for_command_line_to_file("${3RD_PARTY_OPENSSL_BUILD_DIR}/run-build-release.bat"
            "nmake" "install" "DESTDIR=${3RD_PARTY_OPENSSL_ROOT_DIR}" "2>&1"
        )

        # build & install
        message(STATUS "@${3RD_PARTY_OPENSSL_BUILD_DIR} Run: ${3RD_PARTY_OPENSSL_BUILD_DIR}/run-config.bat")
        message(STATUS "@${3RD_PARTY_OPENSSL_BUILD_DIR} Run: ${3RD_PARTY_OPENSSL_BUILD_DIR}/run-build-release.bat")
        execute_process(
            COMMAND "${3RD_PARTY_OPENSSL_BUILD_DIR}/run-config.bat"
            WORKING_DIRECTORY ${3RD_PARTY_OPENSSL_BUILD_DIR}
        )

        execute_process(
            COMMAND "${3RD_PARTY_OPENSSL_BUILD_DIR}/run-build-release.bat"
            WORKING_DIRECTORY ${3RD_PARTY_OPENSSL_BUILD_DIR}
        )
    endif ()
    unset(3RD_PARTY_OPENSSL_BUILD_MULTI_CORE)

    find_package(OpenSSL)
    if(OPENSSL_FOUND)
        find_program(3RD_PARTY_OPENSSL_EXECUTABLE NAMES openssl openssl.exe PATHS "${3RD_PARTY_OPENSSL_ROOT_DIR}/bin" "${3RD_PARTY_OPENSSL_ROOT_DIR}" NO_DEFAULT_PATH)
        EchoWithColor(COLOR GREEN "-- Dependency: openssl found.(${OPENSSL_VERSION})")
        if (TARGET OpenSSL::SSL OR TARGET OpenSSL::Crypto)
            if (TARGET OpenSSL::Crypto)
                list(APPEND 3RD_PARTY_CRYPT_LINK_NAME OpenSSL::Crypto)
                list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES OpenSSL::Crypto)
            endif()
            if (TARGET OpenSSL::SSL)
                list(APPEND 3RD_PARTY_CRYPT_LINK_NAME OpenSSL::SSL)
                list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES OpenSSL::SSL)
            endif()
        else()
            if (OPENSSL_INCLUDE_DIR)
                list(APPEND 3RD_PARTY_PUBLIC_INCLUDE_DIRS ${OPENSSL_INCLUDE_DIR})
            endif ()
        
            if (NOT OPENSSL_LIBRARIES)
                set(OPENSSL_LIBRARIES ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY} CACHE INTERNAL "Fix cmake module path for openssl" FORCE)
            endif ()
            if(OPENSSL_LIBRARIES)
                list(APPEND 3RD_PARTY_CRYPT_LINK_NAME ${OPENSSL_LIBRARIES})
                list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES ${OPENSSL_LIBRARIES})
            endif()
        endif ()
    endif()
endif ()

if (3RD_PARTY_OPENSSL_BACKUP_FIND_ROOT)
    set(CMAKE_FIND_ROOT_PATH ${3RD_PARTY_OPENSSL_BACKUP_FIND_ROOT})
    unset(3RD_PARTY_OPENSSL_BACKUP_FIND_ROOT)
endif ()
