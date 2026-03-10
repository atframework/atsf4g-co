#include "utility/rank_util.h"

#include <config/compiler/protobuf_prefix.h>
#include <protocol/config/com.struct.rank.config.pb.h>
#include <protocol/pbdesc/svr.struct.pb.h>

#include <config/compiler/protobuf_suffix.h>
#include <utility/protobuf_mini_dumper.h>

PROJECT_NAMESPACE_BEGIN

bool operator<(const PROJECT_NAMESPACE_ID::DRankInstanceKey& l, const PROJECT_NAMESPACE_ID::DRankInstanceKey& r) noexcept {
  if (l.instance_type() != r.instance_type()) {
    return l.instance_type() < r.instance_type();
  }
  return l.instance_id() < r.instance_id();
}

bool operator==(const PROJECT_NAMESPACE_ID::DRankInstanceKey& l, const PROJECT_NAMESPACE_ID::DRankInstanceKey& r) noexcept {
  return l.instance_type() == r.instance_type() && l.instance_id() == r.instance_id();
}

bool operator<(const PROJECT_NAMESPACE_ID::DRankUserKey& l, const PROJECT_NAMESPACE_ID::DRankUserKey& r) noexcept {
  if (l.user_id() != r.user_id()) {
    return l.user_id() < r.user_id();
  }
  return l.zone_id() == r.zone_id() ? l.rank_instance_key() < r.rank_instance_key() : l.zone_id() < r.zone_id();
}

bool operator==(const PROJECT_NAMESPACE_ID::DRankUserKey& l, const PROJECT_NAMESPACE_ID::DRankUserKey& r) noexcept {
  return l.user_id() == r.user_id() && l.zone_id() == r.zone_id() && l.rank_instance_key() == r.rank_instance_key();
}

bool operator<(const PROJECT_NAMESPACE_ID::rank_sort_score& l, const PROJECT_NAMESPACE_ID::rank_sort_score& r) noexcept {
  if (l.score() != r.score()) {
    return l.score() < r.score();
  }
  if (l.sort_fields_size() != r.sort_fields_size()) {
    return l.sort_fields_size() < r.sort_fields_size();
  }

  for (int32_t i = 0; i < l.sort_fields_size(); ++i) {
    if (l.sort_fields().at(i) != r.sort_fields().at(i)) {
      return l.sort_fields().at(i) < r.sort_fields().at(i);
    }
  }

  return false;
}

bool operator==(const PROJECT_NAMESPACE_ID::rank_sort_score& l, const PROJECT_NAMESPACE_ID::rank_sort_score& r) noexcept {
  if (l.score() != r.score()) {
    return false;
  }
  if (l.sort_fields_size() != r.sort_fields_size()) {
    return false;
  }

  for (int32_t i = 0; i < l.sort_fields_size(); ++i) {
    if (l.sort_fields().at(i) != r.sort_fields().at(i)) {
      return false;
    }
  }

  return true;
}

bool operator<(const PROJECT_NAMESPACE_ID::rank_sort_data& l, const PROJECT_NAMESPACE_ID::rank_sort_data& r) noexcept {
  return l.value() == r.value() ? l.key() < r.key() : l.value() < r.value();
}

bool operator==(const PROJECT_NAMESPACE_ID::rank_sort_data& l, const PROJECT_NAMESPACE_ID::rank_sort_data& r) noexcept {
  return l.value() == r.value() && l.key() == r.key();
}

PROJECT_NAMESPACE_END

namespace rank_util {

void dump_rank_basic_board_from_rank_data(const PROJECT_NAMESPACE_ID::rank_storage_data& in,
                                          PROJECT_NAMESPACE_ID::DRankUserBoardData& out) {
  out.set_submit_timepoint(in.sort_data().value().submit_timepoint());
  out.set_score(in.sort_data().value().score());
  protobuf_copy_message(*out.mutable_custom_data(), in.custom_data());
  protobuf_copy_message(*out.mutable_user_key(), in.sort_data().key());
}

}  // namespace rank_util