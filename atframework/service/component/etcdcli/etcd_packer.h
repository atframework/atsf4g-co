/**
 * etcd_cluster.h
 *
 *  Created on: 2017-11-17
 *      Author: owent
 *
 *  Released under the MIT license
 */

#ifndef ATFRAME_SERVICE_COMPONENT_ETCDCLI_ETCD_PACKER_H
#define ATFRAME_SERVICE_COMPONENT_ETCDCLI_ETCD_PACKER_H

#pragma once

#include <config/compiler/template_prefix.h>
#include "rapidjson/document.h"
#include <config/compiler/template_suffix.h>

#include "etcd_def.h"

namespace atframe {
    namespace component {
        class etcd_packer {
        public:
            static bool parse_object(rapidjson::Document &doc, const char *data);

            static void pack(const etcd_key_value &etcd_val, rapidjson::Value &json_val, rapidjson::Document &doc);
            static void unpack(etcd_key_value &etcd_val, rapidjson::Value &json_val);

            static void pack(const etcd_response_header &etcd_val, rapidjson::Value &json_val, rapidjson::Document &doc);
            static void unpack(etcd_response_header &etcd_val, rapidjson::Value &json_val);

            /**
             * @brief pack key-range into json_val
             * @note if range_end = key+1(key="aa" and range_end="ab" or key="a\0xff" and range_end="b"), then the range is all keys prefixed with key
             * @note if range_end = '\0', then the range is all keys greater than key
             * @note if both key = '\0' and range_end = '\0', then the range is all the keys
             * @see https://coreos.com/etcd/docs/latest/learning/api.html#key-ranges
             * @param json_val where to write
             * @param key data of key
             * @param key data of range_end, can be any key value or +1
             */
            static void pack_key_range(rapidjson::Value &json_val, const std::string &key, std::string range_end, rapidjson::Document &doc);

            static void        pack_string(rapidjson::Value &json_val, const char *key, const char *val, rapidjson::Document &doc);
            static bool        unpack_string(rapidjson::Value &json_val, const char *key, std::string &val);
            static std::string unpack_to_string(const rapidjson::Value &json_val);

            static void pack_base64(rapidjson::Value &json_val, const char *key, const std::string &val, rapidjson::Document &doc);
            static bool unpack_base64(rapidjson::Value &json_val, const char *key, std::string &val);

            static void unpack_int(rapidjson::Value &json_val, const char *key, int64_t &out);
            static void unpack_int(rapidjson::Value &json_val, const char *key, uint64_t &out);
            static void unpack_bool(rapidjson::Value &json_val, const char *key, bool &out);
        };
    } // namespace component
} // namespace atframe

#endif