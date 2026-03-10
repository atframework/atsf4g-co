#include "logic_rank_handle.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/common/com.struct.rank.common.pb.h>
#include <protocol/config/com.const.config.pb.h>
#include <protocol/config/com.struct.rank.config.pb.h>
#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <algorithm/murmur_hash.h>
#include <common/string_oprs.h>
#include <log/log_wrapper.h>
#include <time/time_utility.h>

#include <config/logic_config.h>

#include <utility/protobuf_mini_dumper.h>

#include "logic_rank_algorithm.h"
#include "rpc/rank/rank.h"

RANK_SDK_API logic_rank_handle_key::logic_rank_handle_key(const PROJECT_NAMESPACE_ID::config::ExcelRankRule& rule)
    : rank_type_(rule.rank_type()),
      instance_id_(rule.rank_instance_id()),
      sub_rank_type_(rule.content().sub_rank_type()),
      sub_instance_id_(rule.content().sub_rank_instance_id()) {}

RANK_SDK_API logic_rank_handle_key::logic_rank_handle_key(const PROJECT_NAMESPACE_ID::config::ExcelRankRule& rule,
                                                          uint32_t auto_reward_rank_type)
    : rank_type_(rule.rank_type()),
      instance_id_(rule.rank_instance_id()),
      sub_rank_type_(rule.content().sub_rank_type()),
      sub_instance_id_(rule.content().sub_rank_instance_id()) {
    rank_type_ = auto_reward_rank_type;
}

RANK_SDK_API logic_rank_handle_key::logic_rank_handle_key(uint32_t rank_type, uint32_t instance_id,
                                                          uint32_t sub_rank_type, uint32_t sub_instance_id)
    : rank_type_(rank_type),
      instance_id_(instance_id),
      sub_rank_type_(sub_rank_type),
      sub_instance_id_(sub_instance_id) {}

RANK_SDK_API bool operator==(const logic_rank_handle_key& l, const logic_rank_handle_key& r) noexcept {
  return l.get_rank_type() == r.get_rank_type() && l.get_instance_id() == r.get_instance_id() &&
         l.get_sub_rank_type() == r.get_sub_rank_type() && l.get_sub_instance_id() == r.get_sub_instance_id();
}

#if defined(__cpp_impl_three_way_comparison) && !defined(_MSC_VER)
RANK_SDK_API std::strong_ordering operator<=>(const logic_rank_handle_key& l, const logic_rank_handle_key& r) noexcept {
  if (l.get_rank_type() != r.get_rank_type()) {
    return l.get_rank_type() < r.get_rank_type() ? std::strong_ordering::less : std::strong_ordering::greater;
  }

  if (l.get_instance_id() != r.get_instance_id()) {
    return l.get_instance_id() < r.get_instance_id() ? std::strong_ordering::less : std::strong_ordering::greater;
  }

  if (l.get_sub_rank_type() != r.get_sub_rank_type()) {
    return l.get_sub_rank_type() < r.get_sub_rank_type() ? std::strong_ordering::less : std::strong_ordering::greater;
  }

  if (l.get_sub_rank_type() != r.get_sub_rank_type()) {
    return l.get_sub_instance_id() < r.get_sub_instance_id() ? std::strong_ordering::less
                                                             : std::strong_ordering::greater;
  }

  return std::strong_ordering::equal;
}

#else
RANK_SDK_API bool operator!=(const logic_rank_handle_key& l, const logic_rank_handle_key& r) noexcept {
  return !(l == r);
}

RANK_SDK_API bool operator<(const logic_rank_handle_key& l, const logic_rank_handle_key& r) noexcept {
  if (l.get_rank_type() != r.get_rank_type()) {
    return l.get_rank_type() < r.get_rank_type();
  }

  if (l.get_instance_id() != r.get_instance_id()) {
    return l.get_instance_id() < r.get_instance_id();
  }

  if (l.get_sub_rank_type() != r.get_sub_rank_type()) {
    return l.get_sub_rank_type() < r.get_sub_rank_type();
  }

  return l.get_sub_instance_id() < r.get_sub_instance_id();
}

RANK_SDK_API bool operator<=(const logic_rank_handle_key& l, const logic_rank_handle_key& r) noexcept {
  if (l.get_rank_type() != r.get_rank_type()) {
    return l.get_rank_type() < r.get_rank_type();
  }

  if (l.get_instance_id() != r.get_instance_id()) {
    return l.get_instance_id() < r.get_instance_id();
  }

  if (l.get_sub_rank_type() != r.get_sub_rank_type()) {
    return l.get_sub_rank_type() < r.get_sub_rank_type();
  }

  return l.get_sub_instance_id() <= r.get_sub_instance_id();
}

