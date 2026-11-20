// Copyright 2026 atframework
//
// Multi-connection atgateway benchmark client.
//
// Opens N client sessions to an atgateway (v2 protocol), negotiates ECDH +
// crypto + compression, floods fixed-size echo requests once the handshake
// completes and verifies the echoed sequence numbers. Writes a JSON summary
// with sent/recv message counts and payload bytes so a runner can derive the
// server-side (atgateway) QPS and throughput.
//
// usage:
//   atgateway_benchmark_client --host 127.0.0.1 --port 9005 --clients 1 \
//       --size 1024 --duration 15 --compression zstd --level kDefault \
//       --key-exchange p-256 --crypto aes-256-gcm --access-token <token> \
//       --json out.json

#include <uv.h>

#include <common/string_oprs.h>

#include <atgateway/protocol/v2/libatgw_protocol_sdk.h>

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr size_t kSeqPrefixSize = sizeof(uint64_t);

struct bench_config {
  std::string host = "127.0.0.1";
  int port = 9005;
  int clients = 1;
  size_t payload_size = 1024;
  double duration_sec = 15.0;
  std::string compression = "none";
  std::string level = "kDefault";
  std::string key_exchange = "p-256";
  std::string crypto = "aes-256-gcm";
  std::string access_token;
  std::string json_output;
};

struct connection_t {
  uv_tcp_t tcp_sock{};
  uv_connect_t connect_req{};
  bool connect_pending = false;
  std::unique_ptr<::atframework::gateway::libatgw_protocol_sdk> proto;
  bool handshake_done = false;
  bool closed = false;
  bool write_pending = false;
  bool payload_ready = false;
  uint64_t send_seq = 0;
  uint64_t recv_seq = 0;
  uint64_t sent_msgs = 0;
  uint64_t sent_bytes = 0;
  uint64_t recv_msgs = 0;
  uint64_t recv_bytes = 0;
  uint64_t errors = 0;
  std::vector<unsigned char> payload;
};

struct bench_config g_conf;
struct run_stats_t {
  bool stopped = false;
  uint64_t sent_msgs = 0;
  uint64_t sent_bytes = 0;
  uint64_t recv_msgs = 0;
  uint64_t recv_bytes = 0;
  uint64_t errors = 0;
  uint64_t handshake_done_count = 0;
  uint64_t reconnects = 0;
  std::chrono::steady_clock::time_point start_time;
  std::chrono::steady_clock::time_point stop_time;
} g_stats;

uv_loop_t *g_loop = nullptr;
uv_timer_t g_tick_timer;
uv_timer_t g_stop_timer;
uv_prepare_t g_send_pump;
std::vector<std::unique_ptr<connection_t>> g_connections;
std::unordered_map<::atframework::gateway::libatgw_protocol_api *, connection_t *> g_proto_to_conn;
::atframework::gateway::libatgw_protocol_sdk::proto_callbacks_t g_proto_callbacks;
std::shared_ptr<::atframework::gateway::v2::crypto_shared_context_t> g_shared_context;

void build_payload(connection_t &conn) {
  conn.payload.resize(g_conf.payload_size);
  // Reserve the first 8 bytes for the per-connection sequence counter.
  // Deterministic JSON-like records: compressible, mirrors typical game traffic.
  static const char *kKeys[] = {"player", "guild", "level", "score", "item", "quest", "zone", "match"};
  static const char *kFillers[] = {"aabbccdd", "00112233", "deadbeef", "01234567", "89abcdef"};

  size_t pos = kSeqPrefixSize;
  uint32_t rng = 0x12345678u;
  char *text = reinterpret_cast<char *>(conn.payload.data());
  while (pos + 1 < g_conf.payload_size) {
    rng = rng * 1103515245u + 12345u;
    const char *key = kKeys[(rng >> 16) & 7];
    rng = rng * 1103515245u + 12345u;
    uint32_t num = (rng >> 8) & 0xFFFF;
    rng = rng * 1103515245u + 12345u;
    const char *filler = kFillers[(rng >> 16) % (sizeof(kFillers) / sizeof(kFillers[0]))];
    int written = snprintf(text + pos, g_conf.payload_size - pos, "{\"%s\":%u,\"tag\":\"%s%06u\"}", key, num, filler,
                           num & 0xFFFFFF);
    if (written <= 0) {
      break;
    }
    pos += static_cast<size_t>(written);
  }
  for (; pos < g_conf.payload_size; ++pos) {
    text[pos] = static_cast<char>('0' + (pos & 7));
  }
  conn.payload_ready = true;
}

