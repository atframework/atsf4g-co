// Copyright 2026 atframework
// Created by owent on 2026-11-26.
//

#include "trade/trade_algorithm.h"

#include <config/compile_optimize.h>

#include <xxhash.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <protocol/common/svr.trade.common.pb.h>
#include <protocol/config/com.struct.trade.config.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <common/string_oprs.h>

#include <config/excel/config_traits.h>
#include <config/logic_config.h>
#include <memory/object_allocator.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

#include "log/log_wrapper.h"
#include "utility/protobuf_mini_dumper.h"

namespace atframework {
namespace trade_api {

namespace {
template <class T>
inline static void copy_buffer_into(char *start, size_t offset, T value) {
  memcpy(start + offset, &value, sizeof(value));
}

static uint64_t make_area_order_hash_key(int32_t area_code,
                                         atfw::trade::DTradeOrderTicketMeta::TypeCase order_type) noexcept {
  // 保留订单类型在原有哈希输入中的符号扩展。
  return (static_cast<uint64_t>(area_code) << 32U) | static_cast<uint64_t>(static_cast<int32_t>(order_type));
}
}  // namespace

TRADE_SDK_COMMON_API size_t product_key_hash_type::operator()(int32_t area_code, int32_t trade_product_id,
                                                              int32_t ext_id, uint64_t seed) const noexcept {
  char hash_buffer[sizeof(int32_t) * 3];
  copy_buffer_into(hash_buffer, 0, area_code);
  copy_buffer_into(hash_buffer, sizeof(int32_t), trade_product_id);
  copy_buffer_into(hash_buffer, sizeof(int32_t) * 2, ext_id);

  return static_cast<size_t>(XXH64(hash_buffer, sizeof(hash_buffer), static_cast<XXH64_hash_t>(seed)));
}

TRADE_SDK_COMMON_API size_t
product_key_hash_type::operator()(const atfw::trade::DTradeMarketProductKey &key) const noexcept {
  // 随便搞个素数作magic number
  return (*this)(key, 0, 920431627);
}

TRADE_SDK_COMMON_API size_t product_key_hash_type::operator()(const atfw::trade::DTradeMarketProductKey &key,
                                                              size_t index) const noexcept {
  // 随便搞个素数作magic number
  return (*this)(key, index, 938536211);
}

TRADE_SDK_COMMON_API size_t product_key_hash_type::operator()(const atfw::trade::DTradeMarketProductKey &key,
                                                              size_t index, uint64_t seed) const noexcept {
  char hash_buffer[sizeof(size_t) + (sizeof(int32_t) * 3)];
  copy_buffer_into(hash_buffer, 0, index);
  copy_buffer_into(hash_buffer, sizeof(size_t), key.area_code());
  copy_buffer_into(hash_buffer, sizeof(size_t) + sizeof(int32_t), key.trade_market_id());
  copy_buffer_into(hash_buffer, sizeof(size_t) + (sizeof(int32_t) * 2), key.trade_product_id());

  // 随便搞个素数作magic number
  return static_cast<size_t>(XXH64(hash_buffer, sizeof(hash_buffer), static_cast<XXH64_hash_t>(seed)));
}

TRADE_SDK_COMMON_API size_t product_key_hash_type::operator()(const atfw::trade::DTradeMarketProductKey &key,
                                                              int32_t id, uint64_t seed) const noexcept {
  char hash_buffer[sizeof(int32_t) * 4];
  copy_buffer_into(hash_buffer, 0, id);
  copy_buffer_into(hash_buffer, sizeof(int32_t), key.area_code());
  copy_buffer_into(hash_buffer, sizeof(int32_t) * 2, key.trade_market_id());
  copy_buffer_into(hash_buffer, sizeof(int32_t) * 3, key.trade_product_id());

  // 随便搞个素数作magic number
  return static_cast<size_t>(XXH64(hash_buffer, sizeof(hash_buffer), static_cast<XXH64_hash_t>(seed)));
}

TRADE_SDK_COMMON_API bool product_key_equal_type::operator()(
    const atfw::trade::DTradeMarketProductKey &left, const atfw::trade::DTradeMarketProductKey &right) const noexcept {
  return left.area_code() == right.area_code() && left.trade_market_id() == right.trade_market_id() &&
         left.trade_product_id() == right.trade_product_id();
}

TRADE_SDK_COMMON_API size_t
standard_price_key_hash_type::operator()(const atfw::trade::DTradeStandardPriceKey &key) const noexcept {
  // 随便搞个素数作magic number
  return (*this)(key, 0, 920431627);
}

TRADE_SDK_COMMON_API size_t standard_price_key_hash_type::operator()(const atfw::trade::DTradeStandardPriceKey &key,
                                                                     size_t index) const noexcept {
  // 随便搞个素数作magic number
  return (*this)(key, index, 938536211);
}

TRADE_SDK_COMMON_API size_t standard_price_key_hash_type::operator()(const atfw::trade::DTradeStandardPriceKey &key,
                                                                     size_t index, uint64_t seed) const noexcept {
  char hash_buffer[sizeof(size_t) + (sizeof(int32_t) * 3)];
  copy_buffer_into(hash_buffer, 0, index);
  copy_buffer_into(hash_buffer, sizeof(size_t), key.area_code());
  copy_buffer_into(hash_buffer, sizeof(size_t) + sizeof(int32_t), key.price_pool_id());
  copy_buffer_into(hash_buffer, sizeof(size_t) + (sizeof(int32_t) * 2), key.trade_product_id());

  // 随便搞个素数作magic number
  return static_cast<size_t>(XXH64(hash_buffer, sizeof(hash_buffer), static_cast<XXH64_hash_t>(seed)));
}

TRADE_SDK_COMMON_API size_t standard_price_key_hash_type::operator()(const atfw::trade::DTradeStandardPriceKey &key,
                                                                     int32_t id, uint64_t seed) const noexcept {
  char hash_buffer[sizeof(int32_t) * 4];
  copy_buffer_into(hash_buffer, 0, id);
  copy_buffer_into(hash_buffer, sizeof(int32_t), key.area_code());
  copy_buffer_into(hash_buffer, sizeof(int32_t) * 2, key.price_pool_id());
  copy_buffer_into(hash_buffer, sizeof(int32_t) * 3, key.trade_product_id());

  // 随便搞个素数作magic number
  return static_cast<size_t>(XXH64(hash_buffer, sizeof(hash_buffer), static_cast<XXH64_hash_t>(seed)));
}

TRADE_SDK_COMMON_API bool standard_price_key_equal_type::operator()(
    const atfw::trade::DTradeStandardPriceKey &lhs, const atfw::trade::DTradeStandardPriceKey &rhs) const noexcept {
  return lhs.area_code() == rhs.area_code() && lhs.price_pool_id() == rhs.price_pool_id() &&
         lhs.trade_product_id() == rhs.trade_product_id();
}

TRADE_SDK_COMMON_API size_t trade_pair_hash_type::operator()(const std::pair<int32_t, int32_t> &key) const noexcept {
  return excel::traits::tuple_hasher<std::pair<int32_t, int32_t>>()(
      std::tuple<int32_t, int32_t>{key.first, key.second});
}

TRADE_SDK_COMMON_API bool trade_pair_equal_type::operator()(const std::pair<int32_t, int32_t> &left,
                                                            const std::pair<int32_t, int32_t> &right) const noexcept {
  return left.first == right.first && left.second == right.second;
}

TRADE_SDK_COMMON_API atfw::trade::trade_order_ticket_key trade_ticket_meta_to_key(
    const atfw::trade::DTradeOrderTicketMeta &meta) {
  atfw::trade::trade_order_ticket_key ret;
  ret.set_area_code(meta.trade_product_key().area_code());
  ret.set_slot_id(meta.slot_id());
  ret.set_trade_order_ticket_id(meta.trade_order_ticket_id());
  ret.set_order_type(meta.type_case());

  return ret;
}

TRADE_SDK_COMMON_API std::string trade_ticket_to_transaction_participator_key(int32_t area_code,
                                                                              int64_t order_ticket_id, int32_t slot_id,
                                                                              int32_t order_type) {
  return atfw::util::string::format("trade:{}:{}:{}:{}", area_code, order_ticket_id, slot_id, order_type);
}

TRADE_SDK_COMMON_API atfw::trade::trade_order_ticket_key transaction_participator_key_to_trade_ticket(
    const std::string &key) {
  if (key.size() <= 6) {
    return {};
  }
  const char *start = key.c_str();
  const char *next = start + 6;
  int32_t area_code = 0;
  int64_t trade_order_ticket_id = 0;
  int32_t slot_id = 0;
  int32_t order_type = 0;
  next = atfw::util::string::str2int(area_code, next, key.size() - static_cast<size_t>(next - start));
  if (next != nullptr && next + 1 < start + key.size()) {
    next = atfw::util::string::str2int(trade_order_ticket_id, next + 1,
                                       key.size() - static_cast<size_t>(next - start) - 1);
    if (next != nullptr && next + 1 < start + key.size()) {
      next = atfw::util::string::str2int(slot_id, next + 1, key.size() - static_cast<size_t>(next - start) - 1);
      if (next != nullptr && next + 1 < start + key.size()) {
        atfw::util::string::str2int(order_type, next + 1, key.size() - static_cast<size_t>(next - start) - 1);
        atfw::trade::trade_order_ticket_key ret;
        ret.set_area_code(area_code);
        ret.set_trade_order_ticket_id(trade_order_ticket_id);
        ret.set_slot_id(slot_id);
        ret.set_order_type(order_type);
        FWLOGDEBUG(
            "transaction_participator_key_to_trade_ticket key {} area_code {} trade_order_ticket_id {} slot_id {} "
            "order_type {}",
            key, area_code, trade_order_ticket_id, slot_id, order_type);
        return ret;
      }
    }
  }
  return {};
}

TRADE_SDK_COMMON_API const ::atfw::distributed_system::transaction_client_handle::transaction_options &
get_normal_transaction_options() {
  static std::unique_ptr<atfw::distributed_system::transaction_client_handle::transaction_options> options;
  if (options) {
    return *options;
  }

  options = std::make_unique<atfw::distributed_system::transaction_client_handle::transaction_options>();

  // 采用数据库CAS一致性算法
  options->replication_read_count = 0;
  options->replication_total_count = 0;
  options->memory_only = false;
  options->timeout = protobuf_to_system_clock(logic_config::me()->get_logic_cfg().transaction().timeout());

  return *options;
}

TRADE_SDK_COMMON_API const ::atfw::distributed_system::transaction_client_handle::transaction_options &
get_force_commit_transaction_options() {
  static std::unique_ptr<atfw::distributed_system::transaction_client_handle::transaction_options> options;
  if (options) {
    return *options;
  }

  options = std::make_unique<atfw::distributed_system::transaction_client_handle::transaction_options>(
      get_normal_transaction_options());
  options->force_commit = true;
  return *options;
}

TRADE_SDK_COMMON_API void normalize(atfw::trade::DTradeOrderTicketLabel &label) {
  // 商品Tag标准化
  if (label.item_tag_size() > 0) {
    auto &tags = *label.mutable_item_tag();
    // 类型排序
    std::sort(tags.begin(), tags.end(),
              [](const atfw::trade::DTradeOrderTicketLabelTag &l, const atfw::trade::DTradeOrderTicketLabelTag &r) {
                return l.label_type() < r.label_type();
              });

    // Tag排序
    for (auto &tag_values : tags) {
      std::sort(tag_values.mutable_label_value()->begin(), tag_values.mutable_label_value()->end());
    }
  }

  // 商品Attribute 标准化
  if (label.item_attribute_size() > 0) {
    auto &attributes = *label.mutable_item_attribute();
    std::sort(
        attributes.begin(), attributes.end(),
        [](const atfw::trade::DTradeOrderTicketLabelAttribute &l,
           const atfw::trade::DTradeOrderTicketLabelAttribute &r) { return l.attribute_type() < r.attribute_type(); });
  }
}

TRADE_SDK_COMMON_API void normalize(atfw::trade::DTradeOrderTicketSearchRule &rule) {
  // 价格范围标准化
  if (rule.min_price() < 0) {
    rule.set_min_price(0);
  }
  if (rule.max_price() < -1) {
    rule.set_max_price(-1);
  } else if (rule.max_price() >= 0 && rule.max_price() < rule.min_price()) {
    rule.set_max_price(rule.min_price());
  }

  // 搜索Tag标准化
  if (rule.item_tag_size() > 0) {
    auto &tags = *rule.mutable_item_tag();
    // Tag 值排序
    for (auto &tag_values : tags) {
      std::sort(tag_values.mutable_label_value()->begin(), tag_values.mutable_label_value()->end());
    }

    // 类型排序
    std::sort(tags.begin(), tags.end(),
              [](const atfw::trade::DTradeOrderTicketSearchTag &l, const atfw::trade::DTradeOrderTicketSearchTag &r) {
                if (l.label_type() != r.label_type()) {
                  return l.label_type() < r.label_type();
                }
                if (l.label_value_size() != r.label_value_size()) {
                  return l.label_value_size() < r.label_value_size();
                }

                for (int i = 0; i < l.label_value_size(); ++i) {
                  if (l.label_value(i) != r.label_value(i)) {
                    return l.label_value(i) < r.label_value(i);
                  }
                }

                return false;
              });
  }

  if (rule.exclude_item_tag_size() > 0) {
    auto &exclude_tags = *rule.mutable_exclude_item_tag();
    // Tag 值排序
    for (auto &tag_values : exclude_tags) {
      std::sort(tag_values.mutable_label_value()->begin(), tag_values.mutable_label_value()->end());
    }

    // 类型排序
    std::sort(exclude_tags.begin(), exclude_tags.end(),
              [](const atfw::trade::DTradeOrderTicketSearchTag &l, const atfw::trade::DTradeOrderTicketSearchTag &r) {
                if (l.label_type() != r.label_type()) {
                  return l.label_type() < r.label_type();
                }
                if (l.label_value_size() != r.label_value_size()) {
                  return l.label_value_size() < r.label_value_size();
                }

                for (int i = 0; i < l.label_value_size(); ++i) {
                  if (l.label_value(i) != r.label_value(i)) {
                    return l.label_value(i) < r.label_value(i);
                  }
                }

                return false;
              });
  }

  // 搜索Attribute 标准化
  if (rule.item_attribute_size() > 0) {
    auto &attributes = *rule.mutable_item_attribute();
    std::sort(
        attributes.begin(), attributes.end(),
        [](const atfw::trade::DTradeOrderTicketSearchAttribute &l,
           const atfw::trade::DTradeOrderTicketSearchAttribute &r) { return l.attribute_type() < r.attribute_type(); });
  }
}

TRADE_SDK_COMMON_API bool product_key_is_valid(const atfw::trade::DTradeMarketProductKey &key) {
  return key.area_code() >= 0 && key.trade_market_id() > 0 && key.trade_product_id() > 0;
}

TRADE_SDK_COMMON_API bool price_key_is_valid(const atfw::trade::DTradeStandardPriceKey &key) {
  return key.area_code() >= 0 && key.price_pool_id() > 0 && key.trade_product_id() > 0;
}

TRADE_SDK_COMMON_API size_t
product_label_view_key::hash_type::operator()(const product_label_view_key &key) const noexcept {
  return key.hash_code();
}

TRADE_SDK_COMMON_API bool product_label_view_key::hash_equal_no_bound_exclude::operator()(
    const product_label_view_key &l, const product_label_view_key &r) const noexcept {
  return product_label_view_key::equal(l, r, true);
}

TRADE_SDK_COMMON_API bool product_label_view_key::equal_type::operator()(
    const product_label_view_key &l, const product_label_view_key &r) const noexcept {
  return product_label_view_key::equal(l, r, false);
}

TRADE_SDK_COMMON_API product_label_view_key::product_label_view_key(
    std::shared_ptr<atfw::trade::DTradeOrderTicketSearchRule> &&rule)
    : hash_code_{0}, hash_code_ignore_bound_value_{0}, rule_{std::move(rule)} {}

TRADE_SDK_COMMON_API product_label_view_key::product_label_view_key(
    const atfw::trade::DTradeOrderTicketSearchRule &origin)
    : hash_code_(0),
      hash_code_ignore_bound_value_{0},
      rule_{atfw::memory::stl::make_shared<atfw::trade::DTradeOrderTicketSearchRule>(origin)} {
  normalize(*rule_);
}

TRADE_SDK_COMMON_API product_label_view_key::product_label_view_key(
    const ctor_reset_options &reset_options, const atfw::trade::DTradeOrderTicketSearchRule &origin)
    : hash_code_(0),
      hash_code_ignore_bound_value_{0},
      rule_{atfw::memory::stl::make_shared<atfw::trade::DTradeOrderTicketSearchRule>(origin)} {
  normalize(*rule_);

  if (reset_options.trade_market_id != 0) {
    rule_->set_trade_market_id(reset_options.trade_market_id);
  }

  if (reset_options.major_index_type != 0) {
    rule_->set_major_index_type(reset_options.major_index_type);
  }

  if (reset_options.minor_index_type != 0) {
    rule_->set_minor_index_type(reset_options.minor_index_type);
  }
}

TRADE_SDK_COMMON_API product_label_view_key::product_label_view_key(int32_t trade_market_id,
                                                                    const atfw::trade::DTradeProductSearchRule &origin)
    : hash_code_(0),
      hash_code_ignore_bound_value_{0},
      rule_{atfw::memory::stl::make_shared<atfw::trade::DTradeOrderTicketSearchRule>()} {
  rule_->set_trade_market_id(trade_market_id);
  rule_->set_major_index_type(origin.major_index_type());
  rule_->set_minor_index_type(origin.minor_index_type());
  rule_->set_min_price(origin.min_price());
  rule_->set_max_price(origin.max_price());

  if (origin.item_tag_size() > 0) {
    protobuf_copy_message(*rule_->mutable_item_tag(), origin.item_tag());
  }

  if (origin.item_attribute_size() > 0) {
    protobuf_copy_message(*rule_->mutable_item_attribute(), origin.item_attribute());
  }

  if (origin.exclude_item_tag_size() > 0) {
    protobuf_copy_message(*rule_->mutable_exclude_item_tag(), origin.exclude_item_tag());
  }

  normalize(*rule_);
}

TRADE_SDK_COMMON_API product_label_view_key::~product_label_view_key() {}

TRADE_SDK_COMMON_API product_label_view_key::product_label_view_key(product_label_view_key &&other) noexcept
    : hash_code_{other.hash_code_},
      hash_code_ignore_bound_value_{other.hash_code_ignore_bound_value_},
      rule_{std::move(other.rule_)} {
  other.hash_code_ = 0;
  other.hash_code_ignore_bound_value_ = 0;
}

TRADE_SDK_COMMON_API product_label_view_key::product_label_view_key(const product_label_view_key &other)
    : hash_code_{other.hash_code_},
      hash_code_ignore_bound_value_{other.hash_code_ignore_bound_value_},
      rule_{other.rule_} {}

TRADE_SDK_COMMON_API product_label_view_key &product_label_view_key::operator=(
    const product_label_view_key &other) noexcept = default;

TRADE_SDK_COMMON_API product_label_view_key &product_label_view_key::operator=(
    product_label_view_key &&other) noexcept {
  hash_code_ = other.hash_code_;
  hash_code_ignore_bound_value_ = other.hash_code_ignore_bound_value_;

  rule_ = std::move(other.rule_);
  other.hash_code_ = 0;
  other.hash_code_ignore_bound_value_ = 0;

  return *this;
}

TRADE_SDK_COMMON_API const atfw::trade::DTradeOrderTicketSearchRule &product_label_view_key::get_search_rule()
    const noexcept {
  if (rule_) {
    return *rule_;
  }

  return atfw::trade::DTradeOrderTicketSearchRule::default_instance();
}

TRADE_SDK_COMMON_API bool product_label_view_key::check_subview(const product_label_view_key &checked_subview,
                                                                bool ignore_bound_exclude) const noexcept {
  const atfw::trade::DTradeOrderTicketSearchRule &self_rule = get_search_rule();
  const atfw::trade::DTradeOrderTicketSearchRule &other_rule = checked_subview.get_search_rule();
  if (self_rule.trade_market_id() != other_rule.trade_market_id() ||
      (self_rule.major_index_type() != 0 && self_rule.major_index_type() != other_rule.major_index_type()) ||
      (self_rule.minor_index_type() != 0 && self_rule.minor_index_type() != other_rule.minor_index_type())) {
    return false;
  }

  // 排除相等
  if (hash_code() == checked_subview.hash_code()) {
    if (equal(*this, checked_subview, ignore_bound_exclude)) {
      return false;
    }
  }

  if (self_rule.price_mode() != other_rule.price_mode()) {
    return false;
  }

  if (self_rule.min_price() > other_rule.min_price() ||
      (self_rule.max_price() >= 0 && (other_rule.max_price() < 0 || other_rule.max_price() > self_rule.max_price()))) {
    return false;
  }
  if ((self_rule.min_price() > 0 || self_rule.max_price() >= 0) && self_rule.price_mode() != other_rule.price_mode()) {
    return false;
  }

  // bound_exclude 仅用于翻页不影响视图索引Key，所以不用于子视图判定
  do {
    if (ignore_bound_exclude) {
      break;
    }

    if (!self_rule.has_bound_exclude() && !other_rule.has_bound_exclude()) {
      break;
    }
    if (self_rule.bound_exclude().trade_order_ticket_id() > other_rule.bound_exclude().trade_order_ticket_id()) {
      return false;
    }
    if (self_rule.bound_exclude().price() > other_rule.bound_exclude().price()) {
      return false;
    }
    if (self_rule.bound_exclude().recommend_score() < other_rule.bound_exclude().recommend_score()) {
      return false;
    }
  } while (false);

  if (other_rule.item_tag_size() < self_rule.item_tag_size() ||
      other_rule.item_attribute_size() < self_rule.item_attribute_size() ||
      other_rule.exclude_item_tag_size() < self_rule.exclude_item_tag_size()) {
    return false;
  }

  // Tag子集判定，Include规则self包含others
  {
    auto tag_key_search_begin = self_rule.item_tag().begin();
    auto tag_key_search_end = self_rule.item_tag().end();
    for (const auto &other_tag : other_rule.item_tag()) {
      tag_key_search_begin = std::lower_bound(
          tag_key_search_begin, tag_key_search_end, other_tag.label_type(),
          [](const atfw::trade::DTradeOrderTicketSearchTag &l, int32_t r) { return l.label_type() < r; });
      if (tag_key_search_begin == tag_key_search_end) {
        return false;
      }
      if ((*tag_key_search_begin).label_type() != other_tag.label_type()) {
        return false;
      }

      if ((*tag_key_search_begin).label_value().empty()) {
        continue;
      }

      if ((*tag_key_search_begin).label_value_size() < other_tag.label_value_size()) {
        return false;
      }

      if (other_tag.label_value().empty()) {
        return false;
      }

      auto tag_value_search_begin = (*tag_key_search_begin).label_value().begin();
      auto tag_value_search_end = (*tag_key_search_begin).label_value().end();
      for (const auto &other_tag_value : other_tag.label_value()) {
        tag_value_search_begin = std::lower_bound(tag_value_search_begin, tag_value_search_end, other_tag_value);
        if (tag_value_search_begin == tag_value_search_end) {
          return false;
        }
        if ((*tag_value_search_begin) != other_tag_value) {
          return false;
        }
      }
    }
  }

  // Tag子集判定，Exclude规则others包含self
  {
    auto tag_key_search_begin = other_rule.exclude_item_tag().begin();
    auto tag_key_search_end = other_rule.exclude_item_tag().end();
    for (const auto &self_tag : self_rule.exclude_item_tag()) {
      tag_key_search_begin = std::lower_bound(
          tag_key_search_begin, tag_key_search_end, self_tag.label_type(),
          [](const atfw::trade::DTradeOrderTicketSearchTag &l, int32_t r) { return l.label_type() < r; });
      if (tag_key_search_begin == tag_key_search_end) {
        return false;
      }
      if ((*tag_key_search_begin).label_type() != self_tag.label_type()) {
        return false;
      }

      if (self_tag.label_value_size() > (*tag_key_search_begin).label_value_size()) {
        return false;
      }

      auto tag_value_search_begin = (*tag_key_search_begin).label_value().begin();
      auto tag_value_search_end = (*tag_key_search_begin).label_value().end();
      for (const auto &self_tag_value : self_tag.label_value()) {
        tag_value_search_begin = std::lower_bound(tag_value_search_begin, tag_value_search_end, self_tag_value);
        if (tag_value_search_begin == tag_value_search_end) {
          return false;
        }
        if ((*tag_value_search_begin) != self_tag_value) {
          return false;
        }
      }
    }
  }

  // Attribute子集判定
  auto attribute_key_search_begin = other_rule.item_attribute().begin();
  auto attribute_key_search_end = other_rule.item_attribute().end();
  for (const auto &self_attribute : self_rule.item_attribute()) {
    attribute_key_search_begin = std::lower_bound(
        attribute_key_search_begin, attribute_key_search_end, self_attribute.attribute_type(),
        [](const atfw::trade::DTradeOrderTicketSearchAttribute &l, int32_t r) { return l.attribute_type() < r; });
    if (attribute_key_search_begin == attribute_key_search_end) {
      return false;
    }
    if ((*attribute_key_search_begin).attribute_type() != self_attribute.attribute_type()) {
      return false;
    }

    if ((*attribute_key_search_begin).min_value() < self_attribute.min_value()) {
      return false;
    }

    if (self_attribute.max_value() >= 0 && ((*attribute_key_search_begin).max_value() < 0 ||
                                            self_attribute.max_value() < (*attribute_key_search_begin).max_value())) {
      return false;
    }
  }

  return true;
}

TRADE_SDK_COMMON_API size_t product_label_view_key::hash_code() const noexcept {
  if (hash_code_ != 0) {
    return hash_code_;
  }

  XXH64_hash_t result = 900000041;
  if (!rule_) {
    return static_cast<size_t>(result);
  }

  {
    char hash_buffer[(sizeof(int32_t) * 6) + (sizeof(int64_t) * 3)];
    copy_buffer_into(hash_buffer, 0, rule_->trade_market_id());
    copy_buffer_into(hash_buffer, sizeof(int32_t), rule_->major_index_type());
    copy_buffer_into(hash_buffer, sizeof(int32_t) * 2, rule_->minor_index_type());
    copy_buffer_into(hash_buffer, sizeof(int32_t) * 3, rule_->min_price());
    copy_buffer_into(hash_buffer, sizeof(int32_t) * 4, rule_->max_price());
    copy_buffer_into(hash_buffer, sizeof(int32_t) * 5, static_cast<int32_t>(rule_->price_mode()));
    copy_buffer_into(hash_buffer, sizeof(int32_t) * 6, static_cast<int64_t>(rule_->bound_exclude().price()));
    copy_buffer_into(hash_buffer, (sizeof(int32_t) * 6) + sizeof(int64_t),
                     static_cast<int64_t>(rule_->bound_exclude().recommend_score()));
    copy_buffer_into(hash_buffer, (sizeof(int32_t) * 6) + (sizeof(int64_t) * 2),
                     rule_->bound_exclude().trade_order_ticket_id());

    result = XXH64(hash_buffer, sizeof(hash_buffer), result);
  }

  for (const auto &tag : rule_->item_tag()) {
    int32_t key = tag.label_type();
    if (tag.label_value_size() == 1) {
      char hash_buffer[sizeof(int32_t) * 2];
      copy_buffer_into(hash_buffer, 0, key);
      copy_buffer_into(hash_buffer, sizeof(int32_t), tag.label_value(0));

      result = XXH64(hash_buffer, sizeof(hash_buffer), result);
    } else if (tag.label_value_size() == 2) {
      char hash_buffer[sizeof(int32_t) * 3];
      copy_buffer_into(hash_buffer, 0, key);
      copy_buffer_into(hash_buffer, sizeof(int32_t), tag.label_value(0));
      copy_buffer_into(hash_buffer, sizeof(int32_t) * 2, tag.label_value(1));

      result = XXH64(hash_buffer, sizeof(hash_buffer), result);
    } else if (tag.label_value_size() == 3) {
      char hash_buffer[sizeof(int32_t) * 4];
      copy_buffer_into(hash_buffer, 0, key);
      copy_buffer_into(hash_buffer, sizeof(int32_t), tag.label_value(0));
      copy_buffer_into(hash_buffer, sizeof(int32_t) * 2, tag.label_value(1));
      copy_buffer_into(hash_buffer, sizeof(int32_t) * 3, tag.label_value(2));

      result = XXH64(hash_buffer, sizeof(hash_buffer), result);
    } else {
      result = XXH64(&key, sizeof(key), result);
      if (tag.label_value_size() > 0) {
        size_t unit_size = sizeof(*tag.label_value().data());
        size_t unit_count = static_cast<size_t>(tag.label_value_size());
        result = XXH64(tag.label_value().data(), unit_size * unit_count, result);
      }
    }
  }

  for (const auto &tag : rule_->exclude_item_tag()) {
    int32_t key = tag.label_type();
    if (tag.label_value_size() == 1) {
      char hash_buffer[sizeof(int32_t) * 2];
      copy_buffer_into(hash_buffer, 0, key);
      copy_buffer_into(hash_buffer, sizeof(int32_t), tag.label_value(0));

      result = XXH64(hash_buffer, sizeof(hash_buffer), result);
    } else if (tag.label_value_size() == 2) {
      char hash_buffer[sizeof(int32_t) * 3];
      copy_buffer_into(hash_buffer, 0, key);
      copy_buffer_into(hash_buffer, sizeof(int32_t), tag.label_value(0));
      copy_buffer_into(hash_buffer, sizeof(int32_t) * 2, tag.label_value(1));

      result = XXH64(hash_buffer, sizeof(hash_buffer), result);
    } else if (tag.label_value_size() == 3) {
      char hash_buffer[sizeof(int32_t) * 4];
      copy_buffer_into(hash_buffer, 0, key);
      copy_buffer_into(hash_buffer, sizeof(int32_t), tag.label_value(0));
      copy_buffer_into(hash_buffer, sizeof(int32_t) * 2, tag.label_value(1));
      copy_buffer_into(hash_buffer, sizeof(int32_t) * 3, tag.label_value(2));

      result = XXH64(hash_buffer, sizeof(hash_buffer), result);
    } else {
      result = XXH64(&key, sizeof(key), result);
      if (tag.label_value_size() > 0) {
        size_t unit_size = sizeof(*tag.label_value().data());
        size_t unit_count = static_cast<size_t>(tag.label_value_size());
        result = XXH64(tag.label_value().data(), unit_size * unit_count, result);
      }
    }
  }

  for (const auto &attribute : rule_->item_attribute()) {
    char hash_buffer[sizeof(int32_t) * 3];
    copy_buffer_into(hash_buffer, 0, attribute.attribute_type());
    copy_buffer_into(hash_buffer, sizeof(int32_t), attribute.min_value());
    copy_buffer_into(hash_buffer, sizeof(int32_t) * 2, attribute.max_value());

    result = XXH64(hash_buffer, sizeof(hash_buffer), result);
  }

  const_cast<product_label_view_key *>(this)->hash_code_ = static_cast<size_t>(result);
  return hash_code_;
}

TRADE_SDK_COMMON_API size_t product_label_view_key::hash_code_ignore_bound_value() const noexcept {
  if (hash_code_ignore_bound_value_ != 0) {
    return hash_code_ignore_bound_value_;
  }

  XXH64_hash_t result = 900000041;
  if (!rule_) {
    return static_cast<size_t>(result);
  }

  {
    char hash_buffer[(sizeof(int32_t) * 6) + (sizeof(int64_t) * 3)];
    copy_buffer_into(hash_buffer, 0, rule_->trade_market_id());
    copy_buffer_into(hash_buffer, sizeof(int32_t), rule_->major_index_type());
    copy_buffer_into(hash_buffer, sizeof(int32_t) * 2, rule_->minor_index_type());
    copy_buffer_into(hash_buffer, sizeof(int32_t) * 3, rule_->min_price());
    copy_buffer_into(hash_buffer, sizeof(int32_t) * 4, rule_->max_price());
    copy_buffer_into(hash_buffer, sizeof(int32_t) * 5, static_cast<int32_t>(rule_->price_mode()));
    copy_buffer_into(hash_buffer, sizeof(int32_t) * 6, 0);
    copy_buffer_into(hash_buffer, (sizeof(int32_t) * 6) + sizeof(int64_t), 0);
    copy_buffer_into(hash_buffer, (sizeof(int32_t) * 6) + (sizeof(int64_t) * 2), 0);

    result = XXH64(hash_buffer, sizeof(hash_buffer), result);
  }

  for (const auto &tag : rule_->item_tag()) {
    int32_t key = tag.label_type();
    if (tag.label_value_size() == 1) {
      char hash_buffer[sizeof(int32_t) * 2];
      copy_buffer_into(hash_buffer, 0, key);
      copy_buffer_into(hash_buffer, sizeof(int32_t), tag.label_value(0));

      result = XXH64(hash_buffer, sizeof(hash_buffer), result);
    } else if (tag.label_value_size() == 2) {
      char hash_buffer[sizeof(int32_t) * 3];
      copy_buffer_into(hash_buffer, 0, key);
      copy_buffer_into(hash_buffer, sizeof(int32_t), tag.label_value(0));
      copy_buffer_into(hash_buffer, sizeof(int32_t) * 2, tag.label_value(1));

      result = XXH64(hash_buffer, sizeof(hash_buffer), result);
    } else if (tag.label_value_size() == 3) {
      char hash_buffer[sizeof(int32_t) * 4];
      copy_buffer_into(hash_buffer, 0, key);
      copy_buffer_into(hash_buffer, sizeof(int32_t), tag.label_value(0));
      copy_buffer_into(hash_buffer, sizeof(int32_t) * 2, tag.label_value(1));
      copy_buffer_into(hash_buffer, sizeof(int32_t) * 3, tag.label_value(2));

      result = XXH64(hash_buffer, sizeof(hash_buffer), result);
    } else {
      result = XXH64(&key, sizeof(key), result);
      if (tag.label_value_size() > 0) {
        size_t unit_size = sizeof(*tag.label_value().data());
        size_t unit_count = static_cast<size_t>(tag.label_value_size());
        result = XXH64(tag.label_value().data(), unit_size * unit_count, result);
      }
    }
  }

  for (const auto &tag : rule_->exclude_item_tag()) {
    int32_t key = tag.label_type();
    if (tag.label_value_size() == 1) {
      char hash_buffer[sizeof(int32_t) * 2];
      copy_buffer_into(hash_buffer, 0, key);
      copy_buffer_into(hash_buffer, sizeof(int32_t), tag.label_value(0));

      result = XXH64(hash_buffer, sizeof(hash_buffer), result);
    } else if (tag.label_value_size() == 2) {
      char hash_buffer[sizeof(int32_t) * 3];
      copy_buffer_into(hash_buffer, 0, key);
      copy_buffer_into(hash_buffer, sizeof(int32_t), tag.label_value(0));
      copy_buffer_into(hash_buffer, sizeof(int32_t) * 2, tag.label_value(1));

      result = XXH64(hash_buffer, sizeof(hash_buffer), result);
    } else if (tag.label_value_size() == 3) {
      char hash_buffer[sizeof(int32_t) * 4];
      copy_buffer_into(hash_buffer, 0, key);
      copy_buffer_into(hash_buffer, sizeof(int32_t), tag.label_value(0));
      copy_buffer_into(hash_buffer, sizeof(int32_t) * 2, tag.label_value(1));
      copy_buffer_into(hash_buffer, sizeof(int32_t) * 3, tag.label_value(2));

      result = XXH64(hash_buffer, sizeof(hash_buffer), result);
    } else {
      result = XXH64(&key, sizeof(key), result);
      if (tag.label_value_size() > 0) {
        size_t unit_size = sizeof(*tag.label_value().data());
        size_t unit_count = static_cast<size_t>(tag.label_value_size());
        result = XXH64(tag.label_value().data(), unit_size * unit_count, result);
      }
    }
  }

  for (const auto &attribute : rule_->item_attribute()) {
    char hash_buffer[sizeof(int32_t) * 3];
    copy_buffer_into(hash_buffer, 0, attribute.attribute_type());
    copy_buffer_into(hash_buffer, sizeof(int32_t), attribute.min_value());
    copy_buffer_into(hash_buffer, sizeof(int32_t) * 2, attribute.max_value());

    result = XXH64(hash_buffer, sizeof(hash_buffer), result);
  }

  const_cast<product_label_view_key *>(this)->hash_code_ignore_bound_value_ = static_cast<size_t>(result);
  return hash_code_ignore_bound_value_;
}

TRADE_SDK_COMMON_API bool product_label_view_key::has_bound_exclude() const noexcept {
  if (!rule_) {
    return false;
  }
  return rule_->has_bound_exclude();
}

TRADE_SDK_COMMON_API bool product_label_view_key::is_page_start() const noexcept {
  if (!rule_) {
    return true;
  }

  if (rule_->min_price() > 0) {
    return false;
  }
  if (rule_->max_price() >= 0) {
    return false;
  }

  if (!rule_->has_bound_exclude()) {
    return true;
  }

  return rule_->bound_exclude().trade_order_ticket_id() == 0;
}

// 保留现有 noexcept 契约，内存分配失败时仍按原行为终止。
// NOLINTNEXTLINE(bugprone-exception-escape)
TRADE_SDK_COMMON_API product_label_view_key product_label_view_key::make_page_start() const noexcept {
  if (is_page_start()) {
    return product_label_view_key{*this};
  }

  if (rule_) {
    std::shared_ptr<atfw::trade::DTradeOrderTicketSearchRule> rule =
        atfw::memory::stl::make_shared<atfw::trade::DTradeOrderTicketSearchRule>(*rule_);
    rule->clear_bound_exclude();
    rule->set_min_price(0);
    rule->set_max_price(-1);
    return product_label_view_key{std::move(rule)};
  }

  atfw::trade::DTradeOrderTicketSearchRule empty;
  return product_label_view_key{empty};
}

TRADE_SDK_COMMON_API bool product_label_view_key::match(
    const atfw::trade::DTradeOrderTicketLabel &label) const noexcept {
  const atfw::trade::DTradeOrderTicketSearchRule &rule = get_search_rule();

  if (rule.major_index_type() != 0 && rule.major_index_type() != label.major_index_type()) {
    return false;
  }

  if (rule.minor_index_type() != 0 && rule.minor_index_type() != label.minor_index_type()) {
    return false;
  }

  // Tag 搜索
  for (int i = 0; i < rule.item_tag_size(); ++i) {
    const auto &tag_data = rule.item_tag(i);

    auto tag_iter =
        std::lower_bound(label.item_tag().begin(), label.item_tag().end(), tag_data.label_type(),
                         [](const atfw::trade::DTradeOrderTicketLabelTag &l, int32_t r) { return l.label_type() < r; });
    if (tag_iter == label.item_tag().end()) {
      return false;
    }
    if ((*tag_iter).label_type() != tag_data.label_type()) {
      return false;
    }

    // 如果 tag_data.values_size() == 0 || tag_iter->values_size()
    // 则视为搜索规则匹配任意该tag值,或该物品匹配任意搜索条件,即为成功。
    bool has_matched = tag_data.label_value_size() == 0 || tag_iter->label_value_size() == 0;
    for (int j = 0; !has_matched && j < tag_data.label_value_size(); ++j) {
      const auto &tag_values = (*tag_iter).label_value();
      // 空值表示允许所有
      if (tag_values.empty()) {
        break;
      }

      auto tag_value_iter = std::lower_bound(tag_values.begin(), tag_values.end(), tag_data.label_value(j));
      if (tag_value_iter == tag_values.end()) {
        continue;
      }
      if ((*tag_value_iter) == tag_data.label_value(j)) {
        has_matched = true;
      }
    }

    if (!has_matched) {
      return false;
    }
  }

  for (int i = 0; i < rule.exclude_item_tag_size(); ++i) {
    const auto &tag_data = rule.exclude_item_tag(i);

    auto tag_iter =
        std::lower_bound(label.item_tag().begin(), label.item_tag().end(), tag_data.label_type(),
                         [](const atfw::trade::DTradeOrderTicketLabelTag &l, int32_t r) { return l.label_type() < r; });
    if (tag_iter == label.item_tag().end()) {
      continue;
    }

    if ((*tag_iter).label_type() != tag_data.label_type()) {
      continue;
    }

    if (tag_data.label_value().empty()) {
      return false;
    }

    for (int j = 0; j < tag_data.label_value_size(); ++j) {
      const auto &tag_values = (*tag_iter).label_value();
      // 空值表示允许所有
      if (tag_values.empty()) {
        break;
      }

      auto tag_value_iter = std::lower_bound(tag_values.begin(), tag_values.end(), tag_data.label_value(j));
      if (tag_value_iter == tag_values.end()) {
        continue;
      }
      // 任意匹配则排除
      if ((*tag_value_iter) == tag_data.label_value(j)) {
        return false;
      }
    }
  }

  for (int i = 0; i < rule.item_attribute_size(); ++i) {
    const auto &attribute_rule_data = rule.item_attribute(i);

    auto tag_iter = std::lower_bound(
        label.item_attribute().begin(), label.item_attribute().end(), attribute_rule_data.attribute_type(),
        [](const atfw::trade::DTradeOrderTicketLabelAttribute &l, int32_t r) { return l.attribute_type() < r; });
    if (tag_iter == label.item_attribute().end()) {
      return false;
    }
    if ((*tag_iter).attribute_type() != attribute_rule_data.attribute_type()) {
      return false;
    }

    if (attribute_rule_data.min_value() >= 0 && attribute_rule_data.min_value() > (*tag_iter).attribute_value()) {
      return false;
    }

    if (attribute_rule_data.max_value() >= 0 && attribute_rule_data.max_value() < (*tag_iter).attribute_value()) {
      return false;
    }
  }

  return true;
}

TRADE_SDK_COMMON_API bool product_label_view_key::operator==(const product_label_view_key &other) const noexcept {
  return equal(*this, other);
}

TRADE_SDK_COMMON_API bool product_label_view_key::equal(const product_label_view_key &l,
                                                        const product_label_view_key &r, bool ignore_bound_exclude,
                                                        bool ignore_bound_value) noexcept {
  if (!ignore_bound_exclude && !ignore_bound_value && l.hash_code_ != r.hash_code_) {
    return false;
  }

  if (l.hash_code_ignore_bound_value_ != r.hash_code_ignore_bound_value_) {
    return false;
  }

  if (l.rule_ == r.rule_) {
    return true;
  }

  if (!l.rule_ || !r.rule_) {
    return false;
  }

  return equal(*l.rule_, *r.rule_, ignore_bound_exclude, ignore_bound_value);
}

TRADE_SDK_COMMON_API bool product_label_view_key::equal(const atfw::trade::DTradeOrderTicketSearchRule &l,
                                                        const atfw::trade::DTradeOrderTicketSearchRule &r,
                                                        bool ignore_bound_exclude, bool ignore_bound_value) noexcept {
  if (l.trade_market_id() != r.trade_market_id()) {
    return false;
  }

  if (l.major_index_type() != r.major_index_type()) {
    return false;
  }

  if (l.minor_index_type() != r.minor_index_type()) {
    return false;
  }

  if (l.min_price() != r.min_price()) {
    return false;
  }

  if (l.max_price() != r.max_price()) {
    return false;
  }

  if (l.price_mode() != r.price_mode()) {
    return false;
  }

  if (!ignore_bound_exclude) {
    if (l.has_bound_exclude() != r.has_bound_exclude()) {
      return false;
    }

    if (l.has_bound_exclude()) {
      if (!ignore_bound_value) {
        if (l.bound_exclude().price() != r.bound_exclude().price()) {
          return false;
        }

        if (l.bound_exclude().recommend_score() != r.bound_exclude().recommend_score()) {
          return false;
        }

        if (l.bound_exclude().trade_order_ticket_id() != r.bound_exclude().trade_order_ticket_id()) {
          return false;
        }
      }
    }
  }

  if (l.item_tag_size() != r.item_tag_size()) {
    return false;
  }

  if (l.exclude_item_tag_size() != r.exclude_item_tag_size()) {
    return false;
  }

  if (l.item_attribute_size() != r.item_attribute_size()) {
    return false;
  }

  for (int i = 0; i < l.item_tag_size(); ++i) {
    const auto &lt = l.item_tag(i);
    const auto &rt = r.item_tag(i);

    if (lt.label_type() != rt.label_type()) {
      return false;
    }

    if (lt.label_value_size() != rt.label_value_size()) {
      return false;
    }

    for (int j = 0; j < lt.label_value_size(); ++j) {
      if (lt.label_value(j) != rt.label_value(j)) {
        return false;
      }
    }
  }

  for (int i = 0; i < l.exclude_item_tag_size(); ++i) {
    const auto &lt = l.exclude_item_tag(i);
    const auto &rt = r.exclude_item_tag(i);

    if (lt.label_type() != rt.label_type()) {
      return false;
    }

    if (lt.label_value_size() != rt.label_value_size()) {
      return false;
    }

    for (int j = 0; j < lt.label_value_size(); ++j) {
      if (lt.label_value(j) != rt.label_value(j)) {
        return false;
      }
    }
  }

  for (int i = 0; i < l.item_attribute_size(); ++i) {
    const auto &lattr = l.item_attribute(i);
    const auto &rattr = r.item_attribute(i);

    if (lattr.attribute_type() != rattr.attribute_type()) {
      return false;
    }

    if (lattr.min_value() != rattr.min_value()) {
      return false;
    }

    if (lattr.max_value() != rattr.max_value()) {
      return false;
    }
  }

  return true;
}

TRADE_SDK_COMMON_API bool product_label_view_key::equal(const atfw::trade::DTradeOrderTicketLabel &l,
                                                        const atfw::trade::DTradeOrderTicketLabel &r) noexcept {
  if (l.major_index_type() != r.major_index_type()) {
    return false;
  }

  if (l.minor_index_type() != r.minor_index_type()) {
    return false;
  }

  if (l.item_tag_size() != r.item_tag_size()) {
    return false;
  }

  if (l.item_attribute_size() != r.item_attribute_size()) {
    return false;
  }

  for (int i = 0; i < l.item_tag_size(); ++i) {
    const auto &lt = l.item_tag(i);
    const auto &rt = r.item_tag(i);

    if (lt.label_type() != rt.label_type()) {
      return false;
    }

    if (lt.label_value_size() != rt.label_value_size()) {
      return false;
    }

    for (int j = 0; j < lt.label_value_size(); ++j) {
      if (lt.label_value(j) != rt.label_value(j)) {
        return false;
      }
    }
  }

  for (int i = 0; i < l.item_attribute_size(); ++i) {
    const auto &lattr = l.item_attribute(i);
    const auto &rattr = r.item_attribute(i);

    if (lattr.attribute_type() != rattr.attribute_type()) {
      return false;
    }

    if (lattr.attribute_value() != rattr.attribute_value()) {
      return false;
    }
  }

  return true;
}

TRADE_SDK_COMMON_API size_t
area_product_view_key::hash_type::operator()(const area_product_view_key &key) const noexcept {
  return key.hash_code();
}

TRADE_SDK_COMMON_API bool area_product_view_key::equal_type::operator()(const area_product_view_key &l,
                                                                        const area_product_view_key &r) const noexcept {
  if (l.hash_code_ != r.hash_code_) {
    return false;
  }

  if (l.hash_code_ignore_bound_value_ != r.hash_code_ignore_bound_value_) {
    return false;
  }

  if (l.area_code_ != r.area_code_) {
    return false;
  }

  if (l.order_type_ != r.order_type_) {
    return false;
  }

  return product_label_view_key::equal(l.label_view_, r.label_view_, false, false);
}

TRADE_SDK_COMMON_API size_t
area_product_view_key::ignore_bound_hash_type::operator()(const area_product_view_key &key) const noexcept {
  return key.hash_code_ignore_bound_value();
}

TRADE_SDK_COMMON_API bool area_product_view_key::ignore_bound_equal_type::operator()(
    const area_product_view_key &l, const area_product_view_key &r) const noexcept {
  // if (l.hash_code_ != r.hash_code_) {
  //   return false;
  // }

  if (l.hash_code_ignore_bound_value_ != r.hash_code_ignore_bound_value_) {
    return false;
  }

  if (l.area_code_ != r.area_code_) {
    return false;
  }

  if (l.order_type_ != r.order_type_) {
    return false;
  }

  return product_label_view_key::equal(l.label_view_, r.label_view_, true, true);
}

TRADE_SDK_COMMON_API area_product_view_key::area_product_view_key(
    int32_t area_code, atfw::trade::DTradeOrderTicketMeta::TypeCase order_type,
    const atfw::trade::DTradeOrderTicketSearchRule &origin)
    : area_code_(area_code), order_type_(order_type), label_view_(origin) {
  uint64_t hash_data[2] = {
      make_area_order_hash_key(area_code_, order_type_),
      static_cast<uint64_t>(label_view_.hash_code()),
  };
  hash_code_ = static_cast<size_t>(XXH64(&hash_data, sizeof(hash_data), 800600041));

  uint64_t ignore_bound_hash_data[2] = {
      make_area_order_hash_key(area_code_, order_type_),
      static_cast<uint64_t>(label_view_.hash_code_ignore_bound_value()),
  };
  hash_code_ignore_bound_value_ =
      static_cast<size_t>(XXH64(&ignore_bound_hash_data, sizeof(ignore_bound_hash_data), 800600041));
}

TRADE_SDK_COMMON_API area_product_view_key::area_product_view_key(
    int32_t area_code, atfw::trade::DTradeOrderTicketMeta::TypeCase order_type, product_label_view_key &&label_view)
    : area_code_(area_code), order_type_(order_type), label_view_(std::move(label_view)) {
  uint64_t hash_data[2] = {
      make_area_order_hash_key(area_code_, order_type_),
      static_cast<uint64_t>(label_view_.hash_code()),
  };
  hash_code_ = static_cast<size_t>(XXH64(&hash_data, sizeof(hash_data), 800600041));

  uint64_t ignore_bound_hash_data[2] = {
      make_area_order_hash_key(area_code_, order_type_),
      static_cast<uint64_t>(label_view_.hash_code_ignore_bound_value()),
  };
  hash_code_ignore_bound_value_ =
      static_cast<size_t>(XXH64(&ignore_bound_hash_data, sizeof(ignore_bound_hash_data), 800600041));
}

TRADE_SDK_COMMON_API area_product_view_key::~area_product_view_key() {}

TRADE_SDK_COMMON_API area_product_view_key::area_product_view_key(area_product_view_key &&other) noexcept
    : area_code_(other.area_code_),
      order_type_(other.order_type_),
      label_view_(std::move(other.label_view_)),
      hash_code_(other.hash_code_),
      hash_code_ignore_bound_value_(other.hash_code_ignore_bound_value_) {}

TRADE_SDK_COMMON_API area_product_view_key::area_product_view_key(const area_product_view_key &other)
    : area_code_(other.area_code_),
      order_type_(other.order_type_),
      label_view_(other.label_view_),
      hash_code_(other.hash_code_),
      hash_code_ignore_bound_value_(other.hash_code_ignore_bound_value_) {}

TRADE_SDK_COMMON_API area_product_view_key &area_product_view_key::operator=(
    const area_product_view_key &other) noexcept {
  area_code_ = other.area_code_;
  order_type_ = other.order_type_;
  label_view_ = other.label_view_;
  hash_code_ = other.hash_code_;
  hash_code_ignore_bound_value_ = other.hash_code_ignore_bound_value_;
  return *this;
}

TRADE_SDK_COMMON_API area_product_view_key &area_product_view_key::operator=(area_product_view_key &&other) noexcept {
  area_code_ = other.area_code_;
  order_type_ = other.order_type_;
  label_view_ = std::move(other.label_view_);
  hash_code_ = other.hash_code_;
  hash_code_ignore_bound_value_ = other.hash_code_ignore_bound_value_;
  return *this;
}

TRADE_SDK_COMMON_API size_t area_product_view_key::hash_code() const noexcept { return hash_code_; }
TRADE_SDK_COMMON_API size_t area_product_view_key::hash_code_ignore_bound_value() const noexcept {
  return hash_code_ignore_bound_value_;
}

TRADE_SDK_COMMON_API int32_t area_product_view_key::get_area_code() const noexcept { return area_code_; }

TRADE_SDK_COMMON_API atfw::trade::DTradeOrderTicketMeta::TypeCase area_product_view_key::get_order_type()
    const noexcept {
  return order_type_;
}

TRADE_SDK_COMMON_API const product_label_view_key &area_product_view_key::get_label_view() const noexcept {
  return label_view_;
}

TRADE_SDK_COMMON_API bool area_product_view_key::is_page_start() const noexcept { return label_view_.is_page_start(); }

TRADE_SDK_COMMON_API area_product_view_key area_product_view_key::make_page_start() const noexcept {
  return area_product_view_key{area_code_, order_type_, label_view_.make_page_start()};
}

TRADE_SDK_COMMON_API trade_order_ticket_price_index_key
area_product_view_key::make_price_bound_key(atfw::trade::EnTradePriceMode price_mode) const noexcept {
  return trade_order_ticket_price_index_key{price_mode, label_view_.get_search_rule().bound_exclude().price(),
                                            label_view_.get_search_rule().bound_exclude().trade_order_ticket_id()};
}

TRADE_SDK_COMMON_API trade_order_ticket_recommend_score_index_key
area_product_view_key::make_recommend_score_bound_key(atfw::trade::EnTradePriceMode price_mode) const noexcept {
  return {
      label_view_.get_search_rule().bound_exclude().recommend_score(),
      label_view_.get_search_rule().bound_exclude().price(),
      label_view_.get_search_rule().bound_exclude().trade_order_ticket_id(),
      label_view_.get_search_rule().bound_exclude().rarity(),
      price_mode,
  };
}

TRADE_SDK_COMMON_API bool area_product_view_key::operator==(const area_product_view_key &other) const noexcept {
  if (hash_code_ != other.hash_code_) {
    return false;
  }

  if (hash_code_ignore_bound_value_ != other.hash_code_ignore_bound_value_) {
    return false;
  }

  if (area_code_ != other.area_code_) {
    return false;
  }

  if (order_type_ != other.order_type_) {
    return false;
  }

  return label_view_ == other.label_view_;
}

const atfw::trade::DTradeOrderTicketSearchBound &area_product_view_key::get_bound_exclude() const noexcept {
  return label_view_.get_search_rule().bound_exclude();
}

}  // namespace trade_api
}  // namespace atframework
