# tgrep / CodeGraph MCP 集成计划（Node.js 实现）

状态：P0–P8 已完成并验收（2026-09-18）；P9 进行中——P9.0–P9.3 已完成（2026-09-20，
证据摘要见 11.9），P9.4–P9.6 待实施（见 11.8）。本文件是实施与验收依据；已完成部分仅保留
简要记录与仍然生效的契约，未实施部分保留完整设计。历史 Python 方案差异见第 10 节。

## 1. 目标与验收边界

实现两个可独立配置的 MCP 服务，分别位于 `project/integration/mcp/tgrep` 和
`project/integration/mcp/codegraph`，均通过 stdio 与 AI Agent 通信。每个服务启动后自动检查本工程的索引，
缺失时初始化，已有有效索引时复用，并自动运行持续刷新的后端。客户端断开后，必须回收本次启动的整个进程树。

“不能有内容泄露”按以下可验证要求落实，同时覆盖进程、句柄和内存资源泄漏：

- 运行中的索引、搜索和刷新不向外部网络发送请求，不启动可被其他本机用户直接查询的无认证服务。
- 查询限定在启动时确定的本仓库；工具参数不能切换仓库、访问仓库外文件或开启远程功能。
- 不把源码、查询原文、密钥、完整工具结果写入诊断日志；索引及 SQLite WAL 按源码数据保护。
- 关闭、取消、启动失败和进程异常都进入可验证的清理流程；不留下后台刷新服务。
- MCP 正常返回的源码属于用户授权给当前 Agent 的结果。Agent/宿主如何继续发送这些结果由宿主的数据策略决定，
  本集成不能把“正常返回源码”和“绝不向 Agent 提供源码”同时作为承诺。

本期不做 HTTP/SSE MCP、跨机器索引、系统常驻服务、共享后台守护进程，也不把 CodeGraph 的结构分析
当作编译器语义或 C++ 调用关系完整性的证明。

## 2. 已核实的工作区与上游事实

### 2.1 工作区（要点）

- `<BUILD_DIR>` 推导：`.vscode/settings.json` 未设 `cmake.buildDirectory`，clangd 参数指向
  `${workspaceFolder}/build_jobs_cmake_tools`，即本工作区默认值。包装层状态、运行时、上游源码与
  日志放在 `<BUILD_DIR>/integration/mcp/` 下。
- 包装层要求系统 Node >= 20（SDK engines 约束）；CodeGraph 后端与首次索引 helper 使用其平台包
  捆绑的 Node 24 运行时，不依赖系统 Node（见 2.3 的 node:sqlite 约束）。2026-09-18 实测工具链：
  Windows node v24.21.0 / npm 12.0.2 / rustc 1.98.0；WSL/Debian（独立 ext4 clone）node v20.19.2 /
  corepack npm@10 / rustc 1.98.1。
- `project/integration/Bootstrap.cmake` 没有 MCP 初始化逻辑；普通 CMake configure/build 不隐式启动索引进程。
- `.gitignore` 已覆盖 `/build_*`、`.codegraph*` 及多种 Agent 配置路径。ignore 规则不代表安装器已支持
  对应 Agent；多候选配置文件的完整检查见 11.7。

### 2.2 固定调研基线

| 项目 | 基线 | 使用方式 |
| --- | --- | --- |
| tgrep | 源码提交 `239711cfb6e69e8780cabf912a8987162a223ff1`（1.0.9，Rust edition 2024）+ 本集成 stdio 补丁 | cargo release 构建，固定于 `tgrep/upstream-lock.json` |
| CodeGraph | npm `@colbymchenry/codegraph@1.6.0`（行为参考源码提交 `ba3c21e50d9129d2f5f3843ec3728868ae6d47a1`，该树**领先于** 1.6.0 发布；两者差异以制品 `--help` 实测为准，已实测差异：制品无 `ui` 子命令），平台包 `@colbymchenry/codegraph-<platform>-<arch>` | 平台包捆绑独立 Node 24 运行时，固定于 `codegraph/upstream-lock.json` |
| MCP SDK | npm `@modelcontextprotocol/server@2.0.0`、`@modelcontextprotocol/client@2.0.0`（engines: node >= 20） | 官方 TS SDK，低层 Server API + StdioClientTransport |
| jsonc-parser | npm `jsonc-parser@3.3.1`（MIT，零运行时依赖） | 随源码分发于 `agents/vendor/jsonc-parser/`，校验清单在其 `VENDOR.json`（见 11.2） |

版本号本身不证明二进制相同；`prepare` 记录实际制品哈希到
`<BUILD_DIR>/integration/mcp/state/prepared-state.json`。

### 2.3 CodeGraph（已对照固定提交源码核实）

- 入口 `serve --mcp --path ROOT`。默认会尝试共享 detached daemon；`CODEGRAPH_NO_DAEMON=1` 强制
  direct 模式：单进程 stdio MCP 会话，启动 watcher 并做离线补齐（catch-up sync）。
  依据：`src/mcp/index.ts` `startDirect`。
- direct 模式用 `writer.pid` 拒绝同项目第二个写入者（退出码 1）。
- **direct 模式不会做首次索引**：`engine.doInitialize` 只上溯查找已存在的 `.codegraph/`；没有索引时
  报“no default project”并禁用同步。首次索引必须显式执行。
- 库接口 `CodeGraph.init(root, {index:false})` + `instance.indexAll()` + `close()` 可以完成首次索引，
  且不经过 CLI 的 Git hooks 安装回退路径。这是包装层初始化 helper 的实现序列。
- **库运行时要求 Node 22.5+ 的内置 `node:sqlite` 模块**（上游 package.json 声明 `>=20`
  与实现不符；Linux 上系统 Node 20 实测失败）。因此 serve 子进程与首次索引 helper
  都必须运行在平台包捆绑的 Node 24 上（每个平台包根目录都有通用 `node`/`node.exe`），
  系统 Node 只承载包装层本身（SDK 约束 >= 20）。Linux 的 `bin/codegraph` 是 POSIX
  shell 包装，包装层不使用它（它会重新注入 `CODEGRAPH_HOST_PPID`）。
- 生命周期（对进程回收设计至关重要）：
  - stdin `end`/`close`/`error` 都触发 `stop()` → `process.exit(0)`（`src/mcp/stdin-teardown.ts`）；
  - PPID watchdog 轮询父进程存活，父进程被 SIGKILL 也触发退出；
  - 进程内挂起保护（#850）spawn 一个 watchdog 子进程，该子进程监听自己的 stdin 生命线，
    父进程退出（管道关闭）后自行退出；
  - `CODEGRAPH_STARTUP_HANDSHAKE_TIMEOUT_MS`（无流量自杀计时）必须设为 `0`，
    因为包装层会在握手套接前先做初始化检查。
- 遥测默认开启：`DO_NOT_TRACK=1`、`CODEGRAPH_TELEMETRY=0`、`CODEGRAPH_NO_UPDATE_CHECK=1`、
  `CODEGRAPH_NO_DOWNLOAD=1` 全部强制设置，且在任何上游模块加载前生效。
- `CODEGRAPH_DIR` 只接受仓库根下单段目录名。包装层新建索引用 `.codegraph-atsf4g-<platform>`；
  上游索引与文件监视会跳过所有 `.codegraph*` 兄弟目录。
- 8 个上游工具（explore/search/node/callers/callees/impact/files/status）全部携带可选 `projectPath`。
  参数形状以固定版本 `list_tools()` 返回为准（v1.6.0 中 callers/callees/impact 用 `symbol`/`file`，
  explore 用 `query`/`maxFiles`）；包装层不硬编码参数表，启动时从后端拉取 schema 并剥除 `projectPath`。

### 2.4 tgrep（已对照固定提交源码与补丁核实）

- 上游 CLI 无 MCP 子命令；`serve` 原生绑定 `127.0.0.1:0` 且无认证。本集成维护最小补丁
  （`tgrep/patches/0001-serve-add-private-stdio-transport.patch`）增加 `--transport stdio`：
  行分隔 JSON-RPC 走私有管道，不创建 TCP listener，不发布 `serve.json`；stdin EOF 干净退出
  （与 SIGINT 相同的停止语义，释放 watcher/auto-save 线程与 `serve.lock`）；搜索在后端侧
  按 20000 行上限截断并返回 `truncated` 标记。
- 内部 RPC 仅包装 `search`/`files`/`status`；`reload` 不暴露。错误码 `-32001`（索引未就绪）、
  `-32002`（索引无效）。`serve` 参数：`--index-path`、`--exclude`（可重复）、`--no-require-git`。