RANK_SDK_API bool operator>(const logic_rank_handle_key& l, const logic_rank_handle_key& r) noexcept {
  if (l.get_rank_type() != r.get_rank_type()) {
    return l.get_rank_type() > r.get_rank_type();
  }

  if (l.get_instance_id() != r.get_instance_id()) {
    return l.get_instance_id() > r.get_instance_id();
  }

  if (l.get_sub_rank_type() != r.get_sub_rank_type()) {
    return l.get_sub_rank_type() > r.get_sub_rank_type();
  }

  return l.get_sub_instance_id() > r.get_sub_instance_id();
}

RANK_SDK_API bool operator>=(const logic_rank_handle_key& l, const logic_rank_handle_key& r) noexcept {
  if (l.get_rank_type() != r.get_rank_type()) {
    return l.get_rank_type() > r.get_rank_type();
  }

  if (l.get_instance_id() != r.get_instance_id()) {
    return l.get_instance_id() > r.get_instance_id();
  }

  if (l.get_sub_rank_type() != r.get_sub_rank_type()) {
    return l.get_sub_rank_type() > r.get_sub_rank_type();
  }

  return l.get_sub_instance_id() >= r.get_sub_instance_id();
}
#endif

RANK_SDK_API size_t logic_rank_handle_key_hash::operator()(const logic_rank_handle_key& key) const noexcept {
#if (defined(__cplusplus) && __cplusplus >= 201402L) || ((defined(_MSVC_LANG) && _MSVC_LANG >= 201402L))
  static_assert(std::is_trivially_copyable<logic_rank_handle_key>::value, "logic_rank_handle_key check failed");
#endif

  uint64_t out[2];
  util::hash::murmur_hash3_x64_128(&key, sizeof(key), 0, out);
  return static_cast<size_t>(out[0]);
}

RANK_SDK_API logic_rank_handle_decl::logic_rank_handle_decl()
    : last_rank_cache_cursor_(nullptr), last_rank_cache_total_count_(0) {}

RANK_SDK_API logic_rank_handle_decl::~logic_rank_handle_decl() {}

RANK_SDK_API void logic_rank_handle_decl::reset_cursor_front() noexcept {
  if (last_rank_cache_.empty()) {
    last_rank_cache_cursor_ = nullptr;
    return;
  }

  last_rank_cache_cursor_ = &last_rank_cache_[0];
}

RANK_SDK_API void logic_rank_handle_decl::reset_cursor_back() noexcept {
  if (last_rank_cache_.empty()) {
    last_rank_cache_cursor_ = nullptr;
    return;
  }

  last_rank_cache_cursor_ = &last_rank_cache_[last_rank_cache_.size() - 1];
}

RANK_SDK_API bool logic_rank_handle_decl::next_cursor() noexcept {
  if (last_rank_cache_.empty()) {
    return false;
  }

  if (last_rank_cache_cursor_ == nullptr || last_rank_cache_cursor_ < &last_rank_cache_[0]) {
    last_rank_cache_cursor_ = &last_rank_cache_[0];
    return true;
  } else {
    size_t offset = static_cast<size_t>(last_rank_cache_cursor_ - &last_rank_cache_[0]);
    if (offset >= last_rank_cache_.size()) {
      return false;
    }
    ++last_rank_cache_cursor_;
    return true;
  }
}

RANK_SDK_API bool logic_rank_handle_decl::previous_cursor() noexcept {
  if (last_rank_cache_.empty()) {
    return false;
  }

  if (last_rank_cache_cursor_ == nullptr || last_rank_cache_cursor_ > &last_rank_cache_[last_rank_cache_.size() - 1]) {
    last_rank_cache_cursor_ = &last_rank_cache_[last_rank_cache_.size() - 1];
    return true;
  } else {
    if (last_rank_cache_cursor_ < &last_rank_cache_[0]) {
      return false;
    }
    --last_rank_cache_cursor_;
    return true;
  }
}

RANK_SDK_API bool logic_rank_handle_decl::valid_cursor() const noexcept {
  if (last_rank_cache_.empty()) {
    return false;
  }

  if (nullptr == last_rank_cache_cursor_) {
    return false;
  }

  return last_rank_cache_cursor_ >= &last_rank_cache_[0] &&
         last_rank_cache_cursor_ < &last_rank_cache_[0] + last_rank_cache_.size();
}

