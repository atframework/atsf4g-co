// Copyright 2026 atframework

#include "logic/room/team_room.h"

#include <log/log_wrapper.h>
#include <nostd/nullability.h>
#include <std/explicit_declare.h>
#include <string/string_format.h>
#include <time/time_utility.h>

#include <atframe/atapp_conf.h>
#include <config/logic_config.h>
#include <logic/logic_server_setup.h>
#include <memory/object_allocator.h>
#include <rpc/rpc_async_invoke.h>
#include <rpc/rpc_context.h>
#include <rpc/rpc_shared_message.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/dynamic_message.h>
#include <protocol/config/team_room.config.pb.h>
#include <protocol/pbdesc/com.struct.dtmq.pb.h>
#include <protocol/pbdesc/dtmq_proxy.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/extern_service_types.h>

#include <data/user_key_hash_helper.h>
#include <utility/protobuf_mini_dumper.h>

#include <rpc/dtmq/dtmq_client_api.h>
#include <rpc/team/team_common_api.h>

#include <algorithm>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "logic/room/team_room_manager.h"

namespace {
static const atfw::team::config::teamsvr_room_cfg& get_teamsvr_room_cfg() noexcept {
  return logic_config::me()->get_server_instance_config<atfw::team::config::teamsvr_room_cfg>();
}

static gsl::string_view get_timer_event_name(team_room_timer_event_type event_type) noexcept {
  switch (event_type) {
    case team_room_timer_event_type::kAcquireLock:
      return "team_room.timer_event.acquire_lock";
    case team_room_timer_event_type::kMaintenance:
      return "team_room.timer_event.maintenance";
    case team_room_timer_event_type::kKickOfflineMember:
      return "team_room.timer_event.kick_offline_member";
    case team_room_timer_event_type::kDestroyEmptyRoom:
      return "team_room.timer_event.destroy_empty_room";
    case team_room_timer_event_type::kDestroyChannel:
      return "team_room.timer_event.destroy_channel";
    default:
      return "team_room.timer_event.unknown";
  }
}

// 用 repeated 的 key-value 元素整体替换 unordered_map(同 key 后写覆盖先写)，并清空源字段。
// 内存中共享数据统一以 unordered_map 维护 key-value 关系，proto 的 repeated 字段仅用于线上协议与快照
static void move_team_any_data_to_map(std::unordered_map<int64_t, atfw::team::DTeamAnyData>& output,
                                      google::protobuf::RepeatedPtrField<atfw::team::DTeamAnyDataWithKey>& from) {
  output.clear();
  for (auto& kv : from) {
    protobuf_move_message(output[kv.key()], std::move(*kv.mutable_value()));
  }
  from.Clear();
}

// 将 unordered_map 中的共享数据回填到 repeated 字段(dump 快照/构建下发事件时使用)
static void dump_team_any_data_from_map(google::protobuf::RepeatedPtrField<atfw::team::DTeamAnyDataWithKey>* output,
                                        const std::unordered_map<int64_t, atfw::team::DTeamAnyData>& from) {
  if (nullptr == output) {
    return;
  }
  for (const auto& kv : from) {
    auto* entry = output->Add();
    entry->set_key(kv.first);
    protobuf_copy_message(*entry->mutable_value(), kv.second);
  }
}

// 仅拷贝 EN_TEAM_PERMISSION_TYPE_PUBLIC 权限的数据(下发给未入队玩家时隐藏成员权限数据)
static void copy_public_permission_data(const std::unordered_map<int64_t, atfw::team::DTeamAnyData>& from,
                                        google::protobuf::RepeatedPtrField<atfw::team::DTeamAnyDataWithKey>* output) {
  if (nullptr == output) {
    return;
  }
  for (const auto& kv : from) {
    if (kv.second.permission() == atfw::team::EN_TEAM_PERMISSION_TYPE_PUBLIC) {
      auto* entry = output->Add();
      entry->set_key(kv.first);
      protobuf_copy_message(*entry->mutable_value(), kv.second);
    }
  }
}

// 判断 Any 类型名是否为知名包装类型(google.protobuf.*Value)。这类消息只有单个标量字段，
// 序列化字节是规范形式: 字节相同解包出来必然一致，直接字节比较即可，无需解包展开
// 判断消息类型的序列化字节是否为规范形式(字节相同当且仅当语义相同)，命中时 Any 比较无需解包展开:
// 仅有一个字段且该字段是单数标量(非 message/group、非 map、非 repeated)，
// 知名包装类型(google.protobuf.*Value)即属此类。
// 不适用情形: repeated 数值存在 packed/unpacked 两种合法编码；message/map 的字节与字段/条目顺序相关。
// 注意若发送方携带本端未知的新字段(进入 unknown field set)，字节会不同而已知字段语义相同，
// 此时按不相等处理(保守拒绝，客户端观察到最新数据后重试即可)
static bool team_any_has_canonical_bytes(const google::protobuf::Descriptor* descriptor) {
  if (nullptr == descriptor || descriptor->field_count() != 1) {
    return false;
  }
  const auto* field = descriptor->field(0);
  return !field->is_repeated() && field->cpp_type() != google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE;
}

// 语义比较两个 Any 的值。注意不能只做字节比较: Any.value 是内层消息的序列化字节，protobuf
// 序列化不是规范化的(字段顺序、内层 map 条目顺序、未知字段、不同语言客户端的序列化实现
// 都可能产生不同字节)，字节不同不代表语义不等。
// 规范字节类型(单标量字段，见 team_any_has_canonical_bytes)直接字节比较；其余快路径字节相等
// 直接通过，否则类型可解析时解包成消息按字段语义比较(与打包布局无关)；类型不在描述符池中
// (未知类型)回退为字节比较；解析失败视为不相等。
// arena 非空时解包临时对象分配在 arena 上(通常是 rpc::context 绑定的任务 arena)，减少内存碎片
static bool team_any_value_equal(const google::protobuf::Any& l, const google::protobuf::Any& r,
                                 google::protobuf::Arena* arena) {
  if (l.type_url() != r.type_url()) {
    return false;
  }

  // type_url 格式为 "<prefix>/<full.message.Name>"
  size_t slash_pos = l.type_url().find_last_of('/');
  const google::protobuf::Descriptor* descriptor = nullptr;
  if (std::string::npos == slash_pos) {
    descriptor = google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(l.type_url());
  } else {
    descriptor = google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
        {l.type_url().data() + slash_pos + 1, l.type_url().size() - slash_pos - 1});
  }

  // 规范字节类型: 字节比较即语义比较
  if (team_any_has_canonical_bytes(descriptor)) {
    return l.value() == r.value();
  }
  // 快路径: 字节相等必然语义相等
  if (l.value() == r.value()) {
    return true;
  }
  // 未知类型(含空 type_url)无法解包，字节不同即不相等
  if (nullptr == descriptor) {
    return false;
  }

  google::protobuf::DynamicMessageFactory factory;
  const auto* prototype = factory.GetPrototype(descriptor);
  if (nullptr == prototype) {
    return false;
  }
  google::protobuf::Message* l_message = nullptr;
  google::protobuf::Message* r_message = nullptr;
  // 无 arena 时的堆分配兜底(arena 上的对象由 arena 统一释放，不需要 holder)
  std::unique_ptr<google::protobuf::Message> l_holder;
  std::unique_ptr<google::protobuf::Message> r_holder;
  if (nullptr != arena) {
    l_message = prototype->New(arena);
    r_message = prototype->New(arena);
  } else {
    l_holder.reset(prototype->New());
    r_holder.reset(prototype->New());
    l_message = l_holder.get();
    r_message = r_holder.get();
  }
  if (nullptr == l_message || nullptr == r_message || !l_message->ParseFromString(l.value()) ||
      !r_message->ParseFromString(r.value())) {
    return false;
  }
  return atfw::atapp::protobuf_equal(*l_message, *r_message);
}

// 语义比较 DTeamAnyData: 权限级别 + Any 数据(见 team_any_value_equal)
static bool team_any_data_equal(const atfw::team::DTeamAnyData& l, const atfw::team::DTeamAnyData& r,
                                google::protobuf::Arena* arena) {
  return l.permission() == r.permission() && team_any_value_equal(l.data(), r.data(), arena);
}

// GAP-11: 检查单个请求内 keyed repeated 字段的 key 唯一性。同一请求中同一字段出现重复 key
// 属于非法请求(拒绝且零写入)；跨请求的同 key 数据由归一化路径按"后写覆盖先写"合并
static bool team_any_data_keys_unique(const google::protobuf::RepeatedPtrField<atfw::team::DTeamAnyDataWithKey>& data) {
  std::unordered_set<int64_t> keys;
  keys.reserve(static_cast<size_t>(data.size()));
  for (const auto& item : data) {
    if (!keys.insert(item.key()).second) {
      return false;
    }
  }
  return true;
}

static bool team_any_value_keys_unique(
    const google::protobuf::RepeatedPtrField<atfw::team::DTeamAnyValueWithKey>& data) {
  std::unordered_set<int64_t> keys;
  keys.reserve(static_cast<size_t>(data.size()));
  for (const auto& item : data) {
    if (!keys.insert(item.key()).second) {
      return false;
    }
  }
  return true;
}

// GAP-11: 校验一条 DTeamAction 中全部 keyed repeated 字段(成员/队伍共享数据、admission 数据、
// 更新条件与成员条件组)。任一字段内 key 重复即整条请求非法
static bool team_action_keyed_data_valid(const atfw::team::DTeamAction& action) {
  switch (action.action_case()) {
    case atfw::team::DTeamAction::kAddMember:
      return team_any_data_keys_unique(action.add_member().shared_member_data());
    case atfw::team::DTeamAction::kMemberUpdate: {
      const auto& update = action.member_update();
      if (!team_any_data_keys_unique(update.shared_member_data())) {
        return false;
      }
      for (const auto& condition : update.condition()) {
        if (!team_any_value_keys_unique(condition.shared_team_data())) {
          return false;
        }
        for (const auto& group : condition.member_condition_group()) {
          if (!team_any_value_keys_unique(group.member_condition().shared_member_data())) {
            return false;
          }
        }
      }
      return true;
    }
    case atfw::team::DTeamAction::kTeamUpdate: {
      const auto& update = action.team_update();
      if (!team_any_data_keys_unique(update.shared_team_data())) {
        return false;
      }
      for (const auto& condition : update.condition()) {
        if (!team_any_value_keys_unique(condition.shared_team_data())) {
          return false;
        }
        for (const auto& group : condition.member_condition_group()) {
          if (!team_any_value_keys_unique(group.member_condition().shared_member_data())) {
            return false;
          }
        }
      }
      return true;
    }
    case atfw::team::DTeamAction::kAddInvitation: {
      const auto& invitation = action.add_invitation();
      if (!team_any_data_keys_unique(invitation.team_admission_data())) {
        return false;
      }
      for (const auto& member_data : invitation.member_admission_data()) {
        if (!team_any_data_keys_unique(member_data.member_admission_data())) {
          return false;
        }
      }
      return true;
    }
    case atfw::team::DTeamAction::kAddJoinRequest:
      return team_any_data_keys_unique(action.add_join_request().member_admission_data());
    default:
      return true;
  }
}

// GAP-09(2026-08-29 澄清): "有 key 但 value 为空或 type_url 为空则删除该 key"的删除标记语义
// 仅适用于 kTeamUpdate/kMemberUpdate 的 keyed 共享数据(见 apply_team_update/apply_member_update)；
// 邀请/加入请求/成员的 admission 数据为全量覆盖，不做按键合并。
// admission 的 repeated 数据(team_admission_data/member_admission_data)允许无序: 重复判定
// 需忽略这些字段的元素顺序，按 key 归一化为确定性顺序后复用 protobuf_equal 整体比较

// 按 key 升序排序 keyed repeated 字段(在传入消息的拷贝上执行，仅用于判重归一化，不改变存储顺序)
static void sort_team_any_data_by_key(google::protobuf::RepeatedPtrField<atfw::team::DTeamAnyDataWithKey>& field) {
  std::sort(field.pointer_begin(), field.pointer_end(),
            [](const atfw::team::DTeamAnyDataWithKey* l, const atfw::team::DTeamAnyDataWithKey* r) {
              return l->key() < r->key();
            });
}

// 邀请的 admission 数据归一化: team_admission_data 按 key 排序；member_admission_data 按
// user_key 排序，各成员的 member_admission_data 再按 key 排序
static void sort_invitation_admission_data(atfw::team::DTeamInvitation& value) {
  sort_team_any_data_by_key(*value.mutable_team_admission_data());
  auto& members = *value.mutable_member_admission_data();
  std::sort(members.pointer_begin(), members.pointer_end(),
            [](const atfw::team::DTeamInvitation::MemberAdmissionData* l,
               const atfw::team::DTeamInvitation::MemberAdmissionData* r) {
              return user_key_less_t()(l->user_key(), r->user_key());
            });
  for (auto& member : members) {
    sort_team_any_data_by_key(*member.mutable_member_admission_data());
  }
}

// 邀请的顺序无关语义比较: 除 admission 列表顺序外逐字段相等即视为未变化
static bool invitation_semantically_equal(const atfw::team::DTeamInvitation& l, const atfw::team::DTeamInvitation& r) {
  atfw::team::DTeamInvitation normalized_l = l;
  atfw::team::DTeamInvitation normalized_r = r;
  sort_invitation_admission_data(normalized_l);
  sort_invitation_admission_data(normalized_r);
  return atfw::atapp::protobuf_equal(normalized_l, normalized_r);
}

// 加入请求的顺序无关语义比较: member_admission_data 按 key 归一化后整体比较
static bool join_request_semantically_equal(const atfw::team::DTeamJoinRequest& l,
                                            const atfw::team::DTeamJoinRequest& r) {
  atfw::team::DTeamJoinRequest normalized_l = l;
  atfw::team::DTeamJoinRequest normalized_r = r;
  sort_team_any_data_by_key(*normalized_l.mutable_member_admission_data());
  sort_team_any_data_by_key(*normalized_r.mutable_member_admission_data());
  return atfw::atapp::protobuf_equal(normalized_l, normalized_r);
}

// member_update 事件的空操作判定: 逐字段对照 apply_member_update 的应用语义
// (仅非空 client_version、非 0 user_router_server_id、已设置的 user_channel 会落地，
// 共享数据按 key 覆盖、空 value/type_url 项删除该 key)；所有会落地的字段都与现有值相等时，
// 写入频道日志不产生任何状态变化。
// 不携带任何可落地字段的空更新不判定为空操作(保留事件以维持锁探测等既有行为)，
// 成员不存在时同样不判定为空操作
static bool team_member_update_no_change(const atfw::team::DTeamMemberUpdateData& update_data,
                                         const team_room::member_runtime_data& member, google::protobuf::Arena* arena) {
  bool has_applicable_field = false;
  if (!update_data.client_version().empty()) {
    has_applicable_field = true;
    if (update_data.client_version() != member.member_data.client_version()) {
      return false;
    }
  }
  if (update_data.user_router_server_id() != 0) {
    has_applicable_field = true;
    if (update_data.user_router_server_id() != member.member_data.user_router_server_id()) {
      return false;
    }
  }
  if (update_data.has_user_channel()) {
    has_applicable_field = true;
    if (!atfw::atapp::protobuf_equal(update_data.user_channel(), member.member_data.user_channel())) {
      return false;
    }
  }
  for (const auto& kv : update_data.shared_member_data()) {
    auto it = member.shared_member_data.find(kv.key());
    // GAP-09 删除标记(value 的 type_url/payload 为空): 命中已存在的 key 才产生状态变化
    if (kv.value().data().type_url().empty() || kv.value().data().value().empty()) {
      if (it != member.shared_member_data.end()) {
        return false;
      }
      continue;
    }
    has_applicable_field = true;
    if (it == member.shared_member_data.end() || !team_any_data_equal(it->second, kv.value(), arena)) {
      return false;
    }
  }
  return has_applicable_field;
}

// team_update 事件的空操作判定: 对照 apply_team_update 的应用语义(configure 整体覆盖、
// 共享数据按 key 覆盖)。configure 要求双方均已完成默认门槛修订后再比较(send_action 写入前
// 修订事件载荷，storage_ 中的配置在 create_team/restore_snapshot/apply_team_update 后修订)。
// 与 member_update 相同，不携带任何可落地字段的空更新不判定为空操作
static bool team_update_no_change(const atfw::team::DTeamUpdateData& update_data,
                                  const atfw::team::DTeamConfigure& current_configure,
                                  const std::unordered_map<int64_t, atfw::team::DTeamAnyData>& current_shared_data,
                                  google::protobuf::Arena* arena) {
  bool has_applicable_field = update_data.has_configure();
  if (update_data.has_configure() && !atfw::atapp::protobuf_equal(update_data.configure(), current_configure)) {
    return false;
  }
  for (const auto& kv : update_data.shared_team_data()) {
    auto it = current_shared_data.find(kv.key());
    // GAP-09 删除标记(value 的 type_url/payload 为空): 命中已存在的 key 才产生状态变化
    if (kv.value().data().type_url().empty() || kv.value().data().value().empty()) {
      if (it != current_shared_data.end()) {
        return false;
      }
      continue;
    }
    has_applicable_field = true;
    if (it == current_shared_data.end() || !team_any_data_equal(it->second, kv.value(), arena)) {
      return false;
    }
  }
  return has_applicable_field;
}
// 百分比条件计算基准: 用整数交叉相乘(satisfied * kTeamPercentBase 与 total * percent)
// 代替浮点除法，避免精度问题
constexpr const int64_t kTeamPercentBase = 100;

// 最小/最大值范围检查: 0 表示该方向不限制
static inline bool team_condition_range_match(int64_t value,
                                              const atfw::team::DTeamConditionMinMaxValue& range) noexcept {
  if (range.min_value() != 0 && value < range.min_value()) {
    return false;
  }
  if (range.max_value() != 0 && value > range.max_value()) {
    return false;
  }
  return true;
}

// 共享数据等值检查(与关系): expected 中的每个 key 都必须存在于 actual 且 Any 值相等；
// actual 中额外存在的 key 不影响判定(条件只约束它列出的子集)
static bool team_shared_data_match(const std::unordered_map<int64_t, atfw::team::DTeamAnyData>& actual,
                                   const google::protobuf::RepeatedPtrField<atfw::team::DTeamAnyValueWithKey>& expected,
                                   google::protobuf::Arena* arena) {
  for (const auto& kv : expected) {
    auto it = actual.find(kv.key());
    if (it == actual.end() || !team_any_value_equal(it->second.data(), kv.value(), arena)) {
      return false;
    }
  }
  return true;
}

// 单个成员是否满足成员条件(与关系): 共享成员数据等值 + 角色范围
static bool team_member_condition_match(const atfw::team::DTeamMemberConditionChecker& condition,
                                        const team_room::member_runtime_data& member, google::protobuf::Arena* arena) {
  if (!team_shared_data_match(member.shared_member_data, condition.shared_member_data(), arena)) {
    return false;
  }
  return !condition.has_permission() ||
         team_condition_range_match(static_cast<int64_t>(member.member_data.role()), condition.permission());
}

// 角色门槛解析: GUEST/NORMAL/ADMIN/OWNER 只是当前定义的档位参考点，方便以后在任意档位之间按需
// 插入新角色。因此角色与门槛的比较一律按大小进行，不做等值判断；配置值不高于 GUEST(0，含非法
// 负值)视为未配置，使用默认门槛，其余值(包括低于 NORMAL 或高于 OWNER 的自定义档位)按数值直接生效。
static inline atfw::team::EnTeamPermissionRole resolve_permission_role(
    atfw::team::EnTeamPermissionRole role, atfw::team::EnTeamPermissionRole default_role) noexcept {
  if (role <= atfw::team::EN_TEAM_MEMBER_ROLE_GUEST) {
    return default_role;
  }
  return role;
}

