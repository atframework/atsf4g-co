---
name: dsa-dsc-dsm-pseudocode
description: 'Use when drafting, reviewing, or extending DSA, DSC, DSM pseudocode in this repo. Triggers: 伪代码, pseudocode, DS lifecycle, scheduler, session routing, protocol, SDK, atframework component, x-server style, design-to-implementation. Read the design guide, implementation plan, pseudocode roadmap, and atframework/x-server baseline before editing.'
user-invocable: true
---

# DSA/DSC/DSM Pseudocode Workflow

## When to Use

- 需要把设计拆成可落地的伪代码切片。
- 需要新增或修改 `doc\orbit\pseudocode` 下的文档。
- 需要检查伪代码是否仍与设计文档一致。
- 需要决定一个需求应该落在哪个伪代码目录。
- 需要把流程补成函数级、包含类与函数结构的完整伪代码。
- 需要为伪代码同步补测试用例伪代码。
- 需要让 atorbit 的实现方案对齐 atframework submodule 的使用方式。
- 需要让文档、伪代码和正式代码风格严格对齐 x-server 仓库的 atframework 组件风格。

## Required Context

1. 阅读 [设计文档](../../../doc/guide.md)。
2. 阅读 [实现计划](../../../doc/implementation-plan.md)。
3. 阅读 [伪代码路线](../../../doc/pseudocode-roadmap.md)。
4. 如需更细的执行步骤，继续阅读 [workflow](./references/workflow.md)。
5. 如需定位文件归属，继续阅读 [document-map](./references/document-map.md)。
6. 如需确认函数级写法，继续阅读 [pseudocode-style](./references/pseudocode-style.md)。
7. 如需确认 atframework submodule 复用顺序和 x-server 风格基线，继续阅读 [atframework-baseline](./references/atframework-baseline.md)。

## Workflow

1. 先确定当前任务对应的设计章节与责任边界。
2. 选择一个最小实现切片，只覆盖一个核心流程或一个核心状态机。
3. 先判断当前切片主要复用 `cmake-toolset`、`atframe_utils`、`libatbus`、`libatapp` 中的哪一层能力。
4. 先确定模块的目录层级和文件拆分，伪代码实现文件统一使用 `.pseudo.h` / `.pseudo.cpp`，不能只写索引式说明文。
5. 将内容写入最接近责任归属的 `doc\orbit\pseudocode` 子目录，按 `<module>.pseudo.h`、`<module>.pseudo.cpp`、`<module>_test.pseudo.cpp` 组织。
6. 使用函数级伪代码展开主流程和辅助流程，整体写法向 C++ 靠拢：头文件声明结构，实现文件展开逻辑，函数体内部可用中文步骤简化细节。
7. 对于协议消息，直接引用 Protobuf 定义，不在伪代码中重复发明消息结构。
8. 同步补充测试用例伪代码，覆盖成功、失败、超时、断线和回放场景，并尽量映射到 atframework 私有测试框架风格。
9. 如遇设计缺口，只记录待确认问题，不自行扩展未定义协议。
10. 伪代码稳定后，才进入真实代码实现。

## Output Expectations

- 伪代码应可直接映射到后续模块、接口和测试切片。
- 伪代码默认采用 C++ 风格的头文件 / 实现文件结构，保留类与函数层次，函数体可以用中文步骤注释简化实现。
- 伪代码产物必须体现真实模块拆分和目录层级，文件后缀统一为 `.pseudo.h`、`.pseudo.cpp` 和 `_test.pseudo.cpp`。
- 每个关键流程都应下沉到函数级，并带测试伪代码。
- 伪代码与后续真实代码都应能解释其与 atframework submodule 的依赖边界。
- 风格、目录布局、日志、测试与构建流程都应严格对齐 x-server 仓库中的 atframework 组件习惯。
- 改动应同时维护相关索引文档，避免设计、计划和目录说明脱节。
- DSM 若无设计依据，仅维护占位和问题清单。