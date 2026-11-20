---
title: atgateway Linux 性能测试
---

# atgateway Linux 性能测试

本次测试了两种回复方式。**原文回显**返回整条请求。**单向流**每收到一条请求就回复 3 字节的 `got`，用于测试大部分数据都从客户端发往网关的场景。

[下载本轮测试数据（CSV）](/benchmarks/atgateway-linux-results.csv)

## 最高结果 {#results}

| 回复方式 | 最高 QPS | 最高请求吞吐（MiB/s） |
| --- | --- | --- |
| 原文回显 | 764,162（128 B，10 个客户端，zstd kHighRatio） | 1,295.1（300 KiB，10 个客户端，无压缩） |
| `got` | 788,004（128 B，10 个客户端，lz4 kHighRatio） | 2,876.6（300 KiB，10 个客户端，无压缩） |

QPS 是网关每秒完成的请求数。请求吞吐按压缩前的请求字节数计算。CSV 也记录回包吞吐，单向流按每条回包 3 字节计算。两项吞吐的单位都是 MiB/s。CSV 中 `reply=payload` 表示原文回显，`reply=got` 表示单向流，`algorithm=none` 表示关闭压缩。

## 无压缩基线 {#no-compression}

以下测试使用 AES-256-GCM 加密，两端均关闭压缩。每个单元格显示 **QPS / 请求吞吐（MiB/s）**。

### 原文回显 {#baseline-echo}

| 请求大小 | 1 个客户端 | 10 个客户端 | 100 个客户端 | 1000 个客户端 |
| --- | ---: | ---: | ---: | ---: |
| 128 B | 700,034 / 85.5 | 763,515 / 93.2 | 724,037 / 88.4 | 630,999 / 77.0 |
| 256 B | 712,492 / 173.9 | 696,542 / 170.1 | 644,061 / 157.2 | 527,436 / 128.8 |
| 1 KiB | 456,528 / 445.8 | 433,620 / 423.5 | 418,471 / 408.7 | 274,945 / 268.5 |
| 8 KiB | 97,989 / 765.5 | 98,360 / 768.4 | 93,154 / 727.8 | 74,626 / 583.0 |
| 32 KiB | 34,867 / 1,089.6 | 38,184 / 1,193.2 | 35,035 / 1,094.8 | 29,748 / 929.6 |
| 300 KiB | 3,679 / 1,077.8 | 4,420 / 1,295.1 | 4,198 / 1,230.0 | 4,208 / 1,232.7 |

### 只回复 got {#baseline-got}

| 请求大小 | 1 个客户端 | 10 个客户端 | 100 个客户端 | 1000 个客户端 |
| --- | ---: | ---: | ---: | ---: |
| 128 B | 760,136 / 92.8 | 776,462 / 94.8 | 748,306 / 91.3 | 666,085 / 81.3 |
| 256 B | 761,381 / 185.9 | 740,486 / 180.8 | 732,336 / 178.8 | 582,011 / 142.1 |
| 1 KiB | 572,610 / 559.2 | 552,182 / 539.2 | 554,198 / 541.2 | 340,048 / 332.1 |
| 8 KiB | 190,259 / 1,486.4 | 185,894 / 1,452.3 | 180,069 / 1,406.8 | 100,690 / 786.6 |
| 32 KiB | 69,408 / 2,169.0 | 75,849 / 2,370.3 | 61,614 / 1,925.5 | 52,308 / 1,634.6 |
| 300 KiB | 8,536 / 2,500.7 | 9,819 / 2,876.6 | 9,209 / 2,697.9 | 8,665 / 2,538.6 |

## 原文回显曲线 {#full-echo}

不同回复方式的对应图表使用相同的纵轴范围。按包大小绘制的图使用对数刻度，便于比较大小包之间的差距。1 kQPS 等于 1,000 QPS。

### QPS 随请求大小变化 {#qps-by-size}

![原文回显时 QPS 随请求大小变化](./diagrams/atgateway-linux-benchmark/qps-by-size.echarts.json)

