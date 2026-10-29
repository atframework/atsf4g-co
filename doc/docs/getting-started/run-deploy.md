---
title: 运行与部署
---

# 运行与部署

## 本地快速运行

构建产物在 `<BUILD_DIR>/publish/`。先在
`install/cloud-native/values/personal/` 填写本地覆盖值并构建，或临时修改 publish 中的 values。
至少确认 etcd、Redis 地址、实例布局和 Excel 资源路径。
本地生成脚本依次加载 `default`、`dev`、`personal`，并设置 `global.world_id=1`；
需要其他 world id 时调整调用参数。

### Linux / macOS

```bash
cd <BUILD_DIR>/publish/tools/script
bash start_local_test_env.sh
bash generate_config.sh
cd ../..
bash start_all.sh
```

### Windows

```powershell
Set-Location <BUILD_DIR>/publish/tools/script
pwsh -NoLogo -NoProfile -File start_local_test_env.ps1
pwsh -NoLogo -NoProfile -File generate_config.ps1
Set-Location ../..
pwsh -NoLogo -NoProfile -File start_all.ps1
```

已有外部 etcd/Redis 时不需要启动临时依赖，但 values 必须使用对应地址。
`generate_config` 先用 Helm 更新打包的 chart 依赖，再调用发布的 atdtool，生成每实例 YAML 和启动脚本，以及 publish 根目录的
`start_all`、`stop_all`、`kill_all`。检查服务日志和 etcd 注册后，再验证业务请求。
依赖更新失败时停止生成。修改 libapp 源模板后，必须同步刷新各服务 `charts/libapp-*.tgz`，
否则渲染仍会使用旧脚本。直接调用 atdtool 前，先运行 `tools/script/update_dependency.sh/.ps1`。

## 按实例启动与重载

生成产物按 `<server>/cfg/*_<bus_id>.yaml` 和
`<server>/bin/start_<bus_id>.sh/.ps1` 布局存放。可只启动所需服务实例；
使用数据层的业务服务需配套依赖与组件实例。

热更新使用实例的 `reload_<bus_id>.sh/.ps1`，停止使用 `stop_<bus_id>.sh/.ps1`。
整体停止使用 publish 根目录的 `stop_all.sh/.ps1`；临时依赖由
`tools/script/stop_local_test_env.sh/.ps1` 停止。
修改配置的具体步骤见[服务端配置](../development/server-config)。

## 进程托管

Linux shell 脚本通过 `tools/script/service-manager.sh` 选择托管方式。默认 `auto` 依次检查
可连接的 systemd 用户管理器、PATH 中的 `supervisord` 和 `supervisorctl`，最后使用普通后台进程。
安装了 `systemctl` 但没有用户会话总线时，不会选用 systemd。显式指定的后端不可用时，启动报错。

```bash
bash start_all.sh -m auto
bash start_all.sh -m systemd
bash start_all.sh -m supervisord
bash start_all.sh -m none

# 单实例及依赖服务也读取此环境变量。
export ATF_SERVICE_MANAGER=supervisord
bash tools/script/etcd/setup-etcd.sh start --service-manager supervisord
bash tools/script/redis/redis.sh start --service-manager systemd --container-engine podman
```

运行中的实例保留最初的命令、参数、部署环境变量和后端；改变它们需要先停止再启动。每个实例在
`bin/<PID 文件名>.service/` 记录控制器路径、实例名和启动参数，etcd/Redis 则使用各自工作目录的
`service/`。stop、kill 和 cleanup 使用这份记录，不重新探测后端，也不读取新的后端选择。
控制器不可用时保留状态并报错，恢复控制器后重试停止。迁移部署目录前先停止服务，运行中不要删除状态目录。
若上次 stop 未完成，但业务进程已退出且托管器已停止该实例，下一次 start 会先完成收尾，再按新参数启动。
仍在运行、等待崩溃重启或无法确认控制器状态时，不会覆盖原记录。已卸载且确认停止的 unit 也可正常完成 stop。
没有托管记录的旧部署仍使用原有 PID 停止方式。

