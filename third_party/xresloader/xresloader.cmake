set(PROJECT_THIRD_PARTY_XRESLOADER_VERSION "2.19.0")
set(PROJECT_THIRD_PARTY_XRESLOADER_DUMP_BIN_VERSION "2.5.0")

set(PROJECT_THIRD_PARTY_XRESLOADER_JAR_URL
    "${PROJECT_GITHUB_GIT_HTTP_MIRROR}xresloader/xresloader/releases/download/v${PROJECT_THIRD_PARTY_XRESLOADER_VERSION}/xresloader-${PROJECT_THIRD_PARTY_XRESLOADER_VERSION}.jar"
)
set(PROJECT_THIRD_PARTY_XRESLOADER_PROTOCOL_URL
    "${PROJECT_GITHUB_GIT_HTTP_MIRROR}xresloader/xresloader/releases/download/v${PROJECT_THIRD_PARTY_XRESLOADER_VERSION}/protocols.zip"
)
set(PROJECT_THIRD_PARTY_XRESLOADER_DUMP_BIN_WINDOWS_URL
    "${PROJECT_GITHUB_GIT_HTTP_MIRROR}xresloader/xresloader-dump-bin/releases/download/v${PROJECT_THIRD_PARTY_XRESLOADER_DUMP_BIN_VERSION}/x86_64-pc-windows-msvc.zip"
)
set(PROJECT_THIRD_PARTY_XRESLOADER_DUMP_BIN_MACOS_URL
    "${PROJECT_GITHUB_GIT_HTTP_MIRROR}xresloader/xresloader-dump-bin/releases/download/v${PROJECT_THIRD_PARTY_XRESLOADER_DUMP_BIN_VERSION}/x86_64-apple-darwin.tar.gz"
)
set(PROJECT_THIRD_PARTY_XRESLOADER_LINUX_URL
    "${PROJECT_GITHUB_GIT_HTTP_MIRROR}xresloader/xresloader-dump-bin/releases/download/v${PROJECT_THIRD_PARTY_XRESLOADER_DUMP_BIN_VERSION}/x86_64-unknown-linux-musl.tar.gz"
)

set(PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/package")
set(PROJECT_THIRD_PARTY_XRESLOADER_JAR
    "${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}/bin/xresloader-${PROJECT_THIRD_PARTY_XRESLOADER_VERSION}.jar")
set(PROJECT_THIRD_PARTY_XRESLOADER_PROTO_DIR "${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}/protocols")
set(PROJECT_THIRD_PARTY_XRESLOADER_EXCEL_DIR "${PROJECT_SOURCE_DIR}/resource/excel")

set(PROJECT_THIRD_PARTY_XRESLOADER_CLI "${CMAKE_CURRENT_LIST_DIR}/xresconv-cli/xresconv-cli.py")

if(NOT EXISTS ${PROJECT_THIRD_PARTY_XRESLOADER_CLI})
  project_git_clone_repository(
    URL
    "${PROJECT_GITHUB_GIT_HTTP_MIRROR}xresloader/xresconv-cli.git"
    REPO_DIRECTORY
    "${CMAKE_CURRENT_LIST_DIR}/xresconv-cli"
    DEPTH
    100
    BRANCH
    main
    WORKING_DIRECTORY
    ${PROJECT_THIRD_PARTY_PACKAGE_DIR}
    CHECK_PATH
    "xresconv-cli.py")
endif()

#[[
+ 文档: https://xresloader.atframe.work
+ 主仓库和简要功能说明: https://github.com/xresloader/xresloader
]]

if(EXISTS "${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}")
  if(NOT EXISTS "${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}/xresloader.version")
    file(REMOVE_RECURSE "${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}")
  else()
    file(READ "${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}/xresloader.version"
         PROJECT_THIRD_PARTY_XRESLOADER_OLD_VERSION)
    string(STRIP "${PROJECT_THIRD_PARTY_XRESLOADER_OLD_VERSION}" PROJECT_THIRD_PARTY_XRESLOADER_OLD_VERSION)
    if(NOT PROJECT_THIRD_PARTY_XRESLOADER_OLD_VERSION STREQUAL PROJECT_THIRD_PARTY_XRESLOADER_VERSION)
      file(REMOVE_RECURSE "${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}")
    endif()
  endif()
endif()

file(MAKE_DIRECTORY "${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}")
if(NOT EXISTS "${PROJECT_THIRD_PARTY_XRESLOADER_JAR}")
  get_filename_component(PROJECT_THIRD_PARTY_XRESLOADER_JAR_DIR "${PROJECT_THIRD_PARTY_XRESLOADER_JAR}" DIRECTORY)
  file(MAKE_DIRECTORY "${PROJECT_THIRD_PARTY_XRESLOADER_JAR_DIR}")
  findconfigurepackagedownloadfile("${PROJECT_THIRD_PARTY_XRESLOADER_JAR_URL}" "${PROJECT_THIRD_PARTY_XRESLOADER_JAR}")
