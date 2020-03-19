
set (PROJECT_3RD_PARTY_CMAKE_INHERIT_VARS 
    CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_RELWITHDEBINFO CMAKE_C_FLAGS_MINSIZEREL
    CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL
    CMAKE_ASM_FLAGS CMAKE_EXE_LINKER_FLAGS CMAKE_MODULE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS CMAKE_STATIC_LINKER_FLAGS
    CMAKE_TOOLCHAIN_FILE CMAKE_C_COMPILER CMAKE_CXX_COMPILER CMAKE_AR CMAKE_C_COMPILER_LAUNCHER CMAKE_CXX_COMPILER_LAUNCHER
    CMAKE_C_COMPILER_LAUNCHER CMAKE_CXX_COMPILER_LAUNCHER CMAKE_RANLIB CMAKE_SYSTEM_NAME PROJECT_ATFRAME_TARGET_CPU_ABI 
    CMAKE_SYSROOT CMAKE_SYSROOT_COMPILE # CMAKE_SYSTEM_LIBRARY_PATH # CMAKE_SYSTEM_LIBRARY_PATH ninja里解出的参数不对，原因未知
    CMAKE_OSX_SYSROOT CMAKE_OSX_ARCHITECTURES 
    ANDROID_TOOLCHAIN ANDROID_ABI ANDROID_STL ANDROID_PIE ANDROID_PLATFORM ANDROID_CPP_FEATURES
    ANDROID_ALLOW_UNDEFINED_SYMBOLS ANDROID_ARM_MODE ANDROID_ARM_NEON ANDROID_DISABLE_NO_EXECUTE ANDROID_DISABLE_RELRO
    ANDROID_DISABLE_FORMAT_STRING_CHECKS ANDROID_CCACHE
)

