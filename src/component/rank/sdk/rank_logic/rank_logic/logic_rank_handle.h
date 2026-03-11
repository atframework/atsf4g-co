#pragma once

#include <config/compile_optimize.h>

#include <gsl/select-gsl.h>

#include <stdint.h>
#include <cstddef>
#include <string>
#include <type_traits>
#include <vector>

#include <common/string_oprs.h>

#ifdef __cpp_impl_three_way_comparison
#  include <compare>
#endif

#include "config/server_frame_build_feature.h"
#include "rpc/rpc_utils.h"


namespace PROJECT_NAMESPACE_ID {
class DRankImageData;
class DRankUserKey;
class DRankInstanceKey;
namespace config {
class ExcelRankRule;
}  // namespace config
}  // namespace PROJECT_NAMESPACE_ID


struct rank_ret_t {
  int32_t api_result;     // rpc系统的返回码

  rank_ret_t() noexcept : api_result(0) {}
  explicit rank_ret_t(int32_t api) noexcept : api_result(api) {}
  explicit rank_ret_t(task_type_trait::task_status status) noexcept : api_result(0) {}
};



struct rank_callback_private_data {
  int64_t submit_timepoint;
  uint64_t openid;
  unsigned char _[48];  // 预留大小
};


struct logic_rank_user_extend_data {
  // 参与排序字段，最大不超过5项
  uint32_t sort_fields[5];
  // 对齐到8
  unsigned char __padding[4];

  // 额外扩展字段，最大不超过3项
  uint64_t ext_fields[3];
};

static_assert(64 == sizeof(rank_callback_private_data), "64 != sizeof(rank_callback_private_data)");
#pragma pack(pop)

struct logic_rank_user_extend_span {
  // 参与排序字段，最大不超过5项
  gsl::span<const uint32_t> sort_fields;
  // 额外扩展字段，最大不超过3项
  gsl::span<const uint64_t> ext_fields;

  inline logic_rank_user_extend_span() : sort_fields({}), ext_fields({}) {}
};


RANK_LOGIC_SDK_API std::string rank_user_key_to_openid(uint32_t user_zone_id, uint64_t user_id, int32_t instance_type, int64_t instance_id);

RANK_LOGIC_SDK_API std::string rank_user_key_to_openid(uint32_t user_zone_id, uint64_t user_id, PROJECT_NAMESPACE_ID::DRankInstanceKey instance_key);

RANK_LOGIC_SDK_API std::tuple<uint32_t, uint64_t, int32_t, int64_t> rank_openid_to_user_key(gsl::string_view openid);

struct UTIL_SYMBOL_VISIBLE logic_rank_handle_data {
  std::string open_id;
  uint64_t user_id;
  uint32_t zone_id;
  int32_t instance_type;
  int64_t instance_id;
  uint32_t rank_no;
  uint32_t score;
  int64_t timestamp;

  logic_rank_user_extend_data extend_data;

    inline logic_rank_handle_data() : user_id(0), zone_id(0), instance_type(0), instance_id(0), rank_no(0), score(0), timestamp(0) {
    /* Ensure default initialization without relying on trivial traits. */
    extend_data = logic_rank_user_extend_data{};
    }
};

class logic_rank_handle_key {
 public:
  RANK_LOGIC_SDK_API explicit logic_rank_handle_key(const PROJECT_NAMESPACE_ID::config::ExcelRankRule& rule);
  RANK_LOGIC_SDK_API explicit logic_rank_handle_key(const PROJECT_NAMESPACE_ID::config::ExcelRankRule& rule,
                                              uint32_t auto_reward_rank_type);
  RANK_LOGIC_SDK_API explicit logic_rank_handle_key(uint32_t rank_type, uint32_t instance_id, uint32_t sub_rank_type,
                                              uint32_t sub_instance_id);

  RANK_LOGIC_SDK_API inline logic_rank_handle_key(const logic_rank_handle_key&) = default;
  RANK_LOGIC_SDK_API inline logic_rank_handle_key(logic_rank_handle_key&&) = default;
  RANK_LOGIC_SDK_API inline logic_rank_handle_key& operator=(const logic_rank_handle_key&) = default;
  RANK_LOGIC_SDK_API inline logic_rank_handle_key& operator=(logic_rank_handle_key&&) = default;

