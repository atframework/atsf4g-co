// Copyright 2026 atframework

#include <atframework/testing/mock_dns.h>
#include <atframework/testing/runtime.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "frame/test_macros.h"
#include "rpc/dns/lookup.h"
#include "rpc/rpc_lru_cache_map.h"

namespace {
// 实例计数器：用于在每个用例结束时验证缓存对象全部释放。缓存对象的 io_task 强引用任务，
// 任务的 lambda 又捕获缓存的强引用，构成引用环；只有所有路径都正确 reset_task 环才会断开。
struct counted_object {
  static int live_instances;
  int payload;

  counted_object() : payload(0) { ++live_instances; }
  explicit counted_object(int v) : payload(v) { ++live_instances; }
  counted_object(const counted_object &other) : payload(other.payload) { ++live_instances; }
  counted_object(counted_object &&other) noexcept : payload(other.payload) { ++live_instances; }
  counted_object &operator=(const counted_object &) = default;
  counted_object &operator=(counted_object &&) noexcept = default;
  ~counted_object() { --live_instances; }
};
int counted_object::live_instances = 0;

using test_map_type = rpc::rpc_lru_cache_map<std::string, counted_object>;
using test_cache_ptr = test_map_type::cache_ptr_type;
using test_fetch_fn =
    std::function<rpc::result_code_type(rpc::context &, const std::string &, counted_object &, int64_t *)>;

int start_test_runtime(atfw::testing::runtime &test) {
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::dns};
  if (0 != test.start(options) || !test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return -1;
  }
  return 0;
}

// 泵循环直到条件满足（或超过代数上限），返回条件是否满足。
bool pump_until(atfw::testing::runtime &test, const std::function<bool()> &pred, int max_pumps = 64) {
  for (int i = 0; i < max_pumps && !pred(); ++i) {
    test.pump_once();
  }
  return pred();
}

// 立即成功的拉取 fn。
test_fetch_fn make_immediate_fetch_fn(int &calls) {
  return [&calls](rpc::context &, const std::string &, counted_object &val_out,
                  int64_t *out_version) -> rpc::result_code_type {
    ++calls;
    val_out.payload = 1;
    if (nullptr != out_version) {
      *out_version = 1;
    }
    RPC_RETURN_CODE(0);
  };
}

// 挂起原语：通过带 delay_generations 的 DNS 规则在 fn 内制造真实的在途 IO 窗口。
void register_delayed_gate(atfw::testing::runtime &test, gsl::string_view domain, uint32_t delay_generations) {
  atfw::testing::dns_rule_options rule_options;
  rule_options.delay_generations = delay_generations;
  auto rule = test.dns().mock_a(domain, "127.0.0.1", rule_options);
  CASE_EXPECT_TRUE(!!rule);
}

rpc::result_code_type await_dns_gate(rpc::context &ctx, gsl::string_view domain) {
  std::vector<rpc::dns::address_record> records;
  RPC_RETURN_CODE(RPC_AWAIT_CODE_RESULT(rpc::dns::lookup(ctx, domain, records)));
}

// 等待并校验一个任务正常退出且返回指定错误码。
void expect_task_done(const atfw::testing::wait_result &result, int32_t expected_code) {
  CASE_EXPECT_TRUE(result.task_exited);
  CASE_EXPECT_FALSE(result.hard_timed_out);
  CASE_EXPECT_EQ(expected_code, result.result_code);
}

// 清空池，用于泄漏断言前释放全部缓存对象。
void drain_pool(test_map_type &caches) {
  while (!caches.empty()) {
    caches.pop_front();
  }
}
}  // namespace

// 同步 API：get_cache/set_cache/remove_cache 与 LRU 访问序、淘汰语义。
// lru_map 的访问序为 front=最久未访问、back=最近访问；get_cache 会把条目挪到 back。
CASE_TEST(server_frame_unit_test, rpc_lru_cache_sync_api_order_and_eviction) {
  CASE_EXPECT_EQ(0, counted_object::live_instances);
  atfw::testing::runtime test;
  if (0 != start_test_runtime(test)) {
    return;
  }

  test_map_type caches;
  int fetch_calls = 0;
  test_cache_ptr evicted;
  {
    auto task = test.run_task(
        "lru_sync_seed", std::chrono::seconds{5}, [&caches, &fetch_calls](rpc::context &ctx) -> rpc::result_code_type {
          for (const char *key : {"a", "b", "c"}) {
            test_cache_ptr out;
            int32_t res =
                RPC_AWAIT_CODE_RESULT(caches.await_fetch(ctx, key, out, make_immediate_fetch_fn(fetch_calls)));
            CASE_EXPECT_EQ(0, res);
          }
          RPC_RETURN_CODE(0);
        });
    if (task.empty()) {
      test.stop();
      return;
    }
    expect_task_done(test.wait(task, std::chrono::seconds{10}), 0);
  }

  // 插入序 a,b,c：back=最近插入，front=最久未访问。
  CASE_EXPECT_EQ(3, static_cast<int>(caches.size()));
  CASE_EXPECT_EQ(std::string("a"), caches.front().first);
  CASE_EXPECT_EQ(std::string("c"), caches.back().first);
  CASE_EXPECT_TRUE(nullptr == caches.get_cache("missing"));
  CASE_EXPECT_EQ(3, static_cast<int>(caches.size()));

  // 访问 a 后 a 挪到 back：b,c,a。
  CASE_EXPECT_TRUE(nullptr != caches.get_cache("a"));
  CASE_EXPECT_EQ(std::string("b"), caches.front().first);
  CASE_EXPECT_EQ(std::string("a"), caches.back().first);

  // remove_cache 只生效一次；条目被真正从池中删除（不再占池容量），旧句柄置 removed 防止复活。
  // 此前访问序为 c,a,b（a、b 先后被 get_cache 挪到 back），删除 b 后余 c,a。
  evicted = caches.get_cache("b");
  CASE_EXPECT_TRUE(caches.remove_cache("b"));
  CASE_EXPECT_FALSE(caches.remove_cache("b"));
  CASE_EXPECT_EQ(2, static_cast<int>(caches.size()));
  CASE_EXPECT_TRUE(nullptr == caches.get_cache("b"));
  CASE_EXPECT_TRUE(nullptr != evicted && evicted->removed);

  // 淘汰（pop_front/pop_back）不置 removed：淘汰不是删除。
  caches.pop_front();  // evicts "c"
  CASE_EXPECT_EQ(1, static_cast<int>(caches.size()));
  CASE_EXPECT_EQ(std::string("a"), caches.front().first);
  caches.pop_back();  // evicts "a"
  CASE_EXPECT_TRUE(caches.empty());
  CASE_EXPECT_EQ(3, fetch_calls);

  evicted.reset();
  pump_until(test, []() { return 0 == counted_object::live_instances; });
  CASE_EXPECT_EQ(0, counted_object::live_instances);

  test.stop();
}

