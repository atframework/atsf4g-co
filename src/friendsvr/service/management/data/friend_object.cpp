// Copyright 2026 atframework
// Created by owent on 2026-09-22.
//

#include "data/friend_object.h"

#include <memory/object_allocator.h>

#include <rpc/rpc_context.h>

#include <config/excel_config_const_index.h>
#include <config/logic_config.h>

#include <logic/misc/logic_datetime_cache.h>

#include <router/router_friend_manager.h>

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef min
#  undef min
#endif

namespace atframework {
namespace friend_api {

namespace {
static std::chrono::system_clock::duration get_configure_friend_invite_expire_timeout() {
  const auto& cfg_value = excel::get_const_config().friend_invite_expire();
  if (cfg_value.seconds() > 0) {
    return protobuf_to_system_clock(cfg_value);
  }
  return std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::hours{72});
}

static std::chrono::system_clock::duration get_configure_friend_gift_expire_timeout() {
  const auto& cfg_value = excel::get_const_config().friend_gift_expire();
  if (cfg_value.seconds() > 0) {
    return protobuf_to_system_clock(cfg_value);
  }
  return std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::hours{24});
}

static size_t get_configure_friend_total_inviter_limit() {
  auto friend_total_inviter_limit = excel::get_const_config().friend_total_inviter_limit();
  if (friend_total_inviter_limit <= 0) {
    return 100;
  }
  return static_cast<size_t>(friend_total_inviter_limit);
}

static std::chrono::system_clock::duration get_configure_transaction_timeout() {
  return protobuf_to_system_clock(logic_config::me()->get_logic_cfg().transaction().timeout());
}

static size_t get_configure_friend_max_number() {
  auto friend_max_number = excel::get_const_config().friend_max_number();
  if (friend_max_number <= 0) {
    return 200;
  }

  return static_cast<size_t>(friend_max_number);
}

static size_t get_configure_friend_daily_invite_limit() {
  auto friend_daily_invite_limit = excel::get_const_config().friend_daily_invite_limit();
  if (friend_daily_invite_limit <= 0) {
    return 200;
  }

  return static_cast<size_t>(friend_daily_invite_limit);
}

static size_t get_configure_friend_total_invitee_limit() {
  auto friend_total_invitee_limit = excel::get_const_config().friend_total_invitee_limit();
  if (friend_total_invitee_limit <= 0) {
    return 2000;
  }

  return static_cast<size_t>(friend_total_invitee_limit);
}

}  // namespace

friend_object::friend_object(
    ctor_guard_t& guard,
    atfw::util::nostd::nonnull<atfw::util::memory::strong_rc_ptr<friend_wal_publisher_type>>&& wal_publisher,
    atfw::util::nostd::nonnull<atfw::util::memory::strong_rc_ptr<friend_transaction_participator_handle>>&&
        transaction_handle)
    : friend_cache(guard),
      already_setup_quick_save_(false),
      event_id_allocator_(0),
      wal_publisher_(std::move(wal_publisher)),
      transaction_handle_(std::move(transaction_handle)) {
  transaction_handle_->set_private_data(reinterpret_cast<void*>(this));
  wal_publisher_->get_private_data() = this;
}

friend_object::~friend_object() {}

void friend_object::init(rpc::context& ctx) { friend_cache::init(ctx); }

friend_object::ptr_t friend_object::create(rpc::context& ctx, uint32_t zone_id, uint64_t user_id) {
  ctor_guard_t guard;
  guard.zone_id = zone_id;
  guard.user_id = user_id;

  util::memory::strong_rc_ptr<friend_transaction_participator_handle> transaction_handle =
      create_transaction_handle(ctx, zone_id, user_id);
  if (!transaction_handle) {
    FCTXLOGERROR(ctx, "malloc friend_transaction_participator_handle failed");
    return nullptr;
  }

  auto wal_publisher = create_friend_publisher(ctx);
  if (!wal_publisher) {
    FCTXLOGERROR(ctx, "malloc friend_wal_publisher_type failed");
    return nullptr;
  }

  auto ret =
      atfw::memory::stl::make_shared<friend_object>(guard, std::move(wal_publisher), std::move(transaction_handle));
  if (ret) {
    ret->init(ctx);
  }
  return ret;
}

