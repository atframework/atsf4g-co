#pragma once

#include <BattleUtility/BattleUtilityConfig.h>

#include <config/compiler/protobuf_prefix.h>

#include <protocol/common/com.struct.item.common.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <config/excel_type_trait_setting.h>

#include <Random/Random.h>

namespace excel {
struct config_group_t;
}

PROJECT_NAMESPACE_BEGIN

namespace battle_utility {
namespace item_initialize {

struct ItemInitializeArgs {
  int64_t container_guid = 0;
};

BATTLE_UTILITY_API bool ItemInitialize(PROJECT_NAMESPACE_ID::DItemInstance &Item, const ItemInitializeArgs &args);

BATTLE_UTILITY_API bool ItemInitialize(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t> &config_group,
    ::PROJECT_NAMESPACE_ID::battle_utility::random::RandomGenerator &random, PROJECT_NAMESPACE_ID::DItemInstance &Item,
    const ItemInitializeArgs &args);

BATTLE_UTILITY_API bool CreateItem(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t> &config_group,
    ::PROJECT_NAMESPACE_ID::battle_utility::random::RandomGenerator &random, int32_t ItemId, int64_t Cnt,
    const ItemInitializeArgs &initialize_args,
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance> &out);

//  1 l > r
//  0 l == r
// -1 l < r
BATTLE_UTILITY_API int32_t RarityCompare(PROJECT_NAMESPACE_ID::EnItemRarity left,
                                         PROJECT_NAMESPACE_ID::EnItemRarity right);

}  // namespace item_initialize
}  // namespace battle_utility

PROJECT_NAMESPACE_END