// 修订配置中的角色门槛默认值: 未配置(不高于 GUEST)的门槛就地改写为各字段默认值。
// storage_.configure 每次被修改后都必须重新修订(create_team/restore_snapshot/apply_team_update)，
// 且 team_update 事件在写入频道日志前同样修订(send_action)，保证随快照和增量事件下发给
// member 订阅者的始终是修订后的完整配置，订阅者无需再自行补默认值。
static inline void revise_configure_default_permission(atfw::team::DTeamConfigure& configure) {
  configure.set_manage_member_role(
      resolve_permission_role(configure.manage_member_role(), atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN));
  configure.set_approve_join_request_role(
      resolve_permission_role(configure.approve_join_request_role(), atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
  configure.set_invite_role(resolve_permission_role(configure.invite_role(), atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
  configure.set_update_team_data_role(
      resolve_permission_role(configure.update_team_data_role(), atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL));
  configure.set_reject_invitation_role(
      resolve_permission_role(configure.reject_invitation_role(), atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN));
  configure.set_set_member_role_role(
      resolve_permission_role(configure.set_member_role_role(), atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN));
}

// GAP-11: 检查 action 内嵌 team key 是否与当前房间不一致(含未设置)。只有携带内嵌 team 标识的
// action 种类需要判定(destroy_team/remove_member/invitation/join_request，见 com.struct.team.proto)
static inline bool action_team_key_mismatch(const atfw::team::DTeamAction& action,
                                            const atfw::team::DTeamKey& team_key) noexcept {
  switch (action.action_case()) {
    case atfw::team::DTeamAction::kDestroyTeam:
      return !rpc::team::team_api::team_key_equal_t()(action.destroy_team(), team_key);
    case atfw::team::DTeamAction::kRemoveMember:
      return !rpc::team::team_api::team_key_equal_t()(action.remove_member().team_key(), team_key);
    case atfw::team::DTeamAction::kAddInvitation:
      return !rpc::team::team_api::team_key_equal_t()(action.add_invitation().team_key(), team_key);
    case atfw::team::DTeamAction::kApproveInvitation:
      return !rpc::team::team_api::team_key_equal_t()(action.approve_invitation().team_key(), team_key);
    case atfw::team::DTeamAction::kRejectInvitation:
      return !rpc::team::team_api::team_key_equal_t()(action.reject_invitation().team_key(), team_key);
    case atfw::team::DTeamAction::kAddJoinRequest:
      return !rpc::team::team_api::team_key_equal_t()(action.add_join_request().team_key(), team_key);
    case atfw::team::DTeamAction::kApproveJoinRequest:
      return !rpc::team::team_api::team_key_equal_t()(action.approve_join_request().team_key(), team_key);
    case atfw::team::DTeamAction::kRejectJoinRequest:
      return !rpc::team::team_api::team_key_equal_t()(action.reject_join_request().team_key(), team_key);
    default:
      return false;
  }
}

// GAP-11: 把 action 内嵌 team key 统一改写为当前房间完整 team key(调用前先经 action_team_key_mismatch 判定)
static inline void normalize_action_team_key(atfw::team::DTeamAction& action, const atfw::team::DTeamKey& team_key) {
  switch (action.action_case()) {
    case atfw::team::DTeamAction::kDestroyTeam:
      protobuf_copy_message(*action.mutable_destroy_team(), team_key);
      break;
    case atfw::team::DTeamAction::kRemoveMember:
      protobuf_copy_message(*action.mutable_remove_member()->mutable_team_key(), team_key);
      break;
    case atfw::team::DTeamAction::kAddInvitation:
      protobuf_copy_message(*action.mutable_add_invitation()->mutable_team_key(), team_key);
      break;
    case atfw::team::DTeamAction::kApproveInvitation:
      protobuf_copy_message(*action.mutable_approve_invitation()->mutable_team_key(), team_key);
      break;
    case atfw::team::DTeamAction::kRejectInvitation:
      protobuf_copy_message(*action.mutable_reject_invitation()->mutable_team_key(), team_key);
      break;
    case atfw::team::DTeamAction::kAddJoinRequest:
      protobuf_copy_message(*action.mutable_add_join_request()->mutable_team_key(), team_key);
      break;
    case atfw::team::DTeamAction::kApproveJoinRequest:
      protobuf_copy_message(*action.mutable_approve_join_request()->mutable_team_key(), team_key);
      break;
    case atfw::team::DTeamAction::kRejectJoinRequest:
      protobuf_copy_message(*action.mutable_reject_join_request()->mutable_team_key(), team_key);
      break;
    default:
      break;
  }
}

static team_room* get_team_room_from_subscriber(const rpc::dtmq::client_subscriber::ptr_t& subscriber,
                                                gsl::string_view callback_name) {
  if (!subscriber) {
    return nullptr;
  }
  auto local_private_data = subscriber->get_local_private_data();
  if (local_private_data.empty()) {
    FWLOGERROR("team_room {} callback missing local_private_data", callback_name);
    return nullptr;
  }

  static_assert(sizeof(team_room*) == sizeof(*local_private_data.data()), "team_room* size mismatch");
  // NOLINTNEXTLINE(performance-no-int-to-ptr)
  return reinterpret_cast<team_room*>(*local_private_data.data());
}

static rpc::dtmq::client_subscriber::event_callback_set_ptr_t build_shared_team_room_channel_event_callback_set() {
  rpc::dtmq::client_subscriber::event_callback_set_ptr_t ret =
      rpc::dtmq::client_subscriber::create_event_callback_set();

  rpc::dtmq::client_subscriber::set_event_callback_on_receive_snapshot_finished(
      *ret, [](rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber,
               const ::atfw::dtmq::DChannelSnapshot& /*snapshot*/, int32_t result_code) {
        team_room* room = get_team_room_from_subscriber(subscriber, "on_receive_snapshot_finished");
        if (room != nullptr) {
          room->on_receive_snapshot_finished(ctx, subscriber, result_code);
        }
      });

  // 必须订阅 raw message 而不是 event: DTMQ 频道日志不止 kEvent，服务端在 update/reset_lock/send_message
  // 携带锁检查器成功时都会追加 kResetLock 日志(见 mq_channel::set_lock append_log)，还有 kCreate/kDestroy/kText。
  // 乐观锁每次续租都会产生一条 kResetLock 日志，若只监听 event，空队伍的 ack 和最老未压缩日志时间点都不会前进，
  // 按时间维度的压缩加速将无法触发。common_action 保证 ready 后每种 command_case 恰好回调一次。
  rpc::dtmq::client_subscriber::set_event_callback_on_receive_raw_message(
      *ret, [](rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber,
               const ::atfw::dtmq::DChannelMessage& data) {
        team_room* room = get_team_room_from_subscriber(subscriber, "on_receive_raw_message");
        if (room != nullptr) {
          room->on_receive_raw_message(ctx, subscriber, data);
        }
      });

  rpc::dtmq::client_subscriber::set_event_callback_on_update_optimistic_lock(
      *ret, [](rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber,
               const ::atfw::dtmq::DChannelOptimisticLock& from, const ::atfw::dtmq::DChannelOptimisticLock& to) {
        team_room* room = get_team_room_from_subscriber(subscriber, "on_update_optimistic_lock");
        if (room != nullptr) {
          room->on_update_optimistic_lock(ctx, subscriber, from, to);
        }
      });

  rpc::dtmq::client_subscriber::set_event_callback_on_destroyed(
      *ret, [](rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber, int64_t /*log_sequence*/,
               std::chrono::system_clock::time_point /*destroy_time*/) {
        team_room* room = get_team_room_from_subscriber(subscriber, "on_destroyed");
        if (room != nullptr) {
          room->on_destroyed(ctx, subscriber);
        }
      });
  return ret;
}

static rpc::dtmq::client_subscriber::event_callback_set_ptr_t& get_shared_team_room_channel_event_callback_set() {
  static rpc::dtmq::client_subscriber::event_callback_set_ptr_t ret =
      build_shared_team_room_channel_event_callback_set();
  return ret;
}
}  // namespace

struct team_room::iterating_member_protect_t {
  std::unordered_map<PROJECT_NAMESPACE_ID::DUserIDKey, member_ptr_t, user_key_hash_t, user_key_equal_t> pending_to_add;
  std::unordered_map<PROJECT_NAMESPACE_ID::DUserIDKey, member_ptr_t, user_key_hash_t, user_key_equal_t>
      pending_to_remove;
};

team_room::team_room(ctor_guard&, const atfw::team::DTeamKey& team_key, const atfw::dtmq::DChannelIdKey& channel_key,
                     std::string&& subscriber_key, std::string&& lock_holder,
                     atfw::util::nostd::nonnull<rpc::dtmq::client_subscriber::ptr_t>&& subscriber)
    : team_key_(team_key),
      subscriber_key_(std::move(subscriber_key)),
      lock_holder_(std::move(lock_holder)),
      subscriber_(std::move(subscriber)) {
  protobuf_copy_message(channel_key_, channel_key);
  protobuf_copy_message(*storage_.mutable_team_key(), team_key_);

  // 创建之后，加载快照数据前重置一下用于延迟删除的空房间计时器，避免在创建后立即触发删除
  refresh_empty_tracking(restore_timepoint_);
}

team_room::ptr_t team_room::create(rpc::context& ctx, const atfw::team::DTeamKey& team_key) {
  // 乐观锁持有者标识与服务节点名和节点ID相关，节点切换后新节点据此区分老锁
  gsl::string_view server_name = logic_config::me()->get_local_server_name();

  std::string lock_holder;
  if (server_name.empty()) {
    uint64_t server_id = logic_config::me()->get_local_server_id();
    lock_holder = atfw::util::string::format("teamsvr-room:{:#x}", server_id);
  } else {
    lock_holder = atfw::util::string::format("teamsvr-room:{}", server_name);
  }
  std::string subscriber_key = atfw::util::string::format("team_room:{}:{}", team_key.zone_id(), team_key.team_id());

  rpc::dtmq::client_subscriber::subscriber_options subscribe_options{subscriber_key};
  subscribe_options.with_private_data = true;
  subscribe_options.event_callback_set = get_shared_team_room_channel_event_callback_set();
  atfw::dtmq::DChannelIdKey channel_key;
  channel_key.CopyFrom(rpc::team::team_api::make_team_room_channel_key(team_key));
  auto subscriber = rpc::dtmq::client_subscriber::create(channel_key, subscribe_options);
  if (!subscriber) {
    FWLOGERROR("team_room create subscriber of channel {}:{} failed, maybe configure is missing",
               channel_key.channel_type(), channel_key.channel_id());
    return nullptr;
  }

  ctor_guard guard;
  auto room = atfw::component::memory::stl::make_strong_rc<team_room>(
      guard, team_key, channel_key, std::move(subscriber_key), std::move(lock_holder), std::move(subscriber));
  uintptr_t local_private_data[] = {reinterpret_cast<uintptr_t>(room.get())};
  room->subscriber_->set_local_private_data(local_private_data);
  if (!room->subscriber_->get_shared_event_callback_set()) {
    room->subscriber_->set_shared_event_callback_set(get_shared_team_room_channel_event_callback_set());
  }
  // 已就绪(共享层复用)则不会再触发 snapshot_finished，直接恢复当前缓存快照
  // NOLINTNEXTLINE(readability-redundant-nested-if)
  if (room->subscriber_->is_ready()) {
    if (!room->restore_snapshot(ctx)) {
      FCTXLOGERROR(ctx, "team room {}:{} restore reused subscriber snapshot failed", team_key.zone_id(),
                   team_key.team_id());
    }
  }

  // 每个房间有且只有一个定时器，创建后即开始调度
  room->schedule_next_timer();

  return room;
}

const atfw::team::DTeamKey& team_room::get_team_key() const noexcept { return team_key_; }

const atfw::dtmq::DChannelIdKey& team_room::get_channel_key() const noexcept { return channel_key_; }

bool team_room::is_subscriber_ready() const noexcept { return subscriber_->is_ready(); }

bool team_room::is_lock_holder() const noexcept { return lock_acquired_ && !subscriber_->is_destroyed(); }

void team_room::on_remove() { timer_watcher_.reset(); }

rpc::result_code_type team_room::await_ready(rpc::context& ctx) {
  if (!subscriber_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
  }
  if (!subscriber_->is_ready()) {
    auto await_ret = RPC_AWAIT_CODE_RESULT(rpc::dtmq::client_subscriber::global_await_pending_heartbeat(ctx));
    if (await_ret != 0) {
      FCTXLOGERROR(ctx, "team room {}:{} await pending heartbeat failed: {}", team_key_.zone_id(), team_key_.team_id(),
                   await_ret);
      RPC_RETURN_CODE(await_ret);
    }
    if (!subscriber_ || !subscriber_->is_ready()) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
    }
  }

  // NOLINTNEXTLINE(readability-redundant-nested-if)
  if (!snapshot_restored_) {
    if (!restore_snapshot(ctx)) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM_BAD_PACKAGE);
    }
  }
  RPC_RETURN_CODE(0);
}

rpc::result_code_type team_room::send_action(rpc::context& ctx, const atfw::team::DTeamAction& action, bool no_wait) {
  const atfw::team::DTeamAction* actions[] = {&action};
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(send_actions(ctx, actions, no_wait)));
}

rpc::result_code_type team_room::send_actions(rpc::context& ctx,
                                              gsl::span<const atfw::team::DTeamAction* const> actions, bool no_wait) {
  if (!subscriber_ || !subscriber_->is_ready()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
  }
  // 快照未成功恢复前不允许权威写入(座位带: action 层入口都会先 await_ready，这里兜底防止
  // 恢复失败/半恢复状态下向频道追加日志)
  if (!snapshot_restored_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
  }
  // 已销毁(收到 destroy_team/destroy 事件)后拒绝新的业务写入，避免向即将销毁的频道追加日志
  if (destroyed_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED);
  }
  // GAP-11: 同一请求内 keyed repeated 字段的 key 不允许重复，重复即拒绝且零写入
  for (const auto* action_ptr : actions) {
    if (nullptr != action_ptr && !team_action_keyed_data_valid(*action_ptr)) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
    }
  }

  auto* events = ctx.create<google::protobuf::RepeatedPtrField<google::protobuf::Any>>();
  if (nullptr == events) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM);
  }
  events->Reserve(static_cast<int>(actions.size()));

  bool need_schedule_next_timer = false;
  for (const auto* action : actions) {
    if (nullptr == action) {
      continue;
    }

    // 部分 action 自带内嵌 team key，统一改写为当前房间的完整 team key。
    // 频道由外层 team key 决定，事件日志绝不能携带指向其他队伍的矛盾标识
    rpc::context::message_holder<atfw::team::DTeamAction> normalized_action{ctx};
    const atfw::team::DTeamAction* action_ptr = action;
    auto mutable_action = [&normalized_action, &action_ptr, action]() -> atfw::team::DTeamAction& {
      if (action_ptr == action) {
        normalized_action->CopyFrom(*action);
        action_ptr = &(*normalized_action);
      }
      return *normalized_action;
    };
    if (action_team_key_mismatch(*action, team_key_)) {
      normalize_action_team_key(mutable_action(), team_key_);
    }

    // 更新条件仅作提交前的数据一致性检查(见 check_action_permission)，
    // 检查通过后按协议约定从最终事件数据中裁剪掉，不写入频道日志
    switch (action_ptr->action_case()) {
      case atfw::team::DTeamAction::kMemberUpdate:
        if (action_ptr->member_update().condition_size() > 0) {
          mutable_action().mutable_member_update()->clear_condition();
        }
        break;
      case atfw::team::DTeamAction::kTeamUpdate:
        if (action_ptr->team_update().condition_size() > 0) {
          mutable_action().mutable_team_update()->clear_condition();
        }
        // 配置变更写入频道日志前修订默认门槛，保证订阅者收到的增量事件携带完整配置
        if (action_ptr->team_update().has_configure()) {
          revise_configure_default_permission(*mutable_action().mutable_team_update()->mutable_configure());
        }
        break;
      default:
        break;
    }

    // member_update/team_update 去重: 事件载荷应用到当前状态不产生任何变化时直接跳过，
    // 避免周期性全量刷新等重复上报造成无效的频道日志增长与广播(判定口径见
    // team_member_update_no_change/team_update_no_change，与 apply 语义逐字段对应)。
    // 仅持锁者去重: 非持锁者保留原写入路径的乐观锁检查/CAS 接管语义(写入冲突退位、
    // 空更新作锁探测等)，去重只是持锁者写入路径上的纯优化
    bool skip_event = false;
    if (is_lock_holder()) {
      switch (action_ptr->action_case()) {
        case atfw::team::DTeamAction::kMemberUpdate: {
          auto member = find_member(action_ptr->member_update().user_key(), false);
          if (member &&
              team_member_update_no_change(action_ptr->member_update(), *member, ctx.get_protobuf_arena().get())) {
            skip_event = true;
          }
          break;
        }
        case atfw::team::DTeamAction::kTeamUpdate:
          // 此处的 configure 已在上方完成默认门槛修订，可与 storage_ 中的修订后配置直接比较
          if (team_update_no_change(action_ptr->team_update(), storage_.configure(), shared_team_data_,
                                    ctx.get_protobuf_arena().get())) {
            skip_event = true;
            break;
          }
          break;
        default:
          break;
      }
    }
    if (skip_event) {
      continue;
    }

    // 移除成员: 记录移除原因并加入重试队列，等待频道事件回环后真正移除；
    // 已在重试队列中说明移除消息在途，不再重复发送
    if (action_ptr->has_remove_member()) {
      const auto& user_key = action_ptr->remove_member().user_key();
      if (member_retry_remove_.end() != member_retry_remove_.find(user_key)) {
        continue;
      }
      auto member = find_member(user_key, false);
      if (member) {
        member->exit_reason = action_ptr->remove_member().remove_member_reason();

        auto retry_data = atfw::component::memory::stl::make_strong_rc<member_retry_data>();
        if (retry_data) {
          retry_data->next_retry_timepoint =
              atfw::util::time::time_utility::now() +
              protobuf_to_system_clock(get_teamsvr_room_cfg().member_channel_notification_retry_interval());
          member_retry_remove_.insert_key_value(user_key, std::move(retry_data));
          need_schedule_next_timer = true;
        }
      }
    }

    if (!events->Add()->PackFrom(*action_ptr)) {
      FCTXLOGERROR(ctx, "team room {}:{} pack DTeamAction failed", team_key_.zone_id(), team_key_.team_id());

      if (need_schedule_next_timer) {
        schedule_next_timer();
      }
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PACK);
    }
  }

  if (need_schedule_next_timer) {
    schedule_next_timer();
  }

  if (events->empty()) {
    RPC_RETURN_CODE(0);
  }
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(send_events_with_lock(ctx, std::move(*events), no_wait)));
}

rpc::result_code_type team_room::do_send_action(rpc::context& ctx, const atfw::team::DTeamAction& action,
                                                bool no_wait) {
  const atfw::team::DTeamAction* actions[] = {&action};
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(do_send_actions(ctx, actions, no_wait)));
}

rpc::result_code_type team_room::do_send_actions(rpc::context& ctx,
                                                 gsl::span<const atfw::team::DTeamAction* const> actions,
                                                 bool no_wait) {
  auto* events = ctx.create<google::protobuf::RepeatedPtrField<google::protobuf::Any>>();
  if (nullptr == events) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM);
  }
  events->Reserve(static_cast<int>(actions.size()));

  for (const auto* action : actions) {
    if (nullptr == action) {
      continue;
    }
    if (!events->Add()->PackFrom(*action)) {
      FCTXLOGERROR(ctx, "team room {}:{} pack DTeamAction failed", team_key_.zone_id(), team_key_.team_id());
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PACK);
    }
  }

  if (events->empty()) {
    RPC_RETURN_CODE(0);
  }
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(send_events_with_lock(ctx, std::move(*events), no_wait)));
}

rpc::result_code_type team_room::send_member_action(rpc::context& ctx, const atfw::dtmq::DChannelIdKey& channel_key,
                                                    const atfw::team::DTeamMemberAction& action) {
  const atfw::team::DTeamMemberAction* actions[] = {&action};
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(send_member_actions(ctx, channel_key, actions)));
}

rpc::result_code_type team_room::send_member_actions(rpc::context& ctx, const atfw::dtmq::DChannelIdKey& channel_key,
                                                     gsl::span<const atfw::team::DTeamMemberAction* const> actions) {
  if (channel_key.channel_type() == 0 || channel_key.channel_id().empty() || actions.empty()) {
    RPC_RETURN_CODE(0);
  }

  auto* details = ctx.create<google::protobuf::RepeatedPtrField<atfw::dtmq::DChannelMessageDetail>>();
  if (nullptr == details) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM);
  }
  details->Reserve(static_cast<int>(actions.size()));
  for (const auto* action : actions) {
    if (nullptr == action) {
      continue;
    }
    if (!details->Add()->mutable_event()->PackFrom(*action)) {
      FCTXLOGERROR(ctx, "team room {}:{} pack DTeamMemberAction failed", team_key_.zone_id(), team_key_.team_id());
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_PACK);
    }
  }
  if (details->empty()) {
    RPC_RETURN_CODE(0);
  }

  atfw::dtmq::channel_subscriber no_subscriber;
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::dtmq::send_message(ctx, std::move(no_subscriber), channel_key,
                                                                std::move(*details), nullptr, nullptr, false, true)));
}

rpc::result_code_type team_room::heartbeat(rpc::context& ctx, const atfw::team::SSTeamRoomHeartbeatReq& req) {
  if (!subscriber_ || !subscriber_->is_ready()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
  }
  if (destroyed_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED);
  }
  if (!lock_acquired_) {
    auto lock_ret = RPC_AWAIT_CODE_RESULT(acquire_lock(ctx));
    if (lock_ret != 0) {
      RPC_RETURN_CODE(lock_ret);
    }
  }

  auto member = find_member(req.user_key(), true);
  if (!member) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_MEMBER_NOT_FOUND);
  }

  // 更新成员已确认的日志序号(随 custom_data 下发，用于成员侧补差量)
  bool runtime_data_changed = false;
  if (req.sequence() > member->member_data.acknowledge_action_sequence()) {
    member->member_data.set_acknowledge_action_sequence(req.sequence());
    member->member_data.set_acknowledge_action_hash_code(req.hash_code());
    runtime_data_changed = true;
  }

  // 服务器如果触发反订阅，这里会传0
  if (req.user_router_server_id() > 0) {
    auto heartbeat_now = atfw::util::time::time_utility::now();
    if (member->last_heartbeat_timepoint < heartbeat_now) {
      member->last_heartbeat_timepoint = heartbeat_now;
      *member->member_data.mutable_last_heartbeat_timepoint() =
          protobuf_from_system_clock(member->last_heartbeat_timepoint);
      runtime_data_changed = true;
    }
  }
  if (member->user_router_server_id != req.user_router_server_id()) {
    member->user_router_server_id = req.user_router_server_id();
    member->member_data.set_user_router_server_id(req.user_router_server_id());
    runtime_data_changed = true;
  } else {
    member->member_data.set_user_router_server_id(req.user_router_server_id());
  }
  if (runtime_data_changed) {
    // GAP-07: 心跳只更新本地运行时状态、不写频道日志；标脏后由下一次维护随续租 update
    // 持久化 custom/private 快照(负载迁移转出前数据已保存)
    runtime_data_dirty_ = true;
  }
  RPC_RETURN_CODE(0);
}