void friend_object::on_loaded(rpc::context& ctx) {
  friend_cache::on_loaded(ctx);

  gifts_.clear();
  gift_sender_index_.clear();
  inviters_.clear();
  invitees_.clear();
  friends_.clear();

  auto now = ctx.logical_now();

  // 注意不要直接重载 load(db_data), 基类的 friend_cache::load(db_data) 和 friend_cache::load_and_move_db(db_data,
  // db_version) 都会触发这个事件 这时候数据已经被存入 db_data_ , 可通过 get_db_blob() 提取和转移数据
  table_friend_blob_data& blob_data = mutable_db_data();
  friend_key_type key;
  event_id_allocator_ = blob_data.event_id_allocator();

  // WAL Load
  {
    int32_t result = 0;
    friend_wal_publisher_context wal_param{ctx, result};
    wal_publisher_->load(friend_partipator_storege_type{blob_data}, wal_param);
  }

  // 事务 Load
  if (blob_data.has_transaction_storage()) {
    friend_transaction_participator_handle::snapshot_type storage;
    if (blob_data.transaction_storage().UnpackTo(&storage)) {
      transaction_handle_->load(storage);
    } else {
      std::string error_msg = storage.InitializationErrorString();
      const std::string type_url = protobuf_get_any_type_url<friend_transaction_participator_handle::snapshot_type>();
      if (error_msg.empty() && type_url != blob_data.transaction_storage().type_url()) {
        error_msg =
            "type mismatch, expect: " + type_url + " , got: " + std::string{blob_data.transaction_storage().type_url()};
      }

      FCTXLOGDEBUG(ctx, "{} unpack transaction storage failed, msg: {}", *this, error_msg);
    }
  }

  for (int i = 0; i < blob_data.friend_list_size(); ++i) {
    const auto& friend_data = blob_data.friend_list(i);

    // 过期数据忽略
    if ((friend_data.removed_time().seconds() > 0 && protobuf_to_system_clock(friend_data.removed_time()) <= now) ||
        (friend_data.expired_time().seconds() > 0 && protobuf_to_system_clock(friend_data.expired_time()) <= now)) {
      continue;
    }

    key.zone_id = friend_data.user_key().zone_id();
    key.user_id = friend_data.user_key().user_id();

    friends_[key].Swap(blob_data.mutable_friend_list(i));
  }
  blob_data.clear_friend_list();

  for (int i = 0; i < blob_data.inviter_list_size(); ++i) {
    const auto& inviter_data = blob_data.inviter_list(i);

    // 过期数据忽略
    if ((inviter_data.removed_time().seconds() > 0 && protobuf_to_system_clock(inviter_data.removed_time()) <= now) ||
        (inviter_data.expired_time().seconds() > 0 && protobuf_to_system_clock(inviter_data.expired_time()) <= now)) {
      continue;
    }

    key.zone_id = inviter_data.from_user().zone_id();
    key.user_id = inviter_data.from_user().user_id();

    inviters_[key].Swap(blob_data.mutable_inviter_list(i));
  }
  blob_data.clear_inviter_list();

  for (int i = 0; i < blob_data.invitee_list_size(); ++i) {
    const auto& invitee_data = blob_data.invitee_list(i);

    // 过期数据忽略
    if ((invitee_data.removed_time().seconds() > 0 && protobuf_to_system_clock(invitee_data.removed_time()) <= now) ||
        (invitee_data.expired_time().seconds() > 0 && protobuf_to_system_clock(invitee_data.expired_time()) <= now)) {
      continue;
    }

    key.zone_id = invitee_data.to_user().zone_id();
    key.user_id = invitee_data.to_user().user_id();

    invitees_[key].Swap(blob_data.mutable_invitee_list(i));
  }
  blob_data.clear_invitee_list();

  for (int i = 0; i < blob_data.gift_list_size(); ++i) {
    const auto& gift = blob_data.gift_list(i);

    // 过期数据忽略
    if ((gift.removed_time().seconds() > 0 && protobuf_to_system_clock(gift.removed_time()) <= now) ||
        (gift.expired_time().seconds() > 0 && protobuf_to_system_clock(gift.expired_time()) <= now)) {
      continue;
    }

    key.zone_id = gift.from_user().zone_id();
    key.user_id = gift.from_user().user_id();
    int64_t id = gift.gift_id();

    gift_sender_index_[key].insert(id);
    gifts_[id].Swap(blob_data.mutable_gift_list(i));
  }
  blob_data.clear_gift_list();

  refresh_feature_limit(ctx);
}

void friend_object::on_saved(rpc::context& ctx, uint64_t svr_id) {
  friend_cache::on_saved(ctx, svr_id);

  // 如果是离线保存，这里的 svr_id 会是0
  if (svr_id != logic_config::me()->get_local_server_id()) {
    return;
  }

  refresh_feature_limit(ctx);
}

int friend_object::dump(rpc::context& ctx, PROJECT_NAMESPACE_ID::table_friend& db_data) {
  int ret = base_type::dump(ctx, db_data);
  if (ret < 0) {
    return ret;
  }
  already_setup_quick_save_ = false;

  auto now = ctx.logical_now();

  table_friend_blob_data& blob_data = *db_data.mutable_blob_data();
  blob_data.clear_friend_list();
  blob_data.clear_inviter_list();
  blob_data.clear_invitee_list();
  blob_data.clear_gift_list();

  blob_data.set_event_id_allocator(event_id_allocator_);

  // WAL Dump
  {
    int32_t result = 0;
    friend_wal_publisher_context wal_param{ctx, result};
    friend_partipator_storege_type storage{blob_data};
    wal_publisher_->dump(storage, wal_param);
  }

  // 事务 Dump
  {
    friend_transaction_participator_handle::snapshot_type storage;
    transaction_handle_->dump(storage);
    if (!blob_data.mutable_transaction_storage()->PackFrom(storage)) {
      FCTXLOGDEBUG(ctx, "{} pack transaction storage failed, msg: {}", *this,
                   blob_data.transaction_storage().InitializationErrorString());
    }
  }

  cleanup_invalid_friends(ctx, now);
  for (auto& kv : friends_) {
    protobuf_copy_message(*blob_data.add_friend_list(), kv.second);
  }

  cleanup_invalid_inviters(ctx, now);
  for (auto& kv : inviters_) {
    protobuf_copy_message(*blob_data.add_inviter_list(), kv.second);
  }

  cleanup_invalid_invitees(ctx, now);
  for (auto& kv : invitees_) {
    protobuf_copy_message(*blob_data.add_invitee_list(), kv.second);
  }

  cleanup_invalid_gifts(ctx, now);
  for (const auto& kv : gifts_) {
    protobuf_copy_message(*blob_data.add_gift_list(), kv.second);
  }

  return ret;
}