// await_fetch 基本流：未命中拉取并写入缓存；命中直接返回同一对象且不再拉取；参数校验。
CASE_TEST(server_frame_unit_test, rpc_lru_cache_fetch_populate_hit_and_param_check) {
  CASE_EXPECT_EQ(0, counted_object::live_instances);
  atfw::testing::runtime test;
  if (0 != start_test_runtime(test)) {
    return;
  }

  test_map_type caches;
  int fetch_calls = 0;
  int must_not_call = 0;
  test_cache_ptr first_ptr;
  int32_t second_res = -1;
  int32_t param_res = -1;
  {
    auto task = test.run_task("lru_fetch_basic", std::chrono::seconds{5},
                              [&caches, &fetch_calls, &must_not_call, &first_ptr, &second_res,
                               &param_res](rpc::context &ctx) -> rpc::result_code_type {
                                test_cache_ptr out;
                                int32_t res = RPC_AWAIT_CODE_RESULT(caches.await_fetch(
                                    ctx, "k1", out,
                                    [&fetch_calls](rpc::context &, const std::string &, counted_object &val_out,
                                                   int64_t *out_version) -> rpc::result_code_type {
                                      ++fetch_calls;
                                      val_out.payload = 42;
                                      if (nullptr != out_version) {
                                        *out_version = 7;
                                      }
                                      RPC_RETURN_CODE(0);
                                    }));
                                CASE_EXPECT_EQ(0, res);
                                CASE_EXPECT_TRUE(nullptr != out);
                                CASE_EXPECT_EQ(42, out->data_object.payload);
                                CASE_EXPECT_EQ(7, static_cast<int>(out->data_version));
                                first_ptr = out;

                                // 第二次 fetch 命中缓存：fn 不应被调用，返回同一对象。
                                test_cache_ptr again;
                                second_res = RPC_AWAIT_CODE_RESULT(caches.await_fetch(
                                    ctx, "k1", again,
                                    [&must_not_call](rpc::context &, const std::string &, counted_object &,
                                                     int64_t *) -> rpc::result_code_type {
                                      ++must_not_call;
                                      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_UNKNOWN);
                                    }));
                                CASE_EXPECT_EQ(0, second_res);
                                CASE_EXPECT_TRUE(first_ptr == again);
                                CASE_EXPECT_TRUE(task_type_trait::empty(again->io_task));

                                // 空 fn 参数校验。
                                test_cache_ptr dummy;
                                param_res = RPC_AWAIT_CODE_RESULT(caches.await_fetch(ctx, "k1", dummy, nullptr));
                                CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM, param_res);
                                RPC_RETURN_CODE(0);
                              });
    if (task.empty()) {
      test.stop();
      return;
    }
    expect_task_done(test.wait(task, std::chrono::seconds{10}), 0);
  }

  CASE_EXPECT_EQ(0, second_res);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM, param_res);
  CASE_EXPECT_EQ(1, fetch_calls);
  CASE_EXPECT_EQ(0, must_not_call);
  CASE_EXPECT_EQ(1, static_cast<int>(caches.size()));
  CASE_EXPECT_FALSE(caches.is_io_task_running("k1"));
  CASE_EXPECT_FALSE(caches.is_io_task_running("missing"));

  // 引用环按路径释放：任务句柄出作用域 + 泵若干代后缓存对象全部销毁。
  caches.remove_cache("k1");
  drain_pool(caches);
  first_ptr.reset();
  pump_until(test, []() { return 0 == counted_object::live_instances; });
  CASE_EXPECT_EQ(0, counted_object::live_instances);

  test.stop();
}

// await_fetch 失败：错误码透传、条目被移除、后续可重新拉取。
CASE_TEST(server_frame_unit_test, rpc_lru_cache_fetch_failure_removes_entry) {
  CASE_EXPECT_EQ(0, counted_object::live_instances);
  atfw::testing::runtime test;
  if (0 != start_test_runtime(test)) {
    return;
  }

  test_map_type caches;
  int fetch_calls = 0;
  int32_t fail_res = 0;
  {
    auto task = test.run_task("lru_fetch_fail", std::chrono::seconds{5},
                              [&caches, &fetch_calls, &fail_res](rpc::context &ctx) -> rpc::result_code_type {
                                test_cache_ptr out;
                                fail_res = RPC_AWAIT_CODE_RESULT(caches.await_fetch(
                                    ctx, "k1", out,
                                    [&fetch_calls](rpc::context &, const std::string &, counted_object &,
                                                   int64_t *) -> rpc::result_code_type {
                                      ++fetch_calls;
                                      RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND);
                                    }));
                                CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND, fail_res);

                                // 缓存被移除后重新拉取成功。
                                test_cache_ptr refetch;
                                int32_t res = RPC_AWAIT_CODE_RESULT(
                                    caches.await_fetch(ctx, "k1", refetch, make_immediate_fetch_fn(fetch_calls)));
                                CASE_EXPECT_EQ(0, res);
                                CASE_EXPECT_EQ(1, refetch->data_object.payload);
                                RPC_RETURN_CODE(0);
                              });
    if (task.empty()) {
      test.stop();
      return;
    }
    expect_task_done(test.wait(task, std::chrono::seconds{10}), 0);
  }

  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND, fail_res);
  CASE_EXPECT_EQ(2, fetch_calls);
  CASE_EXPECT_EQ(1, static_cast<int>(caches.size()));
  CASE_EXPECT_TRUE(nullptr != caches.get_cache("k1"));

  test.stop();
}

