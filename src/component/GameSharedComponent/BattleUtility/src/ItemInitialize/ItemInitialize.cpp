#include "ItemInitialize/ItemInitialize.h"

#include <config/compiler_features.h>

#include <Random/Random.h>
#include <config/excel/config_manager.h>
#include <config/excel/item_type_config.h>

#include <algorithm>

#include "config/excel/config_easy_api.h"

PROJECT_NAMESPACE_BEGIN
namespace battle_utility::item_initialize {

BATTLE_UTILITY_API bool ItemInitialize(PROJECT_NAMESPACE_ID::DItemInstance &Item, const ItemInitializeArgs &args) {
  return ItemInitialize(excel::config_manager::me()->get_current_config_group(), random::GetRandomGenerator(), Item,
                        args);
}

BATTLE_UTILITY_API bool ItemInitialize(const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t> &,
                                       ::PROJECT_NAMESPACE_ID::battle_utility::random::RandomGenerator &,
                                       PROJECT_NAMESPACE_ID::DItemInstance &Item, const ItemInitializeArgs &args) {
  bool ret = true;
  if (ItemAlgorithmTypeOption::IsNeedGuid(Item.item_basic().type_id())) {
    // 业务功能 GUID分配在外层
  }
  Item.mutable_item_basic()->mutable_position()->set_container_guid(args.container_guid);
  return ret;
}

BATTLE_UTILITY_API bool CreateItem(
    const ::excel::excel_config_type_traits::shared_ptr<::excel::config_group_t> &config_group,
    ::PROJECT_NAMESPACE_ID::battle_utility::random::RandomGenerator &random, int32_t ItemId, int64_t Cnt,
    const ItemInitializeArgs &initialize_args,
    google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance> &out) {
  auto item_row = ::excel::get_ExcelItemType_by_type_id(config_group, ItemId);
  if (item_row == nullptr) {
    return false;
  }

  int32_t stack_max = 0;
  if (ItemAlgorithmTypeOption::IsNeedOccupyTheGrid(ItemId)) {
    auto ue_item_row = ::excel::get_UESourceInventory_by_type_id(config_group, item_row->ue_source_type_id());
    if (ue_item_row == nullptr) {
      return false;
    }
    stack_max = ue_item_row->position_cfg().accumulation_limit();
  }
  if (stack_max < 0) {
    stack_max = 0;
  }

  while (Cnt > 0) {
    int64_t current_cnt = Cnt;
    if (current_cnt > stack_max && stack_max != 0) {
      Cnt -= stack_max;
      current_cnt = stack_max;
    } else {
      Cnt = 0;
    }
    auto add_item = out.Add();
    add_item->mutable_item_basic()->set_type_id(ItemId);
    add_item->mutable_item_basic()->set_count(current_cnt);

    ItemInitialize(config_group, random, *add_item, initialize_args);
  }
  return true;
}

BATTLE_UTILITY_API int32_t RarityCompare(PROJECT_NAMESPACE_ID::EnItemRarity left,
                                         PROJECT_NAMESPACE_ID::EnItemRarity right) {
  if (left > right) {
    return 1;
  }
  if (left < right) {
    return -1;
  }
  return 0;
}
}  // namespace battle_utility::item_initialize

PROJECT_NAMESPACE_END
