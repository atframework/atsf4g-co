# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

set(PROJECT_THIRD_PARTY_XRESLOADER_DUMP_BIN_WINDOWS_URL
    "${PROJECT_GITHUB_GIT_HTTP_MIRROR}xresloader/xresloader-dump-bin/releases/download/v${PROJECT_THIRD_PARTY_XRESLOADER_DUMP_BIN_VERSION}/x86_64-pc-windows-msvc.zip"
)
set(PROJECT_THIRD_PARTY_XRESLOADER_DUMP_BIN_MACOS_URL
    "${PROJECT_GITHUB_GIT_HTTP_MIRROR}xresloader/xresloader-dump-bin/releases/download/v${PROJECT_THIRD_PARTY_XRESLOADER_DUMP_BIN_VERSION}/x86_64-apple-darwin.tar.gz"
)
set(PROJECT_THIRD_PARTY_XRESLOADER_LINUX_URL
    "${PROJECT_GITHUB_GIT_HTTP_MIRROR}xresloader/xresloader-dump-bin/releases/download/v${PROJECT_THIRD_PARTY_XRESLOADER_DUMP_BIN_VERSION}/x86_64-unknown-linux-musl.tar.gz"
)

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