void try_send(connection_t &conn) {
  if (g_stats.stopped || conn.write_pending || !conn.handshake_done || conn.closed || !conn.proto) {
    return;
  }
  if (!conn.payload_ready) {
    build_payload(conn);
  }

  ++conn.send_seq;
  memcpy(conn.payload.data(), &conn.send_seq, sizeof(uint64_t));
  int res = conn.proto->write(gsl::span<const unsigned char>(conn.payload.data(), conn.payload.size()));
  if (res < 0) {
    ++conn.errors;
    ++g_stats.errors;
  }
  // sent counters are increased in the write callback (data handed to libuv)
}

int32_t proto_on_write(::atframework::gateway::libatgw_protocol_api *proto, gsl::span<unsigned char> buffer,
                       bool *is_done) {
  auto iter = g_proto_to_conn.find(proto);
  if (iter == g_proto_to_conn.end() || buffer.empty()) {
    if (nullptr != is_done) {
      *is_done = true;
    }
    return -1;
  }
  connection_t &conn = *iter->second;

  char *buffer_start = reinterpret_cast<char *>(buffer.data());
  unsigned int buffer_len = static_cast<unsigned int>(buffer.size());
  unsigned int header_offset = static_cast<unsigned int>(conn.proto->get_write_header_offset());
  if (buffer_len > header_offset) {
    buffer_start += header_offset;
    buffer_len -= header_offset;
  }

  uv_buf_t bufs[1] = {uv_buf_init(buffer_start, buffer_len)};
  uv_write_t *write_req = new (std::nothrow) uv_write_t();
  if (nullptr == write_req) {
    if (nullptr != is_done) {
      *is_done = true;
    }
    ++conn.errors;
    ++g_stats.errors;
    return -1;
  }
  write_req->data = proto;
  int res =
      uv_write(write_req, reinterpret_cast<uv_stream_t *>(&conn.tcp_sock), bufs, 1, [](uv_write_t *req, int status) {
        auto *proto_ptr = static_cast<::atframework::gateway::libatgw_protocol_api *>(req->data);
        auto iter = g_proto_to_conn.find(proto_ptr);
        if (iter != g_proto_to_conn.end()) {
          iter->second->write_pending = false;
          iter->second->proto->write_done(status);
          if (0 != status) {
            ++iter->second->errors;
            ++g_stats.errors;
          }
        }
        delete req;
      });
  if (0 != res) {
    delete write_req;
    if (nullptr != is_done) {
      *is_done = true;
    }
    ++conn.errors;
    ++g_stats.errors;
    return res;
  }

  conn.write_pending = true;
  ++conn.sent_msgs;
  ++g_stats.sent_msgs;
  conn.sent_bytes += g_conf.payload_size;
  g_stats.sent_bytes += g_conf.payload_size;
  if (nullptr != is_done) {
    *is_done = 0;  // libuv owns the write now; write_done arrives in the completion callback
  }
  return 0;
}

int32_t proto_on_message(::atframework::gateway::libatgw_protocol_api *proto, gsl::span<const unsigned char> buffer) {
  auto iter = g_proto_to_conn.find(proto);
  if (iter == g_proto_to_conn.end() || buffer.size() < kSeqPrefixSize) {
    return 0;
  }
  connection_t &conn = *iter->second;
  uint64_t seq = 0;
  memcpy(&seq, buffer.data(), sizeof(uint64_t));
  if (conn.recv_seq + 1 != seq) {
    ++conn.errors;
    ++g_stats.errors;
  }
  conn.recv_seq = seq;
  ++conn.recv_msgs;
  ++g_stats.recv_msgs;
  conn.recv_bytes += static_cast<uint64_t>(buffer.size());
  g_stats.recv_bytes += static_cast<uint64_t>(buffer.size());
  return 0;
}