macro(project_build_cmake_options OUTVAR)
    set (${OUTVAR} "-G" "${CMAKE_GENERATOR}"
        "-DCMAKE_POLICY_DEFAULT_CMP0075=NEW" 
        "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
    )

    foreach (VAR_NAME IN LISTS PROJECT_3RD_PARTY_CMAKE_INHERIT_VARS)
        if (DEFINED ${VAR_NAME})
            set(VAR_VALUE "${${VAR_NAME}}")
            if (VAR_VALUE)
                list (APPEND ${OUTVAR} "-D${VAR_NAME}=${VAR_VALUE}")
            endif ()
            unset(VAR_VALUE)
        endif ()
    endforeach ()

    if (CMAKE_GENERATOR_PLATFORM)
        list (APPEND ${OUTVAR} "-A" "${CMAKE_GENERATOR_PLATFORM}")
    endif ()

    if (CMAKE_BUILD_TYPE)
        if (MSVC)
            list (APPEND ${OUTVAR} "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
        elseif (CMAKE_BUILD_TYPE STREQUAL "Debug")
            list (APPEND ${OUTVAR} "-DCMAKE_BUILD_TYPE=RelWithDebInfo")
        else ()
            list (APPEND ${OUTVAR} "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
        endif ()
    endif ()
endmacro ()

# =========== 3rd_party ===========
# =========== 3rd_party - jemalloc ===========
if(NOT MSVC OR PROJECT_ENABLE_JEMALLOC)
    include("${PROJECT_3RD_PARTY_ROOT_DIR}/jemalloc/jemalloc.cmake")
endif()

# =========== 3rd_party - zlib ===========
# if (NOT WIN32)
    include("${PROJECT_3RD_PARTY_ROOT_DIR}/zlib/zlib.cmake")
# endif()

# =========== 3rd_party - libuv ===========
include("${PROJECT_3RD_PARTY_ROOT_DIR}/libuv/libuv.cmake")

# =========== 3rd_party - rapidjson ===========
include("${PROJECT_3RD_PARTY_ROOT_DIR}/rapidjson/rapidjson.cmake")

# =========== 3rd_party - flatbuffers ===========
include("${PROJECT_3RD_PARTY_ROOT_DIR}/flatbuffers/flatbuffers.cmake")

# =========== 3rd_party - protobuf ===========
include("${PROJECT_3RD_PARTY_ROOT_DIR}/protobuf/protobuf.cmake")

# =========== 3rd_party - crypto ===========
# copy from atframework/libatframe_utils/repo/project/cmake/ProjectBuildOption.cmake
set (OPENSSL_USE_STATIC_LIBS TRUE)
if (CRYPTO_USE_OPENSSL OR CRYPTO_USE_LIBRESSL OR CRYPTO_USE_BORINGSSL)
    if (CRYPTO_USE_OPENSSL)
        include("${PROJECT_3RD_PARTY_ROOT_DIR}/openssl/openssl.cmake")
    elseif (CRYPTO_USE_LIBRESSL)
        include("${PROJECT_3RD_PARTY_ROOT_DIR}/libressl/libressl.cmake")
    else()
        find_package(OpenSSL)
    endif ()
    if (NOT OPENSSL_FOUND)
        message(FATAL_ERROR "CRYPTO_USE_OPENSSL,CRYPTO_USE_LIBRESSL,CRYPTO_USE_BORINGSSL is set but openssl not found")
    endif()
elseif (CRYPTO_USE_MBEDTLS)
    EchoWithColor(COLOR GREEN "-- Try to find and use mbedtls")
    find_package(MbedTLS)
    if (MBEDTLS_FOUND) 
        include_directories(${MbedTLS_INCLUDE_DIRS})
    else()
        message(FATAL_ERROR "CRYPTO_USE_MBEDTLS is set but mbedtls not found")
    endif()
elseif (NOT CRYPTO_DISABLED)
    # try to find openssl or mbedtls
    find_package(OpenSSL)
    if(NOT OPENSSL_FOUND)
        include("${PROJECT_3RD_PARTY_ROOT_DIR}/openssl/openssl.cmake")
    endif ()
    if(NOT OPENSSL_FOUND)
        include("${PROJECT_3RD_PARTY_ROOT_DIR}/libressl/libressl.cmake")
    endif ()
    if (OPENSSL_FOUND)
        message(STATUS "Crypto enabled.(openssl found)")
        set(CRYPTO_USE_OPENSSL 1)
        include_directories(${OPENSSL_INCLUDE_DIR})
    else ()
        EchoWithColor(COLOR GREEN "-- Try to find and use mbedtls")
        find_package(MbedTLS)
        if (MBEDTLS_FOUND) 
            message(STATUS "Crypto enabled.(mbedtls found)")
            set(CRYPTO_USE_MBEDTLS 1)
            include_directories(${MbedTLS_INCLUDE_DIRS})
        endif()
    endif()
endif()

if (NOT OPENSSL_FOUND AND NOT MBEDTLS_FOUND)
    message(FATAL_ERROR "Dependency: must at least have one of openssl,libressl or mbedtls.")
endif()

if (NOT CRYPTO_DISABLED)
    find_package(Libsodium)
    if (Libsodium_FOUND)
        list(APPEND 3RD_PARTY_PUBLIC_LINK_NAMES ${Libsodium_LIBRARIES})
        list(APPEND 3RD_PARTY_CRYPT_LINK_NAME ${Libsodium_LIBRARIES})
    endif()
endif()

# =========== 3rd_party - libcurl ===========
include("${PROJECT_3RD_PARTY_ROOT_DIR}/libcurl/libcurl.cmake")

# =========== 3rd_party - libwebsockets ===========
if (OPENSSL_ROOT_DIR AND OPENSSL_FOUND AND NOT LIBRESSL_FOUND)
    include("${PROJECT_3RD_PARTY_ROOT_DIR}/libwebsockets/libwebsockets.cmake")
endif ()

# =========== 3rd_party - lua ===========
include("${PROJECT_3RD_PARTY_ROOT_DIR}/lua/lua.cmake")

# =========== 3rd_party - libcopp ===========
include("${PROJECT_3RD_PARTY_ROOT_DIR}/libcopp/libcopp.cmake")