endif()

if(NOT EXISTS "${PROJECT_THIRD_PARTY_XRESLOADER_PROTO_DIR}/extensions/v3/xresloader.proto")
  file(MAKE_DIRECTORY "${PROJECT_THIRD_PARTY_XRESLOADER_PROTO_DIR}")
  findconfigurepackagedownloadfile("${PROJECT_THIRD_PARTY_XRESLOADER_PROTOCOL_URL}"
                                   "${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}/protocols.zip")
  findconfigurepackageunzip("${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}/protocols.zip"
                            "${PROJECT_THIRD_PARTY_XRESLOADER_PROTO_DIR}")
endif()
if(EXISTS "${PROJECT_THIRD_PARTY_XRESLOADER_JAR}"
   AND EXISTS "${PROJECT_THIRD_PARTY_XRESLOADER_PROTO_DIR}/extensions/v3/xresloader.proto")
  file(WRITE "${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}/xresloader.version"
       "${PROJECT_THIRD_PARTY_XRESLOADER_VERSION}")
endif()

# xresloader-dump-bin
if(EXISTS "${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}")
  if(NOT EXISTS "${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}/xresloader-dump-bin.version")
    file(REMOVE_RECURSE "${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}/xresloader-dump-bin")
  else()
    file(READ "${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}/xresloader-dump-bin.version"
         PROJECT_THIRD_PARTY_XRESLOADER_DUMP_BIN_OLD_VERSION)
    string(STRIP "${PROJECT_THIRD_PARTY_XRESLOADER_DUMP_BIN_OLD_VERSION}"
                 PROJECT_THIRD_PARTY_XRESLOADER_DUMP_BIN_OLD_VERSION)
    if(NOT PROJECT_THIRD_PARTY_XRESLOADER_DUMP_BIN_OLD_VERSION STREQUAL PROJECT_THIRD_PARTY_XRESLOADER_DUMP_BIN_VERSION)
      file(REMOVE_RECURSE "${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}/xresloader-dump-bin")
    endif()
  endif()
endif()

if(NOT EXISTS "${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}/xresloader-dump-bin/win64/bin/xresloader-dump-bin.exe")
  file(MAKE_DIRECTORY "${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}/xresloader-dump-bin/win64")
  findconfigurepackagedownloadfile(
    "${PROJECT_THIRD_PARTY_XRESLOADER_DUMP_BIN_WINDOWS_URL}"
    "${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}/xresloader-dump-bin/x86_64-pc-windows-msvc.zip")
  findconfigurepackageunzip("${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}/xresloader-dump-bin/x86_64-pc-windows-msvc.zip"
                            "${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}/xresloader-dump-bin/win64")
endif()

if(NOT EXISTS "${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}/xresloader-dump-bin/macos/bin/xresloader-dump-bin")
  file(MAKE_DIRECTORY "${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}/xresloader-dump-bin/macos")
  findconfigurepackagedownloadfile(
    "${PROJECT_THIRD_PARTY_XRESLOADER_DUMP_BIN_MACOS_URL}"
    "${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}/xresloader-dump-bin/x86_64-apple-darwin.tar.gz")
  findconfigurepackagetarxv("${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}/xresloader-dump-bin/x86_64-apple-darwin.tar.gz"
                            "${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}/xresloader-dump-bin/macos")
endif()

if(NOT EXISTS "${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}/xresloader-dump-bin/linux/bin/xresloader-dump-bin")
  file(MAKE_DIRECTORY "${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}/xresloader-dump-bin/linux")
  findconfigurepackagedownloadfile(
    "${PROJECT_THIRD_PARTY_XRESLOADER_LINUX_URL}"
    "${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}/xresloader-dump-bin/x86_64-unknown-linux-musl.tar.gz")
  findconfigurepackagetarxv(
    "${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}/xresloader-dump-bin/x86_64-unknown-linux-musl.tar.gz"
    "${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}/xresloader-dump-bin/linux")
endif()

if(EXISTS "${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}/xresloader-dump-bin/win64/bin/xresloader-dump-bin.exe"
   AND EXISTS "${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}/xresloader-dump-bin/macos/bin/xresloader-dump-bin"
   AND EXISTS "${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}/xresloader-dump-bin/linux/bin/xresloader-dump-bin")
  file(WRITE "${PROJECT_THIRD_PARTY_XRESLOADER_ROOT_DIR}/xresloader-dump-bin.version"
       "${PROJECT_THIRD_PARTY_XRESLOADER_DUMP_BIN_VERSION}")
endif()
