#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "uv.h"

#include "common/string_oprs.h"
#include "std/smart_ptr.h"

#include <config/atframe_services_build_feature.h>
#include <libatgw_inner_v1_c.h>

template <typename TCH = char>
TCH to_lower_case(TCH c) {
    if (c >= 'A' && c <= 'Z') {
        return static_cast<TCH>(c - 'A' + 'a');
    }

    return c;
}

struct client_libuv_data_t {
    uv_tcp_t         tcp_sock;
    uv_connect_t     tcp_req;
    uv_getaddrinfo_t dns_req;
    uv_write_t       write_req;
    uv_timer_t       tick_timer;
};

client_libuv_data_t g_client;

struct proto_wrapper {
    libatgw_inner_v1_c_context ctx;
    proto_wrapper() : ctx(libatgw_inner_v1_c_create()) {}
    ~proto_wrapper() {
        if (NULL != ctx) {
            libatgw_inner_v1_c_destroy(ctx);
        }
    }
};

struct client_session_data_t {
    uint64_t                       session_id;
    long long                      seq;
    std::shared_ptr<proto_wrapper> proto;

    bool   print_msg;
    bool   busy_mode;
    bool   allow_reconnect;
    size_t sec_send_bound;
    size_t sec_recv_count;
    size_t sum_recv_count;
    size_t sec_send_count;
    size_t sum_send_count;
    size_t sec_recv_size;
    size_t sum_recv_size;
    size_t sec_send_size;
    size_t sum_send_size;
};

client_session_data_t g_client_sess;

std::string g_host;
int         g_port;
std::string crypt_types;

static std::pair<unsigned long long, const char *> get_size_readable(size_t sz) {
    const char *unit = "B";
    if (sz >= 32768) {
        sz /= 1024;
        unit = "KB";
    }

    if (sz >= 32768) {
        sz /= 1024;
        unit = "MB";
    }

    if (sz >= 32768) {
        sz /= 1024;
        unit = "GB";
    }

    if (sz >= 32768) {
        sz /= 1024;
        unit = "TB";
    }

    return std::pair<unsigned long long, const char *>(static_cast<unsigned long long>(sz), unit);
}

// ======================== 以下为网络处理及回调 ========================
static int  close_sock();
static void libuv_close_sock_callback(uv_handle_t *handle);

static void libuv_tcp_recv_alloc_fn(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    if (!g_client_sess.proto) {
        uv_read_stop((uv_stream_t *)handle);
        return;
    }


#if _MSC_VER
    uint64_t len = 0;
    libatgw_inner_v1_c_read_alloc(g_client_sess.proto->ctx, suggested_size, &buf->base, &len);
    buf->len = static_cast<ULONG>(len);
#else
    uint64_t len = 0;
    libatgw_inner_v1_c_read_alloc(g_client_sess.proto->ctx, suggested_size, &buf->base, &len);
    buf->len = len;
#endif

    if (NULL == buf->base && 0 == buf->len) {
        uv_read_stop((uv_stream_t *)handle);
    }
}

static void libuv_tcp_recv_read_fn(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
    // if no more data or EAGAIN or break by signal, just ignore
    if (0 == nread || UV_EAGAIN == nread || UV_EAI_AGAIN == nread || UV_EINTR == nread) {
        return;
    }

    // if network error or reset by peer, move session into reconnect queue
    if (nread < 0) {
        // notify to close fd
        close_sock();
        return;
    }

    if (g_client_sess.proto) {
        // add reference in case of destroyed in read callback
        std::shared_ptr<proto_wrapper> sess_proto = g_client_sess.proto;
        int32_t                        errcode    = 0;
        libatgw_inner_v1_c_read(sess_proto->ctx, static_cast<int32_t>(nread), buf->base, static_cast<size_t>(nread), &errcode);
        if (0 != errcode) {
            fprintf(stderr, "[Read]: failed, res: %d\n", errcode);
            close_sock();
        }
    }
}