RANK_SDK_API gsl::string_view logic_rank_handle_decl::get_current_open_id() const noexcept {
  if (!valid_cursor()) {
    return gsl::string_view{};
  }

  return last_rank_cache_cursor_->open_id;
}

RANK_SDK_API uint32_t logic_rank_handle_decl::get_current_user_zone_id() const noexcept {
  if (!valid_cursor()) {
    return 0;
  }

  return static_cast<uint32_t>(last_rank_cache_cursor_->zone_id);
}

RANK_SDK_API uint32_t logic_rank_handle_decl::get_current_no() const noexcept {
  if (!valid_cursor()) {
    return 0;
  }

  return last_rank_cache_cursor_->rank_no;
}

RANK_SDK_API uint32_t logic_rank_handle_decl::get_current_score() const noexcept {
  if (!valid_cursor()) {
    return 0;
  }

  return last_rank_cache_cursor_->score;
}

RANK_SDK_API int64_t logic_rank_handle_decl::get_current_timestamp() const noexcept {
  if (!valid_cursor()) {
    return 0;
  }

  return last_rank_cache_cursor_->timestamp;
}

RANK_SDK_API uint32_t logic_rank_handle_decl::get_current_total_count() const noexcept {
  return last_rank_cache_total_count_;
}

RANK_SDK_API uint32_t logic_rank_handle_decl::get_current_count() const noexcept {
  return static_cast<uint32_t>(last_rank_cache_.size());
}

RANK_SDK_API const logic_rank_handle_data* logic_rank_handle_decl::get_current_cursor() const noexcept {
  if (!valid_cursor()) {
    return nullptr;
  }

  return last_rank_cache_cursor_;
}

RANK_SDK_API gsl::span<logic_rank_handle_data> logic_rank_handle_decl::get_current_span() noexcept {
  return gsl::span<logic_rank_handle_data>{last_rank_cache_};
}

RANK_SDK_API gsl::span<const logic_rank_handle_data> logic_rank_handle_decl::get_current_span() const noexcept {
  return gsl::span<const logic_rank_handle_data>{last_rank_cache_};
}

void logic_rank_handle_decl::fetch_current_rank_key(gsl::string_view& openid, uint32_t zone_id,
          PROJECT_NAMESPACE_ID::DRankUserKey& rank_user_key) const {
  // PROJECT_NAMESPACE_ID::DRankUserKey rank_user_key;

  uint64_t user_id = 0;
  int32_t instance_key_type = 0;
  int64_t instance_key_id = 0;
  std::tie(zone_id, user_id, instance_key_type, instance_key_id) = rank_openid_to_user_key(openid);
  rank_user_key.set_user_id(user_id);
  rank_user_key.set_zone_id(zone_id);
  auto instance_rank_key = rank_user_key.mutable_rank_instance_key();
  instance_rank_key->set_instance_type(instance_key_type);
  instance_rank_key->set_instance_id(instance_key_id);
}


void logic_rank_handle_decl::reinit_last_cache(uint32_t total_count) {
  last_rank_cache_total_count_ = total_count;

  std::sort(last_rank_cache_.begin(), last_rank_cache_.end(),
            [](const logic_rank_handle_data& l, const logic_rank_handle_data& r) { return l.rank_no < r.rank_no; });

  if (last_rank_cache_.empty()) {
    last_rank_cache_cursor_ = nullptr;
  } else {
    last_rank_cache_cursor_ = &last_rank_cache_[0];
  }
}

static void logic_rank_handle_self_impl_dump_rank_data(logic_rank_handle_data& to,
                                                       const PROJECT_NAMESPACE_ID::DRankUserBoardData& from) {
  // to.open_id = from.stUserRankInfo.stUserInfo.szOpenId;
  to.zone_id = from.user_key().zone_id();
  to.user_id = from.user_key().user_id();
  to.instance_type = from.user_key().rank_instance_key().instance_type();
  to.instance_id = from.user_key().rank_instance_key().instance_id();

  to.rank_no = from.rank_no();
  to.score = static_cast<uint32_t>(from.score());
  to.timestamp = static_cast<int64_t>(from.submit_timepoint());

  int32_t extend_sz = from.custom_data().sort_fields().size();
  if (extend_sz >= 5) {
    extend_sz = 5;
  }

  for (int32_t i = 0; i < extend_sz; ++i) {
    to.extend_data.sort_fields[0] = static_cast<uint32_t>(from.custom_data().sort_fields().at(i));
  }
}

