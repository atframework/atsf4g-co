# =========== 3rd_party ===========
if (NOT PROJECT_3RD_PARTY_PACKAGE_DIR)
    set (PROJECT_3RD_PARTY_PACKAGE_DIR "${CMAKE_CURRENT_LIST_DIR}/packages")
endif ()
if (NOT PROJECT_3RD_PARTY_INSTALL_DIR)
    set (PROJECT_3RD_PARTY_INSTALL_DIR "${CMAKE_CURRENT_LIST_DIR}/install/${PROJECT_PREBUILT_PLATFORM_NAME}")
endif ()

if (NOT EXISTS ${PROJECT_3RD_PARTY_PACKAGE_DIR})
    file(MAKE_DIRECTORY ${PROJECT_3RD_PARTY_PACKAGE_DIR})
endif ()

if (NOT EXISTS ${PROJECT_3RD_PARTY_INSTALL_DIR})
    file(MAKE_DIRECTORY ${PROJECT_3RD_PARTY_INSTALL_DIR})
endif ()

# =========== 3rd_party - jemalloc ===========
if(NOT MSVC OR PROJECT_ENABLE_JEMALLOC)
    include("${PROJECT_3RD_PARTY_ROOT_DIR}/jemalloc/jemalloc.cmake")
endif()

# =========== 3rd_party - fmtlib/std::format ===========
include("${PROJECT_3RD_PARTY_ROOT_DIR}/fmtlib/fmtlib.cmake")

# =========== 3rd_party - zlib ===========
include("${PROJECT_3RD_PARTY_ROOT_DIR}/compression/import.cmake")

# =========== 3rd_party - libuv ===========
include("${PROJECT_3RD_PARTY_ROOT_DIR}/libuv/libuv.cmake")

# =========== 3rd_party - libunwind ===========
if (UNIX AND NOT CYGWIN)
    include("${PROJECT_3RD_PARTY_ROOT_DIR}/libunwind/libunwind.cmake")
endif ()

# =========== 3rd_party - rapidjson ===========
include("${PROJECT_3RD_PARTY_ROOT_DIR}/rapidjson/rapidjson.cmake")

# =========== 3rd_party - flatbuffers ===========
include("${PROJECT_3RD_PARTY_ROOT_DIR}/flatbuffers/flatbuffers.cmake")

# =========== 3rd_party - protobuf ===========
include("${PROJECT_3RD_PARTY_ROOT_DIR}/protobuf/protobuf.cmake")

# =========== 3rd_party - crypto ===========
include("${PROJECT_3RD_PARTY_ROOT_DIR}/ssl/port.cmake")

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

# =========== 3rd_party - grpc ===========
# Must be imported after ssl,protobuf,zlib
include("${PROJECT_3RD_PARTY_ROOT_DIR}/grpc/import.cmake")

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

# Copy executable/dll/.so files
file(GLOB 3RD_PARTY_JEMALLOC_ALL_DYNAMIC_LIB_FILES  
    "${PROJECT_3RD_PARTY_INSTALL_DIR}/lib/*.so*"
    "${PROJECT_3RD_PARTY_INSTALL_DIR}/lib/*.dll*"
    "${PROJECT_3RD_PARTY_INSTALL_DIR}/lib64/*.so*"
    "${PROJECT_3RD_PARTY_INSTALL_DIR}/lib64/*.dll*"
)
project_copy_shared_lib(${3RD_PARTY_JEMALLOC_ALL_DYNAMIC_LIB_FILES} ${PROJECT_INSTALL_SHARED_DIR})

if (NOT EXISTS "${PROJECT_INSTALL_BAS_DIR}/bin")
    file(MAKE_DIRECTORY "${PROJECT_INSTALL_BAS_DIR}/bin")
endif ()
file(GLOB 3RD_PARTY_JEMALLOC_ALL_DYNAMIC_BIN_FILES  
    "${PROJECT_3RD_PARTY_INSTALL_DIR}/bin/*.dll"
    ${3RD_PARTY_COPY_EXECUTABLE_PATTERN}
)

if (3RD_PARTY_JEMALLOC_ALL_DYNAMIC_BIN_FILES)
    list(REMOVE_DUPLICATES 3RD_PARTY_JEMALLOC_ALL_DYNAMIC_BIN_FILES)
    project_copy_shared_lib(${3RD_PARTY_JEMALLOC_ALL_DYNAMIC_BIN_FILES} "${PROJECT_INSTALL_BAS_DIR}/bin")
endif()
