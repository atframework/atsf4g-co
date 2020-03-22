//
// Created by owt50 on 2019/09/06.
//

#ifndef UTILITY_INI_LOADER_HELPER_H
#define UTILITY_INI_LOADER_HELPER_H

#pragma once

#include <stdint.h>
#include <cstddef>
#include <cstring>
#include <string>
#include <memory>
#include <type_traits>

#include <rapidjson/document.h>

#include <log/log_wrapper.h>

namespace util {
    namespace config {
        class ini_value;
    }
}

namespace google {
    namespace protobuf {
        class Message;
    }
}

void ini_loader_helper_dump_to(const util::config::ini_value& src, ::google::protobuf::Message& dst);

// void ini_loader_helper_load_from(util::config::ini_value& dst, const ::google::protobuf::Message& src);


#endif