  UTIL_FORCEINLINE void set_rank_type(uint32_t v) noexcept { rank_type_ = v; }
  UTIL_FORCEINLINE void set_instance_id(uint32_t v) noexcept { instance_id_ = v; }
  UTIL_FORCEINLINE void set_sub_rank_type(uint32_t v) noexcept { sub_rank_type_ = v; }
  UTIL_FORCEINLINE void set_sub_instance_id(uint32_t v) noexcept { sub_instance_id_ = v; }

  UTIL_FORCEINLINE uint32_t get_rank_type() const noexcept { return rank_type_; }
  UTIL_FORCEINLINE uint32_t get_instance_id() const noexcept { return instance_id_; }
  UTIL_FORCEINLINE uint32_t get_sub_rank_type() const noexcept { return sub_rank_type_; }
  UTIL_FORCEINLINE uint32_t get_sub_instance_id() const noexcept { return sub_instance_id_; }

 private:
  uint32_t rank_type_;
  uint32_t instance_id_;
  uint32_t sub_rank_type_;
  uint32_t sub_instance_id_;
};

RANK_LOGIC_SDK_API bool operator==(const logic_rank_handle_key& l, const logic_rank_handle_key& r) noexcept;
#if defined(__cpp_impl_three_way_comparison) && !defined(_MSC_VER)
RANK_LOGIC_SDK_API std::strong_ordering operator<=>(const logic_rank_handle_key& l, const logic_rank_handle_key& r) noexcept;
#else
RANK_LOGIC_SDK_API bool operator!=(const logic_rank_handle_key& l, const logic_rank_handle_key& r) noexcept;
RANK_LOGIC_SDK_API bool operator<(const logic_rank_handle_key& l, const logic_rank_handle_key& r) noexcept;
RANK_LOGIC_SDK_API bool operator<=(const logic_rank_handle_key& l, const logic_rank_handle_key& r) noexcept;
RANK_LOGIC_SDK_API bool operator>(const logic_rank_handle_key& l, const logic_rank_handle_key& r) noexcept;
RANK_LOGIC_SDK_API bool operator>=(const logic_rank_handle_key& l, const logic_rank_handle_key& r) noexcept;
#endif

struct logic_rank_handle_key_hash {
  RANK_LOGIC_SDK_API inline logic_rank_handle_key_hash() = default;
  RANK_LOGIC_SDK_API inline logic_rank_handle_key_hash(const logic_rank_handle_key_hash&) = default;
  RANK_LOGIC_SDK_API inline logic_rank_handle_key_hash(logic_rank_handle_key_hash&&) = default;
  RANK_LOGIC_SDK_API inline logic_rank_handle_key_hash& operator=(const logic_rank_handle_key_hash&) = default;
  RANK_LOGIC_SDK_API inline logic_rank_handle_key_hash& operator=(logic_rank_handle_key_hash&&) = default;

  RANK_LOGIC_SDK_API size_t operator()(const logic_rank_handle_key& key) const noexcept;
};

class logic_rank_handle_decl {
 private:
  logic_rank_handle_decl(const logic_rank_handle_decl&) = delete;
  logic_rank_handle_decl(logic_rank_handle_decl&&) = delete;
  logic_rank_handle_decl& operator=(const logic_rank_handle_decl&) = delete;
  logic_rank_handle_decl& operator=(logic_rank_handle_decl&&) = delete;

 public:
  RANK_LOGIC_SDK_API logic_rank_handle_decl();
  RANK_LOGIC_SDK_API virtual ~logic_rank_handle_decl();

  RANK_LOGIC_SDK_API void reset_cursor_front() noexcept;

  RANK_LOGIC_SDK_API void reset_cursor_back() noexcept;

  RANK_LOGIC_SDK_API bool next_cursor() noexcept;

  RANK_LOGIC_SDK_API bool previous_cursor() noexcept;

  RANK_LOGIC_SDK_API bool valid_cursor() const noexcept;

  RANK_LOGIC_SDK_API gsl::string_view get_current_open_id() const noexcept;

  RANK_LOGIC_SDK_API uint32_t get_current_user_zone_id() const noexcept;

