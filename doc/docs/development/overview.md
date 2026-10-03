---
title: 开发快速上手
---

# 开发快速上手

已完成[构建](../getting-started/build)并[运行服务](../getting-started/run-deploy)后，按下表完成日常开发。
每篇指南先给出最小步骤，再列出需要定制时的阅读入口。

使用已有功能时，修改 proto、Excel、XML 或 YAML 等声明配置，由工程现有生成流程处理编译和生成代码。
新增业务仍需实现业务逻辑；新增服务或组件时，复制已有工程并填写少量构建声明即可，不需要研究
CMake 脚本实现或编写 Mako 模板。

## 按任务选择

| 我要做什么 | 最小操作 | 上手章节 |
| --- | --- | --- |
| 增加或修改 RPC | 编辑消息和 rpc 声明，重新配置/构建，填充生成的 action | [RPC 与 task action](add-rpc-task) |
| 增加服务 | 复制标准服务，修改名称/协议声明，登记目录和部署配置 | [新增服务](add-service) |
| 接入或增加组件 | 填写 SDK 依赖声明；新组件复制现有目录结构 | [组件接入与新增](add-component) |
| 修改 Excel 数值 | 修改已有表格，构建 `resource-config`，发布资源并 reload | [Excel 配置](excel-config) |
| 增加 Excel 表/字段 | 填写配置 proto、loader 索引和 XML 转换项，再构建 | [Excel 配置](excel-config) |
| 修改服务端配置 | 修改 values，生成实例 YAML，再 reload 或重启 | [服务端配置](server-config) |
| 增加协议消息/文件 | 选择 public/private 目录，定义 message，重新配置/构建 | [新增协议](add-protocol) |
| 增加数据库表 | 在表 proto 中声明字段和存储选项，构建后使用生成 API | [数据库表](add-db-table) |

## 统一的生成与构建步骤

复用已经配置好的构建目录。下面的 `<BUILD_DIR>` 替换为你的实际目录；本工作区的 VS Code
配置使用 `build_jobs_cmake_tools`。

```bash
cmake -S . -B <BUILD_DIR>
cmake --build <BUILD_DIR> --config Debug --parallel 4
```

首次配置按[构建指南](../getting-started/build)选择工具链。后续重新配置保留原构建目录中的
generator、工具链和缓存选项；多配置构建按实际配置替换 `Debug`。
新增 proto、Excel 或业务源文件后要重新配置，以刷新配置阶段收集的文件清单。

## 哪些文件可以修改

- proto、Excel、XML、values YAML 是声明输入；业务 `task_action_*.h/.cpp` 是手写实现。
- 默认生成规则仅在业务骨架不存在时创建它，已有骨架不覆盖。修改 RPC 请求/响应类型后，需同步已有业务文件。
- `*.atfw.gen.*`、`<BUILD_DIR>/_generated/` 下的自动生成文件不应手改；修改输入后重新生成。

## 何时阅读详细设计

只有需要改变 RPC 分发、传输协议、存储语义、生成文件布局、Excel 的自定义派生数据或部署模板行为时，
再阅读[架构设计](../architecture/overview)。普通配置项和已有 RPC 的扩展，从各篇快速上手完成即可。
