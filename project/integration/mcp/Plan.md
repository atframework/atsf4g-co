# tgrep / CodeGraph MCP 集成计划（Node.js 实现）

状态：设计已定稿并开始实现（2026-09-18）。本文件是实施与验收依据。

本计划取代同一会话早前的 Python 包装层设计。变更原因与差异见第 10 节。

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

### 2.1 工作区

- `.vscode/settings.json` 未设置 `cmake.buildDirectory`，clangd 参数指向
  `${workspaceFolder}/build_jobs_cmake_tools`，因此本工作区 `<BUILD_DIR>` 为 `build_jobs_cmake_tools`。
  包装层状态、运行时、上游源码与日志放在 `<BUILD_DIR>/integration/mcp/` 下。
- 本机工具链（2026-09-18 实测）：Windows 侧 node v24.21.0、npm 12.0.2、cargo/rustc 1.98.0；
  WSL/Debian 侧（独立 ext4 clone）node v20.19.2、corepack npm@10、cargo/rustc 1.98.1。
  包装层要求系统 Node >= 20（SDK engines 约束）；CodeGraph 后端与首次索引 helper 使用
  其平台包捆绑的 Node 24 运行时，不依赖系统 Node（见 2.3 的 node:sqlite 约束）。
- `project/integration/Bootstrap.cmake` 没有 MCP 初始化逻辑。不让普通 CMake configure/build 隐式启动索引进程。
- 根 `.gitignore` 已忽略 `/build_*`；`.codegraph*` 索引目录需要补充忽略条目（见 5.3）。

### 2.2 固定调研基线

| 项目 | 基线 | 使用方式 |
| --- | --- | --- |
| tgrep | 源码提交 `239711cfb6e69e8780cabf912a8987162a223ff1`（1.0.9，Rust edition 2024）+ 本集成 stdio 补丁 | cargo release 构建，固定于 `tgrep/upstream-lock.json` |
| CodeGraph | npm `@colbymchenry/codegraph@1.6.0`（源码提交 `ba3c21e50d9129d2f5f3843ec3728868ae6d47a1`），平台包 `@colbymchenry/codegraph-<platform>-<arch>` | 平台包捆绑独立 Node 24 运行时，固定于 `codegraph/upstream-lock.json` |
| MCP SDK | npm `@modelcontextprotocol/server@2.0.0`、`@modelcontextprotocol/client@2.0.0`（engines: node >= 20） | 官方 TS SDK，低层 Server API + StdioClientTransport |

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

第一版每个“规范化工作树根目录 + 工具 + 状态目录”只允许一个 MCP 写入实例。两个不同工具可以同时运行；
同工具的第二个实例返回明确的 `INDEX_IN_USE`，不自动接管、杀死或复用无主进程。

### 3.1 目录布局

`project/integration/mcp` 下只有三个组件目录、一个安装入口和两份顶层文档；每个组件自包含
自己的源码、测试、包管理信息与上游固定清单，公共代码与流程工具只放在 `common` 一个目录：

```text
project/integration/mcp/
  Plan.md                         本文件
  README.md                       安装入口、各 Agent 配置示例、路径策略、故障排查
  setup.js                        安装入口：后端二选一 → 镜像 → 依赖准备 → Agent 配置写入/卸载
  common/
    package.json                  依赖 @modelcontextprotocol/server（mcpServer.mjs 使用）
    src/
      paths.mjs                   仓库根/构建目录解析、路径包含判断、scope 校验
      errors.mjs                  错误码与 BackendError
      limits.mjs                  限额与整记录截断
      state.mjs                   原子状态记录、实例锁（O_EXCL + pid 存活检查）
      supervisor.mjs              受管子进程：spawn/EOF 优先停机/有界 stderr 汇
      lineRpc.mjs                 行分隔 JSON-RPC 客户端（tgrep 内部协议）
      mcpServer.mjs               runWrapperServer：低层 Server + 固定工具表 + 生命周期接线
      prepare.mjs                 依赖准备库（npm/cargo/npm pack，固定版本与哈希）
      agents.mjs                  各 Agent 项目级配置的写入/合并/卸载（可单测）
    tools/
      doctor.mjs                  只读诊断
    test/                         node --test 单元测试
  tgrep/
    package.json                  测试用 devDependencies（client SDK）；运行时依赖 common（相对导入）
    upstream-lock.json            tgrep 源码提交、补丁、构建命令
    src/server.mjs                MCP 入口 + 三工具
    src/backend.mjs               补丁版 tgrep serve 子进程适配
    patches/0001-serve-add-private-stdio-transport.patch
    test/                         假后端 + 协议/生命周期测试
  codegraph/
    package.json                  依赖 @modelcontextprotocol/client（backend.mjs 使用）+ 测试 devDependencies
    upstream-lock.json            npm 包、版本、制品哈希、调用方式、强制环境
    src/server.mjs                MCP 入口 + 工具代理 + status
    src/backend.mjs               SDK Client/StdioClientTransport 适配 + 首次索引触发
    src/initialize.mjs            库初始化 helper（运行于捆绑运行时，一次性进程）
    test/                         假后端 + 协议/参数隔离测试
```

