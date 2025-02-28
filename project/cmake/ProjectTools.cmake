set(LOG_WRAPPER_CATEGORIZE_SIZE
    16
    CACHE STRING "全局日志分类个数限制")
add_compiler_define(LOG_WRAPPER_CATEGORIZE_SIZE=${LOG_WRAPPER_CATEGORIZE_SIZE})

set(PROJECT_INSTALL_EXPORT_NAME "${PROJECT_NAME}-target")
set(PROJECT_INSTALL_EXPORT_FILE
    "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/${PROJECT_NAME}/${PROJECT_INSTALL_EXPORT_NAME}.cmake")
if(NOT EXISTS "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/${PROJECT_NAME}")
  file(MAKE_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/${PROJECT_NAME}")
endif()
macro(project_install_and_export_targets)
  foreach(PROJECT_INSTALL_EXPORT_TARGET ${ARGN})
    install(
      TARGETS ${PROJECT_INSTALL_EXPORT_TARGET}
      EXPORT ${PROJECT_INSTALL_EXPORT_NAME}
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
      LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}/${CMAKE_INSTALL_LIBDIR}"
      ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}/${CMAKE_INSTALL_LIBDIR}")
  endforeach()
  unset(PROJECT_INSTALL_EXPORT_TARGET)
endmacro()

unset(PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS)
unset(PROJECT_COMMON_PRIVATE_LINK_OPTIONS)
unset(PROJECT_RUNTIME_POST_BUILD_DYNAMIC_LIBRARY_BASH)
unset(PROJECT_RUNTIME_POST_BUILD_DYNAMIC_LIBRARY_PWSH)
unset(PROJECT_RUNTIME_POST_BUILD_STATIC_LIBRARY_BASH)
unset(PROJECT_RUNTIME_POST_BUILD_STATIC_LIBRARY_PWSH)
unset(PROJECT_RUNTIME_POST_BUILD_EXECUTABLE_LIBRARY_BASH)
unset(PROJECT_RUNTIME_POST_BUILD_EXECUTABLE_LIBRARY_PWSH)
if(COMPILER_STRICT_CFLAGS)
  list(APPEND PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS ${COMPILER_STRICT_CFLAGS})
endif()
if(COMPILER_STRICT_EXTRA_CFLAGS)
  list(APPEND PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS ${COMPILER_STRICT_EXTRA_CFLAGS})
endif()
if(COMPILER_STRICT_RECOMMEND_EXTRA_CFLAGS)
  list(APPEND PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS ${COMPILER_STRICT_RECOMMEND_EXTRA_CFLAGS})
endif()
if(COMPILER_STRICT_RECOMMEND_REMOVE_CFLAGS)
  list(REMOVE_ITEM PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS ${COMPILER_STRICT_RECOMMEND_REMOVE_CFLAGS})
endif()
if((PROJECT_OPTIMIZE_OPTIONS_NO_OMIT_FRAME_POINTER OR PROJECT_WITH_SANTIZER_NAME)
   AND ATFRAMEWORK_CMAKE_TOOLSET_TARGET_IS_LINUX
   AND NOT "-fno-omit-frame-pointer" IN_LIST PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS)
  list(APPEND PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS -fno-omit-frame-pointer)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  if(NOT "-Wconversion" IN_LIST PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS)
    list(APPEND PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS -Wconversion)
  endif()
  add_compiler_flags_to_inherit_var_unique(CMAKE_CXX_FLAGS "-pipe")
  add_compiler_flags_to_inherit_var_unique(CMAKE_C_FLAGS "-pipe")
endif()

