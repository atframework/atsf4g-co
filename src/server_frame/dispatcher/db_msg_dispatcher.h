//
// Created by owt50 on 2016/9/27.
//

#ifndef DISPATCHER_DB_MSG_DISPATCHER_H
#define DISPATCHER_DB_MSG_DISPATCHER_H

#pragma once

#include <config/compile_optimize.h>
#include <std/functional.h>

#include <config/compiler_features.h>
#include <design_pattern/singleton.h>

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4005)
#endif

#include <uv.h>

#include <hiredis_happ.h>

#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

#include <config/logic_config.h>
#include <rpc/db/unpack.h>

#include "dispatcher_implement.h"

extern "C" struct redisAsyncContext;

namespace hiredis {
namespace happ {
class cluster;
class raw;
class cmd_exec;
class connection;
}  // namespace happ
}  // namespace hiredis

namespace PROJECT_NAMESPACE_ID {
class table_all_message;
}

class db_msg_dispatcher : public dispatcher_implement {
 public:
  using msg_op_type_t = dispatcher_implement::msg_op_type_t;
  using msg_raw_t = dispatcher_implement::msg_raw_t;
  using msg_type_t = dispatcher_implement::msg_type_t;
  using unpack_fn_t = int32_t (*)(PROJECT_NAMESPACE_ID::table_all_message &msg, const redisReply *reply);
  using user_callback_t = std::function<int()>;

  struct ATFW_UTIL_SYMBOL_VISIBLE channel_t {
    enum type {
      CLUSTER_BOUND = 0,
      CLUSTER_DEFAULT,

      SENTINEL_BOUND,

      RAW_DEFAULT,
      RAW_BOUND,

      MAX
    };
  };

  enum class script_type : uint8_t {
    kInvalid = 0,
    kCompareAndSetHashTable = 1,
    kMax  // Unused
  };

#if defined(SERVER_FRAME_API_DLL) && SERVER_FRAME_API_DLL
#  if defined(SERVER_FRAME_API_NATIVE) && SERVER_FRAME_API_NATIVE
  UTIL_DESIGN_PATTERN_SINGLETON_EXPORT_DECL(db_msg_dispatcher)
#  else
  UTIL_DESIGN_PATTERN_SINGLETON_IMPORT_DECL(db_msg_dispatcher)
#  endif
#else
  UTIL_DESIGN_PATTERN_SINGLETON_VISIBLE_DECL(db_msg_dispatcher)
#endif

 private:
  SERVER_FRAME_API db_msg_dispatcher();

 public:
  SERVER_FRAME_API virtual ~db_msg_dispatcher();

  SERVER_FRAME_API int32_t init() override;

  SERVER_FRAME_API const char *name() const override;

  /**
   * @brief run tick handle and return active action number
   * @return active action number or error code
   */
  SERVER_FRAME_API int tick() override;

  /**
   * @brief 获取任务信息
   * @param raw_msg 消息抽象结构
   * @return 相关的任务id
   */
  SERVER_FRAME_API uint64_t pick_msg_task_id(msg_raw_t &raw_msg) override;

  /**
   * @brief 获取消息名称
   * @param raw_msg 消息抽象结构
   * @return 消息类型ID
   */
  SERVER_FRAME_API msg_type_t pick_msg_type_id(msg_raw_t &raw_msg) override;

  /**
   * @brief 获取消息的RPC名字
   * @param raw_msg 消息抽象结构
   * @return 消息的RPC名字,如果不是RPC消息，返回空字符串
   */
  SERVER_FRAME_API const std::string &pick_rpc_name(msg_raw_t &raw_msg) override;

  /**
   * @brief 获取操作类型
   * @param raw_msg 消息抽象结构
   * @note 这只是一个调度曾规范，不强制执行。详情 @see PROJECT_NAMESPACE_ID::EnMsgOpType
   * @return 消息操作类型
   */
  SERVER_FRAME_API msg_op_type_t pick_msg_op_type(msg_raw_t &raw_msg) override;

  SERVER_FRAME_API int32_t dispatch(const void *req, size_t reqsz);

  /**
   * @brief 启动标准的DB数据发送流程
   * @param t 发送的数据库渠道
   * @param ks redis key的内容（仅cluster渠道有效）
   * @param kl redis key的长度（仅cluster渠道有效）
   * @param task_id 任务id
   * @param pd 启动进程pd
   * @param fn 解包函数
   * @param sequence 传出本次RPC的sequence
   * @param argc @see redisAsyncCommandArgv
   * @param argv @see redisAsyncCommandArgv
   * @param argvlen @see redisAsyncCommandArgv
   * @return 0或错误码
   */
  SERVER_FRAME_API int send_msg(channel_t::type t, const char *ks, size_t kl, uint64_t task_id, uint64_t pd,
                                unpack_fn_t fn, uint64_t &sequence, int argc, const char **argv, const size_t *argvlen);