跨组件引用只用相对路径导入 `../common/src/*.mjs`。`common` 的 SDK 依赖从 `common/node_modules`
解析（解析基于文件物理位置），`tgrep`、`codegraph` 各自声明并安装自己直接 import 的包；
测试用的 client SDK 由各工具自己的 devDependencies 提供。

### 3.2 安装入口与依赖准备

安装统一入口为 `node project/integration/mcp/setup.js`（依赖准备库在
`common/src/prepare.mjs`，不再有独立的 prepare CLI）：

1. 选择检索后端：tgrep / CodeGraph 同类二选一；重复执行可切换（旧后端条目从所有
   已配置 Agent 中移除后写入新后端）。
2. 选择下载镜像：`cn`（npm 用 npmmirror、cargo 用 rsproxy 的 `--config` 传入，不修改
   全局 npm/cargo 配置）或 `official`。未指定时按时区/语言建议国内环境用 `cn`。
   tgrep 源码始终从 GitHub 固定提交拉取并校验提交号，不经镜像。
3. 依赖准备（固定提交/版本 + 哈希校验，产物在 `<BUILD_DIR>/integration/mcp/` 下）：
   选中后端的组件目录 `npm install`（lockfile 存在时 `npm ci`）；
   tgrep 为浅克隆 → 校验提交 → 应用补丁 → `cargo build --release`；
   CodeGraph 为 `npm pack` 平台包 → 校验 sha1 → 解包 → 验证捆绑 Node 与入口。
4. **依赖准备全部成功后才进入 Agent 配置阶段**；任何失败（网络、构建、哈希不符）都直接
   报错退出，不改动任何 Agent 配置。
5. Agent 配置写入/卸载由 `common/src/agents.mjs` 完成：仅仓库内项目级文件、合并保留
   用户已有条目、按标记幂等、卸载时可整体移除本集成条目（详见 README 的 Agent 矩阵与
   路径策略：VS Code 用 `${workspaceFolder}`、Codex 用 `cwd`+相对路径，其余写绝对路径）。
6. 正常 MCP 启动只使用固定的本地程序（`prepared-state.json` 指向的绝对路径）。
   缺失依赖时 stderr 给出确定的准备步骤并退出；不在握手中下载、升级或弹出终端。
7. MCP 客户端配置形如 `command=node, args=[<repo>/project/integration/mcp/<tool>/src/server.mjs]`。
   `--repo-root`/`--build-dir` 可选（仓库根缺省从入口文件位置推导并校验
   `project/integration/mcp/Plan.md` 标记；构建目录缺省 `build_jobs_cmake_tools`）；
   不依赖宿主当前目录，不向父目录扩大扫描。
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
- 限额（common/src/limits.mjs，初值沿用已实现版本）：请求 256 KiB、模式长度 4096 字符、
  匹配行默认 200、上下文每侧 3 行、文件列表 2000 条、响应 2 MiB、单查询超时 60 秒、
  每后端并发 1。截断发生在完整记录边界并返回 `truncated`。
- 后端不支持可靠取消时，取消/超时结束本次等待；查询超时后该后端标记不可用直到实际计算结束，
  不允许遗留继续消耗 CPU 的查询（初版以“串行 + 超时”实现该约束）。
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
  见 2.3 的 node:sqlite 约束；stdout 逐行 JSON 进度供状态展示），完成后启动后端。
