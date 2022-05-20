#ifndef STAN_SERVICES_PATHFINDER_MULTI_HPP
#define STAN_SERVICES_PATHFINDER_MULTI_HPP

#include <stan/callbacks/interrupt.hpp>
#include <stan/callbacks/logger.hpp>
#include <stan/callbacks/writer.hpp>
#include <stan/io/var_context.hpp>
#include <stan/optimization/bfgs.hpp>
#include <stan/optimization/lbfgs_update.hpp>
#include <stan/services/pathfinder/single.hpp>
#include <stan/services/pathfinder/psis.hpp>
#include <stan/services/error_codes.hpp>
#include <stan/services/util/initialize.hpp>
#include <stan/services/util/create_rng.hpp>
#include <tbb/parallel_for.h>
#include <tbb/concurrent_vector.h>
#include <boost/random/discrete_distribution.hpp>
#include <boost/iterator.hpp>
#include <string>
#include <vector>

namespace stan {
namespace services {
namespace pathfinder {

/**
 * Runs multiple pathfinders with final approximate samples drawn using PSIS.
 * @tparam Model A model implementation
 * @tparam InitContext Type inheriting from `stan::io::var_context`
 * @tparam InitWriter Type inheriting from `stan::io::var_context`
 * @tparam DiagnosticWriter Type inheriting from stan::callbacks::writer
 * @tparam ParamWriter Type inheriting from stan::callbacks::writer
 * @tparam SingleDiagnosticWriter Type inheriting from stan::callbacks::writer
 * @tparam SingleParamWriter Type inheriting from stan::callbacks::writer
 * @param[in] model ($log p$ in paper) Input model to test (with data already
 * instantiated)
 * @param[in] init ($\pi_0$ in paper) var context for initialization
 * @param[in] random_seed random seed for the random number generator
 * @param[in] path path id to advance the pseudo random number generator
 * @param[in] init_radius radius to initialize
 * @param[in] history_size  (J in paper) amount of history to keep for L-BFGS
 * @param[in] init_alpha line search step size for first iteration
 * @param[in] tol_obj convergence tolerance on absolute changes in
 *   objective function value
 * @param[in] tol_rel_obj ($\tau^{rel}$ in paper) convergence tolerance on
 * relative changes in objective function value
 * @param[in] tol_grad convergence tolerance on the norm of the gradient
 * @param[in] tol_rel_grad convergence tolerance on the relative norm of
 *   the gradient
 * @param[in] tol_param convergence tolerance on changes in parameter
 *   value
 * @param[in] num_iterations (L in paper) maximum number of LBFGS iterations
 * @param[in] save_iterations indicates whether all the iterations should
 *   be saved to the parameter_writer
 * @param[in] refresh how often to write output to logger
 * @param[in,out] interrupt callback to be called every iteration
 * @param[in] num_elbo_draws (K in paper) number of MC draws to evaluate ELBO
 * @param[in] num_draws (M in paper) number of approximate posterior draws to
 * return
 * @param[in] num_multi_draws The number of draws to return from PSIS sampling
 * @param[in] num_paths The number of single pathfinders to run.
 * @param[in] num_eval_attempts Number of times to attempt to calculate
 * the log probability of an MC sample while calculating the ELBO.
 * @param[in,out] logger Logger for messages
 * @param[in,out] init_writer Writer callback for unconstrained inits
 * @param[in,out] single_path_parameter_writer output for parameter values
 * @param[in,out] single_path_diagnostic_writer output for diagnostics values
 * @param[in,out] parameter_writer output for parameter values
 * @param[in,out] diagnostic_writer output for diagnostics values
 * @return error_codes::OK if successful
 */
template <class Model, typename InitContext, typename InitWriter,
          typename DiagnosticWriter, typename ParamWriter,
          typename SingleParamWriter, typename SingleDiagnosticWriter>
inline int pathfinder_lbfgs_multi(
    Model& model, InitContext&& init, unsigned int random_seed,
    unsigned int path, double init_radius, int history_size, double init_alpha,
    double tol_obj, double tol_rel_obj, double tol_grad, double tol_rel_grad,
    double tol_param, int num_iterations, bool save_iterations, int refresh,
    callbacks::interrupt& interrupt, int num_elbo_draws, int num_draws,
    int num_multi_draws, int num_eval_attempts, int num_paths,
    callbacks::logger& logger, InitWriter&& init_writers,
    std::vector<SingleParamWriter>& single_path_parameter_writer,
    std::vector<SingleDiagnosticWriter>& single_path_diagnostic_writer,
    ParamWriter& parameter_writer, DiagnosticWriter& diagnostic_writer) {
  const auto start_pathfinders_time = std::chrono::steady_clock::now();
  std::vector<std::string> param_names;
  model.constrained_param_names(param_names, true, true);
  param_names.push_back("lp_approx__");
  param_names.push_back("lp__");
  parameter_writer(param_names);
  diagnostic_writer(param_names);
  const size_t num_params = param_names.size();
  tbb::concurrent_vector<Eigen::Array<double, -1, 1>> individual_lp_ratios;
  individual_lp_ratios.reserve(num_paths);
  tbb::concurrent_vector<Eigen::Array<double, -1, -1>> individual_samples;
  individual_samples.reserve(num_paths);
  std::atomic<size_t> lp_calls{0};
  tbb::parallel_for(
      tbb::blocked_range<int>(0, num_paths), [&](tbb::blocked_range<int> r) {
        for (int iter = r.begin(); iter < r.end(); ++iter) {
          auto pathfinder_ret
              = stan::services::pathfinder::pathfinder_lbfgs_single<true>(
                  model, *(init[iter]), random_seed, path + iter, init_radius,
                  history_size, init_alpha, tol_obj, tol_rel_obj, tol_grad,
                  tol_rel_grad, tol_param, num_iterations, save_iterations,
                  refresh, interrupt, num_elbo_draws, num_draws,
                  num_eval_attempts, logger, init_writers[iter],
                  single_path_parameter_writer[iter],
                  single_path_diagnostic_writer[iter]);
          if (std::get<0>(pathfinder_ret) == error_codes::SOFTWARE) {
            logger.info(
                std::string("Pathfinder iteration: ") + std::to_string(iter)
                + " failed. [Return better error codes for nice message here]");
            return;
          }
          individual_lp_ratios.emplace_back(
              std::move(std::get<1>(std::move(pathfinder_ret))));
          individual_samples.emplace_back(
              std::move(std::get<2>(std::move(pathfinder_ret))));
          lp_calls += std::get<3>(pathfinder_ret);
        }
      });
  const auto end_pathfinders_time = std::chrono::steady_clock::now();
  const double pathfinders_delta_time
      = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_pathfinders_time - start_pathfinders_time)
            .count()
        / 1000.0;
  const auto start_psis_time = std::chrono::steady_clock::now();
  // Because of failure in lp calcs we can have multiple returned sizes
  size_t num_returned_samples = 0;
  const size_t successful_pathfinders = individual_samples.size();
  if (successful_pathfinders == 0) {
    logger.info("No pathfinders ran successfully");
    return error_codes::SOFTWARE;
  }
  if (refresh != 0) {
    logger.info("Total Evaluations: (" + std::to_string(lp_calls) + ")");
  }
  for (size_t i = 0; i < successful_pathfinders; i++) {
    num_returned_samples += individual_lp_ratios[i].size();
  }
  Eigen::Array<double, -1, 1> lp_ratios(num_returned_samples);
  Eigen::Array<double, -1, -1> samples(individual_samples[0].rows(),
                                       num_returned_samples);
  Eigen::Index filling_start_row = 0;
  for (size_t iter = 0; iter < successful_pathfinders; ++iter) {
    const Eigen::Index individ_num_samples = individual_lp_ratios[iter].size();
    lp_ratios.segment(filling_start_row, individ_num_samples)
        = individual_lp_ratios[iter];
    samples.middleCols(filling_start_row, individ_num_samples)
        = individual_samples[iter];
    filling_start_row += individ_num_samples;
  }
  const auto tail_len = std::min(0.2 * num_returned_samples,
                                 3 * std::sqrt(num_returned_samples));
  Eigen::Array<double, -1, 1> weight_vals
      = stan::services::psis::psis_weights(lp_ratios, tail_len, logger);
  boost::ecuyer1988 rng
      = util::create_rng<boost::ecuyer1988>(random_seed, path);
  boost::variate_generator<
      boost::ecuyer1988&,
      boost::random::discrete_distribution<Eigen::Index, double>>
      rand_psis_idx(rng,
                    boost::random::discrete_distribution<Eigen::Index, double>(
                        boost::iterator_range<double*>(
                            weight_vals.data(),
                            weight_vals.data() + weight_vals.size())));
  for (size_t i = 0; i <= num_multi_draws - 1; ++i) {
    parameter_writer(samples.col(rand_psis_idx()));
  }
  const auto end_psis_time = std::chrono::steady_clock::now();
  double psis_delta_time
      = std::chrono::duration_cast<std::chrono::milliseconds>(end_psis_time
                                                              - start_psis_time)
            .count()
        / 1000.0;
  parameter_writer();
  const auto time_header = std::string("Elapsed Time: ");
  std::string optim_time_str = time_header
                               + std::to_string(pathfinders_delta_time)
                               + " seconds (Pathfinders)";
  parameter_writer(optim_time_str);
  std::string psis_time_str = std::string(time_header.size(), ' ')
                              + std::to_string(psis_delta_time)
                              + " seconds (PSIS)";
  parameter_writer(psis_time_str);
  std::string total_time_str
      = std::string(time_header.size(), ' ')
        + std::to_string(pathfinders_delta_time + psis_delta_time)
        + " seconds (Total)";
  parameter_writer(total_time_str);
  parameter_writer();
  return 0;
}
}  // namespace pathfinder
}  // namespace services
}  // namespace stan
#endif
