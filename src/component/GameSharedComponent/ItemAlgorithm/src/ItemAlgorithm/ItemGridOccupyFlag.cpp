// Copyright 2026 atframework

#include "ItemAlgorithm/ItemGridOccupyFlag.h"

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace item_algorithm {

// ============================================================
// ItemGridOccupyFlag — 背包占用位图
// ============================================================

void ItemGridOccupyFlag::resize(size_t rows, size_t cols) {
  row_count_ = rows;
  column_count_ = cols;
  words_per_row_ = (cols + 63) / 64;
  data_.assign(rows * words_per_row_, 0);
}

void ItemGridOccupyFlag::clear() { std::fill(data_.begin(), data_.end(), 0); }

bool ItemGridOccupyFlag::is_occupied(int32_t x, int32_t y) const {
  if (y < 0 || static_cast<size_t>(y) >= row_count_ || x < 0 || static_cast<size_t>(x) >= column_count_) {
    return false;
  }
  const size_t word_index = static_cast<size_t>(y) * words_per_row_ + (static_cast<size_t>(x) >> 6);
  const size_t bit_index = static_cast<size_t>(x) & 63;
  return (data_[word_index] >> bit_index) & 1u;
}

void ItemGridOccupyFlag::set(int32_t x, int32_t y, bool occupied) {
  if (y < 0 || static_cast<size_t>(y) >= row_count_ || x < 0 || static_cast<size_t>(x) >= column_count_) {
    return;
  }
  const size_t word_index = static_cast<size_t>(y) * words_per_row_ + (static_cast<size_t>(x) >> 6);
  const size_t bit_index = static_cast<size_t>(x) & 63;
  uint64_t& word = data_[word_index];
  if (occupied) {
    word |= (uint64_t{1} << bit_index);
  } else {
    word &= ~(uint64_t{1} << bit_index);
  }
}

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END