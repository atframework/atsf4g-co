// Copyright 2026 atframework

#include "rpc/cache/cache_api.h"

#include <log/log_wrapper.h>
#include <nostd/function_ref.h>
#include <std/explicit_declare.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/config/svr.protocol.config.pb.h>
#include <protocol/pbdesc/cache_service.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.protocol.pb.h>
#include <protocol/pbdesc/svr.struct.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframe/etcdcli/etcd_discovery.h>

#include <config/extern_service_types.h>
#include <config/logic_config.h>
#include <config/server_frame_build_feature.h>

#include <dispatcher/task_action_ss_req_base.h>

#include <utility/protobuf_mini_dumper.h>

#include <logic/logic_server_setup.h>

#include <rpc/rpc_utils.h>

#include <logic/hpa/logic_hpa_easy_api.h>

#include <cstdint>
#include <cstring>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "rpc/cache/cachesvrservice.h"
#include "rpc/rpc_shared_message.h"

namespace rpc {

namespace cache_api {
namespace {
struct pull_group_t {
  uint64_t cachesvr_id;
  PROJECT_NAMESPACE_ID::SSCachePullCacheReq *req_body;
  PROJECT_NAMESPACE_ID::SSCachePullCacheRsp *rsp_body;
};
}  // namespace

CACHE_RPC_API uint64_t get_cachesvr_server_id(PROJECT_NAMESPACE_ID::EnCacheApiCacheType cache_type,
                                              EXPLICIT_UNUSED_ATTR uint32_t zone_id, uint64_t instance_id) {
  logic_server_common_module *mod = logic_server_last_common_module();
  if (mod == nullptr) {
    return 0;
  }

  auto discovery_set =
      mod->get_discovery_index_by_type(static_cast<uint64_t>(atfw::component::logic_service_type::kCacheSvr));
  if (!discovery_set) {
    return 0;
  }

  uint32_t cache_type_id = static_cast<uint32_t>(cache_type);
  unsigned char buffer[sizeof(cache_type_id) + sizeof(instance_id)] = {0};

  memcpy(buffer, &cache_type_id, sizeof(cache_type_id));
  memcpy(buffer + sizeof(cache_type_id), &instance_id, sizeof(instance_id));

  auto node = discovery_set->get_node_hash_by_consistent_hash(
      buffer,
      logic_hpa_discovery_select(PROJECT_NAMESPACE_ID::config::logic_discovery_selector_cfg::kCachesvrFieldNumber,
                                 logic_hpa_discovery_select_mode::kReady));
  if (!node.node) {
    return 0;
  }

  return node.node->get_discovery_info().id();
}

CACHE_RPC_API uint64_t get_cachesvr_server_id(const PROJECT_NAMESPACE_ID::object_cache_key &cache_key) {
  return get_cachesvr_server_id(cache_key.cache_type(), cache_key.zone_id(), cache_key.instance_id());
}

// NOLINTNEXTLINE(misc-use-internal-linkage)
CACHE_RPC_API uint64_t get_cachesvr_server_id(const PROJECT_NAMESPACE_ID::object_cache_watch_key &watch_key) {
  return get_cachesvr_server_id(watch_key.cache_type(), watch_key.zone_id(), watch_key.instance_id());
}

CACHE_RPC_API bool has_cachesvr() {
  logic_server_common_module *mod = logic_server_last_common_module();
  if (mod == nullptr) {
    return false;
  }

  atfw::util::memory::strong_rc_ptr<atapp::etcd_discovery_set> discovery_set =
      mod->get_discovery_index_by_type(static_cast<uint64_t>(atfw::component::logic_service_type::kCacheSvr));
  if (!discovery_set) {
    return false;
  }

  return !discovery_set
              ->get_sorted_nodes(logic_hpa_discovery_select(
                  PROJECT_NAMESPACE_ID::config::logic_discovery_selector_cfg::kCachesvrFieldNumber,
                  logic_hpa_discovery_select_mode::kReady))
              .empty();
}

// NOLINTNEXTLINE(misc-use-internal-linkage)
CACHE_RPC_API rpc::result_code_type batch_get_cache(
    ::rpc::context &ctx, const PROJECT_NAMESPACE_ID::object_cache_watcher &watcher,
    ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_pull_key> &&cache_pull_keys,
    ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_content> &cache_contents) {
  cache_contents.Reserve(cache_pull_keys.size());
  // 按服务器ID分组
  std::list<pull_group_t> pending_to_send;
  {
    std::unordered_map<uint64_t, ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_pull_key> *>
        pull_keys_by_server_id;
    for (auto &cache_pull_key : cache_pull_keys) {
      uint64_t server_inst_id = rpc::cache_api::get_cachesvr_server_id(cache_pull_key.cache_key());
      if (0 == server_inst_id) {
        continue;
      }

      PROJECT_NAMESPACE_ID::object_cache_pull_key *pull_key = nullptr;
      auto iter = pull_keys_by_server_id.find(server_inst_id);
      if (iter == pull_keys_by_server_id.end()) {
        ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_pull_key> *pull_keys =
            ctx.create< ::google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::object_cache_pull_key> >();
        if (nullptr != pull_keys) {
          pull_keys->Reserve(cache_pull_keys.size());
          pull_key = pull_keys->Add();

          pull_keys_by_server_id[server_inst_id] = pull_keys;
        }
      } else {
        pull_key = iter->second->Add();
      }

      if (nullptr == pull_key) {
        break;
      }

      protobuf_move_message(*pull_key, std::move(cache_pull_key));

      if (iter != pull_keys_by_server_id.end()) {
        // 最多50个一组，防止一次性拉取过多包过大
        if (iter->second->size() >= 50) {
          PROJECT_NAMESPACE_ID::SSCachePullCacheReq *req_body = ctx.create<PROJECT_NAMESPACE_ID::SSCachePullCacheReq>();
          PROJECT_NAMESPACE_ID::SSCachePullCacheRsp *rsp_body = ctx.create<PROJECT_NAMESPACE_ID::SSCachePullCacheRsp>();
          if (nullptr == req_body || nullptr == rsp_body) {
            FWLOGERROR("malloc SSCachePullCacheReq or SSCachePullCacheRsp failed.");
          } else {
            protobuf_copy_message(*req_body->mutable_watcher(), watcher);
            protobuf_move_message(*req_body->mutable_pull_keys(), std::move(*iter->second));
            pending_to_send.push_back(pull_group_t{iter->first, req_body, rsp_body});
          }

          pull_keys_by_server_id.erase(iter);
        }
      }
    }

    for (auto &left_keys : pull_keys_by_server_id) {
      PROJECT_NAMESPACE_ID::SSCachePullCacheReq *req_body = ctx.create<PROJECT_NAMESPACE_ID::SSCachePullCacheReq>();
      PROJECT_NAMESPACE_ID::SSCachePullCacheRsp *rsp_body = ctx.create<PROJECT_NAMESPACE_ID::SSCachePullCacheRsp>();
      if (nullptr == req_body || nullptr == rsp_body) {
        FWLOGERROR("malloc SSCachePullCacheReq or SSCachePullCacheRsp failed.");
      } else {
        protobuf_copy_message(*req_body->mutable_watcher(), watcher);
        protobuf_move_message(*req_body->mutable_pull_keys(), std::move(*left_keys.second));
        pending_to_send.push_back(pull_group_t{left_keys.first, req_body, rsp_body});
      }
    }
  }

  if (pending_to_send.empty()) {
    RPC_RETURN_CODE(0);
  }

  // 调用发送接口，记录sequence
  std::unordered_set<dispatcher_await_options> waiter_options_set;
  std::unordered_map<uint64_t, atframework::SSMsg *> waiter_messages;
  waiter_options_set.reserve(pending_to_send.size());
  waiter_messages.reserve(pending_to_send.size());
  for (auto &pull_group : pending_to_send) {
    dispatcher_await_options one_waiter_options = dispatcher_make_default<dispatcher_await_options>();
    int res = RPC_AWAIT_CODE_RESULT(rpc::cache::pull_caches(ctx, pull_group.cachesvr_id, *pull_group.req_body,
                                                            *pull_group.rsp_body, false, &one_waiter_options));
    if (res >= 0 && one_waiter_options.sequence > 0) {
      waiter_messages[one_waiter_options.sequence] = ctx.create<atframework::SSMsg>();
      waiter_options_set.insert(one_waiter_options);
    } else {
      FWLOGERROR("try to call rpc::call::pull_caches to {:#x} failed, res: {}({})", pull_group.cachesvr_id, res,
                 protobuf_mini_dumper_get_error_msg(res));
    }
  }

  int ret = RPC_AWAIT_CODE_RESULT(rpc::wait(ctx, waiter_options_set, waiter_messages));
  if (ret < 0) {
    FWLOGERROR("try to call rpc::call::pull_caches for {} times and wait failed, res: {}({})",
               waiter_options_set.size(), ret, protobuf_mini_dumper_get_error_msg(ret));
    RPC_RETURN_CODE(ret);
  }

  // 回包合并
  for (auto &waiter : waiter_messages) {
    if (nullptr == waiter.second) {
      continue;
    }

    if (!waiter.second->has_head()) {
      continue;
    }

    if (waiter.second->head().rpc_response().type_url() !=
        PROJECT_NAMESPACE_ID::SSCachePullCacheRsp::descriptor()->full_name()) {
      FWLOGERROR("rpc {}.{} expect response message {}, but got {}", PROJECT_NAMESPACE, "CachesvrService.pull_caches",
                 PROJECT_NAMESPACE_ID::SSCachePullCacheRsp::descriptor()->full_name(),
                 waiter.second->head().rpc_response().type_url());
      continue;
    }

    if (waiter.second->body_bin().empty()) {
      continue;
    }

    PROJECT_NAMESPACE_ID::SSCachePullCacheRsp *rsp_body = ctx.create<PROJECT_NAMESPACE_ID::SSCachePullCacheRsp>();
    if (nullptr == rsp_body) {
      FWLOGERROR("malloc SSCachePullCacheRsp failed");
      continue;
    }

    if (false == rsp_body->ParseFromString(waiter.second->body_bin())) {
      FWLOGERROR("rpc {}.{} parse message {} for failed, msg: {}", PROJECT_NAMESPACE, "CachesvrService.pull_caches",
                 PROJECT_NAMESPACE_ID::SSCachePullCacheRsp::descriptor()->full_name(),
                 rsp_body->InitializationErrorString());

      continue;
    }

    FWLOGDEBUG("rpc {}.{} parse message {} success:\n{}", PROJECT_NAMESPACE, "CachesvrService.pull_caches",
               PROJECT_NAMESPACE_ID::SSCachePullCacheRsp::descriptor()->full_name(),
               protobuf_mini_dumper_get_readable(*rsp_body));

    for (int i = 0; i < rsp_body->content_size(); ++i) {
      PROJECT_NAMESPACE_ID::object_cache_content *output_content = cache_contents.Add();
      if (nullptr == output_content) {
        FWLOGERROR("malloc object_cache_content failed");
        continue;
      }
      PROJECT_NAMESPACE_ID::object_cache_content *input_content = rsp_body->mutable_content(i);
      if (nullptr == input_content) {
        continue;
      }

      protobuf_move_message(*output_content, std::move(*input_content));
    }
  }

  RPC_RETURN_CODE(ret);
}

CACHE_RPC_API void pick_key_from_meta(::rpc::context &ctx, PROJECT_NAMESPACE_ID::object_cache_key &output,
                                      const ::google::protobuf::Any &input) {
  auto unpack_message = rpc::make_shared_message<PROJECT_NAMESPACE_ID::DCacheApiMetaData>(ctx);
  if (!unpack_cache_meta_from_any(ctx, *unpack_message, input)) {
    FWLOGERROR("unpack cache meta from any failed, got type_url: {}, size: {}, unpack to {}", input.type_url(),
               input.value().size(), unpack_message->GetDescriptor()->full_name());
    return;
  }

  pick_key_from_meta(ctx, output, *unpack_message);
}

CACHE_RPC_API rpc::result_void_type set_cache_expired(::rpc::context &ctx,
                                                      PROJECT_NAMESPACE_ID::EnCacheApiCacheType cache_type,
                                                      uint32_t zone_id, uint64_t instance_id) {
  PROJECT_NAMESPACE_ID::SSCacheSetExpiredSync *request_body = ctx.create<PROJECT_NAMESPACE_ID::SSCacheSetExpiredSync>();
  if (nullptr == request_body) {
    FWLOGERROR("malloc SSCacheSetExpiredSync failed");
    RPC_RETURN_VOID;
  }

  auto *key = request_body->mutable_expired_keys()->Add();
  if (nullptr == key) {
    FWLOGERROR("malloc object_cache_key failed");
    RPC_RETURN_VOID;
  }
  key->set_cache_type(cache_type);
  key->set_zone_id(zone_id);
  key->set_instance_id(instance_id);
  auto ret = RPC_AWAIT_CODE_RESULT(
      rpc::cache::set_expired(ctx, get_cachesvr_server_id(cache_type, zone_id, instance_id), *request_body));
  if (ret != 0) {
    FWLOGERROR("set key(type:{}, zone_id:{}, instance_id:{}) expired failed, ret {}", static_cast<int>(cache_type),
               zone_id, instance_id, ret);
  }
  RPC_RETURN_VOID;
}

}  // namespace cache_api
}  // namespace rpc