bool friend_object::add_friend(rpc::context& ctx, int64_t event_id, DFriendInfo& friend_data) {
  friend_key_type key;
  key.zone_id = friend_data.user_key().zone_id();
  key.user_id = friend_data.user_key().user_id();

  // 已过期则忽略
  if (friend_data.expired_time().seconds() > 0 &&
      protobuf_to_system_clock(friend_data.expired_time()) <= ctx.logical_now()) {
    return false;
  }

  DFriendInfo& dst = friends_[key];

  // Spanner 使用的是 Wound-wait 策略， 这里 event_id 相当于 Spanner 里的 TrueTime， 而且我们保证对单个key一定递增
  // 这里的冲突处理更简单一些，直接忽略老的即可
  // 这里有个特殊的地方是 好友的 event_id 和 邀请/被邀请的 event_id 也必须互斥
  if (dst.event_id() >= event_id) {
    return false;
  }

  friend_data.set_event_id(event_id);

  protobuf_from_system_clock(*friend_data.mutable_created_time(), ctx.logical_now());
  friend_data.mutable_removed_time()->Clear();
  // 好友 expired_time 为0表示永不过期

  protobuf_copy_message(dst, friend_data);

  FCTXLOGDEBUG(ctx, "{} event {} add/update friend {}:{}, current count:{}", *this, event_id, key.zone_id, key.user_id,
               get_current_friend_count());

  // TODO(any): OSS

  set_quick_save();
  return true;
}

bool friend_object::remove_friend(rpc::context& ctx, int64_t event_id, DFriendInfo& friend_data) {
  friend_key_type key;
  key.zone_id = friend_data.user_key().zone_id();
  key.user_id = friend_data.user_key().user_id();

  auto iter = friends_.find(key);
  if (iter == friends_.end()) {
    return false;
  }

  if (iter->second.event_id() >= event_id) {
    return false;
  }

  if (iter->second.removed_time().seconds() > 0) {
    iter->second.set_event_id(event_id);
    return false;
  }

  iter->second.set_event_id(event_id);
  auto now = ctx.logical_now();
  protobuf_from_system_clock(*iter->second.mutable_removed_time(), now);
  protobuf_copy_message(friend_data, iter->second);
  friends_.erase(iter);

  FCTXLOGDEBUG(ctx, "event {} remove friend {}:{}, current count:{}", *this, event_id, key.zone_id, key.user_id,
               get_current_friend_count());

  // TODO(any): OSS

  set_quick_save();
  return true;
}

bool friend_object::add_inviter(rpc::context& ctx, int64_t event_id, DFriendInvitationInfo& src) {
  friend_key_type key;
  key.zone_id = src.from_user().zone_id();
  key.user_id = src.from_user().user_id();

  DFriendInvitationInfo& dst = inviters_[key];

  if (dst.event_id() >= event_id) {
    return false;
  }

  src.set_event_id(event_id);
  protobuf_from_system_clock(*src.mutable_created_time(), ctx.logical_now());
  src.mutable_removed_time()->Clear();
  protobuf_from_system_clock(*src.mutable_expired_time(),
                             ctx.logical_now() + get_configure_friend_invite_expire_timeout());

  protobuf_copy_message(dst, src);

  FCTXLOGDEBUG(ctx, "{} event {} add/update inviter {}:{}", *this, event_id, key.zone_id, key.user_id);

  // stats
  DFriendStatistics& stats_data = mutable_statistics();
  stats_data.set_daily_inviter(stats_data.daily_inviter() + 1);
  stats_data.set_weekly_inviter(stats_data.weekly_inviter() + 1);
  stats_data.set_sum_inviter(stats_data.sum_inviter() + 1);

  // TODO(any): OSS

  check_inviter_count_exceed(ctx, event_id);

  set_quick_save();
  return true;
}

bool friend_object::remove_inviter(rpc::context& ctx, int64_t event_id, DFriendInvitationInfo& src) {
  friend_key_type key;
  key.zone_id = src.from_user().zone_id();
  key.user_id = src.from_user().user_id();

  auto iter = inviters_.find(key);
  if (iter == inviters_.end()) {
    return false;
  }

  if (iter->second.event_id() >= event_id) {
    return false;
  }

  iter->second.set_event_id(event_id);
  auto now = ctx.logical_now();
  protobuf_from_system_clock(*iter->second.mutable_removed_time(), now);

  protobuf_copy_message(src, iter->second);

  inviters_.erase(iter);

  FCTXLOGDEBUG(ctx, "{} event {} remove inviter {}:{}", *this, event_id, key.zone_id, key.user_id);

  // TODO(any): OSS

  set_quick_save();
  return true;
}

bool friend_object::remove_all_inviters(rpc::context& ctx, int64_t event_id) {
  if (inviters_.empty()) {
    return false;
  }

  std::unordered_set<friend_key_type, friend_key_hash_type> remove_keys;
  remove_keys.reserve(inviters_.size());

  for (auto& kv : inviters_) {
    if (kv.second.event_id() >= event_id) {
      continue;
    }

    remove_keys.insert(kv.first);
  }

  int64_t remove_event_id = 0;
  if (!remove_keys.empty()) {
    remove_event_id = allocate_event_id();
  }

  for (const auto& k : remove_keys) {
    rpc::context::message_holder<DFriendInvitationInfo> to_remove_inviter{ctx};
    to_remove_inviter->mutable_from_user()->set_zone_id(k.zone_id);
    to_remove_inviter->mutable_from_user()->set_user_id(k.user_id);
    remove_inviter(ctx, remove_event_id, *to_remove_inviter);
  }

  // TODO(any): OSS

  if (!remove_keys.empty()) {
    set_quick_save();
  }
  return !remove_keys.empty();
}

