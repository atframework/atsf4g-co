// Copyright 2021 atframework
// Created by owent on 2018/05/01.
//

#ifndef ROUTER_ROUTER_MANAGER_H
#define ROUTER_ROUTER_MANAGER_H

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <libcotask/task.h>

#include <config/logic_config.h>
#include <log/log_wrapper.h>

#include <rpc/router/routerservice.h>
#include <rpc/rpc_utils.h>

#include <config/logic_config.h>
#include <dispatcher/task_manager.h>

#include <functional>
#include <unordered_map>
#include <vector>

#include "router_manager_base.h"
#include "router_manager_set.h"

template <typename TCache, typename TObj, typename TPrivData>
class router_manager : public router_manager_base {
 public:
  using cache_t = TCache;
  using priv_data_t = TPrivData;
  using self_type = router_manager<TCache, TObj, TPrivData>;
  using key_t = typename cache_t::key_t;
  using flag_t = typename cache_t::flag_t;
  using object_ptr_t = typename cache_t::object_ptr_t;
  using ptr_t = typename cache_t::ptr_t;

  using remove_fn_t = std::function<void(self_type &, const key_t &, const ptr_t &, priv_data_t)>;
  using pull_fn_t = std::function<void(self_type &, const ptr_t &, priv_data_t)>;

  using store_ptr_t = std::weak_ptr<cache_t>;

 public:
  explicit router_manager(uint32_t type_id) : router_manager_base(type_id) {}

  /**
   * @brief 拉取缓存对象，如果不存在返回空
   * @param key
   * @return 缓存对象
   */
  std::shared_ptr<router_object_base> get_base_cache(const key_t &key) const override {
    typename std::unordered_map<key_t, ptr_t>::const_iterator iter = caches_.find(key);
    if (iter == caches_.end()) {
      return nullptr;
    }

    return std::dynamic_pointer_cast<router_object_base>(get_cache(key));
  }

  void on_stop() override {
    router_manager_base::on_stop();

    // unbind LRU timer
    for (typename std::unordered_map<key_t, ptr_t>::iterator iter = caches_.begin(); iter != caches_.end(); ++iter) {
      if (iter->second) {
        iter->second->unset_timer_ref();
      }
    }
  }

  ptr_t get_cache(const key_t &key) const {
    typename std::unordered_map<key_t, ptr_t>::const_iterator iter = caches_.find(key);
    if (iter == caches_.end()) {
      return nullptr;
    }

    return iter->second;
  }

  ptr_t get_object(const key_t &key) const {
    typename std::unordered_map<key_t, ptr_t>::const_iterator iter = caches_.find(key);
    if (iter == caches_.end() || !iter->second->is_writable()) {
      return nullptr;
    }

    return iter->second;
  }

  int mutable_cache(std::shared_ptr<router_object_base> &out, const key_t &key, void *priv_data) override {
    ptr_t outc;
    int ret = mutable_cache(outc, key, reinterpret_cast<priv_data_t>(priv_data));
    out = std::dynamic_pointer_cast<router_object_base>(outc);
    return ret;
  }

