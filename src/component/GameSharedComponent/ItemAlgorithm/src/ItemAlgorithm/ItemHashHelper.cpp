// Copyright 2025 atframework

#include "ItemAlgorithm/ItemHashHelper.h"

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/com.struct.item.common.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <assert.h>
#include <xxhash.h>

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace {

// template <typename T>
// ATFW_UTIL_FORCEINLINE static T __hash_combine(T h1, T h2) noexcept {
//   h1 ^= h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2);
//   return h1;
// }

template <class T>
static unsigned char* __append_hash_data(unsigned char* dst, size_t len, T d) {
  assert(sizeof(d) <= len);
  if (sizeof(d) > len) {
    return dst;
  }
  *reinterpret_cast<T*>(dst) = d;
  dst += sizeof(d);
  return dst;
}

ATFW_UTIL_FORCEINLINE static size_t __hash_buffer(unsigned char* buffer, size_t len, size_t seed) {
  return static_cast<size_t>(XXH64(buffer, len, static_cast<XXH64_hash_t>(seed)));
}
}  // namespace

ITEM_ALGORITHM_API size_t
ItemPositionHash::operator()(const PROJECT_NAMESPACE_ID::DItemBoxPosition& position) const noexcept {
  unsigned char hash_data[32] = {};
  unsigned char* ptr = __append_hash_data(hash_data, sizeof(hash_data), position.x());
  ptr = __append_hash_data(hash_data, sizeof(hash_data), position.y());
  return __hash_buffer(hash_data, static_cast<size_t>(ptr - hash_data), static_cast<size_t>(15631515927093459181ULL));
}

ITEM_ALGORITHM_API size_t
ItemPositionHash::operator()(const PROJECT_NAMESPACE_ID::DItemSlotPosition& position) const noexcept {
  unsigned char hash_data[16] = {};
  unsigned char* ptr = __append_hash_data(hash_data, sizeof(hash_data), position.slot_idx());
  return __hash_buffer(hash_data, static_cast<size_t>(ptr - hash_data), static_cast<size_t>(17554239190826720461ULL));
}

ITEM_ALGORITHM_API size_t
ItemPositionHash::operator()(const PROJECT_NAMESPACE_ID::DItemPosition& position) const noexcept {
  unsigned char hash_data[64] = {};
  unsigned char* ptr =
      __append_hash_data(hash_data, sizeof(hash_data), static_cast<int32_t>(position.posotion_type_case()));
  switch (position.posotion_type_case()) {
    case PROJECT_NAMESPACE_ID::DItemPosition::kInventory: {
      ptr = __append_hash_data(ptr, sizeof(hash_data) - static_cast<size_t>(ptr - hash_data), position.inventory().x());
      ptr = __append_hash_data(ptr, sizeof(hash_data) - static_cast<size_t>(ptr - hash_data), position.inventory().y());
      return __hash_buffer(hash_data, static_cast<size_t>(ptr - hash_data),
                           static_cast<size_t>(11091358986149305771ULL));
    }
    case PROJECT_NAMESPACE_ID::DItemPosition::kRoleBackpack: {
      ptr =
          __append_hash_data(ptr, sizeof(hash_data) - static_cast<size_t>(ptr - hash_data), position.container_guid());
      ptr = __append_hash_data(ptr, sizeof(hash_data) - static_cast<size_t>(ptr - hash_data),
                               position.role_backpack().x());
      ptr = __append_hash_data(ptr, sizeof(hash_data) - static_cast<size_t>(ptr - hash_data),
                               position.role_backpack().y());
      return __hash_buffer(hash_data, static_cast<size_t>(ptr - hash_data),
                           static_cast<size_t>(11091358986149305771ULL));
    }
    case PROJECT_NAMESPACE_ID::DItemPosition::kRoleEquipment: {
      ptr =
          __append_hash_data(ptr, sizeof(hash_data) - static_cast<size_t>(ptr - hash_data), position.container_guid());
      ptr = __append_hash_data(ptr, sizeof(hash_data) - static_cast<size_t>(ptr - hash_data),
                               position.role_equipment().slot_idx());
      return __hash_buffer(hash_data, static_cast<size_t>(ptr - hash_data),
                           static_cast<size_t>(11091358986149305771ULL));
    }
    default:
      return 0;
  }
}

ITEM_ALGORITHM_API bool ItemPositionEqualTo::operator()(
    const PROJECT_NAMESPACE_ID::DItemBoxPosition& l, const PROJECT_NAMESPACE_ID::DItemBoxPosition& r) const noexcept {
  return l.x() == r.x() && l.y() == r.y();
}

ITEM_ALGORITHM_API bool ItemPositionEqualTo::operator()(
    const PROJECT_NAMESPACE_ID::DItemSlotPosition& l, const PROJECT_NAMESPACE_ID::DItemSlotPosition& r) const noexcept {
  return l.slot_idx() == r.slot_idx();
}

ITEM_ALGORITHM_API bool ItemPositionEqualTo::operator()(const PROJECT_NAMESPACE_ID::DItemPosition& l,
                                                        const PROJECT_NAMESPACE_ID::DItemPosition& r) const noexcept {
  if (l.posotion_type_case() != r.posotion_type_case()) {
    return false;
  }

  switch (l.posotion_type_case()) {
    case PROJECT_NAMESPACE_ID::DItemPosition::kInventory: {
      return (*this)(l.inventory(), r.inventory());
    }
    case PROJECT_NAMESPACE_ID::DItemPosition::kRoleBackpack: {
      if (l.container_guid() != r.container_guid()) {
        return false;
      }
      return (*this)(l.role_backpack(), r.role_backpack());
    }
    case PROJECT_NAMESPACE_ID::DItemPosition::kRoleEquipment: {
      if (l.container_guid() != r.container_guid()) {
        return false;
      }
      return (*this)(l.role_equipment(), r.role_equipment());
    }
    default:
      return 0;
  }
}

ITEM_ALGORITHM_NAMESPACE_END