- 排除策略与扫描策略在 index/serve/watch 间保持一致（同一组 `--exclude`）。
- 原生监视失败回退轮询，默认周期 120 秒；“改动立即可见”不是承诺，`tgrep_status` 报告实际模式。

### 2.5 MCP TypeScript SDK v2（已对照 npm 包与源码核实）

- 服务端：`new Server({name, version}, {capabilities, instructions})` +
  `setRequestHandler('tools/list'| 'tools/call', ...)`（纯 JSON Schema，无 zod 依赖）+
  `serveStdio(() => server)`（`@modelcontextprotocol/server/stdio`）。返回 handle，
  `handle.close()` 关停。
- 客户端：`Client` + `StdioClientTransport({command, args, env, cwd, stderr, maxBufferSize})`
  （`@modelcontextprotocol/client/stdio`）。transport 内部用 `cross-spawn`、`shell:false`、
  Windows 上 `windowsHide:true`；环境为最小继承集 `getDefaultEnvironment()` 合并传入 env。
- **`close()` 的停机顺序内建为：stdin EOF → 等 2 秒 → SIGTERM → 等 2 秒 → SIGKILL**，
  与本计划 6.1 的包装层顺序一致；`pid`、`stderr`（PassThrough）均有访问器。
- 协议版本协商由 SDK 处理；CodeGraph 后端声明 `2024-11-05`，SDK 按旧版协议回落。

### 2.6 Agent 接入调研基线（2026-09-20，P9 依据）

用户已确认需求中的「codebuffy」指腾讯 **CodeBuddy**，不是 CodebuffAI 的 Codebuff。
P9 不新增 Codebuff；CodeBuddy Code CLI、CodeBuddy IDE 和 WorkBuddy 分别记录能力。
下表是官方文档/上游源码核验结果，来源见 11.11；尚未逐个安装客户端进行连接验收。
滚动文档或 `main` 源码不能证明所有已发布版本都支持；实施时记录实测版本，不推测最低版本。

| Agent / 计划 id | 配置位置或接入方式 | 配置形状 / 路径策略 | 结论与限制 |
| --- | --- | --- | --- |
| Claude Code / `claude` | 项目根 `.mcp.json` | `mcpServers` / 绝对 args | 保留。与 pi、CodeBuddy CLI 共用一个物理目标；客户端的项目信任/启用由用户完成 |
| pi / `pi` | 安装 `nicobailon/pi-mcp-adapter` 后读取项目根 `.mcp.json` | `mcpServers` / 绝对 args | pi 本体明确不内置 MCP。新增可选 id 与前置条件说明；`.pi/mcp.json` 可覆盖共享条目，需提示冲突 |
| 腾讯 CodeBuddy Code CLI / `codebuddy` | 项目根 `.mcp.json`；旧候选为根 `mcp.json` | `mcpServers` / 绝对 args | 新增可选 id。官方读取首个存在候选，不能忽略旧文件而新建高优先级文件遮蔽其他服务器；共享文件规则见 11.3 |
| 腾讯 CodeBuddy IDE / `codebuddy-ide` | Settings → MCP → Add MCP 打开的配置文件 | `mcpServers` / 导出绝对 args | 官方 IDE 页面确认面板与格式，未确认固定项目路径；列为引导导入，不套用 CLI 路径 |
| WorkBuddy / `workbuddy` | `.workbuddy/mcp.json` | `mcpServers` / 绝对 args | 官方明确区分用户级和项目级；新增项目配置写入 |
| Codex CLI / `codex` | `.codex/config.toml` | `[mcp_servers.<id>]` / 绝对 `cwd` + 相对 args | 保留路径策略 |
| VS Code / Copilot / `vscode` | `.vscode/mcp.json` | `servers`、`type:"stdio"` / `${workspaceFolder}` | 保留；配置编辑必须保留已有非托管内容 |
| Cursor / `cursor` | `.cursor/mcp.json` | `mcpServers`、`type:"stdio"` / `${workspaceFolder}` | 已有 writer；官方明确支持 args 变量插值，纠正“无变量支持”的旧描述 |
| Gemini CLI / `gemini` | `.gemini/settings.json` | `mcpServers` / 绝对 `cwd` + 相对 args | 已有 writer；官方支持 `cwd`，可与 Qwen 统一路径策略，不再声称它不支持 |
| OpenCode / `opencode` | 根 `opencode.json` 或 `opencode.jsonc` | `mcp`、`type: "local"`、command 数组、`environment` | 保留；支持已存在的 JSONC 候选，不能固定另建 `.json` |
| Kilo / `kilo`（兼容 id `kilocode`） | `.kilo/kilo.json(c)`；根 `kilo.json(c)` 的托管条目迁入此目录 | 同 OpenCode 形状 / 绝对 args | 根与目录可分层共存；旧 `.kilocode/mcp.json` 仅列入受控迁移，不能笼统推断所有历史版本均无效 |
| Roo Code / `roo` | `.roo/mcp.json` | `mcpServers` / 绝对 args | 官方文档和 McpHub 均确认；现 writer 的 `.roo/mcp_settings.json` 是错误的项目文件名 |
| ZCode / `zcode` | `.zcode/config.json` | `mcp.servers` / 绝对 args | 保留；不能覆盖 `mcp` 下其他设置 |
| oh-my-pi / `omp` | `.omp/mcp.json` | `mcpServers` / 绝对 args | 保留；OMP 与 pi 不是同一产品，其兼容导入可能读到其他客户端文件 |
| Zed / `zed` | `.zed/settings.json` | `context_servers`，扁平 `command` + `args` + 可选 `env` | 新增；官方示例不要求 `source`，始终写 args；JSONC 与工作区信任必须纳入验收 |
| Kimi Code CLI / `kimi-code` | `.kimi-code/mcp.json` | `mcpServers` / 绝对 `cwd` + 相对 args | 依据 MoonshotAI/kimi-code；不将此路径套给 MoonshotAI/kimi-cli，不自动迁移旧产品用户配置 |
| Qwen Code / `qwen` | `.qwen/settings.json` | `mcpServers` / 绝对 `cwd` + 相对 args | 新增；不写 `trust:true` 或修改用户工具许可 |
| MiMo Code / `mimocode` | 根 `mimocode.json(c)` 或 `.mimocode/mimocode.json(c)`；新建默认后者 `.json` | 同 OpenCode 形状 / 绝对 args | 依据 XiaomiMiMo/MiMo-Code 的配置加载器；不能只检查一个 `.json` 文件 |
| Cline CLI / `cline` | 默认用户配置；源码支持 `CLINE_MCP_SETTINGS_PATH` 显式指定文件 | `mcpServers` / 绝对 args | 未确认原生项目自动发现。仓库内导出文件 + 仅当前进程设置该变量的启动入口见 11.6；这是显式接入，不是原生项目配置 |
| Cline IDE / `cline-ide` | 面板 MCP Servers → Configure MCP Servers | `mcpServers` / 引导导入 | 不猜 IDE 存储路径，也不将 CLI 环境变量方案声称适用于已运行的扩展宿主 |

只有 VS Code/Cursor 的 `${workspaceFolder}` 方案不需要仓库绝对路径；绝对 `cwd` + 相对 args
仍然是本机配置，不能标注为可直接跨机器提交。其他客户端继续用绝对 args 是安装器的保守选择，
不等于厂商不支持任何变量。默认继续使用 `command:"node"`；GUI 的 PATH、Node >= 20 和
Windows/Linux 路径差异需通过实际启动验证，不能仅凭文件存在判断连接成功。

## 3. 总体设计

```text
AI Agent (MCP 客户端)
  ├─ stdio → node tgrep/src/server.mjs
  │           ├─ common/src/mcpServer.mjs      低层 Server + serveStdio + 统一关停入口
  │           ├─ common/src/supervisor.mjs     spawn + EOF 优先的受管停机
  │           └─ 补丁版 tgrep serve --transport stdio（行 JSON-RPC，自有 serve.lock）
  └─ stdio → node codegraph/src/server.mjs
              ├─ common/src/mcpServer.mjs
              ├─ SDK Client + StdioClientTransport（进程生命周期由 SDK transport 托管）
              │    └─ 捆绑 Node 24 运行时: codegraph serve --mcp（direct 模式）
              └─ 首次索引时: codegraph/src/initialize.mjs（库 init+indexAll，同样受管）
```

