//
// Created by owt50 on 2016/10/9.
//

#include <config/compiler_features.h>

#include <algorithm/murmur_hash.h>
#include <common/string_oprs.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <lock/lock_holder.h>
#include <lock/seq_alloc.h>
#include <lock/spin_rw_lock.h>

#include <protocol/config/com.const.config.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.table.pb.h>


#include <dispatcher/db_msg_dispatcher.h>
#include <dispatcher/task_manager.h>


#include "../rpc_utils.h"
#include "db_utils.h"

#include <random/uuid_generator.h>

#include "uuid.h"

namespace rpc {
    namespace db {
        namespace uuid {
            namespace detail {
                struct short_uuid_encoder {
                    short_uuid_encoder() { memcpy(keys, "M7Vy1DQnIj93B2kNPJCRxuoTYhvSpOgstKaZ0lrH8WmGdcXLbzeqwUE5F4i6Af", 62); }

                    ::util::lock::seq_alloc_u32 seq_;
                    char                        keys[62];
                    size_t                      operator()(char *in, size_t insz, uint64_t val) {
                        if (insz == 0 || NULL == in) {
                            return 0;
                        }

                        size_t ret;
                        for (ret = 1; val > 0 && ret < insz; ++ret) {
                            in[ret] = keys[val % 62];
                            val /= 62;
                        }

                        if (ret < 62) {
                            in[0] = keys[ret];
                        } else {
                            in[0] = keys[61];
                        }

                        return ret;
                    }

                    size_t operator()(char *in, size_t insz) {
                        uint32_t v = seq_.inc();
                        if (0 == v) {
                            v = seq_.inc();
                        }

                        return (*this)(in, insz, v);
                    }
                };
                static short_uuid_encoder short_uuid_encoder_;
            } // namespace detail

#if defined(LIBATFRAME_UTILS_ENABLE_UUID) && LIBATFRAME_UTILS_ENABLE_UUID
            int generate_standard_uuid(std::string &uuid) {
                uuid = util::random::uuid_generator::generate_string();
                return hello::err::EN_SUCCESS;
            }
#endif

            std::string generate_short_uuid() {
                // bus_id:(timestamp-2018-01-01 00:00:00):sequence
                // 2018-01-01 00:00:00 UTC => 1514764800
                uint64_t bus_id     = logic_config::me()->get_self_bus_id();
                time_t   time_param = util::time::time_utility::get_now() - 1514764800;

                // 第一个字符用S，表示服务器生成，这样如果客户端生成的用C开头，就不会和服务器冲突
                char   bin_buffer[64] = {'S', 0};
                size_t start_index    = 1;
                start_index += detail::short_uuid_encoder_(&bin_buffer[start_index], sizeof(bin_buffer) - start_index - 1, bus_id);
                start_index += detail::short_uuid_encoder_(&bin_buffer[start_index], sizeof(bin_buffer) - start_index - 1,
                                                           time_param > 0 ? static_cast<uint64_t>(time_param) : 0);
                start_index += detail::short_uuid_encoder_(&bin_buffer[start_index], sizeof(bin_buffer) - start_index - 1);
                bin_buffer[start_index] = 0;

                return bin_buffer;
            }

            int64_t generate_global_increase_id(uint32_t major_type, uint32_t minor_type, uint32_t patch_type) {
                task_manager::task_t *task = task_manager::task_t::this_task();
                if (!task) {
                    WLOGERROR("current not in a task");
                    return hello::err::EN_SYS_RPC_NO_TASK;
                }

                // 这个算法比许固定
                char   keyvar[64];
                size_t keylen                   = sizeof(keyvar) - 1;
                int    __snprintf_writen_length = UTIL_STRFUNC_SNPRINTF(keyvar, static_cast<int>(keylen), "guid:%x-%x-%x", major_type, minor_type, patch_type);
                if (__snprintf_writen_length < 0) {
                    keyvar[sizeof(keyvar) - 1] = '\0';
                    keylen                     = 0;
                } else {
                    keylen                           = static_cast<size_t>(__snprintf_writen_length);
                    keyvar[__snprintf_writen_length] = '\0';
                }

                redis_args args(2);
                {
                    args.push("INCR");
                    args.push(keyvar);
                }

                int res = db_msg_dispatcher::me()->send_msg(db_msg_dispatcher::channel_t::CLUSTER_DEFAULT, keyvar, keylen, task->get_id(),
                                                            logic_config::me()->get_self_bus_id(), rpc::db::detail::unpack_integer,
                                                            static_cast<int>(args.size()), args.get_args_values(), args.get_args_lengths());

                if (res < 0) {
                    return res;
                }

                hello::table_all_message msg;
                // 协程操作
                res = rpc::wait(msg);
                if (res < 0) {
                    return res;
                }

                if (!msg.has_simple()) {
                    return hello::err::EN_DB_RECORD_NOT_FOUND;
                }

                return msg.simple().msg_i64();
            }


            struct unique_id_key_t {
                uint32_t major_type;
                uint32_t minor_type;
                uint32_t patch_type;

                friend bool operator==(const unique_id_key_t &l, const unique_id_key_t &r) UTIL_CONFIG_NOEXCEPT {
                    return l.major_type == r.major_type && l.minor_type == r.minor_type && l.patch_type == r.patch_type;
                }

