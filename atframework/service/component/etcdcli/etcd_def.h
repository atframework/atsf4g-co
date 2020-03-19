/**
 * etcd_cluster.h
 *
 *  Created on: 2017-11-17
 *      Author: owent
 *
 *  Released under the MIT license
 */

#ifndef ATFRAME_SERVICE_COMPONENT_ETCDCLI_ETCD_DEF_H
#define ATFRAME_SERVICE_COMPONENT_ETCDCLI_ETCD_DEF_H

#pragma once

#include <stdint.h>
#include <string>

namespace atframe {
    namespace component {
        struct etcd_response_header {
            uint64_t cluster_id;
            uint64_t member_id;
            int64_t revision;
            uint64_t raft_term;
        };

        struct etcd_key_value {
            std::string key;
            int64_t create_revision;
            int64_t mod_revision;
            int64_t version;
            std::string value;
            int64_t lease;
        };

        struct etcd_watch_event {
            enum type {
                EN_WEVT_PUT = 0,   // put
                EN_WEVT_DELETE = 1 // delete
            };
        };
    } // namespace component
} // namespace atframe

#endif