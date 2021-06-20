file(GLOB TOOLS_DIRS "${CMAKE_CURRENT_LIST_DIR}/*/CMakeLists.txt")

foreach(TOOLS_PROJ_FILE IN LISTS TOOLS_DIRS)
  get_filename_component(TOOLS_PROJ_DIR ${TOOLS_PROJ_FILE} DIRECTORY)
  add_subdirectory(${TOOLS_PROJ_DIR})
endforeach()
