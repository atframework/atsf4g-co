#pragma once

#include <BattleUtility/BattleUtilityConfig.h>

#include <random/random_generator.h>

PROJECT_NAMESPACE_BEGIN

namespace battle_utility {
namespace random {

using RandomGenerator = util::random::xoshiro256_starstar;
BATTLE_UTILITY_API RandomGenerator &GetRandomGenerator();

} // namespace random
} // namespace battle_utility

PROJECT_NAMESPACE_END