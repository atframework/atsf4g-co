#ifndef ATFRAME_SERVICE_COMPONENT_CONFIG_EXTERN_SERVICE_TYPES_H
#define ATFRAME_SERVICE_COMPONENT_CONFIG_EXTERN_SERVICE_TYPES_H

#pragma once

#include <config/atframe_service_types.h>

namespace atframe {
    namespace component {
        struct message_type {
            enum type {
                EN_ATST_SS_MSG = service_type::EN_ATST_CUSTOM_START, // solution services
            };
        };

        struct logic_service_type {
            enum type {
                EN_LST_LOGINSVR = 35,
                EN_LST_GAMESVR  = 36,
            };
        };
    } // namespace component
} // namespace atframe
#endif