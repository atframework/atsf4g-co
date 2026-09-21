// Copyright 2022 atframework
// Created by owent on 2022-03-01.
//

#include "data/friend_transaction_participator_handle.h"

namespace atframework {
namespace friend_api {

namespace {
static size_t _hash_combine(size_t l, size_t r) noexcept { return r + 0x9e3779b9 + (l << 6) + (l >> 2); }
}  // namespace

size_t friend_key_hash_type::operator()(const friend_key_type& in) const noexcept {
  std::hash<uint32_t> lh;
  std::hash<uint64_t> rh;

  return _hash_combine(lh(in.zone_id), rh(in.user_id));
}

atfw::util::memory::strong_rc_ptr<friend_transaction_participator_handle> create_transaction_handle(
    rpc::context&, friend_object&, uint32_t /*zone_id*/, uint64_t /*user_id*/) {
  // TODO(owent): ...
  return nullptr;
}

}  // namespace friend_api
}  // namespace atframework
