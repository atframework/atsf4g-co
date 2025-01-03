// Copyright 2024 atframework
// Created by owent

#pragma once

#include <opentelemetry/metrics/observer_result.h>

#include <gsl/select-gsl.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>

class logic_hpa_observer {
 public:
  logic_hpa_observer(opentelemetry::metrics::ObserverResult& result,
                     const std::unordered_map<std::string, opentelemetry::common::AttributeValue>& shared,
                     const std::unordered_map<std::string, std::string>& priv);

  /**
   * @brief 上报观测到的值（int64类型, 兼容性封装）
   *
   * @param value 指标值
   * @param attributes 附加属性集合 (Please ensure no conflict with shared labels and private labels from policy)
   * @note To minimize unnecessary CPU waste, deduplication and semantic conversation are no longer performed here.
   *       Please ensure that the additional labels in the attributes passed in meet the requirements of Prometheus.
   *       The label key must use only uppercase or lowercase letters, digits, and underscores([a-zA-Z_][a-zA-Z0-9_]*)
   * @see https://prometheus.io/docs/concepts/data_model/
   */
  void observe(int64_t value,
               gsl::span<std::pair<gsl::string_view, opentelemetry::common::AttributeValue>> attributes = {});

  /**
   * @brief 上报观测到的值（int64类型, 兼容性封装）
   *
   * @param value 指标值
   * @param attributes 附加属性集合 (Please ensure no conflict with shared labels and private labels from policy)
   * @note To minimize unnecessary CPU waste, deduplication and semantic conversation are no longer performed here.
   *       Please ensure that the additional labels in the attributes passed in meet the requirements of Prometheus.
   *       The label key must use only uppercase or lowercase letters, digits, and underscores([a-zA-Z_][a-zA-Z0-9_]*)
   * @see https://prometheus.io/docs/concepts/data_model/
   */
  void observe(double value,
               gsl::span<std::pair<gsl::string_view, opentelemetry::common::AttributeValue>> attributes = {});

  inline bool has_record() const noexcept { return has_data_; }

 private:
  bool has_data_;
  std::reference_wrapper<opentelemetry::metrics::ObserverResult> ref_result_;
  std::reference_wrapper<const std::unordered_map<std::string, opentelemetry::common::AttributeValue>>
      ref_shared_attributes_;
  std::reference_wrapper<const std::unordered_map<std::string, std::string>> ref_priv_attributes_;
};
