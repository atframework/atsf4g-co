# atsf4g-co MCP 集成（tgrep / CodeGraph）

本目录提供两个面向 AI Agent 的 stdio MCP 服务，安装时二选一，均只作用于本仓库：

- **tgrep**（`tgrep/`）：快速文本/正则检索，后端为打过 stdio 传输补丁的固定版本
  [microsoft/tgrep](https://github.com/microsoft/tgrep)。
- **CodeGraph**（`codegraph/`）：结构化代码导航（符号、引用、影响面），后端为固定版本的
  [CodeGraph](https://github.com/colbymchenry/codegraph) direct 模式（禁 daemon、禁遥测）。

设计、验收边界与上游事实见 [Plan.md](Plan.md)。公共库与流程工具在 `common/`；
Agent 配置组件（产品注册表、纯变更规划、安全文件存储、批量写入）在 `agents/`；
各组件目录独立（源码、测试、包管理信息、上游固定清单都在自己目录内，
`agents/vendor/jsonc-parser/` 随源码分发）。

## 安装（推荐入口）

```bash
node <PROJECT_DIR>/project/integration/mcp/setup.js
```

安装器会依次完成：选择检索后端（二选一）→ 选择下载镜像 → 准备本地依赖（全部固定版本）
→ 依赖就绪后把 MCP 服务写入所选 Agent 的**项目级**配置。依赖准备失败时不会改动任何
Agent 配置。可重复执行，用于切换后端、升级固定制品、为更多 Agent 接入或卸载。

常用选项：`--backend=tgrep|codegraph`、`--agents=<id,...>|all`、`--mirror=cn|official`
（未指定时按系统地区建议）、`--offline`、`--skip-prepare`（仅调整 Agent 配置）、
`--uninstall [--agents=...|--all-agents]`、`--yes`、`--dry-run`、`--help`、`--list-agents`。

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
配置文件本身的符号链接。进程崩溃后的自动恢复尚未实现。

### CodeGraph 提示词补丁

选择 `--backend=codegraph` 并配置至少一个 Agent 时，安装器同时更新根 `AGENTS.md` 中的
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
  GitHub 固定提交拉取并校验提交号）。安装器不会修改全局 npm/cargo 配置。

## 支持的 Agent

以下 Agent 的配置由 `setup.js` 自动写入（均为仓库内项目级文件，保留其中已有的其他条目；
`--list-agents` 查看全部可选 id）。条目形状均按各家官方文档核验（2026-09-20，含实施前实时复核）：

| Agent | 配置文件 | 格式要点 |
| --- | --- | --- |
| Claude Code / pi / CodeBuddy Code CLI（共用一个文件） | `.mcp.json` | `mcpServers`；选任一 id 即保留整组，卸载任一 id 影响全组（输出会标注） |
| Codex CLI | `.codex/config.toml` | `[mcp_servers.<name>]` + `cwd` |
| VS Code / GitHub Copilot | `.vscode/mcp.json` | `servers` + `${workspaceFolder}` |
| Cursor | `.cursor/mcp.json` | `mcpServers` |
| Gemini CLI | `.gemini/settings.json` | `mcpServers` |
| OpenCode | `opencode.json` 或 `opencode.jsonc` | `mcp` / `type:local` / `command` 数组 |
| Kilo Code（id `kilo`，兼容旧 id `kilocode`） | `.kilo/kilo.json(c)`；根 `kilo.json(c)` 的托管条目迁入此目录 | 同 OpenCode 形状；根与目录配置可分层共存 |
| Roo Code | `.roo/mcp.json` | `mcpServers` |
| ZCode | `.zcode/config.json` | `mcp.servers` |
| oh-my-pi (omp) | `.omp/mcp.json` | `mcpServers` |
| Zed | `.zed/settings.json`（JSONC，保留注释） | `context_servers` / 扁平 `command` + `args` |
| Kimi Code CLI | `.kimi-code/mcp.json` | `mcpServers` + `cwd` + 相对 args |
| Qwen Code | `.qwen/settings.json` | `mcpServers` + `cwd` + 相对 args |
| WorkBuddy | `.workbuddy/mcp.json` | `mcpServers` |
| MiMo Code | `mimocode.json(c)` 或 `.mimocode/mimocode.json(c)`；新建默认后者 | 同 OpenCode 形状 |

pi 需要先安装 `nicobailon/pi-mcp-adapter` 扩展才会读取项目 `.mcp.json`（pi 本体不内置 MCP）；
选择 pi 时安装器会提示该前置条件，配置生成不等于扩展已连接。

以下工具只有用户级（仓库外）配置文件，不自动写入，参考文末示例手工添加：
Windsurf（`~/.codeium/windsurf/mcp_config.json`，支持变量插值）、
Cline（VS Code 面板内配置或 CLI 的 `~/.cline/data/settings/cline_mcp_settings.json`）。

### 候选文件与旧格式迁移

- **多候选限制**：安装器对 OpenCode 根 json/jsonc、Kilo 的 `.kilo/kilo.json` 与
  `.kilo/kilo.jsonc`、MiMo 的根/目录候选采用保守的多文件中止策略；这是安装器支持范围，
  不表示客户端无法合并配置。Kilo 根配置与 `.kilo/` 配置分层加载，允许同时存在。
- **Kilo / Roo 迁移**：旧 `.kilocode/mcp.json`、`.roo/mcp_settings.json` 中的本集成条目
  会在新目标写入成功后被清理（先验证条目的 command/args 确实指向本仓库包装层；同名但
  指向其他程序的用户条目保留不动并提示）。旧文件里的外来条目始终保留。
  根 `kilo.json(c)` 使用其实际的 OpenCode 形状清理。旧托管条目含额外参数或自定义字段时，
  自动迁移中止并保留原文；需要先人工完成字段迁移并移除旧托管条目，再重跑。显式卸载仍可清理这些条目。
- **CodeBuddy 旧根 `mcp.json`**：仅它存在且只选 CodeBuddy 时原地编辑；需要创建
  `.mcp.json` 而旧文件含外来服务器时会中止（避免新文件遮蔽旧配置，预览式迁移未实现）；
  旧文件只含本集成条目时创建 `.mcp.json` 并清理旧条目；两文件并存时按官方优先级只编辑
  `.mcp.json`，旧文件原样保留。仅旧文件存在时，卸载在旧文件中执行并保留外来服务，不新建配置。

## 配置条目示例

`<PROJECT_DIR>` 表示本仓库的绝对路径；`<PROJECT_DIR>/build_jobs_cmake_tools` 为默认
`<BUILD_DIR>`。运行 `setup.js` 时会写入解析后的实际路径（见下节的路径策略）。

### 通用 `mcpServers` 家族（Claude Code / pi / CodeBuddy / Cursor / Gemini CLI / Roo Code / WorkBuddy / oh-my-pi / Windsurf / Cline）

以 Claude Code / pi 的 `.mcp.json` 为例（其余工具同结构，见上表文件位置）：

```json
{
  "mcpServers": {
    "atsf4g-tgrep": {
      "type": "stdio",
      "command": "node",
      "args": ["<PROJECT_DIR>/project/integration/mcp/tgrep/src/server.mjs"]
    }
  }
}
```

命令行等价（Claude Code）：

```bash
claude mcp add --transport stdio --scope project atsf4g-tgrep -- node <PROJECT_DIR>/project/integration/mcp/tgrep/src/server.mjs
```

### VS Code / GitHub Copilot（`.vscode/mcp.json`，键名为 `servers`）

```json
{
  "servers": {
    "atsf4g-codegraph": {
      "type": "stdio",
      "command": "node",
      "args": ["${workspaceFolder}/project/integration/mcp/codegraph/src/server.mjs"]
    }
  }
}
```

### Codex CLI（`.codex/config.toml`，项目级仅在受信任项目中生效）

```toml
[mcp_servers.atsf4g-tgrep]
command = "node"
args = ["project/integration/mcp/tgrep/src/server.mjs"]
cwd = "<PROJECT_DIR>"
```

### OpenCode / Kilo / MiMo（以下以 `opencode.json` 为例）

```json
{
  "mcp": {
    "atsf4g-tgrep": {
      "type": "local",
      "command": ["node", "<PROJECT_DIR>/project/integration/mcp/tgrep/src/server.mjs"],
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
      "atsf4g-codegraph": {
        "type": "stdio",
        "command": "node",
        "args": ["<PROJECT_DIR>/project/integration/mcp/codegraph/src/server.mjs"]
      }
    }
  }
}
```

### 路径策略：cwd 与相对路径能用吗？

各客户端对 `cwd`/变量展开的支持不一致，`setup.js` 的策略是**按官方文档逐家处理**：

| 客户端 | 官方支持 | setup.js 写入 |
| --- | --- | --- |
| VS Code / Copilot | `cwd` 字段与 `${workspaceFolder}` 等变量 | `${workspaceFolder}` + 相对路径（可提交共享） |
| Codex CLI / Kimi Code / Qwen Code | `cwd` 字段 | 本机绝对 `cwd` + 相对 args（仍是本机配置） |
| Cursor | `${workspaceFolder}` 等变量插值 | 当前使用本机绝对 args |
| Gemini CLI | `cwd` 字段 | 当前使用本机绝对 args |
| Windsurf | `command/args/env` 变量插值（具体变量见官方文档） | 不自动写；示例用绝对路径 |
| 其余自动配置目标 | 本实现只依赖已核实的 stdio command/args 形状，不推断统一变量支持 | 绝对路径（本机解析） |

服务端入口支持 `--repo-root <dir>`、`--build-dir <dir>` 参数覆盖默认推导
（缺省从入口文件位置推导仓库根并校验标记，构建目录缺省 `build_jobs_cmake_tools`）。

## 首次启动与索引

- MCP 握手立即响应；首次索引在后台进行，此时查询返回 `INDEX_NOT_READY`，
  用 `tgrep_status` / `codegraph_status` 查看进度。
- tgrep 索引位于 `<BUILD_DIR>/integration/mcp/state/tgrep/...`；
  CodeGraph 索引位于仓库根 `.codegraph-atsf4g-<platform>/`（已加入 `.gitignore`）。
  已存在的有效 `.codegraph/` 会原地复用。
- 同一工具的第二个实例返回 `INDEX_IN_USE`，不会接管或杀死已有实例。

## 手动查询或操作 CodeGraph 索引

索引是标准 SQLite 数据（`codegraph.db` + WAL），可以用上游 CLI 或任何 SQLite 客户端
手动访问。先用 `doctor.mjs` 确认索引位置：包装层新建的在
`<PROJECT_DIR>/.codegraph-atsf4g-<platform>/`，复用的在 `<PROJECT_DIR>/.codegraph/`。

**指向自定义目录**：包装层新建的索引目录名不是默认的 `.codegraph`，手动操作时需要用
`CODEGRAPH_DIR` 环境变量指向它（复用 `.codegraph` 时不需要）。命令在 `<PROJECT_DIR>`
下执行：

```bash
# POSIX shell
CODEGRAPH_DIR=.codegraph-atsf4g-posix <codegraph入口> status .
# PowerShell
$env:CODEGRAPH_DIR = '.codegraph-atsf4g-windows'; <codegraph入口> status .
```

**`<codegraph入口>`**：若已全局安装与固定版本一致的 `codegraph` 可直接使用；否则用
prepare 下来的捆绑入口（与创建索引的程序同版本，避免 schema 迁移意外）：

```text
<BUILD_DIR>/integration/mcp/upstream/codegraph-bundle/node.exe --liftoff-only --disable-warning=ExperimentalWarning <BUILD_DIR>/integration/mcp/upstream/codegraph-bundle/lib/dist/bin/codegraph.js <子命令>
```

以上为 Windows；POSIX 使用同一捆绑目录中的 `node`。`CODEGRAPH_DIR` 必须是根目录下的单段
目录名，例如 `.codegraph-atsf4g-windows`，不接受绝对路径或尾部斜杠。优先与当前包装层的
`codegraph_status` / `doctor.mjs` 所报告的索引保持一致，不打开另一个平台的索引。

Agent 使用 shell 探索时，先确认该索引未被其他实例占用，再设置 direct/offline 环境。
这些变量仅作用于当前进程/子进程，不需要修改全局配置：

```powershell
$env:CODEGRAPH_DIR = '.codegraph-atsf4g-windows'
$env:CODEGRAPH_NO_DAEMON = '1'
$env:DO_NOT_TRACK = '1'
$env:CODEGRAPH_TELEMETRY = '0'
$env:CODEGRAPH_NO_UPDATE_CHECK = '1'
$env:CODEGRAPH_NO_DOWNLOAD = '1'
# 使用上面的固定版本入口执行：explore "<symbol names or question>"
```

POSIX 可将相同变量作为命令前缀，目录名选择实际使用的 `.codegraph` 或本平台目录：

```bash
CODEGRAPH_DIR=.codegraph-atsf4g-posix CODEGRAPH_NO_DAEMON=1 DO_NOT_TRACK=1 \
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
sqlite3 "file:<PROJECT_DIR>/.codegraph-atsf4g-windows/codegraph.db?mode=ro" "SELECT kind, COUNT(*) FROM nodes GROUP BY kind ORDER BY 2 DESC LIMIT 10;"
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
const db = new DatabaseSync('.codegraph-atsf4g-windows/codegraph.db', { readOnly: true });
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
- 诊断：`node <PROJECT_DIR>/project/integration/mcp/common/tools/doctor.mjs`（只读）。

## 测试与维护

每个组件目录内 `npm install` 后：

```bash
npm --prefix <PROJECT_DIR>/project/integration/mcp/agents test
npm --prefix <PROJECT_DIR>/project/integration/mcp/common test
npm --prefix <PROJECT_DIR>/project/integration/mcp/tgrep test
npm --prefix <PROJECT_DIR>/project/integration/mcp/codegraph test
```

（或进入各组件目录执行 `npm test`。`node --test` 需显式传入 `test/*.test.mjs` 文件列表，
直接传目录会被当作模块路径解析失败。）测试使用假后端模拟协议面，不需要真实索引。
Agent 配置写入的回归测试在 `agents/`（JSON/JSONC 损坏零写入、Codex 标记边界、
共享目标去重、备份恢复等）；`agents/` 无 npm 运行时依赖（jsonc-parser 已随源码分发）。

升级固定版本：改对应工具目录的 `upstream-lock.json`（版本、提交、哈希），重跑
`setup.js`（或 `--skip-prepare` 只调配置），重新执行测试。tgrep 补丁如不适用需在
`tgrep/patches/` 重新生成并重放回归。维护指引见 Agent Skill
`mcp-integration-maintenance`（仅在维护本集成时加载）。

## 已验证平台

| 平台 | 单元测试 | 真实后端冒烟 |
| --- | --- | --- |
| Windows x64（Node 24） | 127/127（2026-09-20，agents 88 + common 23 + tgrep 8 + codegraph 8） | 历史 P0–P8：tgrep 首次索引 ~2s、codegraph ~22s；本轮未重跑真实后端/客户端连接 |
| Linux x64（WSL/Debian，Node 20 + 捆绑 Node 24） | 43/43（P9.0 基线；P9.1 起新增测试的 WSL 复验待后续阶段一并执行） | tgrep 首次索引 ~1s、codegraph 首次索引 ~14s，均通过 |

macOS 及 arm64 平台未验证。