static void fetch_self_rank_custom_data(ATFW_EXPLICIT_UNUSED_ATTR const rank_callback_private_data& callback_data,
                                        logic_rank_user_extend_span user_extend_data,
                                        PROJECT_NAMESPACE_ID::DRankCustomData& custom_data) {
  for (uint32_t i = 0; i < 5; ++i) {
    custom_data.mutable_sort_fields()->Add(static_cast<int64_t>(user_extend_data.sort_fields[i]));
  }
}

RANK_SDK_API logic_rank_handle_self_impl::logic_rank_handle_self_impl(uint32_t world_id, uint32_t zone_id)
    : world_id_(world_id), zone_id_(zone_id) {}

RANK_SDK_API logic_rank_handle_self_impl::~logic_rank_handle_self_impl() {}

RANK_SDK_API uint32_t logic_rank_handle_self_impl::get_world_id() const noexcept { return world_id_; }

RANK_SDK_API uint32_t logic_rank_handle_self_impl::get_zone_id() const noexcept { return zone_id_; }

RANK_SDK_API gsl::span<const uint32_t> logic_rank_handle_self_impl::get_current_sort_fields() const noexcept {
  const logic_rank_handle_data* cursor = get_current_cursor();
  if (nullptr == cursor) {
    return {};
  }

  const auto& sort_fields = cursor->extend_data.sort_fields;

  return gsl::span<const uint32_t>{sort_fields};
}

RANK_SDK_API gsl::span<const uint64_t> logic_rank_handle_self_impl::get_current_ext_fields() const noexcept {
  const logic_rank_handle_data* cursor = get_current_cursor();
  if (nullptr == cursor) {
    return {};
  }

  const auto& ext_fields = cursor->extend_data.ext_fields;

  return gsl::span<const uint64_t>{ext_fields};
}

RANK_SDK_API bool logic_rank_handle_self_impl::is_service_available() const noexcept { return true; }

RANK_SDK_API rpc::rpc_result<rank_ret_t> logic_rank_handle_self_impl::get_top_rank(
    rpc::context& ctx, const logic_rank_handle_key& key, uint32_t start, uint32_t count,
    PROJECT_NAMESPACE_ID::DRankImageData* image) {
  last_rank_cache_.resize(0);
  reinit_last_cache(0);

  rpc::context::message_holder<PROJECT_NAMESPACE_ID::DRankKey> rank{ctx};
  rank->set_rank_type(key.get_rank_type());
  rank->set_rank_instance_id(key.get_instance_id());
  rank->set_sub_rank_type(key.get_sub_rank_type());
  rank->set_sub_rank_instance_id(key.get_sub_instance_id());

  PROJECT_NAMESPACE_ID::DRankQueryRspData output;

  int32_t ret = 0;

  if (image != nullptr && image->mirror_id() != 0) {
    ret = RPC_AWAIT_TYPE_RESULT(
        rpc::rank::get_top_from_mirror(ctx, *rank, zone_id_, start, count, image->mirror_id(), output));
  } else {
    ret = RPC_AWAIT_TYPE_RESULT(rpc::rank::get_top(ctx, *rank, start, count, output));
  }

  if (ret != 0) {
    FWLOGERROR("get_top_rank {},{},{},{} for {}-{} failed, ret: {}({})", world_id_, zone_id_, key.get_rank_type(),
               key.get_instance_id(), start, start + count, ret, protobuf_mini_dumper_get_error_msg(ret));
  } else {
    uint32_t sz = static_cast<uint32_t>(output.rank_records().size());
    last_rank_cache_.resize(sz);
    for (uint32_t i = 0; i < sz; ++i) {
      logic_rank_handle_self_impl_dump_rank_data(last_rank_cache_[i],
                                                 output.rank_records().at(static_cast<int32_t>(i)));
    }
    reinit_last_cache(output.rank_total_count());
  }

  RPC_RETURN_TYPE(rank_ret_t(ret));
}