rpc::result_code_type team_room::create_team(rpc::context& ctx, const atfw::team::SSTeamRoomCreateReq& req) {
  if (destroyed_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED);
  }
  if (req.sender_user_key().zone_id() == 0 || req.sender_user_key().user_id() == 0) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  }
  if (team_created_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION);
  }
  // GAP-11: 创建请求携带的 keyed 共享数据同样要求 key 唯一(重复即拒绝且零写入)
  if (!team_any_data_keys_unique(req.shared_team_data())) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  }

  // 先占位再让出协程，避免并发的 create_team 在首个 update 挂起期间同时通过检查而重复创建
  team_created_ = true;
  auto now = atfw::util::time::time_utility::now();
  // 预期频道尚不存在: 首帧 update 直接携带锁 CAS(空锁/过期锁/本节点锁才接受重置，语义与
  // acquire_lock 一致)，一次请求内完成 加锁+写入，省去独立的 reset_lock 往返
  ::atfw::dtmq::DChannelOptimisticLock self_lock;
  atfw::util::memory::strong_rc_ptr<::atfw::dtmq::channel_lock_checker> checker;
  if (lock_acquired_) {
    checker = make_write_lock_checker();
  } else {
    int32_t checker_ret = make_acquire_lock_checker(now, self_lock, checker);
    if (0 != checker_ret) {
      team_created_ = false;
      RPC_RETURN_CODE(checker_ret);
    }
  }
  auto public_data = rpc::make_shared_message<atfw::team::DTeamStorage>(ctx);
  auto private_data = rpc::make_shared_message<atfw::team::DTeamRoomPrivateData>(ctx);
  dump_team_key(*public_data->mutable_team_key());
  protobuf_copy_message(*public_data->mutable_captain_user_key(), req.sender_user_key());
  protobuf_copy_message(*public_data->mutable_configure(), req.configure());
  // 修订默认值后再写入频道快照与 storage_，保证下发给订阅者的配置总是完整门槛
  revise_configure_default_permission(*public_data->mutable_configure());
  protobuf_copy_message(*public_data->mutable_shared_team_data(), req.shared_team_data());

  auto* member_data = public_data->add_member();
  protobuf_copy_message(*member_data->mutable_user_key(), req.sender_user_key());
  protobuf_copy_message(*member_data->mutable_user_channel(), req.sender_user_channel());
  member_data->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
  // 创建者的版本/路由数据由其本人在创建时上报(与 approve_invitation/approve_join_request 的入队路径一致)
  member_data->set_client_version(req.client_version());
  member_data->set_user_router_server_id(req.user_router_server_id());
  *member_data->mutable_joined_timepoint() = protobuf_from_system_clock(now);
  *member_data->mutable_last_heartbeat_timepoint() = protobuf_from_system_clock(now);
  protobuf_copy_message(*member_data->mutable_shared_member_data(), req.shared_member_data());

  private_data->set_team_created(true);

  rpc::dtmq::client_subscriber::update_option options;
  options.save = true;
  options.custom_data = public_data.get();
  options.private_data = private_data.get();
  auto rsp_checker = atfw::component::memory::stl::make_strong_rc<::atfw::dtmq::channel_lock_checker>();
  auto ret = RPC_AWAIT_CODE_RESULT(subscriber_->send_update(ctx, options, checker, rsp_checker));
  if (ret != 0) {
    team_created_ = false;
    if (ret == PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_LOCK_FAILED) {
      if (rsp_checker && rsp_checker->has_real_value()) {
        handle_lock_conflict(ctx, rsp_checker->real_value());
      } else {
        step_down();
      }
    }
    RPC_RETURN_CODE(ret);
  }
  if (!lock_acquired_) {
    // 合并的锁 CAS 已随 update 生效，登记本节点锁状态(与 acquire_lock 成功路径一致)
    current_lock_ = std::move(self_lock);
    lock_acquired_ = true;
    next_renew_lock_timepoint_ = now;
    next_renew_lock_timepoint_ += get_lock_renew_interval();
  }

  protobuf_copy_message(*storage_.mutable_team_key(), public_data->team_key());
  protobuf_copy_message(*storage_.mutable_captain_user_key(), public_data->captain_user_key());
  protobuf_copy_message(*storage_.mutable_configure(), public_data->configure());
  move_team_any_data_to_map(shared_team_data_, *public_data->mutable_shared_team_data());
  apply_add_member(std::move(*public_data->mutable_member(0)), now);
  RPC_RETURN_CODE(0);
}

rpc::result_code_type team_room::add_invitation(rpc::context& ctx, const atfw::team::SSTeamRoomAddInvitationReq& req) {
  if (destroyed_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED);
  }
  const auto& invitation = req.invitation();
  const auto& invitee = invitation.invitee();
  if (invitee.user_id() == 0 || invitee.zone_id() == 0) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  }
  if (!user_key_equal_t()(req.sender_user_key(), invitation.inviter())) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION);
  }
  // 邀请人必须是队伍成员且有发起邀请的权限(默认所有成员)
  auto inviter = find_member(invitation.inviter(), false);
  if (!inviter) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NOT_IN_TEAM);
  }
  if (inviter->member_data.role() < get_invite_role()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION);
  }
  if (find_member(invitee, false)) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_ALREADY_IN_TEAM);
  }

  auto now = atfw::util::time::time_utility::now();
  std::chrono::system_clock::time_point expired_timepoint{};
  if (invitation.expired_timepoint().seconds() <= 0) {
    expired_timepoint = now + protobuf_to_system_clock(get_teamsvr_room_cfg().invitation_expire());
  } else {
    expired_timepoint = protobuf_to_system_clock(invitation.expired_timepoint());
  }
  if (now >= expired_timepoint) {
    FCTXLOGDEBUG(ctx, "team_room {}:{} received a expired invitation and ignored", team_key_.zone_id(),
                 team_key_.team_id());
    RPC_RETURN_CODE(0);
  }

  rpc::context::message_holder<atfw::team::DTeamAction> action(ctx);
  auto* add_data = action->mutable_add_invitation();
  auto existing = pending_invitation_by_invitee_.find(invitee);
  if (existing != pending_invitation_by_invitee_.end() && existing->second) {
    // GAP-09(2026-08-29 澄清): 重复邀请按可变字段刷新。邀请者/被邀请人/team_key/开始时间不可变
    // (保持原值)，来源/频道/过期时间按新请求更新；admission 数据全量覆盖(以新请求携带的列表为准，
    // 不做按键合并、无删除标记语义)。
    // 过期时间保持"不缩短有效期"判定: 仅显式指定且更晚时顺延；未显式指定时保持原值，
    // 避免 room 补齐值随时钟漂移产生冗余日志
    protobuf_copy_message(*add_data, *existing->second);
    add_data->set_team_source_type(invitation.team_source_type());
    if (invitation.team_source_data().type_url().empty()) {
      add_data->clear_team_source_data();
    } else {
      protobuf_copy_message(*add_data->mutable_team_source_data(), invitation.team_source_data());
    }
    if (invitation.invitee_private_channel().channel_type() != 0 ||
        !invitation.invitee_private_channel().channel_id().empty()) {
      protobuf_copy_message(*add_data->mutable_invitee_private_channel(), invitation.invitee_private_channel());
    }
    protobuf_copy_message(*add_data->mutable_team_admission_data(), invitation.team_admission_data());
    protobuf_copy_message(*add_data->mutable_member_admission_data(), invitation.member_admission_data());
    if (invitation.expired_timepoint().seconds() > 0 &&
        expired_timepoint > protobuf_to_system_clock(add_data->expired_timepoint())) {
      protobuf_copy_message(*add_data->mutable_expired_timepoint(), protobuf_from_system_clock(expired_timepoint));
    }

    // 刷新结果与现有记录一致时不重复写入频道事件。admission 的 repeated 数据允许无序:
    // 比较忽略 team_admission_data/member_admission_data 的元素顺序(按 key 归一化后整体比较)
    if (invitation_semantically_equal(*add_data, *existing->second)) {
      FCTXLOGDEBUG(ctx, "team_room {}:{} received a older invitation and ignored", team_key_.zone_id(),
                   team_key_.team_id());
      RPC_RETURN_CODE(0);
    }
    // 事件应用后会向被邀请人补发一次 DTeamMemberAction
  } else {
    protobuf_copy_message(*add_data, invitation);
    protobuf_copy_message(*add_data->mutable_team_key(), team_key_);
    if (add_data->start_timepoint().seconds() <= 0) {
      *add_data->mutable_start_timepoint() = protobuf_from_system_clock(now);
    }
    protobuf_copy_message(*add_data->mutable_expired_timepoint(), protobuf_from_system_clock(expired_timepoint));
  }
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(send_action(ctx, *action)));
}

rpc::result_code_type team_room::approve_invitation(rpc::context& ctx,
                                                    const atfw::team::SSTeamRoomApproveInvitationReq& req) {
  if (destroyed_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED);
  }
  // 被邀请人本人(包括游客)才能接受邀请
  if (req.sender_user_key().user_id() != req.invitee().user_id() ||
      req.sender_user_key().zone_id() != req.invitee().zone_id()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION);
  }
  auto iter = pending_invitation_by_invitee_.find(req.invitee());
  if (iter == pending_invitation_by_invitee_.end() || !iter->second) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND);
  }
  // 持有数据副本(引用计数)，避免协程挂起期间记录被其他任务移除后访问失效
  auto invitation_ptr = iter->second;
  // 有效期判定: 已过期的邀请视为不存在(由定期维护流程移除)
  auto now = atfw::util::time::time_utility::now();
  const auto& invitation = *invitation_ptr;
  if (invitation.expired_timepoint().seconds() > 0 && protobuf_to_system_clock(invitation.expired_timepoint()) <= now) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_INVITATION_NOT_FOUND);
  }

  // 入队事件与接受邀请事件合并为一次写入，日志按列表顺序追加(add_member 先于 approve_invitation)
  rpc::context::message_holder<atfw::team::DTeamAction> add_action(ctx);
  rpc::context::message_holder<atfw::team::DTeamAction> action(ctx);
  const atfw::team::DTeamAction* batch_actions[2] = {nullptr, nullptr};
  size_t batch_action_count = 0;
  if (!find_member(req.invitee(), false)) {
    auto* add_member = add_action->mutable_add_member();
    protobuf_copy_message(*add_member->mutable_user_key(), req.invitee());
    protobuf_copy_message(*add_member->mutable_user_channel(), invitation.invitee_private_channel());
    add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
    // 入队方式和额外数据从邀请里获取
    add_member->set_team_source_type(invitation.team_source_type());
    if (invitation.has_team_source_data()) {
      protobuf_copy_message(*add_member->mutable_team_source_data(), invitation.team_source_data());
    }
    // 邀请阶段邀请者没有有效的成员数据，被邀请人的版本/路由/共享数据由其本人在同意时上报
    add_member->set_client_version(req.client_version());
    add_member->set_user_router_server_id(req.user_router_server_id());
    *add_member->mutable_joined_timepoint() = protobuf_from_system_clock(now);
    *add_member->mutable_last_heartbeat_timepoint() = protobuf_from_system_clock(now);
    protobuf_copy_message(*add_member->mutable_shared_member_data(), req.shared_member_data());
    batch_actions[batch_action_count++] = &(*add_action);
  }

  protobuf_copy_message(*action->mutable_approve_invitation(), invitation);
  protobuf_copy_message(*action->mutable_approve_invitation()->mutable_team_key(), team_key_);
  batch_actions[batch_action_count++] = &(*action);

  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
      send_actions(ctx, gsl::span<const atfw::team::DTeamAction* const>{batch_actions, batch_action_count})));
}

rpc::result_code_type team_room::reject_invitation(rpc::context& ctx,
                                                   const atfw::team::SSTeamRoomRejectInvitationReq& req) {
  if (destroyed_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED);
  }
  // 被邀请人本人(包括游客)可以否决发给自己的邀请;
  // 以其他成员身份否决(撤回邀请等管理操作)需要 reject_invitation_role(默认 ADMIN)
  if (req.sender_user_key().user_id() != req.invitee().user_id() ||
      req.sender_user_key().zone_id() != req.invitee().zone_id()) {
    auto operator_member = find_member(req.sender_user_key(), false);
    if (!operator_member || operator_member->member_data.role() < get_reject_invitation_role()) {
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION);
    }
  }
  auto iter = pending_invitation_by_invitee_.find(req.invitee());
  if (iter == pending_invitation_by_invitee_.end() || !iter->second) {
    // 幂等: 邀请不存在(已处理/已过期被清理/到达时已过期未记录)视为已拒绝，不写事件
    RPC_RETURN_CODE(0);
  }
  // 拒绝可以忽略有效期过期，视为成功即可
  const auto& invitation = *iter->second;
  if (invitation.expired_timepoint().seconds() > 0 &&
      protobuf_to_system_clock(invitation.expired_timepoint()) <= atfw::util::time::time_utility::now()) {
    RPC_RETURN_CODE(0);
  }

  rpc::context::message_holder<atfw::team::DTeamAction> action(ctx);
  protobuf_copy_message(*action->mutable_reject_invitation(), invitation);
  protobuf_copy_message(*action->mutable_reject_invitation()->mutable_team_key(), team_key_);
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(send_action(ctx, *action)));
}

rpc::result_code_type team_room::add_join_request(rpc::context& ctx,
                                                  const atfw::team::SSTeamRoomAddJoinRequestReq& req) {
  if (destroyed_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED);
  }
  const auto& join_request = req.join_request();
  const auto& requester = join_request.requester();
  if (requester.user_id() == 0 || requester.zone_id() == 0) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM);
  }
  if (!user_key_equal_t()(req.sender_user_key(), requester)) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION);
  }
  if (find_member(requester, false)) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_ALREADY_IN_TEAM);
  }
  // 队伍不存在(从未创建)时直接拒绝，避免给不存在的队伍写入加入请求
  if (!team_created_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_ROOM_NOT_FOUND);
  }
  // 默认所有非成员都可以发起加入请求，可配置为禁止外部人员申请(私人小队，仅邀请加入)
  if (!is_join_request_allowed()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION);
  }

  auto now = atfw::util::time::time_utility::now();
  std::chrono::system_clock::time_point expired_timepoint{};
  if (join_request.expired_timepoint().seconds() <= 0) {
    expired_timepoint = now + protobuf_to_system_clock(get_teamsvr_room_cfg().join_request_expire());
  } else {
    expired_timepoint = protobuf_to_system_clock(join_request.expired_timepoint());
  }
  if (now >= expired_timepoint) {
    FCTXLOGDEBUG(ctx, "team_room {}:{} received a expired join request and ignored", team_key_.zone_id(),
                 team_key_.team_id());
    RPC_RETURN_CODE(0);
  }

  rpc::context::message_holder<atfw::team::DTeamAction> action(ctx);
  auto* add_data = action->mutable_add_join_request();
  auto existing = pending_join_request_by_requester_.find(requester);
  if (existing != pending_join_request_by_requester_.end() && existing->second) {
    // GAP-09(2026-08-29 澄清): 重复加入请求按可变字段刷新。发起请求者/team_key 不可变(保持原值)，
    // 来源/版本/路由/频道/过期时间按新请求更新；admission 数据全量覆盖(以新请求携带的列表为准，
    // 不做按键合并、无删除标记语义)。
    // 过期时间保持"不缩短有效期"判定: 仅显式指定且更晚时顺延；未显式指定时保持原值，
    // 避免 room 补齐值随时钟漂移产生冗余日志
    protobuf_copy_message(*add_data, *existing->second);
    add_data->set_team_source_type(join_request.team_source_type());
    if (join_request.team_source_data().type_url().empty()) {
      add_data->clear_team_source_data();
    } else {
      protobuf_copy_message(*add_data->mutable_team_source_data(), join_request.team_source_data());
    }
    add_data->set_client_version(join_request.client_version());
    add_data->set_user_router_server_id(join_request.user_router_server_id());
    if (join_request.requester_private_channel().channel_type() != 0 ||
        !join_request.requester_private_channel().channel_id().empty()) {
      protobuf_copy_message(*add_data->mutable_requester_private_channel(), join_request.requester_private_channel());
    }
    protobuf_copy_message(*add_data->mutable_member_admission_data(), join_request.member_admission_data());
    if (join_request.expired_timepoint().seconds() > 0 &&
        expired_timepoint > protobuf_to_system_clock(add_data->expired_timepoint())) {
      protobuf_copy_message(*add_data->mutable_expired_timepoint(), protobuf_from_system_clock(expired_timepoint));
    }

    // 刷新结果与现有记录一致时不重复写入频道事件、不重复发送受理回执。admission 的 repeated
    // 数据允许无序: 比较忽略 member_admission_data 的元素顺序(按 key 归一化后整体比较)
    if (join_request_semantically_equal(*add_data, *existing->second)) {
      FCTXLOGDEBUG(ctx, "team_room {}:{} received a older join request and ignored", team_key_.zone_id(),
                   team_key_.team_id());
      RPC_RETURN_CODE(0);
    }
  } else {
    protobuf_copy_message(*add_data, join_request);
    protobuf_copy_message(*add_data->mutable_team_key(), team_key_);
    *add_data->mutable_expired_timepoint() = protobuf_from_system_clock(expired_timepoint);
  }
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(send_action(ctx, *action)));
}

rpc::result_code_type team_room::approve_join_request(rpc::context& ctx,
                                                      const atfw::team::SSTeamRoomApproveJoinRequestReq& req) {
  if (destroyed_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED);
  }
  // 默认所有成员都有同意他人的加入请求的权限(可由 DTeamConfigure 配置)
  auto operator_member = find_member(req.sender_user_key(), false);
  if (!operator_member || operator_member->member_data.role() < get_approve_join_request_role()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION);
  }
  auto iter = pending_join_request_by_requester_.find(req.applicant());
  if (iter == pending_join_request_by_requester_.end() || !iter->second) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_JOIN_REQUEST_NOT_FOUND);
  }
  // 持有数据副本(引用计数)，避免协程挂起期间记录被其他任务移除后访问失效
  auto join_request_ptr = iter->second;
  // 有效期判定: 已过期的加入请求视为不存在(由定期维护流程移除)
  auto now = atfw::util::time::time_utility::now();
  const auto& join_request = *join_request_ptr;
  if (join_request.expired_timepoint().seconds() > 0 &&
      protobuf_to_system_clock(join_request.expired_timepoint()) <= now) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_JOIN_REQUEST_NOT_FOUND);
  }

  // 入队事件与批准申请事件合并为一次写入，日志按列表顺序追加(add_member 先于 approve_join_request)
  rpc::context::message_holder<atfw::team::DTeamAction> add_action(ctx);
  rpc::context::message_holder<atfw::team::DTeamAction> action(ctx);
  const atfw::team::DTeamAction* batch_actions[2] = {nullptr, nullptr};
  size_t batch_action_count = 0;
  if (!find_member(req.applicant(), false)) {
    auto* add_member = add_action->mutable_add_member();
    protobuf_copy_message(*add_member->mutable_user_key(), join_request.requester());
    protobuf_copy_message(*add_member->mutable_user_channel(), join_request.requester_private_channel());
    add_member->set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
    // 入队方式和额外数据从加入请求里获取
    add_member->set_team_source_type(join_request.team_source_type());
    if (join_request.has_team_source_data()) {
      protobuf_copy_message(*add_member->mutable_team_source_data(), join_request.team_source_data());
    }
    add_member->set_client_version(join_request.client_version());
    add_member->set_user_router_server_id(join_request.user_router_server_id());
    *add_member->mutable_joined_timepoint() = protobuf_from_system_clock(now);
    *add_member->mutable_last_heartbeat_timepoint() = protobuf_from_system_clock(now);
    // 加入请求携带的 member_admission_data 包含申请人的 shared_member_data 数据
    protobuf_copy_message(*add_member->mutable_shared_member_data(), join_request.member_admission_data());
    batch_actions[batch_action_count++] = &(*add_action);
  }

  protobuf_copy_message(*action->mutable_approve_join_request(), join_request);
  protobuf_copy_message(*action->mutable_approve_join_request()->mutable_team_key(), team_key_);
  batch_actions[batch_action_count++] = &(*action);

  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(
      send_actions(ctx, gsl::span<const atfw::team::DTeamAction* const>{batch_actions, batch_action_count})));
}

