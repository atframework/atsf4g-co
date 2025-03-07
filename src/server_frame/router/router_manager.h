// Copyright 2021 atframework
// Created by owent on 2018-05-01.
//

#pragma once

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <nostd/function_ref.h>

#include <libcotask/task.h>

#include <utility/random_engine.h>

#include <config/logic_config.h>
#include <log/log_wrapper.h>

#include <rpc/router/routerservice.h>
#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_shared_message.h>
#include <rpc/rpc_utils.h>

#include <dispatcher/task_manager.h>

#include <functional>
#include <list>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "router/router_manager_base.h"
#include "router/router_manager_set.h"

/**
 * @brief 路由管理器类模板
 *
 * @tparam TCache 缓存类型
 * @tparam TObj 对象类型
 * @tparam TPrivData 私有数据类型
 */
template <typename TCache, typename TObj, typename TPrivData>
class UTIL_SYMBOL_VISIBLE router_manager : public router_manager_base {
 public:
  using cache_t = TCache;
  using priv_data_t = TPrivData;
  using self_type = router_manager<TCache, TObj, TPrivData>;
  using key_t = typename cache_t::key_t;
  using flag_t = typename cache_t::flag_t;
  using object_ptr_t = typename cache_t::object_ptr_t;
  using ptr_t = typename cache_t::ptr_t;

  using remove_fn_t =
      std::function<rpc::result_code_type(rpc::context &, self_type &, const key_t &, const ptr_t &, priv_data_t)>;
  using pull_fn_t = std::function<rpc::result_code_type(rpc::context &, self_type &, const ptr_t &, priv_data_t)>;

  using store_ptr_t = std::weak_ptr<cache_t>;

 public:
  /**
   * @brief 构造函数
   *
   * @param type_id 类型ID
   */
  SERVER_FRAME_API explicit router_manager(uint32_t type_id) : router_manager_base(type_id) {}

  /**
   * @brief 获取基础缓存对象，如果不存在返回空
   *
   * @param key 缓存对象的键
   * @return std::shared_ptr<router_object_base> 缓存对象
   */
  SERVER_FRAME_API std::shared_ptr<router_object_base> get_base_cache(const key_t &key) const override {
    typename std::unordered_map<key_t, ptr_t>::const_iterator iter = caches_.find(key);
    if (iter == caches_.end()) {
      return nullptr;
    }

    return std::static_pointer_cast<router_object_base>(get_cache(key));
  }

  /**
   * @brief 停止路由管理器
   */
  SERVER_FRAME_API void on_stop() override {
    router_manager_base::on_stop();

    // unbind LRU timer
    for (typename std::unordered_map<key_t, ptr_t>::iterator iter = caches_.begin(); iter != caches_.end(); ++iter) {
      if (iter->second) {
        iter->second->unset_timer_ref();
      }
    }
  }

  /**
   * @brief 获取缓存对象
   *
   * @param key 缓存对象的键
   * @return ptr_t 缓存对象指针
   */
  SERVER_FRAME_API ptr_t get_cache(const key_t &key) const {
    typename std::unordered_map<key_t, ptr_t>::const_iterator iter = caches_.find(key);
    if (iter == caches_.end()) {
      return nullptr;
    }

    return iter->second;
  }

  /**
   * @brief 获取可写的对象
   *
   * @param key 对象的键
   * @return ptr_t 对象指针
   */
  SERVER_FRAME_API ptr_t get_object(const key_t &key) const {
    typename std::unordered_map<key_t, ptr_t>::const_iterator iter = caches_.find(key);
    if (iter == caches_.end() || !iter->second->is_writable()) {
      return nullptr;
    }

    return iter->second;
  }

  using router_manager_base::mutable_cache;

