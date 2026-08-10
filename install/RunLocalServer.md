
# 环境操作指南

## 启动步骤

1. **启动本地测试环境**

   ```bat
   tools\script\start_local_test_env.bat
   ```
2. **启动 OpenTelemetry Collector**

   ```bat
   otelcol\bin\start.bat
   ```
3. **启动所有服务**

   ```bat
   start_all.bat
   ```

## 关闭步骤

1. **停止所有服务**

   ```bat
   stop_all.bat
   ```
2. **关闭 otelcol 终端**

   * 手动关闭运行 `otelcol` 的命令行窗口。
3. **停止本地测试环境**

   ```bat
   tools\script\stop_local_test_env.bat
   ```