  virtual int mutable_cache(ptr_t &out, const key_t &key, priv_data_t priv_data) {
    size_t left_ttl = logic_config::me()->get_cfg_router().retry_max_ttl();
    for (; left_ttl > 0; --left_ttl) {
      int res;
      out = get_cache(key);
      if (!out) {
        out = std::make_shared<cache_t>(key);
        if (!out) {
          return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
        }

        if (!insert(key, out)) {
          return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
        }
      } else {
        // 先等待IO任务完成，完成后可能在其他任务里已经拉取完毕了。
        res = out->await_io_task();
        if (res < 0) {
          return res;
        }

        if (out->is_cache_available()) {
          // 触发拉取实体并命中cache时要取消移除缓存和降级的计划任务
          out->unset_flag(router_object_base::flag_t::EN_ROFT_SCHED_REMOVE_CACHE);
          return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
        }
      }

      // pull using TYPE's API
      res = out->pull_cache_inner(reinterpret_cast<void *>(priv_data));

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
              return res;
            default:
              break;
          }
          continue;
        }
        continue;
      }

      on_evt_pull_cache(out, priv_data);
      return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
    }

    // 超出重试次数限制
    return PROJECT_NAMESPACE_ID::err::EN_ROUTER_TTL_EXTEND;
  }

  /**
   * @brief 检查缓存是否有效，如果过期则尝试拉取一次
   * @param in 输入保存的对象,如果有更新并且更新成功，会自动重设这个变量
   * @param key 重新拉取缓存时的key
   * @param out 输出对象指针
   * @param priv_data
   * @return
   */
  int renew_cache(store_ptr_t &in, ptr_t &out, const key_t &key, priv_data_t priv_data) {
    if (!in.expired()) {
      out = in.lock();
    } else {
      out = nullptr;
    }

    if (out && !out->check_flag(flag_t::EN_ROFT_CACHE_REMOVED)) {
      return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
    }

    int ret = mutable_cache(out, key, priv_data);
    if (ret >= 0) {
      in = out;
    }

    return ret;
  }

  int mutable_object(std::shared_ptr<router_object_base> &out, const key_t &key, void *priv_data) override {
    ptr_t outc;
    int ret = mutable_object(outc, key, reinterpret_cast<priv_data_t>(priv_data));
    out = std::dynamic_pointer_cast<router_object_base>(outc);
    return ret;
  }

  virtual int mutable_object(ptr_t &out, const key_t &key, priv_data_t priv_data) {
    size_t left_ttl = logic_config::me()->get_cfg_router().retry_max_ttl();
    for (; left_ttl > 0; --left_ttl) {
      int res;
      out = get_cache(key);
      if (!out) {
        out = std::make_shared<cache_t>(key);
        if (!out) {
          return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
        }

        if (!insert(key, out)) {
          return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
        }
      } else {
        // 先等待IO任务完成，完成后可能在其他任务里已经拉取完毕了。
        res = out->await_io_task();
        if (res < 0) {
          return res;
        }
        if (out->is_object_available()) {
          // 触发拉取实体并命中cache时要取消移除缓存和降级的计划任务
          out->unset_flag(router_object_base::flag_t::EN_ROFT_SCHED_REMOVE_OBJECT);
          out->unset_flag(router_object_base::flag_t::EN_ROFT_SCHED_REMOVE_CACHE);
          return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
        }
      }

      // 如果处于正在关闭的状态，则不允许创建新的实体，只能访问缓存
      if (is_closing()) {
        return PROJECT_NAMESPACE_ID::err::EN_ROUTER_CLOSING;
      }

      // pull using TYPE's API
      res = out->pull_object_inner(reinterpret_cast<void *>(priv_data));

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
            return res;
          default:
            break;
        }
        continue;
      }

      // 如果中途被移除，则降级回缓存
      if (!out->check_flag(router_object_base::flag_t::EN_ROFT_CACHE_REMOVED)) {
        on_evt_pull_object(out, priv_data);
        return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
      }
    }

    // 超出重试次数限制
    return PROJECT_NAMESPACE_ID::err::EN_ROUTER_TTL_EXTEND;
  }

  virtual int transfer(const key_t &key, uint64_t svr_id, bool need_notify, priv_data_t priv_data) {
    ptr_t obj;
    int ret = mutable_object(obj, key, priv_data);
    if (ret < 0 || !obj) {
      return ret;
    }

    return transfer(obj, svr_id, need_notify, priv_data);
  }

  virtual int transfer(const ptr_t &obj, uint64_t svr_id, bool need_notify, priv_data_t priv_data) {
    if (!obj) {
      return PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM;
    }

    if (svr_id == obj->get_router_server_id()) {
      return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
    }

    task_manager::task_ptr_t self_task(task_manager::task_t::this_task());
    if (!self_task) {
      return PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_NO_TASK;
    }

    // 先等待其他IO任务完成
    int ret = obj->await_io_task(self_task);
    if (ret < 0) {
      return ret;
    }

    if (!obj->is_writable()) {
      return PROJECT_NAMESPACE_ID::err::EN_ROUTER_NOT_WRITABLE;
    }

    if (svr_id == obj->get_router_server_id()) {
      return PROJECT_NAMESPACE_ID::err::EN_SUCCESS;
    }

    // 正在转移中
    router_object_base::flag_guard transfer_flag(*obj, router_object_base::flag_t::EN_ROFT_TRANSFERING);
    if (!transfer_flag) {
      return PROJECT_NAMESPACE_ID::err::EN_ROUTER_TRANSFER;
    }

    on_evt_remove_object(obj->get_key(), obj, reinterpret_cast<priv_data_t>(priv_data));

    // 保存到数据库
    ret = obj->remove_object(reinterpret_cast<void *>(priv_data), svr_id);
    // 数据库失败要强制拉取
    if (ret < 0) {
      obj->unset_flag(router_object_base::flag_t::EN_ROFT_IS_OBJECT);

      // 如果转发不成功，要回发执行失败
      if (!obj->get_transfer_pending_list().empty()) {
        std::list<atframework::SSMsg> all_msgs;
        all_msgs.swap(obj->get_transfer_pending_list());

        for (atframework::SSMsg &msg : all_msgs) {
          obj->send_transfer_msg_failed(COPP_MACRO_STD_MOVE(msg));
        }
      }

      return ret;
    }

    on_evt_object_removed(obj->get_key(), obj, reinterpret_cast<priv_data_t>(priv_data));

    if (0 != svr_id && need_notify) {
      // 如果目标不是0则通知目标服务器
      rpc::context ctx;
      PROJECT_NAMESPACE_ID::SSRouterTransferReq *req = ctx.create<PROJECT_NAMESPACE_ID::SSRouterTransferReq>();
      PROJECT_NAMESPACE_ID::SSRouterTransferRsp *rsp = ctx.create<PROJECT_NAMESPACE_ID::SSRouterTransferRsp>();
      if (nullptr == req || nullptr == rsp) {
        ret = PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
      } else {
        atframework::SSRouterHead *router_head = req->mutable_object();
        if (nullptr != router_head) {
          router_head->set_router_src_bus_id(obj->get_router_server_id());
          router_head->set_router_version(obj->get_router_version());

          router_head->set_object_inst_id(obj->get_key().object_id);
          router_head->set_object_type_id(get_type_id());
          router_head->set_object_zone_id(obj->get_key().zone_id);

          // 转移通知RPC也需要设置为IO任务，这样如果有其他的读写任务或者转移任务都会等本任务完成
          obj->io_task_.swap(self_task);
          ret = RPC_AWAIT_CODE_RESULT(rpc::router::router_transfer(ctx, svr_id, *req, *rsp));
          obj->wakeup_io_task_awaiter();
          if (ret < 0) {
            FWLOGERROR("transfer router object (type={},zone_id={}) {} failed, res: {}", get_type_id(),
                       obj->get_key().zone_id, obj->get_key().object_id, ret);
          }
        } else {
          ret = PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
        }
      }
    }

    // 如果转发成功，要转发这期间收到的消息
    if (ret >= 0) {
      while (!obj->get_transfer_pending_list().empty()) {
        std::list<atframework::SSMsg> all_msgs;
        all_msgs.swap(obj->get_transfer_pending_list());

        uint64_t rpc_sequence;
        for (atframework::SSMsg &msg : all_msgs) {
          int res = send_msg_raw(*obj, std::move(msg), rpc_sequence);
          if (res < 0) {
            FWLOGERROR("transfer router object (type={},zone_id={}) {} message failed, res: {}", get_type_id(),
                       obj->get_key().zone_id, obj->get_key().object_id, res);
          } else {
            obj->send_transfer_msg_failed(COPP_MACRO_STD_MOVE(msg));
          }
        }
      }
    } else {
      // 如果转发不成功，要回发执行失败
      if (!obj->get_transfer_pending_list().empty()) {
        std::list<atframework::SSMsg> all_msgs;
        all_msgs.swap(obj->get_transfer_pending_list());

        for (atframework::SSMsg &msg : all_msgs) {
          obj->send_transfer_msg_failed(COPP_MACRO_STD_MOVE(msg));
        }
      }
    }

    return ret;
  }

  virtual bool remove_cache(const key_t &key, std::shared_ptr<router_object_base> cache, void *priv_data) override {
    ptr_t cache_child;
    bool ret = true;
    {
      typename std::unordered_map<key_t, ptr_t>::iterator iter;
      if (cache) {
        iter = caches_.find(cache->get_key());
      } else {
        iter = caches_.find(key);
      }

      if (iter == caches_.end()) {
        return false;
      }

      if (cache && iter->second != cache) {
        return false;
      }
      cache_child = iter->second;

      if (cache_child->is_writable()) {
        ret = remove_object(key, cache_child, priv_data);
      }
    }
    // After RPC, iter may be different
    bool trigger_evt = !cache_child->check_flag(router_object_base::flag_t::EN_ROFT_CACHE_REMOVED);
    if (trigger_evt) {
      on_evt_remove_cache(key, cache_child, reinterpret_cast<priv_data_t>(priv_data));
      cache_child->set_flag(router_object_base::flag_t::EN_ROFT_CACHE_REMOVED);
    }

    if (!cache_child->check_flag(router_object_base::flag_t::EN_ROFT_CACHE_REMOVED)) {
      on_evt_remove_cache(key, cache_child, reinterpret_cast<priv_data_t>(priv_data));
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

    if (trigger_evt) {
      on_evt_cache_removed(key, cache_child, reinterpret_cast<priv_data_t>(priv_data));
    }
    return ret;
  }

  virtual bool remove_object(const key_t &key, std::shared_ptr<router_object_base> cache, void *priv_data) override {
    ptr_t cache_child;
    if (!cache) {
      typename std::unordered_map<key_t, ptr_t>::iterator iter = caches_.find(key);
      if (iter == caches_.end()) {
        return false;
      }

      cache_child = iter->second;
      cache = std::dynamic_pointer_cast<router_object_base>(iter->second);
      assert(!!cache);
    } else {
      cache_child = std::dynamic_pointer_cast<cache_t>(cache);
      assert(!!cache_child);
    }

    if (!cache_child) {
      return false;
    }

    on_evt_remove_object(key, cache_child, reinterpret_cast<priv_data_t>(priv_data));

    if (cache_child->remove_object(reinterpret_cast<priv_data_t>(priv_data), 0) < 0) {
      return false;
    }

    on_evt_object_removed(key, cache_child, reinterpret_cast<priv_data_t>(priv_data));
    return true;
  }

  void set_on_remove_object(remove_fn_t &&fn) { handle_on_remove_object_ = fn; }
  void set_on_remove_object(const remove_fn_t &fn) { handle_on_remove_object_ = fn; }

  void set_on_object_removed(remove_fn_t &&fn) { handle_on_object_removed_ = fn; }
  void set_on_object_removed(const remove_fn_t &fn) { handle_on_object_removed_ = fn; }

  void set_on_cache_removed(remove_fn_t &&fn) { handle_on_cache_removed_ = fn; }
  void set_on_cache_removed(const remove_fn_t &fn) { handle_on_cache_removed_ = fn; }

  void set_on_remove_cache(remove_fn_t &&fn) { handle_on_remove_cache_ = fn; }
  void set_on_remove_cache(const remove_fn_t &fn) { handle_on_remove_cache_ = fn; }

  void set_on_pull_object(pull_fn_t &&fn) { handle_on_pull_object_ = fn; }
  void set_on_pull_object(const pull_fn_t &fn) { handle_on_pull_object_ = fn; }

  void set_on_pull_cache(pull_fn_t &&fn) { handle_on_pull_cache_ = fn; }
  void set_on_pull_cache(const pull_fn_t &fn) { handle_on_pull_cache_ = fn; }

  void foreach (const std::function<bool(const ptr_t &)> fn) {
    if (!fn) {
      return;
    }

    // 先复制出所有的只能指针，防止回掉过程中成员变化带来问题
    std::vector<ptr_t> res;
    res.reserve(caches_.size());
    for (typename std::unordered_map<key_t, ptr_t>::iterator iter = caches_.begin(); iter != caches_.end(); ++iter) {
      res.push_back(iter->second);
    }

    for (size_t i = 0; i < res.size(); ++i) {
      if (!fn(res[i])) {
        break;
      }
    }
  }

  void foreach_cache(const std::function<bool(const ptr_t &)> fn) {
    if (!fn) {
      return;
    }

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

  void foreach_object(const std::function<bool(const ptr_t &)> fn) {
    if (!fn) {
      return;
    }

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
    return true;
  }

 protected:
  // =============== event =================
  virtual void on_evt_remove_cache(const key_t &key, const ptr_t &cache, priv_data_t priv_data) {
    if (handle_on_remove_cache_) {
      handle_on_remove_cache_(*this, key, cache, priv_data);
    }
  }

  virtual void on_evt_cache_removed(const key_t &key, const ptr_t &cache, priv_data_t priv_data) {
    if (handle_on_cache_removed_) {
      handle_on_cache_removed_(*this, key, cache, priv_data);
    }
  }

  virtual void on_evt_remove_object(const key_t &key, const ptr_t &cache, priv_data_t priv_data) {
    if (handle_on_remove_object_) {
      handle_on_remove_object_(*this, key, cache, priv_data);
    }
  }

  virtual void on_evt_object_removed(const key_t &key, const ptr_t &cache, priv_data_t priv_data) {
    if (handle_on_object_removed_) {
      handle_on_object_removed_(*this, key, cache, priv_data);
    }
  }

  virtual void on_evt_pull_cache(const ptr_t &cache, priv_data_t priv_data) {
    if (handle_on_pull_cache_) {
      handle_on_pull_cache_(*this, cache, priv_data);
    }
  }

  virtual void on_evt_pull_object(const ptr_t &cache, priv_data_t priv_data) {
    if (handle_on_pull_object_) {
      handle_on_pull_object_(*this, cache, priv_data);
    }
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

#endif  //_ROUTER_ROUTER_MANAGER_H