// 并发拉取合并：第一个任务在途时，后续任务排队等待并共享同一次 IO 与同一份缓存。
CASE_TEST(server_frame_unit_test, rpc_lru_cache_concurrent_fetch_merged) {
  CASE_EXPECT_EQ(0, counted_object::live_instances);
  atfw::testing::runtime test;
  if (0 != start_test_runtime(test)) {
    return;
  }

  register_delayed_gate(test, "lru-fetch-gate.local", 3);

  test_map_type caches;
  int fetch_calls = 0;
  test_cache_ptr ptr_a;
  test_cache_ptr ptr_b;
  {
    auto task_a = test.run_task("lru_fetch_a", std::chrono::seconds{10},
                                [&caches, &fetch_calls, &ptr_a](rpc::context &ctx) -> rpc::result_code_type {
                                  int32_t res = RPC_AWAIT_CODE_RESULT(caches.await_fetch(
                                      ctx, "k1", ptr_a,
                                      [&fetch_calls](rpc::context &gate_ctx, const std::string &,
                                                     counted_object &val_out, int64_t *) -> rpc::result_code_type {
                                        ++fetch_calls;
                                        int32_t gate_res =
                                            RPC_AWAIT_CODE_RESULT(await_dns_gate(gate_ctx, "lru-fetch-gate.local"));
                                        if (gate_res < 0) {
                                          RPC_RETURN_CODE(gate_res);
                                        }
                                        val_out.payload = 11;
                                        RPC_RETURN_CODE(0);
                                      }));
                                  RPC_RETURN_CODE(res);
                                });
    if (task_a.empty()) {
      test.stop();
      return;
    }

    // 等到拉取 fn 真正进入 DNS 等待，确认在途 IO 状态可见。
    CASE_EXPECT_TRUE(pump_until(test, [&test]() { return test.dns().calls("lru-fetch-gate.local") >= 1; }));
    CASE_EXPECT_TRUE(caches.is_io_task_running("k1"));

    auto task_b = test.run_task(
        "lru_fetch_b", std::chrono::seconds{10}, [&caches, &ptr_b](rpc::context &ctx) -> rpc::result_code_type {
          int32_t res = RPC_AWAIT_CODE_RESULT(caches.await_fetch(
              ctx, "k1", ptr_b,
              [](rpc::context &, const std::string &, counted_object &, int64_t *) -> rpc::result_code_type {
                // 合并路径不应执行 fn。
                CASE_EXPECT_FALSE(true);
                RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_SYS_UNKNOWN);
              }));
          RPC_RETURN_CODE(res);
        });
    if (task_b.empty()) {
      test.stop();
      return;
    }

    expect_task_done(test.wait(task_a, std::chrono::seconds{20}), 0);
    expect_task_done(test.wait(task_b, std::chrono::seconds{20}), 0);
  }

  CASE_EXPECT_EQ(1, fetch_calls);
  CASE_EXPECT_TRUE(nullptr != ptr_a && ptr_a == ptr_b);
  CASE_EXPECT_EQ(11, ptr_a->data_object.payload);
  CASE_EXPECT_FALSE(caches.is_io_task_running("k1"));
  CASE_EXPECT_TRUE(task_type_trait::empty(ptr_a->io_task));
  CASE_EXPECT_EQ(1, static_cast<int>(caches.size()));

  // 排队等待路径不留下任务句柄引用：全部任务结束后对象可释放。
  caches.remove_cache("k1");
  drain_pool(caches);
  ptr_a.reset();
  ptr_b.reset();
  pump_until(test, []() { return 0 == counted_object::live_instances; });
  CASE_EXPECT_EQ(0, counted_object::live_instances);

  test.stop();
}

// 并发保存排队：在途保存期间其他任务的保存排在后面；序号推进、数据版本推进、句柄复位。
CASE_TEST(server_frame_unit_test, rpc_lru_cache_concurrent_save_queueing) {
  CASE_EXPECT_EQ(0, counted_object::live_instances);
  atfw::testing::runtime test;
  if (0 != start_test_runtime(test)) {
    return;
  }

  register_delayed_gate(test, "lru-save-gate.local", 3);

  test_map_type caches;
  int fetch_calls = 0;
  int save_calls = 0;
  test_cache_ptr ptr;
  {
    auto seed = test.run_task(
        "lru_save_seed", std::chrono::seconds{5},
        [&caches, &fetch_calls, &ptr](rpc::context &ctx) -> rpc::result_code_type {
          int32_t res = RPC_AWAIT_CODE_RESULT(caches.await_fetch(ctx, "k1", ptr, make_immediate_fetch_fn(fetch_calls)));
          RPC_RETURN_CODE(res);
        });
    if (seed.empty()) {
      test.stop();
      return;
    }
    expect_task_done(test.wait(seed, std::chrono::seconds{10}), 0);
    CASE_EXPECT_TRUE(nullptr != ptr);
    CASE_EXPECT_FALSE(caches.is_io_task_running("k1"));

    // 第一个保存：fn 内经 DNS 门控挂起，制造在途保存窗口。
    auto task_c = test.run_task("lru_save_c", std::chrono::seconds{10},
                                [&caches, &save_calls, ptr](rpc::context &ctx) -> rpc::result_code_type {
                                  test_cache_ptr save_ptr = ptr;
                                  save_ptr->data_object.payload = 100;
                                  int32_t res = RPC_AWAIT_CODE_RESULT(caches.await_save(
                                      ctx, save_ptr,
                                      [&save_calls](rpc::context &save_ctx, const counted_object &,
                                                    int64_t *out_version) -> rpc::result_code_type {
                                        ++save_calls;
                                        int32_t gate_res =
                                            RPC_AWAIT_CODE_RESULT(await_dns_gate(save_ctx, "lru-save-gate.local"));
                                        if (gate_res < 0) {
                                          RPC_RETURN_CODE(gate_res);
                                        }
                                        if (nullptr != out_version) {
                                          *out_version = 2;
                                        }
                                        RPC_RETURN_CODE(0);
                                      }));
                                  RPC_RETURN_CODE(res);
                                });
    if (task_c.empty()) {
      test.stop();
      return;
    }

    // 等到第一个保存 fn 进入 DNS 等待：保存 IO 在途。
    CASE_EXPECT_TRUE(pump_until(test, [&test]() { return test.dns().calls("lru-save-gate.local") >= 1; }));
    CASE_EXPECT_TRUE(caches.is_io_task_running("k1"));

    // 第二个保存：排队等待第一个保存完成后执行自己的保存。
    auto task_d = test.run_task(
        "lru_save_d", std::chrono::seconds{10},
        [&caches, &save_calls, ptr](rpc::context &ctx) -> rpc::result_code_type {
          test_cache_ptr save_ptr = ptr;
          save_ptr->data_object.payload = 200;
          int32_t res = RPC_AWAIT_CODE_RESULT(caches.await_save(
              ctx, save_ptr,
              [&save_calls](rpc::context &, const counted_object &in, int64_t *out_version) -> rpc::result_code_type {
                ++save_calls;
                // 共享同一份数据对象：第二个任务的修改对保存同样可见。
                CASE_EXPECT_EQ(200, in.payload);
                if (nullptr != out_version) {
                  *out_version = 3;
                }
                RPC_RETURN_CODE(0);
              }));
          RPC_RETURN_CODE(res);
        });
    if (task_d.empty()) {
      test.stop();
      return;
    }

    expect_task_done(test.wait(task_c, std::chrono::seconds{20}), 0);
    expect_task_done(test.wait(task_d, std::chrono::seconds{20}), 0);
  }

  CASE_EXPECT_EQ(1, fetch_calls);
  CASE_EXPECT_EQ(2, save_calls);
  CASE_EXPECT_EQ(200, ptr->data_object.payload);
  CASE_EXPECT_EQ(3, static_cast<int>(ptr->data_version));
  CASE_EXPECT_EQ(2, static_cast<int>(ptr->saved_sequence));
  CASE_EXPECT_TRUE(ptr->saving_sequence >= ptr->saved_sequence);
  CASE_EXPECT_FALSE(caches.is_io_task_running("k1"));
  CASE_EXPECT_TRUE(task_type_trait::empty(ptr->io_task));

  caches.remove_cache("k1");
  drain_pool(caches);
  ptr.reset();
  pump_until(test, []() { return 0 == counted_object::live_instances; });
  CASE_EXPECT_EQ(0, counted_object::live_instances);

  test.stop();
}

