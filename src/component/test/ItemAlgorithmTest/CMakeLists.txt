# =========== ItemAlgorithm Unit Tests ===========
set(ITEM_ALGORITHM_TEST_FRAME_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../atframework/atframe_utils/test")

set(ITEM_ALGORITHM_TEST_SRC
    "${CMAKE_CURRENT_LIST_DIR}/main.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/item_grid_algorithm_test.cpp"
    "${ITEM_ALGORITHM_TEST_FRAME_DIR}/frame/test_case_base.cpp"
    "${ITEM_ALGORITHM_TEST_FRAME_DIR}/frame/test_manager.cpp")

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
  set(ITEM_ALGORITHM_TEST_TARGET "pc-ItemAlgorithmTest")
else()
  set(ITEM_ALGORITHM_TEST_TARGET "${PROJECT_NAME}-component-ItemAlgorithmTest")
endif()

add_executable(${ITEM_ALGORITHM_TEST_TARGET} ${ITEM_ALGORITHM_TEST_SRC})

target_include_directories(${ITEM_ALGORITHM_TEST_TARGET} PRIVATE "${ITEM_ALGORITHM_TEST_FRAME_DIR}")

target_link_libraries(${ITEM_ALGORITHM_TEST_TARGET} PRIVATE components::ItemAlgorithmSDK)

target_compile_definitions(${ITEM_ALGORITHM_TEST_TARGET} PRIVATE ${PROJECT_GAME_SHARED_COMPONENT_PRIVATE_DEFINITIONS})

target_compile_options(${ITEM_ALGORITHM_TEST_TARGET} PRIVATE ${PROJECT_COMMON_PRIVATE_COMPILE_OPTIONS})

set_target_properties(
  ${ITEM_ALGORITHM_TEST_TARGET}
  PROPERTIES INSTALL_RPATH_USE_LINK_PATH YES
             BUILD_WITH_INSTALL_RPATH NO
             BUILD_RPATH_USE_ORIGIN YES)

set_property(TARGET ${ITEM_ALGORITHM_TEST_TARGET} PROPERTY FOLDER "${PROJECT_NAME}/test")

project_setup_runtime_post_build_bash(${ITEM_ALGORITHM_TEST_TARGET} PROJECT_RUNTIME_POST_BUILD_EXECUTABLE_BASH)
project_setup_runtime_post_build_pwsh(${ITEM_ALGORITHM_TEST_TARGET} PROJECT_RUNTIME_POST_BUILD_EXECUTABLE_PWSH)