  /**
   * @brief 获取可变缓存对象
   *
   * @param ctx RPC上下文
   * @param out 输出缓存对象
   * @param key 缓存对象的键
   * @param priv_data 私有数据
   * @param io_guard IO任务保护
   * @return rpc::result_code_type 结果代码
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type mutable_cache(
      rpc::context &ctx, std::shared_ptr<router_object_base> &out, const key_t &key, void *priv_data,
      router_object_base::io_task_guard &io_guard) override {
    ptr_t outc;
    auto ret = RPC_AWAIT_CODE_RESULT(mutable_cache(ctx, outc, key, reinterpret_cast<priv_data_t>(priv_data), io_guard));
    out = std::static_pointer_cast<router_object_base>(outc);
    RPC_RETURN_CODE(ret);
  }

  /**
   * @brief 获取可变缓存对象
   *
   * @param ctx RPC上下文
   * @param out 输出缓存对象
   * @param key 缓存对象的键
   * @param priv_data 私有数据
   * @return rpc::result_code_type 结果代码
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type mutable_cache(rpc::context &ctx, ptr_t &out,
                                                                               const key_t &key,
                                                                               priv_data_t priv_data) {
    router_object_base::io_task_guard io_guard;
    auto ret = RPC_AWAIT_CODE_RESULT(mutable_cache(ctx, out, key, reinterpret_cast<priv_data_t>(priv_data), io_guard));
    RPC_RETURN_CODE(ret);
  }

  /**
   * @brief 获取可变缓存对象
   *
   * @param ctx RPC上下文
   * @param out 输出缓存对象
   * @param key 缓存对象的键
   * @param priv_data 私有数据
   * @param io_guard IO任务保护
   * @return rpc::result_code_type 结果代码
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type mutable_cache(
      rpc::context &ctx, ptr_t &out, const key_t &key, priv_data_t priv_data,
      router_object_base::io_task_guard &io_guard) {
    size_t left_ttl = logic_config::me()->get_cfg_router().retry_max_ttl();
    for (; left_ttl > 0; --left_ttl) {
      int res;
      out = get_cache(key);
      if (!out) {
        out = atfw::memory::stl::make_shared<cache_t>(key);
        if (!out) {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
        }

        if (!insert(key, out)) {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
        }
      } else {
        // 先等待IO任务完成，完成后可能在其他任务里已经拉取完毕了。
        res = RPC_AWAIT_CODE_RESULT(io_guard.take(ctx, *out));
        if (res < 0) {
          RPC_RETURN_CODE(res);
        }

        if (out->is_cache_available()) {
          // 触发拉取实体并命中cache时要取消移除缓存和降级的计划任务
          out->unset_flag(router_object_base::flag_t::EN_ROFT_SCHED_REMOVE_CACHE);
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
        }
      }

      // pull using TYPE's API
      res = RPC_AWAIT_CODE_RESULT(out->internal_pull_cache(ctx, reinterpret_cast<void *>(priv_data), io_guard));

      if (res < 0) {
        if (res < 0) {
          switch (res) {
            case PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT:
            case PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED:
            case PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED:
            case PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND:
            case PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_EXITING:
            case PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK:
            case PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND:
            case PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND:
              RPC_RETURN_CODE(res);
            case PROJECT_NAMESPACE_ID::err::EN_ROUTER_EAGAIN: {
              time_t wait_interval_ms =
                  static_cast<time_t>(logic_config::me()->get_cfg_router().cache_retry_interval().seconds() * 1000 +
                                      logic_config::me()->get_cfg_router().cache_retry_interval().nanos() / 1000000);
              if (wait_interval_ms <= 0) {
                wait_interval_ms = 512;
              }

              RPC_AWAIT_IGNORE_RESULT(rpc::wait(ctx, std::chrono::milliseconds{util::random_engine::random_between(
                                                         wait_interval_ms / 2, wait_interval_ms)}));
              break;
            }
            default:
              break;
          }
          continue;
        }
        continue;
      }

      RPC_AWAIT_IGNORE_RESULT(on_evt_pull_cache(ctx, out, priv_data));
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }

    // 超出重试次数限制
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_TTL_EXTEND);
  }

  /**
   * @brief 检查缓存是否有效，如果过期则尝试拉取一次
   *
   * @param ctx RPC上下文
   * @param in 输入保存的对象
   * @param out 输出对象指针
   * @param key 重新拉取缓存时的键
   * @param priv_data 私有数据
   * @return rpc::result_code_type 结果代码
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type renew_cache(rpc::context &ctx, store_ptr_t &in,
                                                                             ptr_t &out, const key_t &key,
                                                                             priv_data_t priv_data) {
    if (!in.expired()) {
      out = in.lock();
    } else {
      out = nullptr;
    }

    if (out && !out->check_flag(flag_t::EN_ROFT_CACHE_REMOVED)) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }

    auto ret = RPC_AWAIT_CODE_RESULT(mutable_cache(ctx, out, key, priv_data));
    if (ret >= 0) {
      in = out;
    }

    RPC_RETURN_CODE(ret);
  }

  using router_manager_base::mutable_object;

  /**
   * @brief 获取可变对象
   *
   * @param ctx RPC上下文
   * @param out 输出对象
   * @param key 对象的键
   * @param priv_data 私有数据
   * @param io_guard IO任务保护
   * @return rpc::result_code_type 结果代码
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type mutable_object(
      rpc::context &ctx, std::shared_ptr<router_object_base> &out, const key_t &key, void *priv_data,
      router_object_base::io_task_guard &io_guard) override {
    ptr_t outc;
    auto ret =
        RPC_AWAIT_CODE_RESULT(mutable_object(ctx, outc, key, reinterpret_cast<priv_data_t>(priv_data), io_guard));
    out = std::static_pointer_cast<router_object_base>(outc);
    RPC_RETURN_CODE(ret);
  }

  /**
   * @brief 获取可变对象
   *
   * @param ctx RPC上下文
   * @param out 输出对象
   * @param key 对象的键
   * @param priv_data 私有数据
   * @return rpc::result_code_type 结果代码
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type mutable_object(rpc::context &ctx, ptr_t &out,
                                                                                const key_t &key,
                                                                                priv_data_t priv_data) {
    router_object_base::io_task_guard io_guard;
    auto ret = RPC_AWAIT_CODE_RESULT(mutable_object(ctx, out, key, priv_data, io_guard));
    RPC_RETURN_CODE(ret);
  }

  /**
   * @brief 获取可变对象
   *
   * @param ctx RPC上下文
   * @param out 输出对象
   * @param key 对象的键
   * @param priv_data 私有数据
   * @param io_guard IO任务保护
   * @return rpc::result_code_type 结果代码
   */
  EXPLICIT_NODISCARD_ATTR SERVER_FRAME_API rpc::result_code_type mutable_object(
      rpc::context &ctx, ptr_t &out, const key_t &key, priv_data_t priv_data,
      router_object_base::io_task_guard &io_guard) {
    size_t left_ttl = logic_config::me()->get_cfg_router().retry_max_ttl();
    for (; left_ttl > 0; --left_ttl) {
      rpc::result_code_type::value_type res;
      out = get_cache(key);
      if (!out) {
        out = atfw::memory::stl::make_shared<cache_t>(key);
        if (!out) {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
        }

        if (!insert(key, out)) {
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
        }
      } else {
        // 先等待IO任务完成，完成后可能在其他任务里已经拉取完毕了。
        res = RPC_AWAIT_CODE_RESULT(io_guard.take(ctx, *out));
        if (res < 0) {
          RPC_RETURN_CODE(res);
        }
        if (out->is_object_available()) {
          // 触发拉取实体并命中cache时要取消移除缓存和降级的计划任务
          out->unset_flag(router_object_base::flag_t::EN_ROFT_FORCE_REMOVE_OBJECT);
          out->unset_flag(router_object_base::flag_t::EN_ROFT_SCHED_REMOVE_OBJECT);
          out->unset_flag(router_object_base::flag_t::EN_ROFT_SCHED_REMOVE_CACHE);
          RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
        }
      }

      // 如果处于正在关闭的状态，则不允许创建新的实体，只能访问缓存
      if (is_closing()) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_CLOSING);
      }

