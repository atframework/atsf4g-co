# Agent 指引覆盖记录

输入为 2026-11-28 版《AI Agent 与 Skills 初始化提示词》和《AI 写作规范》。
已完整读取 650 行初始化指南及其结束标记，并读取 677 行写作指南。
本次范围为 atsf4g-co 根工程。子模块维护独立指引。Git 索引已有
`atframework/libatbus` 的暂存修改。

本表在全面刷新时更新，常规开发按任务加载对应 Skill。
实施与运行验证分别记录，父项有未完成的子项时保留其 ID。

## 要求、落点与验证

| ID | 具体要求与适用性 | 落点和验证依据 | 状态 |
| --- | --- | --- | --- |
| C00.1 | 读取完整输入并按独立要求登记 | 核对 C00–C23 和 W01–W06，保留同一份记录供交接 | 已覆盖 |
| C00.2 | 逐条回读产物并核对引用 | 本表及[验证结果](#验证结果)。客户端运行项单独记录 | 已覆盖 |
| C01.1 | 共享规则、按需路径及开发维护路由 | [根指引][root]、[Skill 索引][index]和[源码导航][route] | 已覆盖 |
| C02.1 | 源码、测试及官方材料，区分证实和未核验 | [根指引][root]的 Always-On Rules 与[维护 Skill][maint]的 Verify facts | 已覆盖 |
| C02.2 | 保护用户改动、权限内完成本地验证、外部动作遵守授权 | [根指引][root]与[变更流程][change]。首尾对照暂存的 libatbus gitlink | 已覆盖 |
| C02.3 | 按需加载、工具能力发现、外部文本权限边界 | [根指引][root]与[维护 Skill][maint]。缩短根指引，兼容入口按需查 Skill 索引 | 已覆盖 |
| C02.4 | 强制权限和必要验证在执行层实现 | [维护 Skill][maint]的 Choose one authoritative surface。当前更改为说明文字，未新增 hook 或权限配置 | 已覆盖 |
| C03.1 | 易变结论保留来源、版本、日期、方法、影响和复核条件 | [兼容性来源][compat]、[外部工作流版本][methods]、[客户端检查](#客户端检查) | 已覆盖 |
| C04.1 | 已有结构优先、按需补资源、客户端共享优先复用 | [维护 Skill][maint]、[根指引][root]和 [Claude 兼容入口][claude] | 已覆盖 |
| C05.1 | 区分根规则、Skill、项目角色、强制限制和个人记忆 | [维护 Skill][maint]第 3、6 节保留各自的用途、发现与权限条件 | 已覆盖 |
| C05.2 | VS Code Local 弃用与存量迁移判断 | [兼容性来源][compat]。检查项目路径，现有 [Git hook 模板][commit]由 [hook][hook]读取 | 已覆盖 |
| C06.1 | 核对指南列出的每个客户端，记录版本、采用情况与限制 | [客户端检查](#客户端检查)区分仓库适配器、文档核验和实际加载 | 已覆盖 |
| C06.2 | 核验 Codex、Kilo、Pi 从根目录和子目录发现项目技能 | 三个 CLI 均发现 13/13 个 Skill。Codex 的模型输入还包括更新后的根指引 | 已覆盖 |
| C06.3 | 其余客户端加载、同名遮蔽与无副作用调用 | Oh My Pi 的本机扩展加载受限，未得到 Claude Code 或 VS Code harness 的运行结果。未构造同名配置或运行模型调用 | 阻塞 |
| C07.1 | 根层保留非默认工程约束、生成边界与分任务导航 | [根指引][root]的 C++14、通知校验及测试目标约束。保留 RPC 特例入口，详情见 [RPC 测试][rpc] | 已覆盖 |
| C07.2 | 构建、测试、lint、平台约定与实际入口 | [构建][build]、[测试][testing]、[RPC 测试][rpc]、[工程验证][engineering]和[终端 Skill][shell] | 已覆盖 |
| C08.1 | Claude 导入、遮蔽、个人记忆、Skill 发现和去重 | [Claude 入口][claude]只导入根规则，索引按需打开。版本和 `CLAUDE.local.md` 影响见[兼容性来源][compat] | 已覆盖 |
| C09.1 | 标准六字段、按需引用、触发边界与独立执行权限 | [维护 Skill][maint]第 4 节与[各 Skill](#skill-职责)。13 个现有描述未调整触发范围 | 已覆盖 |
| C09.2 | Skill 脚本合同、供应链和固定版本审查 | [维护 Skill][maint]第 4 节。无新第三方 bundle 或 Skill 脚本 | 已覆盖 |
| C10.1 | 触发与结果分别评估，保留近似查询及对照方法 | [维护 Skill][maint]第 4 节和[人工样例](#人工样例)。未改描述，无需为描述另建调优集 | 已覆盖 |
| C10.2 | 新旧 Skill 的独立模型运行对照 | 缺少可用的隔离调用、轨迹和同模型对照结果。静态样例不能替代 | 阻塞 |
| C11.1 | 自定义角色、schema、权限、隔离与自包含交接 | [维护 Skill][maint]第 3 节。未发现需要专用工具权限或持久化角色的任务 | 已覆盖 |
| C11.2 | 新建自定义角色与验证其权限 | 项目没有待创建的独立角色，也无任务授权要求并行委派 | 不适用 |
| C12.1 | 轻量、缺陷、高风险变更及歧义分流，保留单一验收依据 | [变更流程][change]。去掉固定失败次数后索权的规则，以可诊断新信息和真实阻塞判断 | 已覆盖 |
| C12.2 | OpenSpec v2 与 Superpowers v7 的适用范围和替代流程 | [版本说明][methods]。项目未发现现成 OpenSpec 或 Superpowers 流程，使用现有 Issue、规格或执行计划 | 已覆盖 |
| C12.3 | 安装、迁移或运行可选工作流 | 本次保留项目原生流程，没有采用或迁移任务 | 不适用 |
| C13.1 | MCP 来源、版本、端点、工具元数据、限时与进程清理 | [MCP Skill][mcp]、[兼容性来源][compat]、[工具包 README][mcp-readme]及固定版本源码 | 已覆盖 |
| C13.2 | MCP HTTP/OAuth 与兼容性验收边界 | [兼容性来源][compat]记录适用条件。当前集成为 stdio，不引入 HTTP 认证或协议升级 | 已覆盖 |
| C13.3 | 新传输、认证或 MCP 版本的运行验收 | 本次未改服务实现、工具行为、客户端配置或 SDK | 不适用 |
| C14.1 | 临时产物、密钥传递、日志脱敏与清理 | [根指引][root]、[终端 Skill][shell]和[部署 Skill][deploy]。沿用配置中的构建树 | 已覆盖 |
| C14.2 | 部署目标与制品、迁移、健康检查、回滚和分环境验收 | [部署 Skill][deploy]的 Render and validate。基于 [渲染流程][render]，无发布任务 | 已覆盖 |
| C14.3 | CI 最小权限、短期凭据、依赖固定及不可信输入隔离 | [工程验证][engineering]的 CI and release changes。现有文档部署 action 有版本标签，本次为指引更新 | 已覆盖 |
| C15.1 | 风险对应测试、真实边界和失败证据 | [根指引][root]、[变更流程][change]、[普通测试][testing]和 [RPC 测试][rpc] | 已覆盖 |
| C15.2 | Markdown、链接、命令、资料及图示验证 | [工程验证][engineering]与[站点 Skill][docs]，本次结果见[验证结果](#验证结果) | 已覆盖 |
| C15.3 | C++、部署或已发布站点运行测试 | 改动仅涉及 AI 说明和 Git hook 提示词，没有代码、模板渲染或 Docusaurus 内容变化 | 不适用 |
| C16.1 | 等待与超时、有限重试、幂等、诊断和非交互限制 | [终端 Skill][shell]的 Timeouts and retries 及 PowerShell authoring rules | 已覆盖 |
| C17.1 | 现代 CLI 首选、完整候选清单与回退 | [根指引][root]、[终端 Skill][shell]和 [31 项工具表][cli]。指南表内有 30 个条目，项目保留原有 `lnav` 为第 31 项 | 已覆盖 |
| C17.2 | 实际工具版本、Windows shell、编码和 Git 跨环境传输 | [工具探测][cli]、[根指引][root]的 Terminal and Tooling。已记录六项常用工具 | 已覆盖 |
| C18.1 | docs、ADR、路线图、规格与计划按职责更新 | [源码导航][route]、[变更流程][change]及其验收和续接步骤。无业务事实变化需要改站点或路线图 | 已覆盖 |
| C18.2 | 写作与提交说明、同步术语和语言 | [根指引][root]、[写作指南][writing]和 [Git hook 提示词][commit] | 已覆盖 |
| C19.1 | 从可复现失败维护规则、按影响选择检查或 Skill | [维护 Skill][maint]的 Retain verified lessons。检查硬编码三次失败规则和全量加载的冗余 | 已覆盖 |
| C19.2 | 客户端自动记忆与团队规则，第三方 Skill 审计 | [维护 Skill][maint]第 4、6 节。无第三方候选采购或安装请求 | 已覆盖 |
| C20.1 | 收尾检查、结果分类和真实未完成项 | 本表、[维护 Skill][maint]的 Validate and report 以及[验证结果](#验证结果) | 已覆盖 |
| C21.1 | 审查、分配、落地、复核的完整更新流程 | 本记录、[维护 Skill][maint]的分阶段流程及改动文件 | 已覆盖 |
| C21.2 | 首次配置目录、hooks、工作流或 MCP 初始化 | 此仓库已有工作入口和脚本，任务是维护已有指引 | 不适用 |
| C22.1 | 只保留采用结论的原始来源、日期、版本和限制 | [兼容性来源][compat]、[外部工作流版本][methods]和[写作指南][writing]的 References | 已覆盖 |
| C23.1 | 保持主题、工具偏好和稳定 ID，按需复核 | 本记录与[维护 Skill][maint]。后续使用相同记录，不归档成多套状态表 | 已覆盖 |
| W01 | 保留事实强度、作者判断、读者需要和语用习惯 | [写作指南][writing]的 Core rules、Semantic preservation | 已覆盖 |
| W02 | 标题、入口、图示、语言和连续论述 | [写作指南][writing]及[站点 Skill][docs]。保留必要顺序、锚点与技术术语 | 已覆盖 |
| W03 | 词语、句式、真实比较及上下文检查 | [写作指南][writing]的 Sentence relationships、Formulaic prose 和 Chinese wording cues | 已覆盖 |
| W04 | 注释、接口、命令、日志、提交记录的语义和格式 | [写作指南][writing]的 Semantic preservation 与 [Git hook 提示词][commit] | 已覆盖 |
| W05 | 局部修订、停手条件、反例与新旧版本评估方法 | [写作指南][writing]的 Review、Writing evaluation 及[人工样例](#人工样例) | 已覆盖 |
| W06 | 同模型写作对照及读者盲评 | 缺少可控的独立模型运行与读者样本结果 | 阻塞 |

## 客户端检查

安装版本为 Windows 上的探测结果。适配器存在只证明项目支持生成配置。
无使用记录的客户端未列入本次桥接或运行目标。

| 客户端 | 仓库或会话依据 | 已执行核验与界限 |
| --- | --- | --- |
| Codex | 当前桌面会话可见 13 个项目 Skill，CLI `0.171.0-alpha.13.2` | `codex debug prompt-input` 在两个目录返回更新后的根指引和 13/13 个项目 Skill 的描述。首次沙箱探测返回错误 5，获准重试后成功 |
| Claude Code | 项目有 [CLAUDE.md][claude]，未发现可用 CLI | 查阅官方发现规则，回读导入与相对链接。实机加载未执行 |
| VS Code Copilot | CLI 报告 `1.148.0`，仓库有 VS Code 配置和 MCP 适配器 | 版本探测同时出现 EPERM，当前 Copilot、Claude、Codex、Local harness 的启用情况未确认 |
| Kilo Code | CLI `7.4.20`，本地有 Kilo 配置 | `kilo debug skill --pure` 在两个目录列出 13/13 个 Skill。首次沙箱探测写日志受限，获准重试后成功 |
| Pi | CLI `1.6.1` 与 MCP 适配器 | 获准使用隔离配置离线运行 RPC `get_commands`，从根目录和 `src/server_frame` 各发现 13/13 个 Skill |
| Oh My Pi | CLI `19.2.1` 与 MCP 适配器 | CLI 在提取本机扩展时收到 EPERM |
| OpenCode | 工具包有适配器 | 没有当前仓库启用证据 |
| Command Code | 没有当前仓库启用证据 | 无需专用目录或运行核验 |
| Zoo Code | 工具包已有 zcode 适配器 | 没有当前仓库启用证据 |
| OpenClaw | 没有当前仓库启用证据 | 无需专用目录或运行核验 |
| Hermes Agent | 没有当前仓库启用证据 | 无需专用目录或运行核验 |
| Devin Desktop | 已核对官方 Devin Local 文档 | 没有当前仓库启用证据 |
| Antigravity | 没有当前仓库启用证据 | 无需专用目录或运行核验 |

## Skill 职责

按 [Skill 索引][index] 查入口。专项 Skills 复用各自的代码、测试和文档来源。

| Skill | 核对的用途及边界 |
| --- | --- |
| [ai-agent-maintenance][maint] | 根规则、Skill 元数据、兼容性、精简和验收 |
| [atgateway-protocol](../../atgateway-protocol/SKILL.md) | atgateway v2 协议、握手、压缩及重连。常规路由不触发 |
| [build][build] | CMake 构建、配置和编译或链接失败 |
| [business-logic](../../business-logic/SKILL.md) | 指定业务模块调试。通用 RPC、DTMQ 或部署交给专项流程 |
| [change-workflow][change] | 缺陷和非平凡行为变更。局部文案走短路径 |
| [configure-expression](../../configure-expression/SKILL.md) | `enable_expression` 注解和变量展开。普通 YAML 不触发 |
| [deployment-config][deploy] | 模板、配置和生成脚本校验。纯运行部署不触发 |
| [docs-site][docs] | Docusaurus 内容、双语与主题。根 README 或 Skill 不触发 |
| [engineering-guidelines](../../engineering-guidelines/SKILL.md) | C++、CMake、protobuf、生成源及兼容性 |
| [mcp-integration-maintenance][mcp] | 工具包、安装器、配置生成器和索引。日常搜索不触发 |
| [rpc-unit-test][rpc] | 项目 RPC 和异步 hook 测试。普通测试走 testing |
| [shell-tooling][shell] | shell、工具与进程诊断。构建和测试流程由相应 Skill 管理 |
| [testing][testing] | 普通测试的设计、过滤、运行和诊断 |

## 人工样例

本次仅检查目标规则能否给出合适的处理方向。没有记录为模型调用或质量测试通过。

| 输入 | 规则要求的处理 |
| --- | --- |
| 新增 RPC 测试调用 `register_handles_for_<service>()` | 按项目 helper 连私有库，仅补生成的注册 TU，路径通过 helper 取得 |
| 维护 MCP wrapper 的工具 schema | 走 MCP Skill，复核 SDK、传输和 metadata。普通 CodeGraph 查询不触发维护 |
| 已授权的离线单测失败 | 核对失败原因，修复和复测。命令返回会话 ID 时继续检查该会话 |
| 首次调用含在最多 3 次尝试内 | 复核两次重试的上限，保留计数语义 |
| “先关闭连接，再释放缓冲区”，以及返回只读视图的 API | 保留真实时序与调用限制 |
| 未运行的验证和已失败的验证 | 记录各自的实情，避免将未知写成失败 |
| 同页两个标签执行相同下载 | 合并重复入口，保留不同操作，再核对中英文标签与路径 |

## 验证结果

- 用 Mike Farah `yq` 解析全部 13 个 Skill 的 YAML，核对六字段范围、
  目录名、描述长度及与 HEAD 的路由元数据一致性。
- 核对 17 个改动 Markdown 文件、151 个本地引用和锚点。
  检查源码导航表中的路径和 Claude 导入，运行 `git diff --check`。
- `markdownlint-cli2` 默认配置执行了 48 个文件。长行诊断单独记录，
  因为仓库 `.editorconfig` 的 Markdown 段未限制行长。按该约定
  对照 HEAD 与改动后的诊断，两者均有同样的 19 条既有问题。
  涉及表格对齐、保留尾空格的代码字面量及无标题的 hook 提示词。
- Git hook 模板五个替换标记及顺序已与原版本比对。未实际运行
  提交 hook 或调用模型。无 C++、部署或站点构建变更。
- 复核 Git 索引，`atframework/libatbus` 的暂存对象仍为
  `963a625266ae679339e537700b092455eb6e2369`。

运行验收还缺 C06.3、C10.2 和 W06。复测时用目标客户端的干净会话
核对加载和调用轨迹，再用同模型、同材料对照旧版和新版产物。
保留模型、输入、输出、边界样例和评审结果。

[root]: ../../../../AGENTS.md
[claude]: ../../../../CLAUDE.md
[route]: ../../../source-index.md
[index]: ../../README.md
[maint]: ../SKILL.md
[compat]: compatibility-sources.md
[writing]: writing-guidance.md
[methods]: ../../change-workflow/references/upstream-methods.md
[change]: ../../change-workflow/SKILL.md
[build]: ../../build/SKILL.md
[testing]: ../../testing/SKILL.md
[rpc]: ../../rpc-unit-test/SKILL.md
[engineering]: ../../engineering-guidelines/review-and-validation.md
[shell]: ../../shell-tooling/SKILL.md
[cli]: ../../shell-tooling/references/modern-cli-tools.md
[docs]: ../../docs-site/SKILL.md
[mcp]: ../../mcp-integration-maintenance/SKILL.md
[mcp-readme]: ../../../../project/integration/mcp/README.md
[deploy]: ../../deployment-config/SKILL.md
[render]: ../../deployment-config/references/rendering-workflow.md
[commit]: ../../../../ci/git-commit-message.prompt.md
[hook]: ../../../../ci/git-hook-prepare-commit-message.sh