每个“规范化工作树根目录 + 工具 + 状态目录”只允许一个 MCP 写入实例。两个不同工具可以同时运行；
同工具的第二个实例返回明确的 `INDEX_IN_USE`，不自动接管、杀死或复用无主进程。

### 3.1 目录布局（现状）

四个自包含组件目录（各自的源码、测试、包管理信息、上游固定清单在自己目录内），
顶层 `setup.js` 是唯一安装入口；不建立 Git submodule，也不要求安装各 Agent SDK：

```text
project/integration/mcp/
  Plan.md / README.md / setup.js
  common/                          公共库与流程工具
    src/  paths/errors/limits/state/supervisor/lineRpc/mcpServer/prepare + agents.mjs（shim）
    tools/doctor.mjs               只读诊断
    test/
  agents/                          Agent 自动配置组件（P9.2 建立，模块职责见 11.2）
    src/  registry/entries/configPlan/fileStore/writers + formats/{jsonDocument,codexToml}
    vendor/jsonc-parser/           随源码固定分发（3.3.1，MIT，校验清单 VENDOR.json）
    test/
  tgrep/
    src/server.mjs、src/backend.mjs、patches/0001-…-stdio-transport.patch、upstream-lock.json、test/
  codegraph/
    src/server.mjs、src/backend.mjs、src/initialize.mjs、upstream-lock.json、test/
```

跨组件引用使用相对路径。`common/src/agents.mjs` 仅 re-export `agents/src/writers.mjs` 的公开 API，
`agents/` 不反向导入该 shim（避免循环依赖），但可使用 `common/src/paths.mjs` 解析默认目录。
`common` 的 SDK 依赖从 `common/node_modules` 解析，`tgrep`、`codegraph` 各自声明并安装自己
直接 import 的包；`agents/` 无 npm 运行时依赖（jsonc-parser 随源码分发）。

### 3.2 安装入口与依赖准备

安装统一入口为 `node project/integration/mcp/setup.js`（依赖准备库在 `common/src/prepare.mjs`）：

1. 选择检索后端：tgrep / CodeGraph 同类二选一；重复执行可切换（旧后端条目从所有已配置 Agent 中移除后写入新后端）。
2. 选择下载镜像：`cn`（npm 用 npmmirror、cargo 用 rsproxy 的 `--config` 传入，不修改全局配置）
   或 `official`；未指定时按时区/语言建议。tgrep 源码始终从 GitHub 固定提交拉取并校验提交号。
3. 依赖准备（固定提交/版本 + 哈希校验，产物在 `<BUILD_DIR>/integration/mcp/` 下）：选中后端的组件目录
   `npm install`（lockfile 存在时 `npm ci`）；tgrep 为浅克隆 → 校验提交 → 应用补丁 → `cargo build --release`；
   CodeGraph 为 `npm pack` 平台包 → 校验 sha1 → 解包 → 验证捆绑 Node 与入口。
4. **依赖准备全部成功后才进入 Agent 配置阶段**；任何失败都直接报错退出，不改动任何 Agent 配置。
   `--dry-run` 完全跳过准备（只读规划路径，见 11.5）。
5. Agent 配置写入/卸载由 `agents/src/writers.mjs` 批量完成：仅仓库内项目级文件、合并保留用户已有条目、
   幂等、卸载按归属记录移除（路径策略见 2.6：VS Code/Cursor 用 `${workspaceFolder}`，
   Codex 用绝对 `cwd`+相对路径，其余写绝对路径；引导导入不能计为自动接入成功）。
6. 正常 MCP 启动只使用固定的本地程序（`prepared-state.json` 指向的绝对路径）。缺失依赖时 stderr 给出
   确定的准备步骤并退出；不在握手中下载、升级或弹出终端。
7. MCP 客户端配置形如 `command=node, args=[<repo>/project/integration/mcp/<tool>/src/server.mjs]`。
   `--repo-root`/`--build-dir` 可选（仓库根缺省从入口文件位置推导并校验 `project/integration/mcp/Plan.md`
   标记；构建目录缺省 `build_jobs_cmake_tools`）。
8. 启动外部程序一律参数数组、固定 `cwd`、禁用 shell、隐藏窗口；环境为最小允许集加工具强制变量，
   继承环境中的注入项（`NODE_OPTIONS`、`NODE_PATH`、`CODEGRAPH_DAEMON_INTERNAL` 等）被清除。

## 4. stdio 与对 Agent 的接口

### 4.1 公共协议规则（common/src/mcpServer.mjs）

- 外层 stdout 只允许 MCP JSON-RPC；初始化进度、后端日志、诊断都走 stderr 或状态文件。
- 工具列表在启动时固定，索引未完成时客户端不会缓存到空工具集；后端长时间初始化不阻塞
  initialize/tools 响应。查询未就绪时返回 `INDEX_NOT_READY`，不伪装成成功的空结果。
- 工具错误携带稳定错误码（`INDEX_NOT_READY`、`INDEX_IN_USE`、`INDEX_INVALID`、`BACKEND_FAILED`、
  `BACKEND_TIMEOUT`、`INVALID_PARAMS`、`REQUEST_TOO_LARGE`），区分无匹配、参数错误、未就绪、
  后端失败、超时与截断。
- 限额（common/src/limits.mjs）：请求 256 KiB、模式长度 4096 字符、匹配行默认 200、上下文每侧 3 行、
  文件列表 2000 条、响应 2 MiB、单查询超时 60 秒、每后端并发 1。截断发生在完整记录边界并返回 `truncated`。
- 后端不支持可靠取消时，取消/超时结束本次等待；查询超时后该后端标记不可用直到实际计算结束
  （初版以“串行 + 超时”实现该约束）。
- 不公开重建、删除索引、安装或停止其他服务的 MCP 工具；维护操作走显式本地命令。

### 4.2 tgrep 工具

| 工具 | 主要输入 | 输出与约束 |
| --- | --- | --- |
| `tgrep_search` | `pattern`、仓库相对 `scope`、`fixed_string`/`case_insensitive`/`word_boundary`、`glob`（<=4 个）、`context`（0–3）、`max_results` | 文件、行号、匹配内容、匹配位置、`truncated`、后端耗时。上游 `max_count` 是每文件上限，全局上限由包装层执行 |
| `tgrep_files` | `scope`、`glob`、`limit` | 仓库相对路径列表，不暴露索引存储路径 |
| `tgrep_status` | 无 | 包装层状态、索引/watcher 状态、轮询回退原因、版本 |

参数映射到内部 RPC 的字段名与语义已对照补丁版 `serve.rs` 核实（`fixed_string`、`case_insensitive`、
`word_boundary`、`before_context`/`after_context`、`glob`、`scope`、`detail`）。

### 4.3 CodeGraph 工具

- 默认对 Agent 列出 `codegraph_explore` 和包装层自己的 `codegraph_status`。
  本地静态配置（`<BUILD_DIR>/integration/mcp/state/codegraph/extra-tools.json`）可追加
  上游其余 6 个工具；服务端在 `tools/call` 分派时再次校验 allowlist。
- 工具 schema 启动时从后端 `list_tools()` 拉取，剥除 `projectPath` 属性与 required 项后原样透出，
  避免硬编码参数表随上游漂移。
- 每次调用拒绝 `projectPath`（消息说明查询固定在本仓库）；只转发该工具 schema 中已知属性；
  形如 `path`/`file` 的过滤参数按仓库相对值校验。不透传客户端 roots/rootUri 通知。
- 包装层发自己的简短 instructions，不透传上游 initialize instructions，
  不使用上游 `_meta['anthropic/alwaysLoad']` 元数据。
- 上游工具结果中的 `content`/`structuredContent`/`isError` 原样转交（文本为主），
  上游要求交互输入的结果视为 `BACKEND_FAILED`。

## 5. 自动初始化、复用与数据位置

### 5.1 检测顺序与实例锁

按“显式本地配置 → 已记录的状态目录 → 上游默认目录 → 新受控目录”定位已有索引；
检测只发生在确定的工作树内。初始化前取得工具级实例锁（`common/src/state.mjs`）：
`O_EXCL` 创建锁文件 + 记录 pid/实例 ID/启动时刻（Linux 读 `/proc/<pid>/stat`）；
冲突时检查持有者 pid 是否存活（`process.kill(pid, 0)`），已死则回收锁文件重试一次，
存活则返回 `INDEX_IN_USE`。锁删除失败（Windows 上文件仍被打开）按被持有处理——宁可误报占用，
不误删活锁。后端自己的排他机制（tgrep `serve.lock`、CodeGraph `writer.pid`）提供最终一致性保证。