      // pull using TYPE's API
      res = RPC_AWAIT_CODE_RESULT(out->internal_pull_object(ctx, reinterpret_cast<void *>(priv_data), io_guard));

      if (res < 0) {
        switch (res) {
          case PROJECT_NAMESPACE_ID::err::EN_SYS_TIMEOUT:
          case PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_KILLED:
          case PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_CANCELLED:
          case PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND:
          case PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_TASK_EXITING:
          case PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK:
          case PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND:
          case PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND:
          case PROJECT_NAMESPACE_ID::err::EN_ROUTER_BUSSINESS_VERSION_DENY:
            RPC_RETURN_CODE(res);
          case PROJECT_NAMESPACE_ID::err::EN_ROUTER_EAGAIN: {
            time_t wait_interval_ms =
                static_cast<time_t>(logic_config::me()->get_cfg_router().object_retry_interval().seconds() * 1000 +
                                    logic_config::me()->get_cfg_router().object_retry_interval().nanos() / 1000000);
            if (wait_interval_ms <= 0) {
              wait_interval_ms = 512;
            }

            RPC_AWAIT_IGNORE_RESULT(rpc::wait(ctx, std::chrono::milliseconds{util::random_engine::random_between(
                                                       wait_interval_ms / 2, wait_interval_ms)}));
            break;
          }
          default:
            break;
        }
        continue;
      }