// await_save 失败：当前条目被移除，下次重新拉取。
CASE_TEST(server_frame_unit_test, rpc_lru_cache_save_failure_evicts_current_entry) {
  CASE_EXPECT_EQ(0, counted_object::live_instances);
  atfw::testing::runtime test;
  if (0 != start_test_runtime(test)) {
    return;
  }

  test_map_type caches;
  int fetch_calls = 0;
  int32_t save_res = 0;
  {
    auto seed = test.run_task(
        "lru_save_fail_seed", std::chrono::seconds{5},
        [&caches, &fetch_calls](rpc::context &ctx) -> rpc::result_code_type {
          test_cache_ptr out;
          int32_t res = RPC_AWAIT_CODE_RESULT(caches.await_fetch(ctx, "k1", out, make_immediate_fetch_fn(fetch_calls)));
          RPC_RETURN_CODE(res);
        });
    if (seed.empty()) {
      test.stop();
      return;
    }
    expect_task_done(test.wait(seed, std::chrono::seconds{10}), 0);
    CASE_EXPECT_EQ(1, static_cast<int>(caches.size()));

    auto task = test.run_task(
        "lru_save_fail", std::chrono::seconds{5}, [&caches, &save_res](rpc::context &ctx) -> rpc::result_code_type {
          test_cache_ptr ptr = caches.get_cache("k1");
          CASE_EXPECT_TRUE(nullptr != ptr);
          ptr->data_object.payload = 55;
          save_res = RPC_AWAIT_CODE_RESULT(caches.await_save(
              ctx, ptr, [](rpc::context &, const counted_object &, int64_t *) -> rpc::result_code_type {
                RPC_RETURN_CODE(PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION);
              }));
          RPC_RETURN_CODE(0);
        });
    if (task.empty()) {
      test.stop();
      return;
    }
    expect_task_done(test.wait(task, std::chrono::seconds{10}), 0);
  }

  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_OLD_VERSION, save_res);
  CASE_EXPECT_TRUE(nullptr == caches.get_cache("k1"));
  CASE_EXPECT_FALSE(caches.is_io_task_running("k1"));

  // 移除后重新拉取成功。
  {
    auto task = test.run_task(
        "lru_save_fail_refetch", std::chrono::seconds{5},
        [&caches, &fetch_calls](rpc::context &ctx) -> rpc::result_code_type {
          test_cache_ptr out;
          int32_t res = RPC_AWAIT_CODE_RESULT(caches.await_fetch(ctx, "k1", out, make_immediate_fetch_fn(fetch_calls)));
          RPC_RETURN_CODE(res);
        });
    if (task.empty()) {
      test.stop();
      return;
    }
    expect_task_done(test.wait(task, std::chrono::seconds{10}), 0);
  }
  CASE_EXPECT_EQ(2, fetch_calls);
  CASE_EXPECT_EQ(1, static_cast<int>(caches.size()));

  test.stop();
}

// remove_cache 之后的防复活语义：旧句柄 await_save 被拒绝、set_cache 不再入池、参数校验。
CASE_TEST(server_frame_unit_test, rpc_lru_cache_removed_rejects_save_and_reinsert) {
  CASE_EXPECT_EQ(0, counted_object::live_instances);
  atfw::testing::runtime test;
  if (0 != start_test_runtime(test)) {
    return;
  }

  test_map_type caches;
  int fetch_calls = 0;
  int save_calls = 0;
  test_cache_ptr stale_ptr;
  int32_t stale_save_res = 0;
  int32_t null_ptr_res = 0;
  int32_t empty_fn_res = 0;
  {
    auto seed = test.run_task("lru_removed_seed", std::chrono::seconds{5},
                              [&caches, &fetch_calls, &stale_ptr](rpc::context &ctx) -> rpc::result_code_type {
                                int32_t res = RPC_AWAIT_CODE_RESULT(
                                    caches.await_fetch(ctx, "k1", stale_ptr, make_immediate_fetch_fn(fetch_calls)));
                                RPC_RETURN_CODE(res);
                              });
    if (seed.empty()) {
      test.stop();
      return;
    }
    expect_task_done(test.wait(seed, std::chrono::seconds{10}), 0);
    CASE_EXPECT_TRUE(nullptr != stale_ptr);
  }

  // 显式移除：条目被真正从池中删除，旧句柄 removed 置位且不可再入池。
  CASE_EXPECT_TRUE(caches.remove_cache("k1"));
  CASE_EXPECT_TRUE(stale_ptr->removed);
  CASE_EXPECT_EQ(0, static_cast<int>(caches.size()));
  caches.set_cache(stale_ptr);
  CASE_EXPECT_EQ(0, static_cast<int>(caches.size()));
  CASE_EXPECT_TRUE(nullptr == caches.get_cache("k1"));

  {
    auto task = test.run_task(
        "lru_removed_checks", std::chrono::seconds{5},
        [&caches, &fetch_calls, &save_calls, &stale_ptr, &stale_save_res, &null_ptr_res,
         &empty_fn_res](rpc::context &ctx) -> rpc::result_code_type {
          // 旧句柄保存被拒绝且不触发 fn。
          stale_save_res = RPC_AWAIT_CODE_RESULT(caches.await_save(
              ctx, stale_ptr,
              [&save_calls](rpc::context &, const counted_object &, int64_t *) -> rpc::result_code_type {
                ++save_calls;
                RPC_RETURN_CODE(0);
              }));
          CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND, stale_save_res);

          // 参数校验：空指针句柄。
          test_cache_ptr null_ptr;
          null_ptr_res = RPC_AWAIT_CODE_RESULT(caches.await_save(
              ctx, null_ptr,
              [](rpc::context &, const counted_object &, int64_t *) -> rpc::result_code_type { RPC_RETURN_CODE(0); }));
          CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM, null_ptr_res);

          // 重新拉取：删除后池中无可复用对象，创建全新缓存对象（旧句柄保持置毒）。
          test_cache_ptr fresh;
          int32_t res =
              RPC_AWAIT_CODE_RESULT(caches.await_fetch(ctx, "k1", fresh, make_immediate_fetch_fn(fetch_calls)));
          CASE_EXPECT_EQ(0, res);
          CASE_EXPECT_TRUE(nullptr != fresh && fresh != stale_ptr);
          CASE_EXPECT_FALSE(fresh->removed);
          CASE_EXPECT_TRUE(stale_ptr->removed);
          empty_fn_res = RPC_AWAIT_CODE_RESULT(caches.await_save(ctx, fresh, nullptr));
          CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM, empty_fn_res);
          RPC_RETURN_CODE(0);
        });
    if (task.empty()) {
      test.stop();
      return;
    }
    expect_task_done(test.wait(task, std::chrono::seconds{10}), 0);
  }

  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND, stale_save_res);
  CASE_EXPECT_EQ(0, save_calls);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM, null_ptr_res);
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_PARAM, empty_fn_res);
  CASE_EXPECT_EQ(2, fetch_calls);
  CASE_EXPECT_EQ(1, static_cast<int>(caches.size()));
  CASE_EXPECT_TRUE(nullptr != caches.get_cache("k1") && caches.get_cache("k1") != stale_ptr);
  CASE_EXPECT_TRUE(stale_ptr->removed);

  stale_ptr.reset();
  caches.remove_cache("k1");
  drain_pool(caches);
  pump_until(test, []() { return 0 == counted_object::live_instances; });
  CASE_EXPECT_EQ(0, counted_object::live_instances);

  test.stop();
}