rpc::result_code_type team_room::reject_join_request(rpc::context& ctx,
                                                     const atfw::team::SSTeamRoomRejectJoinRequestReq& req) {
  if (destroyed_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED);
  }
  // 默认所有成员都有否决他人的加入请求的权限(可由 DTeamConfigure 配置)
  auto operator_member = find_member(req.sender_user_key(), false);
  if (!operator_member || operator_member->member_data.role() < get_approve_join_request_role()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION);
  }
  auto iter = pending_join_request_by_requester_.find(req.applicant());
  if (iter == pending_join_request_by_requester_.end() || !iter->second) {
    // 幂等: 加入请求不存在(已处理/已过期被清理/到达时已过期未记录)视为已拒绝，不写事件
    RPC_RETURN_CODE(0);
  }
  // 拒绝可以忽略有效期过期，视为成功即可
  const auto& join_request = *iter->second;
  if (join_request.expired_timepoint().seconds() > 0 &&
      protobuf_to_system_clock(join_request.expired_timepoint()) <= atfw::util::time::time_utility::now()) {
    RPC_RETURN_CODE(0);
  }

  rpc::context::message_holder<atfw::team::DTeamAction> action(ctx);
  protobuf_copy_message(*action->mutable_reject_join_request(), join_request);
  protobuf_copy_message(*action->mutable_reject_join_request()->mutable_team_key(), team_key_);
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(send_action(ctx, *action)));
}

bool team_room::restore_snapshot(rpc::context& ctx) {
  // 恢复开始时清除就绪标记: 恢复失败(损坏/矛盾快照或回放失败)后房间保持不可写，
  // 不允许基于残留的半恢复状态执行权威写入，直到一份完整快照成功恢复
  snapshot_restored_ = false;

  // 先完整验证 custom_data/private_data，再修改本地状态。损坏快照不能进入可写主控状态。
  const auto& custom_data = subscriber_->get_custom_data_content();
  rpc::context::message_holder<atfw::team::DTeamStorage> public_data(ctx);
  if (!custom_data.type_url().empty() && !custom_data.UnpackTo(&(*public_data))) {
    FCTXLOGERROR(ctx, "team room {}:{} unpack custom_data failed", team_key_.zone_id(), team_key_.team_id());
    return false;
  }

  // zone_id=0 是合法的全局团队身份，不是“缺少分区字段”。非空快照身份必须与频道的完整
  // DTeamKey 完全一致，不能把全局或其他分区的同 ID 团队静默改名后加载到当前房间。
  if ((public_data->team_key().team_id() != 0 || public_data->team_key().zone_id() != 0) &&
      !rpc::team::team_api::team_key_equal_t()(public_data->team_key(), team_key_)) {
    FCTXLOGERROR(ctx, "team room {}:{} reject snapshot for team {}:{}", team_key_.zone_id(), team_key_.team_id(),
                 public_data->team_key().zone_id(), public_data->team_key().team_id());
    return false;
  }

  const auto& private_data = subscriber_->get_private_data_content();
  rpc::context::message_holder<atfw::team::DTeamRoomPrivateData> private_storage(ctx);
  if (!private_data.type_url().empty() && !private_data.UnpackTo(&(*private_storage))) {
    FCTXLOGERROR(ctx, "team room {}:{} unpack private_data failed", team_key_.zone_id(), team_key_.team_id());
    return false;
  }

  // GAP-10/RCV-07: 快照边界一致性校验，矛盾快照拒绝恢复(房间保持不可写，由后续快照覆盖修复):
  //   - last_compact_sequence(裁剪边界)不能超出 saved_action_sequence(快照覆盖的最新日志)
  //   - saved_action_sequence 不能超出频道当前已见的最新日志序号
  //   - 序号不允许出现负值(数据损坏)
  int64_t channel_last_sequence = subscriber_->get_last_message_sequence();
  if (public_data->saved_action_sequence() < 0 || private_storage->last_compact_sequence() < 0 ||
      private_storage->last_compact_sequence() > public_data->saved_action_sequence() ||
      public_data->saved_action_sequence() > channel_last_sequence) {
    FCTXLOGERROR(ctx,
                 "team room {}:{} snapshot boundary contradiction: saved_action_sequence={}, "
                 "last_compact_sequence={}, channel_last_sequence={}",
                 team_key_.zone_id(), team_key_.team_id(), public_data->saved_action_sequence(),
                 private_storage->last_compact_sequence(), channel_last_sequence);
    return false;
  }

  restore_timepoint_ = atfw::util::time::time_utility::now();
  // 快照覆盖与增量回放期间不发送个人频道副作用；回放结束后再按 DTMQ 当前锁恢复主控状态。
  lock_acquired_ = false;

  // 从 custom_data 恢复所有成员可见的队伍状态。
  protobuf_copy_message(*storage_.mutable_team_key(), team_key_);
  protobuf_copy_message(*storage_.mutable_captain_user_key(), public_data->captain_user_key());
  protobuf_copy_message(*storage_.mutable_configure(), public_data->configure());
  // 旧快照可能携带未修订的配置(GUEST 表示默认)，恢复后重新修订
  revise_configure_default_permission(*storage_.mutable_configure());
  storage_.set_acknowledge_action_sequence(public_data->acknowledge_action_sequence());
  storage_.set_acknowledge_action_hash_code(public_data->acknowledge_action_hash_code());
  storage_.set_saved_action_sequence(public_data->saved_action_sequence());
  team_created_ = private_storage->team_created() || public_data->saved_action_sequence() > 0 ||
                  !public_data->member().empty() || public_data->has_captain_user_key();

  if (subscriber_->is_destroyed()) {
    destroyed_ = true;
  }
  last_compact_sequence_ = private_storage->last_compact_sequence();
  last_compact_timepoint_ = protobuf_to_system_clock(private_storage->last_compact_timepoint());

  private_team_data_.clear();
  for (const auto& data : private_storage->private_team_data()) {
    protobuf_copy_message(private_team_data_[data.key()], data.value());
  }

  std::unordered_set<PROJECT_NAMESPACE_ID::DUserIDKey, user_key_hash_t, user_key_equal_t> expired_member_key;
  foreach_member([&expired_member_key](atfw::util::nostd::nonnull<const member_ptr_t>& member) {
    expired_member_key.insert(member->member_data.user_key());
    return true;
  });

  std::unordered_set<PROJECT_NAMESPACE_ID::DUserIDKey, user_key_hash_t, user_key_equal_t> snapshot_member_keys;
  std::vector<const atfw::team::DTeamMember * ATFW_UTIL_MACRO_NONNULL> members_by_visit_time;
  members_by_visit_time.reserve(static_cast<size_t>(public_data->member().size()));
  for (const auto& data : public_data->member()) {
    if (data.user_key().zone_id() == 0 || data.user_key().user_id() == 0 ||
        !snapshot_member_keys.insert(data.user_key()).second) {
      continue;
    }
    members_by_visit_time.push_back(&data);
  }
  // 按访问时间排序，确保回复快照时，LRU map前面的member先过期
  std::sort(members_by_visit_time.begin(), members_by_visit_time.end(),
            [](const atfw::team::DTeamMember* ATFW_UTIL_MACRO_NONNULL l,
               const atfw::team::DTeamMember* ATFW_UTIL_MACRO_NONNULL r) {
              auto lv = (std::max)(protobuf_to_system_clock(l->joined_timepoint()),
                                   protobuf_to_system_clock(l->last_heartbeat_timepoint()));
              auto rv = (std::max)(protobuf_to_system_clock(r->joined_timepoint()),
                                   protobuf_to_system_clock(r->last_heartbeat_timepoint()));
              return lv < rv || (lv == rv && user_key_less_t()(l->user_key(), r->user_key()));
            });
  bool member_restore_succeeded = true;
  auto captain_role = atfw::team::EN_TEAM_MEMBER_ROLE_OWNER;
  user_key_equal_t user_key_eq;

  for (const auto* data_ptr : members_by_visit_time) {
    const auto& data = *data_ptr;
    auto member = find_member(data.user_key(), false);
    if (!member) {
      member = mutable_member(data.user_key());
    }
    if (!member) {
      FCTXLOGERROR(ctx, "team room {}:{} restore_snapshot member {} but allocate failed", team_key_.zone_id(),
                   team_key_.team_id(), data.user_key().user_id());
      member_restore_succeeded = false;
      continue;
    }
    expired_member_key.erase(member->member_data.user_key());
    if (user_key_eq(data.user_key(), public_data->captain_user_key())) {
      captain_role = member->member_data.role();
    }

    member->user_router_server_id = data.user_router_server_id();
    member->last_heartbeat_timepoint = protobuf_to_system_clock(data.last_heartbeat_timepoint());
    protobuf_copy_message(member->member_data, data);
    // 成员共享数据移入内存的 key-value 索引，proto 字段在内存中保持为空
    move_team_any_data_to_map(member->shared_member_data, *member->member_data.mutable_shared_member_data());
    member->member_data.set_user_router_server_id(member->user_router_server_id);
    *member->member_data.mutable_last_heartbeat_timepoint() =
        protobuf_from_system_clock(member->last_heartbeat_timepoint);
  }
  if (!member_restore_succeeded) {
    return false;
  }

  for (const auto& removed_key : expired_member_key) {
    remove_member(ctx, removed_key, atfw::team::EN_TEAM_EXIT_REASON_DEFAULT, false);
  }
  // 加载快照时重置重试删除队列
  member_retry_remove_.clear();

  // invitation
  expired_member_key.clear();
  for (const auto& invitation : pending_invitation_by_invitee_) {
    expired_member_key.insert(invitation.first);
  }
  for (const auto& invitation : public_data->pending_invitation()) {
    if (invitation.invitee().user_id() == 0 || invitation.invitee().zone_id() == 0) {
      continue;
    }
    // GAP-08: 快照加载时直接剔除已过期的邀请(不重建本地待处理项)
    if (invitation.expired_timepoint().seconds() > 0 &&
        protobuf_to_system_clock(invitation.expired_timepoint()) <= restore_timepoint_) {
      continue;
    }

    expired_member_key.erase(invitation.invitee());
    auto& data_ptr = pending_invitation_by_invitee_[invitation.invitee()];
    if (!data_ptr) {
      data_ptr = atfw::component::memory::stl::make_strong_rc<atfw::team::DTeamInvitation>();
    }
    protobuf_copy_message(*data_ptr, invitation);
    protobuf_copy_message(*data_ptr->mutable_team_key(), team_key_);
  }
  for (const auto& removed_key : expired_member_key) {
    pending_invitation_by_invitee_.erase(removed_key);
  }

  // join_request
  expired_member_key.clear();
  for (const auto& invitation : pending_join_request_by_requester_) {
    expired_member_key.insert(invitation.first);
  }
  for (const auto& join_request : public_data->pending_join_request()) {
    if (join_request.requester().user_id() == 0 || join_request.requester().zone_id() == 0) {
      continue;
    }
    // GAP-08: 快照加载时直接剔除已过期的加入请求(不重建本地待处理项)
    if (join_request.expired_timepoint().seconds() > 0 &&
        protobuf_to_system_clock(join_request.expired_timepoint()) <= restore_timepoint_) {
      continue;
    }

    expired_member_key.erase(join_request.requester());
    auto& data_ptr = pending_join_request_by_requester_[join_request.requester()];
    if (!data_ptr) {
      data_ptr = atfw::component::memory::stl::make_strong_rc<atfw::team::DTeamJoinRequest>();
    }
    protobuf_copy_message(*data_ptr, join_request);
    protobuf_copy_message(*data_ptr->mutable_team_key(), team_key_);
  }
  for (const auto& removed_key : expired_member_key) {
    pending_join_request_by_requester_.erase(removed_key);
  }

  // shared team data
  shared_team_data_.clear();
  for (const auto& kv : public_data->shared_team_data()) {
    protobuf_copy_message(shared_team_data_[kv.key()], kv.value());
  }
  storage_.clear_shared_team_data();

  change_captain(public_data->captain_user_key(), captain_role);

  // public snapshot 已覆盖到 saved_action_sequence；运行时脏快照可能不推进物理压缩点，
  // 因此恢复只能回放快照覆盖边界之后的增量日志，避免旧日志覆盖快照中的 heartbeat 等运行时数据。
  const int64_t snapshot_replay_sequence =
      (std::max)(last_compact_sequence_, storage_.saved_action_sequence());
  rpc::dtmq::client_subscriber::query_options options;
  options.start_sequence = snapshot_replay_sequence + 1;
  bool replay_succeeded = true;
  subscriber_->query_cached_message(
      ctx,
      [this, &ctx, &replay_succeeded, snapshot_replay_sequence](const ::atfw::dtmq::DChannelMessage& message) {
        if (message.sequence() <= snapshot_replay_sequence) {
          return true;
        }
        replay_succeeded = apply_event_message(ctx, message);
        return replay_succeeded;
      },
      options);
  if (!replay_succeeded) {
    return false;
  }

  // 接管当前乐观锁状态
  current_lock_ = subscriber_->get_lock();
  lock_acquired_ = !current_lock_.lock_holder().empty() && current_lock_.lock_holder() == lock_holder_;

  // 恢复最早的未压缩日志时间点(用于按时间维度压缩的调度与触发)
  refresh_oldest_log_timepoint(ctx);

  if (lock_acquired_) {
    next_renew_lock_timepoint_ = restore_timepoint_ + get_lock_renew_interval();
    if (storage_.captain_user_key().user_id() == 0 && !member_.empty()) {
      team_room_manager::me()->mark_room_pending_flush(*this);
    }
  }

  // 加载snapshot之后重置一下用于延迟删除的空房间计时器，避免在恢复快照后立即触发删除
  refresh_empty_tracking(restore_timepoint_);
  snapshot_restored_ = true;
  schedule_next_timer();
  return true;
}

bool team_room::apply_event_message(rpc::context& ctx, const ::atfw::dtmq::DChannelMessage& message) {
  if (message.sequence() <= last_compact_sequence_) {
    return true;
  }

  const auto& detail = message.detail();
  if (detail.has_event() && !detail.event().type_url().empty()) {
    const auto& event = detail.event();
    if (event.Is<atfw::team::DTeamAction>()) {
      rpc::context::message_holder<atfw::team::DTeamAction> action(ctx);
      if (event.UnpackTo(&(*action))) {
        apply_action(ctx, *action, message.sequence(), message.hash_code(),
                     protobuf_to_system_clock(message.create_timepoint()));
      } else {
        FCTXLOGERROR(ctx, "team room {}:{} unpack DTeamAction failed, got type_url: {}", team_key_.zone_id(),
                     team_key_.team_id(), event.type_url());
        return false;
      }
    }
  } else if (detail.has_destroy()) {
    destroyed_ = true;
  }

  // 事件按序到达且 sequence 只保证递增不保证连续: 压缩点之后的首条事件即最早的未压缩日志
  if (oldest_log_timepoint_ == std::chrono::system_clock::from_time_t(0) &&
      message.sequence() > last_compact_sequence_) {
    oldest_log_timepoint_ = protobuf_to_system_clock(message.create_timepoint());
  }
  update_acknowledge(message.sequence(), message.hash_code());
  return true;
}

void team_room::update_acknowledge(int64_t sequence, uint64_t hash_code) {
  if (sequence > storage_.acknowledge_action_sequence()) {
    storage_.set_acknowledge_action_sequence(sequence);
    storage_.set_acknowledge_action_hash_code(hash_code);
  }
}

void team_room::apply_action(rpc::context& ctx, atfw::team::DTeamAction& action, int64_t sequence, uint64_t hash_code,
                             std::chrono::system_clock::time_point event_timepoint) {
  switch (action.action_case()) {
    case atfw::team::DTeamAction::kDestroyTeam:
      destroyed_ = true;
      break;
    case atfw::team::DTeamAction::kAddMember:
      apply_add_member(std::move(*action.mutable_add_member()), event_timepoint);
      break;
    case atfw::team::DTeamAction::kRemoveMember:
      remove_member(ctx, action.remove_member().user_key(), action.remove_member().remove_member_reason(), true);
      break;
    case atfw::team::DTeamAction::kMemberUpdate:
      apply_member_update(action.member_update());
      break;
    case atfw::team::DTeamAction::kTeamUpdate:
      apply_team_update(action.team_update());
      break;
    case atfw::team::DTeamAction::kMemberSetRole:
      apply_member_set_role(action.member_set_role());
      break;
    case atfw::team::DTeamAction::kElectionCaptain:
      change_captain(action.election_captain().user_key(), action.election_captain().role());
      break;
    case atfw::team::DTeamAction::kAddInvitation:
      apply_add_invitation(action.add_invitation());
      break;
    case atfw::team::DTeamAction::kApproveInvitation:
      apply_approve_invitation(action.approve_invitation());
      break;
    case atfw::team::DTeamAction::kRejectInvitation:
      apply_reject_invitation(action.reject_invitation());
      break;
    case atfw::team::DTeamAction::kAddJoinRequest:
      apply_add_join_request(action.add_join_request());
      break;
    case atfw::team::DTeamAction::kApproveJoinRequest:
      apply_approve_join_request(action.approve_join_request());
      break;
    case atfw::team::DTeamAction::kRejectJoinRequest:
      apply_reject_join_request(action.reject_join_request());
      break;
    default:
      break;
  }
  update_acknowledge(sequence, hash_code);
}

void team_room::apply_member_update(const atfw::team::DTeamMemberUpdateData& update_data) {
  // 只有成员自己发起的更新(携带 user_router_server_id)才刷新 LRU 访问位置，
  // 管理员更新他人数据不应影响离线淘汰顺序
  auto member = find_member(update_data.user_key(), false);
  if (!member) {
    return;
  }

  if (!update_data.client_version().empty()) {
    member->member_data.set_client_version(update_data.client_version());
  }

  // 正常用户切换节点后会重新心跳更新自己的user_router_server_id。
  // update协议可能被管理员或队长发起，修改其他人的数据。
  // 此时他人的user_router_server_id是不可信的，所以置0。
  // 如果是自己更新自己的数据，总是可以更新 user_router_server_id。
  if (update_data.user_router_server_id() != 0) {
    member->member_data.set_user_router_server_id(update_data.user_router_server_id());
  }

  // 更新私有频道
  if (update_data.has_user_channel()) {
    protobuf_copy_message(*member->member_data.mutable_user_channel(), update_data.user_channel());
  }

  // GAP-09(2026-08-29 澄清): 共享成员数据按 key 覆盖，未出现的 key 保留；
  // 携带 key 但 value 的 type_url 或 payload 为空的项表示删除该 key(删除标记仅用于
  // member_update/team_update 的共享数据，admission 等其他入口为全量覆盖)
  for (const auto& kv : update_data.shared_member_data()) {
    if (kv.value().data().type_url().empty() || kv.value().data().value().empty()) {
      member->shared_member_data.erase(kv.key());
    } else {
      protobuf_copy_message(member->shared_member_data[kv.key()], kv.value());
    }
  }
}

void team_room::apply_team_update(const atfw::team::DTeamUpdateData& update_data) {
  if (update_data.has_configure()) {
    protobuf_copy_message(*storage_.mutable_configure(), update_data.configure());
    // 修订默认门槛(新事件已在 send_action 写入前修订，此处兜底旧世代日志回放)，
    // 保证 storage_ 中的配置与后续快照下发总是完整门槛
    revise_configure_default_permission(*storage_.mutable_configure());
  }

  // GAP-09(2026-08-29 澄清): 共享队伍数据按 key 覆盖，未出现的 key 保留；
  // 携带 key 但 value 的 type_url 或 payload 为空的项表示删除该 key(删除标记仅用于
  // member_update/team_update 的共享数据，admission 等其他入口为全量覆盖)
  for (const auto& kv : update_data.shared_team_data()) {
    if (kv.value().data().type_url().empty() || kv.value().data().value().empty()) {
      shared_team_data_.erase(kv.key());
    } else {
      protobuf_copy_message(shared_team_data_[kv.key()], kv.value());
    }
  }
}

void team_room::apply_member_set_role(const atfw::team::DTeamMemberSetRole& set_role) {
  auto member = find_member(set_role.user_key(), false);
  if (!member) {
    return;
  }
  member->member_data.set_role(set_role.role());
}

void team_room::apply_add_invitation(const atfw::team::DTeamInvitation& invitation) {
  const auto& invitee = invitation.invitee();
  if (invitee.user_id() == 0 || invitee.zone_id() == 0) {
    return;
  }
  // 被邀请人已在队伍中则忽略该邀请
  if (find_member(invitee, false)) {
    return;
  }

  auto& data_ptr = pending_invitation_by_invitee_[invitee];
  if (!data_ptr) {
    data_ptr = atfw::component::memory::stl::make_strong_rc<atfw::team::DTeamInvitation>();
  }
  protobuf_copy_message(*data_ptr, invitation);

  // 推送被邀请人: 仅携带 PUBLIC 权限的队伍数据和所有成员的 PUBLIC 权限数据
  atfw::team::DTeamMemberAction notify_action;
  auto* invited = notify_action.mutable_invited();
  protobuf_copy_message(*invited, invitation);
  invited->clear_team_admission_data();
  invited->clear_member_admission_data();
  copy_public_permission_data(shared_team_data_, invited->mutable_team_admission_data());
  foreach_member([invited](atfw::util::nostd::nonnull<const member_ptr_t>& member) {
    auto* member_data = invited->add_member_admission_data();
    protobuf_copy_message(*member_data->mutable_user_key(), member->member_data.user_key());
    copy_public_permission_data(member->shared_member_data, member_data->mutable_member_admission_data());
    return true;
  });

  atfw::dtmq::DChannelIdKey channel_id = invitation.invitee_private_channel();
  append_team_member_channel_notification(invitee, std::move(channel_id), std::move(notify_action));
}

