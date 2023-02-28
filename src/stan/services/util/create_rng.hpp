#ifndef STAN_SERVICES_UTIL_CREATE_RNG_HPP
#define STAN_SERVICES_UTIL_CREATE_RNG_HPP

#include <boost/random/mixmax.hpp>

namespace stan {

typedef boost::random::mixmax rng_t;

namespace services {
namespace util {

/**
 * Creates a pseudo random number generator from a random seed
 * and a chain id by initializing the PRNG with the seed and
 * then advancing past pow(2, 50) times the chain ID draws to
 * ensure different chains sample from different segments of the
 * pseudo random number sequence.
 *
 * Chain IDs should be kept to larger values than one to ensure
 * that the draws used to initialized transformed data are not
 * duplicated.
 *
 * @param[in] seed the random seed
 * @param[in] chain the chain id
 * @return a stan::rng_t instance
 */
inline rng_t create_rng(unsigned int seed, unsigned int chain) {
  rng_t rng(seed + chain);
  return rng;
}

}  // namespace util
}  // namespace services
}  // namespace stan
#endif
