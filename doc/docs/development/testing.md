---
title: 单元测试
---

# 单元测试

## 现有测试

- 框架使用 atframe_utils 的私有测试框架（`CASE_TEST`，非 GTest）；
- 组件测试在 `src/component/test/`（如 `ItemAlgorithmTest` 背包格子算法）；
- 构建：`-DPROJECT_ENABLE_UNITTEST=YES` 后用 CTest 运行：

```powershell
cmake --build <BUILD_DIR> --config Debug
ctest --test-dir <BUILD_DIR> -C Debug --output-on-failure
```

Windows 下若测试启动失败（DLL 找不到），检查 PATH 是否包含依赖 DLL 目录（见
`.agents/skills/testing/`）。

## RPC 单元测试（规划中）

`src/tools/rpc-unit-test/IMPLEMENTATION_PLAN.md` 设计了 RPC 级单元测试支持库，目标是在**不起真实
Redis/DNS/atbus/网关**的前提下跑真实生成的 RPC/dispatcher/task：

- `mock://` atapp connector + 注入 global discovery 节点模拟 SS/router RPC；
- DNS hook：`send_dns_lookup` 发起 `uv_getaddrinfo` 前拦截；
- DB seam：`hash_table` 各操作在"task 校验之后、构造 Redis 命令之前"加钩子，默认内存 backend
  （复现 CAS 版本回写 / KL 单调索引语义）；
- UUID 五类入口统一 provider hook；excel 配置 scoped resource provider；
- 事件泵：`app.run_noblock()` + `time_utility::update()`；
- 新开关 `PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS`（OFF 时零开销），并要求 std-coroutine ON/OFF
  双构建验证；
- CMake 函数 `project_add_rpc_unit_test` 自动注册 CTest。

首个验证目标：`hello.RouterService/router_transfer`。实现落地后本节将补充使用示例。
