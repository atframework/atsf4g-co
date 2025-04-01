// Copyright 2025 atframework

#pragma once

#include <ItemAlgorithm/ItemAlgorithmConfig.h>

PROJECT_NAMESPACE_BEGIN
class SERVER_FRAME_PROTOCOL_COMMON_API DItemBoxPosition;
class SERVER_FRAME_PROTOCOL_COMMON_API DItemSlotPosition;
class SERVER_FRAME_PROTOCOL_COMMON_API DItemPosition;
PROJECT_NAMESPACE_END

ITEM_ALGORITHM_NAMESPACE_BEGIN

struct ItemPositionHash {
  ATFW_UTIL_FORCEINLINE ItemPositionHash() noexcept = default;
  ATFW_UTIL_FORCEINLINE ~ItemPositionHash() noexcept = default;
  ATFW_UTIL_FORCEINLINE ItemPositionHash(const ItemPositionHash&) noexcept = default;
  ATFW_UTIL_FORCEINLINE ItemPositionHash(ItemPositionHash&&) noexcept = default;

  ATFW_UTIL_FORCEINLINE ItemPositionHash& operator=(const ItemPositionHash&) noexcept = default;
  ATFW_UTIL_FORCEINLINE ItemPositionHash& operator=(ItemPositionHash&&) noexcept = default;

  ITEM_ALGORITHM_API size_t operator()(const PROJECT_NAMESPACE_ID::DItemBoxPosition& position) const noexcept;
  ITEM_ALGORITHM_API size_t operator()(const PROJECT_NAMESPACE_ID::DItemSlotPosition& position) const noexcept;
  ITEM_ALGORITHM_API size_t operator()(const PROJECT_NAMESPACE_ID::DItemPosition& position) const noexcept;
};

struct ItemPositionEqualTo {
  ATFW_UTIL_FORCEINLINE ItemPositionEqualTo() noexcept = default;
  ATFW_UTIL_FORCEINLINE ~ItemPositionEqualTo() noexcept = default;
  ATFW_UTIL_FORCEINLINE ItemPositionEqualTo(const ItemPositionEqualTo&) noexcept = default;
  ATFW_UTIL_FORCEINLINE ItemPositionEqualTo(ItemPositionEqualTo&&) noexcept = default;

  ATFW_UTIL_FORCEINLINE ItemPositionEqualTo& operator=(const ItemPositionEqualTo&) noexcept = default;
  ATFW_UTIL_FORCEINLINE ItemPositionEqualTo& operator=(ItemPositionEqualTo&&) noexcept = default;

  ITEM_ALGORITHM_API bool operator()(const PROJECT_NAMESPACE_ID::DItemBoxPosition& l,
                                     const PROJECT_NAMESPACE_ID::DItemBoxPosition& r) const noexcept;

  ITEM_ALGORITHM_API bool operator()(const PROJECT_NAMESPACE_ID::DItemSlotPosition& l,
                                     const PROJECT_NAMESPACE_ID::DItemSlotPosition& r) const noexcept;

  ITEM_ALGORITHM_API bool operator()(const PROJECT_NAMESPACE_ID::DItemPosition& l,
                                     const PROJECT_NAMESPACE_ID::DItemPosition& r) const noexcept;
};

ITEM_ALGORITHM_NAMESPACE_END
