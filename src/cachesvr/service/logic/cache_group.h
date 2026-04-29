// Copyright 2026 atframework
// Created by owent

#pragma once

#include <config/compile_optimize.h>
#include <config/compiler_features.h>

#include <log/log_wrapper.h>
#include <mem_pool/lru_map.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.struct.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>

#include <rpc/cache/cache_api.h>
#include <rpc/rpc_utils.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "logic/cache_common.h"

class cache_object_base;
class cache_group_manager;
template <class TCache>
class cache_object;

class cache_group_base;
template <class TCache>
class cache_group;

class cache_object_base {
 public:
  explicit cache_object_base(const PROJECT_NAMESPACE_ID::object_cache_key &key);
  ~cache_object_base();

  int replace_watcher(rpc::context &ctx, cache_group_manager &manager,
                      const PROJECT_NAMESPACE_ID::object_cache_watcher &watcher_key, int64_t data_version,
                      uint64_t server_inst_id);
  void remove_watcher(const PROJECT_NAMESPACE_ID::object_cache_watcher &watcher_key,
                      const cache_watcher_t *check_watcher = nullptr);
  bool is_cache_valid() const;
  void visit_update();

  void cleanup_all_watchers(bool notify = true);
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type set_cache_expired(rpc::context &ctx);
  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type notify_update_meta(
      rpc::context &ctx, const PROJECT_NAMESPACE_ID::object_cache_meta &input);

  inline bool has_watcher() const noexcept { return !watchers_.empty(); }
  inline int64_t get_cachesvr_version() const noexcept { return cachesvr_version_; }
  inline int64_t get_data_version() const noexcept { return data_version_; }
  inline time_t get_expired_time() const noexcept { return expired_time_; }
  inline time_t get_next_check_time() const noexcept { return next_check_time_; }
  inline time_t get_remove_protect_time() const noexcept { return remove_protect_time_; }

  inline const PROJECT_NAMESPACE_ID::object_cache_key &get_key() const noexcept { return key_; }
  inline size_t get_watcher_size() const noexcept { return watchers_.size(); }

 private:
  void update_cachesvr_version(int64_t cachesvr_version) noexcept;
  void update_pull_cache_time() noexcept;
  void update_next_check_time() noexcept;
  void update_remove_protect_time() noexcept;

  template <class>
  friend class cache_group;

 private:
  int64_t cachesvr_version_;    // Cache版本号
  int64_t data_version_;        // Cache的更新时间
  time_t expired_time_;         // Cache失效时间
  time_t remove_protect_time_;  // Cache移除保护时间（防止拉取期间被删除）
  time_t next_check_time_;      // 下一次检查时间
  PROJECT_NAMESPACE_ID::object_cache_key key_;

  cache_watcher_set_t watchers_;  // 监听者
};

template <class TCache>
class cache_object : public cache_object_base {
 public:
  using cache_type = TCache;

 public:
  explicit cache_object(const PROJECT_NAMESPACE_ID::object_cache_key &key) : cache_object_base(key) {}

  inline const cache_type &get_data() const noexcept { return content_; }
  inline cache_type &get_data() noexcept { return content_; }

 private:
  template <class>
  friend class cache_group;

 private:
  cache_type content_;  // Cache数据类型
};

class cache_group_base {
 public:
  explicit cache_group_base(cache_group_manager &manager);
  virtual ~cache_group_base() = 0;

  ATFW_EXPLICIT_NODISCARD_ATTR virtual rpc::result_code_type pull_content(
      rpc::context &,
      const google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_pull_key> &cache_pull_keys,
      google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_content> &,
      const PROJECT_NAMESPACE_ID::object_cache_watcher &watcher, uint64_t watcher_server_inst_id) = 0;