RANK_SDK_API rpc::rpc_result<rank_ret_t> logic_rank_handle_self_impl::upload_score(
    rpc::context& ctx, const logic_rank_handle_key& key, gsl::string_view openid, uint32_t score,
    const rank_callback_private_data& callback_data, logic_rank_user_extend_span user_extend_data) {
  rpc::context::message_holder<PROJECT_NAMESPACE_ID::DRankUserKey> user{ctx};

  fetch_current_rank_key(openid, zone_id_, *user);

  rpc::context::message_holder<PROJECT_NAMESPACE_ID::DRankKey> rank{ctx};
  rank->set_rank_type(key.get_rank_type());
  rank->set_rank_instance_id(key.get_instance_id());
  rank->set_sub_rank_type(key.get_sub_rank_type());
  rank->set_sub_rank_instance_id(key.get_sub_instance_id());

  rpc::context::message_holder<PROJECT_NAMESPACE_ID::DRankCustomData> custom_data{ctx};
  fetch_self_rank_custom_data(callback_data, user_extend_data, *custom_data);

  auto ret = RPC_AWAIT_TYPE_RESULT(rpc::rank::update_score(ctx, *user, *rank, score, *custom_data));
  if (ret != 0) {
    FWLOGERROR("upload_score {},{},{},{} for {} failed, res: {}({})", world_id_, zone_id_,
               key.get_rank_type(), key.get_instance_id(), openid, ret, protobuf_mini_dumper_get_error_msg(ret));
  }

  RPC_RETURN_TYPE(rank_ret_t(ret));
}

RANK_SDK_API rpc::rpc_result<rank_ret_t> logic_rank_handle_self_impl::clear_all(
    rpc::context& ctx, const logic_rank_handle_key& key, PROJECT_NAMESPACE_ID::DRankImageData* /*image*/) {
  rpc::context::message_holder<PROJECT_NAMESPACE_ID::DRankKey> rank{ctx};
  rank->set_rank_type(key.get_rank_type());
  rank->set_rank_instance_id(key.get_instance_id());
  rank->set_sub_rank_type(key.get_sub_rank_type());
  rank->set_sub_rank_instance_id(key.get_sub_instance_id());

  auto ret = RPC_AWAIT_TYPE_RESULT(rpc::rank::clear_rank(ctx, *rank));
  if (ret != 0) {
    FWLOGERROR("clear_all {},{},{},{} failed, res: {}({})", world_id_, zone_id_, key.get_rank_type(),
               key.get_instance_id(), ret, protobuf_mini_dumper_get_error_msg(ret));
  }

  RPC_RETURN_TYPE(rank_ret_t(ret));
}

RANK_SDK_API rpc::rpc_result<rank_ret_t> logic_rank_handle_self_impl::clear_special_one(
    rpc::context& ctx, const logic_rank_handle_key& key, gsl::string_view openid,
    PROJECT_NAMESPACE_ID::DRankImageData* /*image*/) {
  rpc::context::message_holder<PROJECT_NAMESPACE_ID::DRankUserKey> user_rank_key{ctx};
  fetch_current_rank_key(openid, zone_id_, *user_rank_key);

  rpc::context::message_holder<PROJECT_NAMESPACE_ID::DRankKey> rank{ctx};
  rank->set_rank_type(key.get_rank_type());
  rank->set_rank_instance_id(key.get_instance_id());
  rank->set_sub_rank_type(key.get_sub_rank_type());
  rank->set_sub_rank_instance_id(key.get_sub_instance_id());

  int32_t ret = RPC_AWAIT_TYPE_RESULT(rpc::rank::remove_one(ctx, *user_rank_key, *rank));

  if (ret != 0) {
    FWLOGERROR("clear_special_one {},{},{},{} for {} failed, res: {}({})", world_id_, zone_id_, key.get_rank_type(),
               key.get_instance_id(), openid, ret, protobuf_mini_dumper_get_error_msg(ret));
  }

  RPC_RETURN_TYPE(rank_ret_t(ret));
}

RANK_SDK_API rpc::rpc_result<rank_ret_t> logic_rank_handle_self_impl::increase_score(
    rpc::context& ctx, const logic_rank_handle_key& key, gsl::string_view openid, uint32_t score,
    const rank_callback_private_data& callback_data, logic_rank_user_extend_span user_extend_data) {
  rpc::context::message_holder<PROJECT_NAMESPACE_ID::DRankUserKey> user_rank_key{ctx};
  fetch_current_rank_key(openid, zone_id_, *user_rank_key);

  rpc::context::message_holder<PROJECT_NAMESPACE_ID::DRankKey> rank{ctx};
  rank->set_rank_type(key.get_rank_type());
  rank->set_rank_instance_id(key.get_instance_id());
  rank->set_sub_rank_type(key.get_sub_rank_type());
  rank->set_sub_rank_instance_id(key.get_sub_instance_id());

  rpc::context::message_holder<PROJECT_NAMESPACE_ID::DRankCustomData> custom_data{ctx};
  fetch_self_rank_custom_data(callback_data, user_extend_data, *custom_data);
  int32_t ret = RPC_AWAIT_CODE_RESULT(rpc::rank::modify_score(ctx, *user_rank_key, *rank, score, *custom_data));
  if (ret != 0) {
    FWLOGERROR("increase_score {},{},{},{} for {} failed, res: {}({})", world_id_, zone_id_, key.get_rank_type(),
               key.get_instance_id(), openid, ret, protobuf_mini_dumper_get_error_msg(ret));
  }
  RPC_RETURN_TYPE(rank_ret_t(ret));
}