  RANK_LOGIC_SDK_API uint32_t get_current_score() const noexcept;

  RANK_LOGIC_SDK_API uint32_t get_current_no() const noexcept;

  virtual uint32_t get_world_id() const noexcept = 0;
  virtual uint32_t get_zone_id() const noexcept = 0;

  virtual gsl::span<const uint32_t> get_current_sort_fields() const noexcept = 0;

  virtual gsl::span<const uint64_t> get_current_ext_fields() const noexcept = 0;

  RANK_LOGIC_SDK_API int64_t get_current_timestamp() const noexcept;

  RANK_LOGIC_SDK_API uint32_t get_current_total_count() const noexcept;

  RANK_LOGIC_SDK_API uint32_t get_current_count() const noexcept;

  RANK_LOGIC_SDK_API const logic_rank_handle_data* get_current_cursor() const noexcept;

  RANK_LOGIC_SDK_API gsl::span<logic_rank_handle_data> get_current_span() noexcept;

  RANK_LOGIC_SDK_API gsl::span<const logic_rank_handle_data> get_current_span() const noexcept;

  void fetch_current_rank_key(gsl::string_view& openid, uint32_t zone_id, PROJECT_NAMESPACE_ID::DRankUserKey& rank_user_key) const;

  virtual bool is_service_available() const noexcept = 0;

  virtual bool is_current(const PROJECT_NAMESPACE_ID::config::ExcelRankRule& cfg, time_t now,
                          const logic_rank_handle_key& key) const noexcept = 0;

  virtual rpc::rpc_result<rank_ret_t> get_top_rank(
      rpc::context& ctx, const logic_rank_handle_key& key, uint32_t start, uint32_t count,
      PROJECT_NAMESPACE_ID::DRankImageData* image = nullptr) = 0;

  virtual rpc::rpc_result<rank_ret_t> upload_score(
      rpc::context& ctx, const logic_rank_handle_key& key, gsl::string_view openid, uint32_t score,
      const rank_callback_private_data& callback_data,
      logic_rank_user_extend_span user_extend_data = {}) = 0;

  virtual rpc::rpc_result<rank_ret_t> clear_all(rpc::context& ctx, const logic_rank_handle_key& key,
                                                             PROJECT_NAMESPACE_ID::DRankImageData* image = nullptr) = 0;

  virtual rpc::rpc_result<rank_ret_t> clear_special_one(
      rpc::context& ctx, const logic_rank_handle_key& key, gsl::string_view openid,
      PROJECT_NAMESPACE_ID::DRankImageData* image = nullptr) = 0;

  virtual rpc::rpc_result<rank_ret_t> increase_score(
      rpc::context&, const logic_rank_handle_key& key, gsl::string_view openid, uint32_t score,
      const rank_callback_private_data& callback_data,
      logic_rank_user_extend_span user_extend_data = {}) = 0;

  virtual rpc::rpc_result<rank_ret_t> decrease_score(
      rpc::context&, const logic_rank_handle_key& key, gsl::string_view openid, uint32_t score,
      const rank_callback_private_data& callback_data,
      logic_rank_user_extend_span user_extend_data = {}) = 0;

  virtual rpc::rpc_result<rank_ret_t> get_special_one(
      rpc::context&, logic_rank_handle_data& output, const logic_rank_handle_key& key, gsl::string_view openid,
      uint32_t up_count = 0, uint32_t down_count = 0, PROJECT_NAMESPACE_ID::DRankImageData* image = nullptr) = 0;
  virtual rpc::rpc_result<rank_ret_t> get_special_score(
      rpc::context&, logic_rank_handle_data& output, const logic_rank_handle_key& key, uint32_t score,
      uint32_t up_count = 0, uint32_t down_count = 0, PROJECT_NAMESPACE_ID::DRankImageData* image = nullptr) = 0;

 protected:
  void reinit_last_cache(uint32_t total_count);

 protected:
  logic_rank_handle_data* last_rank_cache_cursor_;
  uint32_t last_rank_cache_total_count_;
  std::vector<logic_rank_handle_data> last_rank_cache_;
};


