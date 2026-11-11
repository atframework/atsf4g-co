// Copyright 2025 atframework

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ItemAlgorithm/ItemAlgorithmConfig.h"
#include "ItemAlgorithm/ItemAlgorithmLog.h"
#include "ItemAlgorithm/ItemContainerEntry.h"
#include "ItemAlgorithm/ItemContainerTypes.h"

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace item_algorithm {

class ItemContainer;
class ItemFiniteGridFindPositionHelper;

using item_container_ptr_t = atfw::util::memory::strong_rc_ptr<ItemContainer>;

/// @brief 物品容器基类
///
/// 本类负责三件事:
///   1. 自己私有的条目存储与索引: item_groups_ (按 type_id 分组, 每个分组保存该类型排好序
///      的条目与数量缓存, 之后所有按 type_id 统计的数据也放这里)、entry_id_index_、
///      日志处理器、自增 entry_id 与操作流水号, 以及这些数据的生命周期与查询接口。
///   2. 增删改查的**批次流程**: check_add / check_sub / check_has / add /
///      sub / load / apply_entries / find_positions_* 全部在基类实现。
///      基类负责持有入参视图、初始化检查、通用校验、循环与结果汇总; 子类只实现单条钩子。
///   3. 给子类提供条目级原语 (建条目 / 入分组 / 改数量 / 改数据 / 刷排序键) 与
///      数量变化通知 (notify_entry_count_changed) 等公共收尾。
///
/// 子类的扩展点 (均为单条/单批钩子, 不含流程), 按"谁来覆盖"分三组:
///   1. 模式钩子 — 三个预设容器各自实现, 业务容器一般不用覆盖:
///      on_check_add / on_check_sub / on_check_has / should_skip_add_request /
///      on_add_one / on_sub_one / on_load_one / on_check_move / on_move_*_one /
///      on_apply_*_one / on_find_positions / is_item_valid / make_entry_sort_key /
///      find_entry_* / has_entry_guid / on_clear
///   2. 业务接入钩子 — 三个预设容器都不覆盖, 业务按需覆盖 (与模式无关):
///      on_check_add_one / on_check_sub_one / on_check_has_one / on_check_item_count_limit /
///      on_item_count_changed / on_item_data_changed / check_item_position /
///      get_item_position_cfg
///   3. 位置字段映射 — 库内不做业务字段映射, 由最上层接入实现:
///      extract_position / apply_position (纯虚, 接入层容器必须实现)
/// 另外, 位置 / GUID 索引的挂摘与格子占用判断由各模式自己维护, 不在上面的钩子列表里。
///
/// 库内不做 proto 位置字段的映射 (即不关心 user_inventory / character_equipment 等业务字段):
/// extract_position / apply_position 是纯虚接口, 由最上层接入按自己的位置字段实现。
///
/// 三种模式容器按其数据结构补充能力:
///   - ItemFiniteGridContainer:   行列有限 + 位图 + 可占多格
///   - ItemInfiniteGridContainer: 不限行列 + 每件占一个位置 (槽位)
///   - ItemNoPositionContainer:   不关心位置, 只按类型计数
class ATFW_UTIL_SYMBOL_VISIBLE ItemContainer : public atfw::util::memory::enable_shared_rc_from_this<ItemContainer> {
  friend struct ItemEntry;
  friend class ItemFiniteGridFindPositionHelper;

 public:
  using item_group_type = std::set<item_entry_ptr_t, item_entry_ptr_comparator>;

  /// @brief 按 type_id 聚合的数据
  ///
  /// 除了排好序的条目, 之后所有"按 type_id 统计"的数据都放这里 (如数量缓存),
  /// 避免为每个统计维度再开一份 map。
  struct ATFW_UTIL_SYMBOL_VISIBLE ItemTypeGroup {
    item_group_type entries;  ///< 该类型下的条目, 按排序键排列
    int64_t count_cache = 0;  ///< 该类型的数量缓存, 供 get_item_count() O(1) 查询
  };

  using item_group_map_type = std::unordered_map<int32_t, ItemTypeGroup>;

 public:
  ITEM_ALGORITHM_API virtual ~ItemContainer();

  ItemContainer(const ItemContainer&) = delete;
  ItemContainer& operator=(const ItemContainer&) = delete;
  ItemContainer(ItemContainer&&) = delete;
  ItemContainer& operator=(ItemContainer&&) = delete;

 public:
  /// @brief 清空容器: 逐条移除现有条目, 并像 sub 那样触发变更通知
  ///
  /// 与 sub 的整体移除共用同一套流程 (摘索引 -> 摘分组 -> 数量归零 -> 通知),
  /// 区别只是不做任何校验 (调用方已经决定要清空)。每条现存条目都会触发一次
  /// on_item_count_changed (new_count == 0) 与 on_item_data_changed, 原因 kClear;
  /// 容器为空时不产生通知。
  ITEM_ALGORITHM_API void clear();

 public:
  // ---- 日志接口 ----
  // 注册日志处理器 (未注册 on_log 时所有日志被忽略)
  // handler.category 会被复制到容器自己的存储, 调用方不必保证它的生命周期
  ITEM_ALGORITHM_API void set_log_handler(const ItemLogHandler& handler);
  ITEM_ALGORITHM_API const ItemLogHandler& get_log_handler() const;
  /// @brief 取本容器的日志实例, 配合框架的 FWINSTLOG* 宏使用
  /// @example FWINSTLOGERROR(logger(), "...", args...)
  ATFW_UTIL_FORCEINLINE ItemLogInstance logger() const noexcept { return ItemLogInstance(&log_handler_); }

 public:
  // ---- 查询接口 ----
  /// @brief 遍历容器内的所有道具实例
  ///
  /// 遍历期间容器处于"只读"状态: add / sub / move / load / apply_entries 会直接失败
  /// (见 is_operation_allowed), 避免迭代器失效或边遍历边改数据。
  /// 回调返回 false 会中断遍历。
  /// @return true 表示所有条目都被访问过 (回调没有提前中断)
  ITEM_ALGORITHM_API bool foreach_instance(
      atfw::util::nostd::function_ref<bool(const PROJECT_NAMESPACE_ID::DItemInstance&)> fn) const;
  // 按照位置排序了的数据
  ITEM_ALGORITHM_API const item_group_type* get_group(int32_t type_id) const;
  /// @brief 道具数量 (读所属分组的 count_cache, O(1), 正常流程用这个)
  ITEM_ALGORITHM_API int64_t get_item_count(int32_t type_id) const;
  /// @brief 道具数量 (遍历分组实时求和, O(条目数), 只用于校验/调试)
  ITEM_ALGORITHM_API int64_t get_item_count_debug(int32_t type_id) const;
  ATFW_UTIL_FORCEINLINE uint64_t peek_next_entry_id() const { return next_entry_id_; }
  ITEM_ALGORITHM_API bool is_empty() const;
  ITEM_ALGORITHM_API const item_group_map_type& get_all_groups() const;
  ITEM_ALGORITHM_API int64_t get_container_guid() const;
  // 容器内当前的条目数量
  ITEM_ALGORITHM_API size_t get_entry_count() const;

  // 通过Basic提取数据
  ITEM_ALGORITHM_API item_entry_ptr_t find_entry(const PROJECT_NAMESPACE_ID::DItemBasic& basic) const;
  ITEM_ALGORITHM_API item_entry_ptr_t find_entry_by_id(uint64_t entry_id) const;

 public:
  // ============================================================
  // 批次操作 — 由基类路由到子类的单条钩子
  //
  // check_* / check_has : 多物品接口。checked request 只持有传入的入参视图, 不复制数据;
  //                       基类负责初始化检查与逐条的通用校验
  //                       (容器归属 / 位置字段 / 类型存在 / GUID 唯一 / 数量上限),
  //                       每条再调子类的 on_check_*_one 补本模式规则。
  // add / sub / load    : 单物品语义。基类校验 checked request 后遍历请求视图,
  //                       子类只实现 on_add_one / on_sub_one / on_load_one。
  // apply_entries       : 基类 = 逐条删除 + 逐条新增/更新, 复用 on_apply_*_one。
  // 整体替换            : 库不再提供 replace 接口, 由调用方用 clear() + 逐条 load() 组合实现。
  // find_positions_*    : 基类做入参校验与输出预留, 子类实现 on_find_positions。
  // ============================================================

  /// @brief 校验放入请求并生成 checked request (多物品)
  ///
  /// 每条依次检查: is_item_valid -> 容器归属 -> check_item_position -> 类型存在 -> GUID 唯一
  /// -> 数量上限 -> on_check_add_one。任一条失败则整个 check 失败, 并把该条的 type_id 记为
  /// failed_type_id。
  /// @param requests 具名请求视图 (checked request 会引用它, 所以不能传临时视图; 调用方需保证它活到 add 执行完)
  ITEM_ALGORITHM_API ItemAddCheckedRequest check_add(const excel_config_group_ptr_t& config_group,
                                                     item_instance_readable_iterable& requests,
                                                     const ItemOperationSource& source) const;

  /// @brief 执行放入: 基类校验 checked request 后逐条调用 on_add_one
  /// @param reason 传给钩子的操作原因 (move 的 Add 阶段等场景由调用方指定, 默认 kAdd)
  ITEM_ALGORITHM_API ItemOperationResult add(ItemAddCheckedRequest& checked_request,
                                             ItemOperationReason reason = ItemOperationReason::kAdd);

  /// @brief 校验扣减请求并生成 checked request (多物品)
  /// @param requests 具名请求视图 (checked request 会引用它, 所以不能传临时视图; 调用方需保证它活到 sub 执行完)
  ITEM_ALGORITHM_API ItemSubCheckedRequest check_sub(const excel_config_group_ptr_t& config_group,
                                                     item_basic_readable_iterable& requests,
                                                     const ItemOperationSource& source) const;

  /// @brief 执行扣减: 基类校验 checked request 后逐条调用 on_sub_one
  /// @param reason 传给钩子的操作原因 (默认 kSub)
  ITEM_ALGORITHM_API ItemOperationResult sub(ItemSubCheckedRequest& checked_request,
                                             ItemOperationReason reason = ItemOperationReason::kSub);

  /// @brief 校验移动请求并生成 checked request (多条目: 先扣来源, 再放目标)
  ///
  /// 基类只做"基础操作": 建 checked request (request 自持) -> 初始化检查 -> 空请求判断
  /// -> 交给 on_check_move 做本模式的移动校验 (来源 / 目标 / 批次内占用预演)
  /// -> 逐条跑业务钩子 (扣减条目 on_check_sub_one, 放入条目 on_check_add_one)。
  /// 业务钩子的前置条件与 check_sub / check_add 一致: 只在入参通过本模式校验之后调用,
  /// 且扣减侧再补一次 validate_item_basic、放入侧再校验目标位置 (容器归属 / 位置字段类型 /
  /// check_item_position), 钩子不会看到 op_count 或目标位置非法的输入。
  /// 没有位置语义的模式 (无位置容器) 不支持 move: 基类默认的 on_check_move 直接拒绝。
  ITEM_ALGORITHM_API ItemMoveCheckedRequest check_move(const excel_config_group_ptr_t& config_group,
                                                       ItemMoveRequest&& request,
                                                       const ItemOperationSource& source) const;

  /// @brief 执行移动: 基类校验 checked request 后, 先跑 kMoveSub 阶段再跑 kMoveAdd 阶段
  ///
  /// 两个阶段各自逐条调用子类的 on_move_sub_one / on_move_add_one; 任一条失败即结束并返回该错误。
  ITEM_ALGORITHM_API ItemOperationResult move(ItemMoveCheckedRequest& checked_request);

  /// @brief 两个 proto 位置是否落在同一个坐标上 (按本模式的字段映射判断)
  ///
  /// 位置字段的映射 (extract_position) 是接入层实现、库内不对外暴露, 需要比较位置时用本接口:
  /// 例如组级编排判断"源位置与目标位置相同"就直接跳过。
  ITEM_ALGORITHM_API bool is_same_grid_position(const PROJECT_NAMESPACE_ID::DItemGridPosition& lhs,
                                                const PROJECT_NAMESPACE_ID::DItemGridPosition& rhs) const;

  /// @brief 校验是否拥有 (多物品, 只对账不改数据)
  ITEM_ALGORITHM_API ItemOperationResult check_has(const excel_config_group_ptr_t& config_group,
                                                   const item_basic_readable_iterable& requests) const;

  /// @brief 载入一条条目数据 (DItemInstanceEntry 形态, 含条目 ID)
  ///
  /// entry_id 非 0 时沿用该 ID (持久化恢复用, 保证多次重建后条目 ID 不变);
  /// entry_id 为 0 (旧数据 / 初始模板) 时退化为容器自增分配。
  /// @param reason 传给钩子的操作原因 (默认 kLoad; 整体替换等场景由调用方指定, 如 kReplaceLoad)
  /// @param source 调用方透传的操作来源
  /// @return false 表示本条载入失败 (调用方决定跳过还是中止)
  ITEM_ALGORITHM_API bool load(const excel_config_group_ptr_t& config_group,
                               const PROJECT_NAMESPACE_ID::DItemInstanceEntry& item_entry,
                               const ItemOperationSource& source,
                               ItemOperationReason reason = ItemOperationReason::kLoad);

  /// @brief 应用客户端同步包: 基类逐条删除 + 逐条新增/更新
  ITEM_ALGORITHM_API void apply_entries(const excel_config_group_ptr_t& config_group,
                                        const google::protobuf::RepeatedField<uint64_t>& remove_entry_ids,
                                        const item_instance_entry_readable_iterable& update_entries,
                                        const ItemOperationSource& source);

  /// @brief 为一批 DItemBasic 寻位 (基类校验入参并预留输出, 子类实现 on_find_positions)
  ITEM_ALGORITHM_API bool find_positions_for_basics(
      const excel_config_group_ptr_t& config_group, const item_basic_readable_iterable& basics,
      const item_basic_readable_iterable& ignore_item,
      google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& success_item,
      google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemBasic>& failed_item) const;

  /// @brief 为一批 DItemInstance 寻位 (语义同 find_positions_for_basics)
  ITEM_ALGORITHM_API bool find_positions_for_instances(
      const excel_config_group_ptr_t& config_group, const item_instance_readable_iterable& items,
      const item_basic_readable_iterable& ignore_item,
      google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& success_item,
      google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& failed_item) const;

 protected:
  ITEM_ALGORITHM_API ItemContainer();

  /// @brief 初始化所有模式共用的部分 (容器 GUID + 条目 ID 自增游标 + 清空)
  /// @param next_entry_id 条目 ID 自增游标; 0 (旧存档 / 新建容器) 时按 1 处理
  ITEM_ALGORITHM_API void init_container(PROJECT_NAMESPACE_ID::DItemGridPosition::PositionTypeCase position_type,
                                         int64_t container_guid, uint64_t next_entry_id);

 protected:
  // 钩子开始
  /// @brief 清空子类自己维护的索引 (位置索引 / GUID 索引 / 占用位图)
  ///
  /// 被调用时机: clear() 在逐条触发变更通知之前调用一次 (与 sub 完全移除时的摘索引同序)。
  /// 跟踪位置的模式必须覆盖本函数, 否则清空后索引会残留已删除条目。
  ITEM_ALGORITHM_API virtual void on_clear();

  // ============================================================
  // 模式钩子 — 由三个预设容器各自实现 (有限格子 / 无限格子 / 无位置)
  // 业务容器一般不需要覆盖; 业务接入钩子集中在下一节
  // ============================================================

  /// @brief 一批放入请求的本模式检查
  ///
  /// 被调用时机: 基类 check_add 完成所有通用字段校验后调用一次。
  /// 子类在这里检查跨条目的模式规则, 如位置碰撞、同批次占位与堆叠数量。
  /// @return EN_SUCCESS 表示整批通过; 失败时填 error_code 与 failed_type_id
  ITEM_ALGORITHM_API virtual ItemOperationResult on_check_add(const ItemAddCheckedRequest& checked_request) const = 0;

  /// @brief 一批扣减请求的本模式检查
  ///
  /// 被调用时机: 基类 check_sub 完成所有通用字段与按 type_id 总量校验后调用一次。
  /// 子类在这里检查 GUID 或指定位置上的条目是否存在、数量是否够扣等模式规则。
  /// @return EN_SUCCESS 表示整批通过; 失败时填 error_code 与 failed_type_id
  ITEM_ALGORITHM_API virtual ItemOperationResult on_check_sub(const ItemSubCheckedRequest& checked_request) const = 0;

  /// @brief 一批对账请求的本模式检查
  ///
  /// 被调用时机: 基类 check_has 完成所有通用字段与按 type_id 总量校验后调用一次。
  /// 子类在这里检查 GUID 或指定位置上的条目是否存在、数量是否满足。
  /// @return EN_SUCCESS 表示整批通过; 失败时填 error_code 与 failed_type_id
  ITEM_ALGORITHM_API virtual ItemOperationResult on_check_has(const excel_config_group_ptr_t& config_group,
                                                              const item_basic_readable_iterable& requests) const = 0;

  /// @brief 本条放入请求是否直接跳过 (即不校验也不执行)
  ///
  /// 被调用时机: check_add / add 的逐条循环开头, 在通用校验之前。默认不跳过;
  /// 无位置容器把 count == 0 的请求当成空请求跳过。
  ITEM_ALGORITHM_API virtual bool should_skip_add_request(const PROJECT_NAMESPACE_ID::DItemInstance& request) const;

 protected:
  // ============================================================
  // 业务接入钩子 (开始) — 三个预设容器都不覆盖, 接入层 / 业务容器按需覆盖
  //
  // 这里的默认实现都是"不做额外限制", 业务要加规则时覆盖即可, 与容器模式无关:
  // 预设容器不实现它们, 所以覆盖后三种模式都会生效。
  // 位置字段映射 (extract_position / apply_position) 同样不由预设容器实现, 见下方同名小节。
  // ============================================================

  /// @brief 单条放入请求的额外校验
  ///
  /// 被调用时机: 基类 check_add 完成该条的通用校验之后 (顺序见 check_add 的说明),
  /// 以及寻位为每件道具试出一个候选位置之后 (候选的位置与数量已经写进 request)。
  /// @return EN_SUCCESS 表示通过; 其他错误码会让该条请求失败, check_add / 寻位整体失败
  ITEM_ALGORITHM_API virtual int32_t on_check_add_one(const excel_config_group_ptr_t& config_group,
                                                      const PROJECT_NAMESPACE_ID::DItemInstance& request) const;

  /// @brief 单条扣减请求的额外校验
  ///
  /// 被调用时机: 基类 check_sub 完成该条的通用校验与按类型总量对账之后;
  /// check_move 的扣减条目也会调用 (业务规则不能只在 check_sub 上生效)。
  /// @param config_group 配置组
  /// @param request      扣减请求 (count = 本次扣减数量)
  /// @param is_move      是否来自 check_move。移动是"位置搬运", 扣减侧看到的中间态
  ///                     (来源位置已空、目标位置还没占上) 并不代表最终结果,
  ///                     需要按最终槽位判断的规则 (如"武器槽不能同时为空") 应在
  ///                     is_move 为 true 时跳过, 交给 on_check_move_positions。
  /// @return EN_SUCCESS 表示通过; 其他错误码会让该条请求失败, check_sub / check_move 失败
  ITEM_ALGORITHM_API virtual int32_t on_check_sub_one(const excel_config_group_ptr_t& config_group,
                                                     const PROJECT_NAMESPACE_ID::DItemBasic& request,
                                                     bool is_move = false) const;

  /// @brief 单条对账请求的额外校验
  ///
  /// 被调用时机: 基类 check_has 完成该条的通用校验与按类型总量对账之后。
  /// @return EN_SUCCESS 表示通过; 其他错误码会让该条请求失败, 整个 check_has 失败
  ITEM_ALGORITHM_API virtual int32_t on_check_has_one(const excel_config_group_ptr_t& config_group,
                                                      const PROJECT_NAMESPACE_ID::DItemBasic& request) const;

  /// @brief 道具数量上限检查
  ///
  /// 被调用时机: check_add / load 逐条时 (current_count 已包含同批次前面的累计);
  /// @return EN_SUCCESS 表示不限, 其他错误码会让整个操作失败
  ITEM_ALGORITHM_API virtual int32_t on_check_item_count_limit(int32_t type_id, int64_t current_count,
                                                               int64_t add_count) const;

  /// @brief 道具数量变化通知
  ///
  /// 被调用时机: 基类的 notify_entry_count_changed (各模式的单条钩子改完数量后调用);
  /// 同一个操作可能触发多次 (每个条目一次)。
  /// @param guid     道具 GUID (无 GUID 时为 0)
  /// @param position 道具位置 (extract_position 的结果)
  /// @param old_count 该条目变化前的数量 (新建条目时为 0)
  /// @param new_count 该条目变化后的数量 (完全移除时为 0)
  /// @param type_total_count 该类型的当前总数量 (已包含本次变更)
  ITEM_ALGORITHM_API virtual void on_item_count_changed(int32_t type_id, const item_entry_ptr_t& entry, int64_t guid,
                                                        const ItemGridPosition& position, int64_t old_count,
                                                        int64_t new_count, int64_t type_total_count,
                                                        const ItemOperationContext& context);

  /// @brief 道具数据变化钩子 (数量 / 位置等任意字段变化时触发)
  ///
  /// 由条目在改写 item_data 时触发 (见 ItemEntry::mutable_item_data), 因此保留在基类。
  /// 三种模式容器在自己的操作流程里也会主动调用 (见各钩子的说明)。
  /// 调用时机: 条目数据已被改写完成之后, 同一个操作可能触发多次 (每个条目一次)。
  /// @param entry   数据发生变化的道具条目
  /// @param context 操作上下文 (容器判定的原因 + 调用方透传的来源)
  ITEM_ALGORITHM_API virtual void on_item_data_changed(const item_entry_ptr_t& entry,
                                                       const ItemOperationContext& context);

  /// @brief 检查道具位置字段是否合法 (container_guid 已由基类判断)
  ///
  /// 被调用时机: 基类的 validate_item_basic, 即所有 check_* / load / apply_entries 逐条校验时。
  /// @return false 会让该条请求被拒
  ITEM_ALGORITHM_API virtual bool check_item_position(const PROJECT_NAMESPACE_ID::DItemPosition& position) const;

  /// @brief 根据 type_id 查表取道具位置配置
  ///
  /// 被调用时机: 各模式需要道具占格尺寸 / 堆叠上限时 (add / sub / load / apply_entries / 寻位)。
  /// 默认走 ExcelItemType -> UESourceInventory.position_cfg; 业务可覆盖 (如换配置表)。
  /// @return nullptr 表示配置缺失, 调用方按失败处理
  ITEM_ALGORITHM_API virtual const PROJECT_NAMESPACE_ID::DItemPositionCfg* get_item_position_cfg(
      const excel_config_group_ptr_t& config_group, const PROJECT_NAMESPACE_ID::DItemBasic& basic) const;

  // ============================================================
  // 业务接入钩子 (结束)
  // ============================================================

 protected:
  // ============================================================
  // 本模式自己实现的字段规则
  // ============================================================

  /// @brief 道具字段是否与本模式自洽 (占格/不占格、GUID 要求、位置配置存在性等)
  ///
  /// 被调用时机: 基类的 validate_item_basic 先调本函数, 因此所有 check_* / load /
  /// apply_entries 的逐条校验都依赖它; 各模式自己的单条钩子在需要时也可直接调用。
  /// @param basic 待判断的基础数据 (位置字段、GUID、数量都会参与判断)
  /// @return false 表示该条数据不属于本容器 (基类按 EN_ERR_INVALID_PARAM 处理)
  ITEM_ALGORITHM_API virtual bool is_item_valid(const excel_config_group_ptr_t& config_group,
                                                const PROJECT_NAMESPACE_ID::DItemBasic& basic) const = 0;

  // ============================================================
  // 排序键 — 容器按自己的位置语义实现
  // ============================================================

  /// @brief 按道具实例算排序键 (容器按自己的位置语义实现; 条目只保存这个不透明键)
  ITEM_ALGORITHM_API virtual uint64_t make_entry_sort_key(const PROJECT_NAMESPACE_ID::DItemInstance& instance) const;

  // ============================================================
  // 位置读取 — 基类自己的查询 (find_entry) 要用, 所以留在基类
  //
  // 位置索引与 GUID 索引都由各模式自己持有并维护 (挂摘接口在模式类里),
  // 基类只通过下面两个读接口取条目; 无位置模式返回 nullptr。
  // ============================================================

  /// @brief 取锚点位置上的条目 (不跟踪位置时返回 nullptr)
  ITEM_ALGORITHM_API virtual item_entry_ptr_t find_entry_at_position(const ItemGridPosition& position) const;
  /// @brief 按 GUID 取条目
  ITEM_ALGORITHM_API virtual item_entry_ptr_t find_entry_by_guid(int64_t guid) const;
  /// @brief 该 GUID 是否已被本容器占用 (不接受 GUID 的模式返回 false)
  ///
  /// 被调用时机: check_add / check_sub 逐条校验时做 GUID 唯一性判断 (含同批次内已出现过的 GUID)。
  ITEM_ALGORITHM_API virtual bool has_entry_guid(int64_t guid) const;

  // ============================================================
  // 位置字段映射 — 本库不做业务字段映射, 由最上层接入实现
  //
  // 这两个接口是库内唯一接触 DItemGridPosition (proto 位置字段) 的地方:
  // 具体用 user_inventory / character_inventory / character_equipment 哪一个字段,
  // 由接入层的具体容器决定。库内一律用容器自己的 ItemGridPosition 坐标。
  // ============================================================

  /// @brief 把 proto 位置字段读成容器内部坐标 (由接入层按自己的位置字段实现)
  ITEM_ALGORITHM_API virtual ItemGridPosition extract_position(
      const PROJECT_NAMESPACE_ID::DItemGridPosition& position) const = 0;
  /// @brief 把容器内部坐标写回 proto 位置字段 (由接入层按自己的位置字段实现)
  ITEM_ALGORITHM_API virtual void apply_position(PROJECT_NAMESPACE_ID::DItemGridPosition& position,
                                                 const ItemGridPosition& grid_pos) const = 0;

  // ============================================================
  // 单条执行钩子 — 基类负责 checked request 校验与循环, 子类只处理一件
  // ============================================================

  /// @brief 放入一件道具
  ///
  /// 被调用时机: add 逐条循环时, 每条一次。
  /// 基类已确认 checked request 合法 (无错误码 / 未 apply / 容器 GUID 与流水号匹配);
  /// 子类只需在本条上做落位: 合并到已有条目, 或新建条目并挂上自己的索引。
  /// @param checked_request 批次请求
  /// @param instance 本条待放入的道具 (只读; 需要落位时自己拷一份再改)
  /// @return 失败时返回错误码, 基类把它转成失败位置 (failed_type_id) 并结束整个操作
  ITEM_ALGORITHM_API virtual ItemOperationResult on_add_one(ItemAddCheckedRequest& checked_request,
                                                            const PROJECT_NAMESPACE_ID::DItemInstance& instance,
                                                            const ItemOperationContext& context) = 0;

  /// @brief 扣减一件道具
  ///
  /// 被调用时机: sub 逐条循环时, 每条一次。基类的保证同 on_add_one。
  /// @param request 本条待扣减的请求 (只读)
  ITEM_ALGORITHM_API virtual ItemOperationResult on_sub_one(ItemSubCheckedRequest& checked_request,
                                                            const PROJECT_NAMESPACE_ID::DItemBasic& request,
                                                            const ItemOperationContext& context) = 0;

  /// @brief 移动请求的本模式校验 (Phase 0 入参校验 + 批次内占用预演)
  ///
  /// 被调用时机: check_move 完成初始化检查与空请求判断后调用一次; 本模式的 move 语义都在这里。
  /// 失败时把错误码写进 checked_request.result.error_code 并返回 false。
  /// 默认实现: 本模式没有位置语义, 不支持 move (写 EN_ERR_INVALID_PARAM 并返回 false)。
  ITEM_ALGORITHM_API virtual bool on_check_move(ItemMoveCheckedRequest& checked_request) const;

  /// @brief 扣减一条移动来源
  ///
  /// 被调用时机: move 的 kMoveSub 阶段逐条循环, 每条一次 (基类已校验过 checked request)。
  /// 子类在这里扣数量 / 整体移除来源条目, 并维护自己的索引与数量缓存。
  /// @param request 本条来源 (entry + 已填好的坐标 / 占格尺寸)
  /// @return 失败时返回错误码, 基类结束本次 move 并原样返回
  ITEM_ALGORITHM_API virtual ItemOperationResult on_move_sub_one(ItemMoveCheckedRequest& checked_request,
                                                                 const ItemMoveSubRequest& request,
                                                                 const ItemOperationContext& context);

  /// @brief 放入一条移动目标
  ///
  /// 被调用时机: move 的 kMoveAdd 阶段逐条循环, 每条一次 (在全部 kMoveSub 之后)。
  /// 子类在这里合入目标位置的同类条目或新建条目, 并维护自己的索引与数量缓存。
  /// @param request 本条目标 (entry 作为数据来源 + 目标位置)
  /// @return 失败时返回错误码, 基类结束本次 move 并原样返回
  ITEM_ALGORITHM_API virtual ItemOperationResult on_move_add_one(ItemMoveCheckedRequest& checked_request,
                                                                 const ItemMoveAddRequest& request,
                                                                 const ItemOperationContext& context);

  /// @brief 载入一条持久化数据的本模式实现
  ///
  /// 被调用时机: load 通过 (is_operation_allowed / 初始化 / is_item_valid / 容器归属 /
  /// check_item_position / 数量上限) 之后调用一次。
  /// @param item_entry 本条数据, 含条目 ID; entry_id 为 0 时由容器自增分配 (make_entry 的默认行为)
  /// @return false 表示本条载入失败 (基类直接返回 false)
  ITEM_ALGORITHM_API virtual bool on_load_one(const excel_config_group_ptr_t& config_group,
                                              const PROJECT_NAMESPACE_ID::DItemInstanceEntry& item_entry,
                                              const ItemOperationContext& context) = 0;

  /// @brief 应用客户端同步的“删除一条”
  ///
  /// 被调用时机: apply_entries 的删除阶段, 每条已经找到的条目调用一次;
  /// 子类在这里摘掉自己的索引 (位置 / GUID / 位图) 并维护分组与数量缓存。
  /// @param config_group 配置组 (查位置配置用)
  ITEM_ALGORITHM_API virtual void on_apply_remove_one(const excel_config_group_ptr_t& config_group,
                                                      const item_entry_ptr_t& entry,
                                                      const ItemOperationContext& context) = 0;

  /// @brief 应用客户端同步的“一批新增或更新” (批量钩子)
  ///
  /// 被调用时机: apply_entries 的新增/更新阶段, 整批原样交给子类 (基类不落任何临时容器);
  /// 逐条的通用校验由子类调 validate_apply_update_entry 完成, 条目不存在时子类负责新建并挂索引,
  /// 已存在时负责覆盖数据并刷新索引与数量缓存。
  ///
  /// 为什么必须是整批: 同一批里可能出现“两条条目互换位置”(客户端拖动对调时服务端一条 dirty
  /// 下发两条 update_entries)。逐条处理时, 后一条摘自己的旧位置会把前一条刚挂上的新位置索引/
  /// 占用位清掉, 那个格子在容器里就变成“空槽”, 后续 check_move 会放行、下发后才被服务端拒绝。
  /// 所以有位置语义的子类要在钩子内分两阶段: 先统一摘掉这批里位置会变的条目的旧位置,
  /// 再统一写数据 + 挂新位置 (可重复遍历 updates)。
  /// @param config_group 配置组 (查位置配置用)
  /// @param updates      这批要新增/更新的条目 (未校验, 子类自行过滤)
  /// @param context      操作上下文 (原因固定为 kApplyUpdate)
  ITEM_ALGORITHM_API virtual void on_apply_update_batch(
      const excel_config_group_ptr_t& config_group, const item_instance_entry_readable_iterable& updates,
      const ItemOperationContext& context) = 0;

  /// @brief apply_entries 更新条目的通用校验 (entry_id 非 0 + 字段合法且归属本容器)
  ///
  /// 同步包是外部输入, 每个容器在落地前都要过这一关; 抽成基类的 protected helper,
  /// 各模式在自己的 on_apply_update_batch 里逐条调用 (不合格的条目打日志并跳过)。
  /// @return true 表示这条可以落地
  ITEM_ALGORITHM_API bool validate_apply_update_entry(
      const excel_config_group_ptr_t& config_group,
      const PROJECT_NAMESPACE_ID::DItemInstanceEntry& update) const;

  // ============================================================
  // 寻位钩子 — 基类做入参校验与输出预留, 子类实现搜索
  // ============================================================

  /// @brief 为一批道具寻位的本模式实现
  ///
  /// 被调用时机: find_positions_for_instances (及由它实现的 find_positions_for_basics) 在
  /// 校验入参并预留输出空间后调用一次。
  /// @param items        待放置的道具 (只读视图)
  /// @param ignore_item  本次消耗掉、即将从容器移除的道具 (可为空视图)
  /// @param success_item [out] 成功找到位置的道具 (位置已写入 position)
  /// @param failed_item  [out] 未找到位置的道具
  /// @return true 表示寻位流程正常执行; false 表示整体失败 (传参或配置有问题)
  ITEM_ALGORITHM_API virtual bool on_find_positions(
      const excel_config_group_ptr_t& config_group, const item_instance_readable_iterable& items,
      const item_basic_readable_iterable& ignore_item,
      google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& success_item,
      google::protobuf::RepeatedPtrField<PROJECT_NAMESPACE_ID::DItemInstance>& failed_item) const = 0;
  // 钩子结束
  // ============================================================
  // 基类存储的访问辅助与公共收尾 — 供子类的单条钩子使用
  //
  // 条目存储、数量缓存仍由基类独占持有, 子类只能通过这些接口改动。
  // 位置字段映射不在库内 (见 extract_position / apply_position)。
  // ============================================================

  /// @brief 条目数量变化的公共收尾: 刷新数量缓存 + 通知钩子
  ///
  /// 被调用时机: 子类的单条钩子改完条目数量之后调用 (基类的批次流程不直接调)。
  /// 内部会按条目当前内容取 GUID 与位置 (extract_position) 再交给 on_item_count_changed,
  /// 并紧接着触发 on_item_data_changed, 避免各模式重复写这一串。
  /// @param old_count 变更前数量 (新建条目时为 0)
  /// @param new_count 变更后数量 (完全移除时为 0)
  /// @return 该类型的当前总数量 (缓存值, 已包含本次变更)
  ITEM_ALGORITHM_API int64_t notify_entry_count_changed(const item_entry_ptr_t& entry, int64_t old_count,
                                                        int64_t new_count, const ItemOperationContext& context);

  /// @brief 只通知数据变化 (数量未变, 如覆盖 instance / 改了 item_data)
  ITEM_ALGORITHM_API void notify_entry_data_changed(const item_entry_ptr_t& entry, const ItemOperationContext& context);

  /// @brief 某类型道具的实时总数量 (遍历分组求和)
  ///
  /// 被调用时机: 基类的批次校验 (check_sub / check_has 判断总数够不够) 与 get_item_count_debug
  /// 使用; 与 get_item_count 的缓存值交叉验证。
  ITEM_ALGORITHM_API int64_t count_type_total(int32_t type_id) const;

  /// @brief 某类型道具的堆叠上限
  ///
  /// 语义: 配置 accumulation_limit <= 0 视为无限堆叠 (返回 int64_t 最大值);
  /// 取不到位置配置时返回 0 (按不可堆叠处理)。
  ITEM_ALGORITHM_API int64_t get_stack_limit(const excel_config_group_ptr_t& config_group, int32_t type_id) const;

  /// @brief 该条目还能再容纳多少件 basic (0 表示不可再堆叠)
  ///
  /// 判定条件: entry 非空; basic.type_id == entry.type_id; basic.guid == 0 且 entry.guid == 0。
  /// 剩余容量 = get_stack_limit - entry.count - batch_reserved_count (下限 0)。
  /// @param batch_reserved_count 本批次已在该条目上额外预订的数量, 允许为负 (表示本批次已知会释放)
  ITEM_ALGORITHM_API int64_t get_entry_stack_room(const excel_config_group_ptr_t& config_group,
                                                  const item_entry_ptr_t& entry,
                                                  const PROJECT_NAMESPACE_ID::DItemBasic& basic,
                                                  int64_t batch_reserved_count = 0) const;

  /// @brief 该条目能否再容纳 basic 的全部数量 (room >= basic.count() 且 basic.count() > 0)
  ITEM_ALGORITHM_API bool can_stack_to_entry(const excel_config_group_ptr_t& config_group,
                                             const item_entry_ptr_t& entry,
                                             const PROJECT_NAMESPACE_ID::DItemBasic& basic,
                                             int64_t batch_reserved_count = 0) const;

  /// @brief 批次校验的公共单条检查 (各 check_* 与 load / apply_entries 共用)
  ///
  /// 依次检查: is_item_valid (条目字段与本模式自洽) -> 归属本容器 -> check_item_position
  /// (位置字段合法) -> 道具类型存在。函数本身不记日志, 调用方拿到错误码后按自己的下标格式记。
  /// @return EN_SUCCESS 通过; 其他为错误码
  ITEM_ALGORITHM_API int32_t validate_item_basic(const excel_config_group_ptr_t& config_group,
                                                 const PROJECT_NAMESPACE_ID::DItemBasic& basic) const;

  /// @brief 批次操作执行前的 checked request 校验 (错误码 / apply 标记 / 容器 GUID / 流水号)
  ///
  /// 被调用时机: add / sub 的开头, 由基类统一调用; 通过后调用方把 apply 置为 true。
  /// @param check_result check_* 得到的 result (失败时原样返回, 保留 failed_type_id)
  /// @return EN_SUCCESS 表示可以执行; 其他值由调用方直接返回给上层
  ITEM_ALGORITHM_API ItemOperationResult validate_checked_request(const ItemOperationResult& check_result, bool apply,
                                                                  int64_t container_guid, int64_t operate_id,
                                                                  const char* operation_name);

  /// @brief 按当前实例内容刷新条目的排序键 (位置变化后必须调用)
  ITEM_ALGORITHM_API void set_entry_sort_key(const item_entry_ptr_t& entry);

  /// @brief 改写条目数量 (条目只对容器开放可变访问, 子类通过这里改)
  ITEM_ALGORITHM_API void set_entry_count(const item_entry_ptr_t& entry, int64_t count);

  /// @brief 覆盖条目数据 (客户端同步等场景; 位置 / 数量 / GUID 的索引刷新由调用方负责)
  ITEM_ALGORITHM_API void set_entry_instance(const item_entry_ptr_t& entry,
                                             const PROJECT_NAMESPACE_ID::DItemInstance& instance);

  /// @brief 是否已经 init_container
  ATFW_UTIL_FORCEINLINE bool is_initialized() const { return init_; }

  /// @brief 当前是否正在 foreach_instance 遍历中
  ATFW_UTIL_FORCEINLINE bool is_iterating() const noexcept { return iteration_depth_ > 0; }

  /// @brief 迭代期间禁止的容器操作统一入口
  ///
  /// 每个模式的 add / sub / move / load / apply_entries 在入口先调本函数;
  /// 返回 false 表示当前正在 foreach_instance 中, 调用方应直接返回
  /// EN_ERR_INVALID_PARAM (函数内部已记错误日志)。
  /// @param operation_name 操作名, 仅用于日志
  ITEM_ALGORITHM_API bool is_operation_allowed(const char* operation_name) const;

  /// @brief 取一个操作流水号 (供子类构造 checked request)
  ATFW_UTIL_FORCEINLINE int64_t issue_operate_id() const { return ++operate_id_; }

  /// @brief 当前操作流水号 (仅供日志)
  ATFW_UTIL_FORCEINLINE int64_t get_operate_id() const { return operate_id_; }

  ATFW_UTIL_FORCEINLINE PROJECT_NAMESPACE_ID::DItemGridPosition::PositionTypeCase get_position_type() const {
    return position_type_;
  }

  /// @brief 创建条目并登记 entry_id 索引
  ITEM_ALGORITHM_API item_entry_ptr_t make_entry(PROJECT_NAMESPACE_ID::DItemInstance&& instance);
  /// @brief 创建条目并登记 entry_id 索引 (指定 entry_id)
  ITEM_ALGORITHM_API item_entry_ptr_t make_entry(PROJECT_NAMESPACE_ID::DItemInstanceEntry&& instance);
  /// @brief 尝试填充 entry_id 索引 并更新entry
  ITEM_ALGORITHM_API item_entry_ptr_t try_update_entry(const item_entry_ptr_t& entry,
                                                       const PROJECT_NAMESPACE_ID::DItemBasic& basic);
  ITEM_ALGORITHM_API item_entry_ptr_t try_update_entry(const item_entry_ptr_t& entry,
                                                       PROJECT_NAMESPACE_ID::DItemBasic&& basic);

  /// @brief 把条目放进它所属类型的排序分组
  /// @return false 表示同排序键的条目已存在, 该条目没有进分组
  ITEM_ALGORITHM_API bool insert_entry_into_group(const item_entry_ptr_t& entry);
  /// @brief 从它所属类型的排序分组移除条目 (分组空了就删掉分组)
  ITEM_ALGORITHM_API void remove_entry_from_group(const item_entry_ptr_t& entry);

  /// @brief 按 delta 增减某类型的缓存数量
  /// @return 该类型的当前总数量; 结果 <= 0 时清理缓存项并返回 0
  ITEM_ALGORITHM_API int64_t change_cached_item_count(int32_t type_id, int64_t delta);

 private:
  // 不应该再新增以下两个接口的调用处
  void add_entry_id_index(const item_entry_ptr_t& entry);
  void remove_entry_id_index(uint64_t entry_id);

 private:
  bool init_ = false;
  int64_t container_guid_ = 0;
  PROJECT_NAMESPACE_ID::DItemGridPosition::PositionTypeCase position_type_ =
      PROJECT_NAMESPACE_ID::DItemGridPosition::POSITION_TYPE_NOT_SET;

  std::unordered_map<uint64_t, item_entry_weak_ptr_t> entry_id_index_;
  item_group_map_type item_groups_;
  ItemLogHandler log_handler_;
  mutable uint64_t next_entry_id_ = 1;
  mutable int64_t operate_id_ = 0;        // 操作流水号
  mutable uint32_t iteration_depth_ = 0;  // foreach_instance 嵌套深度 (>0 时禁止改容器的操作)
};

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END
