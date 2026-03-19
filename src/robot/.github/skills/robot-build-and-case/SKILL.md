---
name: robot-build-and-case
description: "Use when: building the Robot binary, running the connection test case, checking where robot logs are written, asking to run task all from the repository root, or asking how to start the built robot from build/install/bin."
---

# Robot Build And Case

## Purpose

统一说明本仓库里 Robot 的构建方式，以及构建完成后如何直接运行连接测试 Case。

## Build Robot

在仓库根目录执行：

```powershell
task all
```

构建完成后，主要产物位于：

- `build/install/bin/robot.exe`
- `build/install/bin/config.yaml`
- `build/install/case/`

## Run Connection Test Case

构建完成后，进入输出目录直接运行：

```powershell
Set-Location build/install/bin
.\robot.exe
```

## Logs

运行日志可在以下目录查看：

- `build/install/log/user/`

如果需要查看登录或连接阶段的公共日志，也可以同时检查：

- `build/install/log/`

## Notes

- `task all` 会自动完成子模块更新、工具准备、代码生成、编译和测试配置复制。
- 连接测试所需的配置会被复制到 `build/install/bin/` 和 `build/install/case/`，因此运行时应优先使用构建产物目录。
- 在 Windows 环境下直接执行 `.\robot.exe` 即可启动 Robot。