int32_t proto_on_new_session(::atframework::gateway::libatgw_protocol_api *, uint64_t &sess_id) {
  sess_id = 0;
  return 0;
}

int32_t proto_on_reconnect(::atframework::gateway::libatgw_protocol_api *, uint64_t) {
  ++g_stats.reconnects;
  return 0;
}

int32_t proto_on_close(::atframework::gateway::libatgw_protocol_api *proto, int32_t, int32_t,
                       atfw::util::nostd::string_view) {
  auto iter = g_proto_to_conn.find(proto);
  if (iter != g_proto_to_conn.end()) {
    iter->second->closed = true;
  }
  return 0;
}

int32_t proto_on_handshake_done(::atframework::gateway::libatgw_protocol_api *proto, int status) {
  auto iter = g_proto_to_conn.find(proto);
  if (iter == g_proto_to_conn.end()) {
    return 0;
  }
  fprintf(stderr, "[dbg] handshake status=%d\n", status);
  if (0 != status) {
    ++iter->second->errors;
    ++g_stats.errors;
    return 0;
  }
  iter->second->handshake_done = true;
  ++g_stats.handshake_done_count;
  fprintf(stderr, "[dbg] handshake done\n");
  return 0;
}

int32_t proto_on_handshake_update(::atframework::gateway::libatgw_protocol_api *, int) { return 0; }

int32_t proto_on_error(::atframework::gateway::libatgw_protocol_api *proto, const char *filename, uint32_t line,
                       int errcode, const char *errmsg) {
  auto iter = g_proto_to_conn.find(proto);
  if (iter != g_proto_to_conn.end()) {
    ++iter->second->errors;
  }
  ++g_stats.errors;
  fprintf(stderr, "[proto-error] %s:%u code=%d msg=%s\n", filename ? filename : "?", line, errcode,
          errmsg ? errmsg : "");
  return 0;
}

void on_tick_timer(uv_timer_t *) {
  for (auto &conn : g_connections) {
    if (conn->proto && !conn->closed) {
      conn->proto->tick();
    }
  }

  double elapsed =
      std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::steady_clock::now() - g_stats.start_time)
          .count();
  if (elapsed >= g_conf.duration_sec && !g_stats.stopped) {
    fprintf(stderr, "[dbg] duration reached, stopping\n");
    g_stats.stopped = true;
    g_stats.stop_time = std::chrono::steady_clock::now();
    for (auto &conn : g_connections) {
      if (conn->proto && !conn->closed) {
        conn->proto->close(0, 0, "benchmark finished");
      }
      // Closing the tcp handle is required or the libuv loop never drains
      if (!uv_is_closing(reinterpret_cast<uv_handle_t *>(&conn->tcp_sock))) {
        uv_close(reinterpret_cast<uv_handle_t *>(&conn->tcp_sock), nullptr);
        conn->closed = true;
      }
    }
    uv_timer_stop(&g_stop_timer);
    uv_prepare_stop(&g_send_pump);
    if (!uv_is_closing(reinterpret_cast<uv_handle_t *>(&g_send_pump))) {
      uv_close(reinterpret_cast<uv_handle_t *>(&g_send_pump), nullptr);
    }
  }
}

void on_stop_timer(uv_timer_t *) {
  // Safety net: hard-stop if the drain phase takes too long
  if (!g_stats.stopped) {
    g_stats.stopped = true;
    g_stats.stop_time = std::chrono::steady_clock::now();
    for (auto &conn : g_connections) {
      if (conn->proto && !conn->closed) {
        conn->proto->close(0, 0, "hard timeout");
      }
    }
    uv_timer_stop(&g_stop_timer);
  }
}

