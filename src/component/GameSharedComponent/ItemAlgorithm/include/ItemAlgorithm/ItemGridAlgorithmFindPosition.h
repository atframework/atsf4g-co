// Copyright 2026 atframework

#pragma once

#include "ItemAlgorithm/ItemAlgorithmConfig.h"

#include <cstdint>

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace item_algorithm {
template <typename ItemT>
struct ItemGridPendingFindPositionItem {
  const ItemT* item = nullptr;
  int64_t remaining = 0;
  int32_t item_rows = 0;
  int32_t item_cols = 0;
  int64_t accumulation_limit = 0;
};

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END