void team_room::apply_approve_invitation(const atfw::team::DTeamInvitation& invitation) {
  const auto& invitee = invitation.invitee();
  if (invitee.user_id() == 0 || invitee.zone_id() == 0) {
    return;
  }
  // 幂等: 邀请不存在(已处理或已过期)则忽略。
  // 成员由 approve_invitation 流程先写入的 add_member 事件添加，这里移除邀请并通知。
  auto iter = pending_invitation_by_invitee_.find(invitee);
  if (iter == pending_invitation_by_invitee_.end() || !iter->second) {
    return;
  }
  // 通知目标的频道从本地记录获取(此时被邀请人的成员数据可能尚未建立)
  auto record = iter->second;
  pending_invitation_by_invitee_.erase(iter);

  // 通知被邀请人已入队(除新增邀请外的事件不携带 admission 数据)
  atfw::team::DTeamMemberAction notify_action;
  auto* joined_info = notify_action.mutable_joined_team();
  dump_team_key(*joined_info->mutable_team_key());
  protobuf_copy_message(*joined_info->mutable_user_key(), invitee);
  protobuf_copy_message(*joined_info->mutable_team_channel(), channel_key_);
  protobuf_copy_message(*joined_info->mutable_captain_user_key(), storage_.captain_user_key());
  {
    auto member_ptr = find_member(joined_info->user_key(), false);
    if (member_ptr) {
      joined_info->set_user_role(member_ptr->member_data.role());
    } else {
      joined_info->set_user_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
    }
  }

  atfw::dtmq::DChannelIdKey channel_id = record->invitee_private_channel();
  append_team_member_channel_notification(invitee, std::move(channel_id), std::move(notify_action));
}

void team_room::apply_reject_invitation(const atfw::team::DTeamInvitation& invitation) {
  const auto& invitee = invitation.invitee();
  if (invitee.user_id() == 0 || invitee.zone_id() == 0) {
    return;
  }
  // 幂等: 邀请不存在(已处理或已过期)则忽略
  auto iter = pending_invitation_by_invitee_.find(invitee);
  if (iter == pending_invitation_by_invitee_.end() || !iter->second) {
    return;
  }
  // 通知目标的频道从本地记录获取(此时被邀请人不是成员，没有成员数据)
  auto record = iter->second;
  pending_invitation_by_invitee_.erase(iter);

  // 通知被邀请人邀请已否决(作为其操作回执; 邀请人是队伍成员，可通过队伍频道日志感知)
  atfw::team::DTeamMemberAction notify_action;
  auto* rejected = notify_action.mutable_reject_invitation();
  protobuf_copy_message(*rejected, invitation);
  rejected->clear_team_admission_data();
  rejected->clear_member_admission_data();
  atfw::dtmq::DChannelIdKey channel_id = record->invitee_private_channel();
  append_team_member_channel_notification(invitee, std::move(channel_id), std::move(notify_action));
}

void team_room::apply_add_join_request(const atfw::team::DTeamJoinRequest& join_request) {
  const auto& requester = join_request.requester();
  if (requester.user_id() == 0 || requester.zone_id() == 0) {
    return;
  }
  // 申请人已在队伍中则忽略该请求
  if (find_member(requester, false)) {
    return;
  }

  auto& data_ptr = pending_join_request_by_requester_[requester];
  if (!data_ptr) {
    data_ptr = atfw::component::memory::stl::make_strong_rc<atfw::team::DTeamJoinRequest>();
  }
  protobuf_copy_message(*data_ptr, join_request);
  // 不需要额外通知队长审批，队长会通过队伍的频道获取到数据

  // 通知申请人申请已受理(作为其操作回执; 携带房间规范化后的过期时间，
  // 对端以此维护本地待处理列表并在被拒绝/入队/过期时清理)
  atfw::team::DTeamMemberAction notify_action;
  protobuf_copy_message(*notify_action.mutable_apply_join_request(), join_request);
  atfw::dtmq::DChannelIdKey channel_id = join_request.requester_private_channel();
  append_team_member_channel_notification(requester, std::move(channel_id), std::move(notify_action));
}

void team_room::apply_approve_join_request(const atfw::team::DTeamJoinRequest& join_request) {
  const auto& requester = join_request.requester();
  if (requester.user_id() == 0 || requester.zone_id() == 0) {
    return;
  }
  // 幂等: 加入请求不存在(已处理或已过期)则忽略。
  // 成员由 approve_join_request 流程先写入的 add_member 事件添加，这里移除申请并通知。
  auto iter = pending_join_request_by_requester_.find(requester);
  if (iter == pending_join_request_by_requester_.end() || !iter->second) {
    return;
  }
  // 通知目标的频道从本地记录获取(此时申请人的成员数据可能尚未建立)
  auto record = iter->second;
  pending_join_request_by_requester_.erase(iter);

  // 通知申请人已入队(不携带 admission 数据)
  atfw::team::DTeamMemberAction notify_action;
  auto* joined_info = notify_action.mutable_joined_team();
  dump_team_key(*joined_info->mutable_team_key());
  protobuf_copy_message(*joined_info->mutable_user_key(), requester);
  protobuf_copy_message(*joined_info->mutable_team_channel(), channel_key_);
  protobuf_copy_message(*joined_info->mutable_captain_user_key(), storage_.captain_user_key());
  {
    auto member_ptr = find_member(joined_info->user_key(), false);
    if (member_ptr) {
      joined_info->set_user_role(member_ptr->member_data.role());
    } else {
      joined_info->set_user_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
    }
  }
  atfw::dtmq::DChannelIdKey channel_id = record->requester_private_channel();
  append_team_member_channel_notification(requester, std::move(channel_id), std::move(notify_action));
}

void team_room::apply_reject_join_request(const atfw::team::DTeamJoinRequest& join_request) {
  const auto& requester = join_request.requester();
  if (requester.user_id() == 0 || requester.zone_id() == 0) {
    return;
  }
  // 幂等: 加入请求不存在(已处理或已过期)则忽略
  auto iter = pending_join_request_by_requester_.find(requester);
  if (iter == pending_join_request_by_requester_.end() || !iter->second) {
    return;
  }
  // 通知目标的频道从本地记录获取(此时申请人不是成员，没有成员数据)
  auto record = iter->second;
  pending_join_request_by_requester_.erase(iter);

  // 通知申请人已被拒绝(不携带 admission 数据)
  atfw::team::DTeamMemberAction notify_action;
  auto* rejected = notify_action.mutable_reject_join_request();
  protobuf_copy_message(*rejected, join_request);
  rejected->clear_member_admission_data();
  atfw::dtmq::DChannelIdKey channel_id = record->requester_private_channel();
  append_team_member_channel_notification(requester, std::move(channel_id), std::move(notify_action));
}

void team_room::apply_add_member(atfw::team::DTeamMember&& member_data,
                                 std::chrono::system_clock::time_point event_timepoint) {
  if (member_data.user_key().zone_id() == 0 || member_data.user_key().user_id() == 0) {
    return;
  }

  // 入队/心跳时间缺失时以频道事件创建时间兜底，保证各节点状态一致
  if (member_data.joined_timepoint().seconds() <= 0) {
    *member_data.mutable_joined_timepoint() = protobuf_from_system_clock(event_timepoint);
  }
  if (member_data.last_heartbeat_timepoint().seconds() <= 0) {
    *member_data.mutable_last_heartbeat_timepoint() = protobuf_from_system_clock(event_timepoint);
  }

  auto member = find_member(member_data.user_key(), true);
  bool is_new_member = false;
  if (!member) {
    is_new_member = true;
    member = mutable_member(member_data.user_key());
  }
  if (!member) {
    return;
  }
  team_created_ = true;

  auto joined_timepoint = member->member_data.joined_timepoint();
  auto previous_heartbeat_timepoint = member->last_heartbeat_timepoint;
  uint64_t previous_user_router_server_id = member->user_router_server_id;
  auto member_key = member->member_data.user_key();
  member->member_data = std::move(member_data);
  // 成员共享数据移入内存的 key-value 索引，proto 字段在内存中保持为空
  move_team_any_data_to_map(member->shared_member_data, *member->member_data.mutable_shared_member_data());
  // Key不允许修改
  protobuf_copy_message(*member->member_data.mutable_user_key(), member_key);
  // 重复 add_member 只保留更早的入队时间，不能因重试改变成员加入顺序。
  auto old_joined_timepoint = protobuf_to_system_clock(joined_timepoint);
  auto new_joined_timepoint = protobuf_to_system_clock(member->member_data.joined_timepoint());
  if (old_joined_timepoint > std::chrono::system_clock::from_time_t(0) && old_joined_timepoint < new_joined_timepoint) {
    protobuf_copy_message(*member->member_data.mutable_joined_timepoint(), joined_timepoint);
  }

  auto incoming_heartbeat_timepoint = protobuf_to_system_clock(member->member_data.last_heartbeat_timepoint());
  if (is_new_member) {
    member->last_heartbeat_timepoint = incoming_heartbeat_timepoint;
    member->user_router_server_id = member->member_data.user_router_server_id();
  } else if (incoming_heartbeat_timepoint > previous_heartbeat_timepoint) {
    member->last_heartbeat_timepoint = incoming_heartbeat_timepoint;
    member->user_router_server_id = member->member_data.user_router_server_id();
  } else {
    // 延迟到达的重复 add_member 不能回退运行时心跳与路由状态。
    member->last_heartbeat_timepoint = previous_heartbeat_timepoint;
    member->user_router_server_id = previous_user_router_server_id;
    *member->member_data.mutable_last_heartbeat_timepoint() =
        protobuf_from_system_clock(member->last_heartbeat_timepoint);
    member->member_data.set_user_router_server_id(member->user_router_server_id);
  }

  // 首位成员成为队长
  if (!storage_.has_captain_user_key() || 0 == storage_.captain_user_key().user_id()) {
    protobuf_copy_message(*storage_.mutable_captain_user_key(), member->member_data.user_key());
    // 队长一定是owner
    member->member_data.set_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
    schedule_next_timer();
  }

  if (is_new_member) {
    refresh_empty_tracking(atfw::util::time::time_utility::now());
    schedule_next_timer();
  }
}

rpc::result_code_type team_room::elect_captain_after_remove(rpc::context& ctx) {
  member_ptr_t next_captain;
  foreach_member([&next_captain](atfw::util::nostd::nonnull<const member_ptr_t>& member) {
    if (!next_captain) {
      next_captain = member;
      return true;
    }

    auto member_joined = protobuf_to_system_clock(member->member_data.joined_timepoint());
    auto current_joined = protobuf_to_system_clock(next_captain->member_data.joined_timepoint());
    if (member_joined < current_joined ||
        (member_joined == current_joined &&
         user_key_less_t()(member->member_data.user_key(), next_captain->member_data.user_key()))) {
      next_captain = member;
    }
    return true;
  });
  if (!next_captain) {
    RPC_RETURN_CODE(0);
  }

  rpc::context::message_holder<atfw::team::DTeamAction> action(ctx);
  auto* election_captain = action->mutable_election_captain();
  protobuf_copy_message(*election_captain->mutable_user_key(), next_captain->member_data.user_key());
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(send_action(ctx, *action, true)));
}

std::chrono::system_clock::time_point team_room::get_member_offline_deadline(const member_runtime_data& member_data) {
  std::chrono::system_clock::time_point baseline = restore_timepoint_;
  auto member_baseline = protobuf_to_system_clock(member_data.member_data.joined_timepoint());
  if (member_baseline > baseline) {
    baseline = member_baseline;
  }

  if (member_data.last_heartbeat_timepoint > baseline) {
    baseline = member_data.last_heartbeat_timepoint;
  }

  return baseline + protobuf_to_system_clock(get_teamsvr_room_cfg().member_offline_expire());
}

void team_room::refresh_empty_tracking(std::chrono::system_clock::time_point now) {
  if (member_.empty()) {
    if (empty_since_timepoint_ == std::chrono::system_clock::from_time_t(0)) {
      empty_since_timepoint_ = now;
    }
  } else {
    empty_since_timepoint_ = std::chrono::system_clock::from_time_t(0);
  }
}

team_room::member_ptr_t team_room::mutable_member(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key) {
  // mutable接口总是由有效的成员调用，所以总是可以刷新LRU的最近访问时间
  member_ptr_t ret = find_member(user_key, true);
  if (ret) {
    return ret;
  }

  // 递归期间增加的member
  if (iterating_member_protect_ != nullptr) {
    auto pending_iter = iterating_member_protect_->pending_to_add.find(user_key);
    if (pending_iter != iterating_member_protect_->pending_to_add.end()) {
      return pending_iter->second;
    }
  }

  ret = atfw::component::memory::stl::make_strong_rc<member_runtime_data>();
  if (!ret) {
    return ret;
  }
  ret->last_heartbeat_timepoint = atfw::util::time::time_utility::now();
  *ret->member_data.mutable_last_heartbeat_timepoint() = protobuf_from_system_clock(ret->last_heartbeat_timepoint);
  protobuf_copy_message(*ret->member_data.mutable_user_key(), user_key);

  // 防止递归期间增删member，延迟处理
  if (iterating_member_protect_ != nullptr) {
    iterating_member_protect_->pending_to_remove.erase(user_key);
    iterating_member_protect_->pending_to_add.insert({user_key, ret});
    return ret;
  }

  member_.insert_key_value(user_key, ret);

  // 收到新的新增消息，移除删除重试队列
  member_retry_remove_.erase(user_key);
  return ret;
}

team_room::member_ptr_t team_room::find_member(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key, bool update_visit) {
  auto iter = member_.find(user_key, update_visit);
  if (iter == member_.end()) {
    return nullptr;
  }
  return iter->second;
}

bool team_room::remove_member(rpc::context& /*ctx*/, const PROJECT_NAMESPACE_ID::DUserIDKey& user_key,
                              atfw::team::EnTeamExitReason reason, bool with_notify) {
  auto iter = member_.find(user_key, false);

  // 已经收到删除消息，移除重试队列
  member_retry_remove_.erase(user_key);

  if (iter == member_.end()) {
    return false;
  }

  // 防止递归期间增删member，延迟处理
  if (iterating_member_protect_ != nullptr) {
    iterating_member_protect_->pending_to_remove.insert({user_key, iter->second});
    iterating_member_protect_->pending_to_add.erase(user_key);
    return true;
  }
  auto member_ptr = iter->second;

  member_.erase(iter);
  // 队长退出后由 flush_pending_channel_message 发送 election_captain 频道事件。
  const auto& captain = storage_.captain_user_key();
  if (captain.user_id() == user_key.user_id() && captain.zone_id() == user_key.zone_id()) {
    storage_.clear_captain_user_key();
    if (is_lock_holder()) {
      team_room_manager::me()->mark_room_pending_flush(*this);
    }
  }
  refresh_empty_tracking(atfw::util::time::time_utility::now());
  schedule_next_timer();

  // 主动通知私有频道
  if (with_notify && member_ptr && member_ptr->member_data.user_channel().channel_type() != 0 &&
      !member_ptr->member_data.user_channel().channel_id().empty()) {
    atfw::dtmq::DChannelIdKey channel_id = member_ptr->member_data.user_channel();
    atfw::team::DTeamMemberAction action;
    protobuf_copy_message(*action.mutable_remove_member()->mutable_team_key(), team_key_);
    protobuf_copy_message(*action.mutable_remove_member()->mutable_user_key(), member_ptr->member_data.user_key());
    action.mutable_remove_member()->set_remove_member_reason(reason);

    append_team_member_channel_notification(member_ptr->member_data.user_key(), std::move(channel_id),
                                            std::move(action));
  }
  return true;
}

void team_room::foreach_member(
    atfw::util::nostd::function_ref<bool(atfw::util::nostd::nonnull<const member_ptr_t>&)> fn) {
  iterating_member_protect_t current_protect_instance;
  if (iterating_member_protect_ == nullptr) {
    iterating_member_protect_ = &current_protect_instance;
  }
  for (auto iter = member_.cbegin(); iter != member_.cend(); ++iter) {
    if (!iter->second) {
      continue;
    }

    if (!fn(iter->second)) {
      break;
    }
  }

  if (iterating_member_protect_ == &current_protect_instance) {
    iterating_member_protect_ = nullptr;

    for (const auto& pair : current_protect_instance.pending_to_add) {
      member_.insert_key_value(pair.first, pair.second);
      // 收到新的新增消息，移除删除重试队列
      member_retry_remove_.erase(pair.first);
    }

    for (const auto& pair : current_protect_instance.pending_to_remove) {
      member_.erase(pair.first);
      if (storage_.captain_user_key().user_id() == pair.first.user_id() &&
          storage_.captain_user_key().zone_id() == pair.first.zone_id()) {
        storage_.clear_captain_user_key();
      }
      // 收到新的新增消息，移除删除重试队列
      member_retry_remove_.erase(pair.first);
    }

    if (!current_protect_instance.pending_to_remove.empty()) {
      if (storage_.captain_user_key().user_id() == 0 && !member_.empty() && is_lock_holder()) {
        team_room_manager::me()->mark_room_pending_flush(*this);
      }
      refresh_empty_tracking(atfw::util::time::time_utility::now());
      schedule_next_timer();
    }
  }
}

void team_room::append_team_member_channel_notification(const PROJECT_NAMESPACE_ID::DUserIDKey& user_key,
                                                        atfw::dtmq::DChannelIdKey&& channel_id,
                                                        atfw::team::DTeamMemberAction&& action) {
  // 频道事件会被主备 room 节点同时只读订阅，只有当前主控节点负责向个人频道发送副作用通知。
  if (!is_lock_holder()) {
    return;
  }
  if (channel_id.channel_type() == 0 || channel_id.channel_id().empty()) {
    return;
  }

  if (user_key.zone_id() == 0 || user_key.user_id() == 0) {
    return;
  }

  // 队列由空变为非空时注册到 manager，由一组事件处理完后统一发送
  if (pending_member_channel_actions_.empty()) {
    team_room_manager::me()->mark_room_pending_flush(*this);
  }

  auto& pending_channel = pending_member_channel_actions_[user_key];
  pending_channel.emplace_back(std::move(channel_id), std::move(action));
}

std::chrono::system_clock::duration team_room::get_lock_lease() const {
  std::chrono::system_clock::duration ret = std::chrono::system_clock::duration::zero();
  if (subscriber_) {
    const auto& configure = subscriber_->get_configure();
    // 租约时长不低于频道配置的订阅者心跳过期淘汰时间
    ret = protobuf_to_system_clock(configure.subscriber_timeout());
    if (ret <= std::chrono::system_clock::duration::zero()) {
      // 配置未就绪时回退到与 normalize_dtmq_channel_configure 一致的推导: 2*心跳+重试
      ret = protobuf_to_system_clock(configure.heartbeat_interval()) +
            protobuf_to_system_clock(configure.heartbeat_interval()) +
            protobuf_to_system_clock(configure.heartbeat_retry_interval());
    }
  }
  if (ret <= std::chrono::system_clock::duration::zero()) {
    ret = std::chrono::seconds{60};
  }
  return ret;
}

std::chrono::system_clock::duration team_room::get_lock_renew_interval() const {
  auto ret = get_lock_lease() / 2;
  if (ret <= std::chrono::seconds{1}) {
    ret = std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::seconds{1});
  }

  return ret;
}

