# 通用工作空间 MCP 集成（tgrep / CodeGraph）

本目录提供两个面向 AI Agent 的 stdio MCP 服务，安装时二选一，均只作用于安装时指定的工作空间：

- **tgrep**（`tgrep/`）：快速文本/正则检索，后端为打过 stdio 传输补丁的固定版本
  [microsoft/tgrep](https://github.com/microsoft/tgrep)。
- **CodeGraph**（`codegraph/`）：结构化代码导航（符号、引用、影响面），后端为固定版本的
  [CodeGraph](https://github.com/colbymchenry/codegraph) direct 模式（禁 daemon、禁遥测）。

公共库与流程工具在 `common/`；
Agent 配置组件（**每产品一个独立配置器模块** `agents/src/agents/*.mjs` + 共享基类
`base.mjs`、纯变更规划、安全文件存储、批量写入）在 `agents/`；
各组件目录独立（源码、测试、包管理信息、上游固定清单都在自己目录内，
`agents/vendor/jsonc-parser/` 随源码分发）。新增一个 Agent 通常只需新增一个配置器模块
并在 `agents/src/agents/index.mjs` 注册。

## 安装（推荐入口）

```bash
node <MCP_DIR>/setup.js
```

先进入 Agent 打开的工程目录或其子目录，再运行命令。`<MCP_DIR>` 是这套工具的实际目录，
可以位于工程之外，也可以改名、移动或复制，无需改源码。

`--repo-root=<dir>` 优先指定工作空间。未指定时，从执行目录向上寻找最近的工程标记：
`.uproject`、`.sln`/`.slnx`、`.git`（目录或 worktree 文件）、`.hg`、`.svn`、`.p4config`、
`Cargo.toml`、`go.work`/`go.mod`、`pyproject.toml`、有效 `package.json` 或含字面量 `project()`
的 `CMakeLists.txt`。工具自身的 npm 包不作为宿主工程。没有这些标记时采用最近的
`.p4ignore`；仍未找到则使用执行目录。只检查本地文件，不调用 Git/P4 服务或执行工程脚本。
安装器打印选定目录及检测依据；若希望配置版本库内一个没有独立标记的子目录，显式传 `--repo-root`。

工程显示名依次取唯一 `.uproject` 文件名、npm 包名、CMake 字面量工程名、Cargo/Python/Go
清单名称、唯一解决方案名，最后使用目录名。无法解析的名称不执行求值。索引目录使用经过
规范化的工程名；客户端服务键固定为通用的 `workspace-tgrep` / `workspace-codegraph`。
新配置始终带绝对 `--repo-root`；wrapper 的工作空间不随 IDE 的启动 cwd 改变。
旧的无此参数配置从 wrapper 所在工具的外层工程推导，找不到时才回到执行目录检测。

`--build-dir=<dir>` 可显式选择工作空间内的缓存目录；缺省依次读取 `.vscode/settings.json`
的 `cmake.buildDirectory`、`clangd.arguments` 中的 `--compile-commands-dir=`，再检查根目录
直接子目录中唯一且属于本工程的 `CMakeCache.txt`。否则 UE 工程使用 `Intermediate/AI/MCP`，
其他工程使用 `build`。支持 JSONC、`${workspaceFolder}` 和 `${sourceDir}`；不猜测其他变量。
依赖安装在工具副本中，上游制品、配置备份和运行状态放在当前工作空间的构建目录中。
缓存目录不能等于工作空间根目录，也不能通过 junction/symlink 指到工作空间之外。

旧工程前缀的服务键和 TOML 托管标记在验证命令路径、工作空间后迁移。参数、注释、TOML 子表
和字符串原文保留；新旧服务键同时存在时中止整批写入。Cline 迁移到 `.cline/mcp.json`，
将旧导出中的自定义服务器同时带入新文件，旧文件中非托管内容保留。旧的 JSON/Cordis 导出
仅在完全符合历史生成格式且指向当前工具时重新生成；额外字段或修改过的内容仍中止写入。
改换工具目录后，指向其他副本的条目不作为本次工具接管，需先处理旧条目。

安装器会依次完成：选择检索后端（二选一）→ 选择下载镜像 → 准备本地依赖（全部固定版本）
→ 依赖就绪后把 MCP 服务写入所选 Agent 的**项目级**配置。依赖准备失败时不会改动任何
Agent 配置。可重复执行，用于切换后端、升级固定制品、为更多 Agent 接入或卸载。

常用选项：`--backend=tgrep|codegraph`、`--agents=<id,...>|all`（`all` 只含可自动写入项目配置的
目标，面板导入类需显式指定）、`--mirror=cn|official`（未指定时按系统地区建议）、`--offline`、
`--skip-prepare`（仅调整 Agent 配置）、`--uninstall [--agents=...|--all-agents]`、`--yes`、
`--ui=line`（交互菜单用编号行模式，缺省在支持的终端用方向键）、`--dry-run`、`--help`、
`--list-agents`。

交互菜单：支持的终端使用方向键（↑/↓ 移动、空格勾选、回车确认，多选 a 全选、n 清空，
Esc/Ctrl+C 取消，Agent 配置保持不变，已准备的依赖保留）；不支持时回退编号行输入。安装时空选择不会静默卸载——会先列出
将移除的已配置条目并要求 y/N 确认；非交互模式（`--yes`/CI/非 TTY）不打开交互输入，
未使用 `--yes` 且缺少后续菜单需要的选项时，在准备依赖前报错。
`--yes` 可沿用上次准备的后端与已有配置/导入片段选择。菜单结束会释放 stdin；EOF、流错误与取消
不会被视为默认确认，选项较多时只显示当前终端容得下的部分，保持当前选项可见。

`--dry-run` 完全只读：打印拟准备的依赖与拟写入/移除的配置动作，但不下载、不安装、
不写状态文件、不写任何 Agent 配置。`--help` 与 `--list-agents` 无副作用，后者列出
可配置 Agent 的 id、说明与配置文件位置。未知 Agent id 与非法参数值在任何下载或
交互前报错。

**损坏配置保护**：任一目标配置文件损坏（JSON 语法错误、根或服务器表不是对象、重复键、
Codex 托管标记未闭合或重叠）时，安装器会中止本轮**全部**配置写入并保持该文件原字节，
需先手工修复后重跑。带注释/尾逗号的 JSONC 是合法输入：编辑采用 Microsoft
jsonc-parser 的局部修改（`agents/vendor/jsonc-parser/`，固定 3.3.1），注释、外来条目、
键序与换行风格保留；插入/删除处可能留下空白，已有条目不经整段格式化。重复配置保留已选服务的
可选字段和额外参数。同名但 command/args 不指向本仓库包装层的主配置条目会中止写入。
每次写入前会与规划时的原文比对，期间被外部修改即中止；
写入前原文备份到 `<BUILD_DIR>/_agent_tmp/mcp/agent-config-backups/`，中途失败自动回滚
已写文件，包括写入后状态/日志失败的那个文件；逐项报告未恢复文件和备份位置，单个恢复失败不阻断
其他文件恢复。用户后续修改或删除的文件不被回滚覆盖。临时文件位于构建目录；构建目录与目标跨文件系统
导致 rename 失败时中止并尝试恢复本轮已写配置。仅当归属记录
（`<BUILD_DIR>/integration/mcp/state/agent-config-state.json`）证明文件由本安装器创建、
且文件标识仍匹配、卸载后只剩空托管骨架时才整体删除该文件；预存在的空对象不会被删。
旧归属记录若没有文件标识，则保留卸载后的空文件。路径检查也用于预览和恢复，拒绝仓库外链接及
配置文件本身的符号链接。

**崩溃后自动恢复**：每次写入批次在操作日志（`<BUILD_DIR>/_agent_tmp/mcp/agent-config-journal.jsonl`）
中记录开始、逐文件计划（含目标内容摘要、原文件权限与标识）、结束。安装器被强杀等中断后，
下一次运行在依赖准备成功后、读取 Agent 默认选择前恢复；卸载则直接进入恢复。确认原进程已退出后，
按备份把涉及的文件回滚到批次前状态，打印恢复摘要，再继续本次操作。同一文件的多次修改也恢复到
批次开始前。配置写入和恢复共用互斥检查，已有安装或恢复进程活动时中止。

归属备份在替换配置前记录；恢复替换前记录新文件标识，使恢复再次中断后仍能继续。归属记录仅保留
身份可验证的文件，用户后来替换的文件即使内容与原文相同，也不会因此取得安装器归属。缺失归属备份时
清除受影响文件的归属，保留其他文件的记录；清除失败则中止，不继续写配置。
崩溃后被编辑、删除或路径不安全的文件会被跳过，输出原因与备份目录。`--dry-run` 只提示待恢复批次，
不执行恢复。完整日志行的语法或必需字段损坏会中止；只有未以换行结束的残缺尾行可以在确认无活动写入后丢弃。
Windows 与 WSL 的进程号不能互相验证；带平台记录的中断批次需回到原平台恢复。此恢复针对进程中断，
不承诺机器断电后的落盘顺序。

### CodeGraph 提示词补丁

选择 `--backend=codegraph` 并写入至少一个 Agent 项目配置时，安装器同时更新根 `AGENTS.md` 中的
`<!-- CODEGRAPH_START -->` / `<!-- CODEGRAPH_END -->` 段落。`CLAUDE.md` 继续引用根说明，
不复制这段正文。提示词模板在 `agents/src/guidance/codegraph.mjs`。

若标记间正文相同，忽略文件编码和换行差异后不写回，mtime、备份和日志也不变。支持 UTF-8
（可带 BOM）、带 BOM 的 UTF-16 LE/BE，以及 LF/CRLF/CR 换行。更新时保留原编码、BOM、
标记行和块外内容；没有标记就追加一个块。重复、反向、未闭合标记或无法可靠解码的文件会中止
整个配置批次，Markdown 围栏中的示例不算托管标记。`--dry-run` 只显示拟更新动作。

提示词引导 Agent 在已有可用索引时优先探索结构，并在不可用、忙碌或覆盖不足时回退源码检索。
它不授权自行建立、重建或解锁索引，也不保证完整动态派发分析。切换 tgrep 或卸载 MCP 时保留
这段条件式说明和索引；已有同版本本地 CLI 仍可使用它。

### 前置条件

- Node.js >= 20 + npm（包装层与安装器；Windows 开发机 Node 24、WSL/Debian Node 20
  均已验证）。CodeGraph 的首次索引与后端服务运行在平台包捆绑的 Node 24 上，不依赖
  系统 Node 版本（其库需要 Node 22.5+ 的内置 `node:sqlite` 模块）。
- Git；选择 tgrep 后端还需要 Rust 工具链（edition 2024，rustc 1.98 验证过）。
- 首次准备需要联网拉取固定版本制品；之后可用 `--offline` 离线复用。
- 国内网络建议 `--mirror=cn`（npm 用 npmmirror、cargo 用 rsproxy；tgrep 源码仍从
  GitHub 固定提交拉取并校验提交号）。注意 npmmirror 尚未同步 CodeGraph 的平台包
  （`@colbymchenry/codegraph-<platform>-<arch>` 1.6.0），选择 codegraph 后端时若
  `cn` 镜像报“无匹配版本”，请改用 `--mirror=official`。安装器不会修改全局 npm/cargo 配置。

## 支持的 Agent

以下 Agent 的配置由 `setup.js` 自动写入（均为仓库内项目级文件，保留其中已有的其他条目；
`--list-agents` 查看全部可选 id）。条目形状均按各家官方文档核验（2026-09-20，含实施前实时复核）：

| Agent | 配置文件 | 格式要点 |
| --- | --- | --- |
| Claude Code / pi / CodeBuddy Code CLI（共用一个文件） | `.mcp.json` | `mcpServers`；选任一 id 即保留整组，卸载任一 id 影响全组（输出会标注） |
| Codex | `.codex/config.toml` | `[mcp_servers.<name>]` + `cwd`；CLI 与 IDE 扩展共用 |
| GitHub Copilot（VS Code / Visual Studio） | `.vscode/mcp.json` | `servers` + 绝对路径；共用 id `vscode` |
| Cursor | `.cursor/mcp.json` | `mcpServers` |
| Gemini CLI | `.gemini/settings.json` | `mcpServers` |
| OpenCode | `opencode.json` 或 `opencode.jsonc` | `mcp` / `type:local` / `command` 数组 |
| Kilo Code（id `kilo`，兼容旧 id `kilocode`） | `.kilo/kilo.json(c)`；根 `kilo.json(c)` 的托管条目迁入此目录 | 同 OpenCode 形状；根与目录配置可分层共存 |
| Roo Code | `.roo/mcp.json` | `mcpServers` |
| ZCode | `.zcode/config.json` | `mcp.servers` |
| oh-my-pi (omp) | `.omp/mcp.json` | `mcpServers` |
| Zed | `.zed/settings.json`（JSONC，保留注释） | `context_servers` / 扁平 `command` + `args` |
| Kimi Code | `.kimi-code/mcp.json` | `mcpServers` + `cwd` + 相对 args；使用当前 Kimi Code 运行时 |
| Qwen Code | `.qwen/settings.json` | `mcpServers` + `cwd` + 相对 args |
| WorkBuddy | `.workbuddy/mcp.json` | `mcpServers` |
| TRAE / TraeCode（IDE 与 CLI） | `.trae/mcp.json` | `mcpServers`；IDE 需启用项目级 MCP |
| MiMo Code | `mimocode.json(c)` 或 `.mimocode/mimocode.json(c)`；新建默认后者 | 同 OpenCode 形状 |
| Cline CLI | `.cline/mcp.json`（安装器自定义导出文件） | `mcpServers`；Cline 不自动发现它，需经下方启动器显式接入 |

新增接入方式按 2026-09-22 官方文档核对：

| 接入项 | 生成内容和启用方式 |
| --- | --- |
| `vscode` | 保留旧 id；别名 `copilot`、`copilot-vscode`、`copilot-visual-studio`、`visual-studio`。Visual Studio 2022 17.14+ / 2026 可发现解决方案目录的 `.vscode/mcp.json`，因此共用文件和代码；使用双方均支持的绝对参数路径，避免依赖 VS Code 变量。见 [Microsoft 文档](https://learn.microsoft.com/en-us/visualstudio/ide/mcp-servers?view=visualstudio)。 |
| `copilot-jetbrains` | Rider 与其他 JetBrains IDE 的 Copilot 插件共用一个接入项，别名 `copilot-rider` / `rider-copilot`。生成 `servers` JSON，按 Copilot Chat → Agent → Add MCP Tools 导入插件打开的配置；有 Workspace 选项时选择当前工作区。官方已支持工作区服务器，但未在配置文档明确稳定的自动写入路径，安装器不猜测内部路径。见 [配置文档](https://docs.github.com/en/copilot/how-tos/provide-context/use-mcp-in-your-ide/extend-copilot-chat-with-mcp?tool=jetbrains) 与 [工作区支持公告](https://github.blog/changelog/2026-07-07-codex-as-agent-provider-and-agentic-enhancements-in-jetbrains-ides/)。 |
| `jetbrains-ai` | Rider 等 IDE 的 AI Assistant；别名 `rider-ai` / `rider`。生成 `mcpServers` JSON，进入 Settings → Tools → AI Assistant → MCP → Add → STDIO，Server level 选当前项目。与 Copilot 插件是两个独立入口。见 [JetBrains 文档](https://www.jetbrains.com/help/ai-assistant/mcp.html)。 |
| `trae` | IDE 与 CLI 共用 `.trae/mcp.json`，别名 `trae-ide` / `trae-cli` / `traecode`。IDE 需打开 Settings → MCP → 启用项目级 MCP。见 [IDE 文档](https://docs.trae.cn/ide_add-mcp-servers) 与 [CLI 文档](https://docs.trae.cn/cli_model-context-protocol)。 |
| `dsh` | DeepSeek Harness，别名 `deepseek-harness`。生成 `dsh-mcp.cordis.yml`，使用官方 `@deepseek-ai/dsh-mcp-client` 的 `insert/config/serverName/transport` 结构。按安装器输出执行 `dsh web --patch "<生成路径>"`；当前 profile 必须能解析匹配宿主版本的插件。见 [官方接入示例](https://github.com/deepseek-ai/deepseek-harness/blob/master/docs/user/guide/mcp-memory.md)。 |

三个显式导入项（`copilot-jetbrains`、`jetbrains-ai`、`dsh`）的文件在
`<BUILD_DIR>/integration/mcp/exports/`，与已有 Cline IDE / CodeBuddy IDE 共用规划、
损坏检测、备份、回滚和卸载代码。它们不在 `--agents=all` 内，须显式选择；`--agents` 仍表示
本次最终集合，添加时请同时列出需要保留的其他客户端。DSH 的 `--patch` 是原生显式加载，
不会覆盖用户 YAML 或自动安装 DSH 插件。移除导出文件后，已导入 IDE 的服务器仍需在 IDE 中移除。
这些客户端的配置生成与本地包装层测试不代表已验收登录后的 GUI 会话。

### Unreal Engine / Perforce 工作空间

可从 P4 根目录、UE 解决方案目录或含 `.uproject` 的游戏目录调用同一工具副本。配置与查询范围
以执行目录为准；Visual Studio 应从 `.sln` 所在目录运行，游戏 Agent 则从游戏目录运行。
不依赖 `.git`，安装器不会执行 `p4 sync/edit/reconcile/submit`，也不修改 P4 全局设置。

识别到 `Engine/Source`、`Client/Engine/Source` 或当前目录的 `.uproject` 后：

- tgrep 使用非 Git 工作区模式，排除 `Binaries`、`Intermediate`、`Saved`、`DerivedDataCache`、
  `Content` 等生成/资源目录，保留 `Source`、`Plugins` 与源码中的 `Build` 目录。
- CodeGraph 安装把 `codegraph.json` 纳入同一配置事务。默认加入源码目录的 `include`，
  防止 P4 源码被 Git 专用规则隐藏，并追加 UE 生成/资源目录的 `exclude`。已有 `include`
  是用户选择，保持不扩展；已有排除项、其他配置和注释保留。损坏规则阻止整批写入。
- 不把 `.p4ignore` 当成 Git 语法解析；P4 提交前仍按本工程规则排除缓存、索引和 `node_modules`。
  CodeGraph 是语法级索引，不运行 UHT/UBT，也不承诺完整解析 UE 反射宏或所有 Shader 语言。

Codex CLI 与 IDE 扩展在同一 Codex 主机上共享 MCP 配置，项目配置仍要求项目信任，因此统一显示为
Codex（id `codex`）。见 [官方 MCP 配置说明](https://learn.chatgpt.com/docs/extend/mcp?surface=cli)。
Kimi Code（id `kimi-code`）统一使用产品名；IDE 通过同一新版运行时的 `kimi acp` 接入时可复用配置。
官方 VS Code 扩展仍有新旧运行时兼容限制，不能据此认定所有插件版本都会读取 `.kimi-code/mcp.json`。
见 [IDE 接入说明](https://moonshotai.github.io/kimi-code/en/guides/ides.html) 和
[VS Code 扩展兼容说明](https://www.kimi.com/en/help/kimi-code/vscode-getting-started)。

pi 需要先安装 `nicobailon/pi-mcp-adapter` 扩展才会读取项目 `.mcp.json`（pi 本体不内置 MCP）；
选择 pi 时安装器会提示该前置条件，配置生成不等于扩展已连接。若存在 `.pi/mcp.json`，
安装器只做只读的同名条目冲突提示，不修改该文件。

**Cline CLI 的显式接入**（`.cline/mcp.json` 不是 Cline 的原生项目配置；已对发布版
`cline@3.0.62` 核实其 MCP 设置只读用户级文件或 `CLINE_MCP_SETTINGS_PATH` 指向的文件）：

```bash
node <MCP_DIR>/agents/tools/launch.mjs --agent=cline -- [cline 参数...]
# Cline CLI 未在常见全局目录时：
node ... --agent=cline --cline <cline 包 bin/cline 脚本或原生可执行文件> -- [cline 参数...]
```

启动器仅对子进程设置绝对 `CLINE_MCP_SETTINGS_PATH` 与仓库 cwd，以参数数组调用入口
（禁用 shell；Windows 的 npm `.cmd` shim 不可用，需 `--cline` 指向脚本或原生二进制）。
`--cline` 与 `CLINE_BIN_PATH` 使用相同的文件校验：JS 文件和带 Node shebang 的无扩展名
`bin/cline` 用当前 Node 运行，`.cmd/.bat` 拒绝；显式覆盖无效时报错，不回退其他安装。
入口确定后不再向子进程传 `CLINE_BIN_PATH`，避免 npm resolver 重复解析覆盖变量。
显式路径替换 Cline 本次进程的默认 MCP 配置源，不合并全局服务器；导出文件中的用户补充
条目会被保留。这是显式接入，不等于已自动接入。

**面板导入类（无已核实项目级写入路径，不写任何项目配置文件）**：Cline IDE（`cline-ide`）、
CodeBuddy IDE（`codebuddy-ide`）。选择它们时安装器在
`<BUILD_DIR>/integration/mcp/exports/` 生成本机导入片段（只含本次条目与绝对路径），
并打印各家面板的导入步骤；卸载/取消选择时删除片段。它们不参与 `--agents=all`。
片段与项目配置一起预检、写入和失败回滚；片段被手工修改、不可读或经链接指向仓库外时，整批中止。
修改过的片段需先移走再生成。省略 `--agents` 的更新会保留已有片段选择；重复生成无写入。
卸载片段不会移除已经手动导入 IDE 的条目，后者需在对应面板清理。

以下工具只有用户级（仓库外）配置文件，不自动写入，参考文末示例手工添加：
Windsurf（`~/.codeium/windsurf/mcp_config.json`，支持变量插值）。

### 客户端信任与批准（2026-09-20 实测记录）

多数客户端在“读到了项目配置”和“真正连接”之间还有一步用户确认。实测各家的步骤与门槛：

| 客户端 | 实测版本 | 项目配置被发现 | 连接前需要的用户步骤 | 连接验收 |
| --- | --- | --- | --- | --- |
| Qwen Code | 0.24.1 | 是 | `qwen mcp approve workspace-tgrep`（批准后绑定当前配置） | ✓ `mcp list` 显示 Connected（真实握手） |
| OpenCode | 1.18.31 | 是（根 `opencode.json`） | 无 | ✓ `opencode mcp list` 显示 connected（真实握手） |
| Kilo CLI | 7.7.6 | 是（`.kilo/kilo.json`） | 无 | ✓ `kilo mcp list` 显示 connected（真实握手） |
| MiMo CLI | 0.1.14 | 是（`.mimocode/mimocode.json`，列表还标注配置来源文件） | 无 | ✓ `mimo mcp list` 显示 connected（真实握手） |
| Cline CLI | 3.0.62 | 是（经 `launch.mjs` 显式路径，Windows/WSL 双平台） | 无（导出文件即配置源）；会话需登录模型供应商 | `cline config` MCP 标签显示已加载（双平台）；会话内调用未验收 |
| Codex CLI | 0.155.1 | 是（需项目信任） | 用户级 `config.toml` 写 `project_config_enabled = true` + `[projects.'<仓库绝对路径>'] trust_level = "trusted"`（或交互式首跑的信任对话） | `mcp list`/`get` 正确解析；会话内连接需 ChatGPT 登录，未验收 |
| Gemini CLI | 0.60.0 | 是 | 目录信任（未信任目录按官方行为禁用项目 MCP）+ 模型认证 | 认证门槛（rc=41）后停止，未验收 |
| Claude Code | 2.1.278 | 是 | 交互式运行 `claude` 批准项目 `.mcp.json`（官方帮助：未批准的服务器不会被连接；离线写 `enabledMcpjsonServers` 等状态实测无效） | Pending approval 后停止，未验收 |

Kimi Code（2.0.2）与 oh-my-pi（18.2.7）当前版本没有免登录的非交互 MCP 查看命令
（Kimi 仅 TUI 内 `/mcp` 且需 `/login`、项目配置另有目录信任提示；omp 仅 TUI 内
`/mcp list`），无法在无凭据环境验收连接，其配置形状由单元测试与官方文档覆盖。
Roo/Zed/WorkBuddy 为 GUI 客户端，同样只在文档层覆盖。

隔离工程实测详情与环境注意事项由维护 Skill `mcp-integration-maintenance` 的参考页保存。

### 候选文件与旧格式迁移

- **同目录候选整合**：OpenCode、Kilo、MiMo 的同目录 json/jsonc 并存时，保留声明顺序中
  首个存在的文件，递归合并兼容对象字段。数组或叶值不同会中止整批操作，包括本集成条目的
  自定义选项。目标文件局部编辑，保留 BOM、换行和原有注释；来源注释移到目标末尾的标注区块。
  成功写入合并结果后才删除来源文件，各步都有备份、日志和失败回滚。`--dry-run` 展示目标更新
  与冗余文件删除；卸载只原地清理托管条目，不整合文件。
- **不同目录保持分层**：Kilo 根配置与 `.kilo/`，以及 MiMo 根配置与 `.mimocode/`，
  都可以同时存在。外来字段、插件和服务器留在各自层级，仅迁移已确认属于本集成的条目。
  MiMo 优先在已有 `.mimocode/` 层写入；只有根候选存在时原地编辑根文件。
- **Kilo / Roo 迁移**：旧 `.kilocode/mcp.json`、`.roo/mcp_settings.json` 中的本集成条目
  会在新目标写入成功后被清理（先验证条目的 command/args 确实指向本仓库包装层；同名但
  指向其他程序的用户条目保留不动并提示）。旧文件里的外来条目始终保留。
  根 `kilo.json(c)` 按其实际 OpenCode 形状迁移。同格式迁移保留自定义选项；旧 Kilo stdio
  的 `env`、`disabled` 转为 `environment`、`enabled`，附加启动参数一并保留。
  不同值冲突、未知跨格式字段、自定义工作目录、`{file:...}` 引用以及其他后端的自定义选项
  会中止整批写入并指出来源，需要人工处理后重跑。显式卸载仍可清理这些托管条目。
- **CodeBuddy 旧根 `mcp.json`**：仅它存在且只选 CodeBuddy 时原地编辑；需要创建
  `.mcp.json` 而旧文件含外来服务器时会中止（避免新文件遮蔽旧配置，预览式迁移未实现）；
  旧文件只含本集成条目时创建 `.mcp.json` 并清理旧条目；两文件并存时按官方优先级只编辑
  `.mcp.json`，旧文件原样保留。仅旧文件存在时，卸载在旧文件中执行并保留外来服务，不新建配置。

## 配置条目示例

`<PROJECT_DIR>` 表示选定工作空间，`<MCP_DIR>` 表示工具目录，`<BUILD_DIR>` 表示检测到的
构建/缓存目录。运行 `setup.js` 时会写入解析后的实际路径（见下节的路径策略）。

### 通用 `mcpServers` 家族（Claude Code / pi / CodeBuddy / Cursor / Gemini CLI / Roo Code / WorkBuddy / oh-my-pi / Cline / Windsurf）

以 Claude Code / pi 的 `.mcp.json` 为例（其余工具同结构，见上表文件位置）：

```json
{
  "mcpServers": {
    "workspace-tgrep": {
      "type": "stdio",
      "command": "node",
      "args": ["<MCP_DIR>/tgrep/src/server.mjs", "--repo-root", "<PROJECT_DIR>"]
    }
  }
}
```

命令行等价（Claude Code）：

```bash
claude mcp add --transport stdio --scope project workspace-tgrep -- node <MCP_DIR>/tgrep/src/server.mjs --repo-root <PROJECT_DIR>
```

### GitHub Copilot：VS Code / Visual Studio（`.vscode/mcp.json`，键名为 `servers`）

```json
{
  "servers": {
    "workspace-codegraph": {
      "type": "stdio",
      "command": "node",
      "args": ["<MCP_DIR>/codegraph/src/server.mjs", "--repo-root", "<PROJECT_DIR>"]
    }
  }
}
```

### Codex（`.codex/config.toml`，项目级仅在受信任项目中生效）

```toml
[mcp_servers.workspace-tgrep]
command = "node"
args = ["<MCP_DIR>/tgrep/src/server.mjs", "--repo-root", "<PROJECT_DIR>"]
cwd = "<PROJECT_DIR>"
```

### OpenCode / Kilo / MiMo（以下以 `opencode.json` 为例）

```json
{
  "mcp": {
    "workspace-tgrep": {
      "type": "local",
      "command": ["node", "<MCP_DIR>/tgrep/src/server.mjs", "--repo-root", "<PROJECT_DIR>"],
      "enabled": true
    }
  }
}
```

### ZCode（`.zcode/config.json`）

```json
{
  "mcp": {
    "servers": {
      "workspace-codegraph": {
        "type": "stdio",
        "command": "node",
        "args": ["<MCP_DIR>/codegraph/src/server.mjs", "--repo-root", "<PROJECT_DIR>"]
      }
    }
  }
}
```

### 路径策略：cwd 与相对路径能用吗？

各客户端对 `cwd`/变量展开的支持不一致，`setup.js` 的策略是**按官方文档逐家处理**：

| 客户端 | 官方支持 | setup.js 写入 |
| --- | --- | --- |
| VS Code / Visual Studio Copilot | 两者可发现 `.vscode/mcp.json` | 共用绝对 args；已有 `${workspaceFolder}` 条目可迁移 |
| Codex / Kimi Code / Qwen Code | `cwd` 字段 | 本机绝对 `cwd`；工具在工程内用相对 args，外部工具用绝对 args |
| Cursor | `${workspaceFolder}` 等变量插值 | 当前使用本机绝对 args |
| Gemini CLI | `cwd` 字段 | 当前使用本机绝对 args |
| Windsurf | `command/args/env` 变量插值（具体变量见官方文档） | 不自动写；示例用绝对路径 |
| 其余自动配置目标 | 本实现只依赖已核实的 stdio command/args 形状，不推断统一变量支持 | 绝对路径（本机解析） |

服务端入口支持 `--repo-root <dir>`、`--build-dir <dir>`。安装器始终生成显式根目录参数，
后端不会依赖 IDE 启动时的 cwd。旧配置未带根参数时，从安装位置向上检测工程，找不到再从 cwd 检测。
构建目录按上述工作空间设置解析。跨工程配置的旧根参数冲突时中止，不静默改为检索另一个工程。

## 首次启动与索引

- MCP 握手立即响应；首次索引在后台进行，此时查询返回 `INDEX_NOT_READY`，
  用 `tgrep_status` / `codegraph_status` 查看进度。
- tgrep 索引位于 `<BUILD_DIR>/integration/mcp/state/tgrep/...`；
  CodeGraph 新索引位于仓库根 `.codegraph-<project>-<platform>/`。
  优先复用已有 `.codegraph/codegraph.db`，其次复用当前平台唯一的 `.codegraph-*-<platform>`
  数据库，包括旧工程前缀。若当前平台存在多个候选，则中止并提示选择，不自动重建。
  复制到新工程后，应把这些索引、依赖和缓存加入该工程的版本管理排除规则。
- 同一工具的第二个实例返回 `INDEX_IN_USE`，不会接管或杀死已有实例。
- 固定 CodeGraph 1.6.0 对不足 500 个索引文件的工程隐藏状态工具的发现条目，但保留处理器。
  包装层会验证该只读处理器后使用自己的状态 schema；真正缺失或失败仍报告降级。

## 手动查询或操作 CodeGraph 索引

索引是标准 SQLite 数据（`codegraph.db` + WAL），可以用上游 CLI 或任何 SQLite 客户端
手动访问。先用 `doctor.mjs` 确认索引位置：包装层新建的在
`<PROJECT_DIR>/.codegraph-<project>-<platform>/`，复用时也可能使用旧前缀或 `.codegraph/`；
以 doctor 输出的实际路径为准。

**指向自定义目录**：包装层新建的索引目录名不是默认的 `.codegraph`，手动操作时需要用
`CODEGRAPH_DIR` 环境变量指向它（复用 `.codegraph` 时不需要）。命令在 `<PROJECT_DIR>`
下执行：

```bash
# POSIX shell
CODEGRAPH_DIR=.codegraph-<project>-posix <codegraph入口> status .
# PowerShell
$env:CODEGRAPH_DIR = '.codegraph-<project>-windows'; <codegraph入口> status .
```

**`<codegraph入口>`**：若已全局安装与固定版本一致的 `codegraph` 可直接使用；否则用
prepare 下来的捆绑入口（与创建索引的程序同版本，避免 schema 迁移意外）：

```text
<BUILD_DIR>/integration/mcp/upstream/codegraph-bundle/node.exe --liftoff-only --disable-warning=ExperimentalWarning <BUILD_DIR>/integration/mcp/upstream/codegraph-bundle/lib/dist/bin/codegraph.js <子命令>
```

以上为 Windows；POSIX 使用同一捆绑目录中的 `node`。`CODEGRAPH_DIR` 必须是根目录下的单段
目录名，例如 `.codegraph-<project>-windows`，不接受绝对路径或尾部斜杠。优先与当前包装层的
`codegraph_status` / `doctor.mjs` 所报告的索引保持一致，不打开另一个平台的索引。

Agent 使用 shell 探索时，先确认该索引未被其他实例占用，再设置 direct/offline 环境。
这些变量仅作用于当前进程/子进程，不需要修改全局配置：

```powershell
$env:CODEGRAPH_DIR = '.codegraph-<project>-windows'
$env:CODEGRAPH_NO_DAEMON = '1'
$env:DO_NOT_TRACK = '1'
$env:CODEGRAPH_TELEMETRY = '0'
$env:CODEGRAPH_NO_UPDATE_CHECK = '1'
$env:CODEGRAPH_NO_DOWNLOAD = '1'
# 使用上面的固定版本入口执行：explore "<symbol names or question>"
```

POSIX 可将相同变量作为命令前缀，目录名选择实际使用的 `.codegraph` 或本平台目录：

```bash
CODEGRAPH_DIR=.codegraph-<project>-posix CODEGRAPH_NO_DAEMON=1 DO_NOT_TRACK=1 \
CODEGRAPH_TELEMETRY=0 CODEGRAPH_NO_UPDATE_CHECK=1 CODEGRAPH_NO_DOWNLOAD=1 \
codegraph explore "<symbol names or question>"
```

`codegraph` 仅在已安装且版本匹配时使用；否则使用已准备的捆绑入口。不要为执行提示词临时运行
无版本约束的 `npx`。CLI 可用性、索引占用或 schema 不匹配导致失败时回退普通源码工具，不自行解锁或重建。

常用命令（下表基于固定版本 1.6.0 制品的 `--help` 实测；命令普遍接受位置参数
`[path]` 或 `-p/--path` 选项定位项目——`status`/`sync`/`index` 用位置参数，
`query`/`callers`/`files` 等用 `-p`；多数支持 `--json` 输出）：

| 命令 | 用途 |
| --- | --- |
| `status [path]` | 索引健康检查（文件/节点/边数量） |
| `query <名称> [-k kind] [-l N]` | 按名称搜符号 |
| `node <名称>` / `callers|callees <符号>` / `impact <符号>` | 单符号详情与调用关系 |
| `files` / `explore <查询...>` / `context <任务...>` | 索引文件列表 / 综合探索 / 为任务组装上下文 |
| `affected [文件...]` | 找出受源文件变更影响的测试文件 |
| `sync [path]` | 增量同步离线变更 |
| `index [path]` | **全量重建**索引（先删后建，等价重新 init） |
| `unlock [path]` | 清理阻塞索引的残留锁 |

查看完整的可用选项：`npx @colbymchenry/codegraph --help`（拉取的是 registry 最新版，
命令面可能与本集成固定的 1.6.0 不同——例如新版可能有 `ui` 浏览器查看器而 1.6.0
制品没有；操作本集成索引时仍请用同版本入口）。

**直接查 SQLite**（表结构见捆绑包内 `lib/dist/db/schema.sql`，核心表 `nodes`/`edges`/`files`）：

```bash
sqlite3 "file:<PROJECT_DIR>/.codegraph-<project>-windows/codegraph.db?mode=ro" "SELECT kind, COUNT(*) FROM nodes GROUP BY kind ORDER BY 2 DESC LIMIT 10;"
```

没有 sqlite3 CLI 时，Node 22.5+ 自带的 `node:sqlite` 也能只读查询
（`new DatabaseSync(path, { readOnly: true })`）。

**图形界面现状**：截至固定的 1.6.0（也是 npm latest，2026-08），上游**尚未发布**任何
GUI；官方浏览器图查看器（`codegraph ui`，别名 `web`）已在上游源码树实现但未随版本
发布——按上游设计它只监听 127.0.0.1、只读打开现有索引、跟随文件变更刷新，支持符号
调用关系视图、全项目模块依赖图（Map）和 `A -> B` 流程查询。后续版本发布后，按
“测试与维护”的升级流程更新固定版本即可使用。在那之前的替代：

- **查数据库**：用任意 SQLite GUI（DB Browser for SQLite、DBeaver、DataGrip、VS Code
  SQLite 插件等）只读打开 `codegraph.db`（WAL 模式，`-wal`/`-shm` 是正常组成），
  核心表 `nodes`/`edges`/`files`，建表语句见捆绑包 `lib/dist/db/schema.sql`。
- **画调用子图**：导出一跳调用关系到 Graphviz DOT 再渲染，例如（Node 22.5+）：

```bash
node -e "
const { DatabaseSync } = require('node:sqlite');
const symbol = 'cs_msg_dispatcher'; // 换成要查的符号
const db = new DatabaseSync('.codegraph-<project>-windows/codegraph.db', { readOnly: true });
const rows = db.prepare(\"SELECT n2.name AS caller, n1.name AS callee FROM edges e JOIN nodes n1 ON n1.id=e.target JOIN nodes n2 ON n2.id=e.source WHERE n1.name=?\").all(symbol);
console.log('digraph G { rankdir=LR;'); for (const r of rows) console.log(JSON.stringify(r.caller) + ' -> ' + JSON.stringify(r.callee) + ';'); console.log('}');
" | dot -Tsvg -o callgraph.svg
```

（需要 Graphviz 的 `dot` 命令；把索引目录名换成实际值，`doctor.mjs` 可查。）

**注意事项**：

- 写操作（`index`、`sync`）前先确认没有包装层实例在运行（`doctor.mjs` 的 lock-holder
  检查，或在 Agent 客户端中断开该 MCP）；`codegraph` 自身用 `writer.pid` 拒绝并发
  写入者，但不要依赖它兜底。
- SQLite 直查建议按上面示例用 `mode=ro` 只读打开；库处于 WAL 模式，目录里的
  `-wal`/`-shm` 是正常组成部分，不要单独删除。
- 手动 `index`/`sync` 后无需重新安装：包装层下次启动直接复用现有索引。
- 上游 CLI 默认启用匿名遥测；如需关闭参考上游 `TELEMETRY.md`（`DO_NOT_TRACK=1`、
  `CODEGRAPH_TELEMETRY=0`——包装层自己启动的后端已强制关闭）。

## 行为要点

- 查询始终限定在本仓库：`scope`/`path` 只接受仓库相对路径，CodeGraph 的
  `projectPath` 参数被拒绝，未在 schema 中的参数被拒绝。
- 结果带 `truncated` 标记的截断发生在完整记录边界；错误带稳定错误码
  （`INDEX_NOT_READY`、`INDEX_IN_USE`、`INVALID_PARAMS`、`BACKEND_FAILED` 等）。
- 客户端断开（stdin EOF）、SIGINT/SIGTERM 都会停止后端进程树；包装层被强杀时，
  两个后端都会因 stdin 管道断开自行退出（上游已验证的生命线行为）。
- 后端不监听任何端口、不启用遥测/更新检查/下载；CodeGraph 强制 direct 模式。
- 诊断：`node <MCP_DIR>/common/tools/doctor.mjs`（只读）。

## 测试与维护

每个组件目录内 `npm install` 后：

```bash
npm --prefix <MCP_DIR>/agents test
npm --prefix <MCP_DIR>/common test
npm --prefix <MCP_DIR>/tgrep test
npm --prefix <MCP_DIR>/codegraph test
```

（或进入各组件目录执行 `npm test`。`node --test` 需显式传入 `test/*.test.mjs` 文件列表，
直接传目录会被当作模块路径解析失败。）测试使用假后端模拟协议面，不需要真实索引。
Agent 配置写入的回归测试在 `agents/`（JSON/JSONC 损坏零写入、Codex 标记边界、
共享目标去重、备份恢复、外部工具目录、旧名称迁移等）；`common/test/workspace.test.mjs`
覆盖根目录、工程名、缓存和旧索引检测。`agents/` 无 npm 运行时依赖（jsonc-parser 已随源码分发）。

真实后端样例应放在不会被父目录忽略规则排除的位置。tgrep 的 `--no-require-git` 仍读取
父目录 `.gitignore`；若样例位于上层仓库排除的构建目录中，可能得到空索引。

升级固定版本：改对应工具目录的 `upstream-lock.json`（版本、提交、哈希），重跑
`setup.js`（或 `--skip-prepare` 只调配置），重新执行测试。tgrep 补丁如不适用需在
`tgrep/patches/` 重新生成并重放回归。维护指引见 Agent Skill
`mcp-integration-maintenance`（仅在维护本集成时加载）。

组件 `package-lock.json` 保持与镜像无关（不含 `resolved` 或仅指向 npmjs/npmmirror）：
npm 12 会以 `EALLOWREMOTE` 拒绝含第三方 tarball 域名的 lockfile，`npm ci` 直接失败。
`common/test/lockfiles.test.mjs` 守护此约束；在私有镜像环境重新生成 lockfile 后请确认
没有把镜像域名写进 `resolved`。

## 已验证平台

| 平台 | 单元测试 | 真实后端冒烟 |
| --- | --- | --- |
| Windows x64（Node 24.21.0） | 266 项（264 通过，2 个 POSIX 权限/文件符号链接用例跳过；2026-09-22，agents 213 + common 35 + tgrep 9 + codegraph 9） | 通用工具副本放在任意含空格目录，UE 小型源码样例从不同 cwd 启动；首次索引/复用/查询/退出：tgrep 891/374ms，CodeGraph 3673/745ms；生成及资源目录均被排除。单元回归覆盖自动检测、完整 CLI 周期、迁移、范围冲突、缓存 junction 越界与事务保护。既有验收：2026-09-21 OpenCode/Kilo/MiMo 真实连接与 ConPTY 菜单 |
| Linux x64（WSL/Debian，Node 20.19.2） | 266/266，零跳过（2026-09-22）；独立工具副本在 ext4 上运行上述四套测试，包括 POSIX 权限与符号链接 | 本轮未复跑真实后端；既有验收：2026-09-21 真实 prepare、chmod/EXDEV 与 PTY；2026-09-18 后端冒烟（tgrep ~1s、codegraph ~14s） |

macOS 及 arm64 平台未验证。GUI 客户端（VS Code/Cursor/IDE 面板导入）、Kimi/omp（无非交互
命令面）与登录后 Agent 会话内的工具调用未验收（认证/交互门槛）。
本轮 DSH、TRAE、Visual Studio 与 JetBrains 两种插件验证到官方格式、安装器和 MCP 协议层；
未启动登录后的客户端会话，也未对完整 UE 引擎执行首次索引。单元测试日志位于
`<BUILD_DIR>/_agent_tmp/mcp-generic-verified/`（Windows）及
`<BUILD_DIR>/_agent_tmp/mcp-generic-work/test-logs/`（WSL）。