// clear：真正清空全部条目（size 归零），旧句柄仍被置 removed、拒绝写回与重新入池；
// 之后对同一 key 的 await_fetch 创建新对象而不是复用旧对象。
CASE_TEST(server_frame_unit_test, rpc_lru_cache_clear_erases_all_entries) {
  CASE_EXPECT_EQ(0, counted_object::live_instances);
  atfw::testing::runtime test;
  if (0 != start_test_runtime(test)) {
    return;
  }

  test_map_type caches;
  int fetch_calls = 0;
  int save_calls = 0;
  test_cache_ptr ptr_a;
  test_cache_ptr ptr_b;
  test_cache_ptr refetch_ptr;
  int32_t stale_save_res = 0;
  int32_t refetch_res = -1;

  // 空池 clear 是空操作。
  CASE_EXPECT_EQ(0, static_cast<int>(caches.clear()));
  CASE_EXPECT_TRUE(caches.empty());

  {
    auto seed = test.run_task(
        "lru_clear_seed", std::chrono::seconds{5},
        [&caches, &fetch_calls, &ptr_a, &ptr_b](rpc::context &ctx) -> rpc::result_code_type {
          int32_t res =
              RPC_AWAIT_CODE_RESULT(caches.await_fetch(ctx, "k1", ptr_a, make_immediate_fetch_fn(fetch_calls)));
          CASE_EXPECT_EQ(0, res);
          res = RPC_AWAIT_CODE_RESULT(caches.await_fetch(ctx, "k2", ptr_b, make_immediate_fetch_fn(fetch_calls)));
          CASE_EXPECT_EQ(0, res);
          RPC_RETURN_CODE(0);
        });
    if (seed.empty()) {
      test.stop();
      return;
    }
    expect_task_done(test.wait(seed, std::chrono::seconds{10}), 0);
  }
  CASE_EXPECT_TRUE(nullptr != ptr_a && nullptr != ptr_b);
  CASE_EXPECT_EQ(2, static_cast<int>(caches.size()));

  // remove_cache 直接删除 k2：旧句柄置毒，池内不留已移除条目。
  CASE_EXPECT_TRUE(caches.remove_cache("k2"));
  CASE_EXPECT_EQ(1, static_cast<int>(caches.size()));
  CASE_EXPECT_TRUE(ptr_b->removed);

  // clear 返回被标记 removed 的活跃条目数（仅 k1），并把全部条目真正移出池。
  CASE_EXPECT_EQ(1, static_cast<int>(caches.clear()));
  CASE_EXPECT_TRUE(caches.empty());
  CASE_EXPECT_EQ(0, static_cast<int>(caches.size()));
  CASE_EXPECT_TRUE(ptr_a->removed);
  CASE_EXPECT_TRUE(ptr_b->removed);
  CASE_EXPECT_TRUE(nullptr == caches.get_cache("k1"));
  CASE_EXPECT_TRUE(nullptr == caches.get_cache("k2"));

  // 旧句柄不可重新入池。
  caches.set_cache(ptr_a);
  caches.set_cache(ptr_b);
  CASE_EXPECT_TRUE(caches.empty());

  // 对已清空的池重复 clear 幂等。
  CASE_EXPECT_EQ(0, static_cast<int>(caches.clear()));

  {
    auto task = test.run_task(
        "lru_clear_checks", std::chrono::seconds{5},
        [&caches, &fetch_calls, &save_calls, &ptr_a, &refetch_ptr, &stale_save_res,
         &refetch_res](rpc::context &ctx) -> rpc::result_code_type {
          // 旧句柄保存被拒绝且不触发 fn。
          stale_save_res = RPC_AWAIT_CODE_RESULT(caches.await_save(
              ctx, ptr_a, [&save_calls](rpc::context &, const counted_object &, int64_t *) -> rpc::result_code_type {
                ++save_calls;
                RPC_RETURN_CODE(0);
              }));
          CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND, stale_save_res);

          // 重新拉取：池中没有已移除可复用，创建新缓存对象。
          refetch_res =
              RPC_AWAIT_CODE_RESULT(caches.await_fetch(ctx, "k1", refetch_ptr, make_immediate_fetch_fn(fetch_calls)));
          CASE_EXPECT_EQ(0, refetch_res);
          CASE_EXPECT_TRUE(nullptr != refetch_ptr && refetch_ptr != ptr_a);
          CASE_EXPECT_FALSE(refetch_ptr->removed);
          RPC_RETURN_CODE(0);
        });
    if (task.empty()) {
      test.stop();
      return;
    }
    expect_task_done(test.wait(task, std::chrono::seconds{10}), 0);
  }

  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND, stale_save_res);
  CASE_EXPECT_EQ(0, save_calls);
  CASE_EXPECT_EQ(0, refetch_res);
  CASE_EXPECT_EQ(3, fetch_calls);
  CASE_EXPECT_EQ(1, static_cast<int>(caches.size()));
  CASE_EXPECT_TRUE(caches.get_cache("k1") == refetch_ptr);

  ptr_a.reset();
  ptr_b.reset();
  refetch_ptr.reset();
  caches.remove_cache("k1");
  drain_pool(caches);
  pump_until(test, []() { return 0 == counted_object::live_instances; });
  CASE_EXPECT_EQ(0, counted_object::live_instances);

  test.stop();
}