bool friend_object::add_invitee(rpc::context& ctx, int64_t event_id, DFriendInvitationInfo& src) {
  friend_key_type key;
  key.zone_id = src.to_user().zone_id();
  key.user_id = src.to_user().user_id();

  DFriendInvitationInfo& dst = invitees_[key];

  if (dst.event_id() >= event_id) {
    return false;
  }

  src.set_event_id(event_id);
  protobuf_from_system_clock(*src.mutable_created_time(), ctx.logical_now());
  src.mutable_removed_time()->Clear();
  protobuf_from_system_clock(*src.mutable_expired_time(),
                             ctx.logical_now() + get_configure_friend_invite_expire_timeout());

  protobuf_copy_message(dst, src);

  FCTXLOGDEBUG(ctx, "{} event {} add/update invitee {}:{}", *this, event_id, key.zone_id, key.user_id);

  // stats
  DFriendStatistics& stats_data = mutable_statistics();
  stats_data.set_daily_invitee(stats_data.daily_invitee() + 1);
  stats_data.set_weekly_invitee(stats_data.weekly_invitee() + 1);
  stats_data.set_sum_invitee(stats_data.sum_invitee() + 1);

  return true;
}

bool friend_object::remove_invitee(rpc::context& ctx, int64_t event_id, DFriendInvitationInfo& src) {
  friend_key_type key;
  key.zone_id = src.to_user().zone_id();
  key.user_id = src.to_user().user_id();

  auto iter = invitees_.find(key);
  if (iter == invitees_.end()) {
    return false;
  }

  if (iter->second.event_id() >= event_id) {
    return false;
  }

  iter->second.set_event_id(event_id);
  auto now = ctx.logical_now();
  protobuf_from_system_clock(*iter->second.mutable_removed_time(), now);
  protobuf_copy_message(src, iter->second);

  invitees_.erase(iter);

  FCTXLOGDEBUG(ctx, "{} event {} removed invitee {}:{}", *this, event_id, key.zone_id, key.user_id);

  return true;
}

bool friend_object::add_gift(rpc::context& ctx, int64_t event_id, DFriendGift& src) {
  if (0 == src.gift_id()) {
    return false;
  }

  friend_key_type key;
  key.zone_id = src.from_user().zone_id();
  key.user_id = src.from_user().user_id();

  do {
    auto user_index_iter = gift_sender_index_.find(key);
    if (gift_sender_index_.end() == user_index_iter) {
      break;
    }

    auto id_index_iter = user_index_iter->second.find(src.gift_id());
    if (id_index_iter == user_index_iter->second.end()) {
      break;
    }

    // 找出老的礼物并删除,如果仅仅是索引删除索引即可
    auto gift_iter = gifts_.find(*id_index_iter);
    if (gift_iter == gifts_.end()) {
      user_index_iter->second.erase(id_index_iter);
      if (user_index_iter->second.empty()) {
        gift_sender_index_.erase(user_index_iter);
      }
    } else {
      remove_gift(ctx, event_id, gift_iter->second);
    }
  } while (false);

  DFriendGift& dst = gifts_[src.gift_id()];

  if (dst.event_id() >= event_id) {
    return false;
  }

  if (key.zone_id != 0 && key.user_id != 0) {
    gift_sender_index_[key].insert(src.gift_id());
  }

  src.set_event_id(event_id);
  protobuf_from_system_clock(*src.mutable_created_time(), ctx.logical_now());
  src.mutable_removed_time()->Clear();
  protobuf_from_system_clock(*src.mutable_expired_time(),
                             ctx.logical_now() + get_configure_friend_gift_expire_timeout());

  protobuf_copy_message(dst, src);

  FCTXLOGDEBUG(ctx, "{} event {} add/update gift {} from {}:{}", *this, event_id, src.gift_id(),
               src.from_user().zone_id(), src.from_user().user_id());

  set_quick_save();
  return true;
}

bool friend_object::remove_gift(rpc::context& ctx, int64_t event_id, DFriendGift& src) {
  int64_t gift_id = src.gift_id();
  auto iter = gifts_.find(gift_id);
  if (iter == gifts_.end()) {
    return false;
  }

  if (iter->second.event_id() >= event_id) {
    return false;
  }
  iter->second.set_event_id(event_id);
  auto now = ctx.logical_now();
  protobuf_from_system_clock(*iter->second.mutable_removed_time(), now);

  friend_key_type key;
  key.zone_id = iter->second.from_user().zone_id();
  key.user_id = iter->second.from_user().user_id();
  protobuf_copy_message(src, iter->second);

  FCTXLOGDEBUG(ctx, "{} event {} remove gift {} from {}:{}", *this, event_id, gift_id,
               iter->second.from_user().zone_id(), iter->second.from_user().user_id());

  gifts_.erase(iter);
  auto user_index_iter = gift_sender_index_.find(key);
  if (user_index_iter != gift_sender_index_.end()) {
    user_index_iter->second.erase(gift_id);
    if (user_index_iter->second.empty()) {
      gift_sender_index_.erase(user_index_iter);
    }
  }

  set_quick_save();
  return true;
}