void on_connect(uv_connect_t *req, int status) {
  auto *conn = static_cast<connection_t *>(req->data);
  conn->connect_pending = false;
  if (0 != status) {
    fprintf(stderr, "[connect] %s:%d failed: %s\n", g_conf.host.c_str(), g_conf.port, uv_strerror(status));
    ++conn->errors;
    ++g_stats.errors;
    conn->closed = true;
    return;
  }

  conn->proto = std::make_unique<::atframework::gateway::libatgw_protocol_sdk>(g_shared_context);
  conn->proto->set_callbacks(&g_proto_callbacks);
  g_proto_to_conn[conn->proto.get()] = conn;
  conn->tcp_sock.data = conn->proto.get();
  conn->proto->set_receive_buffer_limit(2 * 1024 * 1024, 0);
  conn->proto->set_send_buffer_limit(2 * 1024 * 1024, 0);

  int res = uv_read_start(
      reinterpret_cast<uv_stream_t *>(&conn->tcp_sock),
      [](uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
        auto *c = static_cast<connection_t *>(handle->data);
        if (nullptr == c || !c->proto) {
          buf->base = nullptr;
          buf->len = 0;
          return;
        }
        uint64_t len = 0;
        char *base = nullptr;
        c->proto->alloc_receive_buffer(suggested_size, base, len);
        buf->base = base;
#if defined(_MSC_VER)
        buf->len = static_cast<ULONG>(len);
#else
        buf->len = len;
#endif
      },
      [](uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
        auto *c = static_cast<connection_t *>(stream->data);
        if (nullptr == c || !c->proto) {
          return;
        }
        if (nread < 0) {
          c->proto->close(0, 0, "peer closed");
          return;
        }
        if (nread > 0) {
          int errcode = 0;
          c->proto->read(static_cast<int32_t>(nread),
                         gsl::span<const unsigned char>(reinterpret_cast<const unsigned char *>(buf->base),
                                                        static_cast<size_t>(nread)),
                         errcode);
        }
      });
  if (0 != res) {
    fprintf(stderr, "uv_read_start failed: %s\n", uv_strerror(res));
    ++conn->errors;
    ++g_stats.errors;
  }

  res = conn->proto->start_session();
  if (res < 0) {
    fprintf(stderr, "start_session failed: %d\n", res);
    ++conn->errors;
    ++g_stats.errors;
  }
}

void setup_shared_context() {
  ::atframework::gateway::libatgw_protocol_sdk::crypto_conf_t config;
  config.set_default();
  config.client_mode = true;

  if (0 == ATFW_UTIL_STRFUNC_STRCASE_CMP("x25519", g_conf.key_exchange.c_str())) {
    config.key_exchange_algorithm =
        ::atframework::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(key_exchange_t, kX25519);
  } else if (0 == ATFW_UTIL_STRFUNC_STRCASE_CMP("p-384", g_conf.key_exchange.c_str())) {
    config.key_exchange_algorithm =
        ::atframework::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(key_exchange_t, kSecp384r1);
  } else {
    // default: secp256r1 (p-256)
    config.key_exchange_algorithm =
        ::atframework::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(key_exchange_t, kSecp256r1);
  }

  if (g_conf.crypto != "none") {
    // aes-256-gcm is the benchmark target
    config.supported_algorithms.push_back(
        ::atframework::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kAes256Gcm));
  } else {
    config.supported_algorithms.push_back(
        ::atframework::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(crypto_algorithm_t, kNone));
  }

  if (g_conf.compression == "zstd") {
    config.compression_algorithms.push_back(
        ::atframework::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_algorithm_t, kZstd));
  } else if (g_conf.compression == "lz4") {
    config.compression_algorithms.push_back(
        ::atframework::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_algorithm_t, kLz4));
  } else {
    config.compression_algorithms.push_back(
        ::atframework::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_algorithm_t, kNone));
  }

  config.compression_level =
      ::atframework::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_level_t, kDefault);
  if (g_conf.level == "kStorage") {
    config.compression_level =
        ::atframework::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_level_t, kStorage);
  } else if (g_conf.level == "kFast") {
    config.compression_level =
        ::atframework::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_level_t, kFast);
  } else if (g_conf.level == "kLowCpu") {
    config.compression_level =
        ::atframework::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_level_t, kLowCpu);
  } else if (g_conf.level == "kBalanced") {
    config.compression_level =
        ::atframework::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_level_t, kBalanced);
  } else if (g_conf.level == "kHighRatio") {
    config.compression_level =
        ::atframework::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_level_t, kHighRatio);
  } else if (g_conf.level == "kMaxRatio") {
    config.compression_level =
        ::atframework::gateway::v2::ATFRAMEWORK_GATEWAY_MACRO_ENUM_VALUE(compression_level_t, kMaxRatio);
  }

  if (!g_conf.access_token.empty()) {
    config.access_tokens.emplace_back(g_conf.access_token.begin(), g_conf.access_token.end());
  }

  g_shared_context = ::atframework::gateway::libatgw_protocol_sdk::create_shared_context(config);
}