| 现有状态 | 动作 |
| --- | --- |
| 有效、兼容 | 跳过首次初始化；启动后端，由后端补齐离线变化 |
| 从未初始化 | 自动初始化（tgrep 由 serve 顺带建索引；CodeGraph 由 initialize.mjs），验证后进入 ready |
| 损坏/无法确认身份 | 保留数据并报告 `INDEX_INVALID`；不自动删除或覆盖 |
| 其他进程占用 | `INDEX_IN_USE`；不杀进程、不删锁 |

### 5.2 tgrep 索引

索引放在 `<BUILD_DIR>/integration/mcp/state/tgrep/<workspace-id>/<platform>/index/`。
`serve --transport stdio --index-path <该目录>` 首次启动自动建索引并处理后续变更；
`meta.json` 校验失败按“损坏”处理。排除目录列表固定在 `tgrep/src/backend.mjs`
（`.git`、构建目录、`install`、`packages`、`.tgrep`、`.codegraph*` 等）。

### 5.3 CodeGraph 索引

- 启动时检查 `<repo>/.codegraph-atsf4g-<platform>/codegraph.db` 是否存在（与上游
  `isInitialized` 同一判据）。存在则直接启动后端（后端 catch-up 补齐离线变化）；
  缺失则先以受限环境运行 `initialize.mjs`（捆绑 Node 24 执行库 `init+indexAll+close`，
  stdout 逐行 JSON 进度供状态展示），完成后启动后端。
- 已存在且有效的根 `.codegraph`（上游默认名）优先原地复用；不自动搬移。
- 索引目录是持久数据，位于仓库根（`CODEGRAPH_DIR` 只接受根下单段名）；根 `.gitignore` 有 `/.codegraph*`。
  包装层日志/运行时/临时文件仍全部位于 `<BUILD_DIR>`。
- Windows 与 WSL 使用不同 `<platform>` 目录名，天然隔离，不共享 SQLite 目录。
- 索引目录包含 `codegraph.db` 与 WAL，按源码数据保护，不提交、不打包进 CI artifact。
  手动用上游 CLI 或 SQLite 访问索引的方法（`CODEGRAPH_DIR` 指向、捆绑入口、并发写
  约束）在 README 的“手动查询或操作 CodeGraph 索引”一节维护。

## 6. 关闭与异常清理

### 6.1 stdin 生命线 + 分级停机

- 正常关停（包装层收到 EOF/SIGINT/SIGTERM/内部异常）：
  1. 停止接受新查询；
  2. 关闭后端 stdin（EOF）。tgrep 补丁版按 SIGINT 同语义退出并释放 `serve.lock`；
     CodeGraph direct 模式在 stdin `end/close/error` 时 `stop()` 退出并释放 `writer.pid`；
  3. 等待宽限期（5 秒）后 SIGTERM，再等待后 SIGKILL，只针对本实例创建的 pid。
     CodeGraph 腿的该顺序由 SDK `StdioClientTransport.close()` 内建（2 秒/2 秒梯度）；
     tgrep 腿由 `common/supervisor.mjs` 实现；
  4. 回收状态文件与实例锁。
- 包装层被 SIGKILL：子进程 stdin 管道随句柄关闭而断开——tgrep 补丁版与 CodeGraph direct 模式
  都在 stdin EOF 时退出；CodeGraph 另有 PPID watchdog 兜底。不依赖包装层代码执行。
- 残余风险（如实声明）：后端进程在 EOF 之前已彻底卡死且父进程同时被 SIGKILL 时，
  用户态 stdin 生命线无法生效，由用户经 `doctor` 报告的 pid 定向清理。不做跨平台内核级保证的声明。
- 只清理本实例创建且仍可证明身份的进程；不按进程名扫描、不 `taskkill /IM`。
  正常关闭保留持久索引。旧 Python 方案的 Job Object/guardian 已随生态切换删除。

### 6.2 状态机

```text
STARTING → INITIALIZING（仅 CodeGraph 首建）/OPENING → READY 或 DEGRADED
任一阶段 → STOPPING → STOPPED
启动失败 → STOPPING → FAILED
```

输入 EOF、stdout 断开、SIGINT/SIGTERM、初始化失败、后端退出、内部异常都进入同一幂等停机入口。
状态记录（`wrapper-state.json`，原子替换）含真实根路径、平台、状态、pid、实例 ID 与更新时间；
`doctor` 与 `*_status` 工具读取同一份数据。

## 7. 内容边界与隐私

1. canonical/realpath 校验根目录与 scope；包含关系用规范化路径比较，不做字符串前缀判断。
   工具参数不能指定索引根。
2. 后端以最小环境启动：Windows 保留 `SYSTEMROOT/TEMP/PATH` 等系统必需项，POSIX 保留
   `PATH/HOME/LANG/TMPDIR`；CodeGraph 强制变量（禁 daemon/遥测/更新/下载、`CODEGRAPH_DIR`、
   `CODEGRAPH_MCP_TOOLS`、握手超时 0）在 spawn 前注入；云平台密钥、代理凭据、Agent token 不传入。
3. tgrep 扫描遵循 Git ignore 并叠加固定排除；CodeGraph 索引范围由上游默认（跳过 `.codegraph*`、
   node_modules 等）决定，`atframework` 源码默认纳入，其构建/缓存输出经 `codegraph.json` 或默认规则排除。
4. 索引含源码派生数据：不提交 Git、不上传 CI artifact；默认日志只记录相对路径、错误类别、耗时和数量；
   后端 stderr 写入有界文件（`backend.stderr.log`，轮转上限 256 KiB）。
5. 安装后用禁止外联的环境验证初始化、查询、刷新与关闭均可用。

## 8. 测试与验收

测试框架用 Node 内置 `node:test`，不引入额外测试依赖（`agents/` 的 jsonc-parser 除外，随源码分发）。
假后端（`.mjs`）模拟真实协议面；真实后端冒烟先在 Windows x64 执行。测试数据放
`<BUILD_DIR>/_agent_tmp/mcp/tests/`，不拿用户业务文件做破坏性测试。

| 类别 | 场景 | 必须观察到的结果 |
| --- | --- | --- |
| MCP 协议 | initialize、tools/list 固定、真实调用、非法参数、stdout 纯净性 | 正确协议语义；stdout 无非 MCP 行；错误与空结果有区别 |
| 首次启动 | 无索引的临时小工程 | 自动初始化一次；协议发现不等待全量索引 |
| 重复启动 | 已有效索引 | 跳过全量初始化，复用持久数据 |
| 实例互斥 | 同工具双实例 | 第二实例 `INDEX_IN_USE`；不误杀 |
| 实时性 | 新建/编辑/删除文件后查询 | 旧内容消失、新内容可见（CodeGraph 秒级；tgrep 按实际模式与周期报告） |
| 生命周期 | 客户端 EOF、SIGINT、SIGTERM、包装层强杀 | 所有本实例后代进程在时限内消失（外部进程观察） |
| 内容边界 | `../`、绝对路径、UNC、`projectPath` 注入、未知参数 | 请求被拒绝且外部文件未被读取 |
| 限额截断 | 超大结果、超长模式 | 整记录边界截断并带 `truncated`；错误码正确 |
| 后端异常 | 后端退出、超时、返回错误 | `DEGRADED`/`BACKEND_FAILED`；不伪装空结果 |

平台矩阵：Windows x64（原生）与 Linux x64（WSL/Debian）已按上表执行并通过（2026-09-18，含真实后端
冒烟、进程回收与锁释放检查）；macOS、Linux arm64、Windows arm64 未验证。等待断言使用状态/内容条件加
固定截止时间，不用任意 sleep。

## 9. 分阶段执行

- [x] P0 依赖固定：SDK 版本与 API、CodeGraph 平台包结构、tgrep 补丁协议、生命周期证据核实。
- [x] P1 公共层：`common`（paths/errors/limits/state/supervisor/lineRpc/mcpServer）+ prepare/doctor。
- [x] P2 tgrep MCP：三工具、内部 RPC 适配、状态映射、假后端协议测试。
- [x] P3 CodeGraph MCP：SDK client 桥接、后端派生 schema、projectPath 隔离、首次索引接线、假后端测试。
- [x] P4 Windows x64 真实后端冒烟：tgrep 补丁构建、两服务对本仓库初始化/查询/关闭回收。
- [x] P5 文档与 .gitignore 同步。
- [x] P6 Linux x64 验证（WSL/Debian）：三组件测试、cargo 构建、双冒烟、无孤儿进程与锁残留；
  发现并修复 node:sqlite 运行时约束（见 2.3/5.3）。
