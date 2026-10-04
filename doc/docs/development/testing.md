---
title: 单元测试
---

# 单元测试

## 快速上手

- 框架使用 atframe_utils 的私有测试框架（`CASE_TEST`，非 GTest）；
- 组件与服务测试位于各模块的 `test/`；共享算法另有 `src/component/test/`；
- 构建：`-DPROJECT_ENABLE_UNITTEST=YES` 后用 CTest 运行：

```powershell
cmake --build <BUILD_DIR> --config Debug
ctest --test-dir <BUILD_DIR> -C Debug --output-on-failure
```

Windows 下若测试启动失败（DLL 找不到），检查 PATH 是否包含依赖 DLL 目录（见
`.agents/skills/testing/`）。

## RPC 单元测试（离线 mock）

已实现：`src/tools/rpc-unit-test/` 在**不起真实 Redis/DNS/atbus/网关**的前提下跑真实生成的
RPC/dispatcher/task，用内存 mock 引擎替代已支持的框架依赖路径（SS/DNS/CS/DB/UUID/resource/HPA/telemetry）。完整的
工作原理、使用指南与语义契约见 [RPC 单元测试（离线 mock）](rpc-unit-test)。