// await_io_task：排空在途拉取/保存而不发起新读写；key 不存在时立即返回 0。
CASE_TEST(server_frame_unit_test, rpc_lru_cache_await_io_task_drains) {
  CASE_EXPECT_EQ(0, counted_object::live_instances);
  atfw::testing::runtime test;
  if (0 != start_test_runtime(test)) {
    return;
  }

  register_delayed_gate(test, "lru-io-gate.local", 3);

  test_map_type caches;
  int fetch_calls = 0;
  test_cache_ptr ptr_a;
  bool drained = false;
  int32_t drain_missing_res = -1;
  {
    auto task_a = test.run_task("lru_io_a", std::chrono::seconds{10},
                                [&caches, &fetch_calls, &ptr_a](rpc::context &ctx) -> rpc::result_code_type {
                                  int32_t res = RPC_AWAIT_CODE_RESULT(caches.await_fetch(
                                      ctx, "k1", ptr_a,
                                      [&fetch_calls](rpc::context &gate_ctx, const std::string &,
                                                     counted_object &val_out, int64_t *) -> rpc::result_code_type {
                                        ++fetch_calls;
                                        int32_t gate_res =
                                            RPC_AWAIT_CODE_RESULT(await_dns_gate(gate_ctx, "lru-io-gate.local"));
                                        if (gate_res < 0) {
                                          RPC_RETURN_CODE(gate_res);
                                        }
                                        val_out.payload = 21;
                                        RPC_RETURN_CODE(0);
                                      }));
                                  RPC_RETURN_CODE(res);
                                });
    if (task_a.empty()) {
      test.stop();
      return;
    }

    CASE_EXPECT_TRUE(pump_until(test, [&test]() { return test.dns().calls("lru-io-gate.local") >= 1; }));
    CASE_EXPECT_TRUE(caches.is_io_task_running("k1"));

    auto task_b = test.run_task("lru_io_b", std::chrono::seconds{10},
                                [&caches, &drained](rpc::context &ctx) -> rpc::result_code_type {
                                  int32_t res = RPC_AWAIT_CODE_RESULT(caches.await_io_task(ctx, "k1"));
                                  drained = true;
                                  RPC_RETURN_CODE(res);
                                });
    if (task_b.empty()) {
      test.stop();
      return;
    }

    // 排空任务必须等到在途 IO 完成后才返回。
    expect_task_done(test.wait(task_b, std::chrono::seconds{20}), 0);
    CASE_EXPECT_TRUE(drained);
    expect_task_done(test.wait(task_a, std::chrono::seconds{20}), 0);

    auto task_c = test.run_task("lru_io_c", std::chrono::seconds{5},
                                [&caches, &drain_missing_res](rpc::context &ctx) -> rpc::result_code_type {
                                  drain_missing_res = RPC_AWAIT_CODE_RESULT(caches.await_io_task(ctx, "missing"));
                                  RPC_RETURN_CODE(drain_missing_res);
                                });
    if (task_c.empty()) {
      test.stop();
      return;
    }
    expect_task_done(test.wait(task_c, std::chrono::seconds{10}), 0);
  }

  CASE_EXPECT_EQ(1, fetch_calls);
  CASE_EXPECT_TRUE(nullptr != ptr_a);
  CASE_EXPECT_EQ(21, ptr_a->data_object.payload);
  CASE_EXPECT_EQ(0, drain_missing_res);
  CASE_EXPECT_FALSE(caches.is_io_task_running("k1"));
  CASE_EXPECT_TRUE(task_type_trait::empty(ptr_a->io_task));

  caches.remove_cache("k1");
  drain_pool(caches);
  ptr_a.reset();
  pump_until(test, []() { return 0 == counted_object::live_instances; });
  CASE_EXPECT_EQ(0, counted_object::live_instances);

  test.stop();
}

// 拉取失败只清除属于自己的条目：拉取期间被淘汰且被其他任务重新拉取成功的新缓存不能被误删。
CASE_TEST(server_frame_unit_test, rpc_lru_cache_fetch_failure_keeps_newer_entry) {
  CASE_EXPECT_EQ(0, counted_object::live_instances);
  atfw::testing::runtime test;
  if (0 != start_test_runtime(test)) {
    return;
  }

  register_delayed_gate(test, "lru-fail-gate.local", 4);
  register_delayed_gate(test, "lru-refetch-gate.local", 1);

  test_map_type caches;
  int fail_calls = 0;
  int refetch_calls = 0;
  test_cache_ptr stale_ptr;
  test_cache_ptr fresh_ptr;
  int32_t fail_res = 0;
  {
    // 任务 A：拉取 k1，fn 内经长延迟 DNS 门控挂起，随后失败。
    auto task_a = test.run_task(
        "lru_fail_a", std::chrono::seconds{10},
        [&caches, &fail_calls, &stale_ptr, &fail_res](rpc::context &ctx) -> rpc::result_code_type {
          fail_res = RPC_AWAIT_CODE_RESULT(caches.await_fetch(
              ctx, "k1", stale_ptr,
              [&fail_calls](rpc::context &gate_ctx, const std::string &, counted_object &,
                            int64_t *) -> rpc::result_code_type {
                ++fail_calls;
                int32_t gate_res = RPC_AWAIT_CODE_RESULT(await_dns_gate(gate_ctx, "lru-fail-gate.local"));
                RPC_RETURN_CODE(gate_res != 0 ? gate_res : PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND);
              }));
          RPC_RETURN_CODE(0);
        });
    if (task_a.empty()) {
      test.stop();
      return;
    }
    CASE_EXPECT_TRUE(pump_until(test, [&test]() { return test.dns().calls("lru-fail-gate.local") >= 1; }));

    // 拉取在途时条目被淘汰，另一个任务重新拉取并成功。
    caches.pop_front();
    CASE_EXPECT_TRUE(nullptr == caches.get_cache("k1"));
    auto task_b = test.run_task("lru_refetch_b", std::chrono::seconds{10},
                                [&caches, &refetch_calls, &fresh_ptr](rpc::context &ctx) -> rpc::result_code_type {
                                  int32_t res = RPC_AWAIT_CODE_RESULT(caches.await_fetch(
                                      ctx, "k1", fresh_ptr,
                                      [&refetch_calls](rpc::context &gate_ctx, const std::string &,
                                                       counted_object &val_out, int64_t *) -> rpc::result_code_type {
                                        ++refetch_calls;
                                        int32_t gate_res =
                                            RPC_AWAIT_CODE_RESULT(await_dns_gate(gate_ctx, "lru-refetch-gate.local"));
                                        if (gate_res < 0) {
                                          RPC_RETURN_CODE(gate_res);
                                        }
                                        val_out.payload = 33;
                                        RPC_RETURN_CODE(0);
                                      }));
                                  RPC_RETURN_CODE(res);
                                });
    if (task_b.empty()) {
      test.stop();
      return;
    }
    CASE_EXPECT_TRUE(pump_until(test, [&test]() { return test.dns().calls("lru-refetch-gate.local") >= 1; }));

    // 短延迟的重拉先完成；随后长延迟的失败拉取返回。
    expect_task_done(test.wait(task_b, std::chrono::seconds{20}), 0);
    CASE_EXPECT_TRUE(nullptr != fresh_ptr);
    CASE_EXPECT_EQ(33, fresh_ptr->data_object.payload);

    expect_task_done(test.wait(task_a, std::chrono::seconds{20}), 0);
  }

  // A 的失败不得删除 B 已成功的新缓存。
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_DB_RECORD_NOT_FOUND, fail_res);
  CASE_EXPECT_EQ(1, fail_calls);
  CASE_EXPECT_EQ(1, refetch_calls);
  CASE_EXPECT_TRUE(caches.get_cache("k1") == fresh_ptr);
  CASE_EXPECT_EQ(33, fresh_ptr->data_object.payload);
  CASE_EXPECT_FALSE(fresh_ptr->removed);

  fresh_ptr.reset();
  stale_ptr.reset();
  caches.remove_cache("k1");
  drain_pool(caches);
  pump_until(test, []() { return 0 == counted_object::live_instances; });
  CASE_EXPECT_EQ(0, counted_object::live_instances);

  test.stop();
}