- [x] P7 安装器与 Agent 接入：`setup.js` 统一入口 + 10 个配置目标写入。
- [x] P8 维护 Skill：`.agents/skills/mcp-integration-maintenance/`。
- [ ] P9 Agent 接入扩展与交互重构（进行中）：
  - [x] P9.0 调研固定与本地复现记录；jsonc-parser 制品确定并固定（见 11.2）。
  - [x] P9.1 安装器数据保护与 CLI 修复：JSON 读取错误分类与损坏零写入、Codex 旧表边界/
    引号键/子表/多行字符串/未闭合与重叠标记、真实 `--dry-run`、两阶段批量写入、
    `--help`/`--list-agents`、参数与 id 前置校验、`agentStates` 时序、readline 释放（见 11.1）。
  - [x] P9.2 `agents/` 组件：registry/entries/formats/configPlan/fileStore/writers +
    随源码 jsonc-parser；JSONC 注释保留局部编辑；`common/src/agents.mjs` 改为兼容 shim；
    `setup.js` 接入批量 plan/apply（备份/日志/归属/回滚，见 11.2/11.4）。
  - [x] P9.3 新目标接入与迁移：产品数 10→17（pi/CodeBuddy 共用组、Zed/Kimi/Qwen/WorkBuddy/
    MiMo、Kilo 现行格式+`kilocode` 别名、Roo 修正路径）；候选发现（exclusive 冲突中止 /
    priority 首个存在）；Kilo/Roo/CodeBuddy 旧文件迁移清理（验证 command/args 指向本仓库、
    先写新目标后清理、遮蔽冲突中止）；共享组选择/移除规则；.gitignore 补根文件规则（见 11.3）。
  - [ ] P9.4 方向键菜单、行模式、非交互校验、引导导入（含 CodeBuddy IDE/Cline IDE）、
    Cline 显式启动（见 11.5/11.6）。
  - [ ] P9.5 README/Skill/help 与 registry 的一致性终检与执行证据（见 11.7 剩余项）。
  - [ ] P9.6 隔离工程与实际客户端验收（见 11.8 验收矩阵）。

## 10. 相对旧 Python 计划的变更记录（简要）

同一会话早前存在 Python 包装层设计，后改为 Node.js（ESM、无编译步骤）：生态与 CodeGraph/官方 TS SDK
对齐，维护面收敛为 npm + cargo；删除 Windows Job Object 与 POSIX guardian（两后端均有 stdin 生命线
自退出行为，SDK `close()` 内建 EOF→SIGTERM→SIGKILL，残余风险声明见 6.1）；目录收敛为组件自包含、
上游固定清单拆分到各工具；CodeGraph 工具 schema 改为后端派生；首次索引接线补全；tgrep 的 Rust 补丁不变，
重放以 `git apply` 为准。

## 11. P9 设计：Agent 接入扩展与交互重构

### 11.1 已修复的安装器缺陷（P9.1，2026-09-20）

修复并回归覆盖：JSON 读取错误折叠为 null 后整文件覆盖；Codex 无标记旧表删除越界到 EOF；
`--dry-run` 仍执行 prepare/状态写入；多 Agent 逐文件立即写入（无全量预检）；`agentStates` 在依赖
准备前扫描；`Ui` 无 close/finally；`--help` 未实现；README 的 `node --test <目录>` 命令形式错误。
Roo 项目文件名、Kilo 旧格式和多候选发现已在 P9.3 实施，本轮复核其扫描和迁移缺陷（11.10）。

### 11.2 `agents/` 组件与随源码依赖（P9.2 已实施）

`agents/` 为普通源码组件（无 npm 运行时依赖），模块职责：

- `src/registry.mjs`：产品与物理配置目标分离（id/别名/能力/目标/官方证据链接），
  导出 `BACKENDS/SERVER_IDS/agentDefinitions/agentById/targetFor/managedServerIds`。
- `src/entries.mjs`：逐格式 stdio 条目序列化与路径策略（只写该客户端证据支持的字段）。
- `src/formats/jsonDocument.mjs`：JSON/JSONC 解析校验与**局部编辑**（modify/applyEdits），
  损伤分类 `invalid-json`/`duplicate-key`/`not-object`/`map-not-object`。重复键用 JSON.parse
  成功后通过 parseTree 的 property 节点检测（包含键与冒号之间有注释的情况）。编辑保留注释、
  外来值、键序、BOM 与换行风格；删除只移除属性与必要的逗号，保留相邻注释；不承诺任意布局往返
  后空白完全相同。新文件单独格式化，已有文件不启用会重排相邻属性的 formatter。
- `src/formats/codexToml.mjs`：标记段与无标记旧表扫描（边界=下一张真实表头，多行字符串内的
  伪表头不是边界；未闭合/重叠标记中止）。扫描器拆到 `tomlDocument.mjs`，支持引用键和多行数组；
  多行字符串原字节保留，标记内混入外来表中止。内联/点键声明托管服务器时中止，需先迁为表结构；
  此扫描器不替代完整 TOML 语法验证。主配置同名条目需通过 command/args 归属核查。
- `src/configPlan.mjs`：纯规划（注入 `readFile`/`ownsFile`）：共享物理目标去重、同文件后端冲突、
  损坏目标进 problems（批量零写入）、整文件删除需“空无注释托管骨架 + 归属记录”双条件。
- `src/fileStore.mjs`：仓库 realpath 包含校验（经最近存在祖先解析，未建目录亦可判定）、写入前与
  规划原文比对（并发修改中止）、构建目录临时文件 + rename 原子替换（跨文件系统失败则中止）、备份与 JSONL 日志（仅相对路径
  与动作，不含配置值）、归属记录 `<BUILD_DIR>/integration/mcp/state/agent-config-state.json`、
  失败恢复 `tryRestore`（文件被并发修改或删除时保留备份并报告，不覆盖后续操作）。配置替换成功后立即
  记录本轮修改，再写归属/日志；批次失败按逆序恢复，单个恢复异常不阻断其余文件。归属记录含文件标识，
  避免误删用户重新创建的同名文件；恢复同步恢复归属。新文件、备份和元数据请求 0600 权限，更新/恢复保留
  原 POSIX mode；Windows ACL 仍受所在目录权限控制，不声称与 POSIX mode 等价。
- `src/writers.mjs`：批量 plan/apply（problems 非空零写入；apply 中途失败回滚已写文件并汇总）
  与单 Agent 兼容 API（`configureAgent`/`removeAgentServers` 等旧签名，经 plan+apply 实现）。

`common/src/agents.mjs` 仅 re-export writers 公开 API；`agents/` 不反向导入 shim，但可使用
`common/src/paths.mjs` 解析默认目录（state=`<BUILD_DIR>/integration/mcp/state`，
tmp=`<BUILD_DIR>/_agent_tmp/mcp`；备份在 `tmp/agent-config-backups/`）。`setup.js` 的配置写入/卸载
走 `runAgentConfigBatch`。

JSONC 编辑采用 Microsoft `jsonc-parser` 的语法树与 `modify`/`applyEdits`（解析 API 容错返回部分结果，
必须检查错误列表后再检查对象类型/重复键，才允许编辑）。随源码分发制品在
`agents/vendor/jsonc-parser/`（固定 `3.3.1`，MIT，零运行时依赖；tarball URL、sha512/sha256 与加载方式
记录在其 `VENDOR.json`——经 `createRequire` 用 `lib/umd` 构建；包内 `lib/esm` 的 import 无 `.js`
扩展名、Node ESM 无法加载，故未收录）。升级 = 重下制品、校验 integrity、同步 VENDOR.json 与
LICENSE；不为首次菜单执行 `npx`。`agents/package.json` 只承担测试入口。

### 11.3 配置目标、候选与迁移（P9.3 已实施，2026-09-20）

实施记录（契约仍生效；实现见 `agents/src/registry.mjs`、`configPlan.mjs`、`writers.mjs`）：

- 产品 17 个。Claude Code/pi/CodeBuddy CLI 共用 `.mcp.json` 一个目标组：任一 id 选中即保留
  整组（未选中的组员不触发移除），卸载任一 id 影响全组且输出标注全部消费者；
  `--agents=claude,pi,codebuddy` 归并为一次物理写入。pi 标注 pi-mcp-adapter 前置条件
  （capability=extension），配置生成不等于扩展已连接。
