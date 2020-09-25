# This is grpc, an asynchronous resolver library.
# https://github.com/grpc/grpc.git
# git@github.com:grpc/grpc.git
# https://grpc.io/

if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.10")
    include_guard(GLOBAL)
endif()

# =========== 3rdparty grpc ==================
if (NOT TARGET gRPC::grpc++_alts AND NOT TARGET gRPC::grpc++ AND NOT TARGET gRPC::grpc)
    macro(PROJECT_3RD_PARTY_GRPC_IMPORT)
        if (TARGET gRPC::grpc++_alts)
            message(STATUS "grpc using target: gRPC::grpc++_alts (version: ${gRPC_VERSION})")
            set (3RD_PARTY_GRPC_LINK_NAME gRPC::grpc++_alts)
        elseif (TARGET gRPC::grpc++)
            message(STATUS "grpc using target: gRPC::grpc++ (version: ${gRPC_VERSION})")
            set (3RD_PARTY_GRPC_LINK_NAME gRPC::grpc++)
        elseif (TARGET gRPC::grpc)
            message(STATUS "grpc using target: gRPC::grpc (version: ${gRPC_VERSION})")
            set (3RD_PARTY_GRPC_LINK_NAME gRPC::grpc)
        endif()
    endmacro()

    if (VCPKG_TOOLCHAIN)
        find_package(gRPC QUIET)
        PROJECT_3RD_PARTY_GRPC_IMPORT()
    endif ()

    set (3RD_PARTY_GRPC_DEFAULT_VERSION "v1.32.0")

    set(3RD_PARTY_GRPC_BACKUP_FIND_ROOT ${CMAKE_FIND_ROOT_PATH})
    list(APPEND CMAKE_FIND_ROOT_PATH ${PROJECT_3RD_PARTY_INSTALL_DIR})
    set(grpc_ROOT ${PROJECT_3RD_PARTY_INSTALL_DIR})
    FindConfigurePackage(
        PACKAGE gRPC
        BUILD_WITH_CMAKE CMAKE_INHIRT_BUILD_ENV CMAKE_INHIRT_BUILD_ENV_DISABLE_CXX_FLAGS
        CMAKE_FLAGS "-DCMAKE_POSITION_INDEPENDENT_CODE=YES" # "-DBUILD_SHARED_LIBS=OFF"
                    "-DgRPC_INSTALL=ON" "-DCMAKE_BUILD_TYPE=Release" "-DgRPC_ABSL_PROVIDER=package" "-DgRPC_CARES_PROVIDER=package"
                    "-DgRPC_PROTOBUF_PROVIDER=package" "-DgRPC_PROTOBUF_PACKAGE_TYPE=CONFIG" "-DgRPC_RE2_PROVIDER=package" 
                    "-DgRPC_SSL_PROVIDER=package" "-DgRPC_ZLIB_PROVIDER=package" "-DgRPC_GFLAGS_PROVIDER=none" 
                    "-DgRPC_BENCHMARK_PROVIDER=none" "-DgRPC_BUILD_TESTS=OFF" "-DgRPC_BUILD_CODEGEN=${gRPC_BUILD_CODEGEN}"
                    "-DCMAKE_CROSSCOMPILING:BOOL=${gRPC_CMAKE_CROSSCOMPILING}"
        MSVC_CONFIGURE ${gRPC_MSVC_CONFIGURE}
        WORKING_DIRECTORY "${PROJECT_3RD_PARTY_PACKAGE_DIR}"
        BUILD_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/deps/grpc-${3RD_PARTY_GRPC_DEFAULT_VERSION}/build_jobs_${PROJECT_PREBUILT_PLATFORM_NAME}"
        PREFIX_DIRECTORY "${PROJECT_3RD_PARTY_INSTALL_DIR}"
        SRC_DIRECTORY_NAME "grpc-${3RD_PARTY_GRPC_DEFAULT_VERSION}"
        GIT_BRANCH "${3RD_PARTY_GRPC_DEFAULT_VERSION}"
        GIT_URL "https://github.com/grpc/grpc.git"
    )

    if (TARGET gRPC::grpc++_alts OR TARGET gRPC::grpc++ OR TARGET gRPC::grpc)
        PROJECT_3RD_PARTY_GRPC_IMPORT()
    else ()
        message(FATAL_ERROR "grpc build failed.")
    endif()
endif ()

if (3RD_PARTY_GRPC_BACKUP_FIND_ROOT)
    set(CMAKE_FIND_ROOT_PATH ${3RD_PARTY_GRPC_BACKUP_FIND_ROOT})
    unset(3RD_PARTY_GRPC_BACKUP_FIND_ROOT)
endif ()

