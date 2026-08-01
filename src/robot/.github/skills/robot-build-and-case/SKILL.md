---
name: robot-build-and-case
description: "Use when: building the Robot binary through the CMake robot-build target or the src/robot Taskfile, running a Robot connection case, locating Robot outputs or logs, or starting a built robot executable."
---

# Robot Build And Case

## Purpose

统一说明本仓库里 Robot 的构建方式，以及构建完成后如何直接运行连接测试 Case。

## Build through CMake

先按根 `AGENTS.md` 解析 `<BUILD_DIR>`，再从仓库根目录显式选择非 `ALL` 的目标：

```powershell
cmake --build <BUILD_DIR> --target robot-build -- -j 12
```

主要产物位于 `<BUILD_DIR>/publish/robot/bin/robot.exe`。默认 `all` 不构建 Robot；不要把
`robot-build` 加入默认依赖图。

## Build standalone

仅验证 Robot 自身流程时，从 Robot 源码目录运行 Taskfile：

```powershell
Set-Location src/robot
task all
```

默认产物是 `src/robot/build/install/bin/robot.exe`。需要隔离产物时，显式注入
`ROBOT_BUILD_DIR`、`INSTALL_PATH`、`ENV_BUILD_TARGET_DIR` 和 `ENV_BUILD_TOOLS_DOWNLOAD_DIR`，
并把这些路径放在 `<BUILD_DIR>/_agent_tmp/...` 下。

## Run Connection Test Case

进入相应的 `bin` 目录运行；程序默认从当前目录读取 `config.yaml`，因此先准备实际测试配置：

```powershell
Set-Location <BUILD_DIR>/publish/robot/bin
.\robot.exe
```

## Logs

运行日志可在以下目录查看：

- `<BUILD_DIR>/publish/robot/log/user/`

如果需要查看登录或连接阶段的公共日志，也可以同时检查：

- `<BUILD_DIR>/publish/robot/log/`

## Notes

- `task all` 会完成按需子模块初始化、工具准备、代码生成和 Go 编译。
- 当前构建只发布 Robot 可执行文件，不会复制 `config.yaml` 或 case 目录；不要从旧产物推断它们已生成。
- CMake 集成和独立 Taskfile 使用不同的默认构建/安装目录，报告结果时写明实际路径。