// LRU 淘汰不是删除：淘汰后的重新拉取允许重建缓存（removed 不置位）。
CASE_TEST(server_frame_unit_test, rpc_lru_cache_eviction_allows_refetch_reinsert) {
  CASE_EXPECT_EQ(0, counted_object::live_instances);
  atfw::testing::runtime test;
  if (0 != start_test_runtime(test)) {
    return;
  }

  test_map_type caches;
  int fetch_calls = 0;
  test_cache_ptr old_ptr;
  test_cache_ptr new_ptr;
  {
    auto seed = test.run_task("lru_evict_seed", std::chrono::seconds{5},
                              [&caches, &fetch_calls, &old_ptr](rpc::context &ctx) -> rpc::result_code_type {
                                int32_t res = RPC_AWAIT_CODE_RESULT(
                                    caches.await_fetch(ctx, "k1", old_ptr, make_immediate_fetch_fn(fetch_calls)));
                                RPC_RETURN_CODE(res);
                              });
    if (seed.empty()) {
      test.stop();
      return;
    }
    expect_task_done(test.wait(seed, std::chrono::seconds{10}), 0);
    CASE_EXPECT_TRUE(nullptr != old_ptr);

    // 淘汰：不置 removed。
    caches.pop_front();
    CASE_EXPECT_TRUE(nullptr == caches.get_cache("k1"));
    CASE_EXPECT_FALSE(old_ptr->removed);

    auto task = test.run_task("lru_evict_refetch", std::chrono::seconds{5},
                              [&caches, &fetch_calls, &new_ptr](rpc::context &ctx) -> rpc::result_code_type {
                                int32_t res = RPC_AWAIT_CODE_RESULT(
                                    caches.await_fetch(ctx, "k1", new_ptr, make_immediate_fetch_fn(fetch_calls)));
                                RPC_RETURN_CODE(res);
                              });
    if (task.empty()) {
      test.stop();
      return;
    }
    expect_task_done(test.wait(task, std::chrono::seconds{10}), 0);
  }

  CASE_EXPECT_EQ(2, fetch_calls);
  CASE_EXPECT_TRUE(nullptr != new_ptr && new_ptr != old_ptr);
  CASE_EXPECT_EQ(1, static_cast<int>(caches.size()));
  CASE_EXPECT_TRUE(caches.get_cache("k1") == new_ptr);

  old_ptr.reset();
  new_ptr.reset();
  caches.remove_cache("k1");
  drain_pool(caches);
  pump_until(test, []() { return 0 == counted_object::live_instances; });
  CASE_EXPECT_EQ(0, counted_object::live_instances);

  test.stop();
}

// 先删除又获取：remove_cache 把条目真正移出池，对同一 key 的 await_fetch 创建全新对象（不复用旧对象）；
// 旧句柄的 removed 置毒是永久的，即使记录被重新拉取，旧句柄的 await_save 依旧被拒绝。
CASE_TEST(server_frame_unit_test, rpc_lru_cache_remove_then_fetch_creates_new_object) {
  CASE_EXPECT_EQ(0, counted_object::live_instances);
  atfw::testing::runtime test;
  if (0 != start_test_runtime(test)) {
    return;
  }

  test_map_type caches;
  int fetch_calls = 0;
  int save_calls = 0;
  test_cache_ptr ptr;
  test_cache_ptr fresh_ptr;
  int32_t poisoned_save_res = 0;
  int32_t poisoned_after_refetch_res = 0;
  {
    auto seed = test.run_task(
        "lru_recreate_seed", std::chrono::seconds{5},
        [&caches, &fetch_calls, &ptr](rpc::context &ctx) -> rpc::result_code_type {
          int32_t res = RPC_AWAIT_CODE_RESULT(caches.await_fetch(ctx, "k1", ptr, make_immediate_fetch_fn(fetch_calls)));
          RPC_RETURN_CODE(res);
        });
    if (seed.empty()) {
      test.stop();
      return;
    }
    expect_task_done(test.wait(seed, std::chrono::seconds{10}), 0);
    CASE_EXPECT_TRUE(nullptr != ptr);
    CASE_EXPECT_EQ(1, ptr->data_object.payload);
  }

  // 删除：条目被真正移出池，旧句柄保存被拒绝。
  CASE_EXPECT_TRUE(caches.remove_cache("k1"));
  CASE_EXPECT_TRUE(ptr->removed);
  CASE_EXPECT_TRUE(nullptr == caches.get_cache("k1"));
  CASE_EXPECT_EQ(0, static_cast<int>(caches.size()));
  {
    auto poisoned = test.run_task(
        "lru_recreate_poisoned_save", std::chrono::seconds{5},
        [&caches, &save_calls, &ptr, &poisoned_save_res](rpc::context &ctx) -> rpc::result_code_type {
          poisoned_save_res = RPC_AWAIT_CODE_RESULT(caches.await_save(
              ctx, ptr, [&save_calls](rpc::context &, const counted_object &, int64_t *) -> rpc::result_code_type {
                ++save_calls;
                RPC_RETURN_CODE(0);
              }));
          RPC_RETURN_CODE(0);
        });
    if (poisoned.empty()) {
      test.stop();
      return;
    }
    expect_task_done(test.wait(poisoned, std::chrono::seconds{10}), 0);
  }
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND, poisoned_save_res);
  CASE_EXPECT_EQ(0, save_calls);

  // 又获取：await_fetch 创建全新对象（不是复用旧对象），旧句柄保持置毒。
  {
    auto task = test.run_task("lru_recreate_fetch", std::chrono::seconds{5},
                              [&caches, &fetch_calls, &ptr, &fresh_ptr](rpc::context &ctx) -> rpc::result_code_type {
                                int32_t res = RPC_AWAIT_CODE_RESULT(caches.await_fetch(
                                    ctx, "k1", fresh_ptr,
                                    [&fetch_calls](rpc::context &, const std::string &, counted_object &val_out,
                                                   int64_t *out_version) -> rpc::result_code_type {
                                      ++fetch_calls;
                                      val_out.payload = 77;
                                      if (nullptr != out_version) {
                                        *out_version = 9;
                                      }
                                      RPC_RETURN_CODE(0);
                                    }));
                                CASE_EXPECT_EQ(0, res);
                                CASE_EXPECT_TRUE(nullptr != fresh_ptr && fresh_ptr != ptr);
                                CASE_EXPECT_FALSE(fresh_ptr->removed);
                                CASE_EXPECT_TRUE(ptr->removed);
                                RPC_RETURN_CODE(res);
                              });
    if (task.empty()) {
      test.stop();
      return;
    }
    expect_task_done(test.wait(task, std::chrono::seconds{10}), 0);
  }
  CASE_EXPECT_EQ(2, fetch_calls);
  CASE_EXPECT_EQ(1, static_cast<int>(caches.size()));
  CASE_EXPECT_TRUE(caches.get_cache("k1") == fresh_ptr);
  CASE_EXPECT_EQ(77, fresh_ptr->data_object.payload);
  CASE_EXPECT_EQ(9, static_cast<int>(fresh_ptr->data_version));

  // 重新拉取后旧句柄依旧被拒绝保存（置毒永久）；新句柄可正常保存。
  {
    auto task = test.run_task(
        "lru_recreate_save", std::chrono::seconds{5},
        [&caches, &save_calls, &ptr, &fresh_ptr, &poisoned_after_refetch_res](rpc::context &ctx) -> rpc::result_code_type {
          poisoned_after_refetch_res = RPC_AWAIT_CODE_RESULT(caches.await_save(
              ctx, ptr, [](rpc::context &, const counted_object &, int64_t *) -> rpc::result_code_type {
                RPC_RETURN_CODE(0);
              }));
          CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND, poisoned_after_refetch_res);

          test_cache_ptr save_ptr = fresh_ptr;
          int32_t res = RPC_AWAIT_CODE_RESULT(caches.await_save(
              ctx, save_ptr,
              [&save_calls](rpc::context &, const counted_object &in, int64_t *out_version) -> rpc::result_code_type {
                ++save_calls;
                CASE_EXPECT_EQ(77, in.payload);
                if (nullptr != out_version) {
                  *out_version = 10;
                }
                RPC_RETURN_CODE(0);
              }));
          RPC_RETURN_CODE(res);
        });
    if (task.empty()) {
      test.stop();
      return;
    }
    expect_task_done(test.wait(task, std::chrono::seconds{10}), 0);
  }
  CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::err::EN_SYS_NOTFOUND, poisoned_after_refetch_res);
  CASE_EXPECT_EQ(1, save_calls);
  CASE_EXPECT_EQ(10, static_cast<int>(fresh_ptr->data_version));

  // 重建后的条目可再次删除。
  CASE_EXPECT_TRUE(caches.remove_cache("k1"));
  CASE_EXPECT_TRUE(fresh_ptr->removed);

  ptr.reset();
  fresh_ptr.reset();
  drain_pool(caches);
  pump_until(test, []() { return 0 == counted_object::live_instances; });
  CASE_EXPECT_EQ(0, counted_object::live_instances);

  test.stop();
}

