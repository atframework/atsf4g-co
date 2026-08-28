
# 环境操作指南

## 连接端口 8001

## Orbit客户端启动参数需要更改 orbit-agent\cfg\orbit-agent_*.yaml 内的 configured_client_command_line 参数

## Linux/macOS

Linux 和 macOS 环境使用 `tools/script/start_local_test_env.sh` 和 `tools/script/stop_local_test_env.sh`。
首次运行会自动下载 etcd；redis 通过容器运行（自动探测 docker / podman / nerdctl，也可用
`--container-engine` 指定），请确保容器引擎已安装且守护进程已启动（macOS 上如
`podman machine start` 或 Docker Desktop）。

## 启动步骤

1. **启动本地测试环境**

   ```powershell
   tools\script\start_local_test_env.ps1
   ```

   若使用 Windows PowerShell 5.1 且执行策略受限，请用：

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File tools\script\start_local_test_env.ps1
   ```

   Linux/macOS：

   ```bash
   sh tools/script/start_local_test_env.sh
   ```
2. **启动所有服务**

   ```powershell
   start_all.ps1
   ```

## 关闭步骤

1. **停止所有服务**

   ```powershell
   stop_all.ps1
   ```
2. **强制停止所有服务（必要时使用）**

   ```powershell
   kill_all.ps1
   ```
3. **停止本地测试环境**

   ```powershell
   tools\script\stop_local_test_env.ps1
   ```

   Linux/macOS：

   ```bash
   sh tools/script/stop_local_test_env.sh
   ```