void friend_object::refresh_feature_limit(rpc::context& ctx) {
  // 刷新限制数据
  auto now = ctx.logical_now();
  DFriendStatistics& stats_data = mutable_statistics();

  auto next_daily_reset_time = protobuf_to_system_clock(stats_data.next_daily_reset_time());
  if (now >= next_daily_reset_time ||
      now + std::chrono::seconds{atfw::util::time::time_utility::DAY_SECONDS} < next_daily_reset_time) {
    protobuf_from_system_clock(
        *stats_data.mutable_next_daily_reset_time(),
        logic_datetime_cache_get_next_day_start_timepoint(logic_datetime_cache_get_default_daily_refresh_offset()));

    stats_data.set_daily_inviter(0);
    stats_data.set_daily_invitee(0);
    stats_data.set_daily_send_gift_times(0);
    stats_data.set_daily_receive_gift_times(0);
  }

  auto next_weekly_reset_time = protobuf_to_system_clock(stats_data.next_weekly_reset_time());
  if (now >= next_weekly_reset_time ||
      now + std::chrono::seconds{atfw::util::time::time_utility::WEEK_SECONDS} < next_weekly_reset_time) {
    protobuf_from_system_clock(
        *stats_data.mutable_next_weekly_reset_time(),
        logic_datetime_cache_get_next_week_start_timepoint(logic_datetime_cache_get_default_daily_refresh_offset()));
    stats_data.set_weekly_inviter(0);
    stats_data.set_weekly_invitee(0);
    stats_data.set_weekly_send_gift_times(0);
    stats_data.set_weekly_receive_gift_times(0);
  }

  // WAL Tick
  {
    int32_t result = 0;
    friend_wal_publisher_context wal_param{ctx, result};
    wal_publisher_->tick(now, wal_param);
  }
  // 事务 Tick
  {
    transaction_handle_->tick(ctx, now);
  }

  // 清理缓存
  while (!transaction_participator_data_cache_.empty()) {
    if (now <= (*transaction_participator_data_cache_.begin()->second).timeout) {
      break;
    }

    transaction_participator_data_cache_.erase(transaction_participator_data_cache_.begin());
  }
}

rpc::result_code_type friend_object::send_notification(rpc::context& ctx) {
  // TODO(owent): 打包和下发数据
  RPC_RETURN_CODE(0);
}

namespace {
static void friend_object_merge_transcation_events(size_t& add_invitee_count, size_t& add_inviter_count,
                                                   size_t& add_friend_count,
                                                   const ::google::protobuf::RepeatedPtrField<DFriendEvent>& events) {
  for (int i = 0; i < events.size(); ++i) {
    switch (events.Get(i).event_case()) {
      case DFriendEvent::kAddInvitee:
        ++add_invitee_count;
        break;
      case DFriendEvent::kAddInviter:
        ++add_inviter_count;
        break;
      case DFriendEvent::kAddFriendData:
        ++add_friend_count;
        break;
      default:
        break;
    }
  }
}
}  // namespace

int32_t friend_object::check_prepare_transcation(rpc::context& ctx,
                                                 const ::google::protobuf::RepeatedPtrField<DFriendEvent>& events) {
  size_t add_invitee_count = 0;
  size_t add_inviter_count = 0;
  size_t add_friend_count = 0;

  friend_object_merge_transcation_events(add_invitee_count, add_inviter_count, add_friend_count, events);
  // 也要附加正在运行的事务事件
  {
    for (const auto& running_transaction : transaction_handle_->get_running_transactions()) {
      if (!running_transaction.second.storage) {
        continue;
      }

      auto trans_data = mutable_transaction_participator_data(
          ctx, running_transaction.second.storage->metadata().transaction_uuid(),
          friend_key_type{get_zone_id(), get_user_id()}, running_transaction.second.storage->participator_data());
      if (!trans_data) {
        continue;
      }
      friend_object_merge_transcation_events(add_invitee_count, add_inviter_count, add_friend_count,
                                             trans_data->event_data());
    }
  }

  if (add_invitee_count <= 0 && add_friend_count <= 0 && add_inviter_count <= 0) {
    return 0;
  }

  // 检查好友数量上限
  if (friends_.size() + add_friend_count > get_configure_friend_max_number()) {
    return PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_MAX_NUMBER_LIMIT;
  }

  // 好友数量到达上限后不允许再增加邀请和被邀请
  if (friends_.size() >= get_configure_friend_max_number() && (add_inviter_count > 0 || add_invitee_count > 0)) {
    return PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_MAX_NUMBER_LIMIT;
  }

  // 检查每日加邀请的数量
  if (get_statistics().daily_invitee() + add_invitee_count > get_configure_friend_daily_invite_limit()) {
    return PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_INVITE_DAILY_LIMIT;
  }

  auto now = ctx.logical_now();

  // 检查邀请的数量
  size_t friend_total_invitee_limit = get_configure_friend_total_invitee_limit();
  if (invitees_.size() + add_invitee_count > friend_total_invitee_limit) {
    cleanup_invalid_invitees(ctx, now);
    if (invitees_.size() + add_invitee_count > friend_total_invitee_limit) {
      return PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_INVITEE_TOTAL_LIMIT;
    }
  }

  // 检查被邀请的数量
  size_t friend_total_inviter_limit = get_configure_friend_total_inviter_limit();
  if (inviters_.size() + add_inviter_count > friend_total_inviter_limit) {
    cleanup_invalid_inviters(ctx, now);
    if (inviters_.size() + add_inviter_count > friend_total_inviter_limit) {
      return PROJECT_NAMESPACE_ID::EN_ERR_FRIEND_INVITER_TOTAL_LIMIT;
    }
  }

  return 0;
}