class logic_rank_handle_self_impl : public logic_rank_handle_decl {
 public:
  RANK_LOGIC_SDK_API logic_rank_handle_self_impl(uint32_t world_id, uint32_t zone_id);
  RANK_LOGIC_SDK_API virtual ~logic_rank_handle_self_impl();

  RANK_LOGIC_SDK_API uint32_t get_world_id() const noexcept override;
  RANK_LOGIC_SDK_API uint32_t get_zone_id() const noexcept override;

  RANK_LOGIC_SDK_API gsl::span<const uint32_t> get_current_sort_fields() const noexcept override;

  RANK_LOGIC_SDK_API gsl::span<const uint64_t> get_current_ext_fields() const noexcept override;

  RANK_LOGIC_SDK_API bool is_service_available() const noexcept override;

  RANK_LOGIC_SDK_API bool is_current(const PROJECT_NAMESPACE_ID::config::ExcelRankRule& cfg, time_t now,
                               const logic_rank_handle_key& key) const noexcept override;

  EXPLICIT_NODISCARD_ATTR RANK_LOGIC_SDK_API rpc::rpc_result<rank_ret_t> get_top_rank(
      rpc::context& ctx, const logic_rank_handle_key& key, uint32_t start, uint32_t count,
      PROJECT_NAMESPACE_ID::DRankImageData* image = nullptr) override;

  EXPLICIT_NODISCARD_ATTR RANK_LOGIC_SDK_API rpc::rpc_result<rank_ret_t> upload_score(
      rpc::context& ctx, const logic_rank_handle_key& key, gsl::string_view openid, uint32_t score,
      const rank_callback_private_data& callback_data,
      logic_rank_user_extend_span user_extend_data = {}) override;

  EXPLICIT_NODISCARD_ATTR RANK_LOGIC_SDK_API rpc::rpc_result<rank_ret_t> clear_all(
      rpc::context& ctx, const logic_rank_handle_key& key,
      PROJECT_NAMESPACE_ID::DRankImageData* image = nullptr) override;

  EXPLICIT_NODISCARD_ATTR RANK_LOGIC_SDK_API rpc::rpc_result<rank_ret_t> clear_special_one(
      rpc::context& ctx, const logic_rank_handle_key& key, gsl::string_view openid,
      PROJECT_NAMESPACE_ID::DRankImageData* image = nullptr) override;

  EXPLICIT_NODISCARD_ATTR RANK_LOGIC_SDK_API rpc::rpc_result<rank_ret_t> increase_score(
      rpc::context&, const logic_rank_handle_key& key, gsl::string_view openid, uint32_t score,
      const rank_callback_private_data& callback_data,
      logic_rank_user_extend_span user_extend_data = {}) override;

  EXPLICIT_NODISCARD_ATTR RANK_LOGIC_SDK_API rpc::rpc_result<rank_ret_t> decrease_score(
      rpc::context&, const logic_rank_handle_key& key, gsl::string_view openid, uint32_t score,
      const rank_callback_private_data& callback_data,
      logic_rank_user_extend_span user_extend_data = {}) override;

  EXPLICIT_NODISCARD_ATTR RANK_LOGIC_SDK_API rpc::rpc_result<rank_ret_t> get_special_one(
      rpc::context&, logic_rank_handle_data& output, const logic_rank_handle_key& key, gsl::string_view openid,
      uint32_t up_count = 0, uint32_t down_count = 0, PROJECT_NAMESPACE_ID::DRankImageData* image = nullptr) override;

  EXPLICIT_NODISCARD_ATTR RANK_LOGIC_SDK_API rpc::rpc_result<rank_ret_t> get_special_score(
      rpc::context&, logic_rank_handle_data& output, const logic_rank_handle_key& key, uint32_t score,
      uint32_t up_count = 0, uint32_t down_count = 0, PROJECT_NAMESPACE_ID::DRankImageData* image = nullptr) override;

 private:
  uint32_t world_id_;
  uint32_t zone_id_;
};

class logic_rank_handle_variant {
 public:
  enum variant_type { KSelfRank = 0 };

 public:
  RANK_LOGIC_SDK_API logic_rank_handle_variant(uint32_t world_id, uint32_t zone_id,
                                         const PROJECT_NAMESPACE_ID::config::ExcelRankRule& rule);