static void libuv_tcp_connect_callback(uv_connect_t *req, int status) {
    req->data = NULL;
    if (0 != status) {
        fprintf(stderr, "libuv_tcp_connect_callback callback failed, msg: %s\n", uv_strerror(status));
        uv_stop(req->handle->loop);
        return;
    }

    uv_read_start(req->handle, libuv_tcp_recv_alloc_fn, libuv_tcp_recv_read_fn);
    int ret = 0;

    std::shared_ptr<proto_wrapper> sess_proto = std::make_shared<proto_wrapper>();
    libatgw_inner_v1_c_set_recv_buffer_limit(sess_proto->ctx, 2 * 1024 * 1024, 0);
    libatgw_inner_v1_c_set_send_buffer_limit(sess_proto->ctx, 2 * 1024 * 1024, 0);

    if (g_client_sess.proto && g_client_sess.allow_reconnect) {
        std::vector<unsigned char> secret;
        uint64_t                   secret_len = libatgw_inner_v1_c_get_crypt_secret_size(g_client_sess.proto->ctx);
        secret.resize(secret_len);
        std::string crypt_type = libatgw_inner_v1_c_get_crypt_type(g_client_sess.proto->ctx);
        libatgw_inner_v1_c_copy_crypt_secret(g_client_sess.proto->ctx, &secret[0], secret_len);

        g_client_sess.proto = sess_proto;
        ret                 = libatgw_inner_v1_c_reconnect_session(sess_proto->ctx, g_client_sess.session_id, crypt_type.c_str(), &secret[0], secret_len);
    } else {
        g_client_sess.proto = sess_proto;

        ret = libatgw_inner_v1_c_start_session(sess_proto->ctx, crypt_types.c_str());
    }
    if (0 != ret) {
        fprintf(stderr, "start session failed, res: %d\n", ret);
        uv_close((uv_handle_t *)&g_client.tcp_sock, libuv_close_sock_callback);
        libatgw_inner_v1_c_close(sess_proto->ctx, 0);
    }
}

static void libuv_dns_callback(uv_getaddrinfo_t *req, int status, struct addrinfo *res) {
    req->data = NULL;
    do {
        if (0 != status) {
            fprintf(stderr, "uv_getaddrinfo callback failed, msg: %s\n", uv_strerror(status));
            uv_stop(req->loop);
            break;
        }

        if (NULL != g_client.dns_req.data || NULL != g_client.tcp_sock.data) {
            break;
        }

        sockaddr_storage real_addr;
        uv_tcp_init(req->loop, &g_client.tcp_sock);
        g_client.tcp_sock.data = &g_client;

        if (AF_INET == res->ai_family) {
            sockaddr_in *res_c  = (struct sockaddr_in *)(res->ai_addr);
            char         ip[17] = {0};
            uv_ip4_name(res_c, ip, sizeof(ip));
            uv_ip4_addr(ip, g_port, (struct sockaddr_in *)&real_addr);
        } else if (AF_INET6 == res->ai_family) {
            sockaddr_in6 *res_c  = (struct sockaddr_in6 *)(res->ai_addr);
            char          ip[40] = {0};
            uv_ip6_name(res_c, ip, sizeof(ip));
            uv_ip6_addr(ip, g_port, (struct sockaddr_in6 *)&real_addr);
        } else {
            fprintf(stderr, "uv_tcp_connect failed, ai_family not supported: %d\n", res->ai_family);
            break;
        }

        int res_code = uv_tcp_connect(&g_client.tcp_req, &g_client.tcp_sock, (struct sockaddr *)&real_addr, libuv_tcp_connect_callback);
        if (0 != res_code) {
            fprintf(stderr, "uv_tcp_connect failed, msg: %s\n", uv_strerror(res_code));
            uv_close((uv_handle_t *)&g_client.tcp_sock, libuv_close_sock_callback);
            uv_stop(req->loop);
            break;
        }
    } while (false);

    // free addrinfo
    if (NULL != res) {
        uv_freeaddrinfo(res);
    }
}

