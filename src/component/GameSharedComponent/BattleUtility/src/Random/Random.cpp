#include "Random/Random.h"

#include <config/compiler_features.h>

#include <algorithm>
#include <mutex>

PROJECT_NAMESPACE_BEGIN

namespace battle_utility {
namespace random {

BATTLE_UTILITY_API RandomGenerator &GetRandomGenerator() {
  static std::unique_ptr<RandomGenerator> ret;
  if ATFW_UTIL_LIKELY_CONDITION (ret) {
    return *ret;
  }

  ret = std::unique_ptr<RandomGenerator>(new RandomGenerator());
  ret->init_seed(static_cast<RandomGenerator::result_type>(time(NULL)));
  return *ret;
}

} // namespace random
} // namespace battle_utility

PROJECT_NAMESPACE_END
