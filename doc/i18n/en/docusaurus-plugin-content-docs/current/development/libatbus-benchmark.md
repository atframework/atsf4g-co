---
title: libatbus Benchmark
---

# libatbus Benchmark

:::caution Scope: Communication Library Benchmark Only
This report measures only the underlying communication library `libatbus` (full `atbus::node` v2 protocol stack). **It does not represent end-to-end application RPC performance in `atsf4g-co` services.**

Real business RPCs built on `atsf4g-co` traverse additional layers above `libatbus`:

- **Observability**: OpenTelemetry trace/span creation, context propagation, metrics collection, and structured logging (see [Telemetry](../architecture/telemetry));
- **Richer RPC headers and dispatch**: `SSMsg` / `CSMsg` envelopes, `ss_msg_dispatcher` / `cs_msg_dispatcher` unpacking, and coroutine task scheduling (see [Message Flow and Threading](../architecture/message-flow));
- **Routing and forwarding**: object routing cache lookup, version validation, and on-demand forwarding (`SSRouterHead`, `router_manager`), plus service discovery and multi-hop `atproxy` forwarding (see [Router](../architecture/router) and [Gateway & Proxy](../architecture/gateway-proxy));
- **Business logic and storage**: domain execution, serialization, and Redis or external I/O.

Therefore, **actual business RPC throughput and QPS will be lower than these communication-library baselines**. Use this report to estimate the transport-layer ceiling and tune encryption, compression, shared memory, and memory allocators; **actual service capacity must be verified by simulating and replaying realistic business RPC traffic** (see [robot](../services/robot)).
:::

## Background {#background}