- 候选发现：OpenCode（根 json/jsonc）、Kilo（`.kilo/` 的 json(c)，新建默认
  `.kilo/kilo.json`）、MiMo（根与 `.mimocode/` 的 json(c)，新建默认 `.mimocode/mimocode.json`）
  为 exclusive 模式——多个候选并存时中止该轮全部写入。这是安装器的保守限制，客户端加载顺序与合并
  不等同于此限制。Kilo 根 `kilo.json(c)` 与 `.kilo/` 是可共存的层级；根托管条目按 OpenCode 形状迁移
  清理，外来内容不动。单个目标候选原地编辑（JSONC 注释保留）。
- CodeBuddy 旧根 `mcp.json`（priority 模式，官方顺序 `.mcp.json` 优先、只读首个存在文件）：
  仅它存在且只选 CodeBuddy 时原地编辑；需创建 `.mcp.json` 而旧文件含外来服务器时按
  `legacy-shadow-conflict` 中止（预览式迁移未实现，列为 P9.4 交互确认的候选增强）；旧文件
  只含本集成条目时创建 `.mcp.json` 并清理旧条目；两文件并存时只编辑 `.mcp.json`，
  旧文件字节不动。仅旧文件存在时，卸载原地移除本集成，保留外来服务且不新建配置。
- 旧格式迁移（`kilocode`→`kilo` 别名、`.roo/mcp_settings.json`→`.roo/mcp.json`）：新目标
  写入成功后才清理旧文件中的托管条目；清理前验证条目 command/args 指向本仓库包装层，
  同名但非本集成的条目保留并输出提示；旧文件中的外来条目始终保留。`agentStates` 扫描
  候选+legacy：缺失 legacy 是正常状态；仅 legacy 有托管条目时卸载也能清理；多候选并存报 error。
  自动迁移遇到自定义字段/额外参数时中止，先人工迁移并删除旧托管条目再重跑，避免丢失设置；显式卸载可清理。
- 新增条目形状实施前已按官方文档实时复核（Zed `context_servers` 扁平 command/args；Kimi/Qwen
  `mcpServers` 支持 `cwd`；Kilo/MiMo 为 OpenCode 形状；CodeBuddy 优先级规则），来源见 11.11。
- 安装/切换仍是“所选目标作为最终集合”语义；`.gitignore` 已补根文件规则并逐项
  check-ignore 验证（见 11.7）。

仍未实现：CodeBuddy 旧文件含外来服务器时的预览式迁移（当前中止）；共享文件内按客户端
独立开关（明确不在本期承诺）。

### 11.4 文档保护与文件写入

P9.2 已实现（契约仍生效）：损坏分类中止且零写入（语法/重复键/根类型/map 类型/TOML 标记），
损坏或冲突目标进 problems 时健康目标同样零写入；JSONC 注释保留的局部编辑；写入前与规划原文
比对、期间外部修改即中止；构建目录 temp+rename 替换；备份 + JSONL 日志（不含配置值）+ 中途
失败回滚（并发改动保留备份并报告）；整文件删除需归属记录 + 空无注释托管骨架双条件（预存在的
空对象保留，无归属记录或旧记录缺少文件标识时同样保留空骨架）。

仍未实现：中断后的下次运行先检查操作日志再继续（当前靠备份/日志人工核查）；真实 Windows ACL
和 Linux 权限/跨文件系统实测。旧目标清理已实现；本轮增加元数据和日志失败、恢复失败继续处理的故障注入。

### 11.5 交互与 CLI 控制流

已实现（P9.1/P9.2）：`--help`/`--list-agents` 无副作用；未知 id 与空值参数在任何下载/提示前报错；
依赖准备成功后才扫描 Agent 配置（准备前后端建议读 prepared-state，不读 Agent 文件）；
`--dry-run` 完全只读（不 prepare、不写状态、不联网、动作以“拟”前缀打印）；两阶段 plan/apply；
readline 在 finally 中关闭；菜单输出走 stdout、诊断走 stderr。

待实施（P9.4）：方向键选择（Node 内置 `readline.emitKeypressEvents`；TTY+raw mode+ANSI 为
↑↓移动、空格勾选、Enter 确认，多选 a 全选、n 清空；终端不支持光标控制或 `--ui=line` 时编号行
模式；stdin 非 TTY/CI/`--yes` 不打开交互输入，选项不足立即报错且管道 EOF 不能当作默认确认；
Ctrl+C/Escape/EOF/渲染异常在 finally 恢复终端并取消退出非零）。空选择不能静默卸载：TTY 显示
移除摘要确认；非交互只有显式卸载参数才允许清空全部。`--agents=all` 只处理本轮可自动写入的
目标组；需要扩展/启动/面板导入的客户端明确列为待用户操作。显式选择不存在已核实写入路径的
IDE id 时输出引导，不伪造项目配置文件。

### 11.6 pi / Cline / IDE 的接入交付（P9.4 待实施）

pi 选择项注明需要 `pi-mcp-adapter`，链接扩展作者 README 和安装步骤；setup 不自动安装第三方
扩展，也不写未经核实的 pi 配置开关。完成共享文件写入后显示“配置已生成，扩展加载/连接待验证”。
检查高优先级 `.pi/mcp.json` 中本集成条目的冲突，但不自动覆盖用户的 adapter 设置。

Cline CLI 的 `CLINE_MCP_SETTINGS_PATH` 由上游 `resolveMcpSettingsPath()` 明确读取，可作为
不改全局配置的显式接入方式。计划在用户选择 `cline` 时生成 `.cline/atsf4g-mcp.json`，
再给出 `node <PROJECT_DIR>/project/integration/mcp/agents/tools/launch.mjs --agent=cline -- ...`
的启动命令；这个文件名是本安装器自定义导出路径，Cline 不会自动发现它。启动器仅对子进程
设置绝对 `CLINE_MCP_SETTINGS_PATH`、仓库 cwd，以参数数组调用经验证的 Cline CLI 入口，
不修改用户环境、不生成平台 shell 脚本，也不在 setup 结束后自动启动 Agent。
显式路径替换默认 MCP 配置源，不合并全局服务器；说明该效果，保留导出文件里的用户补充条目。
Windows 的原生可执行文件与 npm shim 必须分别验证，不把 `.cmd` 当可直接 `shell:false` 执行。
若实际发布版不支持此环境变量，保持引导导入状态，不能输出“已自动接入”。

Cline IDE、CodeBuddy IDE 通过各自官方面板导入生成的片段；可以把片段放在
`<BUILD_DIR>/integration/mcp/exports/`，只含本次条目与本机路径。不读取/改写 IDE 全局文件，
不假定它们会读取 CLI 项目配置。Cline CLI 的默认路径按源码是
`~/.cline/data/settings/cline_mcp_settings.json`，还受 `CLINE_DATA_DIR`/显式路径影响；
官方 MCP 概览仍写 `~/.cline/mcp.json`，此处以实际 resolver 为准，不将这一默认路径套给所有 IDE。

### 11.7 `.gitignore` 与相关文档（ignore 规则已随 P9.3 补齐；终检待 P9.5）

已补齐并 `git check-ignore --no-index -v` 逐项验证：根 `/mcp.json`、`/kilo.json`、
`/kilo.jsonc`、`/mimocode.json`、`/mimocode.jsonc`（限定仓库根，不影响 vendored 子项目；
`.vscode/mcp.json` 已被 `.vscode/*` 覆盖且 `!.vscode/tasks.json` 例外保留；目录类候选
`.zed/`、`.kimi-code/`、`.qwen/`、`.workbuddy/`、`.kilo/`、`.mimocode/` 等此前已覆盖）。
负向检查 `project/integration/mcp/` 源码与 Skill 文件仍可提交。README 矩阵/迁移说明与
维护 Skill references 已随 P9.3 同步。P9.5 剩余：`--help`/`--list-agents` 输出与 registry
的一致性终检、客户端实测后的一次性文档核对；ignore 不移除已被跟踪的文件。

### 11.8 分阶段执行与验收