                friend bool operator<(const unique_id_key_t &l, const unique_id_key_t &r) UTIL_CONFIG_NOEXCEPT {
                    if (l.major_type != r.major_type) {
                        return l.major_type < r.major_type;
                    }

                    if (l.minor_type != r.minor_type) {
                        return l.minor_type < r.minor_type;
                    }

                    return l.patch_type < r.patch_type;
                }

                friend bool operator<=(const unique_id_key_t &l, const unique_id_key_t &r) UTIL_CONFIG_NOEXCEPT { return l == r || l < r; }
            };

            struct unique_id_value_t {
                task_manager::task_ptr_t alloc_task;
                int64_t                  unique_id_index;
                int64_t                  unique_id_base;
            };

#if defined(UTIL_ENV_AUTO_UNORDERED) && UTIL_ENV_AUTO_UNORDERED
            struct unique_id_container_helper {
                std::size_t operator()(unique_id_key_t const &v) const UTIL_CONFIG_NOEXCEPT {
                    uint32_t data[3] = {v.major_type, v.minor_type, v.patch_type};
                    uint64_t out[2];
                    util::hash::murmur_hash3_x64_128(data, sizeof(data), 0, out);
                    return static_cast<std::size_t>(out[0]);
                }
            };
#else
            struct unique_id_container_helper {
                bool operator()(unique_id_key_t const &l, unique_id_key_t const &r) const UTIL_CONFIG_NOEXCEPT { return l < r; }
            };

#endif

            static UTIL_ENV_AUTO_MAP(unique_id_key_t, unique_id_value_t, unique_id_container_helper) g_unique_id_pools;
            static util::lock::spin_rw_lock g_unique_id_pool_locker;

            int64_t generate_global_unique_id(uint32_t major_type, uint32_t minor_type, uint32_t patch_type) {
                task_manager::task_t *this_task = task_manager::task_t::this_task();
                if (NULL == this_task) {
                    return hello::err::EN_SYS_RPC_NO_TASK;
                }

                // POOL => 1 | 50 | 13
                UTIL_CONFIG_CONSTEXPR int64_t bits_off   = 13;
                UTIL_CONFIG_CONSTEXPR int64_t bits_range = 1 << bits_off;
                UTIL_CONFIG_CONSTEXPR int64_t bits_mask  = bits_range - 1;

                unique_id_key_t key;
                key.major_type = major_type;
                key.minor_type = minor_type;
                key.patch_type = patch_type;

                typedef UTIL_ENV_AUTO_MAP(unique_id_key_t, unique_id_value_t, unique_id_container_helper) real_map_type;
                real_map_type::iterator iter;

                do {
                    {
                        util::lock::read_lock_holder<util::lock::spin_rw_lock> lock_guard(g_unique_id_pool_locker);
                        iter = g_unique_id_pools.find(key);
                    }
                    if (g_unique_id_pools.end() == iter) {
                        util::lock::write_lock_holder<util::lock::spin_rw_lock> lock_guard(g_unique_id_pool_locker);
                        unique_id_value_t                                       val;
                        val.unique_id_index = 0;
                        val.unique_id_base  = 0;
                        iter                = g_unique_id_pools.insert(real_map_type::value_type(key, val)).first;

                        if (g_unique_id_pools.end() == iter) {
                            return hello::err::EN_SYS_MALLOC;
                        }
                    }
                } while (false);

                unique_id_value_t *alloc = &iter->second;

                int64_t ret      = 0;
                int     try_left = 5;

                while (try_left-- > 0 && ret <= 0) {
                    // must in task, checked before
                    assert(this_task == task_manager::task_t::this_task());

                    // 任务已经失败或者不在任务中
                    if (nullptr == this_task || this_task->is_exiting()) {
                        ret = 0;
                        break;
                    }

                    // 如果已有分配请求，仅仅排队即可
                    if (alloc->alloc_task && !alloc->alloc_task->is_exiting()) {
                        alloc->alloc_task->next(task_manager::task_ptr_t(this_task));
                        this_task->yield(NULL); // 切出，等待切回后继续
                        ret = 0;
                        continue;
                    }

                    int64_t &unique_id_index = alloc->unique_id_index;
                    int64_t &unique_id_base  = alloc->unique_id_base;
                    unique_id_index &= bits_mask;

                    ret = (unique_id_base << bits_off) | (unique_id_index++);

                    // call rpc to allocate a id pool
                    if (0 == (ret >> bits_off) || 0 == (ret & bits_mask)) {
                        alloc->alloc_task = task_manager::me()->get_task(this_task->get_id());
                        int64_t res       = generate_global_increase_id(major_type, minor_type, patch_type);
                        // WLOGINFO("=====DEBUG===== generate uuid pool for (%u, %u, %u), val: %lld", major_type, minor_type, patch_type,
                        // static_cast<long long>(res));
                        if (alloc->alloc_task.get() == this_task) {
                            alloc->alloc_task.reset();
                        }
                        if (res <= 0) {
                            ret = res;
                            continue;
                        }
                        unique_id_base  = res;
                        unique_id_index = 1;
                    }
                }

                // WLOGINFO("=====DEBUG===== malloc uuid for (%u, %u, %u), val: %lld", major_type, minor_type, patch_type, static_cast<long
                // long>(ret));
                return ret;
            }
        } // namespace uuid
    }     // namespace db
} // namespace rpc