      // 如果中途被移除，则降级回缓存
      if (!out->check_flag(router_object_base::flag_t::EN_ROFT_CACHE_REMOVED)) {
        RPC_AWAIT_IGNORE_RESULT(on_evt_pull_object(ctx, out, priv_data));
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
      }
    }

    // 超出重试次数限制
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_TTL_EXTEND);
  }

  /**
   * @brief 转移对象
   *
   * @param ctx RPC上下文
   * @param key 对象的键
   * @param svr_id 服务器ID
   * @param need_notify 是否需要通知
   * @param priv_data 私有数据
   * @return rpc::result_code_type 结果代码
   */
  EXPLICIT_NODISCARD_ATTR UTIL_SYMBOL_VISIBLE virtual rpc::result_code_type transfer(rpc::context &ctx,
                                                                                     const key_t &key, uint64_t svr_id,
                                                                                     bool need_notify,
                                                                                     priv_data_t priv_data) {
    ptr_t obj;
    auto ret = RPC_AWAIT_CODE_RESULT(mutable_object(ctx, obj, key, priv_data));
    if (ret < 0 || !obj) {
      RPC_RETURN_CODE(ret);
    }

    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(transfer(ctx, obj, svr_id, need_notify, priv_data)));
  }

  /**
   * @brief 转移对象
   *
   * @param ctx RPC上下文
   * @param obj 对象指针
   * @param svr_id 服务器ID
   * @param need_notify 是否需要通知
   * @param priv_data 私有数据
   * @return rpc::result_code_type 结果代码
   */
  EXPLICIT_NODISCARD_ATTR UTIL_SYMBOL_VISIBLE virtual rpc::result_code_type transfer(rpc::context &ctx,
                                                                                     const ptr_t &obj, uint64_t svr_id,
                                                                                     bool need_notify,
                                                                                     priv_data_t priv_data) {
    if (!obj) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
    }

    if (svr_id == obj->get_router_server_id()) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }

    // 先等待其他IO任务完成
    router_object_base::io_task_guard io_guard;
    auto ret = RPC_AWAIT_CODE_RESULT(io_guard.take(ctx, *obj));
    if (ret < 0) {
      RPC_RETURN_CODE(ret);
    }

    if (!obj->is_writable()) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_WRITABLE);
    }

    if (svr_id == obj->get_router_server_id()) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
    }

    // 正在转移中
    router_object_base::flag_guard transfer_flag(*obj, router_object_base::flag_t::EN_ROFT_TRANSFERING);
    if (!transfer_flag) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_TRANSFER);
    }

    router_object_base::flag_guard remove_object_flag(*obj, router_object_base::flag_t::EN_ROFT_REMOVING_OBJECT);

    RPC_AWAIT_IGNORE_RESULT(on_evt_remove_object(ctx, obj->get_key(), obj, reinterpret_cast<priv_data_t>(priv_data)));

    // 保存到数据库
    ret = RPC_AWAIT_CODE_RESULT(obj->remove_object(ctx, reinterpret_cast<void *>(priv_data), svr_id, io_guard));
    // 数据库失败要强制拉取
    if (ret < 0) {
      obj->unset_flag(router_object_base::flag_t::EN_ROFT_IS_OBJECT);

      // 如果转发不成功，要回发执行失败
      if (!obj->get_transfer_pending_list().empty()) {
        std::list<atframework::SSMsg> all_msgs;
        all_msgs.swap(obj->get_transfer_pending_list());

        for (atframework::SSMsg &msg : all_msgs) {
          obj->send_transfer_msg_failed(std::move(msg));
        }
      }

      RPC_RETURN_CODE(ret);
    }

    RPC_AWAIT_IGNORE_RESULT(on_evt_object_removed(ctx, obj->get_key(), obj, reinterpret_cast<priv_data_t>(priv_data)));

    if (0 != svr_id && need_notify) {
      // 如果目标不是0则通知目标服务器
      auto req = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSRouterTransferReq>(ctx);
      auto rsp = rpc::make_shared_message<PROJECT_NAMESPACE_ID::SSRouterTransferRsp>(ctx);
      atframework::SSRouterHead *router_head = req->mutable_object();
      if (nullptr != router_head) {
        router_head->set_router_source_node_id(obj->get_router_server_id());
        router_head->set_router_source_node_name(obj->get_router_server_name());
        router_head->set_router_version(obj->get_router_version());

        router_head->set_object_inst_id(obj->get_key().object_id);
        router_head->set_object_type_id(get_type_id());
        router_head->set_object_zone_id(obj->get_key().zone_id);

        // 转移通知RPC也需要设置为IO任务，这样如果有其他的读写任务或者转移任务都会等本任务完成
        ret = RPC_AWAIT_CODE_RESULT(rpc::router::router_transfer(ctx, svr_id, *req, *rsp));
        if (ret < 0) {
          FWLOGERROR("transfer router object (type={},zone_id={}) {} failed, res: {}", get_type_id(),
                     obj->get_key().zone_id, obj->get_key().object_id, ret);
        }
      } else {
        ret = PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
      }

      if (ret < 0) {
        RPC_RETURN_CODE(ret);
      }
    }

    // 如果转发成功，要转发这期间收到的消息
    if (ret >= 0) {
      while (!obj->get_transfer_pending_list().empty()) {
        std::list<atframework::SSMsg> all_msgs;
        all_msgs.swap(obj->get_transfer_pending_list());

        uint64_t rpc_sequence;
        for (atframework::SSMsg &msg : all_msgs) {
          auto res = RPC_AWAIT_CODE_RESULT(send_msg_raw(ctx, *obj, std::move(msg), rpc_sequence));
          if (res < 0) {
            FWLOGERROR("transfer router object (type={},zone_id={}) {} message failed, res: {}", get_type_id(),
                       obj->get_key().zone_id, obj->get_key().object_id, res);
          } else {
            obj->send_transfer_msg_failed(std::move(msg));
          }
        }
      }
    } else {
      // 如果转发不成功，要回发执行失败
      if (!obj->get_transfer_pending_list().empty()) {
        std::list<atframework::SSMsg> all_msgs;
        all_msgs.swap(obj->get_transfer_pending_list());

        for (atframework::SSMsg &msg : all_msgs) {
          obj->send_transfer_msg_failed(std::move(msg));
        }
      }
    }

    RPC_RETURN_CODE(ret);
  }

  using router_manager_base::remove_cache;

  /**
   * @brief 移除缓存
   *
   * @param ctx RPC上下文
   * @param key 缓存的键
   * @param cache 缓存对象
   * @param priv_data 私有数据
   * @param io_guard IO任务保护
   * @return rpc::result_code_type 结果代码
   */
  EXPLICIT_NODISCARD_ATTR UTIL_SYMBOL_VISIBLE rpc::result_code_type remove_cache(
      rpc::context &ctx, const key_t &key, std::shared_ptr<router_object_base> cache, void *priv_data,
      router_object_base::io_task_guard &io_guard) override {
    ptr_t cache_child;
    rpc::result_code_type::value_type ret = PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
    {
      typename std::unordered_map<key_t, ptr_t>::iterator iter;
      if (cache) {
        iter = caches_.find(cache->get_key());
      } else {
        iter = caches_.find(key);
      }

      if (iter == caches_.end()) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND);
      }

      if (cache && iter->second != cache) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND);
      }
      cache_child = iter->second;

      if (cache_child->is_writable()) {
        ret = RPC_AWAIT_CODE_RESULT(remove_object(ctx, key, cache_child, priv_data, io_guard));
        if (ret < 0) {
          RPC_RETURN_CODE(ret);
        }
      }
    }

    ret = RPC_AWAIT_CODE_RESULT(io_guard.take(ctx, *cache_child));
    if (ret < 0) {
      RPC_RETURN_CODE(ret);
    }

    router_object_base::flag_guard remove_cache_flag(*cache_child, router_object_base::flag_t::EN_ROFT_REMOVING_CACHE);

    // After RPC, iter may be different
    bool trigger_evt = !cache_child->check_flag(router_object_base::flag_t::EN_ROFT_CACHE_REMOVED);
    if (trigger_evt) {
      RPC_AWAIT_IGNORE_RESULT(on_evt_remove_cache(ctx, key, cache_child, reinterpret_cast<priv_data_t>(priv_data)));
      cache_child->set_flag(router_object_base::flag_t::EN_ROFT_CACHE_REMOVED);
    }

    // Double check the value of iterator
    {
      typename std::unordered_map<key_t, ptr_t>::iterator iter;
      iter = caches_.find(cache_child->get_key());
      if (iter != caches_.end() && cache_child == iter->second) {
        caches_.erase(iter);
      }
    }
    stat_size_ = caches_.size();

    std::shared_ptr<router_manager_metrics_data> metrics_data = mutable_metrics_data();
    if (metrics_data) {
      metrics_data->cache_count.store(static_cast<int64_t>(stat_size_), std::memory_order_release);
    }

    if (trigger_evt) {
      RPC_AWAIT_IGNORE_RESULT(on_evt_cache_removed(ctx, key, cache_child, reinterpret_cast<priv_data_t>(priv_data)));
    }
    RPC_RETURN_CODE(ret);
  }

  using router_manager_base::remove_object;

  /**
   * @brief 移除对象
   *
   * @param ctx RPC上下文
   * @param key 对象的键
   * @param cache 缓存对象
   * @param priv_data 私有数据
   * @param io_guard IO任务保护
   * @return rpc::result_code_type 结果代码
   */
  EXPLICIT_NODISCARD_ATTR UTIL_SYMBOL_VISIBLE rpc::result_code_type remove_object(
      rpc::context &ctx, const key_t &key, std::shared_ptr<router_object_base> cache, void *priv_data,
      router_object_base::io_task_guard &io_guard) override {
    ptr_t cache_child;
    if (!cache) {
      typename std::unordered_map<key_t, ptr_t>::iterator iter = caches_.find(key);
      if (iter == caches_.end()) {
        RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND);
      }

      cache_child = iter->second;
      cache = std::static_pointer_cast<router_object_base>(iter->second);
      assert(!!cache);
    } else {
      cache_child = std::static_pointer_cast<cache_t>(cache);
      assert(!!cache_child);
    }

    if (!cache_child) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_FOUND);
    }

    router_object_base::flag_guard remove_object_flag(*cache_child,
                                                      router_object_base::flag_t::EN_ROFT_REMOVING_OBJECT);

    auto ret = RPC_AWAIT_CODE_RESULT(io_guard.take(ctx, *cache_child));
    if (ret < 0) {
      RPC_RETURN_CODE(ret);
    }

    RPC_AWAIT_IGNORE_RESULT(on_evt_remove_object(ctx, key, cache_child, reinterpret_cast<priv_data_t>(priv_data)));

    ret = RPC_AWAIT_CODE_RESULT(cache_child->remove_object(ctx, reinterpret_cast<priv_data_t>(priv_data), 0, io_guard));
    if (ret < 0) {
      RPC_RETURN_CODE(ret);
    }

    RPC_AWAIT_IGNORE_RESULT(on_evt_object_removed(ctx, key, cache_child, reinterpret_cast<priv_data_t>(priv_data)));
    RPC_RETURN_CODE(ret);
  }

  /**
   * @brief 设置移除对象事件处理函数
   *
   * @param fn 事件处理函数
   */
  UTIL_FORCEINLINE void set_on_remove_object(remove_fn_t &&fn) { handle_on_remove_object_ = std::move(fn); }

  /**
   * @brief 设置移除对象事件处理函数
   *
   * @param fn 事件处理函数
   */
  UTIL_FORCEINLINE void set_on_remove_object(const remove_fn_t &fn) { handle_on_remove_object_ = fn; }

  /**
   * @brief 设置对象移除完成事件处理函数
   *
   * @param fn 事件处理函数
   */
  UTIL_FORCEINLINE void set_on_object_removed(remove_fn_t &&fn) { handle_on_object_removed_ = std::move(fn); }

  /**
   * @brief 设置对象移除完成事件处理函数
   *
   * @param fn 事件处理函数
   */
  UTIL_FORCEINLINE void set_on_object_removed(const remove_fn_t &fn) { handle_on_object_removed_ = fn; }

  /**
   * @brief 设置缓存移除完成事件处理函数
   *
   * @param fn 事件处理函数
   */
  UTIL_FORCEINLINE void set_on_cache_removed(remove_fn_t &&fn) { handle_on_cache_removed_ = std::move(fn); }

  /**
   * @brief 设置缓存移除完成事件处理函数
   *
   * @param fn 事件处理函数
   */
  UTIL_FORCEINLINE void set_on_cache_removed(const remove_fn_t &fn) { handle_on_cache_removed_ = fn; }

  /**
   * @brief 设置移除缓存事件处理函数
   *
   * @param fn 事件处理函数
   */
  UTIL_FORCEINLINE void set_on_remove_cache(remove_fn_t &&fn) { handle_on_remove_cache_ = std::move(fn); }

  /**
   * @brief 设置移除缓存事件处理函数
   *
   * @param fn 事件处理函数
   */
  UTIL_FORCEINLINE void set_on_remove_cache(const remove_fn_t &fn) { handle_on_remove_cache_ = fn; }

  /**
   * @brief 设置拉取对象事件处理函数
   *
   * @param fn 事件处理函数
   */
  UTIL_FORCEINLINE void set_on_pull_object(pull_fn_t &&fn) { handle_on_pull_object_ = std::move(fn); }

  /**
   * @brief 设置拉取对象事件处理函数
   *
   * @param fn 事件处理函数
   */
  UTIL_FORCEINLINE void set_on_pull_object(const pull_fn_t &fn) { handle_on_pull_object_ = fn; }

  /**
   * @brief 设置拉取缓存事件处理函数
   *
   * @param fn 事件处理函数
   */
  UTIL_FORCEINLINE void set_on_pull_cache(pull_fn_t &&fn) { handle_on_pull_cache_ = std::move(fn); }

  /**
   * @brief 设置拉取缓存事件处理函数
   *
   * @param fn 事件处理函数
   */
  UTIL_FORCEINLINE void set_on_pull_cache(const pull_fn_t &fn) { handle_on_pull_cache_ = fn; }

  /**
   * @brief 遍历所有缓存对象
   *
   * @param fn 遍历函数
   */
  UTIL_SYMBOL_VISIBLE void foreach_cache(util::nostd::function_ref<bool(const ptr_t &)> fn) {
    // 先复制出所有的只能指针，防止回掉过程中成员变化带来问题
    std::vector<ptr_t> res;
    res.reserve(caches_.size());
    for (typename std::unordered_map<key_t, ptr_t>::iterator iter = caches_.begin(); iter != caches_.end(); ++iter) {
      if (iter->second && !iter->second->check_flag(router_object_base::flag_t::EN_ROFT_IS_OBJECT)) {
        res.push_back(iter->second);
      }
    }

    for (size_t i = 0; i < res.size(); ++i) {
      if (!fn(res[i])) {
        break;
      }
    }
  }

  /**
   * @brief 遍历所有对象
   *
   * @param fn 遍历函数
   */
  void foreach_object(util::nostd::function_ref<bool(const ptr_t &)> fn) {
    // 先复制出所有的只能指针，防止回掉过程中成员变化带来问题
    std::vector<ptr_t> res;
    res.reserve(caches_.size());
    for (typename std::unordered_map<key_t, ptr_t>::iterator iter = caches_.begin(); iter != caches_.end(); ++iter) {
      if (iter->second && iter->second->check_flag(router_object_base::flag_t::EN_ROFT_IS_OBJECT)) {
        res.push_back(iter->second);
      }
    }

    for (size_t i = 0; i < res.size(); ++i) {
      if (!fn(res[i])) {
        break;
      }
    }
  }

 private:
  /**
   * @brief 插入缓存对象
   *
   * @param key 缓存对象的键
   * @param d 缓存对象指针
   * @return true 插入成功
   * @return false 插入失败
   */
  bool insert(const key_t &key, const ptr_t &d) {
    if (!d || caches_.find(key) != caches_.end()) {
      return false;
    }

    // 插入定时器
    if (!router_manager_set::me()->insert_timer(this, d)) {
      return false;
    }

    caches_[key] = d;
    stat_size_ = caches_.size();

    std::shared_ptr<router_manager_metrics_data> metrics_data = mutable_metrics_data();
    if (metrics_data) {
      metrics_data->cache_count.store(static_cast<int64_t>(stat_size_), std::memory_order_release);
    }

    return true;
  }

 protected:
  /**
   * @brief 移除缓存事件处理函数
   *
   * @param ctx RPC上下文
   * @param key 缓存对象的键
   * @param cache 缓存对象指针
   * @param priv_data 私有数据
   * @return rpc::result_code_type 结果代码
   */
  EXPLICIT_NODISCARD_ATTR virtual rpc::result_code_type on_evt_remove_cache(rpc::context &ctx, const key_t &key,
                                                                            const ptr_t &cache, priv_data_t priv_data) {
    if (handle_on_remove_cache_) {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(handle_on_remove_cache_(ctx, *this, key, cache, priv_data)));
    }
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  /**
   * @brief 缓存移除完成事件处理函数
   *
   * @param ctx RPC上下文
   * @param key 缓存对象的键
   * @param cache 缓存对象指针
   * @param priv_data 私有数据
   * @return rpc::result_code_type 结果代码
   */
  EXPLICIT_NODISCARD_ATTR virtual rpc::result_code_type on_evt_cache_removed(rpc::context &ctx, const key_t &key,
                                                                             const ptr_t &cache,
                                                                             priv_data_t priv_data) {
    if (handle_on_cache_removed_) {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(handle_on_cache_removed_(ctx, *this, key, cache, priv_data)));
    }
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  /**
   * @brief 移除对象事件处理函数
   *
   * @param ctx RPC上下文
   * @param key 对象的键
   * @param cache 缓存对象指针
   * @param priv_data 私有数据
   * @return rpc::result_code_type 结果代码
   */
  EXPLICIT_NODISCARD_ATTR virtual rpc::result_code_type on_evt_remove_object(rpc::context &ctx, const key_t &key,
                                                                             const ptr_t &cache,
                                                                             priv_data_t priv_data) {
    if (handle_on_remove_object_) {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(handle_on_remove_object_(ctx, *this, key, cache, priv_data)));
    }
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  /**
   * @brief 对象移除完成事件处理函数
   *
   * @param ctx RPC上下文
   * @param key 对象的键
   * @param cache 缓存对象指针
   * @param priv_data 私有数据
   * @return rpc::result_code_type 结果代码
   */
  EXPLICIT_NODISCARD_ATTR virtual rpc::result_code_type on_evt_object_removed(rpc::context &ctx, const key_t &key,
                                                                              const ptr_t &cache,
                                                                              priv_data_t priv_data) {
    if (handle_on_object_removed_) {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(handle_on_object_removed_(ctx, *this, key, cache, priv_data)));
    }
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  /**
   * @brief 拉取缓存事件处理函数
   *
   * @param ctx RPC上下文
   * @param cache 缓存对象指针
   * @param priv_data 私有数据
   * @return rpc::result_code_type 结果代码
   */
  EXPLICIT_NODISCARD_ATTR virtual rpc::result_code_type on_evt_pull_cache(rpc::context &ctx, const ptr_t &cache,
                                                                          priv_data_t priv_data) {
    if (handle_on_pull_cache_) {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(handle_on_pull_cache_(ctx, *this, cache, priv_data)));
    }
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

  /**
   * @brief 拉取对象事件处理函数
   *
   * @param ctx RPC上下文
   * @param cache 缓存对象指针
   * @param priv_data 私有数据
   * @return rpc::result_code_type 结果代码
   */
  EXPLICIT_NODISCARD_ATTR virtual rpc::result_code_type on_evt_pull_object(rpc::context &ctx, const ptr_t &cache,
                                                                           priv_data_t priv_data) {
    if (handle_on_pull_object_) {
      RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(handle_on_pull_object_(ctx, *this, cache, priv_data)));
    }
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SUCCESS);
  }

 private:
  remove_fn_t handle_on_remove_cache_;
  remove_fn_t handle_on_cache_removed_;
  remove_fn_t handle_on_remove_object_;
  remove_fn_t handle_on_object_removed_;
  pull_fn_t handle_on_pull_cache_;
  pull_fn_t handle_on_pull_object_;

  std::unordered_map<key_t, ptr_t> caches_;
};
