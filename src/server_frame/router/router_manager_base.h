// Copyright 2021 atframework
// Created by owent on 2018/05/01.
//

#pragma once

#include <memory>

#include "router/router_object_base.h"

/**
 * @brief 路由管理器基类
 */
class UTIL_SYMBOL_VISIBLE router_manager_base {
 public:
  /**
   * @brief 路由对象的键类型
   */
  using key_t = router_object_base::key_t;

 protected:
  /**
   * @brief 构造函数
   * @param type_id 路由管理器类型ID
   */
  SERVER_FRAME_API explicit router_manager_base(uint32_t type_id);

 public:
  /**
   * @brief 析构函数
   */
  SERVER_FRAME_API virtual ~router_manager_base();

  /**
   * @brief 获取路由管理器名称
   * @return 路由管理器名称
   */
  SERVER_FRAME_API virtual const char *name() const = 0;

  /**
   * @brief 获取路由管理器类型ID
   * @return 路由管理器类型ID
   */
  UTIL_FORCEINLINE uint32_t get_type_id() const { return type_id_; }

  /**
   * @brief 获取基础缓存
   * @param key 路由对象的键
   * @return 路由对象的共享指针
   */
  SERVER_FRAME_API virtual std::shared_ptr<router_object_base> get_base_cache(const key_t &key) const = 0;

  /**
   * @brief 可变缓存
   * @param ctx RPC上下文
   * @param out 输出的路由对象共享指针
   * @param key 路由对象的键
   * @param priv_data 私有数据
   * @return RPC结果代码
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type mutable_cache(rpc::context &ctx,
                                                                               std::shared_ptr<router_object_base> &out,
                                                                               const key_t &key, void *priv_data);

  /**
   * @brief 可变缓存（带IO任务保护）
   * @param ctx RPC上下文
   * @param out 输出的路由对象共享指针
   * @param key 路由对象的键
   * @param priv_data 私有数据
   * @param io_guard IO任务保护
   * @return RPC结果代码
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API virtual rpc::result_code_type mutable_cache(
      rpc::context &ctx, std::shared_ptr<router_object_base> &out, const key_t &key, void *priv_data,
      router_object_base::io_task_guard &io_guard) = 0;

  /**
   * @brief 可变对象
   * @param ctx RPC上下文
   * @param out 输出的路由对象共享指针
   * @param key 路由对象的键
   * @param priv_data 私有数据
   * @return RPC结果代码
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type mutable_object(
      rpc::context &ctx, std::shared_ptr<router_object_base> &out, const key_t &key, void *priv_data);

  /**
   * @brief 可变对象（带IO任务保护）
   * @param ctx RPC上下文
   * @param out 输出的路由对象共享指针
   * @param key 路由对象的键
   * @param priv_data 私有数据
   * @param io_guard IO任务保护
   * @return RPC结果代码
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API virtual rpc::result_code_type mutable_object(
      rpc::context &ctx, std::shared_ptr<router_object_base> &out, const key_t &key, void *priv_data,
      router_object_base::io_task_guard &io_guard) = 0;

  /**
   * @brief 移除缓存
   * @param ctx RPC上下文
   * @param key 路由对象的键
   * @param cache 路由对象的共享指针
   * @param priv_data 私有数据
   * @return RPC结果代码
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type remove_cache(rpc::context &ctx, const key_t &key,
                                                                              std::shared_ptr<router_object_base> cache,
                                                                              void *priv_data);

  /**
   * @brief 移除缓存（带IO任务保护）
   * @param ctx RPC上下文
   * @param key 路由对象的键
   * @param cache 路由对象的共享指针
   * @param priv_data 私有数据
   * @param io_guard IO任务保护
   * @return RPC结果代码
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API virtual rpc::result_code_type remove_cache(
      rpc::context &ctx, const key_t &key, std::shared_ptr<router_object_base> cache, void *priv_data,
      router_object_base::io_task_guard &io_guard) = 0;

  /**
   * @brief 移除对象
   * @param ctx RPC上下文
   * @param key 路由对象的键
   * @param cache 路由对象的共享指针
   * @param priv_data 私有数据
   * @return RPC结果代码
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type remove_object(
      rpc::context &ctx, const key_t &key, std::shared_ptr<router_object_base> cache, void *priv_data);

  /**
   * @brief 移除对象（带IO任务保护）
   * @param ctx RPC上下文
   * @param key 路由对象的键
   * @param cache 路由对象的共享指针
   * @param priv_data 私有数据
   * @param io_guard IO任务保护
   * @return RPC结果代码
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API virtual rpc::result_code_type remove_object(
      rpc::context &ctx, const key_t &key, std::shared_ptr<router_object_base> cache, void *priv_data,
      router_object_base::io_task_guard &io_guard) = 0;

  /**
   * @brief 判断是否自动可变对象
   * @return 是否自动可变对象
   */
  SERVER_FRAME_API virtual bool is_auto_mutable_object() const;

