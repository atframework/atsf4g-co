set(PROJECT_THIRD_PARTY_OTELCOL_VERSION "0.89.0")

set(PROJECT_THIRD_PARTY_OTELCOL_LINUX_URL
    "https://github.com/open-telemetry/opentelemetry-collector-releases/releases/download/v${PROJECT_THIRD_PARTY_OTELCOL_VERSION}/otelcol-contrib_${PROJECT_THIRD_PARTY_OTELCOL_VERSION}_linux_amd64.tar.gz"
)
set(PROJECT_THIRD_PARTY_OTELCOL_WINDOWS_URL
    "https://github.com/open-telemetry/opentelemetry-collector-releases/releases/download/v${PROJECT_THIRD_PARTY_OTELCOL_VERSION}/otelcol-contrib_${PROJECT_THIRD_PARTY_OTELCOL_VERSION}_windows_amd64.tar.gz"
)
set(PROJECT_THIRD_PARTY_OTELCOL_MACOS_URL
    "https://github.com/open-telemetry/opentelemetry-collector-releases/releases/download/v${PROJECT_THIRD_PARTY_OTELCOL_VERSION}/otelcol-contrib_${PROJECT_THIRD_PARTY_OTELCOL_VERSION}_darwin_amd64.tar.gz"
)

set(PROJECT_THIRD_PARTY_OTELCOL_ROOT_DIR
    "${PROJECT_THIRD_PARTY_PACKAGE_DIR}/otelcol-${PROJECT_THIRD_PARTY_OTELCOL_VERSION}")

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
  set(PROJECT_THIRD_PARTY_OTELCOL_DOWNLOAD_URL "${PROJECT_THIRD_PARTY_OTELCOL_WINDOWS_URL}")
  set(PROJECT_THIRD_PARTY_OTELCOL_EXECUTABLE_PATH "${PROJECT_THIRD_PARTY_OTELCOL_ROOT_DIR}/bin/otelcol-contrib.exe")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
  set(PROJECT_THIRD_PARTY_OTELCOL_DOWNLOAD_URL "${PROJECT_THIRD_PARTY_OTELCOL_MACOS_URL}")
  set(PROJECT_THIRD_PARTY_OTELCOL_EXECUTABLE_PATH "${PROJECT_THIRD_PARTY_OTELCOL_ROOT_DIR}/bin/otelcol-contrib")
else()
  set(PROJECT_THIRD_PARTY_OTELCOL_DOWNLOAD_URL "${PROJECT_THIRD_PARTY_OTELCOL_LINUX_URL}")
  set(PROJECT_THIRD_PARTY_OTELCOL_EXECUTABLE_PATH "${PROJECT_THIRD_PARTY_OTELCOL_ROOT_DIR}/bin/otelcol-contrib")
endif()

if(EXISTS "${PROJECT_THIRD_PARTY_OTELCOL_ROOT_DIR}")
  if(NOT EXISTS "${PROJECT_THIRD_PARTY_OTELCOL_ROOT_DIR}/otelcol.version")
    file(REMOVE_RECURSE "${PROJECT_THIRD_PARTY_OTELCOL_ROOT_DIR}")
  else()
    file(READ "${PROJECT_THIRD_PARTY_OTELCOL_ROOT_DIR}/otelcol.version" PROJECT_THIRD_PARTY_OTELCOL_OLD_VERSION)
    string(STRIP "${PROJECT_THIRD_PARTY_OTELCOL_OLD_VERSION}" PROJECT_THIRD_PARTY_OTELCOL_OLD_VERSION)
    if(NOT PROJECT_THIRD_PARTY_OTELCOL_OLD_VERSION STREQUAL PROJECT_THIRD_PARTY_OTELCOL_VERSION)
      file(REMOVE_RECURSE "${PROJECT_THIRD_PARTY_OTELCOL_ROOT_DIR}")
    endif()
  endif()
endif()

if(NOT EXISTS "${PROJECT_THIRD_PARTY_OTELCOL_EXECUTABLE_PATH}")
  file(MAKE_DIRECTORY "${PROJECT_THIRD_PARTY_OTELCOL_ROOT_DIR}/bin")
  file(MAKE_DIRECTORY "${PROJECT_THIRD_PARTY_OTELCOL_ROOT_DIR}/etc")
  get_filename_component(PROJECT_THIRD_PARTY_OTELCOL_DOWNLOAD_BASENAME "${PROJECT_THIRD_PARTY_OTELCOL_DOWNLOAD_URL}"
                         NAME)

  findconfigurepackagedownloadfile(
    "${PROJECT_THIRD_PARTY_OTELCOL_DOWNLOAD_URL}"
    "${PROJECT_THIRD_PARTY_OTELCOL_ROOT_DIR}/${PROJECT_THIRD_PARTY_OTELCOL_DOWNLOAD_BASENAME}")
  findconfigurepackagetarxv("${PROJECT_THIRD_PARTY_OTELCOL_ROOT_DIR}/${PROJECT_THIRD_PARTY_OTELCOL_DOWNLOAD_BASENAME}"
                            "${PROJECT_THIRD_PARTY_OTELCOL_ROOT_DIR}/bin")
endif()
if(EXISTS "${PROJECT_THIRD_PARTY_OTELCOL_EXECUTABLE_PATH}")
  file(WRITE "${PROJECT_THIRD_PARTY_OTELCOL_ROOT_DIR}/otelcol.version" "${PROJECT_THIRD_PARTY_OTELCOL_VERSION}")
endif()

file(GLOB PROJECT_THIRD_PARTY_OTELCOL_BIN_FILES "${PROJECT_THIRD_PARTY_OTELCOL_ROOT_DIR}/bin/*")