int64_t team_room::get_gc_log_count() const {
  if (subscriber_) {
    const auto& configure = subscriber_->get_configure();
    if (configure.gc_log_count() > 0) {
      return configure.gc_log_count();
    }
  }
  // 与 normalize_dtmq_channel_configure 默认值一致
  return 30;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
std::chrono::system_clock::duration team_room::get_compact_log_keep_time() const {
  const auto& cfg = get_teamsvr_room_cfg();
  auto start_time = protobuf_to_system_clock(cfg.compact_log_start_time());
  auto keep_time = protobuf_to_system_clock(cfg.compact_log_keep_time());
  if (keep_time <= std::chrono::system_clock::duration::zero()) {
    // 缺省取开始压缩时长的一半
    keep_time = start_time / 2;
  }
  // 保留窗口不能大于触发窗口，否则按时间维度永远无法压缩
  return (std::min)(keep_time, start_time);
}

atfw::team::EnTeamPermissionRole team_room::get_manage_member_role() const {
  return resolve_permission_role(storage_.configure().manage_member_role(), atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
}

atfw::team::EnTeamPermissionRole team_room::get_approve_join_request_role() const {
  return resolve_permission_role(storage_.configure().approve_join_request_role(),
                                 atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
}

atfw::team::EnTeamPermissionRole team_room::get_invite_role() const {
  return resolve_permission_role(storage_.configure().invite_role(), atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
}

atfw::team::EnTeamPermissionRole team_room::get_update_team_data_role() const {
  return resolve_permission_role(storage_.configure().update_team_data_role(), atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
}

atfw::team::EnTeamPermissionRole team_room::get_reject_invitation_role() const {
  return resolve_permission_role(storage_.configure().reject_invitation_role(), atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
}

atfw::team::EnTeamPermissionRole team_room::get_set_member_role_role() const {
  return resolve_permission_role(storage_.configure().set_member_role_role(), atfw::team::EN_TEAM_MEMBER_ROLE_ADMIN);
}

bool team_room::is_join_request_allowed() const { return !storage_.configure().disable_join_request(); }

bool team_room::check_update_conditions(
    rpc::context& ctx, const google::protobuf::RepeatedPtrField<atfw::team::DTeamConditionChecker>& conditions) {
  // 未携带条件列表表示无附加检查(保持无条件的既有行为)
  if (conditions.empty()) {
    return true;
  }
  // checker 之间是或关系: 任一 checker 通过即整体通过
  for (const auto& checker : conditions) {
    if (check_condition_checker(ctx, checker)) {
      return true;
    }
  }
  return false;
}

bool team_room::check_condition_checker(rpc::context& ctx, const atfw::team::DTeamConditionChecker& checker) {
  // 共享队伍数据等值检查(与关系)
  if (!team_shared_data_match(shared_team_data_, checker.shared_team_data(), ctx.get_protobuf_arena().get())) {
    return false;
  }

  // 队伍成员数量范围
  if (checker.has_members_count() &&
      !team_condition_range_match(static_cast<int64_t>(member_.size()), checker.members_count())) {
    return false;
  }

  // 成员条件组(与关系): 所有条件组都必须满足
  for (const auto& group : checker.member_condition_group()) {
    if (!check_member_condition_group(ctx, group)) {
      return false;
    }
  }
  return true;
}

bool team_room::check_member_condition_group(rpc::context& ctx,
                                             const atfw::team::DTeamConditionChecker::DMemberConditionGroup& group) {
  const auto& condition = group.member_condition();
  switch (group.scope_type_case()) {
    case atfw::team::DTeamConditionChecker::DMemberConditionGroup::kUserKey: {
      // 指定成员: 成员必须存在且满足条件
      auto member = find_member(group.user_key(), false);
      return member && team_member_condition_match(condition, *member, ctx.get_protobuf_arena().get());
    }
    case atfw::team::DTeamConditionChecker::DMemberConditionGroup::kAllMembers: {
      // 所有成员都满足条件(空队伍视为 vacuously true)
      bool all_match = true;
      foreach_member([&condition, &ctx, &all_match](atfw::util::nostd::nonnull<const member_ptr_t>& member) {
        if (!team_member_condition_match(condition, *member, ctx.get_protobuf_arena().get())) {
          all_match = false;
          return false;
        }
        return true;
      });
      return all_match;
    }
    case atfw::team::DTeamConditionChecker::DMemberConditionGroup::kAnyMembers: {
      // 任一成员满足条件即可
      bool any_match = false;
      foreach_member([&condition, &ctx, &any_match](atfw::util::nostd::nonnull<const member_ptr_t>& member) {
        if (team_member_condition_match(condition, *member, ctx.get_protobuf_arena().get())) {
          any_match = true;
          return false;
        }
        return true;
      });
      return any_match;
    }
    default:
      break;
  }

  // 数量/百分比维度: 遍历前把限制统一换算成绝对数量门槛(只计算一次)，遍历中不再做 scope 分支与
  // 百分比换算，只剩整数比较。百分比换算: satisfied * kTeamPercentBase >= total * percent
  // 等价于 satisfied >= ceil(total * percent / kTeamPercentBase)，max 侧取 floor
  const int64_t total_count = static_cast<int64_t>(member_.size());
  int64_t pass_at = 0;  // 满足数达到该值即通过 min 维度
  // NOLINTNEXTLINE(readability-redundant-parentheses)
  int64_t fail_above = (std::numeric_limits<int64_t>::max)();  // 满足数超过该值即违反 max 维度(默认不限制)
  switch (group.scope_type_case()) {
    case atfw::team::DTeamConditionChecker::DMemberConditionGroup::kMembersCount: {
      pass_at = group.members_count().min_value();
      if (group.members_count().max_value() != 0) {
        fail_above = group.members_count().max_value();
      }
      break;
    }
    case atfw::team::DTeamConditionChecker::DMemberConditionGroup::kMembersPercent: {
      // min_percent 超过 100 恒不可满足、max_percent 达到 100 即等效不限制；
      // 先钳制百分比避免 total_count * percent 溢出 int64(恶意超大值)
      const int64_t min_percent = (std::min)(group.members_percent().min_value(), kTeamPercentBase + 1);
      const int64_t max_percent = (std::min)(group.members_percent().max_value(), kTeamPercentBase);
      if (min_percent != 0) {
        pass_at = ((total_count * min_percent) + kTeamPercentBase - 1) / kTeamPercentBase;
      }
      if (max_percent != 0) {
        fail_above = (total_count * max_percent) / kTeamPercentBase;
      }
      break;
    }
    default:
      // 未设置 scope 的条件组视为无效，按不满足处理(失败关闭，空条件组不应被当成永真)
      return false;
  }

  // max 上限不低于成员总数时永不可能被违反: min 达到即可提前通过；无条件限制时遍历前直接通过
  const bool max_never_violated = fail_above >= total_count;
  if (pass_at <= 0 && max_never_violated) {
    return true;
  }
  int64_t satisfied_count = 0;
  int64_t visited_count = 0;
  bool decided = false;
  bool passed = false;
  foreach_member([&condition, &ctx, &satisfied_count, &visited_count, pass_at, fail_above, total_count,
                  max_never_violated, &decided, &passed](atfw::util::nostd::nonnull<const member_ptr_t>& member) {
    ++visited_count;
    if (team_member_condition_match(condition, *member, ctx.get_protobuf_arena().get())) {
      ++satisfied_count;
      // 超过 max 上限，提前拒绝
      if (satisfied_count > fail_above) {
        passed = false;
        decided = true;
        return false;
      }
      // 达到 min 门槛且 max 不可能被违反，提前通过
      if (max_never_violated && satisfied_count >= pass_at) {
        passed = true;
        decided = true;
        return false;
      }
    }
    // 剩余成员全部满足也不足以达到 min 门槛，提前拒绝
    if (satisfied_count + (total_count - visited_count) < pass_at) {
      passed = false;
      decided = true;
      return false;
    }
    return true;
  });
  if (decided) {
    return passed;
  }
  return satisfied_count >= pass_at && satisfied_count <= fail_above;
}

rpc::result_code_type team_room::check_action_permission(rpc::context& ctx,
                                                         const PROJECT_NAMESPACE_ID::DUserIDKey& operator_key,
                                                         const atfw::team::DTeamAction& action) {
  if (destroyed_) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED);
  }

  auto is_self = [&operator_key](const PROJECT_NAMESPACE_ID::DUserIDKey& user_key) {
    return operator_key.user_id() == user_key.user_id() && operator_key.zone_id() == user_key.zone_id();
  };

  // 要求操作者是队伍成员且角色不低于 role_limit
  auto require_role = [this, &operator_key](atfw::team::EnTeamPermissionRole role_limit) -> int32_t {
    auto operator_member = find_member(operator_key, false);
    if (!operator_member) {
      return PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NOT_IN_TEAM;
    }
    if (operator_member->member_data.role() < role_limit) {
      return PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION;
    }
    return 0;
  };
  // 要求操作者是队伍成员(操作自己)
  auto require_member = [this, &operator_key]() -> int32_t {
    if (!find_member(operator_key, false)) {
      return PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NOT_IN_TEAM;
    }
    return 0;
  };

  int32_t ret = 0;
  switch (action.action_case()) {
    case atfw::team::DTeamAction::kRemoveMember: {
      // 所有成员都可以删除自己(视为主动退出)，直接删除其他成员需要 manage_member_role(默认 ADMIN)
      ret = is_self(action.remove_member().user_key()) ? require_member() : require_role(get_manage_member_role());
      if (0 == ret && !is_self(action.remove_member().user_key())) {
        // 目标成员的当前角色必须严格低于或等于操作者，队长可以被移除，移除后自动选新队长
        auto target_member = find_member(action.remove_member().user_key(), false);
        if (!target_member) {
          ret = PROJECT_NAMESPACE_ID::EN_ERR_TEAM_MEMBER_NOT_FOUND;
        } else {
          auto operator_member = find_member(operator_key, false);
          if (!operator_member || target_member->member_data.role() > operator_member->member_data.role()) {
            ret = PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION;
          }
        }
      }
      break;
    }
    case atfw::team::DTeamAction::kAddMember:
      // 直接添加成员需要 manage_member_role(默认 ADMIN)，邀请/加入请求流程的入队不经由此处。
      // 已存在成员不能通过重放完整 DTeamMember 改写自身角色；也不能授予高于操作者的角色。
      ret = require_role(get_manage_member_role());
      if (ret == 0) {
        const auto& new_member = action.add_member();
        if (new_member.user_key().zone_id() == 0 || new_member.user_key().user_id() == 0) {
          ret = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
        } else if (find_member(new_member.user_key(), false)) {
          ret = PROJECT_NAMESPACE_ID::EN_ERR_TEAM_ALREADY_IN_TEAM;
        } else {
          auto operator_member = find_member(operator_key, false);
          if (!operator_member || new_member.role() > operator_member->member_data.role()) {
            ret = PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION;
          }
        }
      }
      break;
    case atfw::team::DTeamAction::kMemberUpdate:
      // 所有成员都可以 member_update 自己的数据，更新他人的信息需要 manage_member_role(默认 ADMIN)
      ret = is_self(action.member_update().user_key()) ? require_member() : require_role(get_manage_member_role());
      // 携带更新条件时还需满足至少一个 checker(或关系，内部与关系)，通过后的最终事件数据
      // 会被裁剪掉 condition 字段(见 send_action)
      if (ret == 0 && !check_update_conditions(ctx, action.member_update().condition())) {
        ret = PROJECT_NAMESPACE_ID::EN_ERR_TEAM_CONDITION_NOT_MATCH;
      }
      break;
    case atfw::team::DTeamAction::kTeamUpdate:
      ret = require_role(get_update_team_data_role());
      // 与 member_update 相同: 携带更新条件时需满足至少一个 checker
      if (ret == 0 && !check_update_conditions(ctx, action.team_update().condition())) {
        ret = PROJECT_NAMESPACE_ID::EN_ERR_TEAM_CONDITION_NOT_MATCH;
      }
      break;
    case atfw::team::DTeamAction::kMemberSetRole:
      // 设置成员角色需要 set_member_role_role(默认 ADMIN)；目标必须是成员，
      // 且不能授予高于操作者自身的角色(与 add_member 的授权上限一致)。
      // 目标成员的当前角色必须严格低于操作者，且不能直接修改队长角色(须走 election_captain)
      ret = require_role(get_set_member_role_role());
      if (ret == 0) {
        const auto& set_role = action.member_set_role();
        if (set_role.role() <= atfw::team::EN_TEAM_MEMBER_ROLE_GUEST) {
          ret = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
        } else {
          auto target_member = find_member(set_role.user_key(), false);
          if (!target_member) {
            ret = PROJECT_NAMESPACE_ID::EN_ERR_TEAM_MEMBER_NOT_FOUND;
          } else {
            auto operator_member = find_member(operator_key, false);
            if (!operator_member || set_role.role() > operator_member->member_data.role() ||
                target_member->member_data.role() >= operator_member->member_data.role()) {
              ret = PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION;
            }
          }
        }
      }
      break;
    case atfw::team::DTeamAction::kElectionCaptain: {
      // 当前队长可以主动转让；其他操作者无条件修改队长固定要求 OWNER。
      ret =
          is_self(storage_.captain_user_key()) ? require_member() : require_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
      if (ret == 0) {
        auto member_ptr = find_member(action.election_captain().user_key(), false);
        if (!member_ptr) {
          ret = PROJECT_NAMESPACE_ID::EN_ERR_TEAM_MEMBER_NOT_FOUND;
        } else {
          // GAP-12: election_captain.role 是新队长角色，队长不再恒为 OWNER；
          // 转移/修改队长时新队长的角色不能高于 max(原队长角色, 操作者角色)
          auto captain_member = find_member(storage_.captain_user_key(), false);
          auto operator_member = find_member(operator_key, false);
          atfw::team::EnTeamPermissionRole role_limit = (std::max)(
              captain_member ? captain_member->member_data.role() : atfw::team::EN_TEAM_MEMBER_ROLE_GUEST,
              operator_member ? operator_member->member_data.role() : atfw::team::EN_TEAM_MEMBER_ROLE_GUEST);
          if (action.election_captain().role() > role_limit) {
            ret = PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION;
          }
        }
      }
      break;
    }
    case atfw::team::DTeamAction::kDestroyTeam:
      // 解散队伍固定要求 OWNER
      ret = require_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
      break;
    case atfw::team::DTeamAction::kAddInvitation:
      // 只能以本人身份发起邀请，且需要有发起邀请的权限(默认所有成员)
      if (!is_self(action.add_invitation().inviter())) {
        ret = PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION;
      } else {
        ret = require_role(get_invite_role());
      }
      break;
    case atfw::team::DTeamAction::kApproveInvitation:
      // 所有人(包括游客)都可以同意发给自己的邀请
      if (!is_self(action.approve_invitation().invitee())) {
        ret = PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION;
      }
      break;
    case atfw::team::DTeamAction::kRejectInvitation:
      // 所有人(包括游客)都可以否决发给自己的邀请，
      // 以其他成员身份否决(撤回邀请等管理操作)需要 reject_invitation_role(默认 ADMIN)
      if (!is_self(action.reject_invitation().invitee())) {
        ret = require_role(get_reject_invitation_role());
      }
      break;
    case atfw::team::DTeamAction::kAddJoinRequest:
      // 所有人(包括游客)都可以以本人身份发起加入请求，
      // 可配置为禁止外部人员申请(私人小队，仅邀请加入)
      if (!is_self(action.add_join_request().requester()) || !is_join_request_allowed()) {
        ret = PROJECT_NAMESPACE_ID::EN_ERR_TEAM_NO_PERMISSION;
      }
      break;
    case atfw::team::DTeamAction::kApproveJoinRequest:
    case atfw::team::DTeamAction::kRejectJoinRequest:
      // 默认所有成员都有同意和否决他人的加入请求的权限
      ret = require_role(get_approve_join_request_role());
      break;
    default:
      ret = PROJECT_NAMESPACE_ID::EN_ERR_INVALID_PARAM;
      break;
  }
  RPC_RETURN_CODE(ret);
}

void team_room::refresh_oldest_log_timepoint(rpc::context& ctx) {
  oldest_log_timepoint_ = std::chrono::system_clock::from_time_t(0);
  if (!subscriber_ || !subscriber_->is_ready()) {
    return;
  }

  int64_t oldest_sequence = (std::max)(last_compact_sequence_, subscriber_->get_last_removed_sequence()) + 1;
  if (oldest_sequence > subscriber_->get_last_message_sequence()) {
    return;
  }

  // sequence 只保证递增不保证连续: query_cached_message 内部按 lower_bound 二分查找
  // 第一条不早于 oldest_sequence 的日志(通常就是缓存里最早的日志)，取到一条即中断遍历
  rpc::dtmq::client_subscriber::query_options query_opts;
  query_opts.start_sequence = oldest_sequence;
  query_opts.max_count = 1;
  subscriber_->query_cached_message(
      ctx,
      [this](const ::atfw::dtmq::DChannelMessage& message) {
        oldest_log_timepoint_ = protobuf_to_system_clock(message.create_timepoint());
        return false;
      },
      query_opts);
}

int64_t team_room::pick_compact_sequence(rpc::context& ctx, std::chrono::system_clock::time_point now) {
  if (!subscriber_ || !subscriber_->is_ready()) {
    return 0;
  }
  int64_t last_sequence = subscriber_->get_last_message_sequence();
  if (last_sequence <= last_compact_sequence_) {
    return 0;
  }

  const auto& cfg = get_teamsvr_room_cfg();
  // 数量维度保留条数: keep_percent 与 keep_count 取较大者
  int64_t keep_by_count = (std::max)(get_gc_log_count() * cfg.compact_log_keep_percent() / kTeamPercentBase,
                                     static_cast<int64_t>(cfg.compact_log_keep_count()));
  auto keep_deadline = now - get_compact_log_keep_time();

  // sequence 只保证递增不保证连续: 未压缩日志的真实数量由订阅者缓存统计(内部二分定位起点)
  int64_t log_count = static_cast<int64_t>(subscriber_->get_cached_log_count(last_compact_sequence_ + 1));
  // 数量维度需要裁掉的日志条数(0 表示数量维度不限制)
  int64_t delete_by_count = log_count > keep_by_count ? log_count - keep_by_count : 0;

  // 遍历压缩点之后的缓存日志: 数量维度的裁剪点是第 delete_by_count 条日志，
  // 时间维度的裁剪点是最后一条超出保留窗口前的日志，两个裁剪点都确定后立即中断遍历
  int64_t visit_index = 0;
  int64_t cutoff_by_count = 0;
  int64_t cutoff_by_time = 0;
  rpc::dtmq::client_subscriber::query_options query_opts;
  query_opts.start_sequence = last_compact_sequence_ + 1;
  subscriber_->query_cached_message(
      ctx,
      [&visit_index, &cutoff_by_count, &cutoff_by_time, delete_by_count,
       keep_deadline](const ::atfw::dtmq::DChannelMessage& message) {
        ++visit_index;
        if (visit_index == delete_by_count) {
          cutoff_by_count = message.sequence();
        }
        // 日志按时间点有序，保留窗口内的日志不按时间维度裁剪
        bool in_keep_window = protobuf_to_system_clock(message.create_timepoint()) > keep_deadline;
        if (!in_keep_window) {
          cutoff_by_time = message.sequence();
        }
        // 数量维度裁剪点未到达，或时间维度裁剪点还可能推进时继续遍历
        return visit_index < delete_by_count || !in_keep_window;
      },
      query_opts);

  // 两种保留策略都是硬保证: 日志只有同时不被两个维度保留(既不在最新 keep_by_count 条之内，
  // 也超出时间保留窗口)才可被压缩，取较小裁剪点。某维度裁剪点为 0 表示该维度要保留全部
  // 未压缩日志，整体不可压缩(不能把它当作"该维度不限制"而取另一维度)
  int64_t compact_sequence = (std::min)(cutoff_by_count, cutoff_by_time);
  if (compact_sequence <= last_compact_sequence_) {
    return 0;
  }
  return compact_sequence;
}

::atfw::dtmq::DChannelOptimisticLock team_room::make_self_lock(std::chrono::system_clock::time_point now) const {
  ::atfw::dtmq::DChannelOptimisticLock ret;
  ret.set_lock_holder(lock_holder_);
  *ret.mutable_timeout() = protobuf_from_system_clock(now + get_lock_lease());
  return ret;
}

atfw::util::memory::strong_rc_ptr<::atfw::dtmq::channel_lock_checker> team_room::make_write_lock_checker() const {
  auto checker = atfw::component::memory::stl::make_strong_rc<::atfw::dtmq::channel_lock_checker>();
  // 服务端只比较 lock_holder，写入时要求持有者必须是本节点
  checker->mutable_expect_value()->set_lock_holder(lock_holder_);
  return checker;
}

int32_t team_room::make_acquire_lock_checker(
    std::chrono::system_clock::time_point now, ::atfw::dtmq::DChannelOptimisticLock& self_lock,
    atfw::util::memory::strong_rc_ptr<::atfw::dtmq::channel_lock_checker>& checker) {
  // DTMQ 乐观锁是"持有者回显"语义: expect.lock_holder 与当前持有者一致即接受 reset。
  // 因此空锁/已过期/本就是本节点持有的锁才允许 CAS 接管；视图中的未过期他人锁直接拒绝，
  // 既不发起 CAS(视图新鲜时 holder 回显必然通过，会抢锁)，也不在冲突后用回带的真实锁重试(FIX-07)。
  // 他人锁到期后的接管由定时器驱动(见 get_next_timer_event)。
  const auto& view_lock = subscriber_->get_lock();
  if (!view_lock.lock_holder().empty() && view_lock.lock_holder() != lock_holder_) {
    bool view_lock_expired = false;
    if (view_lock.has_timeout() && view_lock.timeout().seconds() != 0 &&
        now > protobuf_to_system_clock(view_lock.timeout())) {
      view_lock_expired = true;
    }
    if (!view_lock_expired) {
      current_lock_ = view_lock;
      return PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_LOCK_FAILED;
    }
  }

  self_lock = make_self_lock(now);
  checker = atfw::component::memory::stl::make_strong_rc<::atfw::dtmq::channel_lock_checker>();
  checker->set_allow_empty_real_value(true);
  // 新节点订阅后携带看到的老乐观锁做 CAS 切换。服务端只在锁为空/已过期/本就是本节点持有时
  // 接受重置(见 mq_channel::compare_and_maybe_reset_lock)；冲突时不得用回带的真实锁再次尝试，
  // 否则会在他人租约未过期时直接抢锁(租约到期后的接管由定时器驱动，见 get_next_timer_event)
  *checker->mutable_expect_value() = view_lock;
  *checker->mutable_reset_value() = self_lock;
  return 0;
}

rpc::result_code_type team_room::acquire_lock(rpc::context& ctx) {
  if (!subscriber_ || !subscriber_->is_ready()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
  }
  if (subscriber_->is_destroyed()) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED);
  }

  auto now = atfw::util::time::time_utility::now();
  if (!lock_acquired_) {
    // 只有按 team_key(zone_id, team_id) 一致性哈希选中的节点才允许抢占写锁(获得写权限)。
    // task 层已按同一规则路由，这里是防御性校验: 路由表过期或多节点并发收到同队请求时，
    // 非属主节点不得发起 CAS 接管
    uint64_t route_server_id = rpc::team::team_api::get_teamsvr_room_server_id_of_zone(team_key_);
    if (0 == route_server_id || route_server_id != logic_config::me()->get_local_server_id()) {
      FCTXLOGWARNING(
          ctx, "team room {}:{} acquire optimistic lock ignored: this node({:#x}) is not the route owner({:#x})",
          team_key_.zone_id(), team_key_.team_id(), logic_config::me()->get_local_server_id(), route_server_id);
      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_SERVICE_NOT_AVAILABLE);
    }

    ::atfw::dtmq::DChannelOptimisticLock self_lock;
    atfw::util::memory::strong_rc_ptr<::atfw::dtmq::channel_lock_checker> checker;
    int32_t checker_ret = make_acquire_lock_checker(now, self_lock, checker);
    if (0 != checker_ret) {
      RPC_RETURN_CODE(checker_ret);
    }
    auto rsp_checker = atfw::component::memory::stl::make_strong_rc<::atfw::dtmq::channel_lock_checker>();

    auto ret = RPC_AWAIT_CODE_RESULT(subscriber_->send_reset_lock(ctx, checker, rsp_checker));
    if (0 == ret) {
      current_lock_ = std::move(self_lock);
      lock_acquired_ = true;
      next_renew_lock_timepoint_ = now;
      next_renew_lock_timepoint_ += get_lock_renew_interval();
      FCTXLOGINFO(ctx, "team room {}:{} acquire optimistic lock success", team_key_.zone_id(), team_key_.team_id());
      RPC_RETURN_CODE(0);
    }
    if (rsp_checker && rsp_checker->has_real_value()) {
      current_lock_ = rsp_checker->real_value();
      if (current_lock_.lock_holder() == lock_holder_) {
        // 已是本节点持有(可能上次设置成功但响应丢失)
        lock_acquired_ = true;
        next_renew_lock_timepoint_ = now;
        next_renew_lock_timepoint_ += get_lock_renew_interval();
        RPC_RETURN_CODE(0);
      }
    }
    if (ret != PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_LOCK_FAILED) {
      // 非锁冲突错误直接返回
      FCTXLOGERROR(ctx, "team room {}:{} acquire optimistic lock failed: {}", team_key_.zone_id(), team_key_.team_id(),
                   ret);
      RPC_RETURN_CODE(ret);
    }
    FCTXLOGERROR(ctx, "team room {}:{} acquire optimistic lock conflict, current holder: {}", team_key_.zone_id(),
                 team_key_.team_id(), current_lock_.lock_holder());
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_LOCK_FAILED);
  }
  RPC_RETURN_CODE(0);
}

