---
title: libatbus 性能测试
---

# libatbus 性能测试

:::caution 适用范围：仅衡量底层通信库性能
本页数据仅覆盖底层通信库 `libatbus`（`atbus::node` v2 完整协议栈），**不代表 `atsf4g-co` 业务服务的端到端 RPC 性能**。

实际业务 RPC 在 `libatbus` 之上还会经过以下链路与开销：

- **可观测性**：OpenTelemetry trace/span 创建、上下文传播、指标采集与结构化日志（见[可观测性](../architecture/telemetry)）；
- **更复杂的 RPC 协议头与分发**：`SSMsg` / `CSMsg` 协议封装、`ss_msg_dispatcher` / `cs_msg_dispatcher` 解包与协程任务调度（见[消息流与线程模型](../architecture/message-flow)）；
- **路由与转发系统**：对象路由表查询、版本校验与按需转发（`SSRouterHead`、`router_manager`），以及服务发现与多跳 `atproxy` 转发（见[路由系统](../architecture/router)、[网关与代理](../architecture/gateway-proxy)）；
- **业务逻辑与存储访问**：业务处理、序列化及 Redis 等外部 I/O。

因此，**实际业务 RPC 的吞吐量与 QPS 会低于本页给出的通信库基线**。本页数据用于评估通信层上限、加密/压缩算法开销、共享内存收益和内存分配器选型；**实际业务承载能力必须通过模拟回放真实业务 RPC 流量进行压测确认**（压测客户端见[压测机器人](../services/robot)）。
:::

## 背景 {#background}

