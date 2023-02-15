// Copyright 2021 atframework
// Created by owent on 2019-10-09.
//

#pragma once

#include <log/log_wrapper.h>
#include <mem_pool/lru_map.h>
#include <time/time_utility.h>

#include <dispatcher/task_action_base.h>
#include <dispatcher/task_manager.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <assert.h>
#include <stdint.h>
#include <functional>
#include <unordered_map>

#include "rpc/rpc_async_invoke.h"
#include "rpc/rpc_common_types.h"

namespace rpc {
class context;

template <typename TKey, typename TObject>
class rpc_lru_cache_map {
 public:
  using key_type = TKey;
  using value_type = TObject;
  using self_type = rpc_lru_cache_map<TKey, TObject>;
  enum { RPC_LRU_CACHE_MAP_DEFAULT_RETRY_TIMES = 3 };

  struct value_cache_type {
    task_type_trait::task_type pulling_task;
    task_type_trait::task_type saving_task;
    key_type data_key;
    int64_t data_version;
    time_t last_visit_timepoint;
    value_type data_object;
    uint64_t saving_sequence;
    uint64_t saved_sequence;

    explicit value_cache_type(const key_type &k)
        : data_key(k), data_version(0), last_visit_timepoint(0), saving_sequence(0), saved_sequence(0) {}
    value_cache_type(const value_cache_type &) = default;
    value_cache_type(value_cache_type &&) = default;

    value_cache_type &operator=(const value_cache_type &) = default;
    value_cache_type &operator=(value_cache_type &&) = default;
  };

  using lru_map_type = util::mempool::lru_map<key_type, value_cache_type>;
  using cache_ptr_type = typename lru_map_type::store_type;
  using iterator = typename lru_map_type::iterator;
  using const_iterator = typename lru_map_type::const_iterator;
  using size_type = typename lru_map_type::size_type;

 public:
  cache_ptr_type get_cache(const key_type &key) {
    auto iter = pool_.find(key);
    if (iter != pool_.end()) {
      if (iter->second) {
        iter->second->last_visit_timepoint = util::time::time_utility::get_now();
        return iter->second;
      }
    }

    return nullptr;
  }

  void set_cache(cache_ptr_type &cache) {
    if (!cache) {
      return;
    }

    auto iter = pool_.find(cache->data_key);
    if (iter != pool_.end()) {
      if (iter->second == cache) {
        return;
      }
      pool_.erase(iter);
    }

    cache->last_visit_timepoint = util::time::time_utility::get_now();
    pool_.insert_key_value(cache->data_key, cache);
  }

  inline bool empty() const { return pool_.empty(); }

  inline iterator begin() { return pool_.begin(); }
  inline const_iterator cbegin() const { return pool_.cbegin(); }
  inline iterator end() { return pool_.end(); }
  inline const_iterator cend() const { return pool_.cend(); }
  inline const typename lru_map_type::value_type &front() const { return pool_.front(); }
  inline typename lru_map_type::value_type &front() { return pool_.front(); }
  inline const typename lru_map_type::value_type &back() const { return pool_.back(); }
  inline typename lru_map_type::value_type &back() { return pool_.back(); }
  inline void pop_front() { return pool_.pop_front(); }
  inline void pop_back() { return pool_.pop_back(); }
  inline size_type size() const { return pool_.size(); }
  inline void reserve(size_type s) { pool_.reserve(s); }

  bool remove_cache(const key_type &key) {
    auto iter = pool_.find(key);
    if (iter != pool_.end()) {
      pool_.erase(iter);
      return true;
    }

    return false;
  }