To support dynamically scalable cloud mesh proxies and cross-region public-network transport, [libatbus](https://github.com/atframework/libatbus) was refactored to add encryption and compression, topology-driven routing, and revised node-relationship and multi-connection management (design notes: [libatbus routing and protocol changes](https://owent.net/2026/2604.html) and [libatapp connection management and reconnection](https://owent.net/2026/2605.html)).

Modern game-server RPCs run at higher frequency and carry tracing and richer metadata, so the minimum payload size in this benchmark is set to 200 B without testing smaller sizes separately. The TCP (`io_stream`) channel automatically coalesces messages up to 7 KB (`ATBUS_MACRO_DATA_SMALL_SIZE = 7168`) into batches up to roughly 64 KB. Unencrypted, uncompressed 200 B messages already exceed 3.6M QPS on a single core; smaller payloads yield even higher QPS and do not become a bandwidth bottleneck.

This benchmark runs the complete `atbus::node` v2 stack (framing, Protobuf, handshake, X25519 key exchange, encryption, and compression) across cross-platform baselines, memory allocators, encryption/compression combinations, shared memory, and 2-to-1000-node fan-in topologies.

## Key Results {#summary}

The table below summarizes single-core saturated ceilings (`1 KB = 1024 B`, `1 MB = 1024 KB`, `1 GB = 1024 MB`). Filter all 770 single-pair runs and 480 multi-node runs in the [interactive benchmark matrix](pathname:///benchmarks/libatbus/matrix.html?lang=en), or read `atframework/libatbus/docs/Benchmark.md` and the [full GitHub benchmark report](https://github.com/atframework/libatbus/blob/d8b2887c9e68530ece664c6787f2e05274059328/docs/Benchmark.md).

| Scenario | Channel | Allocator | 200 B (QPS / Throughput) | 1 KB (QPS / Throughput) | 64 KB (QPS / Throughput) | 300 KB (QPS / Throughput) |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| Linux (single pair) | TCP | mimalloc | 3.66M / 697.5 MB/s | 1.65M / 1.58 GB/s | 42.7K / 2.60 GB/s | 9.6K / 2.76 GB/s |
| WSL2 (single pair) | TCP | mimalloc | 3.20M / 610.4 MB/s | 1.32M / 1.26 GB/s | 38.1K / 2.33 GB/s | 9.1K / 2.62 GB/s |
| WSL2 (single pair) | TCP | glibc | 3.51M / 669.3 MB/s | 1.40M / 1.33 GB/s | 19.1K / 1.17 GB/s | 3.8K / 1.09 GB/s |
| Windows (single pair) | TCP | MSVC CRT | 917.8K / 175.1 MB/s | 294.8K / 287.9 MB/s | 6.1K / 382.9 MB/s | 1.5K / 450.0 MB/s |
| Linux (single pair) | `shm://` | mimalloc | 2.68M / 510.6 MB/s | 1.50M / 1.43 GB/s | 50.6K / 3.09 GB/s | 10.4K / 2.96 GB/s |
| Windows (single pair) | `shm://` | MSVC CRT | 1.26M / 240.9 MB/s | 890.4K / 869.5 MB/s | 33.8K / 2.06 GB/s | 8.3K / 2.39 GB/s |
| Linux (1000 nodes) | TCP | mimalloc | 3.70M / 706.6 MB/s | 1.39M / 1.33 GB/s | 73.2K / 2.23 GB/s (32 KB) | 9.4K / 2.70 GB/s |

Recommended transport settings for typical deployment scenarios:

| Deployment scenario | Recommended channel & algorithms | Allocator & notes |
| --- | --- | --- |
| Linux intranet cross-host (1 Gbps / 10 Gbps) | TCP + `AES-128/256-GCM`, no compression | Preload `mimalloc` via `LD_PRELOAD` for high-rate large payloads; compression wastes CPU on high-bandwidth links |
| Cross-region / public WAN (bandwidth-bound) | TCP + `LZ4` or `zstd` with AEAD (`AES-GCM` with AES-NI, else `ChaCha20-Poly1305`) | Stack compresses before encrypting; adding `AES-GCM` after compression costs almost no speed |
| Linux same-host plaintext IPC | `shm://` for `>= 8 KB`; keep TCP for small-packet workloads | `shm://` disables encryption and compression by design; each segment maps 256 MB by default |
| Windows same-host plaintext IPC | Prefer `shm://` | 5.4x–5.6x TCP throughput at `>= 8 KB`; cross-process use requires administrator privileges |
| Disallowed in production | Disable raw `ChaCha20` | Only 3.8% of plaintext throughput on x86 and provides no authentication tag |

Algorithm and cost guidance:

- Modern server CPUs commonly provide hardware acceleration for encryption (such as AES-NI), which keeps overhead low. ChaCha20 acceleration depends on the specific hardware model and should be verified before selection.
- With a hardware-accelerated cipher, throughput loss on large messages is small. Enabling encryption across all environments—including internal corporate networks—is recommended to defend against traffic sniffing and man-in-the-middle attacks.
- Hardware acceleration for compression is uncommon. Choose compression based on cost: enable it for cross-region links or expensive dedicated WAN bandwidth, and leave it disabled on high-bandwidth internal networks.

## Test Environment and Method {#method}

All results come from standalone static builds of `libatbus` (`ffdd409`); dynamic-library builds are roughly 22.5% slower at 200 B. Three environments were measured to compare platform overhead:

| Item | Dedicated Linux Host | WSL2 (Debian 13) | Native Windows |
| --- | --- | --- | --- |
| Runtime | Proxmox LXC container, 24 GB RAM | Local WSL2 VM, 20 GB RAM | Windows 11 26H2, 93.4 GB RAM |
| CPU | Ryzen 9 9950X host, 12 vCPU assigned | Ryzen 7 9700X, capped at 4 cores / 8 threads | Ryzen 7 9700X (8 cores / 16 threads) |
| Compiler | GCC 14.2 `-O3` | GCC 14.2 `-O3` | MSVC (VS 2026) `/O2` |
| Allocator | mimalloc 2.1.9 (`LD_PRELOAD`) | glibc (plus full 110-run mimalloc comparison) | MSVC CRT |

Topology and measurement rules:

- **Single-pair baseline**: 1 sender and 1 receiver over real ctrl + data dual connections. The sender transmits fixed-size payloads at full rate, waits to drain when the send buffer fills (8 MB default), and resumes; the receiver reads at full rate and verifies the embedded 8-byte monotonic sequence number.
- **Multi-node fan-in**: 1 single-threaded receiver handling 1 / 9 / 99 / 999 sender processes (2 / 10 / 100 / 1000 total nodes). At 1000 nodes, the per-sender buffer is reduced to 1 MB to keep total memory within host limits.
- **Payload and handshake**: Fixed-seed JSON-like text payload. Encrypted runs use X25519 + HKDF-SHA256; `libatbus` refreshes keys every 3 hours by default, so no renegotiation occurs during 10–15 s runs.
- **Metrics and aggregation**: QPS counts application messages received per second; throughput counts raw payload bytes delivered to the receiver, excluding protocol headers, cipher overhead, or compression savings. Single-pair runs last 15 s and multi-node runs last 10 s. After dropping the first 2 s warmup and trailing partial samples, QPS and throughput use the duration-weighted median.
- **Run-to-run variance**: Loopback TCP shows bimodal variance across runs. On Linux, most repeat runs stay within 10% (up to 25% at 8 KB); on Windows TCP, roughly 20% of configurations vary by more than 20%. WSL2 and Windows runs share the desktop host with background processes and serve as order-of-magnitude references; the dedicated Linux container provides stable benchmark figures.

## Cross-Platform Baseline and Allocator {#platform-allocator}

### Cross-Platform TCP Baseline {#tcp-baseline}

![Unencrypted, uncompressed TCP QPS and throughput across Linux, WSL2 (mimalloc and glibc), and native Windows](./diagrams/libatbus-benchmark/tcp-platforms.echarts.json)

*Unencrypted, uncompressed IPv4 TCP loopback. Top: QPS on a logarithmic scale; bottom: payload throughput on a linear scale.*

On a single Linux core, 200 B messages reach **3.66M msg/s** (697.5 MB/s), while 300 KB messages drop to 9,645 msg/s with payload throughput rising to **2.76 GB/s**. Small packets saturate CPU on per-message framing and processing; large packets are dominated by memory copies and allocation.

With mimalloc enabled on both, WSL2 closely tracks dedicated Linux: across the 110-run matrix, the median WSL2/Linux ratio is 104%, with 104 runs falling between 85% and 118%, making WSL2 practical for routine development and regression benchmarks. The one anomaly is 8 KB, where WSL2 plateaus around 1.06 GB/s under both glibc and mimalloc—roughly half of dedicated Linux (1.74–2.30 GB/s)—for an unresolved reason. Native Windows TCP is significantly slower at 14%–25% of Linux across all sizes (382.9 MB/s at 64 KB) with both sides at 96%–99% CPU; even with `libatbus` small-packet batching reducing syscall count, Windows socket syscall overhead remains the primary bottleneck.

### Memory Allocator Impact (mimalloc vs glibc) {#allocator-impact}

To isolate hardware and OS differences, the same binary ran the full 110-run matrix on WSL2 under default glibc and `LD_PRELOAD` mimalloc. Four representative workloads are compared below:

![WSL2 mimalloc vs glibc QPS and throughput across plaintext, AES-128-GCM, LZ4, and zstd](./diagrams/libatbus-benchmark/allocator-wsl.echarts.json)

*Solid lines: mimalloc; dashed lines: glibc; matching colors share the same workload.*

| Workload | Payload | glibc (QPS / Throughput) | mimalloc (QPS / Throughput) | Throughput delta | Peak RSS delta (recv / send) |
| --- | ---: | ---: | ---: | ---: | --- |
| Plaintext | 200 B | 3.51M / 669.3 MB/s | 3.20M / 610.4 MB/s | -8.8% | +7.8 / +10.0 MB |
| Plaintext | 64 KB | 19.1K / 1.17 GB/s | 38.1K / 2.33 GB/s | **+99.5%** | +7.5 / +11.3 MB |
| Plaintext | 300 KB | 3.8K / 1.09 GB/s | 9.1K / 2.62 GB/s | **+140.5%** | +6.6 / +5.7 MB |
| `AES-128-GCM` | 64 KB | 16.5K / 1.01 GB/s | 37.2K / 2.27 GB/s | **+124.9%** | +7.1 / +18.8 MB |
| `AES-128-GCM` | 300 KB | 3.6K / 1.02 GB/s | 8.3K / 2.37 GB/s | **+133.0%** | +6.5 / +6.2 MB |
| `LZ4@default` | 64 KB | 20.2K / 1.24 GB/s | 20.6K / 1.26 GB/s | +1.9% | +7.4 / +10.4 MB |
| `zstd@default` | 64 KB | 8.3K / 517.6 MB/s | 8.9K / 555.2 MB/s | +7.3% | +7.2 / +7.6 MB |

mimalloc is faster in 85 of 110 runs, lifting uncompressed large-message paths (plaintext and AES-GCM) by 2.0x–2.4x. The root cause is `pack_message_with()` in `atframework/libatbus/src/atbus_connection_context.cpp`: every packed message allocates temporary buffers (currently reserving `compressed_buffer` equal to the message size even when compression is disabled), magnifying glibc ptmalloc overhead at high large-packet rates. The tradeoff is roughly a 9% drop at 200 B and 6–21 MB higher peak RSS per process. Once compression is enabled, CPU time shifts into the compressor and allocator differences shrink.

## Encryption and Compression Overhead {#crypto-compression}

### Encryption Algorithms and Platform Differences {#crypto-algorithms}

Performance of six encryption settings on uncompressed Linux TCP across five payload sizes:

![QPS and throughput of six encryption configurations over uncompressed Linux TCP](./diagrams/libatbus-benchmark/crypto-tcp.echarts.json)

*Linux (mimalloc), no compression. Raw ChaCha20 drops to 337 QPS at 300 KB, so the QPS log axis starts at 100.*

On AES-NI-capable Linux hosts, `AES-GCM` imposes modest overhead on large payloads: 64 KB drops by only 12%–13% from 2.60 GB/s plaintext to 2.26–2.29 GB/s. At 200 B, fixed per-message cipher and authentication costs dominate, lowering QPS by about 55% from 3.66M to 1.62M–1.64M. Key lengths 128 / 192 / 256 differ by about 1% and can be chosen purely by security policy. `ChaCha20-Poly1305` reaches 1.65 GB/s at 64 KB (63% of plaintext) and suits environments without AES-NI.

Raw `ChaCha20` drops to 102 MB/s on Linux (3.8% of plaintext) and lacks an authentication tag; do not use it in production. Unlike `ChaCha20-Poly1305`, raw `ChaCha20` routes through libsodium, while global initialization initializes OpenSSL without calling `sodium_init()`, leaving runtime SIMD optimizations disabled unless the application initializes libsodium explicitly.

At 64 KB across platforms, Windows TCP reverses the Linux ordering:

| 64 KB TCP, no compression | Linux (mimalloc) | WSL2 (mimalloc) | Windows (MSVC) |
| --- | ---: | ---: | ---: |
| No encryption | 42.7K / 2.60 GB/s (100%) | 38.1K / 2.33 GB/s (100%) | 6.1K / 382.9 MB/s (100%) |
| `AES-128-GCM` | 37.6K / 2.29 GB/s (88%) | 37.2K / 2.27 GB/s (98%) | 2.2K / 138.4 MB/s (36%) |
| `AES-256-GCM` | 37.1K / 2.26 GB/s (87%) | 36.4K / 2.22 GB/s (95%) | 1.9K / 118.0 MB/s (31%) |
| `ChaCha20-Poly1305` | 27.0K / 1.65 GB/s (63%) | 28.5K / 1.74 GB/s (75%) | 2.5K / 158.4 MB/s (41%) |
| `ChaCha20` (raw) | 1.6K / 102.0 MB/s (3.8%) | 1.9K / 118.1 MB/s (5.0%) | 2.9K / 179.4 MB/s (47%) |

On Windows TCP, AES-GCM drops to 17%–40% of plaintext and trails `ChaCha20-Poly1305`. The root cause is not yet identified, so cross-platform deployments should not blindly copy Linux cipher priorities.

### Compression and Combined Encryption {#compression-encryption}

`_allow_compress()` enables compression only when the serialized message body reaches at least 1,024 B and emits a compressed frame only if the result shrinks (200 B and 256 B skip compression because headers and dictionary overhead would expand small frames). Processing always runs **compress first, then encrypt**.

![QPS and throughput on Linux TCP for no compression, LZ4, and zstd alone and combined with encryption](./diagrams/libatbus-benchmark/compression-encryption.echarts.json)

*Linux (mimalloc). `200 B*` is below the 1 KB compression threshold, so compressed lines coincide with uncompressed lines of the same cipher. Color encodes compression; line style encodes encryption.*

Summary at 64 KB on Linux TCP (see the [interactive benchmark matrix](pathname:///benchmarks/libatbus/matrix.html?lang=en) for all sizes):

| Compression | Compressed size | No encryption | + `AES-128-GCM` | + `ChaCha20-Poly1305` |
| --- | ---: | ---: | ---: | ---: |
| No compression | 100% | 42.7K / 2.60 GB/s | 37.6K / 2.29 GB/s | 27.0K / 1.65 GB/s |
| `LZ4@default` | 43.6% | 19.8K / 1.21 GB/s | 19.5K / 1.19 GB/s | 17.8K / 1.09 GB/s |
| `zstd@default` | 23.8% | 8.4K / 527.0 MB/s | 8.4K / 525.0 MB/s | 8.3K / 518.2 MB/s |
| `zstd@high-ratio` | 21.0% | 8.6K / 540.5 MB/s | 8.3K / 518.7 MB/s | 8.3K / 515.7 MB/s |

At 64 KB on Linux, adding `AES-128-GCM` to `LZ4` reduces throughput by only 1.3% (1.21 -> 1.19 GB/s), and adding it to `zstd` causes almost no measurable drop. On Windows large messages, compressing before encrypting is dramatically faster than encrypting alone: at 300 KB, `ChaCha20-Poly1305` alone delivers 136.2 MB/s, whereas `LZ4@default + ChaCha20-Poly1305` climbs to **423.7 MB/s** (near the 450.0 MB/s plaintext rate), and `AES-128-GCM` jumps from 81.4 MB/s to 268.0 MB/s because compression cuts encrypted and copied byte volume by more than half.

For cross-region budgeting, if available link bandwidth is B and compressed ratio is r, the link carries roughly B / r of raw payload per second. With this text corpus: on a 1 Gbps link (~119 MB/s line rate), LZ4 supports ~270 MB/s of raw payload—well below the 1.19 GB/s single-core ceiling of `LZ4 + AES-128-GCM`; zstd supports ~496 MB/s, close to the 525 MB/s single-core ceiling. On a 10 Gbps intranet (~1,192 MB/s), uncompressed `AES-128-GCM` (2.29 GB/s) already saturates the NIC, while enabling compression shifts the bottleneck to CPU. Measure r on real production payloads before deployment, and avoid compressing random binary or already-compressed data.

Implementation note: compression uses predefined policy levels mapped per algorithm (`default` maps to each algorithm's default). On the current wire send path, `pack_message_with()` calls `kBalanced`, so wire throughput across levels of the same algorithm is nearly identical.

## Shared Memory vs TCP {#shm-vs-tcp}

Shared memory (`shm://`) targets same-host IPC and disables handshake negotiation, encryption, and compression by design. Across all 315 shm runs with encryption or compression requested on the three platforms, negotiated settings were always `none/none` and throughput matched plaintext. Plaintext comparison:

![Unencrypted, uncompressed QPS and throughput comparison between TCP and shared memory across Linux, WSL2, and Windows](./diagrams/libatbus-benchmark/shm-platforms.echarts.json)

*Solid lines: TCP; dashed lines: `shm://`; matching colors represent the same platform. Linux uses mimalloc, WSL2 uses default glibc, and Windows uses MSVC CRT.*

| Payload | Linux `shm://` (vs TCP) | WSL2 `shm://` (vs TCP) | Windows `shm://` (vs TCP) |
| --- | ---: | ---: | ---: |
| 200 B | 2.68M / 510.6 MB/s (0.73x) | 2.22M / 423.7 MB/s (0.63x) | 1.26M / 240.9 MB/s (1.38x) |
| 1 KB | 1.50M / 1.43 GB/s (0.91x) | 1.52M / 1.45 GB/s (1.09x) | 890.4K / 869.5 MB/s (3.02x) |
| 8 KB | 381.8K / 2.91 GB/s (1.26x) | 383.7K / 2.93 GB/s (2.77x) | 298.2K / 2.28 GB/s (5.63x) |
| 64 KB | 50.6K / 3.09 GB/s (1.18x) | 33.0K / 2.01 GB/s (1.73x) | 33.8K / 2.06 GB/s (5.51x) |
| 300 KB | 10.4K / 2.96 GB/s (1.07x) | 4.2K / 1.19 GB/s (1.10x) | 8.3K / 2.39 GB/s (5.44x) |

Windows sees the largest gain: bypassing sockets yields 3.0x TCP throughput at 1 KB and 5.4x–5.6x at 8 KB–300 KB (2.06–2.39 GB/s on one core). On Linux, the advantage depends on message size: `shm://` beats TCP by 7%–26% at `>= 8 KB` (**3.09 GB/s** at 64 KB), but trails TCP by 27% at 200 B and 9% at 1 KB for two reasons:

- TCP coalesces `<= 7 KB` small messages to amortize per-message overhead, whereas `shm://` writes each block independently with checksum verification;
- `shm://` supports multi-writer single-reader access using atomic operations, causing CPU cache-line invalidation that hurts small-message throughput.

Shared memory's operational advantages are preserving unread messages across process crash recovery and avoiding per-pair connection buffers under multi-writer sharing, which can reduce total memory when planned carefully. Account for fixed mapping size as well: each segment maps 256 MB by default, raising peak RSS of both attached processes from 11–40 MB to ~267–271 MB (physical pages are shared by the host, not double-counted). Use TCP if same-host traffic must be encrypted.

## Multi-Node Fan-In Scaling (2 to 1000 Nodes) {#multinode-scaling}

To measure high-connection scaling, one single-threaded receiver pinned to a dedicated physical core handled 1 / 9 / 99 / 999 sender processes (2 / 10 / 100 / 1000 total nodes) across 20 algorithm combinations and 6 payload sizes (480 runs total).

Aggregate receiver capacity without encryption or compression as connection count grows 500x:

![Aggregate QPS and throughput on unencrypted, uncompressed Linux TCP for 2, 10, 100, and 1000 nodes](./diagrams/libatbus-benchmark/multinode-baseline.echarts.json)

*Linux (mimalloc), aggregate measurements on the single-threaded receiver.*

The four curves stay closely aligned: 200 B, 256 B, and 300 KB show virtually no degradation from 2 to 1000 nodes (at 1000 nodes, 200 B sustains **3.70M QPS / 706.6 MB/s** and 300 KB sustains **9.4K QPS / 2.70 GB/s**). 1 KB and 32 KB decline by about 17% and 11% (1 KB from 1.67M to 1.39M, 32 KB from 82.0K to 73.2K); the receiver-side hotspot at high connection counts has not yet been profiled.

Aggregate QPS and throughput across encryption/compression combinations at 1000 nodes, overlaid with 2-node `LZ4` and `zstd` baselines:

![Aggregate QPS and throughput for encryption and compression combinations at 1000-node fan-in with 2-node LZ4 and zstd references](./diagrams/libatbus-benchmark/multinode-n1000-combos.echarts.json)

*`200 B*` and `256 B*` fall below the 1 KB compression threshold and are not compressed; tiny messages grow after adding compression headers/dictionaries, and the default threshold reflects typical workloads and can be customized. Dotted lines show 2-node references; all other lines are 1000-node measurements.*

| Configuration (Linux TCP) | Payload | 2 nodes (1S + 1R) | 10 nodes (9S + 1R) | 1000 nodes (999S + 1R) |
| --- | ---: | ---: | ---: | ---: |
| No encryption / no compression | 300 KB | 9.9K / 2.85 GB/s | 9.8K / 2.79 GB/s | 9.4K / 2.70 GB/s |
| `AES-128-GCM` | 300 KB | 8.7K / 2.48 GB/s | 8.3K / 2.37 GB/s | 8.1K / 2.31 GB/s |
| `LZ4@default` | 300 KB | 3.1K / 897.7 MB/s | **10.0K / 2.86 GB/s** | **9.4K / 2.70 GB/s** |
| `LZ4@default + AES-128-GCM` | 300 KB | 3.1K / 896.5 MB/s | **9.3K / 2.67 GB/s** | **8.8K / 2.53 GB/s** |
| `zstd@default` | 1 KB | 183.1K / 178.8 MB/s | **321.1K / 313.6 MB/s** | **312.8K / 305.5 MB/s** |
| `zstd@default` | 300 KB | 1.5K / 443.6 MB/s | **4.6K / 1.32 GB/s** | **4.5K / 1.28 GB/s** |

Compression is much slower than decompression: in a 1-to-1 pair, the sender saturates one core first. Once 10 or more senders compress in parallel, the bottleneck moves to decompression on the single receiver, lifting aggregate throughput sharply and staying flat from 10 to 1000 nodes. At 1000 nodes and 300 KB, plain `LZ4` matches uncompressed throughput (both 2.70 GB/s), and `LZ4 + AES-128-GCM` (2.53 GB/s) surpasses uncompressed `AES-128-GCM` (2.31 GB/s) because the receiver decrypts less than half as much ciphertext while LZ4 decompression is inexpensive.

## Test Boundaries and Production Capacity Assessment {#boundaries}

Account for these boundaries when using the data for capacity planning:

- **Communication library only — excludes business RPC overhead**: These tests drive `atbus::node` directly with raw payloads and exclude layers traversed by real `atsf4g-co` RPCs: OpenTelemetry observability and trace propagation ([Telemetry](../architecture/telemetry)), richer RPC headers and coroutine dispatch ([Message Flow and Threading](../architecture/message-flow)), object routing and service-discovery/proxy forwarding ([Router](../architecture/router), [Gateway & Proxy](../architecture/gateway-proxy)), and business logic plus database I/O. **Real business RPC QPS and throughput will be lower than these raw `libatbus` figures**; actual production capacity must be measured by simulating and replaying realistic business RPC workloads on the target topology (see [robot](../services/robot)).
- **No latency percentiles (P50 / P99)**: All runs saturate the send buffer continuously and do not reflect RPC latency under normal load; measure latency distributions separately at controlled send rates.
- **Loopback links only**: Traffic does not cross physical NICs, switches, or lossy WAN links, so numbers reflect single-core stack and kernel CPU ceilings. Production sizing must also account for NIC and kernel network stack overhead: with hardware offload on Linux NICs, kernel stack CPU cost is minimal; without offload, empirical overhead is roughly 20% of one x86 core per Gbps (or ~50% on ARM, subject to target-hardware measurement), which exceeds `libatbus` itself.
- **One-way direct connections**: Multi-hop `atproxy` forwarding and simultaneous full-duplex saturation were not tested; full-duplex load typically halves single-core directional capacity.
- **Single compressible text corpus**: Random binary or pre-compressed business payloads will produce lower compression ratios and throughput.

Three measured anomalies remain open for follow-up investigation:

1. WSL2 plateaus around 1.06 GB/s at 8 KB under both glibc and mimalloc (~50% of dedicated Linux), likely affected by Windows drivers or virtualization.
2. Windows TCP AES-GCM drops sharply (17%–40% of plaintext) and trails `ChaCha20-Poly1305`.
3. Uncompressed 1 KB and 32 KB multi-node runs drop by 11%–17% as node count grows, likely related to cache hit rate and OS scheduling.

## Raw Data {#benchmark-data}

All eight summary CSV files are downloadable below, and the [interactive benchmark matrix](pathname:///benchmarks/libatbus/matrix.html?lang=en) lets you switch between single-pair and multi-node views and inspect CPU, RSS, and negotiated settings per cell. Full build steps, reproduction scripts, and per-run tables live in `atframework/libatbus/docs/Benchmark.md` and the [GitHub benchmark report](https://github.com/atframework/libatbus/blob/d8b2887c9e68530ece664c6787f2e05274059328/docs/Benchmark.md).

| Dataset | Files |
| --- | --- |
| Linux (mimalloc) | [TCP CSV](pathname:///benchmarks/libatbus/linux-tcp.csv) · [Shared Memory CSV](pathname:///benchmarks/libatbus/linux-shm.csv) · [Multi-Node Fan-In 480-Run CSV](pathname:///benchmarks/libatbus/multinode.csv) |
| WSL2 | [TCP (mimalloc) CSV](pathname:///benchmarks/libatbus/wsl-mimalloc-tcp.csv) · [TCP (glibc) CSV](pathname:///benchmarks/libatbus/wsl-tcp.csv) · [Shared Memory CSV](pathname:///benchmarks/libatbus/wsl-shm.csv) |
| Native Windows | [TCP CSV](pathname:///benchmarks/libatbus/windows-tcp.csv) · [Shared Memory CSV](pathname:///benchmarks/libatbus/windows-shm.csv) |

## Summary {#conclusion}

This full-stack benchmark establishes single-core transport ceilings for `libatbus`: 3.6M–3.7M QPS at 200 B, up to 2.76 GB/s (~22 Gbps) over TCP and 3.09 GB/s (~24 Gbps) over shared memory for large messages; clear large-message gains with mimalloc; minimal added cost for encryption after compression on bandwidth-limited links; and a bottleneck shift from sender compression to receiver decompression under multi-node fan-in where LZ4 with AES-GCM can outperform uncompressed AES-GCM on large payloads.

Planned protocol-stack follow-ups include finer-grained compression level control, skipping `compressed_buffer` preallocation when compression is disabled, tuning default cipher and compression priorities based on these measurements, and further optimizing hot-path allocations and CPU cache hit rate (following [CPU-cache-friendly Aho-Corasick optimization](https://owent.net/2026/2614.html)).

Remember that these figures reflect only the `libatbus` communication library. Actual service capacity in `atsf4g-co`—including observability, full RPC headers, and object routing—must still be validated by replaying realistic business RPC traffic.