- 已存在且有效的根 `.codegraph`（上游默认名）优先原地复用；不自动搬移。
- 索引目录是持久数据，位于仓库根（`CODEGRAPH_DIR` 只接受根下单段名，无法指向构建目录）；
  在根 `.gitignore` 补充 `/.codegraph*` 忽略。包装层日志/运行时/临时文件仍全部位于 `<BUILD_DIR>`。
- Windows 与 WSL 使用不同 `<platform>` 目录名，天然隔离，不共享 SQLite 目录。
- 索引目录包含 `codegraph.db` 与 WAL，按源码数据保护，不提交、不打包进 CI artifact。

## 6. 关闭与异常清理

### 6.1 设计：stdin 生命线 + 分级停机，不再使用 Job Object / guardian

旧 Python 方案为“父进程被强杀”实现了 Windows Job Object 与 POSIX guardian 独立监管者。
本轮按已核实的上游生命周期事实改用 stdin 生命线方案，并明确残余风险：

- 正常关停（包装层收到 EOF/SIGINT/SIGTERM/内部异常）：
  1. 停止接受新查询；
  2. 关闭后端 stdin（EOF）。tgrep 补丁版按 SIGINT 同语义退出并释放 `serve.lock`；
     CodeGraph direct 模式在 stdin `end/close/error` 时 `stop()` 退出并释放 `writer.pid`；
  3. 等待宽限期（5 秒）后 SIGTERM，再等待后 SIGKILL，只针对本实例创建的 pid。
     CodeGraph 腿的该顺序由 SDK `StdioClientTransport.close()` 内建（2 秒/2 秒梯度）；
     tgrep 腿由 `common/supervisor.mjs` 实现；
  4. 回收状态文件与实例锁。
- 包装层被 SIGKILL：子进程 stdin 管道随句柄关闭而断开——tgrep 补丁版与 CodeGraph direct 模式
  都在 stdin EOF 时退出；CodeGraph 另有 PPID watchdog 兜底，其 watchdog 子进程再经自己的 stdin
  生命线退出。不依赖包装层代码执行。
- 残余风险（如实声明）：后端进程在 EOF 之前已彻底卡死（如原生代码死循环）且父进程同时被 SIGKILL 时，
  用户态 stdin 生命线无法生效。发生时由用户经 `doctor` 报告的 pid 定向清理。
  该风险与旧方案的 guardian 单点失效风险同级，但实现成本大幅下降；不做跨平台内核级保证的声明。

只清理本实例创建且仍可证明身份的进程；不按进程名扫描、不 `taskkill /IM`。
正常关闭保留持久索引；删除的只是管道、会话记录和临时输出。

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
   node_modules 等）决定，`atframework` 源码默认纳入（结构导航需要），其构建/缓存输出经
   `codegraph.json` 或默认规则排除；如需调整，先在本地配置验证再改 `codegraph/upstream-lock.json` 记录。
4. 索引含源码派生数据：不提交 Git、不上传 CI artifact；默认日志只记录相对路径、错误类别、耗时和数量；
   后端 stderr 写入有界文件（`backend.stderr.log`，轮转上限 256 KiB）。
5. 安装后用禁止外联的环境验证初始化、查询、刷新与关闭均可用（测试以最小网络环境执行）。

## 8. 测试与验收

测试框架用 Node 内置 `node:test`，不引入额外测试依赖。假后端（`.mjs`）模拟真实协议面；
真实后端冒烟在本机 Windows x64 执行，其余平台在执行前记为未验证。测试数据放
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

平台矩阵（2026-09-18 实测）：Windows x64（原生）与 Linux x64（WSL/Debian）已按上表执行并通过，
包括真实后端冒烟、进程回收与锁释放检查；macOS（x64/arm64）、Linux arm64、Windows arm64 未验证。
等待断言使用状态/内容条件加固定截止时间，不用任意 sleep。

## 9. 分阶段执行