int64_t friend_object::allocate_event_id() { return ++event_id_allocator_; }

void friend_object::gm_reset_limit() {
  DFriendStatistics& stats_data = mutable_statistics();
  stats_data.set_daily_send_gift_times(0);
  stats_data.set_daily_receive_gift_times(0);
  stats_data.set_daily_invitee(0);
  stats_data.set_daily_inviter(0);
  stats_data.set_weekly_send_gift_times(0);
  stats_data.set_weekly_receive_gift_times(0);
  stats_data.set_weekly_invitee(0);
  stats_data.set_weekly_inviter(0);
  stats_data.set_sum_send_gift_times(0);
  stats_data.set_sum_receive_gift_times(0);
  stats_data.set_sum_invitee(0);
  stats_data.set_sum_inviter(0);
}

void friend_object::subscribe(rpc::context& ctx, const DFriendSubscribeKey& subscribe_key,
                              uint64_t update_server_node_id) {
  // Implementation for subscribing to friend notifications
  if (subscribe_key.subscriber_user_key().zone_id() == 0 || subscribe_key.subscriber_user_key().user_id() == 0) {
    return;
  }

  int32_t result_code = 0;
  friend_wal_publisher_context param{ctx, result_code};

  auto subscriber = wal_publisher_->find_subscriber(subscribe_key.subscriber_user_key(), param);
  if (subscriber) {
    subscriber->get_private_data().subscriber_server_node_id = update_server_node_id;
    wal_publisher_->receive_subscribe_request(subscribe_key.subscriber_user_key(),
                                              subscribe_key.last_received().sequence(),
                                              subscribe_key.last_received().hash_code(), ctx.logical_now(), param);
  } else {
    friend_wal_subscriber_private_data private_data;
    private_data.subscriber_server_node_id = update_server_node_id;
    subscriber = wal_publisher_->create_subscriber(
        subscribe_key.subscriber_user_key(), ctx.logical_now(),
        {subscribe_key.last_received().sequence(), subscribe_key.last_received().hash_code()}, param, private_data);
  }
}

void friend_object::unsubscribe(rpc::context& ctx, const DFriendSubscribeKey& subscribe_key) {
  // Implementation for unsubscribing from friend notifications
  if (subscribe_key.subscriber_user_key().zone_id() == 0 || subscribe_key.subscriber_user_key().user_id() == 0) {
    return;
  }

  int32_t result_code = 0;
  friend_wal_publisher_context param{ctx, result_code};
  auto subscriber = wal_publisher_->find_subscriber(subscribe_key.subscriber_user_key(), param);
  if (subscriber) {
    subscriber->get_private_data().subscriber_server_node_id = 0;
    wal_publisher_->remove_subscriber(subscribe_key.subscriber_user_key(),
                                      atfw::util::distributed_system::wal_unsubscribe_reason::kClientRequest, param);
  }
}

bool friend_object::clear_all_data(rpc::context& /*ctx*/, int64_t event_id) {
  wal_publisher_->set_global_log_ingore_key(event_id);

  // 仅仅移除event_id更小的记录
  {
    std::vector<int64_t> pending_to_erase;
    std::vector<friend_key_type> pending_to_erase_index;
    pending_to_erase.reserve(gifts_.size());
    pending_to_erase_index.reserve(gifts_.size());
    for (auto& check : gifts_) {
      if (check.second.event_id() <= event_id) {
        pending_to_erase.push_back(check.first);
      }
    }

    for (auto& erase_key : pending_to_erase) {
      auto iter = gifts_.find(erase_key);
      if (iter == gifts_.end()) {
        continue;
      }
      friend_key_type friend_key;
      friend_key.user_id = iter->second.from_user().user_id();
      friend_key.zone_id = iter->second.from_user().zone_id();

      gifts_.erase(iter);
      auto user_index_iter = gift_sender_index_.find(friend_key);
      if (user_index_iter != gift_sender_index_.end()) {
        user_index_iter->second.erase(erase_key);
        if (user_index_iter->second.empty()) {
          gift_sender_index_.erase(user_index_iter);
        }
      }
    }
  }

  {
    std::vector<friend_key_type> pending_to_erase;
    pending_to_erase.reserve(inviters_.size());
    for (auto& check : inviters_) {
      if (check.second.event_id() <= event_id) {
        pending_to_erase.push_back(check.first);
      }
    }

    for (auto& erase_key : pending_to_erase) {
      inviters_.erase(erase_key);
    }
  }
  {
    std::vector<friend_key_type> pending_to_erase;
    pending_to_erase.reserve(invitees_.size());
    for (auto& check : invitees_) {
      if (check.second.event_id() <= event_id) {
        pending_to_erase.push_back(check.first);
      }
    }

    for (auto& erase_key : pending_to_erase) {
      invitees_.erase(erase_key);
    }
  }
  {
    std::vector<friend_key_type> pending_to_erase;
    pending_to_erase.reserve(friends_.size());
    for (auto& check : friends_) {
      if (check.second.event_id() <= event_id) {
        pending_to_erase.push_back(check.first);
      }
    }

    for (auto& erase_key : pending_to_erase) {
      friends_.erase(erase_key);
    }
  }

  return true;
}

bool friend_object::is_empty() const {
  return gifts_.empty() && gift_sender_index_.empty() && inviters_.empty() && invitees_.empty() && friends_.empty();
}

