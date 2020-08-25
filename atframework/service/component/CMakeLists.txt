
set (ATSF4G_APP_NAME ${ATFRAMEWORK_SERVICE_COMPONENT_LINK_NAME})
EchoWithColor(COLOR GREEN "-- Configure ${ATSF4G_APP_NAME} on ${CMAKE_CURRENT_LIST_DIR}")

configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/config/atframe_services_build_feature.h.in"
    "${CMAKE_CURRENT_LIST_DIR}/config/atframe_services_build_feature.h"
    @ONLY
)

# ============ atproxy ============

file(GLOB_RECURSE ATSF4G_APP_SRC_LIST
    *.cpp
    *.cc
    *.c
    *.cxx
    *.h
    *.hpp
)
source_group_by_dir(ATSF4G_APP_SRC_LIST)

if (BUILD_SHARED_LIBS OR ATFRAMEWORK_USE_DYNAMIC_LIBRARY)
    add_library(${ATSF4G_APP_NAME} SHARED ${ATSF4G_APP_SRC_LIST})
else ()
    add_library(${ATSF4G_APP_NAME} STATIC ${ATSF4G_APP_SRC_LIST})
endif ()

if (MSVC)
    set_property(TARGET ${ATSF4G_APP_NAME} PROPERTY FOLDER "atframework/service")
endif (MSVC)

target_link_libraries(${ATSF4G_APP_NAME}
    PUBLIC ${ATFRAMEWORK_ATFRAME_UTILS_LINK_NAME} ${ATFRAMEWORK_ATBUS_LINK_NAME} ${ATFRAMEWORK_ATAPP_LINK_NAME} ${3RD_PARTY_PUBLIC_LINK_NAMES}
    INTERFACE ${3RD_PARTY_INTERFACE_LINK_NAMES}
)

target_compile_options(${ATSF4G_APP_NAME}
    PRIVATE ${ATFRAMEWORK_SREVICE_PRIVATE_OPTIONS}
)

target_include_directories(${ATSF4G_APP_NAME} 
    PUBLIC 
        "$<BUILD_INTERFACE:${3RD_PARTY_PUBLIC_INCLUDE_DIRS}>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>"
)

install(TARGETS ${ATSF4G_APP_NAME}
    EXPORT "${ATSF4G_APP_NAME}-target"
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

export(TARGETS ${ATSF4G_APP_NAME}
    NAMESPACE "atframework::"
    FILE "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/${ATSF4G_APP_NAME}-target.cmake"
)

install(EXPORT "${ATSF4G_APP_NAME}-target"
    NAMESPACE "atframework::"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake"
)