  /**
   * @brief 判断是否自动可变缓存
   * @return 是否自动可变缓存
   */
  SERVER_FRAME_API virtual bool is_auto_mutable_cache() const;

  /**
   * @brief 获取默认路由服务器ID
   * @param key 路由对象的键
   * @return 默认路由服务器ID
   */
  SERVER_FRAME_API virtual uint64_t get_default_router_server_id(const key_t &key) const;

  /**
   * @brief 发送消息
   * @param ctx RPC上下文
   * @param obj 路由对象
   * @param msg 消息
   * @param sequence 消息序列号
   * @return RPC结果代码
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type send_msg(rpc::context &ctx, router_object_base &obj,
                                                                          atframework::SSMsg &&msg, uint64_t &sequence);

  /**
   * @brief 发送消息
   * @param ctx RPC上下文
   * @param key 路由对象的键
   * @param msg 消息
   * @param sequence 消息序列号
   * @return RPC结果代码
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type send_msg(rpc::context &ctx, const key_t &key,
                                                                          atframework::SSMsg &&msg, uint64_t &sequence);

  /**
   * @brief 获取路由管理器大小
   * @return 路由管理器大小
   */
  UTIL_FORCEINLINE size_t size() const { return stat_size_; }

  /**
   * @brief 判断是否正在关闭
   * @return 是否正在关闭
   */
  UTIL_FORCEINLINE bool is_closing() const { return is_closing_; }

  /**
   * @brief 停止路由管理器
   */
  SERVER_FRAME_API virtual void on_stop();

  /**
   * @brief 拉取在线服务器
   * @param ctx RPC上下文
   * @param key 路由对象的键
   * @param router_svr_id 路由服务器ID
   * @param router_svr_ver 路由服务器版本
   * @return RPC结果代码
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API virtual rpc::result_code_type pull_online_server(rpc::context &ctx,
                                                                                            const key_t &key,
                                                                                            uint64_t &router_svr_id,
                                                                                            uint64_t &router_svr_ver);

  /**
   * @brief 获取可变的路由管理器指标数据
   * @return 路由管理器指标数据的共享指针
   */
  SERVER_FRAME_API std::shared_ptr<router_manager_metrics_data> mutable_metrics_data();

 protected:
  /**
   * @brief 发送原始消息
   * @param ctx RPC上下文
   * @param obj 路由对象
   * @param msg 消息
   * @param sequence 消息序列号
   * @return RPC结果代码
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type send_msg_raw(rpc::context &ctx,
                                                                              router_object_base &obj,
                                                                              atframework::SSMsg &&msg,
                                                                              uint64_t &sequence);

 protected:
  size_t stat_size_;  ///< 路由管理器大小

 private:
  uint32_t type_id_;  ///< 路由管理器类型ID
  bool is_closing_;   ///< 是否正在关闭
};