RANK_SDK_API rpc::rpc_result<rank_ret_t> logic_rank_handle_self_impl::decrease_score(
    rpc::context& ctx, const logic_rank_handle_key& key, gsl::string_view openid, uint32_t score,
    const rank_callback_private_data& callback_data, logic_rank_user_extend_span user_extend_data) {
  rpc::context::message_holder<PROJECT_NAMESPACE_ID::DRankUserKey> user_rank_key{ctx};
  fetch_current_rank_key(openid, zone_id_, *user_rank_key);

  rpc::context::message_holder<PROJECT_NAMESPACE_ID::DRankKey> rank{ctx};
  rank->set_rank_type(key.get_rank_type());
  rank->set_rank_instance_id(key.get_instance_id());
  rank->set_sub_rank_type(key.get_sub_rank_type());
  rank->set_sub_rank_instance_id(key.get_sub_instance_id());

  rpc::context::message_holder<PROJECT_NAMESPACE_ID::DRankCustomData> custom_data{ctx};
  fetch_self_rank_custom_data(callback_data, user_extend_data, *custom_data);
  int32_t ret = RPC_AWAIT_CODE_RESULT(rpc::rank::modify_score(ctx, *user_rank_key, *rank, score, *custom_data));
  if (ret != 0) {
    FWLOGERROR("increase_score {},{},{},{} for {} failed, res: {}({})", world_id_, zone_id_, key.get_rank_type(),
               key.get_instance_id(), openid, ret, protobuf_mini_dumper_get_error_msg(ret));
  }
  RPC_RETURN_TYPE(rank_ret_t(ret));
}

RANK_SDK_API rpc::rpc_result<rank_ret_t> logic_rank_handle_self_impl::get_special_one(
    rpc::context& ctx, logic_rank_handle_data& output, const logic_rank_handle_key& key, gsl::string_view openid,
    uint32_t /*up_count*/, uint32_t /*down_count*/, PROJECT_NAMESPACE_ID::DRankImageData* /*image*/) {
  rpc::context::message_holder<PROJECT_NAMESPACE_ID::DRankUserKey> user_rank_key{ctx};
  fetch_current_rank_key(openid, zone_id_, *user_rank_key);

  rpc::context::message_holder<PROJECT_NAMESPACE_ID::DRankKey> rank{ctx};
  rank->set_rank_type(key.get_rank_type());
  rank->set_rank_instance_id(key.get_instance_id());

  rpc::context::message_holder<PROJECT_NAMESPACE_ID::DRankUserBoardData> board_data{ctx};

  auto ret = RPC_AWAIT_TYPE_RESULT(rpc::rank::get_special_one(ctx, *user_rank_key, *rank, *board_data));
  if (ret) {
    RPC_RETURN_TYPE(rank_ret_t(ret));
  }

  logic_rank_handle_self_impl_dump_rank_data(output, *board_data);

  reinit_last_cache(0);
  RPC_RETURN_TYPE(rank_ret_t(ret));
}

RANK_SDK_API rpc::rpc_result<rank_ret_t> logic_rank_handle_self_impl::get_special_score(
    rpc::context& /*ctx*/, logic_rank_handle_data& /*output*/, const logic_rank_handle_key& /*key*/, uint32_t /*score*/,
    uint32_t /*up_count*/, uint32_t /*down_count*/, PROJECT_NAMESPACE_ID::DRankImageData* /*image*/) {
  RPC_RETURN_TYPE(rank_ret_t(PROJECT_NAMESPACE_ID::EN_ERR_RANK_TYPE_OPERATE_INVALID));
}

RANK_SDK_API bool logic_rank_handle_self_impl::is_current(const PROJECT_NAMESPACE_ID::config::ExcelRankRule& /*cfg*/,
                                                          time_t /*now*/,
                                                          const logic_rank_handle_key& /*key*/) const noexcept {
  return true;
}

