---
title: atgateway Linux Benchmark
---

# atgateway Linux Benchmark

The benchmark tests two reply modes. **Full echo** sends the entire request back. **One-way flow** replies with the three bytes `got` per request, so almost all application data travels from the client to the gateway.

[Download the latest measurements (CSV)](/benchmarks/atgateway-linux-results.csv)

## Peak results {#results}

| Reply mode | Peak QPS | Peak request throughput (MiB/s) |
| --- | --- | --- |
| Full echo | 764,162 (128 B, 10 clients, zstd kHighRatio) | 1,295.1 (300 KiB, 10 clients, no compression) |
| `got` | 788,004 (128 B, 10 clients, lz4 kHighRatio) | 2,876.6 (300 KiB, 10 clients, no compression) |

QPS counts requests completed per second. Request throughput uses the request size before compression. The CSV also records reply throughput, which uses three bytes per reply in one-way flow. Both throughput columns use MiB/s. In the CSV, `reply=payload` means full echo, `reply=got` means one-way flow, and `algorithm=none` means compression was disabled.

## No-compression baseline {#no-compression}

These tests use AES-256-GCM with compression disabled at both ends. Each cell shows **QPS / request throughput (MiB/s)**.

### Full echo {#baseline-echo}

| Request size | 1 client | 10 clients | 100 clients | 1000 clients |
| --- | ---: | ---: | ---: | ---: |
| 128 B | 700,034 / 85.5 | 763,515 / 93.2 | 724,037 / 88.4 | 630,999 / 77.0 |
| 256 B | 712,492 / 173.9 | 696,542 / 170.1 | 644,061 / 157.2 | 527,436 / 128.8 |
| 1 KiB | 456,528 / 445.8 | 433,620 / 423.5 | 418,471 / 408.7 | 274,945 / 268.5 |
| 8 KiB | 97,989 / 765.5 | 98,360 / 768.4 | 93,154 / 727.8 | 74,626 / 583.0 |
| 32 KiB | 34,867 / 1,089.6 | 38,184 / 1,193.2 | 35,035 / 1,094.8 | 29,748 / 929.6 |
| 300 KiB | 3,679 / 1,077.8 | 4,420 / 1,295.1 | 4,198 / 1,230.0 | 4,208 / 1,232.7 |

### Reply with got {#baseline-got}

| Request size | 1 client | 10 clients | 100 clients | 1000 clients |
| --- | ---: | ---: | ---: | ---: |
| 128 B | 760,136 / 92.8 | 776,462 / 94.8 | 748,306 / 91.3 | 666,085 / 81.3 |
| 256 B | 761,381 / 185.9 | 740,486 / 180.8 | 732,336 / 178.8 | 582,011 / 142.1 |
| 1 KiB | 572,610 / 559.2 | 552,182 / 539.2 | 554,198 / 541.2 | 340,048 / 332.1 |
| 8 KiB | 190,259 / 1,486.4 | 185,894 / 1,452.3 | 180,069 / 1,406.8 | 100,690 / 786.6 |
| 32 KiB | 69,408 / 2,169.0 | 75,849 / 2,370.3 | 61,614 / 1,925.5 | 52,308 / 1,634.6 |
| 300 KiB | 8,536 / 2,500.7 | 9,819 / 2,876.6 | 9,209 / 2,697.9 | 8,665 / 2,538.6 |

## Full echo charts {#full-echo}

Matching charts use the same vertical scale across reply modes. Charts by request size use a logarithmic scale to show the range from small to large requests. One kQPS is 1,000 QPS.

### QPS by request size {#qps-by-size}

![Full echo QPS by request size](./diagrams/atgateway-linux-benchmark/qps-by-size.echarts.json)

### Request throughput by request size {#throughput-by-size}

![Full echo request throughput by request size](./diagrams/atgateway-linux-benchmark/throughput-by-size.echarts.json)

### QPS by client count {#qps-by-clients}

![Full echo QPS by client count](./diagrams/atgateway-linux-benchmark/qps-by-clients.echarts.json)

### Request throughput by client count {#throughput-by-clients}

![Full echo request throughput by client count](./diagrams/atgateway-linux-benchmark/throughput-by-clients.echarts.json)

## One-way flow charts {#ack-echo}

### QPS by request size {#got-qps-by-size}

![QPS by request size with got replies](./diagrams/atgateway-linux-benchmark/got-qps-by-size.echarts.json)

### Request throughput by request size {#got-throughput-by-size}

![Request throughput by request size with got replies](./diagrams/atgateway-linux-benchmark/got-throughput-by-size.echarts.json)

### QPS by client count {#got-qps-by-clients}

![QPS by client count with got replies](./diagrams/atgateway-linux-benchmark/got-qps-by-clients.echarts.json)

### Request throughput by client count {#got-throughput-by-clients}

![Request throughput by client count with got replies](./diagrams/atgateway-linux-benchmark/got-throughput-by-clients.echarts.json)

## Test method {#method}

- The Release build used GCC 14.2.0 with `-O3 -DNDEBUG` in the WSL `build_jobs_linux` tree. The binaries ran on a native Linux test host with Debian 13.7 (LXC, Linux 7.0.14-20-pve) and an AMD Ryzen 9 9950X. Both the gateway and clients preloaded mimalloc 3.0.1. Test time was 2026-11-20 22:30–23:20 (UTC+8).
- The gateway and clients used TCP loopback with AES-256-GCM and ECDH P-256. Requests contained JSON-like data generated from a fixed seed.
- Both reply modes covered 6 request sizes, 4 client counts, and 7 compression settings. The settings were no compression and three levels each for Zstandard and LZ4. The levels were `kDefault`, `kFast`, and `kHighRatio`. The 128 B and 256 B requests and the three-byte `got` reply are below the 1 KiB compression threshold.
- Each configuration ran twice. Charts and tables use the average of the two measurements. All clients finished the handshake before sending requests in batches. The measurement window was 2 seconds, extended to 16 seconds for 300 KiB at 1000 clients.
- Every connection received replies. In full echo, the client checked reply length and sequence for every reply and compared all bytes in the first reply. In one-way flow, the client checked every reply for `got`. After sending stopped, the client drained outstanding replies and matched the totals. The CSV contains 672 valid measurements.
- The tests use the gateway's built-in echo feature. Application services, networking across hosts, and the contents of real requests all affect performance.

Set `echo_server: true` for full echo. Add `echo_server_ack_only: true` for one-way flow.

```yaml
atgateway:
  echo_server: true
  echo_server_ack_only: true
```

## CPU checks {#cpu-check}

The gateway used CPUs 5, 7, and 29. The load generators used CPUs 0, 2, and 12. The two groups used separate physical cores.

One logical CPU has 100% capacity. The three gateway CPUs had at least **190.7%** idle capacity in every run. At 1000 clients, the minimum was 193.8% for full echo and 195.5% for `got`. The highest usage of a single load generator at 1000 clients was 49.0% for full echo and 100.5% for `got`. The measurement windows had zero CPU throttling events.

At least one load generator reached 95% CPU in 151 runs. The numbers show the rates reached in this environment. The gateway can have spare capacity when a load generator is nearly saturated.