| 阶段 | 状态 | 完成条件 |
| --- | --- | --- |
| P9.0 | 已完成（2026-09-20） | 调研固定与官方样例；本地复现；jsonc-parser 制品确定（11.2/VENDOR.json） |
| P9.1 | 已完成（2026-09-20） | 原错误用例先失败后通过；损坏输入零写入；完整 dry-run 无 prepare 调用和文件变化 |
| P9.2 | 已完成（2026-09-20） | 原 10 个目标行为有回归覆盖；变更计划与 I/O 分离；批量失败和共享目标去重通过；JSONC 注释保留编辑落地 |
| P9.3 | 已完成（2026-09-20） | 官方文档实时复核后实施；候选/迁移/共享组各有独立测试；多候选与遮蔽冲突中止零写入 |
| P9.4 | 待实施 | 状态机测试 + Windows/Linux 终端验证；未装客户端/扩展可解释退出；取消和 EOF 不挂住 |
| P9.5 | 待实施 | 实际候选路径逐项 check-ignore；帮助/文档与 registry 一致；没有将导出称为连接成功 |
| P9.6 | 待实施 | 按下表记录操作系统、Node/客户端版本、加载文件、工具发现、调用、关闭结果；未测条目标未验收 |

P9.6 客户端验收矩阵（必测场景 → 预期）：

| 验收面 | 必测场景 | 预期 |
| --- | --- | --- |
| 文档编辑 | JSON、带注释/尾逗号 JSONC、CRLF/BOM、无效语法、null/数组 map、重复键、权限错误 | 合法文档外来内容保持；非法目标全批次配置零写入；未知可选字段不丢失 |
| TOML | 标记块、无标记旧表、引用键、env 子表、后续外来表、多行字符串、缺 END、重复标记 | 精确编辑托管范围；损坏不改写；控制字符转义正确 |
| 目标选择 | pi/Claude/CodeBuddy 单选和多选、共享卸载、Kilo 旧 id、json/jsonc 多候选、旧根 mcp.json | 一次物理写入；摘要列影响面；无后写覆盖前写；冲突有明确结果 |
| 文件恢复 | 二次运行、同名外来条目、预存空骨架、并发编辑、symlink/junction、写第 N 个失败/中断 | 幂等无写；不误删/越界；恢复结果可核查，不能声称跨文件原子事务 |
| 安装编排 | 无效 flags/id、prepare 失败、offline、skip-prepare、dry-run（不加 skip）、卸载、help/list | 依赖失败不读写 Agent 配置；dry-run/help/list 无副作用；卸载不准备依赖 |
| 终端 | 上下边界、单/多选、共享组提示、全选/清空、窄终端、resize、Escape/Ctrl+C/EOF、stdout 重定向 | 选择正确；流/光标/raw mode 恢复；非 TTY 不等待输入；测试设截止时间但不放宽断言 |
| 客户端 | 实际版本分别加载生成文件，initialize/tools/list、status/查询、关闭 | 不只验证 JSON 可解析；记录用户信任步骤、扩展依赖和单实例限制 |

测试通过依赖注入的 prepare/fileStore/UI 调用边界检查副作用，使用假后端与固定输入，不能下载
真实 Agent 来伪装单元测试。临时工程放 `<BUILD_DIR>/_agent_tmp/mcp/`，Windows 和 Linux x64
分别运行；平台不具备时保留未验收，不以 Windows 单测代替其他平台或 GUI 客户端。
四个组件用各自真实 package script（Windows 可用 `npm.cmd`）：

```text
npm --prefix project/integration/mcp/agents test
npm --prefix project/integration/mcp/common test
npm --prefix project/integration/mcp/tgrep test
npm --prefix project/integration/mcp/codegraph test
```

CLI 集成测试在隔离工程运行完整 install → 重跑 → 切换两次 → uninstall → 重复 uninstall，
再跑相同组合的 dry-run。对仓库与用户目录的文件集合/内容作前后比较，并观察无子进程下载。
外来设置中放哨兵值；故意损坏最后一个目标，验证前面的目标也未被改写。
现有两个后端采用单写入者契约，各客户端实测应逐个连接/关闭；多 Agent 同时读取同一后端
预期仍是 `INDEX_IN_USE`，P9 不引入共享 daemon，也不承诺多客户端同时可用。

### 11.9 执行证据摘要

- 2026-09-18：P0–P8 验收（Windows x64 + WSL/Debian，三组件 38+ 测试、真实后端双冒烟、
  无孤儿进程与锁残留；macOS/arm64 未验证）。
- 2026-09-20 第一轮：计划修订 + 缺陷隔离复现（JSONC 覆盖、TOML 越界删除、dry-run 副作用）；
  测试基线 43/43（显式文件列表）。
- 2026-09-20 第二轮（P9.1）：新增 19 项回归测试先失败后通过；三组件 62/62；CLI 冒烟
  （`--help`/`--list-agents`/install+uninstall dry-run/非法参数）`git status` 前后一致。
- 2026-09-20 第三轮（P9.2）：vendor jsonc-parser 3.3.1（registry integrity 校验一致，UMD 落盘 +
  VENDOR.json；API smoke：注释保留、删除字节级还原、嵌套路径创建、CRLF/eol、空文件按损坏拒绝）；
  `agents/` 组件 + 44 项测试；`common` shim 测试；`setup.js` 接入批量 plan/apply；四组件合计
  83/83（agents 44 + common 23 + tgrep 8 + codegraph 8，Windows x64/Node 24）；CLI dry-run 冒烟
  复验通过（工作区零改动）。
- 2026-09-20 第四轮（P9.3）：实施前实时复核六家官方文档（Zed/Kimi/Qwen/Kilo/MiMo/CodeBuddy，
  确认 `context_servers` 扁平字段、`mcpServers` 的 `cwd` 支持、OpenCode 形状、`.mcp.json` >
  旧 `mcp.json` 优先级与“只读首个存在文件”）；registry 扩至 17 产品 + 候选/legacy 声明；
  configPlan 按 target 分组与候选解析重写；新增 10 项 targets 测试（候选冲突、CodeBuddy 三种
  回退、Kilo/Roo 迁移与防误认领、legacy-only 卸载、共享组选择/移除）。四组件 93/93
  （agents 54 + common 23 + tgrep 8 + codegraph 8）；CLI dry-run 冒烟（新目标组合 install、
  uninstall）零改动；真实工作区 `.kilo/kilo.json` 与 `.kilo/kilo.jsonc` 并存触发候选冲突中止，
  行为符合设计。.gitignore 补 5 条根文件规则并逐项 check-ignore（含子项目不误伤负向检查）。
  未执行：真实客户端写入/连接验收（P9.6）、WSL 复验、CodeBuddy 旧文件含外来条目的预览式
  迁移（按设计中止）。

### 11.10 未提交代码审查（2026-09-20）

本轮检查 P9.0–P9.3 的工作区版本并修复缺陷，不提前实现 P9.4–P9.6。
四组件基线实际为 83/93：Kilo 的结构化 legacy 描述尚未接入读取端，导致扫描和迁移失败；
历史 93/93 记录不代表当前未提交版本。审查还发现 Codex 空文件卸载、标记块检测与多行字符串
清理、JSONC 重复键检测、旧文件卸载，以及写入后元数据失败的恢复问题。

修复以真实文件往返、故障注入和并发修改回归为依据。先验证失败，再检查四组件测试、隔离工程
install → 重跑 → 切换 → uninstall，以及真实工作区 dry-run 的文件内容不变。
写入与恢复都检查仓库路径边界；批次失败应尝试恢复所有已经发生的配置修改，单个恢复失败
不得阻断其他恢复。进程崩溃后的自动恢复和真实客户端连接仍需分别验收。

已完成修复与验证：四组件 **117/117**（agents 78 + common 23 + tgrep 8 + codegraph 8，
Windows x64 / Node 24.21.0）。新增 24 项回归，其中最初 12 项在修复前均失败；正式
`agents/package.json` 测试入口已包含新增文件。隔离 CLI 覆盖全部 17 产品的安装、无写入重跑、
两次后端切换、卸载/重复卸载、别名、非法参数、非 TTY、依赖失败先于配置读取；用户目录哨兵保持不变。
JSONC 相邻注释、TOML 字符串/表边界、同名外来服务、旧配置 BOM、迁移自定义字段、状态/日志异常、
恢复异常继续处理和仓库外 junction 均有回归。vendor 包 SHA256 及 11 个文件与官方 3.3.1 制品一致。

真实工作区 `--help` / `--list-agents` 返回 0；两个后端 install dry-run 与 uninstall dry-run
均因现存 `.kilo/kilo.json` / `.kilo/kilo.jsonc` 并存而按当前限制返回 1。检查了 27 个配置/状态路径
的内容哈希和完整 `git status`，前后一致；未整理或覆盖用户这两个配置文件。`doctor` 显示 tgrep 已停止、
CodeGraph ready 且 PID 20964 正持有锁，因此本轮不把 doctor 记为全绿，也未重启现有实例。
未执行：真实 Agent 连接、Linux/macOS、本轮真实后端首次索引、进程崩溃重启恢复。