static int start_connect() {
    if (NULL != g_client.dns_req.data) {
        return 0;
    }

    int ret = uv_getaddrinfo(uv_default_loop(), &g_client.dns_req, libuv_dns_callback, g_host.c_str(), NULL, NULL);
    if (0 != ret) {
        fprintf(stderr, "uv_getaddrinfo failed, msg: %s\n", uv_strerror(ret));
        return ret;
    }

    g_client.dns_req.data = &g_client;
    return 0;
}

void libuv_close_sock_callback(uv_handle_t *handle) {
    handle->data = NULL;
    printf("close socket finished\n");

    g_client.tcp_sock.data = NULL;
}

int close_sock() {
    if (!g_client_sess.proto) {
        if (NULL != g_client.tcp_sock.data) {
            printf("close socket start\n");
            uv_close((uv_handle_t *)&g_client.tcp_sock, libuv_close_sock_callback);
        }

        return 0;
    }

    printf("close protocol start\n");
    return libatgw_inner_v1_c_close(g_client_sess.proto->ctx, 0);
}

// ======================== 以下为协议处理回调 ========================
static void proto_inner_callback_on_written_fn(uv_write_t *req, int status) {
    if (g_client_sess.proto) {
        libatgw_inner_v1_c_write_done(g_client_sess.proto->ctx, status);
    }
}

static int32_t proto_inner_callback_on_write(libatgw_inner_v1_c_context ctx, void *buffer, uint64_t sz, int32_t *is_done) {
    if (!g_client_sess.proto || NULL == buffer) {
        if (NULL != is_done) {
            *is_done = 1;
        }

        return -1;
    }

    uv_buf_t bufs[1] = {uv_buf_init(reinterpret_cast<char *>(buffer), static_cast<unsigned int>(sz))};
    int      ret     = uv_write(&g_client.write_req, (uv_stream_t *)&g_client.tcp_sock, bufs, 1, proto_inner_callback_on_written_fn);
    if (0 != ret) {
        fprintf(stderr, "send data to proto 0x%llx failed, msg: %s\n", static_cast<unsigned long long>(g_client_sess.session_id), uv_strerror(ret));
    }

    if (NULL != is_done) {
        // if not writting, notify write finished
        *is_done = (0 != ret) ? 1 : 0;
    }

    return ret;
}

static int send_next_hello_message() {
    char msg[64] = {0};
    UTIL_STRFUNC_SNPRINTF(msg, sizeof(msg), "hello 0x%llx, %lld", static_cast<unsigned long long>(g_client_sess.session_id),
                          static_cast<long long>(++g_client_sess.seq));
    size_t msg_sz = strlen(msg);
    int    ret    = libatgw_inner_v1_c_post_msg(g_client_sess.proto->ctx, msg, msg_sz);
    if (g_client_sess.print_msg) {
        printf("[Tick]: send %s, res: %d\n", msg, ret);
    }

    if (0 == ret) {
        ++g_client_sess.sec_send_count;
        ++g_client_sess.sum_send_count;
        g_client_sess.sec_send_size += msg_sz;
        g_client_sess.sum_send_size += msg_sz;
    }

    return ret;
}

static int proto_inner_callback_on_message(libatgw_inner_v1_c_context ctx, const void *buffer, uint64_t sz) {
    if (g_client_sess.print_msg && NULL != buffer && sz > 0) {
        printf("[recv message]: %s\n", std::string(reinterpret_cast<const char *>(buffer), (size_t)sz).c_str());
    }

    ++g_client_sess.sec_recv_count;
    ++g_client_sess.sum_recv_count;
    g_client_sess.sec_recv_size += sz;
    g_client_sess.sum_recv_size += sz;

    if (g_client_sess.sec_send_count < g_client_sess.sec_send_bound) {
        send_next_hello_message();
    }
    return 0;
}

// useless
static int proto_inner_callback_on_new_session(libatgw_inner_v1_c_context ctx, uint64_t *sess_id) {
    printf("create session 0x%llx\n", NULL == sess_id ? 0 : static_cast<unsigned long long>(*sess_id));
    return 0;
}