RANK_SDK_API logic_rank_handle_variant::logic_rank_handle_variant(
    uint32_t world_id, uint32_t zone_id, const PROJECT_NAMESPACE_ID::config::ExcelRankRule& rule)
    : delegate_(reinterpret_cast<logic_rank_handle_decl*>(object_data_)) {
  init_delegate(world_id, zone_id);
  enable_image_ = rule.content().settlement_type() == PROJECT_NAMESPACE_ID::EN_RANK_SETTLEMENT_TYPE_IMAGE;
}

RANK_SDK_API logic_rank_handle_variant::logic_rank_handle_variant(const logic_rank_handle_variant& other)
    : variant_type_(other.variant_type_),
      enable_image_(other.enable_image_),
      delegate_(reinterpret_cast<logic_rank_handle_decl*>(object_data_)) {
  init_delegate(other.get_world_id(), other.get_zone_id());
}

RANK_SDK_API logic_rank_handle_variant& logic_rank_handle_variant::operator=(const logic_rank_handle_variant& other) {
  destructor_delegate();
  variant_type_ = other.variant_type_;
  enable_image_ = other.enable_image_;

  init_delegate(other.get_world_id(), other.get_zone_id());

  return *this;
}

RANK_SDK_API logic_rank_handle_variant::logic_rank_handle_variant(logic_rank_handle_variant&& other)
    : variant_type_(other.variant_type_),
      enable_image_(other.enable_image_),
      delegate_(reinterpret_cast<logic_rank_handle_decl*>(object_data_)) {
  init_delegate(other.get_world_id(), other.get_zone_id());
}

RANK_SDK_API logic_rank_handle_variant& logic_rank_handle_variant::operator=(logic_rank_handle_variant&& other) {
  init_delegate(other.get_world_id(), other.get_zone_id());

  variant_type_ = other.variant_type_;
  enable_image_ = other.enable_image_;
  destructor_delegate();
  return *this;
}

RANK_SDK_API logic_rank_handle_variant::~logic_rank_handle_variant() { destructor_delegate(); }

void logic_rank_handle_variant::init_delegate(uint32_t world_id, uint32_t zone_id) {
  switch (variant_type_) {
    case variant_type::KSelfRank: {
      delegate_ = gsl::not_null<logic_rank_handle_decl*>{new ((void*)object_data_)
                                                             logic_rank_handle_self_impl(world_id, zone_id)};
      break;
    }
    default: {
      break;
    }
  }
}

void logic_rank_handle_variant::destructor_delegate() {
  switch (variant_type_) {
    case variant_type::KSelfRank: {
      static_cast<logic_rank_handle_self_impl*>(&(*delegate_))->~logic_rank_handle_self_impl();
      break;
    }
    default: {
      break;
    }
  }
}

RANK_SDK_API uint32_t logic_rank_handle_variant::get_world_id() const noexcept { return delegate_->get_world_id(); }

RANK_SDK_API uint32_t logic_rank_handle_variant::get_zone_id() const noexcept { return delegate_->get_zone_id(); }

RANK_SDK_API gsl::span<const uint32_t> logic_rank_handle_variant::get_current_sort_fields() const noexcept {
  return delegate_->get_current_sort_fields();
}

RANK_SDK_API gsl::span<const uint64_t> logic_rank_handle_variant::get_current_ext_fields() const noexcept {
  return delegate_->get_current_ext_fields();
}

RANK_SDK_API bool logic_rank_handle_variant::is_service_available() const noexcept {
  return delegate_->is_service_available();
}

RANK_SDK_API bool logic_rank_handle_variant::is_current(const PROJECT_NAMESPACE_ID::config::ExcelRankRule& cfg,
                                                        time_t now, const logic_rank_handle_key& key) const noexcept {
  return delegate_->is_current(cfg, now, key);
}

RANK_SDK_API rpc::rpc_result<rank_ret_t> logic_rank_handle_variant::get_top_rank(
    rpc::context& ctx, const logic_rank_handle_key& key, uint32_t start, uint32_t count,
    PROJECT_NAMESPACE_ID::DRankImageData* image) {
  return delegate_->get_top_rank(ctx, key, start, count, image);
}

RANK_SDK_API rpc::rpc_result<rank_ret_t> logic_rank_handle_variant::upload_score(
    rpc::context& ctx, const logic_rank_handle_key& key, gsl::string_view openid, uint32_t score,
    const rank_callback_private_data& callback_data, logic_rank_user_extend_span user_extend_data) {
  return delegate_->upload_score(ctx, key, openid, score, callback_data, user_extend_data);
}

RANK_SDK_API rpc::rpc_result<rank_ret_t> logic_rank_handle_variant::clear_all(
    rpc::context& ctx, const logic_rank_handle_key& key, PROJECT_NAMESPACE_ID::DRankImageData* image) {
  return delegate_->clear_all(ctx, key, image);
}

