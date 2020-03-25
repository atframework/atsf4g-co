//
// Created by owt50 on 2016/9/28.
//

#include <log/log_wrapper.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <hiredis_happ.h>

#include <config/logic_config.h>
#include <dispatcher/db_msg_dispatcher.h>
#include <dispatcher/task_manager.h>


#include "../rpc_macros.h"
#include "../rpc_utils.h"
#include "db_macros.h"
#include "db_utils.h"
#include "login.h"

#define RPC_DB_TABLE_NAME "login"

namespace rpc {
    namespace db {
        namespace login {

            namespace detail {
                static int32_t unpack_login(hello::table_all_message &table_msg, const redisReply *reply) {
                    if (NULL == reply) {
                        WLOGDEBUG("data not found.");
                        //数据找不到，直接成功结束，外层会判断为无数据
                        return hello::err::EN_SUCCESS;
                    }

                    return ::rpc::db::unpack_message(*table_msg.mutable_login(), reply, table_msg.mutable_version());
                }
            } // namespace detail

            int get(const char *openid, uint32_t zone_id, hello::table_login &rsp, std::string &version) {
                task_manager::task_t *task = task_manager::task_t::this_task();
                if (!task) {
                    WLOGERROR("current not in a task");
                    return hello::err::EN_SYS_RPC_NO_TASK;
                }

                user_table_key_t user_key;
                size_t writen_len = format_user_key(user_key, RPC_DB_TABLE_NAME, openid, zone_id);
                if (writen_len <= 0) {
                    WLOGERROR("format db cmd failed, cmd %s", user_key);
                    return hello::err::EN_DB_SEND_FAILED;
                }

                redis_args args(2);
                {
                    args.push("HGETALL");
                    args.push(user_key);
                }

                uint64_t rpc_sequence = 0;
                int res = db_msg_dispatcher::me()->send_msg(db_msg_dispatcher::channel_t::CLUSTER_DEFAULT, user_key, writen_len, task->get_id(),
                                                            logic_config::me()->get_self_bus_id(), detail::unpack_login, rpc_sequence,
                                                            static_cast<int>(args.size()), args.get_args_values(), args.get_args_lengths());

                if (res < 0) {
                    return res;
                }

                hello::table_all_message table_msg;
                // 协程操作
                res = rpc::wait(table_msg, rpc_sequence);
                if (res < 0) {
                    return res;
                }

                if (!table_msg.has_login() || table_msg.version().empty()) {
                    return hello::err::EN_DB_RECORD_NOT_FOUND;
                }

                version.assign(table_msg.version());
                rsp.Swap(table_msg.mutable_login());
                return hello::err::EN_SUCCESS;
            }

            int set(const char *openid, uint32_t zone_id, hello::table_login &rsp, std::string &version) {
                task_manager::task_t *task = task_manager::task_t::this_task();
                if (!task) {
                    WLOGERROR("current not in a task");
                    return hello::err::EN_SYS_RPC_NO_TASK;
                }

                if (version.empty()) {
                    version = "0";
                }

                user_table_key_t user_key;
                size_t writen_len = format_user_key(user_key, RPC_DB_TABLE_NAME, openid, zone_id);
                if (writen_len <= 0) {
                    WLOGERROR("format db cmd failed, cmd %s", user_key);
                    return hello::err::EN_DB_SEND_FAILED;
                }

                std::vector<const ::google::protobuf::FieldDescriptor *> fds;
                const ::google::protobuf::Descriptor *desc = hello::table_login::descriptor();
                fds.reserve(static_cast<size_t>(desc->field_count()));

                for (int i = 0; i < desc->field_count(); ++i) {
                    fds.push_back(desc->field(i));
                }

                // version will take two segments
                // each fd will take key segment and value segment
                redis_args args(fds.size() * 2 + 6);
                {
                    args.push("EVALSHA");
                    args.push(db_msg_dispatcher::me()->get_db_script_sha1(hello::EN_DBSST_LOGIN));
                    args.push(1);
                    args.push(user_key);
                }

                std::stringstream segs_debug_info;
                int res = ::rpc::db::pack_message(rsp, args, fds, &version, &segs_debug_info);
                if (res < 0) {
                    return res;
                }

                uint64_t rpc_sequence = 0;
                res = db_msg_dispatcher::me()->send_msg(db_msg_dispatcher::channel_t::CLUSTER_DEFAULT, user_key, static_cast<size_t>(writen_len),
                                                        task->get_id(), logic_config::me()->get_self_bus_id(), detail::unpack_login, rpc_sequence,
                                                        static_cast<int>(args.size()), args.get_args_values(), args.get_args_lengths());

                // args unavailable now

                if (res < 0) {
                    return res;
                }

                hello::table_all_message table_msg;
                // 协程操作
                res = rpc::wait(table_msg, rpc_sequence);
                if (res < 0) {
                    if (hello::err::EN_DB_OLD_VERSION == res && !table_msg.version().empty()) {
                        version.assign(table_msg.version());
                    }

                    return res;
                }

                if (table_msg.version().empty()) {
                    return hello::err::EN_DB_RECORD_NOT_FOUND;
                }

                version.assign(table_msg.version());

                WLOGINFO("table_login [openid=%s] all saved, new version: %s, detail: %s", openid, version.c_str(), segs_debug_info.str().c_str());
                return hello::err::EN_SUCCESS;
            }
        } // namespace login
    }     // namespace db
} // namespace rpc