// 拉取在途时删除：remove_cache 置毒并真正移出池，后续 await_fetch 不再等待/复用在途对象，
// 直接创建新对象重新拉取；在途拉取完成后不得复活或覆盖新缓存。
CASE_TEST(server_frame_unit_test, rpc_lru_cache_remove_during_fetch_then_refetch_creates_new_object) {
  CASE_EXPECT_EQ(0, counted_object::live_instances);
  atfw::testing::runtime test;
  if (0 != start_test_runtime(test)) {
    return;
  }

  register_delayed_gate(test, "lru-recreate-gate.local", 4);

  test_map_type caches;
  int fetch_calls = 0;
  test_cache_ptr ptr_a;
  test_cache_ptr ptr_b;
  {
    auto task_a = test.run_task("lru_recreate_io_a", std::chrono::seconds{10},
                                [&caches, &fetch_calls, &ptr_a](rpc::context &ctx) -> rpc::result_code_type {
                                  int32_t res = RPC_AWAIT_CODE_RESULT(caches.await_fetch(
                                      ctx, "k1", ptr_a,
                                      [&fetch_calls](rpc::context &gate_ctx, const std::string &,
                                                     counted_object &val_out, int64_t *) -> rpc::result_code_type {
                                        ++fetch_calls;
                                        int32_t gate_res = RPC_AWAIT_CODE_RESULT(
                                            await_dns_gate(gate_ctx, "lru-recreate-gate.local"));
                                        if (gate_res < 0) {
                                          RPC_RETURN_CODE(gate_res);
                                        }
                                        val_out.payload = 11;
                                        RPC_RETURN_CODE(0);
                                      }));
                                  RPC_RETURN_CODE(res);
                                });
    if (task_a.empty()) {
      test.stop();
      return;
    }
    CASE_EXPECT_TRUE(pump_until(test, [&test]() { return test.dns().calls("lru-recreate-gate.local") >= 1; }));
    CASE_EXPECT_TRUE(caches.is_io_task_running("k1"));

    // 拉取在途时删除：条目被真正移出池，在途句柄被置毒。
    CASE_EXPECT_TRUE(caches.remove_cache("k1"));
    CASE_EXPECT_TRUE(nullptr == caches.get_cache("k1"));
    CASE_EXPECT_EQ(0, static_cast<int>(caches.size()));
    CASE_EXPECT_TRUE(nullptr != ptr_a && ptr_a->removed);
    CASE_EXPECT_FALSE(caches.is_io_task_running("k1"));

    // 又获取：池中无可复用对象，直接成为拉取任务并创建新对象（不等待在途 IO）。
    auto task_b = test.run_task("lru_recreate_io_b", std::chrono::seconds{10},
                                [&caches, &fetch_calls, &ptr_b](rpc::context &ctx) -> rpc::result_code_type {
                                  int32_t res = RPC_AWAIT_CODE_RESULT(caches.await_fetch(
                                      ctx, "k1", ptr_b,
                                      [&fetch_calls](rpc::context &, const std::string &, counted_object &val_out,
                                                     int64_t *out_version) -> rpc::result_code_type {
                                        ++fetch_calls;
                                        val_out.payload = 99;
                                        if (nullptr != out_version) {
                                          *out_version = 5;
                                        }
                                        RPC_RETURN_CODE(0);
                                      }));
                                  RPC_RETURN_CODE(res);
                                });
    if (task_b.empty()) {
      test.stop();
      return;
    }

    expect_task_done(test.wait(task_a, std::chrono::seconds{20}), 0);
    expect_task_done(test.wait(task_b, std::chrono::seconds{20}), 0);
  }

  // 在途拉取（A）正常完成自己的对象但保持置毒、不入池；新拉取（B）的对象占据缓存。
  CASE_EXPECT_EQ(2, fetch_calls);
  CASE_EXPECT_TRUE(nullptr != ptr_a && nullptr != ptr_b && ptr_a != ptr_b);
  CASE_EXPECT_TRUE(ptr_a->removed);
  CASE_EXPECT_EQ(11, ptr_a->data_object.payload);
  CASE_EXPECT_FALSE(ptr_b->removed);
  CASE_EXPECT_EQ(99, ptr_b->data_object.payload);
  CASE_EXPECT_EQ(5, static_cast<int>(ptr_b->data_version));
  CASE_EXPECT_TRUE(caches.get_cache("k1") == ptr_b);
  CASE_EXPECT_FALSE(caches.is_io_task_running("k1"));
  CASE_EXPECT_TRUE(task_type_trait::empty(ptr_a->io_task));
  CASE_EXPECT_TRUE(task_type_trait::empty(ptr_b->io_task));
  CASE_EXPECT_EQ(1, static_cast<int>(caches.size()));

  ptr_a.reset();
  ptr_b.reset();
  caches.remove_cache("k1");
  drain_pool(caches);
  pump_until(test, []() { return 0 == counted_object::live_instances; });
  CASE_EXPECT_EQ(0, counted_object::live_instances);

  test.stop();
}