  RANK_LOGIC_SDK_API logic_rank_handle_variant(const logic_rank_handle_variant& other);
  RANK_LOGIC_SDK_API logic_rank_handle_variant& operator=(const logic_rank_handle_variant& other);

  RANK_LOGIC_SDK_API logic_rank_handle_variant(logic_rank_handle_variant&& other);
  RANK_LOGIC_SDK_API logic_rank_handle_variant& operator=(logic_rank_handle_variant&& other);

  RANK_LOGIC_SDK_API virtual ~logic_rank_handle_variant();

  void init_delegate(uint32_t world_id, uint32_t zone_id);
  void destructor_delegate();

  RANK_LOGIC_SDK_API uint32_t get_world_id() const noexcept;
  RANK_LOGIC_SDK_API uint32_t get_zone_id() const noexcept;

  RANK_LOGIC_SDK_API gsl::span<const uint32_t> get_current_sort_fields() const noexcept;

  RANK_LOGIC_SDK_API gsl::span<const uint64_t> get_current_ext_fields() const noexcept;

  RANK_LOGIC_SDK_API bool is_service_available() const noexcept;

  RANK_LOGIC_SDK_API bool is_current(const PROJECT_NAMESPACE_ID::config::ExcelRankRule& cfg, time_t now,
                               const logic_rank_handle_key& key) const noexcept;

  EXPLICIT_NODISCARD_ATTR RANK_LOGIC_SDK_API rpc::rpc_result<rank_ret_t> get_top_rank(
      rpc::context& ctx, const logic_rank_handle_key& key, uint32_t start, uint32_t count,
      PROJECT_NAMESPACE_ID::DRankImageData* image = nullptr);

  EXPLICIT_NODISCARD_ATTR RANK_LOGIC_SDK_API rpc::rpc_result<rank_ret_t> upload_score(
      rpc::context& ctx, const logic_rank_handle_key& key, gsl::string_view openid, uint32_t score,
      const rank_callback_private_data& callback_data, logic_rank_user_extend_span user_extend_data = {});

  EXPLICIT_NODISCARD_ATTR RANK_LOGIC_SDK_API rpc::rpc_result<rank_ret_t> clear_all(
      rpc::context& ctx, const logic_rank_handle_key& key, PROJECT_NAMESPACE_ID::DRankImageData* image = nullptr);

  EXPLICIT_NODISCARD_ATTR RANK_LOGIC_SDK_API rpc::rpc_result<rank_ret_t> clear_special_one(
      rpc::context& ctx, const logic_rank_handle_key& key, gsl::string_view openid);

  EXPLICIT_NODISCARD_ATTR RANK_LOGIC_SDK_API rpc::rpc_result<rank_ret_t> increase_score(
      rpc::context&, const logic_rank_handle_key& key, gsl::string_view openid, uint32_t score,
      const rank_callback_private_data& callback_data, logic_rank_user_extend_span user_extend_data = {});

  EXPLICIT_NODISCARD_ATTR RANK_LOGIC_SDK_API rpc::rpc_result<rank_ret_t> decrease_score(
      rpc::context&, const logic_rank_handle_key& key, gsl::string_view openid, uint32_t score,
      const rank_callback_private_data& callback_data, logic_rank_user_extend_span user_extend_data = {});

  EXPLICIT_NODISCARD_ATTR RANK_LOGIC_SDK_API rpc::rpc_result<rank_ret_t> get_special_one(
      rpc::context&, logic_rank_handle_data& output, const logic_rank_handle_key& key, gsl::string_view openid,
      uint32_t up_count = 0, uint32_t down_count = 0, PROJECT_NAMESPACE_ID::DRankImageData* image = nullptr);

  //   EXPLICIT_NODISCARD_ATTR RANK_LOGIC_SDK_API rpc::rpc_result<rank_ret_t> get_special_score(
  //       rpc::context&, logic_rank_handle_data& output, const logic_rank_handle_key& key, uint32_t score,
  //       uint32_t up_count = 0, uint32_t down_count = 0, PROJECT_NAMESPACE_ID::DRankImageData* image = nullptr);

