# =========== 3rd_party ===========
# =========== 3rd_party - jemalloc ===========
if(NOT MSVC OR PROJECT_ENABLE_JEMALLOC)
    include("${PROJECT_3RD_PARTY_ROOT_DIR}/jemalloc/jemalloc.cmake")
endif()

# =========== 3rd_party - fmtlib/std::format ===========
include("${PROJECT_3RD_PARTY_ROOT_DIR}/fmtlib/fmtlib.cmake")

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
    include("${PROJECT_3RD_PARTY_ROOT_DIR}/mbedtls/mbedtls.cmake")
    if (NOT 3RD_PARTY_MBEDTLS_FOUND) 
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
    else ()
        include("${PROJECT_3RD_PARTY_ROOT_DIR}/mbedtls/mbedtls.cmake")
        if (3RD_PARTY_MBEDTLS_FOUND) 
            message(STATUS "Crypto enabled.(mbedtls found)")
            set(CRYPTO_USE_MBEDTLS 1)
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
if (OPENSSL_FOUND AND NOT LIBRESSL_FOUND)
    include("${PROJECT_3RD_PARTY_ROOT_DIR}/libwebsockets/libwebsockets.cmake")
endif ()

# =========== 3rd_party - lua ===========
include("${PROJECT_3RD_PARTY_ROOT_DIR}/lua/lua.cmake")

# =========== 3rd_party - yaml-cpp ===========
include("${PROJECT_3RD_PARTY_ROOT_DIR}/yaml-cpp/yaml-cpp.cmake")

# =========== 3rd_party - libcopp ===========
include("${PROJECT_3RD_PARTY_ROOT_DIR}/libcopp/libcopp.cmake")

# =========== 3rd_party - python_env ===========
include("${PROJECT_3RD_PARTY_ROOT_DIR}/python_env/python_env.cmake")

# =========== 3rd_party - xres-code-generator ===========
include("${PROJECT_3RD_PARTY_ROOT_DIR}/xres-code-generator/xres-code-generator.cmake")

if (3RD_PARTY_PUBLIC_LINK_NAMES)
    list(REMOVE_DUPLICATES 3RD_PARTY_PUBLIC_LINK_NAMES)
    list(REVERSE 3RD_PARTY_PUBLIC_LINK_NAMES)
endif()
if (3RD_PARTY_INTERFACE_LINK_NAMES)
    list(REMOVE_DUPLICATES 3RD_PARTY_INTERFACE_LINK_NAMES)
    list(REVERSE 3RD_PARTY_INTERFACE_LINK_NAMES)
endif ()