// useless
static int proto_inner_callback_on_reconnect(libatgw_inner_v1_c_context ctx, uint64_t sess_id) {
    printf("reconnect session 0x%llx\n", static_cast<unsigned long long>(sess_id));
    return 0;
}

static int proto_inner_callback_on_close(libatgw_inner_v1_c_context ctx, int32_t reason) {
    if (NULL == g_client.tcp_sock.data) {
        return 0;
    }

    printf("close socket start, reason: %d\n", reason);
    if (0 == uv_is_closing((uv_handle_t *)&g_client.tcp_sock)) {
        uv_close((uv_handle_t *)&g_client.tcp_sock, libuv_close_sock_callback);
    }

    g_client_sess.allow_reconnect = 1000 > reason;
    return 0;
}

static int proto_inner_callback_on_handshake(libatgw_inner_v1_c_context ctx, int32_t status) {
    char buffer[4096];
    if (0 == status) {
        libatgw_inner_v1_c_get_info(ctx, buffer, 4096);
        printf("[Info]: handshake done\n%s\n", buffer);
        g_client_sess.session_id = libatgw_inner_v1_c_get_session_id(ctx);
    } else {
        fprintf(stderr, "[Error]: handshake failed, status=%d\n", status);
        // handshake failed, do not reconnect any more
        g_client_sess.proto.reset();
        return -1;
    }

    return 0;
}

static int proto_inner_callback_on_handshake_update(libatgw_inner_v1_c_context ctx, int32_t status) {
    char buffer[4096];
    if (0 == status) {
        libatgw_inner_v1_c_get_info(ctx, buffer, 4096);
        printf("[Info]: handshake update done\n%s\n", buffer);
        g_client_sess.session_id = libatgw_inner_v1_c_get_session_id(ctx);
    } else {
        fprintf(stderr, "[Error]: handshake update failed, status=%d\n", status);
        // handshake failed, do not reconnect any more
        libatgw_inner_v1_c_close(ctx, 0);
        g_client_sess.proto.reset();
        return -1;
    }

    return 0;
}

static int proto_inner_callback_on_error(libatgw_inner_v1_c_context ctx, const char *filename, int line, int errcode, const char *errmsg) {
    fprintf(stderr, "[Error][%s:%d]: error code: %d, msg: %s\n", filename, line, errcode, errmsg);
    return 0;
}