  // ==================== 协程接口，可能切出执行上下文 ====================
  /**
   * @brief 等待并提取拉取结果
   * @note 这个接口会自动合并多个拉取请求，并共享使用同一个LRU缓存
   * @param[in]  key  key
   * @param[out] out  输出的缓存对象指针
   * @param[in]  fn   拉取协程函数
   * @return 0或错误码
   */
  result_code_type await_fetch(
      rpc::context &ctx, const key_type &key, cache_ptr_type &out,
      std::function<result_code_type(rpc::context &ctx, const key_type &key, value_type &val_out, int64_t *out_version)>
          fn) {
    if (!fn) {
      FWLOGERROR("{} must be called with rpc function", __FUNCTION__);
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
    }

    TASK_COMPAT_CHECK_TASK_ACTION_RETURN("{}", "this function should be called in task");

    int retry_times = RPC_LRU_CACHE_MAP_DEFAULT_RETRY_TIMES + 1;
    while ((--retry_times) > 0) {
      out = get_cache(key);

      // 如果没有缓存，本任务就是拉取任务
      if (nullptr == out) {
        break;
      }

      // 如果正在拉取，则排到拉取任务后面
      if (!task_type_trait::empty(out->pulling_task)) {
        if (task_type_trait::is_exiting(out->pulling_task)) {
          // fallback, clear data, 理论上不会走到这个流程，前面就是reset掉
          task_type_trait::reset_task(out->pulling_task);
        } else {
          if (task_type_trait::get_task_id(out->pulling_task) == ctx.get_task_context().task_id) {
            break;
          }
          int32_t res = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, out->pulling_task));
          if (res < 0) {
            out.reset();
            RPC_RETURN_CODE(res);
          }
          task_type_trait::reset_task(out->pulling_task);
        }
        continue;
      }

      RPC_RETURN_CODE(0);
    }

