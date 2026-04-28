# Pseudocode Style Reference

## 目标

- 统一 atorbit 工程的伪代码书写方式。
- 保证伪代码能直接映射为真实实现和测试。

## 强制要求

- 伪代码实现文件统一使用 `.pseudo.h` / `.pseudo.cpp`，但不能只是说明性文档；每个模块必须同时体现声明、实现和测试结构。
- 伪代码目录必须模拟未来正式工程的文件层级、模块边界和依赖关系。
- 伪代码采用接近 C++ 的类、函数和控制流写法，优先按头文件 / 实现文件语法组织。
- 每个核心流程必须展开到函数级。
- 每个函数内部必须写出关键语句级逻辑，不能只写“执行某逻辑”“调用某流程”这种空泛描述。
- 对外协议统一引用 Protobuf，不在伪代码中再次定义消息字段。
- 每个关键伪代码模块都必须附带 `_test.pseudo.cpp` 测试伪代码。

## 文件级组织规范

- 每个伪代码模块默认使用一组 `<module>.pseudo.h` 与 `<module>.pseudo.cpp` 描述主体实现；需要测试时补一个 `<module>_test.pseudo.cpp`。
- 若模块较大，可以拆成多个 `.pseudo.*` 文件，但每个文件都必须围绕单一职责，并在注释中标明所属模块与未来真实落点。
- 目录层级必须体现模块关系，例如控制层、会话层、调度层、协议适配层、测试层。
- `README.md` 只能作为索引和说明，不能替代真正的实现伪代码文件。
- 协议定义使用真实 `.proto` 文件；伪代码文件只描述如何围绕这些消息实现逻辑。

## 推荐目录形态

```text
pseudocode/
    dsc/
        scheduler/
                        scheduler_service.pseudo.h
                        scheduler_service.pseudo.cpp
                        scheduler_service_test.pseudo.cpp
        session/
                        session_router.pseudo.h
                        session_router.pseudo.cpp
    dsa/
        agent/
                        agent_service.pseudo.h
                        agent_service.pseudo.cpp
                        agent_service_test.pseudo.cpp
    sdk/
        external/
                        external_client.pseudo.h
                        external_client.pseudo.cpp
```

## 推荐写法

```text
#pragma once

class controller_service {
public:
    result_code_t handle_agent_register(const register_request_t& request);

private:
    agent_table_t agent_table_;
    bool validate_register_request(const register_request_t& request) const;
};

result_code_t controller_service::handle_agent_register(const register_request_t& request) {
    if (!validate_register_request(request)) {
        // 请求非法时直接返回参数错误
        return result_code_t::invalid_argument;
    }

    auto record = build_agent_record(request);
    agent_table_[request.dsa_id()] = record;
    // 更新指标并返回成功
    return result_code_t::ok;
}
```

## 测试推荐写法

```text
# controller_service_test.pseudo.cpp

CASE_TEST(controller_service, handle_agent_register_inserts_agent_record) {
    controller_service controller;
    auto request = make_valid_register_request();

    auto result = controller.handle_agent_register(request);

    CASE_EXPECT_EQ(result_code_t::ok, result);
    CASE_EXPECT_TRUE(controller.has_agent(request.dsa_id()));
}
```

## 禁止写法

- 只写一个 `xxx.md` 或说明文来概括整个模块实现，却没有头文件 / 实现文件结构。
- 仅列步骤，不写函数。
- 只写类名，不写函数体和状态变化。
- 只写接口标题，不按模块层级拆分文档。
- 只写主流程，不写辅助函数和失败路径。
- 用自然语言替代资源回滚、计时器、ACK、日志和指标更新。