static void libuv_tick_timer_callback(uv_timer_t *handle) {
    if (NULL == g_client.tcp_sock.data && NULL == g_client.dns_req.data) {
        if (!g_client_sess.allow_reconnect) {
            puts("client exit.");
            uv_stop(handle->loop);
        } else {
            puts("client try to reconnect.");
            start_connect();
        }
        return;
    }

    if (!g_client_sess.proto) {
        return;
    }

    if (0 == g_client_sess.session_id) {
        return;
    }

    if (libatgw_inner_v1_c_is_closing(g_client_sess.proto->ctx)) {
        return;
    }

    if (!libatgw_inner_v1_c_is_handshake_done(g_client_sess.proto->ctx)) {
        return;
    }

    if (g_client_sess.busy_mode) {
        printf("[Tick]: sec recv(%llu,%llu%s), sum recv(%llu,%llu%s), sec send(%llu,%llu%s), sum send(%llu,%llu%s)\n",
               static_cast<unsigned long long>(g_client_sess.sec_recv_count), get_size_readable(g_client_sess.sec_recv_size).first,
               get_size_readable(g_client_sess.sec_recv_size).second, static_cast<unsigned long long>(g_client_sess.sum_recv_count),
               get_size_readable(g_client_sess.sum_recv_size).first, get_size_readable(g_client_sess.sum_recv_size).second,
               static_cast<unsigned long long>(g_client_sess.sec_send_count), get_size_readable(g_client_sess.sec_send_size).first,
               get_size_readable(g_client_sess.sec_send_size).second, static_cast<unsigned long long>(g_client_sess.sum_send_count),
               get_size_readable(g_client_sess.sum_send_size).first, get_size_readable(g_client_sess.sum_send_size).second);
    }


    g_client_sess.sec_recv_count = 0;
    g_client_sess.sec_send_count = 0;
    g_client_sess.sec_recv_size  = 0;
    g_client_sess.sec_send_size  = 0;

    bool continue_send = true;
    while (continue_send && g_client_sess.sec_send_count < g_client_sess.sec_send_bound) {
        continue_send = 0 == send_next_hello_message();
    }
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <ip> <port> [crypt types] [mode]\n\tmode can be tick or busy\n\t%s 127.0.0.1 9005 xxtea:aes-128-cfb:aes-256-cfb tick\n",
                argv[0], argv[0]);
        return -1;
    }

    // setup crypt algorithms
    libatgw_inner_v1_c_global_init_algorithms();

    // init
    libatgw_inner_v1_c_gset_on_write_start_fn(proto_inner_callback_on_write);
    libatgw_inner_v1_c_gset_on_message_fn(proto_inner_callback_on_message);
    libatgw_inner_v1_c_gset_on_init_new_session_fn(proto_inner_callback_on_new_session);
    libatgw_inner_v1_c_gset_on_init_reconnect_fn(proto_inner_callback_on_reconnect);
    libatgw_inner_v1_c_gset_on_close_fn(proto_inner_callback_on_close);
    libatgw_inner_v1_c_gset_on_handshake_done_fn(proto_inner_callback_on_handshake);
    libatgw_inner_v1_c_gset_on_handshake_update_fn(proto_inner_callback_on_handshake_update);
    libatgw_inner_v1_c_gset_on_error_fn(proto_inner_callback_on_error);

    g_client_sess.session_id      = 0;
    g_client_sess.seq             = 0;
    g_client_sess.print_msg       = false;
    g_client_sess.busy_mode       = false;
    g_client_sess.allow_reconnect = true;
    g_client_sess.sec_send_bound  = 1;
    g_client_sess.sec_recv_count  = 0;
    g_client_sess.sum_recv_count  = 0;
    g_client_sess.sec_send_count  = 0;
    g_client_sess.sum_send_count  = 0;
    g_client_sess.sec_recv_size   = 0;
    g_client_sess.sum_recv_size   = 0;
    g_client_sess.sec_send_size   = 0;
    g_client_sess.sum_send_size   = 0;

    std::string mode = "tick";
    g_host           = argv[1];
    g_port           = static_cast<int>(strtol(argv[2], NULL, 10));
    memset(&g_client, 0, sizeof(g_client));

    if (argc > 3) {
        crypt_types = argv[3];
    }

    if (crypt_types.empty()) {
        crypt_types = "xxtea:aes-128-cfb:aes-256-cfb";
    }

    if (argc > 4) {
        mode = argv[4];
        std::transform(mode.begin(), mode.end(), mode.begin(), to_lower_case<char>);
    }

    if ("tick" == mode) {
        uv_timer_init(uv_default_loop(), &g_client.tick_timer);
        uv_timer_start(&g_client.tick_timer, libuv_tick_timer_callback, 1000, 1000);
        g_client_sess.print_msg = true;
    } else if ("busy" == mode) {
        uv_timer_init(uv_default_loop(), &g_client.tick_timer);
        uv_timer_start(&g_client.tick_timer, libuv_tick_timer_callback, 1000, 1000);
        g_client_sess.print_msg      = false;
        g_client_sess.busy_mode      = true;
        g_client_sess.sec_send_bound = 5000;
    } else {
        fprintf(stderr, "unsupport mode %s\n", mode.c_str());
        return -1;
    }

    int ret = start_connect();
    if (ret < 0) {
        return ret;
    }

    ret = uv_run(uv_default_loop(), UV_RUN_DEFAULT);

    libatgw_inner_v1_c_global_cleanup_algorithms();
    return ret;
}
