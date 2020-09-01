/*
 * timestamp_id_allocator.h
 *
 *  Created on: 2016-07-26
 *      Author: owent
 *
 *  Released under the MIT license
 */

#ifndef ATFRAME_SERVICE_COMPONENT_TIMESTAMP_ID_ALLOCATOR_H
#define ATFRAME_SERVICE_COMPONENT_TIMESTAMP_ID_ALLOCATOR_H

#pragma once

#include <stdint.h>

#include <config/atframe_services_build_feature.h>

#include "lock/atomic_int_type.h"
#include "time/time_utility.h"


namespace atframe {
    namespace component {

        /**
         * @biref allocate a id of specify type
         * @note we guarantee id will not repeated for a short time.
         *       the time depennd the length of TKey.
         */
        template <typename TKey = uint64_t>
        class ATFRAME_SERVICE_COMPONENT_MACRO_API_HEAD_ONLY timestamp_id_allocator {
        public:
            typedef TKey value_type;

            static const value_type npos = 0; /** invalid key **/
        public:
            value_type allocate() UTIL_CONFIG_NOEXCEPT {
                static util::lock::atomic_int_type<value_type> seq_alloc(255);

                static const size_t     seq_bits  = sizeof(value_type) * 4;
                static const value_type time_mask = (static_cast<value_type>(1) << (sizeof(value_type) * 8 - seq_bits)) - 1;

                // always do not allocate 0 as a valid ID
                value_type ret = npos;
                while (npos == ret) {
                    value_type res       = seq_alloc.load();
                    value_type time_part = res >> seq_bits;

                    value_type next_ret       = res + 1;
                    value_type next_time_part = next_ret >> seq_bits;
                    if (0 == time_part || time_part != next_time_part) {
                        value_type now_time = time_part;
                        while (time_part == now_time) {
                            now_time = static_cast<value_type>(util::time::time_utility::get_now()) & time_mask;
                        }

                        // if failed, maybe another thread do it
                        if (seq_alloc.compare_exchange_strong(res, now_time << seq_bits)) {
                            ret = now_time << seq_bits;
                        }
                    } else {
                        if (seq_alloc.compare_exchange_weak(res, next_ret)) {
                            ret = next_ret;
                        }
                    }
                }

                return ret;
            }

            void deallocate(value_type) UTIL_CONFIG_NOEXCEPT {}
        };
    } // namespace component
} // namespace atframe

#endif /* STANDARD_INT_ID_ALLOCATOR_H_ */
