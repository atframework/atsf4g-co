// Copyright 2022 atframework
// Created by owent on 2022-03-01.
//

#include "data/friend_object.h"

#include <memory/object_allocator.h>

#include <rpc/rpc_context.h>

#include <utility>

namespace atframework {
namespace friend_api {

friend_object::friend_object(ctor_guard_t& guard) : friend_cache(guard), event_id_allocator_(0) {}

friend_object::~friend_object() {}

void friend_object::init(rpc::context& ctx) { friend_cache::init(ctx); }

friend_object::ptr_t friend_object::create(rpc::context& ctx, uint32_t zone_id, uint64_t user_id) {
  ctor_guard_t guard;
  guard.zone_id = zone_id;
  guard.user_id = user_id;
  auto ret = atfw::memory::stl::make_shared<friend_object>(guard);
  if (ret) {
    util::memory::strong_rc_ptr<friend_transaction_participator_handle> transaction_handle =
        create_transaction_handle(ctx, *ret, zone_id, user_id);
    if (!transaction_handle) {
      FCTXLOGERROR(ctx, "malloc friend_transaction_participator_handle failed");
      return nullptr;
    }

    auto wal_publisher = create_friend_publisher(ctx, *ret);
    if (!wal_publisher) {
      FCTXLOGERROR(ctx, "malloc friend_wal_publisher_type failed");
      return nullptr;
    }

    ret->transaction_handle_ = std::move(transaction_handle);
    ret->wal_publisher_ = std::move(wal_publisher);

    ret->init(ctx);
  }
  return ret;
}

void friend_object::on_loaded(rpc::context& ctx) {
  friend_cache::on_loaded(ctx);

  inviters_.clear();
  // invitees_.clear();
  friends_.clear();

  // time_t now = util::time::time_utility::get_now();
  // time_t max_expire_timepoint = now + logic_config::me()->get_logic().transaction().timeout().seconds() * 2 + 1;

  // 注意不要直接重载 load(db_data), 基类的 friend_cache::load(db_data) 和 friend_cache::load_and_move_db(db_data,
  // db_version) 都会触发这个事件 这时候数据已经被存入 db_data_ , 可通过 get_db_blob() 提取和转移数据
  table_friend_blob_data& blob_data = mutable_db_data();
  friend_key_type key;
  event_id_allocator_ = blob_data.event_id_allocator();

  // WAL Load
  if (wal_publisher_) {
    int32_t result = 0;
    friend_wal_publisher_context wal_param{ctx, result};
    wal_publisher_->load(friend_partipator_storege_type{blob_data}, wal_param);
  }

  // 事务 Load
  if (transaction_handle_ && blob_data.has_transaction_storage()) {
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
    key.zone_id = blob_data.friend_list(i).user_key().zone_id();
    key.user_id = blob_data.friend_list(i).user_key().user_id();

    friends_[key].Swap(blob_data.mutable_friend_list(i));
  }
  blob_data.clear_friend_list();

  for (int i = 0; i < blob_data.inviter_list_size(); ++i) {
    key.zone_id = blob_data.inviter_list(i).from_user().zone_id();
    key.user_id = blob_data.inviter_list(i).from_user().user_id();
    // 删除的好友被邀请可能会保留一段时间的缓存, 不计数
    // if (blob_data.inviter_list(i).expired_time() > max_expire_timepoint &&
    //     blob_data.inviter_list(i).removed_time() > 0) {
    //   ++inviter_number_cache_;
    // }

    inviters_[key].Swap(blob_data.mutable_inviter_list(i));
  }
  blob_data.clear_inviter_list();

  // for (int i = 0; i < blob_data.invitee_list_size(); ++i) {
  //   key.zone_id = blob_data.invitee_list(i).to_zone_id();
  //   key.user_id = blob_data.invitee_list(i).to_user_id();

  //   invitees_[key].Swap(blob_data.mutable_invitee_list(i));
  // }
  blob_data.clear_invitee_list();

  refresh_feature_limit(ctx);
}

void friend_object::on_saved(rpc::context& ctx, uint64_t svr_id) {
  friend_cache::on_saved(ctx, svr_id);
  // TODO(owent): ...
}

int friend_object::dump(rpc::context& ctx, PROJECT_NAMESPACE_ID::table_friend& db_data) {
  friend_cache::dump(ctx, db_data);
  // TODO(owent): ...
  return 0;
}

bool friend_object::add_friend(rpc::context& /*ctx*/, int64_t /*event_id*/, DFriendInfo& /*friend_data*/) {
  // TODO(owent): ...
  return false;
}

bool friend_object::remove_friend(rpc::context& /*ctx*/, int64_t /*event_id*/, DFriendInfo& /*friend_data*/) {
  // TODO(owent): ...
  return false;
}

bool friend_object::add_inviter(rpc::context& /*ctx*/, int64_t /*event_id*/, DFriendInvitationInfo& /*src*/) {
  // TODO(owent): ...
  return false;
}

bool friend_object::remove_inviter(rpc::context& /*ctx*/, int64_t /*event_id*/, DFriendInvitationInfo& /*src*/) {
  // TODO(owent): ...
  return false;
}

bool friend_object::add_invitee(int64_t /*event_id*/, DFriendInvitationInfo& /*src*/) {
  // TODO(owent): ...
  return false;
}

bool friend_object::remove_invitee(int64_t /*event_id*/, DFriendInvitationInfo& /*src*/) {
  // TODO(owent): ...
  return false;
}

void friend_object::refresh_feature_limit(rpc::context& /*ctx*/) {
  // TODO(owent): ...
}

int32_t friend_object::check_prepare_transcation(const ::google::protobuf::RepeatedPtrField<DFriendEvent>& /*events*/) {
  // TODO(owent): ...
  return 0;
}

int64_t friend_object::allocate_event_id() {
  // TODO(owent): ...
  return 0;
}

void friend_object::gm_reset_limit() {
  // TODO(owent): ...
}

bool friend_object::clear_all_data(int64_t /*event_id*/) {
  // TODO(owent): ...
  return false;
}

bool friend_object::is_empty() const {
  // TODO(owent): ...
  return false;
}

bool friend_object::has_relation() const {
  // TODO(owent): ...
  return false;
}

friend_wal_publisher_type& friend_object::get_wal_publisher() noexcept {
  // TODO(owent): ...
  return *wal_publisher_;
}

const friend_wal_publisher_type& friend_object::get_wal_publisher() const noexcept {
  // TODO(owent): ...
  return *wal_publisher_;
}

friend_transaction_participator_handle& friend_object::get_transaction_handle() noexcept {
  // TODO(owent): ...
  return *transaction_handle_;
}
const friend_transaction_participator_handle& friend_object::get_transaction_handle() const noexcept {
  // TODO(owent): ...
  return *transaction_handle_;
}

atfw::util::memory::strong_rc_ptr<PROJECT_NAMESPACE_ID::friend_transaction_data>
friend_object::mutable_transaction_participator_data(const std::string& /*transaction_uuid*/, friend_key_type /*key*/,
                                                     const google::protobuf::Any& /*data*/) {
  // TODO(owent): ...
  return nullptr;
}

void friend_object::remove_transaction_data(const std::string& /*transaction_uuid*/) {
  // TODO(owent): ...
}

void friend_object::cleanup_invalid_friends(time_t /*now*/, time_t /*max_expire_timepoint*/,
                                            int32_t& /*friend_number_cache*/) {
  // TODO(owent): ...
}

void friend_object::cleanup_invalid_invitees(time_t /*now*/, time_t /*max_expire_timepoint*/,
                                             int32_t& /*invitee_number_cache*/) {
  // TODO(owent): ...
}

int32_t friend_object::get_current_friend_num() const {
  // TODO(owent): ...
  return 0;
}

int32_t friend_object::get_current_inviter_num() const {
  // TODO(owent): ...
  return 0;
}

void friend_object::check_inviter_num_exceed(rpc::context& /*ctx*/, int64_t /*event_id*/) {
  // TODO(owent): ...
}

void friend_object::set_quick_save() const {
  // TODO(owent): ...
}
}  // namespace friend_api
}  // namespace atframework
