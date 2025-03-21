// Copyright 2024 atframework
// Created by owent

#include "logic/hpa/logic_hpa_observer.h"

#include <opentelemetry/common/key_value_iterable_view.h>

#include <utility>

#include "logic/hpa/logic_hpa_data_type.h"

namespace {
class ATFW_UTIL_SYMBOL_LOCAL logic_hpa_policy_observer_key_value_view final
    : public opentelemetry::common::KeyValueIterable {
 public:
  explicit logic_hpa_policy_observer_key_value_view(
      const std::unordered_map<std::string, opentelemetry::common::AttributeValue>& shared,
      const std::unordered_map<std::string, std::string>& priv,
      gsl::span<std::pair<gsl::string_view, opentelemetry::common::AttributeValue>> temporary_attributes) noexcept
      : shared_{&shared}, privated_{&priv}, temporary_{temporary_attributes} {
    cached_size_ = privated_->size();

    for (auto& kv : *shared_) {
      if (privated_->end() == privated_->find(kv.first)) {
        ++cached_size_;
      }
    }

    cached_size_ += temporary_attributes.size();
  }

  // KeyValueIterable
  bool ForEachKeyValue(
      opentelemetry::nostd::function_ref<bool(opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue)>
          callback) const noexcept override {
    for (auto& kv : *privated_) {
      if (!callback(kv.first, kv.second)) {
        return false;
      }
    }

    for (auto& kv : *shared_) {
      if (privated_->end() == privated_->find(kv.first)) {
        if (!callback(kv.first, kv.second)) {
          return false;
        }
      }
    }

    for (auto& kv : temporary_) {
      if (!callback(opentelemetry::nostd::string_view{kv.first.data(), kv.first.size()}, kv.second)) {
        return false;
      }
    }

    return true;
  }

  size_t size() const noexcept override { return cached_size_; }

 private:
  size_t cached_size_;
  const std::unordered_map<std::string, opentelemetry::common::AttributeValue>* shared_;
  const std::unordered_map<std::string, std::string>* privated_;
  gsl::span<std::pair<gsl::string_view, opentelemetry::common::AttributeValue>> temporary_;
};

using otel_observer_result_int64 = opentelemetry::metrics::ObserverResultT<int64_t>;
using otel_observer_result_double = opentelemetry::metrics::ObserverResultT<double>;
}  // namespace

logic_hpa_observer::logic_hpa_observer(
    opentelemetry::metrics::ObserverResult& result,
    const std::unordered_map<std::string, opentelemetry::common::AttributeValue>& shared,
    const std::unordered_map<std::string, std::string>& priv)
    : has_data_{false},
      ref_result_{std::ref(result)},
      ref_shared_attributes_{std::ref(shared)},
      ref_priv_attributes_{std::ref(priv)} {}

void logic_hpa_observer::observe(
    int64_t value, gsl::span<std::pair<gsl::string_view, opentelemetry::common::AttributeValue>> attributes) {
  if (logic_hpa_observable_value::is_nan(value)) {
    return;
  }

  if (opentelemetry::nostd::holds_alternative<opentelemetry::nostd::shared_ptr<otel_observer_result_int64>>(
          ref_result_.get())) {
    auto type_result =
        opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<otel_observer_result_int64>>(ref_result_.get());
    if (type_result) {
      type_result->Observe(value, logic_hpa_policy_observer_key_value_view{ref_shared_attributes_.get(),
                                                                           ref_priv_attributes_.get(), attributes});
      has_data_ = true;
    }
  } else if (opentelemetry::nostd::holds_alternative<opentelemetry::nostd::shared_ptr<otel_observer_result_double>>(
                 ref_result_.get())) {
    auto type_result =
        opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<otel_observer_result_double>>(ref_result_.get());
    if (type_result) {
      type_result->Observe(static_cast<double>(value),
                           logic_hpa_policy_observer_key_value_view{ref_shared_attributes_.get(),
                                                                    ref_priv_attributes_.get(), attributes});
      has_data_ = true;
    }
  }
}

void logic_hpa_observer::observe(
    double value, gsl::span<std::pair<gsl::string_view, opentelemetry::common::AttributeValue>> attributes) {
  if (logic_hpa_observable_value::is_nan(value)) {
    return;
  }

  if (opentelemetry::nostd::holds_alternative<opentelemetry::nostd::shared_ptr<otel_observer_result_int64>>(
          ref_result_.get())) {
    auto type_result =
        opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<otel_observer_result_int64>>(ref_result_.get());
    if (type_result) {
      type_result->Observe(static_cast<int64_t>(value + std::numeric_limits<float>::epsilon()),
                           logic_hpa_policy_observer_key_value_view{ref_shared_attributes_.get(),
                                                                    ref_priv_attributes_.get(), attributes});
      has_data_ = true;
    }
  } else if (opentelemetry::nostd::holds_alternative<opentelemetry::nostd::shared_ptr<otel_observer_result_double>>(
                 ref_result_.get())) {
    auto type_result =
        opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<otel_observer_result_double>>(ref_result_.get());
    if (type_result) {
      type_result->Observe(value, logic_hpa_policy_observer_key_value_view{ref_shared_attributes_.get(),
                                                                           ref_priv_attributes_.get(), attributes});
      has_data_ = true;
    }
  }
}