    if (retry_times <= 0) {
      out.reset();
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_RPC_RETRY_TIMES_EXCEED);
    }

    // 尝试拉取，成功的话放进缓存
    auto res = pool_.insert_key_value(key, value_cache_type(key));
    if (!res.second) {
      out.reset();
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC);
    }

    out = res.first->second;
    out->data_version = 0;
    out->last_visit_timepoint = util::time::time_utility::get_now();

    auto invoke_result = rpc::async_invoke(
        ctx, "rpc_lru_cache_map.await_fetch",
        [out, key, fn = std::move(fn)](rpc::context &child_ctx) -> rpc::result_code_type {
          int32_t ret = RPC_AWAIT_CODE_RESULT(fn(child_ctx, key, out->data_object, &out->data_version));

          if (task_type_trait::get_task_id(out->pulling_task) == child_ctx.get_task_context().task_id) {
            task_type_trait::reset_task(out->pulling_task);
          }

          RPC_RETURN_CODE(ret);
        });
    int32_t ret;
    if (invoke_result.is_error()) {
      ret = *invoke_result.get_error();
    } else {
      // 拉取结束，重置拉取任务
      if (!task_type_trait::is_exiting(*invoke_result.get_success())) {
        out->pulling_task = *invoke_result.get_success();
      }
      ret = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, *invoke_result.get_success()));
      if (ret >= 0) {
        ret = task_type_trait::get_result(*invoke_result.get_success());
      }
    }

    if (0 == ret) {
      cache_ptr_type test_cache = get_cache(key);
      if (nullptr != test_cache) {
        // 可能前面的缓存被淘汰过，新起了拉取任务
        if (task_type_trait::get_task_id(test_cache->pulling_task) == ctx.get_task_context().task_id) {
          task_type_trait::reset_task(test_cache->pulling_task);
        }

        // 可能前面的缓存被淘汰过，新起了拉取任务，那么数据刷到最新即可
        // 理论上也不应该会走到这里流程
        if (out->data_version > test_cache->data_version) {
          set_cache(out);
        }
      } else {
        // 创建缓存，可能前面的缓存被淘汰了
        set_cache(out);
      }
    } else {
      if (PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND == ret) {
        FWLOGWARNING("{} try to rpc fetch data failed and will remove lru cache(task: {}), res: {}",
                     ctx.get_task_context().task_id, ret);
      } else {
        FWLOGERROR("{} try to rpc fetch data failed and will remove lru cache(task: {}), res: {}",
                   ctx.get_task_context().task_id, ret);
      }
      remove_cache(key);
    }

    RPC_RETURN_CODE(ret);
  }

  /**
   * @brief 等待并提取保存结果
   * @note 这个接口会自动排队且合并多个保存请求
   * @param[out] out 输出的缓存对象指针，注意这个参数仅仅是传出参数，并不是保存这个缓存块
   * @param[in]  fn  保存协程函数
   * @return 0或错误码
   */
  result_code_type await_save(
      rpc::context &ctx, cache_ptr_type &inout,
      std::function<result_code_type(rpc::context &ctx, const value_type &in, int64_t *out_version)> fn) {
    if (!inout) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
    }

    if (!fn) {
      FWLOGERROR("{} must be called with rpc function", "rpc_lru_cache_map<KEY,ALUE>.await_save");
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM);
    }

    // 分配一个保存序号，相当于保存版本号
    uint64_t this_saving_seq = ++inout->saving_sequence;

    // 如果有其他任务正在保存，则需要等待那个任务完成。
    // 因为可能叠加很多任务，所以不能直接用拉取接口里得重试次数
    while (true) {
      // 如果其他得任务得保存已经覆盖了自己得版本，直接成功返回
      if (inout->saved_sequence >= this_saving_seq) {
        RPC_RETURN_CODE(0);
      }

      // 自己就是保存任务
      if (task_type_trait::empty(inout->saving_task)) {
        break;
      }

      if (task_type_trait::get_task_id(inout->saving_task) != ctx.get_task_context().task_id) {
        if (task_type_trait::is_exiting(inout->saving_task)) {
          // fallback, clear data, 理论上不会走到这个流程，前面就是reset掉
          task_type_trait::reset_task(inout->saving_task);
        } else {
          if (task_type_trait::get_task_id(inout->saving_task) == ctx.get_task_context().task_id) {
            break;
          }
          int32_t res = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, inout->saving_task));
          if (res < 0) {
            RPC_RETURN_CODE(res);
          }
          task_type_trait::reset_task(inout->saving_task);
        }
      }
    }

    // 实际保存序号，因为可能延时执行，所以实际保存得时候可能被merge了其他请求得数据
    uint64_t real_saving_seq = inout->saving_sequence;

    auto invoke_result = rpc::async_invoke(
        ctx, "rpc_lru_cache_map.await_save",
        [inout, fn = std::move(fn)](rpc::context &child_ctx) -> rpc::result_code_type {
          int32_t ret = RPC_AWAIT_CODE_RESULT(fn(child_ctx, inout->data_object, &inout->data_version));

          if (task_type_trait::get_task_id(inout->saving_task) == child_ctx.get_task_context().task_id) {
            task_type_trait::reset_task(inout->saving_task);
          }

          RPC_RETURN_CODE(ret);
        });
    int32_t ret;
    if (invoke_result.is_error()) {
      ret = *invoke_result.get_error();
    } else {
      // 拉取结束，重置拉取任务
      if (!task_type_trait::is_exiting(*invoke_result.get_success())) {
        inout->saving_task = *invoke_result.get_success();
      }
      ret = RPC_AWAIT_CODE_RESULT(rpc::wait_task(ctx, *invoke_result.get_success()));
      if (ret >= 0) {
        ret = task_type_trait::get_result(*invoke_result.get_success());
      }
    }

    if (0 == ret && real_saving_seq > inout->saved_sequence) {
      inout->saved_sequence = real_saving_seq;
    } else if (0 != ret) {
      cache_ptr_type cur = get_cache(inout->data_key);
      if (cur == inout) {
        // 数据错误，清除缓存，下次重新拉取
        remove_cache(inout->data_key);
      }
    }

    RPC_RETURN_CODE(ret);
  }

 private:
  lru_map_type pool_;
};
}  // namespace rpc