  virtual int watch(rpc::context &ctx, const PROJECT_NAMESPACE_ID::object_cache_watch_key &cache_key,
                    const PROJECT_NAMESPACE_ID::object_cache_watcher &watcher_key, uint64_t server_inst_id) = 0;
  virtual int unwatch(const PROJECT_NAMESPACE_ID::object_cache_key &cache_key,
                      const PROJECT_NAMESPACE_ID::object_cache_watcher &watcher_key) = 0;
  virtual bool remove_cache(const PROJECT_NAMESPACE_ID::object_cache_key &cache_key, bool notify_watcher) = 0;
  virtual std::shared_ptr<cache_object_base> get_cache(const PROJECT_NAMESPACE_ID::object_cache_key &cache_key) = 0;
  virtual std::shared_ptr<cache_object_base> mutable_cache(const PROJECT_NAMESPACE_ID::object_cache_key &cache_key) = 0;

  ATFW_EXPLICIT_NODISCARD_ATTR virtual rpc::result_code_type update_meta(
      rpc::context &ctx, const PROJECT_NAMESPACE_ID::object_cache_key &cache_key,
      const PROJECT_NAMESPACE_ID::object_cache_meta &meta) = 0;

  inline cache_group_manager *get_manager() const { return owner_; }

  int64_t get_local_cachesvr_version() const;

 private:
  cache_group_manager *owner_;
};

template <class TCache>
class cache_group : public cache_group_base {
 public:
  using value_type = cache_object<TCache>;
  using cache_type = typename value_type::cache_type;
  using pull_data_param_t = std::unordered_map<PROJECT_NAMESPACE_ID::object_cache_key, cache_type *,
                                               rpc::cache_api::cache_key_hash_t, rpc::cache_api::cache_key_equal_t>;
  using pull_data_param_ptr_t = std::shared_ptr<pull_data_param_t>;
  using pull_data_fn_t = std::function<rpc::result_code_type(rpc::context &, pull_data_param_ptr_t &)>;
  using pack_data_fn_t =
      std::function<void(rpc::context &, const value_type &,
                         ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_content> &)>;
  using update_meta_fn_t =
      std::function<void(rpc::context &, const PROJECT_NAMESPACE_ID::object_cache_meta &, cache_type &)>;

 public:
  cache_group(cache_group_manager &manager, pull_data_fn_t pull_data_fn, pack_data_fn_t pack_data_fn,
              update_meta_fn_t update_meta_fn)
      : cache_group_base(manager),
        pull_data_fn_(pull_data_fn),
        pack_data_fn_(pack_data_fn),
        update_meta_fn_(update_meta_fn) {}
  virtual ~cache_group() {
    util::mempool::lru_map<PROJECT_NAMESPACE_ID::object_cache_key, value_type, rpc::cache_api::cache_key_hash_t,
                           rpc::cache_api::cache_key_equal_t>
        data;
    data.swap(data_);
  }