RANK_SDK_API rpc::rpc_result<rank_ret_t> logic_rank_handle_variant::clear_special_one(
    rpc::context& ctx, const logic_rank_handle_key& key, gsl::string_view openid) {
  return delegate_->clear_special_one(ctx, key, openid);
}

RANK_SDK_API rpc::rpc_result<rank_ret_t> logic_rank_handle_variant::increase_score(
    rpc::context& ctx, const logic_rank_handle_key& key, gsl::string_view openid, uint32_t score,
    const rank_callback_private_data& callback_data, logic_rank_user_extend_span user_extend_data) {
  return delegate_->increase_score(ctx, key, openid, score, callback_data, user_extend_data);
}

RANK_SDK_API rpc::rpc_result<rank_ret_t> logic_rank_handle_variant::decrease_score(
    rpc::context& ctx, const logic_rank_handle_key& key, gsl::string_view openid, uint32_t score,
    const rank_callback_private_data& callback_data, logic_rank_user_extend_span user_extend_data) {
  return delegate_->decrease_score(ctx, key, openid, score, callback_data, user_extend_data);
}

RANK_SDK_API rpc::rpc_result<rank_ret_t> logic_rank_handle_variant::get_special_one(
    rpc::context& ctx, logic_rank_handle_data& output, const logic_rank_handle_key& key, gsl::string_view openid,
    uint32_t up_count, uint32_t down_count, PROJECT_NAMESPACE_ID::DRankImageData* image) {
  RPC_RETURN_TYPE(
      RPC_AWAIT_CODE_RESULT(delegate_->get_special_one(ctx, output, key, openid, up_count, down_count, image)));
}


std::string rank_user_key_to_openid(uint32_t user_zone_id, uint64_t user_id, int32_t instance_type, int64_t instance_id) {
  return util::log::format("{}:{}:{}:{}", user_zone_id, user_id, instance_type, instance_id);
}

std::string rank_user_key_to_openid(uint32_t user_zone_id, uint64_t user_id, PROJECT_NAMESPACE_ID::DRankInstanceKey instance_key) {
  return util::log::format("{}:{}:{}:{}", user_zone_id, user_id, instance_key.instance_type(), instance_key.instance_id());
}

std::tuple<uint32_t, uint64_t, int32_t, int64_t> rank_openid_to_user_key(gsl::string_view openid) {
  auto colon_idx = std::find(openid.begin(), openid.end(), ':');
  if (colon_idx == openid.end()) {
    return std::tuple<uint32_t, uint64_t, int32_t, int64_t>(0, 0, 0, 0);
  }

  auto second_idx = std::find(colon_idx + 1, openid.end(), ':');
  if (second_idx == openid.end()) {
    return std::tuple<uint32_t, uint64_t, int32_t, int64_t>(
        util::string::to_int<uint32_t>(openid.substr(0, static_cast<size_t>(colon_idx - openid.begin()))),
        util::string::to_int<uint64_t>(openid.substr(static_cast<size_t>(colon_idx - openid.begin() + 1))), 0, 0);
  }

  auto finish_idx = std::find(second_idx + 1, openid.end(), ':');

  if(finish_idx == openid.end()) {
    return std::tuple<uint32_t, uint64_t, int32_t, int64_t>(
        util::string::to_int<uint32_t>(openid.substr(0, static_cast<size_t>(colon_idx - openid.begin()))),
        util::string::to_int<uint64_t>(openid.substr(static_cast<size_t>(colon_idx - openid.begin() + 1),
                                                     static_cast<size_t>(second_idx - openid.begin()))),
        util::string::to_int<int32_t>(openid.substr(static_cast<size_t>(second_idx - openid.begin() + 1))), 0);
  }

  return std::tuple<uint32_t, uint64_t, int32_t, int64_t>(
      util::string::to_int<uint32_t>(openid.substr(0, static_cast<size_t>(colon_idx - openid.begin()))),
      util::string::to_int<uint64_t>(openid.substr(static_cast<size_t>(colon_idx - openid.begin() + 1),
                                                   static_cast<size_t>(second_idx - openid.begin()))),
      util::string::to_int<int32_t>(openid.substr(static_cast<size_t>(second_idx - openid.begin() + 1),
                                                  static_cast<size_t>(finish_idx - openid.begin()))),
      util::string::to_int<int64_t>(openid.substr(static_cast<size_t>(finish_idx - openid.begin() + 1))));
}