bool friend_object::has_relation() const { return !(inviters_.empty() && invitees_.empty() && friends_.empty()); }

friend_wal_publisher_type& friend_object::get_wal_publisher() noexcept { return *wal_publisher_; }

const friend_wal_publisher_type& friend_object::get_wal_publisher() const noexcept { return *wal_publisher_; }

friend_transaction_participator_handle& friend_object::get_transaction_handle() noexcept {
  return *transaction_handle_;
}

const friend_transaction_participator_handle& friend_object::get_transaction_handle() const noexcept {
  return *transaction_handle_;
}

atfw::util::memory::strong_rc_ptr<PROJECT_NAMESPACE_ID::friend_transaction_data>
friend_object::mutable_transaction_participator_data(rpc::context& ctx, const std::string& transaction_uuid,
                                                     friend_key_type key, const google::protobuf::Any& raw_data) {
  auto iter_trans = transaction_participator_data_cache_.find(transaction_uuid);

  atfw::util::memory::strong_rc_ptr<transaction_participator_data_cache_t> cache_ptr;
  if (iter_trans != transaction_participator_data_cache_.end()) {
    cache_ptr = iter_trans->second;
    auto iter_data = iter_trans->second->participator_data.find(key);
    if (iter_data != iter_trans->second->participator_data.end()) {
      return iter_data->second;
    }
  }

  atfw::util::memory::strong_rc_ptr<PROJECT_NAMESPACE_ID::friend_transaction_data> ret =
      atfw::memory::stl::make_strong_rc<PROJECT_NAMESPACE_ID::friend_transaction_data>();
  if (!ret) {
    return ret;
  }

  if (false == raw_data.UnpackTo(ret.get())) {
    std::string error_msg = ret->InitializationErrorString();
    const std::string& expected_type_url = protobuf_get_any_type_url<PROJECT_NAMESPACE_ID::friend_transaction_data>();
    if (error_msg.empty() && expected_type_url != raw_data.type_url()) {
      error_msg = "type mismatch, expect: " + expected_type_url + " , got: " + std::string{raw_data.type_url()};
    }

    FWLOGERROR("{} unpack friend_transaction_data failed, msg: {}", *this, error_msg);
    return nullptr;
  }

  if (!cache_ptr) {
    cache_ptr = atfw::memory::stl::make_strong_rc<transaction_participator_data_cache_t>();
    cache_ptr->timeout = ctx.logical_now() + get_configure_transaction_timeout();
    cache_ptr->timeout += std::chrono::seconds{2};

    cache_ptr->participator_data[key] = ret;

    transaction_participator_data_cache_.insert_key_value(transaction_uuid, std::move(cache_ptr));
  } else {
    cache_ptr->participator_data[key] = ret;
  }

  set_quick_save();

  return ret;
}

void friend_object::remove_transaction_data(rpc::context& /*ctx*/, const std::string& transaction_uuid) {
  // 提前移除缓存数据
  transaction_participator_data_cache_.erase(transaction_uuid);

  set_quick_save();
}

void friend_object::cleanup_invalid_friends(rpc::context& ctx, std::chrono::system_clock::time_point now) {
  if (friends_.empty()) {
    return;
  }

  std::unordered_set<friend_key_type, friend_key_hash_type> expired_keys;
  auto tolerate_time = std::chrono::seconds{logic_config::me()->get_const_settings().time_tolerate()};

  expired_keys.reserve(friends_.size());

  for (auto& kv : friends_) {
    if ((kv.second.removed_time().seconds() > 0 && now >= protobuf_to_system_clock(kv.second.removed_time())) ||
        (now >= protobuf_to_system_clock(kv.second.expired_time()) + tolerate_time)) {
      expired_keys.insert(kv.first);
      continue;
    }
  }

  int64_t new_event_id = 0;
  if (!expired_keys.empty()) {
    new_event_id = allocate_event_id();
  }

  for (const auto& k : expired_keys) {
    rpc::context::message_holder<DFriendInfo> to_remove_friend{ctx};
    to_remove_friend->mutable_user_key()->set_user_id(k.user_id);
    to_remove_friend->mutable_user_key()->set_zone_id(k.zone_id);
    remove_friend(ctx, new_event_id, *to_remove_friend);
  }
}

void friend_object::cleanup_invalid_inviters(rpc::context& ctx, std::chrono::system_clock::time_point now) {
  if (inviters_.empty()) {
    return;
  }

  std::unordered_set<friend_key_type, friend_key_hash_type> expired_keys;
  auto tolerate_time = std::chrono::seconds{logic_config::me()->get_const_settings().time_tolerate()};

  expired_keys.reserve(inviters_.size());

  for (auto& kv : inviters_) {
    if ((kv.second.removed_time().seconds() > 0 && now >= protobuf_to_system_clock(kv.second.removed_time())) ||
        (now >= protobuf_to_system_clock(kv.second.expired_time()) + tolerate_time)) {
      expired_keys.insert(kv.first);
      continue;
    }
  }

  int64_t new_event_id = 0;
  if (!expired_keys.empty()) {
    new_event_id = allocate_event_id();
  }

  for (const auto& k : expired_keys) {
    rpc::context::message_holder<DFriendInvitationInfo> to_remove_inviter{ctx};
    to_remove_inviter->mutable_from_user()->set_zone_id(k.zone_id);
    to_remove_inviter->mutable_from_user()->set_user_id(k.user_id);
    remove_inviter(ctx, new_event_id, *to_remove_inviter);
  }
}