void write_json() {
  double elapsed =
      std::chrono::duration_cast<std::chrono::duration<double>>(g_stats.stop_time - g_stats.start_time).count();
  FILE *out = stdout;
  if (!g_conf.json_output.empty()) {
    out = fopen(g_conf.json_output.c_str(), "w");
  }
  if (nullptr == out) {
    return;
  }
  fprintf(out, "{\n");
  fprintf(out,
          "  \"config\": {\"host\": \"%s\", \"port\": %d, \"clients\": %d, \"payload_size\": %zu, "
          "\"duration_sec\": %.1f, \"compression\": \"%s\", \"level\": \"%s\", \"crypto\": \"%s\", "
          "\"key_exchange\": \"%s\"},\n",
          g_conf.host.c_str(), g_conf.port, g_conf.clients, g_conf.payload_size, g_conf.duration_sec,
          g_conf.compression.c_str(), g_conf.level.c_str(), g_conf.crypto.c_str(), g_conf.key_exchange.c_str());
  fprintf(out,
          "  \"totals\": {\"sent_msgs\": %" PRIu64 ", \"sent_bytes\": %" PRIu64 ", \"recv_msgs\": %" PRIu64
          ", \"recv_bytes\": %" PRIu64 ", \"errors\": %" PRIu64 ", \"handshake_done\": %" PRIu64
          ", \"reconnects\": %" PRIu64 ", \"elapsed_sec\": %.3f},\n",
          g_stats.sent_msgs, g_stats.sent_bytes, g_stats.recv_msgs, g_stats.recv_bytes, g_stats.errors,
          g_stats.handshake_done_count, g_stats.reconnects, elapsed);
  fprintf(out, "  \"per_conn\": [\n");
  bool first = true;
  for (auto &conn : g_connections) {
    fprintf(out,
            "    {\"sent\": %" PRIu64 ", \"recv\": %" PRIu64 ", \"bytes_sent\": %" PRIu64 ", \"bytes_recv\": %" PRIu64
            ", \"errors\": %" PRIu64 "}%s\n",
            conn->sent_msgs, conn->recv_msgs, conn->sent_bytes, conn->recv_bytes, conn->errors, first ? "" : ",");
    first = false;
  }
  fprintf(out, "  ]\n}\n");
  if (out != stdout) {
    fclose(out);
  }
}

}  // namespace

