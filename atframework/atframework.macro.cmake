# import libatframe_utils
include("${ATFRAMEWORK_BASE_DIR}/libatframe_utils/libatframe_utils.cmake")

# import libatbus
include("${ATFRAMEWORK_BASE_DIR}/libatbus/libatbus.cmake")

# import libatapp
include("${ATFRAMEWORK_BASE_DIR}/libatapp/libatapp.cmake")

set(ATFRAMEWORK_SERVICE_COMPONENT_DIR "${ATFRAMEWORK_BASE_DIR}/service/component")
set(ATFRAMEWORK_SERVICE_COMPONENT_LINK_NAME atservice_component)

set(ATFRAMEWORK_SERVICE_GATEWAY_PROTOCOL_DIR "${ATFRAMEWORK_BASE_DIR}/service/atgateway/protocols")
set(ATFRAMEWORK_SERVICE_GATEWAY_VERSION "0.1.0")
set(ATFRAMEWORK_SERVICE_GATEWAY_LINK_NAME "atgateway-protocol")

# export library
include("${ATFRAMEWORK_BASE_DIR}/export/export.macro.cmake")