void friend_object::cleanup_invalid_invitees(rpc::context& ctx, std::chrono::system_clock::time_point now) {
  if (invitees_.empty()) {
    return;
  }

  std::unordered_set<friend_key_type, friend_key_hash_type> expired_keys;
  auto tolerate_time = std::chrono::seconds{logic_config::me()->get_const_settings().time_tolerate()};

  expired_keys.reserve(invitees_.size());

  for (auto& kv : invitees_) {
    if ((kv.second.removed_time().seconds() > 0 && now >= protobuf_to_system_clock(kv.second.removed_time())) ||
        (now >= protobuf_to_system_clock(kv.second.expired_time()) + tolerate_time)) {
      expired_keys.insert(kv.first);
      continue;
    }
  }

  int64_t new_event_id = 0;
  if (!expired_keys.empty()) {
    new_event_id = allocate_event_id();
  }

  for (const auto& k : expired_keys) {
    rpc::context::message_holder<DFriendInvitationInfo> to_remove_invitee{ctx};
    to_remove_invitee->mutable_to_user()->set_zone_id(k.zone_id);
    to_remove_invitee->mutable_to_user()->set_user_id(k.user_id);
    remove_invitee(ctx, new_event_id, *to_remove_invitee);
  }
}

void friend_object::cleanup_invalid_gifts(rpc::context& ctx, std::chrono::system_clock::time_point now) {
  if (gifts_.empty()) {
    return;
  }

  std::unordered_set<int64_t> expired_ids;
  auto tolerate_time = std::chrono::seconds{logic_config::me()->get_const_settings().time_tolerate()};
  for (const auto& kv : gifts_) {
    if ((kv.second.removed_time().seconds() > 0 && now >= protobuf_to_system_clock(kv.second.removed_time())) ||
        (now >= protobuf_to_system_clock(kv.second.expired_time()) + tolerate_time)) {
      expired_ids.insert(kv.first);
      continue;
    }
  }

  int64_t new_event_id = 0;
  if (!expired_ids.empty()) {
    new_event_id = allocate_event_id();
  }

  for (const auto& k : expired_ids) {
    DFriendGift to_remove_gift;
    to_remove_gift.set_gift_id(k);
    remove_gift(ctx, new_event_id, to_remove_gift);
  }
}

size_t friend_object::get_current_friend_count() const noexcept { return friends_.size(); }

size_t friend_object::get_current_inviter_count() const noexcept { return inviters_.size(); }

void friend_object::check_inviter_count_exceed(rpc::context& ctx, int64_t event_id) {
  size_t friend_total_inviter_limit = get_configure_friend_total_inviter_limit();
  size_t current_inviter_count = get_current_inviter_count();
  if (current_inviter_count <= friend_total_inviter_limit) {
    return;
  }

  // 超出数量限制，要自动删除
  size_t remove_count = current_inviter_count - friend_total_inviter_limit;
  using vec_t = std::vector<std::pair<friend_key_type, std::chrono::system_clock::time_point>>;
  vec_t friend_inviter_list;
  friend_inviter_list.reserve(inviters_.size());
  for (const auto& inviter_info : inviters_) {
    friend_inviter_list.emplace_back(inviter_info.first, protobuf_to_system_clock(inviter_info.second.created_time()));
  }
  remove_count = std::min(remove_count, friend_inviter_list.size());
  std::partial_sort(friend_inviter_list.begin(),
                    friend_inviter_list.begin() + static_cast<vec_t::difference_type>(remove_count),
                    friend_inviter_list.end(),
                    [](const std::pair<friend_key_type, std::chrono::system_clock::time_point>& a,
                       const std::pair<friend_key_type, std::chrono::system_clock::time_point>& b) -> bool {
                      return a.second < b.second;
                    });

  int32_t result = 0;
  friend_wal_publisher_context param{ctx, result};
  for (size_t i = 0; i < remove_count; i++) {
    DFriendEvent event_log;
    event_log.mutable_remove_inviter()->mutable_from_user()->set_user_id(friend_inviter_list[i].first.user_id);
    event_log.mutable_remove_inviter()->mutable_from_user()->set_zone_id(friend_inviter_list[i].first.zone_id);
    auto wal_log = wal_publisher_->allocate_log(ctx.logical_now(), DFriendEvent::kRemoveInviter, param, event_log);
    if (!wal_log) {
      FCTXLOGERROR(ctx, "{} malloc DFriendEvent failed {} {} event_id:{}", *this, friend_inviter_list[i].first.user_id,
                   friend_inviter_list[i].first.zone_id, event_id);
      continue;
    }
    wal_publisher_->emplace_back_log(std::move(wal_log), param);
  }
}

void friend_object::set_quick_save() const {
  if (already_setup_quick_save_) {
    return;
  }
  already_setup_quick_save_ = true;

  if (router_manager_set::is_instance_destroyed()) {
    return;
  }

  router_manager_base* mgr = router_manager_set::me()->get_manager(router_friend_manager::me()->get_type_id());
  if (mgr == nullptr) {
    return;
  }

  router_manager_base::key_t key(router_friend_manager::me()->get_type_id(), get_zone_id(), get_user_id());
  std::shared_ptr<router_object_base> obj = mgr->get_base_cache(key);
  if (!obj || !obj->is_writable()) {
    return;
  }

  router_manager_set::me()->mark_fast_save(mgr, obj);
  FWLOGDEBUG("quick_save {}", *this);
}
}  // namespace friend_api
}  // namespace atframework