void team_room::handle_lock_conflict(rpc::context& ctx, const ::atfw::dtmq::DChannelOptimisticLock& real_lock) {
  current_lock_ = real_lock;
  if (real_lock.lock_holder() == lock_holder_) {
    // 仍是本节点持有(可能上次设置成功但响应丢失)
    lock_acquired_ = true;
    return;
  }
  FCTXLOGWARNING(ctx, "team room {}:{} optimistic lock taken by {}, step down", team_key_.zone_id(),
                 team_key_.team_id(), real_lock.lock_holder());
  step_down();
}

void team_room::step_down() {
  lock_acquired_ = false;
  // 退位后转为备用节点: 在锁过期后尝试接管
  schedule_next_timer();
}

rpc::result_code_type team_room::send_event_with_lock(rpc::context& ctx, ::google::protobuf::Any&& event_data,
                                                      bool no_wait) {
  auto* events = ctx.create<google::protobuf::RepeatedPtrField<::google::protobuf::Any>>();
  if (nullptr == events) {
    RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::EN_ERR_SYSTEM);
  }
  protobuf_move_message(*events->Add(), std::move(event_data));
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(send_events_with_lock(ctx, std::move(*events), no_wait)));
}

rpc::result_code_type team_room::send_events_with_lock(
    rpc::context& ctx, google::protobuf::RepeatedPtrField<::google::protobuf::Any>&& events, bool no_wait) {
  if (!lock_acquired_) {
    auto lock_ret = RPC_AWAIT_CODE_RESULT(acquire_lock(ctx));
    if (lock_ret != 0) {
      RPC_RETURN_CODE(lock_ret);
    }
  }

  auto checker = make_write_lock_checker();
  auto rsp_checker = atfw::component::memory::stl::make_strong_rc<::atfw::dtmq::channel_lock_checker>();
  auto ret =
      RPC_AWAIT_CODE_RESULT(subscriber_->send_event(ctx, std::move(events), checker, rsp_checker, true, no_wait));
  if (PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_LOCK_FAILED == ret) {
    // 老节点锁失效: 记录真实持有者并退位
    if (rsp_checker && rsp_checker->has_real_value()) {
      handle_lock_conflict(ctx, rsp_checker->real_value());
    } else {
      step_down();
    }
  } else if (0 != ret) {
    FCTXLOGERROR(ctx, "team room {}:{} send event failed: {}", team_key_.zone_id(), team_key_.team_id(), ret);
  }
  RPC_RETURN_CODE(ret);
}

team_room_timer_event team_room::get_next_timer_event(std::chrono::system_clock::time_point now) {
  team_room_timer_event ret;
  if (!subscriber_->is_ready() || !snapshot_restored_ || subscriber_->is_destroyed()) {
    return ret;
  }

  // 非主控节点: 在当前乐观锁过期后尝试接管(容灾切换，老节点再写入时锁自然失败)
  if (!lock_acquired_) {
    ret.type = team_room_timer_event_type::kAcquireLock;
    if (current_lock_.lock_holder().empty()) {
      ret.timeout = now;
    } else if (current_lock_.has_timeout() && current_lock_.timeout().seconds() > 0) {
      ret.timeout = protobuf_to_system_clock(current_lock_.timeout());
    } else {
      // 锁未设置超时则按租约周期定期检查
      ret.timeout = now + get_lock_lease();
    }
    return ret;
  }

  // 主控节点: 已解散队伍尽快销毁频道
  if (destroyed_) {
    if (!channel_destroy_sent_) {
      ret.type = team_room_timer_event_type::kDestroyChannel;
      ret.timeout = now;
    }
    return ret;
  }

  // 定期维护: 乐观锁续租+过期数据清理+日志压缩(不要求时间非常精确，常规维护时总会尝试压缩日志)
  const auto& cfg = get_teamsvr_room_cfg();
  ret.type = team_room_timer_event_type::kMaintenance;
  ret.timeout = next_renew_lock_timepoint_;
  // 压缩加速触发: 仅作为因日志数量/时间因素提前触发维护的加速点。保留策略是双维度硬保证
  // (见 pick_compact_sequence)，仅当两个维度当前都至少放行一条日志且自上次维护以来有显著
  // 新增日志(>= 保留下限)时加速才有意义: 均衡态下维护自身追加的续租/快照日志抵消裁剪量，
  // 日志数不增长，若无增量门控，加速触发点会永远停留在过去，最早事件恒为"已过期的维护"，
  // kick/destroy 等更晚到期的定时事件会被饿死
  {
    int64_t gc_log_count = get_gc_log_count();
    int64_t keep_by_count = (std::max)(gc_log_count * cfg.compact_log_keep_percent() / kTeamPercentBase,
                                       static_cast<int64_t>(cfg.compact_log_keep_count()));
    int64_t uncompacted_count = static_cast<int64_t>(subscriber_->get_cached_log_count(last_compact_sequence_ + 1));
    // 数量维度放行: 未压缩日志数超出保留条数; 时间维度放行: 最早未压缩日志超出保留窗口;
    // 增量放行: 距上次维护有 >= keep_by_count 条新增日志(突发写入及时压缩，均衡空转不加速)
    bool count_allows = uncompacted_count > keep_by_count;
    bool time_allows = oldest_log_timepoint_ > std::chrono::system_clock::from_time_t(0) &&
                       oldest_log_timepoint_ <= now - get_compact_log_keep_time();
    bool has_growth =
        uncompacted_count - last_maintenance_uncompacted_count_ >= (std::max)(keep_by_count, static_cast<int64_t>(1));
    // 按时间维度: 最早的未压缩日志过了 compact_log_start_time 后提前触发维护
    //             (keep_time <= start_time，触发时最早的日志必然超出保留窗口)
    if (count_allows && time_allows && has_growth) {
      ret.timeout =
          (std::min)(ret.timeout, oldest_log_timepoint_ + protobuf_to_system_clock(cfg.compact_log_start_time()));
      // 按数量维度: 未压缩日志数量(sequence 只保证递增不保证连续，必须用真实缓存数量)超过
      //             gc_log_count * compact_log_over_percent / 100 且超过压缩保留条数时立即触发维护
      if (uncompacted_count > gc_log_count * cfg.compact_log_over_percent() / kTeamPercentBase) {
        ret.timeout = (std::min)(ret.timeout, now);
      }
    }
  }

  // 剔除最久未心跳的成员(LRU front)
  if (!member_.empty()) {
    const auto& oldest = member_.front();
    if (oldest.second) {
      std::chrono::system_clock::time_point deadline = get_member_offline_deadline(*oldest.second);
      if (deadline < ret.timeout) {
        ret.type = team_room_timer_event_type::kKickOfflineMember;
        ret.timeout = deadline;
      }
    }
  }
  // 移除成员的重试队列(LRU front)
  if (!member_retry_remove_.empty()) {
    const auto& oldest = member_retry_remove_.front();
    if (oldest.second) {
      std::chrono::system_clock::time_point deadline = oldest.second->next_retry_timepoint;
      if (deadline < ret.timeout) {
        ret.type = team_room_timer_event_type::kKickOfflineMember;
        ret.timeout = deadline;
      }
    }
  }

  // 空队伍保留到期后解散
  if (member_.empty() && empty_since_timepoint_ > std::chrono::system_clock::from_time_t(0)) {
    std::chrono::system_clock::time_point deadline = empty_since_timepoint_ + get_room_destroy_delay();
    if (deadline < ret.timeout) {
      ret.type = team_room_timer_event_type::kDestroyEmptyRoom;
      ret.timeout = deadline;
    }
  }

  return ret;
}

void team_room::schedule_next_timer() {
  std::chrono::system_clock::time_point now = atfw::util::time::time_utility::now();

  if (subscriber_->is_destroyed() && now >= empty_since_timepoint_ + get_room_destroy_delay()) {
    // 房间即将被回收，不再调度定时器
    team_room_manager::me()->remove_room(get_team_key(), this);
    return;
  }

  auto event = get_next_timer_event(now);
  if (team_room_timer_event_type::kNone == event.type) {
    // 暂无定时事件(订阅未就绪等)，按续租周期保底重查
    event.timeout = now + get_lock_renew_interval();
  }

  // 应该要判定定时器只能提前，不能延后，避免定时器被延迟到过期事件之后才触发
  team_room_manager::me()->reset_room_timer(*this, event.timeout);
}

void team_room::on_timer(rpc::context& ctx) {
  timer_watcher_.reset();
  if (subscriber_->is_destroyed()) {
    schedule_next_timer();
    return;
  }

  std::chrono::system_clock::time_point now = atfw::util::time::time_utility::now();
  auto event = get_next_timer_event(now);
  if (team_room_timer_event_type::kNone == event.type || event.timeout > now) {
    // 事件未到期，重新调度(定时器总是指向当前最近的定时 action)
    schedule_next_timer();
    return;
  }

  if (!task_type_trait::empty(maintenance_task_) && !task_type_trait::is_exiting(maintenance_task_)) {
    // 上一次定时 action 仍在执行，本次是冗余的定时器触发，直接忽略即可
    return;
  }

  auto self = shared_from_this();
  auto invoke_result = rpc::async_invoke(
      ctx, get_timer_event_name(event.type),
      [self, event_type = event.type](rpc::context& child_ctx) mutable -> rpc::result_code_type {
        RPC_AWAIT_CODE_RESULT(self->execute_timer_event(child_ctx, event_type));

        // 先发送数据，再插入定时器
        RPC_AWAIT_IGNORE_RESULT(self->flush_pending_channel_message(child_ctx));

        if (!task_type_trait::empty(self->maintenance_task_) &&
            task_type_trait::get_task_id(self->maintenance_task_) == child_ctx.get_task_context().task_id) {
          task_type_trait::reset_task(self->maintenance_task_);
        }

        // 定时 action 完成后重设下一个定时器
        self->schedule_next_timer();
        RPC_RETURN_CODE(0);
      });
  if (invoke_result.is_error()) {
    FCTXLOGERROR(ctx, "team room {}:{} kickoff timer event {} failed", team_key_.zone_id(), team_key_.team_id(),
                 static_cast<int32_t>(event.type));
    schedule_next_timer();
    return;
  }
  if (!task_type_trait::empty(*invoke_result.get_success()) &&
      !task_type_trait::is_exiting(*invoke_result.get_success())) {
    maintenance_task_ = std::move(*invoke_result.get_success());
  }
}

rpc::result_code_type team_room::execute_timer_event(rpc::context& ctx, team_room_timer_event_type event_type) {
  std::chrono::system_clock::time_point now = atfw::util::time::time_utility::now();
  switch (event_type) {
    case team_room_timer_event_type::kAcquireLock: {
      auto ret = RPC_AWAIT_CODE_RESULT(acquire_lock(ctx));
      if (0 != ret && PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_LOCK_FAILED != ret) {
        FCTXLOGERROR(ctx, "team room {}:{} timer acquire lock failed: {}", team_key_.zone_id(), team_key_.team_id(),
                     ret);
      }
      break;
    }
    case team_room_timer_event_type::kMaintenance:
      RPC_AWAIT_CODE_RESULT(do_maintenance(ctx));
      break;
    case team_room_timer_event_type::kKickOfflineMember:
      RPC_AWAIT_CODE_RESULT(kick_due_offline_members(ctx, now));
      break;
    case team_room_timer_event_type::kDestroyEmptyRoom:
      RPC_AWAIT_CODE_RESULT(destroy_empty_room(ctx));
      break;
    case team_room_timer_event_type::kDestroyChannel: {
      if (subscriber_ && subscriber_->is_ready() && lock_acquired_ && !channel_destroy_sent_) {
        auto checker = make_write_lock_checker();
        auto ret = RPC_AWAIT_CODE_RESULT(subscriber_->send_destroy(ctx, checker));
        if (0 == ret) {
          channel_destroy_sent_ = true;
        } else if (PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_LOCK_FAILED == ret) {
          channel_destroy_sent_ = true;
          step_down();
        } else {
          FCTXLOGERROR(ctx, "team room {}:{} destroy channel failed: {}", team_key_.zone_id(), team_key_.team_id(),
                       ret);
        }
      }
      break;
    }
    default:
      break;
  }
  RPC_RETURN_CODE(0);
}

rpc::result_code_type team_room::do_maintenance(rpc::context& ctx) {
  if (!subscriber_ || !subscriber_->is_ready() || !lock_acquired_) {
    RPC_RETURN_CODE(0);
  }

  std::chrono::system_clock::time_point now = atfw::util::time::time_utility::now();
  last_maintenance_uncompacted_count_ =
      static_cast<int64_t>(subscriber_->get_cached_log_count(last_compact_sequence_ + 1));

  // 过期数据清理(过期的邀请和加入请求，对端有自身的超时失效机制，无需通知)
  RPC_AWAIT_CODE_RESULT(cleanup_expired_admissions(ctx, now));
  if (!lock_acquired_) {
    RPC_RETURN_CODE(0);
  }

  int64_t last_sequence = subscriber_->get_last_message_sequence();

  // 最早的未压缩日志时间点缺失时刷新，用于按时间维度加速触发的调度
  if (oldest_log_timepoint_ == std::chrono::system_clock::from_time_t(0) && last_sequence > last_compact_sequence_) {
    refresh_oldest_log_timepoint(ctx);
  }

  // 每次 send_update 都尝试压缩日志以减少数据量，
  // compact_log_over_percent 和 compact_log_start_time 仅作为因日志数量/时间因素提前触发维护的加速点
  int64_t compact_sequence = pick_compact_sequence(ctx, now);

  // 一次 send_update 完成乐观锁续租(reset_value)，可压缩时同时保存快照并裁剪日志
  auto self_lock = make_self_lock(now);
  auto checker = atfw::component::memory::stl::make_strong_rc<::atfw::dtmq::channel_lock_checker>();
  *checker->mutable_expect_value() = current_lock_;
  *checker->mutable_reset_value() = self_lock;
  auto rsp_checker = atfw::component::memory::stl::make_strong_rc<::atfw::dtmq::channel_lock_checker>();

  rpc::dtmq::client_subscriber::update_option options;
  auto private_data = rpc::make_shared_message<atfw::team::DTeamRoomPrivateData>(ctx);
  auto public_data = rpc::make_shared_message<atfw::team::DTeamStorage>(ctx);
  // GAP-07: 长期无新日志的新心跳不必即时持久化，但运行时数据一旦变化(标脏)，
  // 维护要随续租 update 保存 custom/private 快照，保证负载迁移转出前数据已持久化。
  // 可压缩时同样保存(GAP-08: 已过期 admission 不写入快照)
  bool save_for_runtime_dirty = runtime_data_dirty_;
  if (compact_sequence > 0 || save_for_runtime_dirty) {
    // 当前状态信息存入 custom_data(成员清单、加入请求和加入邀请列表)和 private_data(主控私有数据)，
    // custom_data 状态覆盖到最新日志，裁剪点之前的日志才被压缩移除
    // 先清脏标记再 dump: dump 点之后到达的心跳会重新置脏，保存失败时恢复标记
    runtime_data_dirty_ = false;
    dump_public_data(*public_data, now);
    public_data->set_saved_action_sequence(last_sequence);
    // private_data 必须记录本次请求成功后的压缩边界，而不是成员变量中的上一次边界。
    dump_private_data(*private_data, compact_sequence, now);
    options.save = true;
    options.compact_sequence = compact_sequence;
    options.stateful_sequence = compact_sequence;
    options.custom_data = public_data.get();
    options.private_data = private_data.get();
  }

  auto ret = RPC_AWAIT_CODE_RESULT(subscriber_->send_update(ctx, options, checker, rsp_checker));
  if (0 == ret) {
    current_lock_ = std::move(self_lock);
    next_renew_lock_timepoint_ = now + get_lock_renew_interval();
    if (compact_sequence > 0 || save_for_runtime_dirty) {
      storage_.set_saved_action_sequence(last_sequence);
    }
    if (compact_sequence > 0) {
      last_compact_sequence_ = compact_sequence;
      last_compact_timepoint_ = now;
      // 压缩点推进后，最早的未压缩日志随之变化
      refresh_oldest_log_timepoint(ctx);
    }
    RPC_RETURN_CODE(0);
  }
  if (save_for_runtime_dirty) {
    runtime_data_dirty_ = true;
  }
  if (PROJECT_NAMESPACE_ID::EN_ERR_DTMQ_CHANNEL_LOCK_FAILED == ret) {
    if (rsp_checker && rsp_checker->has_real_value()) {
      handle_lock_conflict(ctx, rsp_checker->real_value());
    } else {
      step_down();
    }
    RPC_RETURN_CODE(0);
  }
  FCTXLOGERROR(ctx, "team room {}:{} maintenance send_update failed: {}", team_key_.zone_id(), team_key_.team_id(),
               ret);
  RPC_RETURN_CODE(ret);
}

rpc::result_code_type team_room::cleanup_expired_admissions(rpc::context& /*ctx*/,
                                                            std::chrono::system_clock::time_point now) {
  // 清理过期邀请
  {
    std::vector<PROJECT_NAMESPACE_ID::DUserIDKey> expired_invitations;
    expired_invitations.reserve(pending_invitation_by_invitee_.size());
    for (const auto& invitation : pending_invitation_by_invitee_) {
      if (!invitation.second) {
        expired_invitations.push_back(invitation.first);
        continue;
      }
      if (protobuf_to_system_clock(invitation.second->expired_timepoint()) <= now) {
        expired_invitations.push_back(invitation.first);
      }
    }

    for (const auto& key : expired_invitations) {
      pending_invitation_by_invitee_.erase(key);
    }
  }

  // 清理过期加入请求
  {
    std::vector<PROJECT_NAMESPACE_ID::DUserIDKey> expired_join_request;
    expired_join_request.reserve(pending_join_request_by_requester_.size());
    for (const auto& join_request : pending_join_request_by_requester_) {
      if (!join_request.second) {
        expired_join_request.push_back(join_request.first);
        continue;
      }
      if (protobuf_to_system_clock(join_request.second->expired_timepoint()) <= now) {
        expired_join_request.push_back(join_request.first);
      }
    }

    for (const auto& key : expired_join_request) {
      pending_join_request_by_requester_.erase(key);
    }
  }
  RPC_RETURN_CODE(0);
}