- [x] P0 依赖固定：SDK 版本与 API、CodeGraph 平台包结构、tgrep 补丁协议、生命周期证据核实完成。
- [x] P1 公共层：`common`（paths/errors/limits/state/supervisor/lineRpc/mcpServer）+ prepare/doctor。
- [x] P2 tgrep MCP：三工具、内部 RPC 适配、状态映射、假后端协议测试。
- [x] P3 CodeGraph MCP：SDK client 桥接、后端派生 schema、projectPath 隔离、首次索引接线、假后端测试。
- [x] P4 真实后端冒烟（本机 Windows x64）：tgrep 补丁构建、两服务对本仓库初始化/查询/关闭回收。
- [x] P5 文档：README 与本计划同步实际命令；.gitignore 补充。
- [x] P6 Linux x64 验证（WSL/Debian，系统 Node 20 + 捆绑 Node 24）：三组件 38/38 单元测试、
  tgrep cargo 构建 + 首次索引 ~1s、codegraph 首次索引 ~14s（期间发现并修复 node:sqlite
  运行时约束，见 2.3/5.3）、双冒烟通过、无孤儿进程与锁残留；linux-x64 包 shasum 已回写
  `codegraph/upstream-lock.json`。macOS arm64 及其他架构仍未验证。
- [x] P7 安装器与 Agent 接入：`setup.js` 统一入口（后端二选一、镜像选择、依赖准备失败
  不触碰 Agent 配置、可重复执行切换/升级/卸载）、`common/src/agents.mjs` 覆盖 11 个
  Agent 的项目级配置写入（含单元测试）；各 Agent 配置格式按调研时的官方文档逐一核实，
  汇总与路径策略（cwd/变量支持差异）见 README。文档与示例统一用 `<PROJECT_DIR>` 占位符。
- [x] P8 维护 Skill：`.agents/skills/mcp-integration-maintenance/`（仅维护本集成时加载，
  入口精简，详细步骤放 references）。

## 10. 相对旧 Python 计划的变更记录

1. **生态切换**：包装层、测试、流程工具全部改为 Node.js（ESM，无编译步骤）。
   CodeGraph 本身是 Node 生态，官方 TS SDK 与其后端共享一套协议栈；跨生态（Python venv +
   mcp SDK + Rust 构建 + npm pack）的维护面收敛为 npm + cargo。
2. **进程管理简化**：删除 Windows Job Object 与 POSIX guardian（约 800 行平台特定代码）。
   依据：两个后端在固定版本下都有 stdin 生命线自退出行为（2.3/2.4 节证据），SDK
   `StdioClientTransport.close()` 已内建 EOF→SIGTERM→SIGKILL。残余风险声明见 6.1。
3. **目录结构简化**：`project/integration/mcp` 下仅 `common`、`tgrep`、`codegraph` 三个组件目录；
   顶层 Python 打包文件（pyproject/requirements）、共享 `tests/`、`tools/`、`debug_client.py` 移除；
   测试与包管理信息随各组件目录走；上游固定清单拆分为各工具自己的 `upstream-lock.json`。
4. **CodeGraph 工具 schema 改为后端派生**：不再硬编码参数 allowlist（旧实现与 v1.6.0 实际
   schema 已有出入，如 callers/callees 用 `symbol`/`file` 而非 `id`/`depth`）。
5. **首次索引接线补全**：旧实现的 `initialize.mjs` 未被调用；本轮接入 CodeGraph 服务启动流程。
6. tgrep 的 Rust 补丁不变（与包装层语言无关）；`apply_edits.py`（补丁再生成辅助脚本）移除，
   补丁重放以 `git apply` 为准，重生成流程在维护文档中说明。

## 参考来源

固定到本轮调研的提交与包版本：

- tgrep 源码：`https://github.com/microsoft/tgrep/tree/239711cfb6e69e8780cabf912a8987162a223ff1`
  （`tgrep-cli/src/main.rs`、`tgrep-cli/src/serve.rs`）
- CodeGraph 源码：`https://github.com/colbymchenry/codegraph/tree/ba3c21e50d9129d2f5f3843ec3728868ae6d47a1`
  （`src/mcp/index.ts`、`src/mcp/engine.ts`、`src/mcp/stdin-teardown.ts`、`src/mcp/liveness-watchdog.ts`、
  `src/mcp/tools.ts`、`src/directory.ts`、`src/index.ts`）
- npm：`@colbymchenry/codegraph@1.6.0`、`@modelcontextprotocol/server@2.0.0`、
  `@modelcontextprotocol/client@2.0.0`（`packages/client/src/client/stdio.ts` 行为已核对）
- MCP 规范与 SDK 文档：`https://github.com/modelcontextprotocol/typescript-sdk`（v2 文档）
