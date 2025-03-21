// Copyright 2021 atframework
// Created by owent on 2016/9/29.
//

#pragma once

#include <config/atframe_services_build_feature.h>

#include <random/random_generator.h>

namespace atframework {
namespace util {
class random_engine {
 private:
  random_engine();
  ~random_engine();

  static ATFRAME_SERVICE_COMPONENT_MACRO_API atfw::util::random::mt19937_64 &_get_common_generator();
  static ATFRAME_SERVICE_COMPONENT_MACRO_API atfw::util::random::xoshiro256_starstar &_get_fast_generator();

 public:
  /**
   * 使用指定种子初始化随机数生成器
   * @param [out] rnd 要初始化的生成器
   * @param [in] seed 随机数种子
   */
  template <typename RandomType>
  static ATFRAME_SERVICE_COMPONENT_MACRO_API_HEAD_ONLY void init_generator_with_seed(
      RandomType &rnd, typename RandomType::result_type seed) {
    rnd.init_seed(seed);
  }

  /**
   * 使用随机种子初始化随机数生成器
   * @param [out] rnd 要初始化的生成器
   */
  template <typename RandomType>
  static ATFRAME_SERVICE_COMPONENT_MACRO_API_HEAD_ONLY void init_generator(RandomType &rnd) {
    init_generator_with_seed(rnd, static_cast<typename RandomType::result_type>(random()));
  }

  /**
   * 标准随机数
   * @return 随机数
   */
  static ATFRAME_SERVICE_COMPONENT_MACRO_API uint64_t random();

  /**
   * 标准随机区间
   * @param [in] lowest 下限
   * @param [in] highest 上限
   * @return 在[lowest, highest) 之间的随机数
   */
  template <typename ResType>
  static ATFRAME_SERVICE_COMPONENT_MACRO_API_HEAD_ONLY ResType random_between(ResType lowest, ResType highest) {
    return _get_common_generator().random_between<ResType>(lowest, highest);
  }

  /**
   * 快速随机数
   * @return 随机数
   */
  static ATFRAME_SERVICE_COMPONENT_MACRO_API uint64_t fast_random();

  /**
   * 快速随机区间
   * @param [in] lowest 下限
   * @param [in] highest 上限
   * @return 在[lowest, highest) 之间的随机数
   */
  template <typename ResType>
  static ATFRAME_SERVICE_COMPONENT_MACRO_API_HEAD_ONLY ResType fast_random_between(ResType lowest, ResType highest) {
    return _get_fast_generator().random_between<ResType>(lowest, highest);
  }
};
}  // namespace util
}  // namespace atframework