  int tick(int64_t cachesvr_version, time_t now, size_t max_count, size_t gc_count, size_t max_recycle_count) {
    if (data_.empty()) {
      return 0;
    }

    size_t res = 0;
    for (; !data_.empty() && (0 == max_recycle_count || res < max_recycle_count); ++res) {
      auto iter = data_.begin();
      // 无效数据
      if (!(*iter).second) {
        FWLOGINFO("Remove cache {}:{}:{}, total cache sz now {}", static_cast<uint32_t>((*iter).first.cache_type()),
                  (*iter).first.zone_id(), (*iter).first.instance_id(), data_.size());
        data_.erase(iter);
        continue;
      }

      bool need_remove = false;
      bool notify_watcher_when_remove = true;
      do {
        // 长时间未访问，缓存可以淘汰。正常watcher会定期刷新访问时间
        if ((*iter).second->get_expired_time() <= now) {
          need_remove = true;
          break;
        }

        // 拉取期间处于保护时间内，不移除缓存
        if (!(*iter).second->has_watcher() && now > (*iter).second->get_remove_protect_time() &&
            ((gc_count > 0 && data_.size() > gc_count) ||  // 数量达到开始主动GC的边界，且无watcher则主动GC
             !(*iter).second->is_cache_valid())) {         // 无watcher且数据已失效，缓存里没有任何有效数据，可以淘汰
          need_remove = true;
          break;
        }

        // 如果Cachesvr负载均衡版本变化，且这个cache不在本节点上。直接移除
        if ((*iter).second->get_cachesvr_version() != cachesvr_version) {
          if (logic_config::me()->get_local_server_id() != rpc::cache_api::get_cachesvr_server_id((*iter).first)) {
            need_remove = true;
            notify_watcher_when_remove = false;
            break;
          }

          // 缓存分布未变化，则直接刷新版本即可
          (*iter).second->update_cachesvr_version(cachesvr_version);
        }

        // 缓存数量超出预期,高负载保护
        if (max_count > 0 && data_.size() > max_count) {
          need_remove = true;

          // TODO OSS日志告警。可能需要扩容缓存服务器
          break;
        }
      } while (false);

      if (need_remove) {
        // obj_ptr 可能在其他地方被临时引用，所以这里不能依赖析构来清理watcher和定时器
        FWLOGINFO("Remove cache {}:{}:{}, total cache sz now {}", static_cast<uint32_t>((*iter).first.cache_type()),
                  (*iter).first.zone_id(), (*iter).first.instance_id(), data_.size());

        auto obj_ptr = (*iter).second;
        data_.erase(iter);
        obj_ptr->cleanup_all_watchers(notify_watcher_when_remove);
        continue;
      }

      if ((*iter).second->get_next_check_time() > now) {
        break;
      }

      // 刷新
      (*iter).second->update_next_check_time();
      data_.find((*iter).first, true);
    }

    return static_cast<int>(res);
  }

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type pull_content(
      rpc::context &ctx,
      const ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_pull_key> &cache_pull_keys,
      ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_content> &output,
      const PROJECT_NAMESPACE_ID::object_cache_watcher &watcher_key, uint64_t watcher_server_inst_id) override {
    if (!pull_data_fn_) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_INIT);
    }
    cache_group_manager *manager = get_manager();
    if (nullptr == manager) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_INIT);
    }

    pull_data_param_ptr_t pull_data_param = std::make_shared<pull_data_param_t>();
    std::vector<std::pair<std::shared_ptr<value_type>, int> > local_holder;
    pull_data_param->reserve(static_cast<size_t>(cache_pull_keys.size()));
    local_holder.reserve(static_cast<size_t>(cache_pull_keys.size()));
    int64_t local_cachesvr_version = get_local_cachesvr_version();
    for (int i = 0; i < cache_pull_keys.size(); ++i) {
      std::shared_ptr<value_type> cache_object = mutable_data(cache_pull_keys.Get(i).cache_key());
      if (!cache_object) {
        continue;
      }

      // 必须先更新watcher，不然tick里会因为没有wacher且缓存无效而释放掉cache对象
      if (watcher_key.cache_type() != PROJECT_NAMESPACE_ID::EN_CACHE_API_CACHE_TYPE_UNKNOWN &&
          watcher_key.instance_id() != 0 && watcher_server_inst_id != 0 &&
          cache_pull_keys.Get(i).type() == PROJECT_NAMESPACE_ID::EN_CACHE_API_GET_CACHE_TYPE_SUBSCRIBE) {
        cache_object->replace_watcher(ctx, *manager, watcher_key, 0, watcher_server_inst_id);
      }

      if (cache_pull_keys.Get(i).type() == PROJECT_NAMESPACE_ID::EN_CACHE_API_GET_CACHE_TYPE_HOT_DATA &&
          watcher_server_inst_id != 0) {
        PROJECT_NAMESPACE_ID::object_cache_watcher cache_key;
        cache_key.set_cache_type(PROJECT_NAMESPACE_ID::EN_CACHE_API_CACHE_TYPE_UNKNOWN);
        cache_key.set_zone_id(0);
        cache_key.set_instance_id(watcher_server_inst_id);
        cache_key.set_server_subscribe(true);
        cache_object->replace_watcher(ctx, *manager, cache_key, 0, watcher_server_inst_id);
      }

      if (cache_object->is_cache_valid()) {
        // 如果缓存有效，则直接打包已有的缓存数据
        if (pack_data_fn_) {
          pack_data_fn_(ctx, *cache_object, output);
        }
        continue;
      }

      cache_object->update_remove_protect_time();
      // 栈上引用一份，以防RPC过程中对象析构
      local_holder.push_back(
          std::pair<std::shared_ptr<value_type>, int>(mutable_data(cache_pull_keys.Get(i).cache_key()), i));
      if (!local_holder.empty() && local_holder.back().first) {
        (*pull_data_param)[cache_pull_keys.Get(i).cache_key()] = &local_holder.back().first->content_;
      }
    }

    int32_t ret = RPC_AWAIT_CODE_RESULT(pull_data_fn_(ctx, pull_data_param));
    if (ret >= 0) {
      for (auto &cache_object : local_holder) {
        if (cache_object.first) {
          cache_object.first->update_cachesvr_version(local_cachesvr_version);
          cache_object.first->update_pull_cache_time();

          // 打包
          if (pack_data_fn_) {
            pack_data_fn_(ctx, *cache_object.first, output);
          }
        }
      }
    }

    RPC_RETURN_CODE(ret);
  }

  int watch(rpc::context &ctx, const PROJECT_NAMESPACE_ID::object_cache_watch_key &cache_watch_key,
            const PROJECT_NAMESPACE_ID::object_cache_watcher &watcher_key, uint64_t server_inst_id) override {
    cache_group_manager *manager = get_manager();
    if (nullptr == manager) {
      return PROJECT_NAMESPACE_ID::err::EN_SYS_INIT;
    }

    PROJECT_NAMESPACE_ID::object_cache_key *cache_key = ctx.create<PROJECT_NAMESPACE_ID::object_cache_key>();
    if (nullptr == cache_key) {
      return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
    }

    cache_key->set_cache_type(cache_watch_key.cache_type());
    cache_key->set_zone_id(cache_watch_key.zone_id());
    cache_key->set_instance_id(cache_watch_key.instance_id());

    std::shared_ptr<value_type> cache_object = get_data(*cache_key);
    if (!cache_object) {
      // 如果缓存分布信息过期，不应该在本节点上，直接忽略监听请求
      if (logic_config::me()->get_local_server_id() != rpc::cache_api::get_cachesvr_server_id(*cache_key)) {
        return 0;
      }
    }

    cache_object = mutable_data(*cache_key);
    if (!cache_object) {
      return PROJECT_NAMESPACE_ID::err::EN_SYS_MALLOC;
    }

    return cache_object->replace_watcher(ctx, *manager, watcher_key, cache_watch_key.data_version(), server_inst_id);
  }

  int unwatch(const PROJECT_NAMESPACE_ID::object_cache_key &cache_key,
              const PROJECT_NAMESPACE_ID::object_cache_watcher &watcher_key) override {
    std::shared_ptr<value_type> cache_object = get_data(cache_key);
    if (!cache_object) {
      return 0;
    }

    cache_object->remove_watcher(watcher_key);

    // 无watcher且数据已失效，缓存里没有任何有效数据，可以淘汰
    if (!cache_object->has_watcher() && !cache_object->is_cache_valid()) {
      remove_data(cache_key, false);
    }

    return 0;
  }

  bool remove_cache(const PROJECT_NAMESPACE_ID::object_cache_key &cache_key, bool notify_watcher) override {
    return remove_data(cache_key, notify_watcher);
  }

  std::shared_ptr<cache_object_base> get_cache(const PROJECT_NAMESPACE_ID::object_cache_key &cache_key) override {
    return std::static_pointer_cast<cache_object_base>(get_data(cache_key));
  }

  std::shared_ptr<cache_object_base> mutable_cache(const PROJECT_NAMESPACE_ID::object_cache_key &cache_key) override {
    return std::static_pointer_cast<cache_object_base>(mutable_data(cache_key));
  }

  ATFW_EXPLICIT_NODISCARD_ATTR rpc::result_code_type update_meta(
      rpc::context &ctx, const PROJECT_NAMESPACE_ID::object_cache_key &cache_key,
      const PROJECT_NAMESPACE_ID::object_cache_meta &meta) override {
    std::shared_ptr<value_type> cache_data = get_data(cache_key);
    if (!cache_data) {
      // 无缓存的话也没有订阅者，直接忽略即可
      RPC_RETURN_CODE(0);
    }

    if (meta.data_version() > 0 && meta.data_version() < cache_data->get_data_version()) {
      // 如果设置了要要验证版本号切过老，也直接忽略即可。
      RPC_RETURN_CODE(0);
    }

    if (update_meta_fn_) {
      update_meta_fn_(ctx, meta, cache_data->get_data());
    }

    RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(cache_data->notify_update_meta(ctx, meta)));
  }

  bool remove_data(const PROJECT_NAMESPACE_ID::object_cache_key &cache_key, bool notify_watcher = true,
                   const value_type *check_cache_object = nullptr) {
    auto iter = data_.find(cache_key, false);
    if (iter == data_.end()) {
      return false;
    }

    if (!(*iter).second) {
      FWLOGINFO("Remove cache {}:{}:{}", static_cast<uint32_t>((*iter).first.cache_type()), (*iter).first.zone_id(),
                (*iter).first.instance_id());

      data_.erase(iter);
      return true;
    }

    if (nullptr != check_cache_object) {
      if (check_cache_object != (*iter).second.get()) {
        return false;
      }
    }

    // obj_ptr 可能在其他地方被临时引用，所以这里不能依赖析构来清理watcher和定时器
    FWLOGINFO("Remove cache {}:{}:{}", static_cast<uint32_t>((*iter).first.cache_type()), (*iter).first.zone_id(),
              (*iter).first.instance_id());
    auto obj_ptr = (*iter).second;
    data_.erase(iter);

    if (obj_ptr) {
      obj_ptr->cleanup_all_watchers(notify_watcher);
    }

    return !!obj_ptr;
  }

  std::shared_ptr<value_type> get_data(const PROJECT_NAMESPACE_ID::object_cache_key &cache_key) {
    auto iter = data_.find(cache_key, false);
    if (iter == data_.end()) {
      return nullptr;
    }

    return (*iter).second;
  }

  std::shared_ptr<value_type> mutable_data(const PROJECT_NAMESPACE_ID::object_cache_key &cache_key) {
    auto iter = data_.find(cache_key);
    if (iter != data_.end() && !(*iter).second) {
      FWLOGINFO("Remove cache {}:{}:{}", static_cast<uint32_t>((*iter).first.cache_type()), (*iter).first.zone_id(),
                (*iter).first.instance_id());
      data_.erase(iter);
      iter = data_.end();
    }

    if (iter == data_.end()) {
      iter = data_.insert_key_value(cache_key, std::make_shared<value_type>(cache_key)).first;
      if (iter == data_.end()) {
        return nullptr;
      }
      (*iter).second->update_next_check_time();

      FWLOGINFO("Insert cache {}:{}:{} now sz {}", static_cast<uint32_t>(cache_key.cache_type()), cache_key.zone_id(),
                cache_key.instance_id(), data_.size());
    }

    if ((*iter).second) {
      (*iter).second->visit_update();
    }

    return (*iter).second;
  }

 private:
  util::mempool::lru_map<PROJECT_NAMESPACE_ID::object_cache_key, value_type, rpc::cache_api::cache_key_hash_t,
                         rpc::cache_api::cache_key_equal_t>
      data_;
  pull_data_fn_t pull_data_fn_;
  pack_data_fn_t pack_data_fn_;
  update_meta_fn_t update_meta_fn_;
};
