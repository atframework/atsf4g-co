//
// Created by owt50 on 2016/9/28.
//

#include <log/log_wrapper.h>

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.table.pb.h>


#include <hiredis_happ.h>

#include <config/logic_config.h>
#include <dispatcher/db_msg_dispatcher.h>
#include <dispatcher/task_manager.h>


#include "../rpc_macros.h"
#include "../rpc_utils.h"
#include "db_macros.h"
#include "db_utils.h"


#include "player.h"


#define RPC_DB_TABLE_NAME "player"

namespace rpc {
    namespace db {
        namespace player {


            namespace detail {
                static int32_t unpack_user(hello::table_all_message &msg, const redisReply *reply) {
                    if (NULL == reply) {
                        WLOGDEBUG("data mot found.");
                        // 数据找不到，直接成功结束，外层会判为无数据
                        return hello::err::EN_SUCCESS;
                    }

                    return ::rpc::db::unpack_message(*msg.mutable_user(), reply, msg.mutable_version());
                }
            } // namespace detail

            int get_all(uint64_t user_id, uint32_t zone_id, hello::table_user &rsp, std::string &version) {
                task_manager::task_t *task = task_manager::task_t::this_task();
                if (!task) {
                    WLOGERROR("current not in a task");
                    return hello::err::EN_SYS_RPC_NO_TASK;
                }

                user_table_key_t user_key;
                size_t user_key_len = format_user_key(user_key, RPC_DB_TABLE_NAME, user_id, zone_id);
                if (user_key_len <= 0) {
                    WLOGERROR("format db cmd failed, cmd %s", user_key);
                    return hello::err::EN_DB_SEND_FAILED;
                }

                redis_args args(2);
                {
                    args.push("HGETALL");
                    args.push(user_key);
                }

                uint64_t rpc_sequence = 0;
                int res = db_msg_dispatcher::me()->send_msg(db_msg_dispatcher::channel_t::CLUSTER_DEFAULT, user_key, user_key_len, task->get_id(),
                                                            logic_config::me()->get_self_bus_id(), detail::unpack_user, rpc_sequence,
                                                            static_cast<int>(args.size()), args.get_args_values(), args.get_args_lengths());

                if (res < 0) {
                    return res;
                }

                hello::table_all_message msg;
                // 协程操作
                res = rpc::wait(msg, rpc_sequence);
                if (res < 0) {
                    return res;
                }

                if (!msg.has_user() || msg.version().empty()) {
                    return hello::err::EN_DB_RECORD_NOT_FOUND;
                }

                version.assign(msg.version());
                rsp.Swap(msg.mutable_user());

                WLOGINFO("table_user[user_id=%llu] get all data version: %s", static_cast<unsigned long long>(user_id), version.c_str());
                return hello::err::EN_SUCCESS;
            }

            int get_basic(uint64_t user_id, uint32_t zone_id, hello::table_user &rsp) {
                std::string version;
                return get_all(user_id, zone_id, rsp, version);
            }

            int set(uint64_t user_id, uint32_t zone_id, hello::table_user &store, std::string &version) {
                task_manager::task_t *task = task_manager::task_t::this_task();
                if (!task) {
                    WLOGERROR("current not in a task");
                    return hello::err::EN_SYS_RPC_NO_TASK;
                }

                if (version.empty()) {
                    version = "0";
                }

                std::stringstream segs_debug_info;

                user_table_key_t user_key;
                size_t user_key_len = format_user_key(user_key, RPC_DB_TABLE_NAME, user_id, zone_id);
                if (user_key_len <= 0) {
                    WLOGERROR("format db cmd failed, cmd %s", user_key);
                    return hello::err::EN_DB_SEND_FAILED;
                }

                std::vector<const ::google::protobuf::FieldDescriptor *> fds;
                const google::protobuf::Reflection *reflect = store.GetReflection();
                if (NULL == reflect) {
                    WLOGERROR("pack message %s failed, get reflection failed", store.GetDescriptor()->full_name().c_str());
                    return hello::err::EN_SYS_PACK;
                }
                reflect->ListFields(store, &fds);
                // version will take two segments
                // each fd will take key segment and value segment
                redis_args args(fds.size() * 2 + 6);

                args.push("EVALSHA");
                args.push(db_msg_dispatcher::me()->get_db_script_sha1(hello::EN_DBSST_PLAYER));
                args.push(1);
                args.push(user_key);

                int res = ::rpc::db::pack_message(store, args, fds, &version, &segs_debug_info);
                if (res < 0) {
                    return res;
                }

                WLOGDEBUG("user %llu save curr data version:%s", static_cast<unsigned long long>(user_id), version.c_str());

                uint64_t rpc_sequence = 0;
                res = db_msg_dispatcher::me()->send_msg(db_msg_dispatcher::channel_t::CLUSTER_DEFAULT, user_key, user_key_len, task->get_id(),
                                                        logic_config::me()->get_self_bus_id(), detail::unpack_user, rpc_sequence,
                                                        static_cast<int>(args.size()), args.get_args_values(), args.get_args_lengths());

                // args unavailable now

                if (res < 0) {
                    return res;
                }

                hello::table_all_message msg;
                // 协程操作
                res = rpc::wait(msg, rpc_sequence);
                if (res < 0) {
                    if (hello::err::EN_DB_OLD_VERSION == res && !msg.version().empty()) {
                        version.assign(msg.version());
                    }
                    return res;
                }

                if (msg.version().empty()) {
                    return hello::err::EN_DB_RECORD_NOT_FOUND;
                }

                version.assign(msg.version());

                WLOGINFO("table_user [user_id=%llu] all saved, new version: %s, detail: %s", static_cast<unsigned long long>(user_id), version.c_str(),
                         segs_debug_info.str().c_str());

                return hello::err::EN_SUCCESS;
            }

        } // namespace player
    }     // namespace db
} // namespace rpc