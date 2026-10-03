---
title: 服务端配置
---

# 服务端配置

## 快速上手：修改已有配置

1. 在 `install/cloud-native/values/personal/` 放置个人覆盖配置，按已有 values 的文件布局填写需要修改的键。
   `default` 提供基础值，`dev` 提供开发覆盖，本地脚本最后加载 `personal`。
2. 修改服务实例数和进程布局时，使用 `non_cloud_native/deploy.yaml` 的 `proc_desc`；
   etcd、Redis 等公共配置使用 `modules/` 下的对应文件。
3. 构建以更新 `<BUILD_DIR>/publish/cloud-native/`，再执行
   `publish/tools/script/generate_config.sh` 或 `generate_config.ps1`。
   也可临时修改 publish 中的 values 并直接生成，但下次构建可能覆盖这些修改。
4. 检查生成的 `<server>/cfg/*_<bus_id>.yaml`，确认地址、端口、实例类型及资源路径符合预期。
5. 对支持 reload 的字段执行该实例的 `reload_<bus_id>.sh/.ps1`；进程身份、监听地址等启动设置变更后重启。
   不要假定所有字段都支持热更新。

完整启动命令见[运行与部署](../getting-started/run-deploy)。

## 快速上手：增加配置字段

1. 公共逻辑配置在 `src/server_frame/protocol/private/protocol/config/` 定义；
   服务专用配置沿用该服务的配置 proto（例如
   `src/authsvr/protocol/protocol/config/authsvr_config.proto`）。
2. 在已有 message 中增加字段，沿用相邻字段的 `atapp` 配置注解、默认值、单位和校验范围，再重新构建。
3. 在对应 chart 的 `cfg/*.yaml.tpl` 中透传 values 参数，并给 values 增加默认值。
   仅使用已有配置键时，无需修改模板。
4. 在业务代码中读取配置。已有服务的加载回调会解析其配置类型；只有新增配置段或新服务才需调整加载回调。
5. 生成 YAML 后验证默认值和覆盖值；需要热更新时同时验证业务的 reload 行为。

## 定制与详细设计

配置加载、Excel 与 YAML 的区别见[配置系统](../architecture/configuration)。
定制部署输出时参照 `install/cloud-native/charts/libapp/` 的公共模板；
表达式注解定义见 `atframework/libatapp/include/atframe/atapp_conf.proto`。
