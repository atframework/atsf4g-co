# Business Skill Routing Checks

Use only when maintaining the business Skill's scope, module layout, or loading conditions.

## Design basis

Official guidance checked on 2026-09-30; recheck the relevant
[compatibility sources](compatibility-sources.md) before relying on mutable client behavior.

- Skill metadata is discovery context; concise, specific descriptions help select the right workflow.
- Keep the entrypoint to routing and necessary shared constraints. Read domain details only when the task needs them.
- Link each module reference directly from `business-logic/SKILL.md`, even when files live in module subdirectories.
  Add a short contents list to long references. Avoid chains of indexes and duplicate authoritative content.
- The single business entrypoint is this repository's organization choice. Official guidance supports domain grouping
  and progressive disclosure; it does not require every repository to merge its business Skills.
- Add future business guidance under `business-logic/references/<module>/` and update the module index. Keep descriptions
  representative of actual coverage without listing every API. Do not create module `SKILL.md` aliases that restore
  separate discovery entries. Shared engineering/build/test procedures retain their own Skills.

## Manual routing cases

Review descriptions first, then check the entrypoint's expected reference selection. These cases are an authored
boundary check, not a measured invocation rate. Measure real selection separately only when a client exposes it.
Review dated baseline sections only for relevant matchmaking reviews or fixes, and verify findings in current source.

| Should select business guidance | Module reference(s) |
| --- | --- |
| 修复 matchsvr 阵营容量分配不满足模板的问题 | Matching |
| 审查匹配迁移时 WAL switch/remove/add 的通知顺序 | Matching |
| 修改 Matching.xlsx 后检查生成配置是否遗漏字段 | Matching |
| 验证匹配关卡候选交集与 Orbit 最终关卡一致 | Matching |
| 为匹配确认超时和建战斗回调失败补回归测试 | Matching |
| 检查 Lobby 首次拉取组队数据前后的 dirty 推送 | Team |
| 修复邀请过期后 pending 列表没有移除的问题 | Team |
| 审查旧队伍对象迟到回调是否误删当前队伍 | Team |
| 为 Team Room 入队权限和成员丢失修复补测试 | Team |
| 排查队伍 ready/start-matching 回调与匹配状态不一致 | Team and Matching |

| Should not select business guidance | Route instead |
| --- | --- |
| 修复通用 RPC dispatcher 的超时处理 | Engineering/change workflow |
| 检查 DTMQ subscriber 的通用重连机制 | Engineering/change workflow |
| 为 teamsvr 目标排查 CMake 链接失败 | Build |
| 只运行 matchsvr 已有测试并汇报结果，不分析业务行为 | Testing/build as needed |
| 修复 orbitsvr 的通用路由缓存，不涉及匹配交接 | Engineering/change workflow |
| 修改 lobbysvr 日志轮转配置 | Owning configuration source |
| 新增普通算法单元测试目标 | Testing |
| 修改 Helm 副本数和资源限制 | Deployment config |
| 维护 Team 文档在 Docusaurus 中的侧栏位置 | Docs site |
| 优化业务 Skill 的索引结构与描述，不修改业务内容 | AI agent maintenance |

## Migration validation

Check frontmatter and folder/name equality, old-path references, relative links, source paths, and Markdown formatting.
Check that only one business `SKILL.md` is discoverable and every module reference is directly linked with a load condition.
Compare migrated business constraints with their original content; file moves must not drop invariants or test boundaries.
Report entrypoint size and metadata size separately. Byte/line reductions are not measured token savings or invocation
quality. Do not run C++ builds or claim business-test coverage for guidance-only changes.