rpc::result_code_type team_room::kick_due_offline_members(rpc::context& ctx,
                                                          std::chrono::system_clock::time_point now) {
  // 重试队列计时: 重发到期的移除消息
  std::vector<member_ptr_t> retry_members;
  std::vector<PROJECT_NAMESPACE_ID::DUserIDKey> invalid_keys;
  std::vector<PROJECT_NAMESPACE_ID::DUserIDKey> force_remove_keys;
  std::vector<PROJECT_NAMESPACE_ID::DUserIDKey> actived_retry_keys;
  retry_members.reserve(8);
  actived_retry_keys.reserve(8);
  uint32_t max_retry_times = get_teamsvr_room_cfg().member_channel_notification_retry_times();
  auto retry_interval = protobuf_to_system_clock(get_teamsvr_room_cfg().member_channel_notification_retry_interval());
  for (const auto& pair : member_retry_remove_) {
    if (!pair.second) {
      invalid_keys.push_back(pair.first);
      continue;
    }

    // 重试时间未到
    if (pair.second->next_retry_timepoint > now) {
      break;
    }

    // 需要更新访问位置
    auto member_iter = member_.find(pair.first);
    if (member_iter == member_.end() || !member_iter->second) {
      invalid_keys.push_back(pair.first);
      continue;
    }

    // 超过重试上限，移除
    if (pair.second->retry_times >= max_retry_times) {
      invalid_keys.push_back(pair.first);
      force_remove_keys.push_back(pair.first);
      FCTXLOGWARNING(ctx, "team_room {}:{} retry to remove member {}:{} but retry time exceeded", team_key_.zone_id(),
                     team_key_.team_id(), pair.first.zone_id(), pair.first.user_id());
      continue;
    }

    retry_members.push_back(member_iter->second);
    actived_retry_keys.push_back(pair.first);
  }

  for (const auto& key : actived_retry_keys) {
    auto iter = member_retry_remove_.find(key);
    if (iter == member_retry_remove_.end()) {
      continue;
    }

    if (!iter->second) {
      invalid_keys.push_back(key);
      continue;
    }

    ++iter->second->retry_times;
    iter->second->next_retry_timepoint = now + retry_interval;
  }

  for (const auto& key : invalid_keys) {
    member_retry_remove_.erase(key);
  }

  // 故障强制移除(GAP-03): 本地删除优先(确保无泄露)，并最后补写一次 remove 日志(no-wait
  // 尽力而为)。重试已全部失败，接受该日志可能丢失导致的节点间分叉; 若日志最终送达，
  // 各节点按事件幂等移除，本地已删除的成员再次应用移除事件无副作用
  if (!force_remove_keys.empty()) {
    std::vector<rpc::context::message_holder<atfw::team::DTeamAction>> force_actions;
    force_actions.reserve(force_remove_keys.size());
    for (const auto& key : force_remove_keys) {
      remove_member(ctx, key, atfw::team::EN_TEAM_EXIT_REASON_OFFLINE_EXPIRED, true);

      auto& action = force_actions.emplace_back(ctx);
      *action->mutable_remove_member()->mutable_user_key() = key;
      action->mutable_remove_member()->set_remove_member_reason(atfw::team::EN_TEAM_EXIT_REASON_OFFLINE_EXPIRED);
    }
    std::vector<const atfw::team::DTeamAction*> force_action_ptrs;
    force_action_ptrs.reserve(force_actions.size());
    for (auto& action : force_actions) {
      force_action_ptrs.push_back(&(*action));
    }
    RPC_AWAIT_IGNORE_RESULT(
        do_send_actions(ctx, gsl::span<const atfw::team::DTeamAction* const>{force_action_ptrs}, true));
  }

  // 重发到期的移除消息(成员已在重试队列中，绕过 send_actions 的去重直接发送;
  // 同一批到期重试合并为一次写入; 移除消息是无条件的且有重试机制，no_wait 避免阻塞)
  if (!retry_members.empty()) {
    std::vector<rpc::context::message_holder<atfw::team::DTeamAction>> retry_actions;
    retry_actions.reserve(retry_members.size());
    for (const auto& user_ptr : retry_members) {
      auto& action = retry_actions.emplace_back(ctx);
      *action->mutable_remove_member()->mutable_user_key() = user_ptr->member_data.user_key();
      action->mutable_remove_member()->set_remove_member_reason(user_ptr->exit_reason);
    }
    std::vector<const atfw::team::DTeamAction*> retry_action_ptrs;
    retry_action_ptrs.reserve(retry_actions.size());
    for (auto& action : retry_actions) {
      retry_action_ptrs.push_back(&(*action));
    }
    RPC_AWAIT_IGNORE_RESULT(
        do_send_actions(ctx, gsl::span<const atfw::team::DTeamAction* const>{retry_action_ptrs}, true));
  }

  // LRU front 为最久未心跳的成员，队伍规模小，全量扫描收集所有到期成员
  std::vector<member_ptr_t> offline_members;
  std::vector<PROJECT_NAMESPACE_ID::DUserIDKey> touch_keys;
  offline_members.reserve(8);
  touch_keys.reserve(member_retry_remove_.size());
  invalid_keys.clear();
  for (const auto& pair : member_) {
    if (member_retry_remove_.end() != member_retry_remove_.find(pair.first)) {
      // 正在重试删除的成员走重试队列
      touch_keys.push_back(pair.first);
      continue;
    }

    if (!pair.second) {
      invalid_keys.push_back(pair.first);
      continue;
    }

    if (get_member_offline_deadline(*pair.second) > now) {
      break;
    }

    offline_members.push_back(pair.second);
  }

  for (const auto& key : invalid_keys) {
    member_.erase(key);
  }

  // 刷新member_里额外需要更新的访问位置
  for (const auto& key : touch_keys) {
    member_.find(key);
  }

  // 发送remove_member消息到team channel频道, send_actions 会记录移除原因并加入重试队列
  // (同一批到期成员合并为一次写入; 移除消息是无条件的且有重试机制，no_wait 避免阻塞)
  if (!offline_members.empty()) {
    std::vector<rpc::context::message_holder<atfw::team::DTeamAction>> kick_actions;
    kick_actions.reserve(offline_members.size());
    for (const auto& user_ptr : offline_members) {
      auto& action = kick_actions.emplace_back(ctx);
      *action->mutable_remove_member()->mutable_user_key() = user_ptr->member_data.user_key();
      action->mutable_remove_member()->set_remove_member_reason(atfw::team::EN_TEAM_EXIT_REASON_OFFLINE_EXPIRED);
    }
    std::vector<const atfw::team::DTeamAction*> kick_action_ptrs;
    kick_action_ptrs.reserve(kick_actions.size());
    for (auto& action : kick_actions) {
      kick_action_ptrs.push_back(&(*action));
    }
    RPC_AWAIT_IGNORE_RESULT(send_actions(ctx, gsl::span<const atfw::team::DTeamAction* const>{kick_action_ptrs}, true));
  }

  RPC_RETURN_CODE(0);
}

rpc::result_code_type team_room::destroy_empty_room(rpc::context& ctx) {
  if (destroyed_ || !member_.empty()) {
    RPC_RETURN_CODE(0);
  }
  rpc::context::message_holder<atfw::team::DTeamAction> action(ctx);
  protobuf_copy_message(*action->mutable_destroy_team(), team_key_);
  auto ret = RPC_AWAIT_CODE_RESULT(send_action(ctx, *action));
  if (0 != ret && PROJECT_NAMESPACE_ID::EN_ERR_TEAM_DESTROYED != ret) {
    FCTXLOGERROR(ctx, "team room {}:{} destroy empty team failed: {}", team_key_.zone_id(), team_key_.team_id(), ret);
  }
  RPC_RETURN_CODE(0);
}

void team_room::dump_private_data(atfw::team::DTeamRoomPrivateData& output, int64_t compact_sequence,
                                  std::chrono::system_clock::time_point compact_timepoint) {
  output.set_last_compact_sequence(compact_sequence);
  *output.mutable_last_compact_timepoint() = protobuf_from_system_clock(compact_timepoint);
  output.set_team_created(team_created_);

  dump_team_any_data_from_map(output.mutable_private_team_data(), private_team_data_);
}

void team_room::dump_public_data(atfw::team::DTeamStorage& output, std::chrono::system_clock::time_point now) {
  protobuf_copy_message(output, storage_);
  dump_team_key(*output.mutable_team_key());

  // dump 队伍共享数据(内存中 storage_.shared_team_data 恒为空，按 key 回填)
  dump_team_any_data_from_map(output.mutable_shared_team_data(), shared_team_data_);

  // dump成员数据
  foreach_member([&output](atfw::util::nostd::nonnull<const member_ptr_t>& member) {
    auto* member_data = output.add_member();
    protobuf_copy_message(*member_data, member->member_data);
    dump_team_any_data_from_map(member_data->mutable_shared_member_data(), member->shared_member_data);
    return true;
  });

  // dump 邀请数据(GAP-08: 已过期的邀请不写入快照)。邀请/加入请求总是必须有有效期，
  // 不判空 expired_timepoint: 无有效期的记录按已过期处理直接过滤
  for (const auto& invitation : pending_invitation_by_invitee_) {
    if (!invitation.second) {
      continue;
    }
    if (protobuf_to_system_clock(invitation.second->expired_timepoint()) <= now) {
      continue;
    }

    protobuf_copy_message(*output.add_pending_invitation(), *invitation.second);
  }

  // dump 加入请求数据(GAP-08: 已过期的加入请求不写入快照，无有效期同样按已过期处理)
  for (const auto& join_request : pending_join_request_by_requester_) {
    if (!join_request.second) {
      continue;
    }
    if (protobuf_to_system_clock(join_request.second->expired_timepoint()) <= now) {
      continue;
    }

    protobuf_copy_message(*output.add_pending_join_request(), *join_request.second);
  }
}

void team_room::on_receive_snapshot_finished(rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber,
                                             int32_t result_code) {
  if (subscriber != subscriber_) {
    return;
  }
  if (result_code < 0) {
    FCTXLOGERROR(ctx, "team room {}:{} receive subscriber snapshot failed: {}({})", team_key_.zone_id(),
                 team_key_.team_id(), result_code, protobuf_mini_dumper_get_error_msg(result_code));
    return;
  }
  if (!restore_snapshot(ctx)) {
    FCTXLOGERROR(ctx, "team room {}:{} restore subscriber snapshot failed", team_key_.zone_id(), team_key_.team_id());
  }
  // 就绪后重设定时器: 非主控节点将由定时器驱动接管乐观锁成为主控节点
  schedule_next_timer();
}

void team_room::on_receive_raw_message(rpc::context& ctx, const rpc::dtmq::client_subscriber::ptr_t& subscriber,
                                       const ::atfw::dtmq::DChannelMessage& message) {
  if (subscriber != subscriber_) {
    return;
  }
  bool has_oldest_log = oldest_log_timepoint_ > std::chrono::system_clock::from_time_t(0);
  if (!apply_event_message(ctx, message)) {
    // 保留已成功加载的快照状态，后续仍可通过 snapshot_finished 覆盖恢复。
    step_down();
    return;
  }
  // 按时间维度的压缩加速依赖最早未压缩日志时间点，0->有效 迁移时立即重算定时器，
  // 否则要等下一次定时器重估(如锁续租)才会应用加速点
  if (!has_oldest_log && oldest_log_timepoint_ > std::chrono::system_clock::from_time_t(0)) {
    schedule_next_timer();
  } else if (message.detail().command_case() == ::atfw::dtmq::DChannelMessageDetail::kEvent) {
    // 数量维度的压缩加速依赖未压缩日志数增长: 新事件使缓存数越过触发线时立即重算定时器，
    // 突发写入不必等下一次续租才压缩(是否真正加速由 get_next_timer_event 的双维度放行+
    // 增量门控决定，均衡态下维护自身追加的日志不会导致加速点反复提前)
    int64_t gc_log_count = get_gc_log_count();
    const auto& cfg = get_teamsvr_room_cfg();
    int64_t keep_by_count = (std::max)(gc_log_count * cfg.compact_log_keep_percent() / kTeamPercentBase,
                                       static_cast<int64_t>(cfg.compact_log_keep_count()));
    int64_t count_trigger_line =
        (std::max)(gc_log_count * cfg.compact_log_over_percent() / kTeamPercentBase, keep_by_count);
    if (static_cast<int64_t>(subscriber_->get_cached_log_count(last_compact_sequence_ + 1)) > count_trigger_line) {
      schedule_next_timer();
    }
  }
}

void team_room::on_update_optimistic_lock(ATFW_EXPLICIT_UNUSED_ATTR rpc::context& ctx,
                                          const rpc::dtmq::client_subscriber::ptr_t& subscriber,
                                          ATFW_EXPLICIT_UNUSED_ATTR const ::atfw::dtmq::DChannelOptimisticLock& from,
                                          const ::atfw::dtmq::DChannelOptimisticLock& to) {
  if (subscriber != subscriber_) {
    return;
  }
  current_lock_ = to;
  if (to.lock_holder() == lock_holder_) {
    lock_acquired_ = true;
    next_renew_lock_timepoint_ = atfw::util::time::time_utility::now() + get_lock_renew_interval();
    if (storage_.captain_user_key().user_id() == 0 && !member_.empty()) {
      team_room_manager::me()->mark_room_pending_flush(*this);
    }
  } else if (lock_acquired_) {
    // 锁已被其他节点接管，本节点退位
    lock_acquired_ = false;
  }
  // 锁状态或过期时间变化，重算下一个定时器事件
  schedule_next_timer();
}

void team_room::on_destroyed(ATFW_EXPLICIT_UNUSED_ATTR rpc::context& ctx,
                             const rpc::dtmq::client_subscriber::ptr_t& subscriber) {
  if (subscriber != subscriber_) {
    return;
  }
  destroyed_ = true;
  lock_acquired_ = false;
  // 可能需要重置定时器
  schedule_next_timer();
}

std::chrono::system_clock::duration team_room::get_room_destroy_delay() noexcept {
  std::chrono::system_clock::duration timeout =
      protobuf_to_system_clock(get_teamsvr_room_cfg().empty_room_destroy_delay());
  if (timeout < std::chrono::seconds{1}) {
    timeout = std::chrono::seconds{5};
  }
  return timeout;
}

void team_room::change_captain(const PROJECT_NAMESPACE_ID::DUserIDKey& new_captain_key,
                               atfw::team::EnTeamPermissionRole set_role) {
  auto old_captain = find_member(storage_.captain_user_key(), false);
  auto new_captain = find_member(new_captain_key, false);
  if (old_captain == new_captain) {
    return;
  }

  if (!new_captain) {
    return;
  }

  protobuf_copy_message(*storage_.mutable_captain_user_key(), new_captain->member_data.user_key());
  // GAP-12: election_captain.role 是新队长角色；未显式指定(不高于 GUEST)时继承原队长角色，
  // 原队长缺失时回退 OWNER(如创建后首次选举/队长移除后的自动选举)。队长不再恒为 OWNER，
  // 但转移不能提升权限: 新队长角色不得高于原队长(见 check_action_permission)
  if (set_role > atfw::team::EN_TEAM_MEMBER_ROLE_GUEST) {
    new_captain->member_data.set_role(set_role);
  } else if (old_captain) {
    new_captain->member_data.set_role(old_captain->member_data.role());
  } else {
    new_captain->member_data.set_role(atfw::team::EN_TEAM_MEMBER_ROLE_OWNER);
  }
  if (old_captain) {
    old_captain->member_data.set_role(atfw::team::EN_TEAM_MEMBER_ROLE_NORMAL);
  }
}

rpc::result_code_type team_room::flush_pending_channel_message(rpc::context& ctx) {
  // 队列即将被清空(或本已为空)，从 manager 注册表注销，避免遗留无意义记录
  team_room_manager::me()->unmark_room_pending_flush(*this);

  int32_t result = 0;
  if (is_lock_holder() && storage_.captain_user_key().user_id() == 0 && !member_.empty()) {
    // 失败了没关系，下次会再重试的
    RPC_AWAIT_IGNORE_RESULT(elect_captain_after_remove(ctx));
  }

  if (pending_member_channel_actions_.empty()) {
    RPC_RETURN_CODE(result);
  }

  // 锁 fencing(GAP-06): 事件应用到 flush 之间锁可能已易主，只有当前主控节点允许发送个人频道副作用。
  // 失锁后直接丢弃待发队列(至多一次语义)，避免旧主控与新主控对同一事件各发一次通知；
  // 通知丢失窗口由对端(admission 超时失效、成员以队伍频道日志为准)兜底。
  if (!is_lock_holder()) {
    if (!pending_member_channel_actions_.empty()) {
      FCTXLOGWARNING(ctx, "team room {}:{} drop {} pending member channel notifications after losing lock",
                     team_key_.zone_id(), team_key_.team_id(), pending_member_channel_actions_.size());
      pending_member_channel_actions_.clear();
    }
    RPC_RETURN_CODE(result);
  }

  pending_team_member_channel_t pending_member_channel_actions;
  pending_member_channel_actions.swap(pending_member_channel_actions_);

  // 同一成员个人频道内的多条通知合并为一次发送(按连续相同频道分组，日志按列表顺序追加)
  for (auto& pending_channel : pending_member_channel_actions) {
    auto& pending_messages = pending_channel.second;
    if (pending_messages.empty()) {
      continue;
    }

    std::vector<const atfw::team::DTeamMemberAction*> member_actions;
    const atfw::dtmq::DChannelIdKey* batch_channel_key = &pending_messages.front().first;
    for (auto iter = pending_messages.begin(); iter != pending_messages.end(); ++iter) {
      // 频道变化时先把已收集的消息合并发送，再开启新的分组
      if (iter->first.channel_id() != batch_channel_key->channel_id() ||
          iter->first.channel_type() != batch_channel_key->channel_type()) {
        auto ret = RPC_AWAIT_CODE_RESULT(send_member_actions(
            ctx, *batch_channel_key, gsl::span<const atfw::team::DTeamMemberAction* const>{member_actions}));
        if (0 != ret) {
          FCTXLOGERROR(ctx, "team room {}:{} send member action to {}:{} failed: {}({})", team_key_.zone_id(),
                       team_key_.team_id(), batch_channel_key->channel_type(), batch_channel_key->channel_id(), ret,
                       protobuf_mini_dumper_get_error_msg(ret));
        }

        member_actions.clear();
        batch_channel_key = &iter->first;
      }
      member_actions.push_back(&iter->second);
    }

    if (!member_actions.empty()) {
      auto ret = RPC_AWAIT_CODE_RESULT(send_member_actions(
          ctx, *batch_channel_key, gsl::span<const atfw::team::DTeamMemberAction* const>{member_actions}));
      if (0 != ret) {
        FCTXLOGERROR(ctx, "team room {}:{} send member action to {}:{} failed: {}({})", team_key_.zone_id(),
                     team_key_.team_id(), batch_channel_key->channel_type(), batch_channel_key->channel_id(), ret,
                     protobuf_mini_dumper_get_error_msg(ret));
      }
    }
  }

  RPC_RETURN_CODE(result);
}

void team_room::dump_team_key(atfw::team::DTeamKey& output) const { protobuf_copy_message(output, team_key_); }

#if defined(PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS) && PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS
std::vector<PROJECT_NAMESPACE_ID::DUserIDKey> team_room::debug_member_lru_keys() const {
  std::vector<PROJECT_NAMESPACE_ID::DUserIDKey> ret;
  ret.reserve(member_.size());
  for (auto iter = member_.cbegin(); iter != member_.cend(); ++iter) {
    ret.push_back(iter->first);
  }
  return ret;
}

int64_t team_room::debug_last_compact_sequence() const noexcept { return last_compact_sequence_; }

int64_t team_room::debug_saved_action_sequence() const noexcept { return storage_.saved_action_sequence(); }
int64_t team_room::debug_acknowledge_action_sequence() const noexcept { return storage_.acknowledge_action_sequence(); }

std::chrono::system_clock::time_point team_room::debug_timer_timeout() const noexcept { return timer_timeout_; }

size_t team_room::debug_retry_remove_count() const { return member_retry_remove_.size(); }

size_t team_room::debug_pending_notification_count() const {
  size_t ret = 0;
  for (const auto& pending_channel : pending_member_channel_actions_) {
    ret += pending_channel.second.size();
  }
  return ret;
}

bool team_room::debug_maintenance_task_running() const noexcept {
  return !task_type_trait::empty(maintenance_task_) && !task_type_trait::is_exiting(maintenance_task_);
}
#endif