可单独重复执行实例的 start/stop 脚本，无需先运行 start_all/stop_all。状态目录中的 `lifecycle.record` 和备份
使用 SHA-256 校验并原子替换，保存启动后端、实例名称和运行/停止意图。字段丢失或损坏、误留 `stopped`、误删
`enabled` 时从有效记录恢复；真实 stop 失败留下的停止意图仍要求先完成 stop，不会被 start 当成损坏忽略。
进程记录同时保存 PID 和启动时间，恢复前校验真实进程；应用启动确认完成后，才允许修复它的 PID 文件。
停止完成记录绑定本次进程，空标记或上一次进程的标记不会跳过业务停止命令。两份生命周期记录都损坏、记录属于
其他目录或无法确认进程身份时，脚本保留现场并报错；恢复记录或控制器后重试，不猜测后端或终止其他进程。

普通进程根据 `tools/script/common/atsf4g@.service` 在实例状态目录生成 unit，与 `run.sh` 放在一起；
`~/.config/systemd/user/` 仅保存指向该 unit 的注册和启用链接，不再创建用户状态目录中的实例索引。
unit 通过 `%Y` 定位自身在发布包中的实际目录。systemd、Quadlet 和 supervisord 的实例标识由完整服务名和
发布根目录绝对路径的 SHA-256 前 16 位组成。后缀只取决于部署目录，不包含启动参数、执行目录、状态目录或包版本。
保留部署入口的逻辑路径，不将软链接解析成版本目录；通过同一个部署入口更新包后名称保持稳定，切换入口前应先停止旧实例。
同一部署目录中的完整服务名必须唯一，不同部署目录可以复用服务名。已有生命周期仍按记录的旧名称停止，下次启动采用新规则。
参数按原始 argv 保存并执行，服务在前台运行，systemd 跟踪实际服务进程。
生成脚本的内部前台模式参数为 `--atapp-foreground`。
启动时还会记录同实例生成的 stop 脚本。正常 stop 先撤销自动启动意图，调用该脚本执行应用停止命令，
并等待业务进程退出，再由托管器完成收尾；`--upgrade` 等停止参数按原始 argv 透传。
停止命令失败时返回失败并保留进程，修复原因后可重试 stop，或显式执行 kill。
kill 跳过业务停止命令并强制终止进程。
systemd 的 `ExecStop` 使用同一回调；supervisord 通过前台适配脚本接收停止请求，
不会将停止信号直接发给业务进程。每次进程运行期间，成功的停止命令仅执行一次，崩溃重启后重新计数。
直接通过 systemctl/supervisorctl 停止时也调用该回调，超时后的终止行为遵循托管器配置。
start 执行 `systemctl --user enable --now`；stop/kill 先移除启动意图并 disable，再通过 systemctl 停止。
`Restart=on-failure` 负责崩溃拉起，`enable/disable` 负责用户管理器启动时的自动启动。
stop/kill 确认停止后会清除该 unit 的 `failed` 记录。旧测试删除配置后若仍显示 `not-found/failed`，
可对已确认停止的具体 unit 执行 `systemctl --user reset-failed <unit>`；这不会删除 journal 日志。
注销后继续运行或开机启动还需要运维按需配置 user lingering；脚本不会修改该系统设置。

supervisord 使用独立的用户级 daemon、Unix socket 和程序配置，默认目录为
`<发布包>/var/service-manager/supervisor`，可在启动前通过 `ATF_SUPERVISOR_DIR` 指定绝对路径
（不包含换行、反斜杠或分号）。该目录必须由当前用户专用。
本地构建的 `tools` 即使链接到源码树，默认配置仍写入发布目录。路径超过 Unix socket 长度限制时，
仅将控制 socket 放入用户运行时目录，配置、日志和 PID 文件继续保存在发布目录。
停止时移除对应程序配置并更新 daemon，
避免它再次启动时重新拉起已停实例。后台运行的服务使用 `autorestart=unexpected`；退出码 0 不触发重启。
普通进程模式没有崩溃重启。日志分别在 systemd journal、supervisord 目录和实例状态目录的 `output.log` 中。

### 已有容器镜像

宿主机托管已有镜像，无需把 supervisord 加入业务镜像。容器内的主进程应在前台运行。
下面的参数在镜像名后通过第二个 `--` 传给容器命令；省略该部分时沿用镜像的 ENTRYPOINT/CMD。

```bash
export ATF_SERVICE_MANAGER=supervisord
bash tools/script/service-manager.sh start "$PWD/runtime/game-1" game-1 "$PWD" \
  --container docker registry.example/game:version -- \
  --publish 127.0.0.1:7001:7001 --volume "$PWD/config:/app/config:ro"
bash tools/script/service-manager.sh stop "$PWD/runtime/game-1"

# 可选：覆盖镜像的 CMD。
bash tools/script/service-manager.sh start "$PWD/runtime/redis-1" redis-1 "$PWD" \
  --container podman docker.io/library/redis:7.2.16 -- \
  --publish 127.0.0.1:16379:6379 -- redis-server --appendonly yes
bash tools/script/service-manager.sh kill "$PWD/runtime/redis-1"
```