  /**
   * @brief 获取用于protobuf序列化的临时缓冲区
   * @param 缓冲区长度
   * @return 缓冲区真实地址
   */
  SERVER_FRAME_API void *get_cache_buffer(size_t len);

  /**
   * @brief 数据库服务是否有效
   * @return 数据库服务有效返回true
   */
  SERVER_FRAME_API bool is_available(channel_t::type t) const;

  /*
   * @brief 获取表脚本SHA1
   */
  SERVER_FRAME_API const std::string &get_db_script_sha1(script_type type) const;

  /*
   * @brief 设置表脚本SHA1
   * @param 字符串指针
   * @param 字符串长度
   */
  SERVER_FRAME_API void set_db_script_sha1(script_type type, const char *str, int len);

  /*
   * @brief 连接完成时调用用户的设置的回调函数
   */
  SERVER_FRAME_API void set_on_connected(channel_t::type t, user_callback_t fn);

  /**
   * allocate a message sequence
   * @return allocated sequence
   */
  SERVER_FRAME_API uint64_t allocate_sequence();

 private:
  static void log_debug_fn(const char *content);
  static void log_info_fn(const char *content);

  int script_load(redisAsyncContext *c, script_type type);

  // common helper
  static void on_timer_proc(uv_timer_t *handle);
  static void script_callback(redisAsyncContext *c, void *r, void *privdata);

  // cluster
  int cluster_init(const PROJECT_NAMESPACE_ID::config::db_group_cfg &conns, int index);
  static void cluster_request_callback(hiredis::happ::cmd_exec *, struct redisAsyncContext *c, void *r, void *privdata);
  static void cluster_on_connect(hiredis::happ::cluster *, hiredis::happ::connection *);
  static void cluster_on_connected(hiredis::happ::cluster *, hiredis::happ::connection *,
                                   const struct redisAsyncContext *, int status);

  /**
   * @brief 启动标准的DB数据发送流程
   * @param clu 发送的数据库渠道
   * @param ks redis key的内容
   * @param kl redis key的长度
   * @param task_id 任务id
   * @param pd 启动进程pd
   * @param fn 解包函数
   * @param argv @see redisAsyncCommandArgv
   * @param argvlen @see redisAsyncCommandArgv
   * @return 0或错误码
   */

  int cluster_send_msg(hiredis::happ::cluster &clu, const char *ks, size_t kl, uint64_t task_id, uint64_t pd,
                       unpack_fn_t fn, uint64_t &sequence, int argc, const char **argv, const size_t *argvlen);

  // raw
  int raw_init(const PROJECT_NAMESPACE_ID::config::db_group_cfg &conns, int index);
  static void raw_request_callback(hiredis::happ::cmd_exec *, struct redisAsyncContext *c, void *r, void *privdata);
  static void raw_on_connect(hiredis::happ::raw *c, hiredis::happ::connection *);
  static void raw_on_connected(hiredis::happ::raw *c, hiredis::happ::connection *, const struct redisAsyncContext *,
                               int status);

  /**
   * @brief 启动标准的DB数据发送流程
   * @param task_id 任务id
   * @param pd 启动进程pd
   * @param fn 解包函数
   * @param argc @see redisAsyncCommandArgv
   * @param argv @see redisAsyncCommandArgv
   * @param argvlen @see redisAsyncCommandArgv
   * @return 0或错误码
   */
  int raw_send_msg(hiredis::happ::raw &raw_conn, uint64_t task_id, uint64_t pd, unpack_fn_t fn, uint64_t &sequence,
                   int argc, const char **argv, const size_t *argvlen);

 private:
  uint64_t sequence_allocator_;
  uv_timer_t *tick_timer_;
  int tick_msg_count_;
  std::vector<char> pack_cache_;

  // user callbacks
  std::list<user_callback_t> user_callback_onconnected_[channel_t::MAX];

  // script sha1
  std::string db_script_sha1_[static_cast<size_t>(script_type::kMax)];

  // channels
  std::shared_ptr<hiredis::happ::cluster> db_cluster_conns_[channel_t::SENTINEL_BOUND];
  std::shared_ptr<hiredis::happ::raw> db_raw_conns_[channel_t::RAW_BOUND - channel_t::SENTINEL_BOUND];
};

#endif  // ATF4G_CO_DB_MSG_DISPATCHER_H