if(${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU")
  add_compiler_flags_to_inherit_var(CMAKE_CXX_FLAGS "-Wframe-larger-than=131072")
  if(PROJECT_ENABLE_SPLIT_DEBUG_INFORMATION
     AND NOT CMAKE_C_COMPILER_LAUNCHER
     AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    list(APPEND PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS "$<$<CONFIG:Debug,RelWithDebInfo>:-gsplit-dwarf>")
  endif()
elseif(${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
  list(APPEND PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS -Wno-unused-private-field)
  if(PROJECT_ENABLE_SPLIT_DEBUG_INFORMATION
     AND NOT CMAKE_C_COMPILER_LAUNCHER
     AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    list(APPEND PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS "$<$<CONFIG:Debug,RelWithDebInfo>:-gsplit-dwarf=split>")
  endif()
  if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "15.0")
    add_compiler_flags_to_inherit_var_unique(CMAKE_CXX_FLAGS "-Wno-gnu-line-marker")
    add_compiler_flags_to_inherit_var_unique(CMAKE_C_FLAGS "-Wno-gnu-line-marker")
  endif()
elseif(${CMAKE_CXX_COMPILER_ID} STREQUAL "AppleClang")
  list(APPEND PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS -Wno-unused-private-field)
  if(PROJECT_ENABLE_SPLIT_DEBUG_INFORMATION
     AND NOT CMAKE_C_COMPILER_LAUNCHER
     AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    list(APPEND PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS "$<$<CONFIG:Debug,RelWithDebInfo>:-gsplit-dwarf=split>")
  endif()
endif()

if(PROJECT_ENABLE_COMPRESS_DEBUG_INFORMATION AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  check_cxx_compiler_flag("-gz=zlib" PROJECT_COMPILER_CHECK_DEBUG_GZ_ZLIB)
  if(PROJECT_COMPILER_CHECK_DEBUG_GZ_ZLIB)
    list(APPEND PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS "$<$<CONFIG:Debug,RelWithDebInfo>:-gz=zlib>")
    list(APPEND PROJECT_COMMON_PRIVATE_LINK_OPTIONS "$<$<CONFIG:Debug,RelWithDebInfo>:-gz=zlib>")
  else()
    check_cxx_compiler_flag("-gz" PROJECT_COMPILER_CHECK_DEBUG_GZ)
    if(PROJECT_COMPILER_CHECK_DEBUG_GZ)
      list(APPEND PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS "$<$<CONFIG:Debug,RelWithDebInfo>:-gz>")
      list(APPEND PROJECT_COMMON_PRIVATE_LINK_OPTIONS "$<$<CONFIG:Debug,RelWithDebInfo>:-gz>")
    endif()
  endif()

  # Check if we support --compress-debug-sections=zlib
  if(NOT PROJECT_COMPILER_CHECK_DEBUG_GZ_ZLIB AND NOT PROJECT_COMPILER_CHECK_DEBUG_GZ)
    set(PROJECT_LINKER_CHECK_TEST_BAKCUP_CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_LINK_OPTIONS}")
    list(APPEND CMAKE_REQUIRED_LINK_OPTIONS "-Wl,--compress-debug-sections=zlib")
    check_cxx_source_compiles(
      "#include <iostream>
      int main() {
        std::cout<< __cplusplus<< std::endl;
        return 0;
      }
      "
      PROJECT_LINKER_CHECK_COMPRESS_DEBUG_SECTIONS_ZLIB)
    if(PROJECT_LINKER_CHECK_TEST_BAKCUP_CMAKE_REQUIRED_FLAGS)
      set(CMAKE_REQUIRED_LINK_OPTIONS "${PROJECT_LINKER_CHECK_TEST_BAKCUP_CMAKE_REQUIRED_FLAGS}")
    else()
      unset(CMAKE_REQUIRED_LINK_OPTIONS)
    endif()
    unset(PROJECT_LINKER_CHECK_TEST_BAKCUP_CMAKE_REQUIRED_FLAGS)
    if(PROJECT_LINKER_CHECK_COMPRESS_DEBUG_SECTIONS_ZLIB)
      list(APPEND PROJECT_COMMON_PRIVATE_LINK_OPTIONS "-Wl,--compress-debug-sections=zlib")
    endif()

  endif()
endif()

set(PROJECT_COMMON_PROTOCOL_SOURCE_COMPILE_OPTIONS ${PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS}
                                                   ${PROJECT_BUILD_TOOLS_PATCH_PROTOBUF_SOURCES_OPTIONS})
if(PROJECT_BUILD_TOOLS_PATCH_PROTOBUF_SOURCES_REMOVE_OPTIONS)
  list(REMOVE_ITEM PROJECT_COMMON_PROTOCOL_SOURCE_COMPILE_OPTIONS
       ${PROJECT_BUILD_TOOLS_PATCH_PROTOBUF_SOURCES_REMOVE_OPTIONS})
endif()

# Try to use static libs for gcc
if(PROJECT_STATIC_LINK_STANDARD_LIBRARIES AND ${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU")
  include(CheckCXXSourceCompiles)
  set(CMAKE_REQUIRED_LIBRARIES ${COMPILER_OPTION_EXTERN_CXX_LIBS})
  unset(PROJECT_LINK_LIBS_TEST_STATIC_LIBGCC CACHE)
  # 测试使用静态libgcc库
  set(CMAKE_REQUIRED_LINK_OPTIONS "-static-libgcc")
  check_cxx_source_compiles("#include<iostream>
    int main() { std::cout<< 1<< std::endl; return 0; }" PROJECT_LINK_LIBS_TEST_STATIC_LIBGCC)
  if(PROJECT_LINK_LIBS_TEST_STATIC_LIBGCC)
    add_linker_flags_for_runtime("-static-libgcc")
    set(PROJECT_LINK_STATIC_LIBS_LIBGCC ON)
    message(STATUS "Using static libgcc")
  else()
    message(STATUS "Using dynamic libgcc")
  endif()
  # 测试使用静态libstdc++库
  set(CMAKE_REQUIRED_LINK_OPTIONS "-static-libstdc++")
  unset(PROJECT_LINK_LIBS_TEST_STATIC_LIBSTDCXX CACHE)
  check_cxx_source_compiles("#include<iostream>
    int main() { std::cout<< 1<< std::endl; return 0; }" PROJECT_LINK_LIBS_TEST_STATIC_LIBSTDCXX)
  if(PROJECT_LINK_LIBS_TEST_STATIC_LIBSTDCXX)
    add_linker_flags_for_runtime("-static-libstdc++")
    set(PROJECT_LINK_STATIC_LIBS_LIBSTDCXX ON)
    message(STATUS "Using static libstdc++")
    list(APPEND COMPILER_OPTION_EXTERN_CXX_LIBS stdc++)
  else()
    message(STATUS "Using dynamic libstdc++")
  endif()

  unset(CMAKE_REQUIRED_LIBRARIES)
  unset(CMAKE_REQUIRED_LINK_OPTIONS)
  unset(PROJECT_LINK_LIBS_TEST_STATIC_LIBGCC CACHE)
  unset(PROJECT_LINK_LIBS_TEST_STATIC_LIBSTDCXX CACHE)
endif()

function(project_link_or_copy_files)
  set(FILE_LIST ${ARGN})
  list(POP_BACK FILE_LIST DESTINATION)
  if(NOT EXISTS "${DESTINATION}")
    file(MAKE_DIRECTORY "${DESTINATION}")
  endif()
  foreach(FILE_PATH IN LISTS FILE_LIST)
    if(IS_SYMLINK "${FILE_PATH}")
      get_filename_component(FILE_BASENAME "${FILE_PATH}" NAME)
      file(READ_SYMLINK "${FILE_PATH}" FILE_REALPATH)
      if(EXISTS "${DESTINATION}/${FILE_BASENAME}")
        file(REMOVE "${DESTINATION}/${FILE_BASENAME}")
      endif()
      file(
        CREATE_LINK "${FILE_REALPATH}" "${DESTINATION}/${FILE_BASENAME}"
        RESULT CREATE_LINK_RESULT
        COPY_ON_ERROR SYMBOLIC)
      if(NOT CREATE_LINK_RESULT EQUAL 0)
        echowithcolor(COLOR GREEN
                      "-- Try to link ${FILE_PATH} to ${DESTINATION}/${FILE_BASENAME} failed: ${CREATE_LINK_RESULT}")
      endif()
    elseif(IS_DIRECTORY "${FILE_PATH}")
      get_filename_component(FILE_BASENAME "${FILE_PATH}" NAME)
      file(
        GLOB FILES_IN_SUBDIRECTORY
        LIST_DIRECTORIES TRUE
        "${FILE_PATH}/*")
      if(FILES_IN_SUBDIRECTORY)
        project_link_or_copy_files(${FILES_IN_SUBDIRECTORY} "${DESTINATION}/${FILE_BASENAME}")
      endif()
    else()
      get_filename_component(FILE_BASENAME "${FILE_PATH}" NAME)
      if(EXISTS "${DESTINATION}/${FILE_BASENAME}")
        file(REMOVE "${DESTINATION}/${FILE_BASENAME}")
      endif()
      file(
        CREATE_LINK "${FILE_PATH}" "${DESTINATION}/${FILE_BASENAME}"
        RESULT CREATE_LINK_RESULT
        COPY_ON_ERROR)
      if(NOT CREATE_LINK_RESULT EQUAL 0)
        echowithcolor(COLOR GREEN
                      "-- Try to link ${FILE_PATH} to ${DESTINATION}/${FILE_BASENAME} failed: ${CREATE_LINK_RESULT}")
      endif()
    endif()
  endforeach()
endfunction()

if(PROJECT_ENABLE_PRECOMPILE_HEADERS)
  if(MSVC)
    add_compiler_flags_to_var_unique(CMAKE_CXX_FLAGS "/wd4702")
  endif()
endif()

if(NOT PROJECT_TOOL_CLANG_FORMAT)
  if(DEFINED ENV{PROJECT_TOOL_CLANG_FORMAT})
    set(PROJECT_TOOL_CLANG_FORMAT $ENV{PROJECT_TOOL_CLANG_FORMAT})
  elseif(DEFINED ENV{CLANG_FORMAT})
    set(PROJECT_TOOL_CLANG_FORMAT $ENV{CLANG_FORMAT})
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang")
    get_filename_component(PROJECT_TOOL_FIND_CLANG_FORMAT "${CMAKE_CXX_COMPILER}" DIRECTORY)
    if(ATFRAMEWORK_CMAKE_TOOLSET_HOST_IS_WINDOWS AND EXISTS "${PROJECT_TOOL_FIND_CLANG_FORMAT}/clang-format.exe")
      set(PROJECT_TOOL_CLANG_FORMAT "${PROJECT_TOOL_FIND_CLANG_FORMAT}/clang-format.exe")
    elseif(EXISTS "${PROJECT_TOOL_FIND_CLANG_FORMAT}/clang-format")
      set(PROJECT_TOOL_CLANG_FORMAT "${PROJECT_TOOL_FIND_CLANG_FORMAT}/clang-format")
    endif()
    unset(PROJECT_TOOL_FIND_CLANG_FORMAT)
  endif()
endif()

if(NOT PROJECT_TOOL_CLANG_FORMAT)
  find_program(PROJECT_TOOL_CLANG_FORMAT NAMES clang-format)
endif()

if(NOT PROJECT_TOOL_CLANG_FORMAT
   AND UNIX
   AND EXISTS "/opt/llvm-latest/bin/clang-format")
  set(PROJECT_TOOL_CLANG_FORMAT "/opt/llvm-latest/bin/clang-format")
endif()

if(NOT PROJECT_TOOL_CLANG_FORMAT)
  if(WIN32 AND ATFRAMEWORK_CMAKE_TOOLSET_PWSH)
    generate_for_pb_initialize_pwsh("${CMAKE_CURRENT_BINARY_DIR}/.format-code-files.ps1")
  else()
    generate_for_pb_initialize_sh("${CMAKE_CURRENT_BINARY_DIR}/.format-code-files.sh")
  endif()
  unset(PROJECT_TOOL_CLANG_FORMAT)
endif()

function(project_tool_clang_format_generate_commands OUTPUT_VAR SCRIPT_BASEPATH)
  if(NOT PROJECT_TOOL_CLANG_FORMAT)
    unset(${OUTPUT_VAR} PARENT_SCOPE)
    return()
  endif()
  if(NOT IS_ABSOLUTE SCRIPT_BASEPATH)
    set(SCRIPT_BASEPATH "${CMAKE_CURRENT_BINARY_DIR}/SCRIPT_BASEPATH")
  endif()
  if(WIN32 AND ATFRAMEWORK_CMAKE_TOOLSET_PWSH)
    set(SCRIPT_PATH "${SCRIPT_BASEPATH}.ps1")
    generate_for_pb_initialize_pwsh("${SCRIPT_PATH}")
    set(${OUTPUT_VAR}
        "${ATFRAMEWORK_CMAKE_TOOLSET_PWSH}" "${SCRIPT_PATH}"
        PARENT_SCOPE)
    set(RUN_PREFIX "& \"${PROJECT_TOOL_CLANG_FORMAT}\" \"-i\"")
  else()
    set(SCRIPT_PATH "${SCRIPT_BASEPATH}.sh")
    generate_for_pb_initialize_sh("${SCRIPT_BASEPATH}.sh")
    set(${OUTPUT_VAR}
        "${ATFRAMEWORK_CMAKE_TOOLSET_BASH}" "${SCRIPT_PATH}"
        PARENT_SCOPE)
    set(RUN_PREFIX "\"${PROJECT_TOOL_CLANG_FORMAT}\" \"-i\"")
  endif()
  # Powershell has line length limit of 32K Some Unix like systems have line length limit of 64K Linux usually has line
  # length limit of 2M We use the minmal val here
  string(LENGTH "${RUN_PREFIX}" SCRIPT_BASELENGTH)
  set(CURRENT_COMMAND_LENGTH ${SCRIPT_BASELENGTH})
  set(CURRENT_COMMAND_FILES "")
  foreach(FILE_PATH ${ARGN})
    string(LENGTH "${FILE_PATH}" FILE_PATHLENGTH)
    math(EXPR NEXT_LINE_LENGTH "${CURRENT_COMMAND_LENGTH} + 3 + ${FILE_PATHLENGTH}")
    # Flush commanline
    if(NEXT_LINE_LENGTH GREATER 32000)
      if(WIN32 AND ATFRAMEWORK_CMAKE_TOOLSET_PWSH)
        file(APPEND "${SCRIPT_PATH}" "${RUN_PREFIX} ${CURRENT_COMMAND_FILES}${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
      else()
        file(APPEND "${SCRIPT_PATH}" "${RUN_PREFIX} ${CURRENT_COMMAND_FILES}${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
      endif()
      set(CURRENT_COMMAND_FILES "")
      math(EXPR NEXT_LINE_LENGTH "${SCRIPT_BASELENGTH} + 3 + ${FILE_PATHLENGTH}")
    endif()
    string(APPEND CURRENT_COMMAND_FILES " \"${FILE_PATH}\"")
    set(CURRENT_COMMAND_LENGTH ${NEXT_LINE_LENGTH})
  endforeach()

  if(CURRENT_COMMAND_FILES)
    if(WIN32 AND ATFRAMEWORK_CMAKE_TOOLSET_PWSH)
      file(APPEND "${SCRIPT_PATH}" "${RUN_PREFIX} ${CURRENT_COMMAND_FILES}${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
    else()
      file(APPEND "${SCRIPT_PATH}" "${RUN_PREFIX} ${CURRENT_COMMAND_FILES}${PROJECT_THIRD_PARTY_BUILDTOOLS_BASH_EOL}")
    endif()
  endif()
endfunction()

function(project_tool_clang_format_generate_cmake_commands OUTPUT_VAR SCRIPT_BASEPATH)
  unset(__OUTPUT_VAR)
  project_tool_clang_format_generate_commands(__OUTPUT_VAR "${SCRIPT_BASEPATH}" ${ARGN})
  if(__OUTPUT_VAR)
    set(${OUTPUT_VAR}
        "COMMAND" ${__OUTPUT_VAR}
        PARENT_SCOPE)
  else()
    unset(${OUTPUT_VAR} PARENT_SCOPE)
  endif()
endfunction()

if(PROJECT_TOOL_ENABLE_SPLIT_DEBUG_SYMBOL_SUFFIX)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    find_program(PROJECT_TOOL_OBJCOPY NAMES objcopy)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang")
    find_program(PROJECT_TOOL_OBJCOPY NAMES llvm-objcopy)
  elseif(MSVC)
    if(NOT PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS MATCHES "/Z7|/Zi|/ZI" AND NOT CMAKE_CXX_FLAGS MATCHES "/Z7|/Zi|/ZI")
      list(APPEND PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS "/Zi")
    endif()
  endif()
else()
  if(MSVC)
    if(NOT PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS MATCHES "/Z7|/Zi|/ZI" AND NOT CMAKE_CXX_FLAGS MATCHES "/Z7|/Zi|/ZI")
      list(APPEND PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS "/Z7")
    endif()
  endif()
endif()

function(project_tool_split_target_debug_sybmol)
  if(PROJECT_TOOL_ENABLE_SPLIT_DEBUG_SYMBOL_SUFFIX AND PROJECT_TOOL_OBJCOPY)
    foreach(TARGET_NAME ${ARGN})
      add_custom_command(
        TARGET ${TARGET_NAME}
        POST_BUILD
        COMMAND "${PROJECT_TOOL_OBJCOPY}" --only-keep-debug "$<TARGET_FILE:${TARGET_NAME}>"
                "$<TARGET_FILE:${TARGET_NAME}>${PROJECT_TOOL_ENABLE_SPLIT_DEBUG_SYMBOL_SUFFIX}"
        COMMAND "${PROJECT_TOOL_OBJCOPY}" --strip-debug --strip-unneeded "$<TARGET_FILE:${TARGET_NAME}>"
        COMMAND "${PROJECT_TOOL_OBJCOPY}" --remove-section ".gnu_debuglink" "$<TARGET_FILE:${TARGET_NAME}>"
        COMMAND
          "${PROJECT_TOOL_OBJCOPY}" --add-gnu-debuglink
          "$<TARGET_FILE_NAME:${TARGET_NAME}>${PROJECT_TOOL_ENABLE_SPLIT_DEBUG_SYMBOL_SUFFIX}"
          "$<TARGET_FILE:${TARGET_NAME}>"
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:${TARGET_NAME}>")
    endforeach()
  endif()
endfunction()

function(project_tool_set_target_runtime_output_directory OUTPUT_DIR)
  file(RELATIVE_PATH TARGET_OUTPUT_RELATIVE_PATH "${OUTPUT_DIR}" "${PROJECT_INSTALL_BAS_DIR}")
  cmake_parse_arguments(project_tool_set_target_runtime_output_directory "WITH_TARGET_RPATH;WITH_ARCHIVE_RPATH" "" ""
                        ${ARGN})
  set(project_tool_set_target_runtime_output_directory_APPEND_RPATH)
  if(UNIX AND NOT APPLE)
    set(ORIGIN_VAR "$ORIGIN")
  else()
    set(ORIGIN_VAR "@loader_path")
  endif()
  if(project_tool_set_target_runtime_output_directory_WITH_TARGET_RPATH)
    list(
      APPEND
      project_tool_set_target_runtime_output_directory_APPEND_RPATH
      "${ORIGIN_VAR}/${TARGET_OUTPUT_RELATIVE_PATH}${CMAKE_INSTALL_LIBDIR}/${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}/${TARGET_NAME}/${CMAKE_INSTALL_LIBDIR}"
    )
  endif()
  list(
    APPEND
    project_tool_set_target_runtime_output_directory_APPEND_RPATH
    "${ORIGIN_VAR}/${TARGET_OUTPUT_RELATIVE_PATH}${CMAKE_INSTALL_LIBDIR}/${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}/${CMAKE_INSTALL_LIBDIR}"
  )
  if(project_tool_set_target_runtime_output_directory_WITH_TARGET_RPATH
     AND project_tool_set_target_runtime_output_directory_WITH_ARCHIVE_RPATH)
    list(
      APPEND
      project_tool_set_target_runtime_output_directory_APPEND_RPATH
      "${ORIGIN_VAR}/${TARGET_OUTPUT_RELATIVE_PATH}${CMAKE_INSTALL_LIBDIR}/archive/${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}/${TARGET_NAME}/${CMAKE_INSTALL_LIBDIR}"
    )
  endif()
  if(project_tool_set_target_runtime_output_directory_WITH_ARCHIVE_RPATH)
    list(
      APPEND
      project_tool_set_target_runtime_output_directory_APPEND_RPATH
      "${ORIGIN_VAR}/${TARGET_OUTPUT_RELATIVE_PATH}${CMAKE_INSTALL_LIBDIR}/archive/${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}/${CMAKE_INSTALL_LIBDIR}"
    )
  endif()

  if(project_tool_set_target_runtime_output_directory_WITH_TARGET_RPATH)
    list(
      APPEND
      project_tool_set_target_runtime_output_directory_APPEND_RPATH
      "${ORIGIN_VAR}/${TARGET_OUTPUT_RELATIVE_PATH}${CMAKE_INSTALL_LIBDIR}/${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}/__shared/${CMAKE_INSTALL_LIBDIR}"
    )
    if(project_tool_set_target_runtime_output_directory_WITH_ARCHIVE_RPATH)
      list(
        APPEND
        project_tool_set_target_runtime_output_directory_APPEND_RPATH
        "${ORIGIN_VAR}/${TARGET_OUTPUT_RELATIVE_PATH}${CMAKE_INSTALL_LIBDIR}/archive/${SERVER_FRAME_VCS_COMMIT_SHORT_SHA}/__shared/${CMAKE_INSTALL_LIBDIR}"
      )
    endif()
  endif()

  set_property(TARGET ${TARGET_NAME} PROPERTY RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}")
  set_property(
    TARGET ${TARGET_NAME}
    APPEND
    PROPERTY INSTALL_RPATH "${project_tool_set_target_runtime_output_directory_APPEND_RPATH}")
endfunction()