为适应云上可动态伸缩的网状代理架构及跨大区公网传输需求，[libatbus](https://github.com/atframework/libatbus) 在重构后补齐了加密与压缩能力，引入了拓扑驱动路由，并重构了节点关系与多路连接管理（相关设计记录见 [libatbus 路由与协议变更](https://owent.net/2026/2604.html) 和 [libatapp 连接管理与自动重连](https://owent.net/2026/2605.html)）。

当前游戏服务端 RPC 频率更高，单条消息也会携带链路跟踪与更丰富的元数据，因此本轮测试的最小 payload 定在 200 B，未单独测试更小包长。TCP（`io_stream`）通道对不超过 7 KB（`ATBUS_MACRO_DATA_SMALL_SIZE = 7168`）的消息会自动合包，单次合并上限约 64 KB；200 B 无加密无压缩单核已达到 360 万 QPS 以上，更小包长的 QPS 只会更高，不会成为带宽瓶颈。

本轮压测直接运行完整的 `atbus::node` v2 协议栈（帧封装、Protobuf、握手、X25519 密钥交换、加密和压缩），覆盖跨平台基线、内存分配器对照、加密与压缩组合、共享内存通道，以及 2 至 1000 节点的多连接汇聚场景。

## 结论速览 {#summary}

下表列出单核满载下的典型实测上限（`1 KB = 1024 B`，`1 MB = 1024 KB`，`1 GB = 1024 MB`）。完整的 770 组单对数据与 480 组多节点数据可在[交互式性能对比表](pathname:///benchmarks/libatbus/matrix.html)中按条件筛选，也可查阅 `atframework/libatbus/docs/Benchmark.md` 或仓库内的[完整压测报告](https://github.com/atframework/libatbus/blob/d8b2887c9e68530ece664c6787f2e05274059328/docs/Benchmark.md)。

| 场景 | 通道 | 分配器 | 200 B（QPS / 吞吐） | 1 KB（QPS / 吞吐） | 64 KB（QPS / 吞吐） | 300 KB（QPS / 吞吐） |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| Linux（单对） | TCP | mimalloc | 3.66M / 697.5 MB/s | 1.65M / 1.58 GB/s | 42.7K / 2.60 GB/s | 9.6K / 2.76 GB/s |
| WSL2（单对） | TCP | mimalloc | 3.20M / 610.4 MB/s | 1.32M / 1.26 GB/s | 38.1K / 2.33 GB/s | 9.1K / 2.62 GB/s |
| WSL2（单对） | TCP | glibc | 3.51M / 669.3 MB/s | 1.40M / 1.33 GB/s | 19.1K / 1.17 GB/s | 3.8K / 1.09 GB/s |
| Windows（单对） | TCP | MSVC CRT | 917.8K / 175.1 MB/s | 294.8K / 287.9 MB/s | 6.1K / 382.9 MB/s | 1.5K / 450.0 MB/s |
| Linux（单对） | `shm://` | mimalloc | 2.68M / 510.6 MB/s | 1.50M / 1.43 GB/s | 50.6K / 3.09 GB/s | 10.4K / 2.96 GB/s |
| Windows（单对） | `shm://` | MSVC CRT | 1.26M / 240.9 MB/s | 890.4K / 869.5 MB/s | 33.8K / 2.06 GB/s | 8.3K / 2.39 GB/s |
| Linux（1000 节点） | TCP | mimalloc | 3.70M / 706.6 MB/s | 1.39M / 1.33 GB/s | 73.2K / 2.23 GB/s（32 KB） | 9.4K / 2.70 GB/s |

日常部署可以参考下表选择通信层配置：

| 使用场景 | 建议通道与算法 | 分配器与注意事项 |
| --- | --- | --- |
| Linux 内网跨机（千兆 / 万兆） | TCP + `AES-128/256-GCM`，关闭压缩 | 大包高频场景推荐 `LD_PRELOAD` 加载 mimalloc；高带宽下压缩会白耗 CPU |
| 跨大区 / 公网（带宽受限） | TCP + `LZ4` 或 `zstd` 搭配 AEAD（有 AES-NI 选 `AES-GCM`，否则选 `ChaCha20-Poly1305`） | 协议栈先压缩再加密，压缩后再叠加 `AES-GCM` 几乎不掉速度 |
| Linux 同机明文通信 | `>= 8 KB` 用 `shm://`，小包为主继续用 TCP | `shm://` 按设计不加密也不压缩；每段默认占 256 MB 共享内存 |
| Windows 同机明文通信 | 优先 `shm://` | 8 KB 以上吞吐是 TCP 的 5.4～5.6 倍，跨进程需管理员权限 |
| 生产环境禁用项 | 禁用裸 `ChaCha20` | x86 上吞吐只有明文的 3.8%，且没有认证标签 |

算法选型与成本建议：

- 现代服务器硬件普遍支持加密算法的硬件加速（如 AES-NI），性能会快很多；是否支持 ChaCha20 系列硬件加速取决于具体设备型号，选型前需核对目标硬件。
- 合理选择带硬件加速的加密算法后，大包性能损耗很小，建议全场景默认开启加密（包含公司内网），以防被其他业务嗅探或遭遇中间人攻击。
- 压缩算法普遍缺少硬件加速，需按成本权衡：跨大区网络或专线带宽成本较高时，可开启压缩换取带宽成本下降；内网高带宽环境默认关闭压缩。

## 测试环境与方法 {#method}

本轮压测基于 `libatbus`（`ffdd409`）独立静态库构建；动态库构建在 200 B 下会低约 22.5%，本页全部数据均取自静态构建。测试覆盖 Linux、WSL2 和 Windows 三套环境，便于对比各平台损耗：

| 项目 | Linux 独立测试机 | WSL2（Debian 13） | Windows 原生 |
| --- | --- | --- | --- |
| 运行环境 | Proxmox LXC 容器，24 GB 内存 | 本机 WSL2 虚拟机，20 GB 内存 | Windows 11 26H2，93.4 GB 内存 |
| CPU | Ryzen 9 9950X 宿主，分配 12 vCPU | Ryzen 7 9700X，限制 4 核 8 线程 | Ryzen 7 9700X（8 核 16 线程） |
| 编译器 | GCC 14.2 `-O3` | GCC 14.2 `-O3` | MSVC（VS 2026）`/O2` |
| 内存分配器 | mimalloc 2.1.9（`LD_PRELOAD`） | glibc（另跑完整 110 组 mimalloc 对照） | MSVC CRT |

测试形态与统计规则：

- **单对基线**：1 发 1 收，走真实的 ctrl + data 双连接；发送端按固定包长全速发包，写满发送缓冲区（默认 8 MB）后等待排空再继续；接收端全速收包并校验消息头的 8 字节递增序号。
- **多节点汇聚**：1 个单线程接收节点对接 1 / 9 / 99 / 999 个发送进程（总节点数 2 / 10 / 100 / 1000），1000 节点下发送缓冲调为 1 MB 以控制总内存。
- **负载与握手**：测试负载采用固定随机种子生成的类 JSON 文本；加密握手走 X25519 + HKDF-SHA256，`libatbus` 默认每 3 小时刷新密钥，单组 10～15 秒运行期间不触发重协商。
- **指标与聚合**：QPS 指接收端每秒收到的应用消息条数，吞吐量按接收端交付的原始 payload 字节数计算（不含协议头、加密膨胀或压缩缩减）。单对每组跑 15 秒、多节点跑 10 秒，去掉前 2 秒建连预热和收尾残缺样本后，按样本实际时长加权取中位数。
- **运行波动**：TCP 通道在多次运行间存在高低两档波动。Linux 同机复测多数配置偏差在 10% 以内，8 KB 档可达 25%；Windows TCP 约两成配置偏差超过 20%。此外，WSL2 和 Windows 宿主测试期间受后台进程负载波动影响，结果以量级参考为主；Linux 测试机为独立压测专用容器，数据更稳定。

## 跨平台基线与内存分配器 {#platform-allocator}

### 跨平台 TCP 基线 {#tcp-baseline}

![Linux、WSL2（mimalloc 与 glibc）和 Windows 原生在五档包长下的无加密无压缩 TCP QPS 与吞吐量](./diagrams/libatbus-benchmark/tcp-platforms.echarts.json)

*无加密无压缩，IPv4 TCP 回环。上图 QPS 为对数刻度，下图吞吐量为线性刻度。*

Linux 单核在 200 B 下收到 **366 万条/s**（697.5 MB/s），到 300 KB 时降为 9,645 条/s，吞吐升至 **2.76 GB/s**。小包吃满 CPU 的是逐条协议处理，大包拼的是内存拷贝与分配效率。

两侧都换用 mimalloc 后，WSL2 与独立 Linux 的走势贴近：110 组完整矩阵的 WSL2 / Linux 比值中位数为 104%，104 组落在 85%～118% 之间，WSL2 适合日常开发和回归压测。唯一异常是 8 KB 档，WSL2 无论使用 glibc 还是 mimalloc 都停在约 1.06 GB/s，约为独立 Linux（1.74～2.30 GB/s）的一半，原因尚未定位。Windows 原生 TCP 明显偏慢，各包长下仅为 Linux 的 14%～25%（64 KB 仅 382.9 MB/s），收发两端 CPU 均在 96%～99%；即便 `libatbus` 已启用小包合批降低系统调用次数，Windows socket 的系统调用开销仍是主要瓶颈。

### 内存分配器影响（mimalloc vs glibc） {#allocator-impact}

为剥离硬件和系统差异，在 WSL2 上用同一份二进制分别跑了默认 glibc 和 `LD_PRELOAD` 注入 mimalloc 的完整 110 组矩阵，下面抽取四类典型负载对比：

![WSL2 同环境下 mimalloc 与 glibc 在明文、AES-128-GCM、LZ4 和 zstd 下的 QPS 与吞吐量](./diagrams/libatbus-benchmark/allocator-wsl.echarts.json)

*实线为 mimalloc，虚线为 glibc，同色代表同一负载。*

| 负载组合 | 包长 | glibc（QPS / 吞吐） | mimalloc（QPS / 吞吐） | 吞吐变化 | 峰值 RSS 变化（收 / 发） |
| --- | ---: | ---: | ---: | ---: | --- |
| 无加密无压缩 | 200 B | 3.51M / 669.3 MB/s | 3.20M / 610.4 MB/s | -8.8% | +7.8 / +10.0 MB |
| 无加密无压缩 | 64 KB | 19.1K / 1.17 GB/s | 38.1K / 2.33 GB/s | **+99.5%** | +7.5 / +11.3 MB |
| 无加密无压缩 | 300 KB | 3.8K / 1.09 GB/s | 9.1K / 2.62 GB/s | **+140.5%** | +6.6 / +5.7 MB |
| `AES-128-GCM` | 64 KB | 16.5K / 1.01 GB/s | 37.2K / 2.27 GB/s | **+124.9%** | +7.1 / +18.8 MB |
| `AES-128-GCM` | 300 KB | 3.6K / 1.02 GB/s | 8.3K / 2.37 GB/s | **+133.0%** | +6.5 / +6.2 MB |
| `LZ4@default` | 64 KB | 20.2K / 1.24 GB/s | 20.6K / 1.26 GB/s | +1.9% | +7.4 / +10.4 MB |
| `zstd@default` | 64 KB | 8.3K / 517.6 MB/s | 8.9K / 555.2 MB/s | +7.3% | +7.2 / +7.6 MB |

110 组中 mimalloc 在 85 组更快，未压缩的大包路径（明文和 AES-GCM）吞吐升至原来的 2.0～2.4 倍。根因在 `atframework/libatbus/src/atbus_connection_context.cpp` 的 `pack_message_with()`：每条消息打包都会申请临时缓冲区（即使关闭压缩，当前也会为 `compressed_buffer` 预留与消息体等大的空间），大包高频申请释放放大了默认 glibc ptmalloc 的开销。换用 mimalloc 的代价是 200 B 小包下降约 9%，两端峰值 RSS 各增加约 6～21 MB。开启压缩后，CPU 主要消耗在压缩算法上，分配器不再是主要瓶颈。

## 加密与压缩开销 {#crypto-compression}

### 加密算法与平台差异 {#crypto-algorithms}

Linux TCP 无压缩时，6 种加密配置在 5 档包长上的表现如下：

![Linux TCP 无压缩下六种加密配置的 QPS 与吞吐量](./diagrams/libatbus-benchmark/crypto-tcp.echarts.json)

*Linux（mimalloc），无压缩。裸 ChaCha20 在 300 KB 时仅 337 QPS，因此 QPS 对数轴下限取 100。*

在支持 AES-NI 的 Linux 上，`AES-GCM` 的大包损耗较小：64 KB 档明文 2.60 GB/s，开启 AES-GCM 后仍有 2.26～2.29 GB/s（仅降 12%～13%）；200 B 小包因每条消息固定加解密和认证头占比高，QPS 从 366 万降至 162～164 万（约降 55%）。128 / 192 / 256 三种密钥长度差距约 1%，按安全规范选择即可。`ChaCha20-Poly1305` 在 64 KB 下达到 1.65 GB/s（明文的 63%），适合无 AES-NI 环境。

裸 `ChaCha20` 在 Linux 上降至 102 MB/s（仅明文的 3.8%），既慢又缺少认证标签，生产环境不应选用。实现中它与 `ChaCha20-Poly1305` 不走同一后端：裸 `ChaCha20` 路由到 libsodium，而全局初始化只做了 OpenSSL 初始化，未调用 `sodium_init()`，业务侧不手动初始化时运行期 SIMD 优化未开启。

跨平台对比 64 KB 档，Windows TCP 的加密算法排序与 Linux 相反：

| 64 KB TCP，无压缩 | Linux（mimalloc） | WSL2（mimalloc） | Windows（MSVC） |
| --- | ---: | ---: | ---: |
| 无加密 | 42.7K / 2.60 GB/s（100%） | 38.1K / 2.33 GB/s（100%） | 6.1K / 382.9 MB/s（100%） |
| `AES-128-GCM` | 37.6K / 2.29 GB/s（88%） | 37.2K / 2.27 GB/s（98%） | 2.2K / 138.4 MB/s（36%） |
| `AES-256-GCM` | 37.1K / 2.26 GB/s（87%） | 36.4K / 2.22 GB/s（95%） | 1.9K / 118.0 MB/s（31%） |
| `ChaCha20-Poly1305` | 27.0K / 1.65 GB/s（63%） | 28.5K / 1.74 GB/s（75%） | 2.5K / 158.4 MB/s（41%） |
| `ChaCha20`（裸流） | 1.6K / 102.0 MB/s（3.8%） | 1.9K / 118.1 MB/s（5.0%） | 2.9K / 179.4 MB/s（47%） |

Windows TCP 开启 AES-GCM 后降至明文的 17%～40%，慢于 `ChaCha20-Poly1305`，具体原因尚未查明；跨平台部署不能直接套用 Linux 的加密选型顺序。

### 压缩及叠加加密 {#compression-encryption}

协议栈在 `_allow_compress()` 中限定数据消息序列化后的 body 达到 1,024 B 才尝试压缩，且压缩后确实变小才输出压缩帧（200 B 和 256 B 直接跳过，避免加上压缩字典和头部后体积反增）。处理顺序固定为**先压缩、再加密**。

![Linux TCP 下无压缩、LZ4 与 zstd 单独及叠加加密后的 QPS 与吞吐量](./diagrams/libatbus-benchmark/compression-encryption.echarts.json)

*Linux（mimalloc）。`200 B*` 低于 1 KB 压缩阈值，各压缩曲线与同加密算法的无压缩曲线重合。颜色区分压缩算法，线型区分加密算法。*

固定 Linux 64 KB 档的压缩叠加加密对比如下（完整各档数据见[交互式性能对比表](pathname:///benchmarks/libatbus/matrix.html)）：

| 压缩配置 | 压缩后体积 | 无加密 | + `AES-128-GCM` | + `ChaCha20-Poly1305` |
| --- | ---: | ---: | ---: | ---: |
| 无压缩 | 100% | 42.7K / 2.60 GB/s | 37.6K / 2.29 GB/s | 27.0K / 1.65 GB/s |
| `LZ4@default` | 43.6% | 19.8K / 1.21 GB/s | 19.5K / 1.19 GB/s | 17.8K / 1.09 GB/s |
| `zstd@default` | 23.8% | 8.4K / 527.0 MB/s | 8.4K / 525.0 MB/s | 8.3K / 518.2 MB/s |
| `zstd@high-ratio` | 21.0% | 8.6K / 540.5 MB/s | 8.3K / 518.7 MB/s | 8.3K / 515.7 MB/s |

Linux 64 KB 上，`LZ4` 再叠加 `AES-128-GCM` 只降 1.3%（1.21 -> 1.19 GB/s），`zstd` 叠加 `AES-128-GCM` 几乎无差异。在 Windows 大包上甚至会出现“先压缩再加密比单独加密更快”的现象：300 KB 档 `ChaCha20-Poly1305` 单独加密为 136.2 MB/s，叠加 `LZ4@default` 后升至 **423.7 MB/s**（接近明文 450.0 MB/s），`AES-128-GCM` 也从 81.4 MB/s 升至 268.0 MB/s；原因在于压缩减少了一半以上需要加密和系统拷贝的数据量。

跨区部署可按带宽与单核能力粗估：设链路可用带宽为 B、压缩后体积比例为 r，链路每秒大约可承载 B / r 的原始负载。按本测试文本估算：1 Gbps 专线（线速约 119 MB/s）开 LZ4 后链路可承载约 270 MB/s，远低于 `LZ4 + AES-128-GCM` 单核 1.19 GB/s 的能力；开 zstd 后链路约 496 MB/s，接近单核 525 MB/s。换成 10 Gbps 内网（线速约 1,192 MB/s），不开压缩的 `AES-128-GCM`（2.29 GB/s）即可打满网卡，默认开压缩反而会让 CPU 先饱和。上线前需用真实业务报文实测压缩比 r，随机二进制或已压缩数据不应开压缩。

当前实现中，为统一配置方式，压缩模块预置了若干压缩级别策略，映射到不同算法时的具体压缩率设置并不相同（`default` 对应各算法的默认级别）；同时 `pack_message_with()` 当前线上打包固定调用 `kBalanced`，因此同算法不同级别档位的线上吞吐基本一致。

## 共享内存与 TCP 对比 {#shm-vs-tcp}

共享内存（`shm://`）只用于本机进程间高性能通信，按设计不走握手协商，也不开启加密与压缩。三平台共 315 组带加密或压缩参数的 shm 测试，协商结果全为 `none/none`，速率与明文一致。三平台明文对比如下：

![Linux、WSL2 和 Windows 原生在 TCP 与共享内存通道下的 QPS 与吞吐量对比](./diagrams/libatbus-benchmark/shm-platforms.echarts.json)

*实线为 TCP，虚线为 `shm://`，同色代表同一平台。Linux 使用 mimalloc，WSL2 为默认 glibc，Windows 为 MSVC CRT。*

| 包长 | Linux `shm://`（vs TCP） | WSL2 `shm://`（vs TCP） | Windows `shm://`（vs TCP） |
| --- | ---: | ---: | ---: |
| 200 B | 2.68M / 510.6 MB/s（0.73×） | 2.22M / 423.7 MB/s（0.63×） | 1.26M / 240.9 MB/s（1.38×） |
| 1 KB | 1.50M / 1.43 GB/s（0.91×） | 1.52M / 1.45 GB/s（1.09×） | 890.4K / 869.5 MB/s（3.02×） |
| 8 KB | 381.8K / 2.91 GB/s（1.26×） | 383.7K / 2.93 GB/s（2.77×） | 298.2K / 2.28 GB/s（5.63×） |
| 64 KB | 50.6K / 3.09 GB/s（1.18×） | 33.0K / 2.01 GB/s（1.73×） | 33.8K / 2.06 GB/s（5.51×） |
| 300 KB | 10.4K / 2.96 GB/s（1.07×） | 4.2K / 1.19 GB/s（1.10×） | 8.3K / 2.39 GB/s（5.44×） |

Windows 上共享内存收益最大：绕开 socket 后，1 KB 达到 TCP 的 3 倍，8 KB～300 KB 达到 5.4～5.6 倍，单核稳定输出 2.06～2.39 GB/s。Linux 上则按包长分化：8 KB 以上 `shm://` 比 TCP 快 7%～26%（64 KB 达到 **3.09 GB/s**），但 200 B 和 1 KB 反而慢了 27% 和 9%。原因有两点：

- TCP 会把 `<= 7 KB` 的小包合并发送摊薄开销，而 `shm://` 逐条写入独立消息块并计算校验；
- 共享内存通道支持多写单读，所有操作都使用原子操作，容易引起 CPU Cache Line 失效，从而拉低小包吞吐。

共享内存的优势在于进程 crash 恢复时未读消息不丢，且多写单读模型不需要为每对连接维护独立缓冲区，规划得当时反而更省总内存。选型时也需计入固定映射开销：每个共享内存段默认映射 256 MB，挂接后收发两进程峰值 RSS 都会从 11～40 MB 升至约 267～271 MB（物理页两进程共享，宿主机不会双倍计费）。若本机通信要求加密，则需改用 TCP。

## 多节点汇聚扩展（2 至 1000 节点） {#multinode-scaling}

为评估高连接数下的性能衰减，汇聚测试使用 1 个绑定独立物理核的单线程接收节点，对接 1 / 9 / 99 / 999 个发送进程（总节点数 2 / 10 / 100 / 1000），覆盖 20 种组合 × 6 档包长共 480 组。

无加密无压缩下，连接数放大 500 倍后单接收核的聚合能力如下：

![Linux TCP 无加密无压缩在 2、10、100 和 1000 节点下的聚合 QPS 与聚合吞吐量](./diagrams/libatbus-benchmark/multinode-baseline.echarts.json)

*Linux（mimalloc），单线程接收节点聚合读数。*

四条曲线基本重合：200 B、256 B 和 300 KB 从 2 节点扩至 1000 节点几乎无衰减（1000 节点下 200 B 仍有 **370 万 QPS / 706.6 MB/s**，300 KB 仍有 **9.4K QPS / 2.70 GB/s**）。1 KB 和 32 KB 随连接数增加下降约 17% 和 11%（1 KB 从 1.67M 降至 1.39M，32 KB 从 82.0K 降至 73.2K），具体热点尚未采样定位。

1000 节点下各加密与压缩组合的聚合表现，以及 2 节点 `LZ4` 与 `zstd` 对照如下：

![1000 节点汇聚下各加密压缩组合的聚合 QPS 与吞吐量，以及 2 节点 LZ4 与 zstd 对照](./diagrams/libatbus-benchmark/multinode-n1000-combos.echarts.json)

*`200 B*` 和 `256 B*` 低于 1 KB 压缩阈值，未执行压缩；过小的包因字典开销压缩后反而变大，默认阈值取自多类业务场景的经验值，可按实际业务自定义。两条点线为 2 节点对照，其余为 1000 节点实测。*

| 配置（Linux TCP） | 包长 | 2 节点（1 发 1 收） | 10 节点（9 发 1 收） | 1000 节点（999 发 1 收） |
| --- | ---: | ---: | ---: | ---: |
| 无加密无压缩 | 300 KB | 9.9K / 2.85 GB/s | 9.8K / 2.79 GB/s | 9.4K / 2.70 GB/s |
| `AES-128-GCM` | 300 KB | 8.7K / 2.48 GB/s | 8.3K / 2.37 GB/s | 8.1K / 2.31 GB/s |
| `LZ4@default` | 300 KB | 3.1K / 897.7 MB/s | **10.0K / 2.86 GB/s** | **9.4K / 2.70 GB/s** |
| `LZ4@default + AES-128-GCM` | 300 KB | 3.1K / 896.5 MB/s | **9.3K / 2.67 GB/s** | **8.8K / 2.53 GB/s** |
| `zstd@default` | 1 KB | 183.1K / 178.8 MB/s | **321.1K / 313.6 MB/s** | **312.8K / 305.5 MB/s** |
| `zstd@default` | 300 KB | 1.5K / 443.6 MB/s | **4.6K / 1.32 GB/s** | **4.5K / 1.28 GB/s** |

压缩远慢于解压：1 发 1 收时发送端先占满单核；发送端增至 10 个以上并行压缩后，瓶颈转移到接收端解压，总吞吐立即抬升并在 10 至 1000 节点间保持平稳。在 1000 节点 300 KB 档，纯 `LZ4` 追平无压缩（均为 2.70 GB/s）；`LZ4 + AES-128-GCM`（2.53 GB/s）甚至高于不压缩的 `AES-128-GCM`（2.31 GB/s），因为接收端需解密的密文体积减少一半以上，而 LZ4 解压代价很低。

## 测试边界与实际业务评估 {#boundaries}

基于本报告做容量规划时，需注意以下边界条件：

- **仅衡量基础通信库，不含业务 RPC 链路开销**：本测试直接调用 `atbus::node` 收发原始测试负载，不包含 `atsf4g-co` 实际业务 RPC 必经的 OpenTelemetry 可观测性采集与 trace 上下文传播（[可观测性](../architecture/telemetry)）、更复杂的 RPC 协议头与协程分发（[消息流与线程模型](../architecture/message-flow)）、对象路由与服务发现转发（[路由系统](../architecture/router)、[网关与代理](../architecture/gateway-proxy)），以及业务逻辑与 DB 访问。**实际业务 RPC 的单核 QPS 与吞吐会明显低于基础通信库**；线上承载能力必须通过模拟回放真实业务 RPC 流量进行端到端压测（见[压测机器人](../services/robot)）。
- **未测延迟分位（P50 / P99）**：本轮全部为打满发送缓冲的饱和吞吐压测，不能代表常规负载下的 RPC 延迟；延迟分布需按固定发包速率单独测量。
- **全部为本机回环链路**：未经过真实网卡、交换机与公网丢包，结果反映协议栈与内核在单核上的 CPU 上限。实际评估还需计入网卡与内核网络协议栈开销：若 Linux 网卡带硬件卸载加速，协议栈开销几乎不落到 CPU 上；若无硬件加速，按经验每 1 Gbps 约占 x86 单核 20%、ARM 约占 50%（以目标硬件实测为准，`libatbus` 自身开销低于系统协议栈处理）。
- **单向直连**：未测多跳 `atproxy` 转发及双向同时满载；双向同时满载时单核承载通常减半。
- **单一可压缩文本负载**：若业务报文为随机二进制或已压缩数据，压缩率与压缩吞吐都会低于本测试结果。

以下三个实测现象尚未定位根因，留待后续排查：

1. WSL2 在 8 KB 档无论使用 glibc 还是 mimalloc 均停在约 1.06 GB/s，约为独立 Linux 的一半（推测与 Windows 驱动或虚拟化层有关）。
2. Windows TCP 下 AES-GCM 降幅偏大（仅为明文的 17%～40%），慢于 `ChaCha20-Poly1305`。
3. 多节点汇聚时，1 KB 和 32 KB 的非压缩组合随连接数增加下降 11%～17%（推测与缓存命中率及系统调度策略有关）。

## 原始数据 {#benchmark-data}

全部 8 份汇总 CSV 可直接下载，也可打开[交互式性能对比表](pathname:///benchmarks/libatbus/matrix.html)切换单对与多节点视图并点击单元格查看 CPU、RSS 与协商明细；完整构建、复现脚本和逐项表格见 `atframework/libatbus/docs/Benchmark.md` 及 [GitHub 压测报告](https://github.com/atframework/libatbus/blob/d8b2887c9e68530ece664c6787f2e05274059328/docs/Benchmark.md)。

| 数据集 | 文件 |
| --- | --- |
| Linux（mimalloc） | [TCP CSV](pathname:///benchmarks/libatbus/linux-tcp.csv) · [共享内存 CSV](pathname:///benchmarks/libatbus/linux-shm.csv) · [多节点汇聚 480 组 CSV](pathname:///benchmarks/libatbus/multinode.csv) |
| WSL2 | [TCP（mimalloc）CSV](pathname:///benchmarks/libatbus/wsl-mimalloc-tcp.csv) · [TCP（glibc）CSV](pathname:///benchmarks/libatbus/wsl-tcp.csv) · [共享内存 CSV](pathname:///benchmarks/libatbus/wsl-shm.csv) |
| Windows 原生 | [TCP CSV](pathname:///benchmarks/libatbus/windows-tcp.csv) · [共享内存 CSV](pathname:///benchmarks/libatbus/windows-shm.csv) |

## 总结 {#conclusion}

本轮完整协议栈压测测出了 `libatbus` 的单核通信上限：200 B 下接收端稳定在 360～370 万 QPS，大包 TCP 达到 2.76 GB/s（约 22 Gbps）、共享内存达到 3.09 GB/s（约 24 Gbps）；大包路径换用 mimalloc 提升明显；内网高带宽默认关闭压缩，跨区带宽紧张时先压缩再加密几乎不额外拖慢加密；多发送端汇聚到中心节点时，压缩瓶颈会从发送端转移到接收端解压，LZ4 在大包加密场景下甚至可反超不压缩。

结合压测结果，后续协议栈的优化方向包括：提供更精细的压缩级别控制、关闭压缩时避免为 `compressed_buffer` 预分配临时缓冲区、按实测结果调整默认加密与压缩算法优先级，以及继续优化收发热路径上的内存分配与 CPU 缓存命中率（参考 [面向 CPU 缓存命中率的 AC 自动机优化](https://owent.net/2026/2614.html)）。

再次强调：上述数字只代表底层通信库 `libatbus` 的极限吞吐。实际接入 `atsf4g-co` 业务服务后，叠加可观测性、完整 RPC 协议头与对象路由系统的真实承载能力，仍需基于真实业务 RPC 回放压测得出。