  UTIL_FORCEINLINE variant_type get_type() const noexcept { return variant_type_; }

  UTIL_FORCEINLINE logic_rank_handle_decl& get_handle() noexcept { return *delegate_; }
  UTIL_FORCEINLINE const logic_rank_handle_decl& get_handle() const noexcept { return *delegate_; }

  UTIL_FORCEINLINE void reset_cursor_front() noexcept { delegate_->reset_cursor_front(); }

  UTIL_FORCEINLINE void reset_cursor_back() noexcept { delegate_->reset_cursor_back(); }

  UTIL_FORCEINLINE bool next_cursor() noexcept { return delegate_->next_cursor(); }

  UTIL_FORCEINLINE bool previous_cursor() noexcept { return delegate_->previous_cursor(); }

  UTIL_FORCEINLINE bool valid_cursor() const noexcept { return delegate_->valid_cursor(); }

  UTIL_FORCEINLINE gsl::string_view get_current_open_id() const noexcept { return delegate_->get_current_open_id(); }

  UTIL_FORCEINLINE uint32_t get_current_user_zone_id() const noexcept { return delegate_->get_current_user_zone_id(); }

  UTIL_FORCEINLINE uint32_t get_current_score() const noexcept { return delegate_->get_current_score(); }

  UTIL_FORCEINLINE uint32_t get_current_no() const noexcept { return delegate_->get_current_no(); }

  UTIL_FORCEINLINE int64_t get_current_timestamp() const noexcept { return delegate_->get_current_timestamp(); }

  UTIL_FORCEINLINE uint32_t get_current_total_count() const noexcept { return delegate_->get_current_total_count(); }

  UTIL_FORCEINLINE uint32_t get_current_count() const noexcept { return delegate_->get_current_count(); }

  UTIL_FORCEINLINE const logic_rank_handle_data* get_current_cursor() const noexcept {
    return delegate_->get_current_cursor();
  }

  UTIL_FORCEINLINE gsl::span<logic_rank_handle_data> get_current_span() noexcept {
    return delegate_->get_current_span();
  }

  UTIL_FORCEINLINE gsl::span<const logic_rank_handle_data> get_current_span() const noexcept {
    return delegate_->get_current_span();
  }

 private:
  template <std::size_t S, class... L>
  struct max_storage_size_helper_s;

  template <std::size_t S, class C>
  struct max_storage_size_helper_s<S, C> {
#if __cplusplus >= 202100L
    static constexpr std::size_t _align_size = alignof(C);
    static constexpr std::size_t value = (S + _align_size - 1) & (~(_align_size - 1));
#else
    static constexpr std::size_t value = S > sizeof(typename std::aligned_storage<sizeof(C)>::type)
                                             ? S
                                             : sizeof(typename std::aligned_storage<sizeof(C)>::type);
#endif
  };

  template <std::size_t S, class C, class... L>
  struct max_storage_size_helper_s<S, C, L...> {
#if __cplusplus >= 202100L
    static constexpr std::size_t _align_size = alignof(C);
    static constexpr std::size_t value = ((sizeof(C) + _align_size - 1) & (~(_align_size - 1))) >
                                                 max_storage_size_helper_s<S, L...>::value
                                             ? ((sizeof(C) + _align_size - 1) & (~(_align_size - 1)))
                                             : max_storage_size_helper_s<S, L...>::value;
#else
    static constexpr std::size_t value = sizeof(typename std::aligned_storage<sizeof(C)>::type) >
                                                 max_storage_size_helper_s<S, L...>::value
                                             ? sizeof(typename std::aligned_storage<sizeof(C)>::type)
                                             : max_storage_size_helper_s<S, L...>::value;
#endif
  };

  template <class... L>
  struct max_storage_size_helper {
    static constexpr std::size_t value = max_storage_size_helper_s<0, L...>::value;
  };

 private:
  variant_type variant_type_;
  bool enable_image_;
  int32_t sort_type_;
  gsl::not_null<logic_rank_handle_decl*> delegate_;
  unsigned char
      object_data_[max_storage_size_helper<logic_rank_handle_self_impl>::value == 0
                       ? 1
                       : max_storage_size_helper<logic_rank_handle_self_impl>::value];
};