### 请求吞吐随请求大小变化 {#throughput-by-size}

![原文回显时请求吞吐随请求大小变化](./diagrams/atgateway-linux-benchmark/throughput-by-size.echarts.json)

### QPS 随客户端数变化 {#qps-by-clients}

![原文回显时 QPS 随客户端数变化](./diagrams/atgateway-linux-benchmark/qps-by-clients.echarts.json)

### 请求吞吐随客户端数变化 {#throughput-by-clients}

![原文回显时请求吞吐随客户端数变化](./diagrams/atgateway-linux-benchmark/throughput-by-clients.echarts.json)

## 单向流曲线 {#ack-echo}

### QPS 随请求大小变化 {#got-qps-by-size}

![只回复 got 时 QPS 随请求大小变化](./diagrams/atgateway-linux-benchmark/got-qps-by-size.echarts.json)

### 请求吞吐随请求大小变化 {#got-throughput-by-size}

![只回复 got 时请求吞吐随请求大小变化](./diagrams/atgateway-linux-benchmark/got-throughput-by-size.echarts.json)

### QPS 随客户端数变化 {#got-qps-by-clients}

![只回复 got 时 QPS 随客户端数变化](./diagrams/atgateway-linux-benchmark/got-qps-by-clients.echarts.json)

### 请求吞吐随客户端数变化 {#got-throughput-by-clients}

![只回复 got 时请求吞吐随客户端数变化](./diagrams/atgateway-linux-benchmark/got-throughput-by-clients.echarts.json)

## 测试方法 {#method}

- 在 WSL 的 `build_jobs_linux` 中用 GCC 14.2.0 构建 Release 版（`-O3 -DNDEBUG`），传到原生 Linux 测试机运行。测试机为 Debian 13.7（LXC，Linux 7.0.14-20-pve），CPU 为 AMD Ryzen 9 9950X。网关和客户端都预加载 mimalloc 3.0.1。测试时间为 2026-11-20 22:30–23:20 (UTC+8)。
- 网关和客户端使用同机 TCP 回环，固定使用 AES-256-GCM 和 ECDH P-256。请求使用固定种子生成的 JSON 样式数据。
- 两种回复方式都测 6 种请求大小、4 种客户端数，以及无压缩、Zstandard 三档和 LZ4 三档。压缩档位为 `kDefault`、`kFast`、`kHighRatio`。128 B、256 B 请求和 3 字节的 `got` 回包都低于 1 KiB 的压缩门槛。
- 每组测两次。图表和表格使用两次测量的平均值。全部连接握手后才发包，客户端在同一轮中批量发送请求。每次测 2 秒。300 KiB、1000 个客户端的组合测 16 秒。
- 每条连接都收到回包。原文回显核对回包长度和请求序号，并完整比对第一条回包。单向流逐条检查回复是否为 `got`。停发后等回包收完，核对发送数和回复数。CSV 共有 672 次有效测量。
- 测试直接使用网关的回显功能。业务服务、跨机网络和真实请求的数据内容都会影响实际性能。

原文回显只需设置 `echo_server: true`。单向流还需设置 `echo_server_ack_only: true`。

```yaml
atgateway:
  echo_server: true
  echo_server_ack_only: true
```

## CPU 检查 {#cpu-check}

网关绑定 CPU 5、7、29。发包进程绑定 CPU 0、2、12。两组使用不同的物理核。

100% 表示一个逻辑 CPU 的容量。整个测试中，网关的三个 CPU 合计至少剩余 **190.7%** 空闲。
1000 个客户端时，原文回显和 `got` 模式分别至少剩余 193.8% 和 195.5%。两个模式中，单个发包进程观察到的最高 CPU 用量分别为 49.0% 和 100.5%。测量窗口内 CPU 限流次数为 0。

有 151 次测量中至少一个发包进程使用了 95% 或以上的 CPU。这些数据表示本次环境实际达到的速率。发包端接近满载时，网关仍可能有余量。
