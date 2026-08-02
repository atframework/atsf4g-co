# =====================================================================================================================
# Remove CMake-generated precompiled header (PCH) build artifacts from a build tree.
#
# Run in script mode, e.g.:
#   cmake -DPROJECT_PCH_CLEANUP_DIR=<build dir> -P project/cmake/CleanupPrecompileHeaders.cmake
#
# Only the compiled PCH outputs (cmake_pch.*.pch / .gch / .obj / .o) are removed, so a subsequent build regenerates
# them without a fresh CMake configure. The generated stub sources (cmake_pch.hxx / cmake_pch.cxx) and any clangd
# index shards under a ".cache" directory are preserved.
# =====================================================================================================================

if(CMAKE_SCRIPT_MODE_FILE)
  if(NOT PROJECT_PCH_CLEANUP_DIR)
    set(PROJECT_PCH_CLEANUP_DIR "${CMAKE_CURRENT_BINARY_DIR}")
  endif()

  if(NOT IS_DIRECTORY "${PROJECT_PCH_CLEANUP_DIR}")
    message(STATUS "[cleanup-precompile-headers] Skip missing directory: ${PROJECT_PCH_CLEANUP_DIR}")
    return()
  endif()

  file(
    GLOB_RECURSE PROJECT_PCH_CLEANUP_FILES
    LIST_DIRECTORIES false
    "${PROJECT_PCH_CLEANUP_DIR}/cmake_pch.*.pch" "${PROJECT_PCH_CLEANUP_DIR}/cmake_pch.*.gch"
    "${PROJECT_PCH_CLEANUP_DIR}/cmake_pch.*.obj" "${PROJECT_PCH_CLEANUP_DIR}/cmake_pch.*.o")

  set(PROJECT_PCH_CLEANUP_COUNT 0)
  foreach(PROJECT_PCH_CLEANUP_FILE IN LISTS PROJECT_PCH_CLEANUP_FILES)
    # Never touch clangd index shards, which live under a ".cache" directory.
    if(PROJECT_PCH_CLEANUP_FILE MATCHES "(^|/)\\.cache/")
      continue()
    endif()
    file(REMOVE "${PROJECT_PCH_CLEANUP_FILE}")
    math(EXPR PROJECT_PCH_CLEANUP_COUNT "${PROJECT_PCH_CLEANUP_COUNT} + 1")
  endforeach()

  message(
    STATUS
      "[cleanup-precompile-headers] Removed ${PROJECT_PCH_CLEANUP_COUNT} precompiled header artifact(s) under ${PROJECT_PCH_CLEANUP_DIR}"
  )
endif()
