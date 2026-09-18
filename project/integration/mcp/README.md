# atsf4g-co MCP 集成（tgrep / CodeGraph）

本目录提供两个面向 AI Agent 的 stdio MCP 服务，安装时二选一，均只作用于本仓库：

- **tgrep**（`tgrep/`）：快速文本/正则检索，后端为打过 stdio 传输补丁的固定版本
  [microsoft/tgrep](https://github.com/microsoft/tgrep)。
- **CodeGraph**（`codegraph/`）：结构化代码导航（符号、引用、影响面），后端为固定版本的
  [CodeGraph](https://github.com/colbymchenry/codegraph) direct 模式（禁 daemon、禁遥测）。

设计、验收边界与上游事实见 [Plan.md](Plan.md)。公共库与流程工具在 `common/`；
三个组件目录各自独立（源码、测试、包管理信息、上游固定清单都在自己目录内）。

## 安装（推荐入口）

```bash
node <PROJECT_DIR>/project/integration/mcp/setup.js
```

安装器会依次完成：选择检索后端（二选一）→ 选择下载镜像 → 准备本地依赖（全部固定版本）
→ 依赖就绪后把 MCP 服务写入所选 Agent 的**项目级**配置。依赖准备失败时不会改动任何
Agent 配置。可重复执行，用于切换后端、升级固定制品、为更多 Agent 接入或卸载。

常用选项：`--backend=tgrep|codegraph`、`--agents=<id,...>|all`、`--mirror=cn|official`
（未指定时按系统地区建议）、`--offline`、`--skip-prepare`（仅调整 Agent 配置）、
`--uninstall [--agents=...|--all-agents]`、`--yes`、`--dry-run`。

### 前置条件

- Node.js >= 20 + npm（包装层与安装器；Windows 开发机 Node 24、WSL/Debian Node 20
  均已验证）。CodeGraph 的首次索引与后端服务运行在平台包捆绑的 Node 24 上，不依赖
  系统 Node 版本（其库需要 Node 22.5+ 的内置 `node:sqlite` 模块）。
- Git；选择 tgrep 后端还需要 Rust 工具链（edition 2024，rustc 1.98 验证过）。
- 首次准备需要联网拉取固定版本制品；之后可用 `--offline` 离线复用。
- 国内网络建议 `--mirror=cn`（npm 用 npmmirror、cargo 用 rsproxy；tgrep 源码仍从
  GitHub 固定提交拉取并校验提交号）。安装器不会修改全局 npm/cargo 配置。

## 支持的 Agent

以下 Agent 的配置由 `setup.js` 自动写入（均为仓库内项目级文件，保留其中已有的其他条目）：

| Agent | 配置文件 | 格式要点 |
| --- | --- | --- |
| Claude Code（与 pi 共用） | `.mcp.json` | `mcpServers` |
| Codex CLI | `.codex/config.toml` | `[mcp_servers.<name>]` + `cwd` |
| VS Code / GitHub Copilot | `.vscode/mcp.json` | `servers` + `${workspaceFolder}` |
| Cursor | `.cursor/mcp.json` | `mcpServers` |
| Gemini CLI | `.gemini/settings.json` | `mcpServers` |
| OpenCode | `opencode.json` | `mcp` / `type:local` / `command` 数组 |
| Kilo Code | `.kilocode/mcp.json` | `mcpServers` |
| Roo Code | `.roo/mcp_settings.json` | `mcpServers` |
| ZCode | `.zcode/config.json` | `mcp.servers` |
| oh-my-pi (omp) | `.omp/mcp.json` | `mcpServers` |

以下工具只有用户级（仓库外）配置文件，不自动写入，参考文末示例手工添加：
Windsurf（`~/.codeium/windsurf/mcp_config.json`，支持变量插值）、
Cline（VS Code 面板内配置或 CLI 的 `~/.cline/data/settings/cline_mcp_settings.json`）。

## 配置条目示例

`<PROJECT_DIR>` 表示本仓库的绝对路径；`<PROJECT_DIR>/build_jobs_cmake_tools` 为默认
`<BUILD_DIR>`。运行 `setup.js` 时会写入解析后的实际路径（见下节的路径策略）。

### 通用 `mcpServers` 家族（Claude Code / pi / Cursor / Gemini CLI / Kilo Code / Roo Code / oh-my-pi / Windsurf / Cline）

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

### OpenCode（`opencode.json`）

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
| Codex CLI | `cwd` 字段 | `cwd` + 相对路径（可提交共享） |
| Windsurf | `command/args/env` 变量插值（具体变量见官方文档） | 不自动写；示例用绝对路径 |
| 其余（Claude Code、Cursor、Gemini、opencode、Kilo、Roo、ZCode、pi、omp、Cline） | 无官方 cwd/相对路径约定（相对路径按客户端自身工作目录解析，不可靠） | 绝对路径（本机解析） |

服务端入口支持 `--repo-root <dir>`、`--build-dir <dir>` 参数覆盖默认推导
（缺省从入口文件位置推导仓库根并校验标记，构建目录缺省 `build_jobs_cmake_tools`）。

## 首次启动与索引

- MCP 握手立即响应；首次索引在后台进行，此时查询返回 `INDEX_NOT_READY`，
  用 `tgrep_status` / `codegraph_status` 查看进度。
- tgrep 索引位于 `<BUILD_DIR>/integration/mcp/state/tgrep/...`；
  CodeGraph 索引位于仓库根 `.codegraph-atsf4g-<platform>/`（已加入 `.gitignore`）。
  已存在的有效 `.codegraph/` 会原地复用。
- 同一工具的第二个实例返回 `INDEX_IN_USE`，不会接管或杀死已有实例。

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
node --test <PROJECT_DIR>/project/integration/mcp/common/test/
node --test <PROJECT_DIR>/project/integration/mcp/tgrep/test/
node --test <PROJECT_DIR>/project/integration/mcp/codegraph/test/
```

或用各目录的 `npm test`。测试使用假后端模拟协议面，不需要真实索引。

升级固定版本：改对应工具目录的 `upstream-lock.json`（版本、提交、哈希），重跑
`setup.js`（或 `--skip-prepare` 只调配置），重新执行测试。tgrep 补丁如不适用需在
`tgrep/patches/` 重新生成并重放回归。维护指引见 Agent Skill
`mcp-integration-maintenance`（仅在维护本集成时加载）。

## 已验证平台

| 平台 | 单元测试 | 真实后端冒烟 |
| --- | --- | --- |
| Windows x64（Node 24） | 43/43 | tgrep 首次索引 ~2s、codegraph 首次索引 ~22s，均通过 |
| Linux x64（WSL/Debian，Node 20 + 捆绑 Node 24） | 43/43 | tgrep 首次索引 ~1s、codegraph 首次索引 ~14s，均通过 |

macOS 及 arm64 平台未验证。
