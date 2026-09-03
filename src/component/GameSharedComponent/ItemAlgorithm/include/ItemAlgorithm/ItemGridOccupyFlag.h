// Copyright 2026 atframework

#pragma once

#include "ItemAlgorithm/ItemAlgorithmConfig.h"

#include <cstdint>
#include <vector>

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace item_algorithm {

/// @brief 背包占用位图 (多行打成一行, 每 64 列一个 uint64)
///
/// 内部以单个 uint64 数组按行优先存储位图, 支持按 (x, y) 快速检查/设置占用,
/// 便于后续寻位优化 (位运算找空位)。
class ATFW_UTIL_SYMBOL_VISIBLE ItemGridOccupyFlag {
 public:
  ITEM_ALGORITHM_API ItemGridOccupyFlag();
  ITEM_ALGORITHM_API ItemGridOccupyFlag(const ItemGridOccupyFlag&);
  ITEM_ALGORITHM_API ItemGridOccupyFlag& operator=(const ItemGridOccupyFlag&);

  /// @brief 初始化位图 (rows 行, cols 列), 全部清零
  ITEM_ALGORITHM_API void resize(size_t rows, size_t cols);

  /// @brief 全部清零 (保留行列尺寸)
  ITEM_ALGORITHM_API void clear();

  /// @brief 是否无任何行
  ATFW_UTIL_FORCEINLINE bool empty() const { return data_.empty(); }

  /// @brief 行数
  ATFW_UTIL_FORCEINLINE size_t row_count() const { return row_count_; }

  /// @brief 列数
  ATFW_UTIL_FORCEINLINE size_t column_count() const { return column_count_; }

  /// @brief 每行占用的 uint64 数量
  ATFW_UTIL_FORCEINLINE size_t words_per_row() const { return words_per_row_; }

  /// @brief 底层数据 (行优先, 每行 words_per_row() 个 uint64)
  ATFW_UTIL_FORCEINLINE const std::vector<uint64_t>& data() const { return data_; }

  /// @brief 检查 (x, y) 是否被占用
  ITEM_ALGORITHM_API bool is_occupied(int32_t x, int32_t y) const;

  /// @brief 设置 (x, y) 占用状态
  ITEM_ALGORITHM_API void set(int32_t x, int32_t y, bool occupied);

 private:
  ATFW_UTIL_SYMBOL_LOCAL std::vector<uint64_t> data_;
  ATFW_UTIL_SYMBOL_LOCAL size_t row_count_ = 0;
  ATFW_UTIL_SYMBOL_LOCAL size_t column_count_ = 0;
  ATFW_UTIL_SYMBOL_LOCAL size_t words_per_row_ = 0;
};

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END