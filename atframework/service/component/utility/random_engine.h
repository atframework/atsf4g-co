//
// Created by owt50 on 2016/9/29.
//

#ifndef _ATFRAME_SERVICE_COMPONENT_RANDOM_ENGINE_H
#define _ATFRAME_SERVICE_COMPONENT_RANDOM_ENGINE_H

#pragma once

#include <random/random_generator.h>

namespace util {
    class random_engine {
    private:
        random_engine();
        ~random_engine();

        static ::util::random::mt19937_64& _get_common_generator();
        static ::util::random::taus88& _get_fast_generator();
    public:
        /**
         * 使用指定种子初始化随机数生成器
         * @param [out] rnd 要初始化的生成器
         * @param [in] seed 随机数种子
         */
        template<typename RandomType>
        static void init_generator_with_seed(RandomType& rnd, typename RandomType::result_type seed) {
            rnd.init_seed(seed);
        }

        /**
         * 使用随机种子初始化随机数生成器
         * @param [out] rnd 要初始化的生成器
         */
        template<typename RandomType>
        static void init_generator(RandomType& rnd) {
            init_generator_with_seed(rnd, static_cast<typename RandomType::result_type>(random()));
        }

        /**
         * 标准随机数
         * @return 随机数
         */
        static uint32_t random();

        /**
         * 标准随机区间
         * @param [in] lowest 下限
         * @param [in] highest 上限
         * @return 在[lowest, highest) 之间的随机数
         */
        template<typename ResType>
        static ResType random_between(ResType lowest, ResType highest) {
            return _get_common_generator().random_between<ResType>(lowest, highest);
        }

        /**
         * 快速随机数
         * @return 随机数
         */
        static uint32_t fast_random();

        /**
         * 快速随机区间
         * @param [in] lowest 下限
         * @param [in] highest 上限
         * @return 在[lowest, highest) 之间的随机数
         */
        template<typename ResType>
        static ResType fast_random_between(ResType lowest, ResType highest) {
            return _get_fast_generator().random_between<ResType>(lowest, highest);
        }
    };
}


#endif // _ATFRAME_SERVICE_COMPONENT_RANDOM_ENGINE_H
