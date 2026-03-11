#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/config/com.struct.rank.config.pb.h>
#include <protocol/pbdesc/svr.struct.pb.h>

#include <config/server_frame_build_feature.h>

PROJECT_NAMESPACE_BEGIN
SERVER_FRAME_API bool operator<(const DRankInstanceKey& l, const DRankInstanceKey& r) noexcept;
SERVER_FRAME_API bool operator==(const DRankInstanceKey& l, const DRankInstanceKey& r) noexcept;

SERVER_FRAME_API bool operator<(const DRankUserKey& l, const DRankUserKey& r) noexcept;
SERVER_FRAME_API bool operator==(const DRankUserKey& l, const DRankUserKey& r) noexcept;

SERVER_FRAME_API bool operator<(const rank_sort_score& l, const rank_sort_score& r) noexcept;
SERVER_FRAME_API bool operator==(const rank_sort_score& l, const rank_sort_score& r) noexcept;

SERVER_FRAME_API bool operator<(const rank_sort_data& l, const rank_sort_data& r) noexcept;
SERVER_FRAME_API bool operator==(const rank_sort_data& l, const rank_sort_data& r) noexcept;
PROJECT_NAMESPACE_END

namespace rank_util {

const uint32_t RANK_GET_TOP_MAX_COUNT = 100; //单次查询的某top排行榜的最大区间


SERVER_FRAME_API void dump_rank_basic_board_from_rank_data(const PROJECT_NAMESPACE_ID::rank_storage_data& in,
                                                           PROJECT_NAMESPACE_ID::DRankUserBoardData& out);

}  // namespace rank_util