int main(int argc, char *argv[]) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    auto pos = arg.find('=');
    if (pos == std::string::npos) {
      continue;
    }
    std::string key = arg.substr(0, pos);
    std::string value = arg.substr(pos + 1);
    if (key == "--host") {
      g_conf.host = value;
    } else if (key == "--port") {
      g_conf.port = atoi(value.c_str());
    } else if (key == "--clients") {
      g_conf.clients = atoi(value.c_str());
    } else if (key == "--size") {
      g_conf.payload_size = static_cast<size_t>(strtoul(value.c_str(), nullptr, 10));
    } else if (key == "--duration") {
      g_conf.duration_sec = atof(value.c_str());
    } else if (key == "--compression") {
      g_conf.compression = value;
    } else if (key == "--level") {
      g_conf.level = value;
    } else if (key == "--key-exchange") {
      g_conf.key_exchange = value;
    } else if (key == "--crypto") {
      g_conf.crypto = value;
    } else if (key == "--access-token") {
      g_conf.access_token = value;
    } else if (key == "--json") {
      g_conf.json_output = value;
    }
  }
  if (g_conf.clients < 1) {
    g_conf.clients = 1;
  }
  if (g_conf.payload_size < kSeqPrefixSize) {
    g_conf.payload_size = kSeqPrefixSize;
  }

  g_proto_callbacks.write_fn = proto_on_write;
  g_proto_callbacks.message_fn = proto_on_message;
  g_proto_callbacks.new_session_fn = proto_on_new_session;
  g_proto_callbacks.reconnect_fn = proto_on_reconnect;
  g_proto_callbacks.close_fn = proto_on_close;
  g_proto_callbacks.on_handshake_done_fn = proto_on_handshake_done;
  g_proto_callbacks.on_handshake_update_fn = proto_on_handshake_update;
  g_proto_callbacks.on_error_fn = proto_on_error;

  setup_shared_context();
  if (!g_shared_context) {
    fprintf(stderr, "create shared context failed\n");
    return -1;
  }

  uv_loop_t loop;
  int res = uv_loop_init(&loop);
  if (0 != res) {
    fprintf(stderr, "uv_loop_init failed: %d\n", res);
    return -1;
  }
  g_loop = &loop;

  g_connections.reserve(static_cast<size_t>(g_conf.clients));
  for (int i = 0; i < g_conf.clients; ++i) {
    auto conn = std::make_unique<connection_t>();
    int ret = uv_tcp_init(&loop, &conn->tcp_sock);
    if (0 != ret) {
      fprintf(stderr, "uv_tcp_init failed: %d\n", ret);
      return -1;
    }
    g_connections.push_back(std::move(conn));
  }

  uv_timer_init(&loop, &g_tick_timer);
  uv_timer_start(&g_tick_timer, on_tick_timer, 1000, 1000);
  uv_timer_init(&loop, &g_stop_timer);
  uv_timer_start(&g_stop_timer, on_stop_timer, static_cast<uint64_t>(g_conf.duration_sec * 1000) + 30000, 30000);

  g_stats.start_time = std::chrono::steady_clock::now();

  sockaddr_in dest{};
  res = uv_ip4_addr(g_conf.host.c_str(), g_conf.port, &dest);
  if (0 != res) {
    fprintf(stderr, "uv_ip4_addr failed: %s\n", uv_strerror(res));
    return -1;
  }
  for (auto &conn : g_connections) {
    conn->connect_pending = true;
    conn->connect_req.data = conn.get();
    res = uv_tcp_connect(&conn->connect_req, &conn->tcp_sock, reinterpret_cast<const sockaddr *>(&dest),
                         [](uv_connect_t *req, int status) { on_connect(req, status); });
    if (0 != res) {
      fprintf(stderr, "uv_tcp_connect failed: %s\n", uv_strerror(res));
      conn->connect_pending = false;
      conn->closed = true;
      ++g_stats.errors;
    }
  }

  // Drive the flood from the prepare phase: try_send on every loop iteration.
  uv_prepare_init(&loop, &g_send_pump);
  uv_prepare_start(&g_send_pump, [](uv_prepare_t *) {
    if (g_stats.stopped) {
      return;
    }
    for (auto &conn : g_connections) {
      try_send(*conn);
    }
  });

  fprintf(stderr, "[dbg] entering uv_run\n");
  uv_run(&loop, UV_RUN_DEFAULT);
  fprintf(stderr, "[dbg] uv_run returned\n");

  if (g_stats.stop_time == std::chrono::steady_clock::time_point{}) {
    g_stats.stop_time = std::chrono::steady_clock::now();
  }

  write_json();
  fprintf(stderr, "[dbg] json written\n");

  uv_prepare_stop(&g_send_pump);
  uv_close(reinterpret_cast<uv_handle_t *>(&g_send_pump), nullptr);
  uv_close(reinterpret_cast<uv_handle_t *>(&g_tick_timer), nullptr);
  uv_close(reinterpret_cast<uv_handle_t *>(&g_stop_timer), nullptr);
  for (auto &conn : g_connections) {
    if (!conn->closed) {
      uv_close(reinterpret_cast<uv_handle_t *>(&conn->tcp_sock), nullptr);
    }
  }
  uv_run(&loop, UV_RUN_NOWAIT);
  uv_loop_close(&loop);
  return 0;
}
