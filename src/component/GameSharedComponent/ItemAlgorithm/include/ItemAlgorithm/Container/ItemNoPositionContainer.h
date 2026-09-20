// Copyright 2025 atframework

#pragma once

#include <cstdint>

#include "ItemAlgorithm/ItemAlgorithmConfig.h"
#include "ItemAlgorithm/ItemContainer.h"

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace item_algorithm {

/// @brief 无位置容器
///
/// 不关心道具位置: 道具按类型合并计数, 不维护位置索引与 GUID (带 GUID 或不占格规则不符的
/// 道具会被拒绝)。用于虚拟背包这类只统计数量的容器。
/// 没有格子概念, 因此不提供 get / 位图 / 区域查询;
/// 也没有位置语义, 因此不接入 move: check_move / move 继承基类流程, 但基类默认的
/// on_check_move / on_move_sub_one / on_move_add_one 会直接以 EN_ERR_INVALID_PARAM 拒绝。
///
/// 增删改查的批次流程 (check_add / add / check_sub / sub / check_has /
/// load / apply_entries / find_positions_*) 都由基类 ItemContainer 实现, 本类只提供单条钩子:
/// 落位方式只有"合入同类型的计数条目"与"新建计数条目"两种, 单条校验只剩 is_item_valid 的字段规则。
/// 位置字段映射 (extract_position / apply_position) 由最上层接入按自己的位置字段实现,
/// 因此本类不是可以直接实例化的最终容器。
class ATFW_UTIL_SYMBOL_VISIBLE ItemNoPositionContainer : public ItemContainer {
 public:
  ITEM_ALGORITHM_API ItemNoPositionContainer();
  ITEM_ALGORITHM_API ~ItemNoPositionContainer() override;

  ItemNoPositionContainer(const ItemNoPositionContainer&) = delete;
  ItemNoPositionContainer& operator=(const ItemNoPositionContainer&) = delete;
  ItemNoPositionContainer(ItemNoPositionContainer&&) = delete;
  ItemNoPositionContainer& operator=(ItemNoPositionContainer&&) = delete;

 public:
  /// @brief 初始化无位置容器
  /// @param position_type  容器声明的位置字段 (无位置模式不做落位, 库内不再用它做字段映射,
  ///                       只保存下来供接入层通过 get_position_type() 读取)
  /// @param container_guid 容器唯一 ID
  ITEM_ALGORITHM_API void init(PROJECT_NAMESPACE_ID::DItemGridPosition::PositionTypeCase position_type,
                               int64_t container_guid);

 protected:
  /// @brief 取本容器声明的位置字段类型
  ///
  /// 接入层的位置字段声明; 库内不再自己映射, 只保存供接入层读取。
  /// 被调用时机: 接入层实现 extract_position / apply_position 时, 用它决定读写哪个 proto 字段。
  ATFW_UTIL_FORCEINLINE PROJECT_NAMESPACE_ID::DItemGridPosition::PositionTypeCase get_position_type() const {
    return position_type_;
  }

  // ============================================================
  // 条目校验 (本模式只接受不占格道具, 且不接受 GUID)
  // ============================================================

  /// @brief 道具字段是否与本模式自洽 (不占格道具 + 无 GUID)
  ///
  /// 被调用时机: 基类 validate_item_basic 逐条校验时 (check_add / check_sub / check_has / load /
  /// apply_entries), 以及本类的寻位钩子判断单条道具时, 只读判断。
  /// @return false 表示 type_id 或 count 非法 / 类型配置缺失 / 需要占格 / 带 GUID
  ITEM_ALGORITHM_API bool is_item_valid(const excel_config_group_ptr_t& config_group,
                                        const PROJECT_NAMESPACE_ID::DItemBasic& basic) const override;

  // ============================================================
  // 单条校验钩子
  //
  // 基类的通用校验 (字段 / 归属 / 位置 / 类型存在 / GUID 唯一 / 数量上限 / 按类型总数对账)
  // 已经覆盖本模式全部规则, 所以这三个钩子都直接放行。
  // ============================================================

  /// @brief 本条放入请求是否直接跳过
  ///
  /// 本模式只按类型计数, count == 0 的请求没有可加的数量, 属于空请求:
  /// check_add 不校验它, add 也不执行它 (is_item_valid 仍然要求 count > 0,
  /// 因此 load / apply_entries 这些同步路径上的 count == 0 依旧按非法数据处理)。
  ITEM_ALGORITHM_API bool should_skip_add_request(const PROJECT_NAMESPACE_ID::DItemInstance& request) const override;

  /// @brief 一批放入请求的本模式检查
  ///
  /// 被调用时机: 基类 check_add 完成通用字段校验后调用一次。
  /// 无位置模式没有位置或批次占用规则, 不占格与无 GUID 已由 is_item_valid 判断。
  /// @return 恒为 EN_SUCCESS
  ITEM_ALGORITHM_API ItemOperationResult on_check_add(const ItemAddCheckedRequest& checked_request) const override;

  /// @brief 一批扣减请求的本模式检查
  ///
  /// 被调用时机: 基类 check_sub 完成通用字段与按 type_id 总量校验后调用一次。
  /// 无位置模式没有指定位置或 GUID 规则。
  /// @return 恒为 EN_SUCCESS
  ITEM_ALGORITHM_API ItemOperationResult on_check_sub(const ItemSubCheckedRequest& checked_request) const override;

  /// @brief 一批对账请求的本模式检查
  ///
  /// 被调用时机: 基类 check_has 完成通用字段与按 type_id 总量校验后调用一次。
  /// 无位置模式没有额外规则。
  /// @return 恒为 EN_SUCCESS
  ITEM_ALGORITHM_API ItemOperationResult on_check_has(const excel_config_group_ptr_t& config_group,
                                                      const item_basic_readable_iterable& requests) const override;

  // ============================================================
  // 单条执行钩子
  // ============================================================

  /// @brief 放入一件道具 (合入同类型的计数条目, 或新建计数条目)
  ///
  /// 被调用时机: 基类 add 逐条循环时, 每条一次; load 也走这里 (载入时同样要落位)。
  /// 基类已确认 checked request 合法并置上 apply。
  /// 本条不合法 (类型缺失 / 需要占格 / 带 GUID) 或条目没能进分组时只记日志并返回成功,
  /// 让整批继续 (这些分支没有可回滚的数据, 与基类"单条失败即整体失败"的钩子语义不同)。
  /// @param instance 本条待放入的道具 (只读)
  /// @return 恒为 EN_SUCCESS
  ITEM_ALGORITHM_API ItemOperationResult on_add_one(ItemAddCheckedRequest& checked_request,
                                                    const PROJECT_NAMESPACE_ID::DItemInstance& instance,
                                                    const ItemOperationContext& context) override;

  /// @brief 扣减一件道具 (按类型逐个条目扣到满足为止)
  ///
  /// 被调用时机: 基类 sub 逐条循环时, 每条一次。
  /// 同类型可能有多个条目, 逐个扣; 被扣光的条目从分组移除并把数量归零。
  /// 条目扣不满只记错误日志 (check_sub 已保证总量够扣), 不影响整批返回。
  /// @param request 本条待扣减的请求 (只读)
  /// @return 恒为 EN_SUCCESS
  ITEM_ALGORITHM_API ItemOperationResult on_sub_one(ItemSubCheckedRequest& checked_request,
                                                    const PROJECT_NAMESPACE_ID::DItemBasic& request,
                                                    const ItemOperationContext& context) override;

  /// @brief 载入一条持久化数据 (合入同类型的计数条目, 或新建计数条目)
  ///
  /// 被调用时机: 基类 load 通过 (遍历保护 / 初始化 / 字段校验 / 容器归属 / 数量上限) 之后调用一次。
  /// @return false 表示条目没能进分组 (基类直接返回 false); 合入或新建成功返回 true
  ITEM_ALGORITHM_API bool on_load_one(const excel_config_group_ptr_t& config_group,
                                      const PROJECT_NAMESPACE_ID::DItemInstance& item_instance,
                                      const ItemOperationContext& context) override;

  /// @brief 应用客户端同步的"删除一条"
  ///
  /// 被调用时机: 基类 apply_entries 的删除阶段, 每条已找到的条目调用一次。
  /// 本模式没有位置 / GUID 索引, 摘掉分组与数量缓存即可。
  ITEM_ALGORITHM_API void on_apply_remove_one(const excel_config_group_ptr_t& config_group,
                                              const item_entry_ptr_t& entry,
                                              const ItemOperationContext& context) override;

  /// @brief 应用客户端同步的"新增或更新一条"
  ///
  /// 被调用时机: 基类 apply_entries 的新增/更新阶段, 每条通过 (entry_id 非 0 / 字段校验 /
  /// 容器归属) 校验、并按 entry_id 查过一次条目之后调用一次。
  /// 已有条目只覆盖数据并刷新数量缓存; 没有条目则按同步包里的 entry_id 新建。
  /// @param existing 该 entry_id 已存在的条目 (不存在时为 nullptr)
  ITEM_ALGORITHM_API void on_apply_update_one(const excel_config_group_ptr_t& config_group,
                                              const item_entry_ptr_t& existing,
                                              const PROJECT_NAMESPACE_ID::DItemInstanceEntry& update,
                                              const ItemOperationContext& context) override;

  // ============================================================
  // 寻位钩子
  // ============================================================

  /// @brief 为一批道具寻位的本模式实现
  ///
  /// 被调用时机: 基类 find_positions_for_instances (及由它实现的 find_positions_for_basics) 在
  /// 初始化检查与输出预留之后调用一次。
  /// 本模式没有格子可找: 只校验是不是不占格道具, 校验通过就写回声明的位置字段与 container_guid
  /// (写回方式由接入层实现的 apply_position 决定), 调用方拿到的结果可以直接喂给 check_add。
  /// 没有可推算的空位, 因此不支持 ignore_item。
  /// @param ignore_item 传入非空直接返回 false
  /// @param success_item [out] 校验通过的道具 (位置字段已写回)
  /// @param failed_item  [out] 本模式不会往里写, 进入时清空
  /// @return false 表示整体失败 (传参不受支持, 或某条道具不合法)
  ITEM_ALGORITHM_API bool on_find_positions(
      const excel_config_group_ptr_t& config_group, const item_instance_readable_iterable& items,
      const item_basic_readable_iterable& ignore_item,
      google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& success_item,
      google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& failed_item) const override;

 private:
  PROJECT_NAMESPACE_ID::DItemGridPosition::PositionTypeCase position_type_ =
      PROJECT_NAMESPACE_ID::DItemGridPosition::POSITION_TYPE_NOT_SET;
};

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END