容器名、detach、restart、rm、cidfile 和 sdnotify 等生命周期参数由控制器管理，调用方不要覆盖。
Docker/supervisord 使用前台容器客户端，stop/kill 还会通过记录的引擎停止实际容器并确认退出。
绑定目录、命名卷和镜像不会随 stop/kill 删除；容器临时层和匿名卷不用于保存持久数据。
Redis 的显式 `cleanup` 仍遵循其原有镜像清理行为。

Podman + systemd 使用 rootless Quadlet：每实例的 `.container` 和启用 drop-in 都保存在实例状态目录，
`~/.config/containers/systemd/` 只保存文件及 drop-in 目录的链接，
由 Podman generator 生成 unit；配置目录遵循 `XDG_CONFIG_HOME`。常用的 publish、volume、env、network
参数转换成 Quadlet 字段，其他引擎选项通过 `PodmanArgs` 传递，需要与 Quadlet 的生命周期设置兼容。
Quadlet 要求支持该功能的 Podman 和 cgroup v2。
WSL 等没有激活系统 `network-online.target` 的环境，若镜像已缓存且服务只需本地网络，启动前设置
`ATF_QUADLET_WAIT_NETWORK=0` 可关闭 Quadlet 的网络就绪等待；默认保留该等待。
生成的 unit 不能直接 `systemctl enable/disable`，因此 start 安装带 `WantedBy=default.target` 的
`[Install]` drop-in，stop/kill 移除它并 daemon-reload；进程启停仍通过 `systemctl --user`。
停止成功后移除安装的 Quadlet 链接，实例状态保留供下次启动使用。

实现依据：[systemd service](https://www.freedesktop.org/software/systemd/man/latest/systemd.service.html)、
[Supervisor 配置](https://supervisord.org/configuration.html)、
[Podman Quadlet](https://docs.podman.io/en/latest/markdown/podman-systemd.unit.5.html)。

旧版 atsf4g 镜像的 `/entrypoint.sh start` 会后台启动业务进程、前台运行配置 watcher。
使用这些镜像时，将 `tools/script/container-entrypoint.sh` 只读挂载为 `/atf-entrypoint.sh`，
设置 `--entrypoint /bin/bash`，容器命令使用 `-- /atf-entrypoint.sh start`，并保留原来的环境变量和配置挂载。
该兼容入口复用镜像内的配置初始化、reload 和 stop，通过 `health_check` 发现业务进程退出并让容器失败退出，
使外部托管器能够重启容器。启动等待默认 180 秒，可通过容器环境变量 `ATF_CONTAINER_START_TIMEOUT` 调整。
此方式不需要重打镜像；仅监控前台容器客户端无法发现旧入口掩盖的业务进程退出。

## 部署形态

| 形态 | 输入与输出 |
| --- | --- |
| Kubernetes | `install/cloud-native/charts/` 与 values，生成工作负载、网络、存储和日志配置 |
| Docker | `install/cloud-native/images/server/` 的 Dockerfile 与 entrypoint |
| 裸机/本地 | `non_cloud_native/deploy.yaml` 的进程布局与 sh/ps1 实例脚本 |

日常调整只改 values。新增实例填写 `proc_desc` 的 chart 名、实例数、起始实例号和启动分组。
服务侧 HPA 配置随实例生成；Kubernetes HPA 和指标适配器需要另行配置，见 [HPA 接入](../whitepaper/hpa-controller)。

## 定制与详细设计

需要自行调用渲染工具时，构建后的 atdtool 位于
`<BUILD_DIR>/publish/tools/atdtool/`；Windows 使用 `atdtool.exe`。
传入逗号分隔的 values 目录以沿用本地生成脚本的调用方式：

```bash
atdtool template install/cloud-native/charts -o <OUTPUT_DIR> \
  --values install/cloud-native/values/default,install/cloud-native/values/dev \
  --set global.world_id=1
```

实例配置的生成与部署模式由 chart 控制；原始模板和共享片段位于
`install/cloud-native/charts/libapp/`。
可观测性接入见[遥测](../architecture/telemetry)。
