if(NOT Python3_EXECUTABLE)
  find_package(Python3 COMPONENTS Interpreter)
endif()

if(Python3_EXECUTABLE)
  add_custom_target(
    generate-for-pb-python-unit-test
    COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s "${CMAKE_CURRENT_LIST_DIR}/test" -t
            "${CMAKE_CURRENT_LIST_DIR}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    COMMENT "Run generate-for-pb Python unit test")
  set_property(TARGET generate-for-pb-python-unit-test PROPERTY FOLDER "${PROJECT_NAME}/test")

  if(BUILD_TESTING)
    add_test(NAME generate-for-pb.python-unit-test
             COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s "${CMAKE_CURRENT_LIST_DIR}/test" -t
                     "${CMAKE_CURRENT_LIST_DIR}")
    set_tests_properties(generate-for-pb.python-unit-test PROPERTIES LABELS "python;generate-for-pb")
  endif()
endif()