### 11.11 调研证据（2026-09-20）

以下均为官方站点或对应项目/扩展作者源码，不用第三方客户端汇总表证明路径。腾讯 CLI、IDE、
WorkBuddy 页面通过官方 URL 直接读取正文；网页工具超时时用 PowerShell 读取同一页面。滚动 `main`
仅作参考；实施时应为 fixture 补实际发布版本或固定提交。

- Claude Code：[MCP 配置与项目作用域](https://code.claude.com/docs/en/mcp)。
- pi：[pi 本体 README 的 No MCP](https://github.com/badlogic/pi-mono/blob/main/packages/coding-agent/README.md)；
  [pi-mcp-adapter 作者 README](https://github.com/nicobailon/pi-mcp-adapter/blob/main/README.md)。
- 腾讯 CodeBuddy：[CLI MCP](https://www.codebuddy.cn/docs/cli/mcp)；[IDE MCP](https://www.codebuddy.cn/docs/ide/User-guide/MCP)。
- WorkBuddy：[官方 MCP 指南](https://www.codebuddy.cn/docs/workbuddy/From-Beginner-to-Expert-Guide/Function-Description/MCP-Guide)。
- Codex：[官方 MCP 配置](https://learn.chatgpt.com/docs/extend/mcp?surface=cli)。
- VS Code：[MCP 配置](https://code.visualstudio.com/docs/agent-customization/mcp-servers)。
- Cursor：[MCP 配置位置与变量插值](https://cursor.com/docs/mcp)。
- Gemini：[MCP 配置与 cwd](https://geminicli.com/docs/tools/mcp-server/)。
- OpenCode：[MCP](https://opencode.ai/docs/mcp-servers/) 与 [配置文件/JSONC](https://opencode.ai/docs/config/)。
- Kilo：[当前 MCP 配置](https://kilo.ai/docs/automate/mcp/using-in-kilo-code) 与
  [设置说明](https://kilo.ai/docs/getting-started/settings)、
  [config.ts 的根配置与配置目录加载/merge 流程](https://github.com/Kilo-Org/kilocode/blob/main/packages/opencode/src/config/config.ts)。
- Roo：[官方文档](https://roocodeinc.github.io/Roo-Code/features/mcp/using-mcp-in-roo/) 与
  [McpHub 的项目路径/监视器](https://github.com/RooCodeInc/Roo-Code/blob/main/src/services/mcp/McpHub.ts)。
- ZCode：[官方配置路径与加载优先级](https://www.zcode.network/en/docs/mcp-services/)。
- OMP：[官方 MCP 配置](https://github.com/can1357/oh-my-pi/blob/main/docs/mcp-config.md)。
- Zed：[MCP 字段示例](https://zed.dev/docs/ai/mcp)、[项目 settings 位置](https://zed.dev/docs/migrate/vs-code)、
  [ProjectSettings/context_servers 与工作区信任](https://github.com/zed-industries/zed/blob/main/crates/project/src/project_settings.rs)。
- Kimi Code：[官方 MCP 文档源码](https://github.com/MoonshotAI/kimi-code/blob/main/docs/en/customization/mcp.md)。
- Qwen：[官方 MCP 项目作用域与字段](https://qwenlm.github.io/qwen-code-docs/en/users/features/mcp/)。
- MiMo：[官方 MCP](https://mimo.xiaomi.com/mimocode/mcp-servers) 与
  [config.ts 的 mimocode.json/jsonc 加载循环](https://github.com/XiaomiMiMo/MiMo-Code/blob/main/packages/opencode/src/config/config.ts)。
- Cline：[MCP 概览](https://docs.cline.bot/mcp/mcp-overview)、
  [resolveMcpSettingsPath 源码](https://github.com/cline/cline/blob/main/sdk/packages/shared/src/storage/paths.ts)、
  [文档错误报告 #11671](https://github.com/cline/cline/issues/11671)、
  [项目作用域需求 #13596](https://github.com/cline/cline/issues/13596)。
- JSONC：[Microsoft node-jsonc-parser](https://github.com/microsoft/node-jsonc-parser)，
  parse 错误列表与 modify/applyEdits API；制品已固定（见 11.2）。
- TOML：[官方 1.0 规范](https://toml.io/en/v1.0.0)，引用键、字符串转义、多行字符串与表边界。

### 11.12 CodeGraph 提示词补丁

在 `setup.js` 为至少一个 Agent 配置 CodeGraph 时，将条件式使用说明更新到仓库根 `AGENTS.md`。
提示词使用 `<!-- CODEGRAPH_START -->` / `<!-- CODEGRAPH_END -->` 标记；当前 `CLAUDE.md` 已导入
`AGENTS.md`，保持该入口结构。补丁与 MCP 配置同批规划/写入，依赖准备失败、损坏配置或损坏标记时均不写入。
预览仅报告提示词动作；切换 tgrep 和卸载不删除仍可供本地 CLI 使用的说明，也不删除索引。

补丁按文本内容比较标记间的正文，忽略 UTF-8 BOM、UTF-16 LE/BE BOM 和 LF/CRLF/CR 的编码/换行差异。
相同则保留文件原字节、mtime、备份和日志；变化时只替换标记间正文，保留原编码、BOM、换行和块外内容。
无标记时追加一个块；重复、反向或未闭合标记中止；Markdown 围栏中的示例不作为托管块。
无法可靠解码的输入中止，不猜测本地代码页。文件存储增加 Buffer 路径，恢复与并发比较仍按原字节执行。

说明依据固定 1.6.0 制品核对：`CODEGRAPH_DIR` 是根目录下的单段目录名；CLI `explore` 复用 MCP handler，
但不是保证可用的后备方案。优先使用已准备的固定版本入口，避免无版本 `npx` 自动下载；索引忙碌、未就绪、
缺失或覆盖不足时回退 `rg`/源码。结构图不保证完整 C++ 动态派发语义；不因提示词要求自行初始化或重建索引。

验收包括多编码/换行等价无写入、带中文的块外内容保留、标记异常零写入、批次失败按原字节恢复、
CLI CodeGraph 安装/重跑/预览/依赖失败，以及四组件完整测试。CodeGraph 运行时和首次索引逻辑不改动。

已完成实现及验证（2026-09-20）：四组件 **127/127**（agents 88 + common 23 + tgrep 8 + codegraph 8，
Windows x64 / Node 24.21.0）。新增 8 项补丁/恢复测试和 2 项 CLI 流程测试；并发重编码时保留用户字节，
同时恢复本轮先前写入的配置。当前仓库根 `AGENTS.md` 已通过同一补丁模块更新，原有前缀字节保持不变；
再次执行为 unchanged，内容和 mtime 均不变，`CLAUDE.md` 未改动。

真实工作区预览检查覆盖 30 个配置、提示词和状态/日志路径的哈希及完整 `git status`，前后一致。
全 Agent 预览仍因现存两份 Kilo 候选配置中止（见 11.10）。固定版本 CLI `explore --help` 已实测；
当前 MCP 探索返回 `INDEX_IN_USE`，未停止其他实例或修改索引。真实客户端连接和 Linux/macOS 本轮未验收。

## 参考来源

固定到本轮调研的提交与包版本：

- tgrep 源码：`https://github.com/microsoft/tgrep/tree/239711cfb6e69e8780cabf912a8987162a223ff1`
  （`tgrep-cli/src/main.rs`、`tgrep-cli/src/serve.rs`）
- CodeGraph 源码：`https://github.com/colbymchenry/codegraph/tree/ba3c21e50d9129d2f5f3843ec3728868ae6d47a1`
  （`src/mcp/index.ts`、`src/mcp/engine.ts`、`src/mcp/stdin-teardown.ts`、`src/mcp/liveness-watchdog.ts`、
  `src/mcp/tools.ts`、`src/directory.ts`、`src/index.ts`）
- npm：`@colbymchenry/codegraph@1.6.0`、`@modelcontextprotocol/server@2.0.0`、
  `@modelcontextprotocol/client@2.0.0`、`jsonc-parser@3.3.1`
  （`packages/client/src/client/stdio.ts` 行为已核对）
- MCP 规范与 SDK 文档：`https://github.com/modelcontextprotocol/typescript-sdk`（v2 文档）
