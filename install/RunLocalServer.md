
# 环境操作指南

## 连接端口 8001

## Orbit客户端启动参数需要更改 orbit-agent\cfg\orbit-agent_*.yaml 内的 configured_client_command_line 参数

## 启动步骤

1. **启动本地测试环境**

   ```powershell
   tools\script\start_local_test_env.ps1
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
