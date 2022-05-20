#ifndef STAN_SERVICES_PATHFINDER_SINGLE_HPP
#define STAN_SERVICES_PATHFINDER_SINGLE_HPP

#include <stan/callbacks/interrupt.hpp>
#include <stan/callbacks/logger.hpp>
#include <stan/callbacks/writer.hpp>
#include <stan/io/var_context.hpp>
#include <stan/optimization/bfgs.hpp>
#include <stan/optimization/lbfgs_update.hpp>
#include <stan/services/error_codes.hpp>
#include <stan/services/util/initialize.hpp>
#include <stan/services/util/create_rng.hpp>
#include <tbb/parallel_for.h>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

// Turns on all debugging
#define STAN_DEBUG_PATH_ALL false
// prints results of lbfgs
#define STAN_DEBUG_PATH_POST_LBFGS false || STAN_DEBUG_PATH_ALL
// prints taylor approximation values each iteration
#define STAN_DEBUG_PATH_TAYLOR_APPX false || STAN_DEBUG_PATH_ALL
// prints approximate draw information each iteration
#define STAN_DEBUG_PATH_ELBO_DRAWS false || STAN_DEBUG_PATH_ALL
// prints taylor curve test info
#define STAN_DEBUG_PATH_CURVE_CHECK false || STAN_DEBUG_PATH_ALL
// prints info used for random normal generations during each iteration
#define STAN_DEBUG_PATH_RNORM_DRAWS false || STAN_DEBUG_PATH_ALL
// prints all debug info that happens each iteration
#define STAN_DEBUG_PATH_ITERS                                      \
  STAN_DEBUG_PATH_ALL || STAN_DEBUG_PATH_POST_LBFGS                \
      || STAN_DEBUG_PATH_TAYLOR_APPX || STAN_DEBUG_PATH_ELBO_DRAWS \
      || STAN_DEBUG_PATH_CURVE_CHECK || STAN_DEBUG_PATH_RNORM_DRAWS

namespace stan {
namespace services {
namespace pathfinder {
namespace internal {

/**
 * Namespace holds debug utils only used if the `STAN_DEBUG_PATH_*` flags are on
 */
namespace debug {
template <typename T0, typename T1, typename T2, typename T3>
inline void elbo_draws(T0&& logger, T1&& taylor_approx, T2&& approx_samples,
                       T3&& lp_mat, double ELBO) {
  if (STAN_DEBUG_PATH_ELBO_DRAWS) {
    Eigen::IOFormat CommaInitFmt(Eigen::StreamPrecision, Eigen::DontAlignCols,
                                 ", ", ", ", "\n", "", "", " ");
    std::stringstream debug_stream;
    debug_stream
        << "\n Rando Sums: \n"
        << approx_samples.array().square().colwise().sum().eval().format(
               CommaInitFmt)
        << "\n";
    debug_stream << "logdetcholHk: " << taylor_approx.logdetcholHk << "\n";
    debug_stream << "ELBO: " << ELBO << "\n";
    debug_stream << "repeat_draws: \n"
                 << approx_samples.transpose().eval().format(CommaInitFmt)
                 << "\n";
    debug_stream << "lp_approx: \n"
                 << lp_mat.col(1).transpose().eval().format(CommaInitFmt)
                 << "\n";
    debug_stream << "fn_call: \n"
                 << lp_mat.col(0).transpose().eval().format(CommaInitFmt)
                 << "\n";
    Eigen::MatrixXd param_vals = approx_samples;
    auto mean_vals = param_vals.rowwise().mean().eval();
    debug_stream << "Mean Values: \n"
                 << mean_vals.transpose().eval().format(CommaInitFmt) << "\n";
    debug_stream << "SD Values: \n"
                 << (((param_vals.colwise() - mean_vals)
                          .array()
                          .square()
                          .matrix()
                          .rowwise()
                          .sum()
                          .array()
                      / (param_vals.cols() - 1))
                         .sqrt())
                        .transpose()
                        .eval()
                 << "\n";
    logger.info(debug_stream);
  }
}

template <typename T0, typename T>
inline void rnorm_draws(T0&& logger, T&& approx_samples_tmp) {
  if (STAN_DEBUG_PATH_RNORM_DRAWS) {
    Eigen::IOFormat CommaInitFmt(Eigen::StreamPrecision, Eigen::DontAlignCols,
                                 ", ", ", ", "\n", "", "", " ");
    Eigen::MatrixXd param_vals = approx_samples_tmp;
    auto mean_vals = param_vals.rowwise().mean().eval();
    std::stringstream debug_stream;
    debug_stream << "Mean Values: \n"
                 << mean_vals.transpose().eval().format(CommaInitFmt) << "\n";
    debug_stream << "SD Values: \n"
                 << (((param_vals.colwise() - mean_vals)
                          .array()
                          .square()
                          .matrix()
                          .rowwise()
                          .sum()
                          .array()
                      / (param_vals.cols() - 1))
                         .sqrt())
                        .transpose()
                        .eval()
                 << "\n";
    logger.info(debug_stream);
  }
}

template <typename T0, typename T1, typename T2>
inline void curve_check(T0&& logger, T1&& Dk, T2&& thetak) {
  if (STAN_DEBUG_PATH_CURVE_CHECK) {
    std::stringstream debug_stream;
    debug_stream << "\n Check Dk: \n" << Dk.transpose() << "\n";
    debug_stream << "\n Check thetak: \n" << thetak.transpose() << "\n";
    logger.info(debug_stream);
  }
}

template <typename T0, typename T1, typename T2, typename T3, typename T4,
          typename T5, typename T6>
inline void post_lbfgs(T0&& logger, T1&& update_best_mutex, T2&& param_size,
                       T3&& num_elbo_draws, T4&& alpha_mat, T5&& Ykt_diff,
                       T6&& Skt_diff) {
  if (STAN_DEBUG_PATH_POST_LBFGS) {
    std::lock_guard<std::mutex> guard(update_best_mutex);
    Eigen::IOFormat CommaInitFmt(Eigen::StreamPrecision, 0, ", ", ", ", "\n",
                                 "", "", " ");
    std::stringstream debug_stream;
    debug_stream << "\n num_params: " << param_size << "\n";
    debug_stream << "\n num_elbo_params: " << num_elbo_draws << "\n";
    // std::cout << "\n param_cols_filled: " << param_cols_filled << "\n";
    debug_stream << "\n Alpha mat: "
                 << alpha_mat.transpose().eval().format(CommaInitFmt) << "\n";
    debug_stream << "\n Ykt_diff mat: "
                 << Ykt_diff.transpose().eval().format(CommaInitFmt) << "\n";
    debug_stream << "\n Skt_diff mat: "
                 << Skt_diff.transpose().eval().format(CommaInitFmt) << "\n";
    logger.info(debug_stream);
  }
}

template <typename T0, typename T1, typename T2, typename T3, typename T4,
          typename T5>
inline void taylor_appx_full1(T0&& logger, T1&& alpha, T2&& ninvRST, T3&& Dk,
                              T4&& point_est, T5&& grad_est) {
  if (STAN_DEBUG_PATH_TAYLOR_APPX) {
    Eigen::IOFormat CommaInitFmt(Eigen::StreamPrecision, 0, ", ", ", ", "\n",
                                 "", "", " ");
    std::stringstream debug_stream;
    debug_stream << "---Full---\n";

    debug_stream << "Alpha: \n" << alpha.format(CommaInitFmt) << "\n";
    debug_stream << "ninvRST: \n" << ninvRST.format(CommaInitFmt) << "\n";
    debug_stream << "Dk: \n" << Dk.format(CommaInitFmt) << "\n";
    debug_stream << "Point: \n" << point_est.format(CommaInitFmt) << "\n";
    debug_stream << "grad: \n" << grad_est.format(CommaInitFmt) << "\n";
    logger.info(debug_stream);
  }
}

template <typename T0, typename T1, typename T2, typename T3, typename T4>
inline void taylor_appx_full2(T0&& logger, T1&& Hk, T2&& L_hk,
                              T3&& logdetcholHk, T4&& x_center) {
  if (STAN_DEBUG_PATH_TAYLOR_APPX) {
    std::cout << "---Full---\n";
    Eigen::IOFormat CommaInitFmt(Eigen::StreamPrecision, 0, ", ", ", ", "\n",
                                 "", "", " ");

    std::stringstream debug_stream;
    debug_stream << "Hk: " << Hk.format(CommaInitFmt) << "\n";
    debug_stream << "L_approx: \n" << L_hk.format(CommaInitFmt) << "\n";
    debug_stream << "logdetcholHk: \n" << logdetcholHk << "\n";
    debug_stream << "x_center: \n" << x_center.format(CommaInitFmt) << "\n";
    logger.info(debug_stream);
  }
}

template <typename T0, typename T1>
inline void taylor_appx_sparse1(T0&& logger, T1&& Wkbart) {
  if (STAN_DEBUG_PATH_TAYLOR_APPX) {
    Eigen::IOFormat CommaInitFmt(Eigen::StreamPrecision, 0, ", ", ", ", "\n",
                                 "", "", " ");
    std::stringstream debug_stream;
    debug_stream << "---Sparse---\n";
    debug_stream << "Wkbar: \n" << Wkbart.format(CommaInitFmt) << "\n";
    logger.info(debug_stream);
  }
}

template <typename T0, typename T1, typename T2, typename T3, typename T4,
          typename T5, typename T6, typename T7, typename T8, typename T9,
          typename T10, typename T11>
inline void taylor_appx_sparse2(T0&& logger, T1&& qr, T2&& alpha, T3&& Qk,
                                T4&& L_approx, T5&& logdetcholHk, T6&& Mkbar,
                                T7&& Wkbart, T8&& x_center, T9&& ninvRST,
                                T10&& ninvRSTg, T11&& Rkbar) {
  if (STAN_DEBUG_PATH_TAYLOR_APPX) {
    Eigen::IOFormat CommaInitFmt(Eigen::StreamPrecision, 0, ", ", ", ", "\n",
                                 "", "", " ");
    std::stringstream debug_stream;
    debug_stream << "Full QR: \n" << qr.matrixQR().format(CommaInitFmt) << "\n";
    debug_stream << "Alpha: \n" << alpha.format(CommaInitFmt) << "\n";
    debug_stream << "Qk: \n" << Qk.format(CommaInitFmt) << "\n";
    debug_stream << "L_approx: \n" << L_approx.format(CommaInitFmt) << "\n";
    debug_stream << "logdetcholHk: \n" << logdetcholHk << "\n";
    debug_stream << "Mkbar: \n" << Mkbar.format(CommaInitFmt) << "\n";
    debug_stream << "Decomp Wkbar: \n" << Wkbart.format(CommaInitFmt) << "\n";
    debug_stream << "x_center: \n" << x_center.format(CommaInitFmt) << "\n";
    debug_stream << "NinvRST: " << ninvRST.format(CommaInitFmt) << "\n";
    debug_stream << "ninvRSTg: \n" << ninvRSTg.format(CommaInitFmt) << "\n";
    debug_stream << "Rkbar: " << Rkbar.format(CommaInitFmt) << "\n";
    logger.info(debug_stream);
  }
}
}  // namespace debug
template <typename T1, typename T2>
inline auto crossprod(T1&& x, T2&& y) {
  return x.transpose() * y;
}

template <typename T1>
inline Eigen::MatrixXd crossprod(T1&& x) {
  return Eigen::MatrixXd(x.cols(), x.cols())
      .setZero()
      .selfadjointView<Eigen::Lower>()
      .rankUpdate(x.adjoint());
}

template <typename T1, typename T2>
inline auto tcrossprod(T1&& x, T2&& y) {
  return x * y.transpose();
}

template <typename T1>
inline Eigen::MatrixXd tcrossprod(T1&& x) {
  return Eigen::MatrixXd(x.rows(), x.rows())
      .setZero()
      .selfadjointView<Eigen::Lower>()
      .rankUpdate(x);
}

/**
 * Perform a `Matrix * vector.asDiagonal()` multiplication with the matrix
 * represented as an `std::vector` of rows.
 * @tparam EigVec1 A type inheriting from `Eigen::DenseBase` with compile time
 *  rows or columns equal to 1
 * @tparam EigVec2 A type inheriting from `Eigen::DenseBase` with compile time
 *  rows or columns equal to 1
 * @param y_buff A standard vector with elements representing the rows of
 *  a matrix.
 * @param alpha An eigen vector representing the diagonals of a matrix.
 * @return Returns the same result as if we called `Matrix *
 * vector.asDiagonal()`
 */
template <typename EigVec1, typename EigVec2>
inline Eigen::MatrixXd std_vec_matrix_times_diagonal(
    const std::vector<EigVec1>& y_buff, const EigVec2& alpha) {
  Eigen::MatrixXd ret(y_buff.size(), alpha.size());
  for (Eigen::Index i = 0; i < y_buff.size(); ++i) {
    ret.row(i) = y_buff[i].array() * alpha.array();
  }
  return ret;
}

/**
 * Perform a `Matrix.transpose() * vector` multiplication with the matrix
 * represented as an `std::vector` of rows.
 * @tparam EigVec1 A type inheriting from `Eigen::DenseBase` with compile time
 *  rows or columns equal to 1
 * @tparam EigVec2 A type inheriting from `Eigen::DenseBase` with compile time
 *  rows or columns equal to 1
 * @param y_buff A standard vector with elements representing the rows of
 *  a matrix.
 * @param alpha An eigen vector.
 * @return Returns the same result as if we called `Matrix.transpose() * vector`
 */
template <typename EigVec1, typename EigVec2>
inline Eigen::VectorXd std_vec_matrix_crossprod_vector(
    const std::vector<EigVec1>& y_buff, const EigVec2& x) {
  Eigen::VectorXd ret(y_buff[0].size());
  ret.setZero();
  for (Eigen::Index i = 0; i < y_buff.size(); ++i) {
    ret.noalias() += y_buff[i] * x.coeff(i);
  }
  return ret;
}

/**
 * Perform a Matrix * vector multiply with the matrix represented as an
 * `std::vector` of rows.
 * @tparam EigVec1 A type inheriting from `Eigen::DenseBase` with compile time
 *  rows or columns equal to 1
 * @tparam EigVec2 A type inheriting from `Eigen::DenseBase` with compile time
 *  rows or columns equal to 1
 * @param y_buff A standard vector with elements representing the rows of
 *  a matrix.
 * @param alpha An eigen vector.
 */
template <typename EigVec1, typename EigVec2>
inline Eigen::MatrixXd std_vec_matrix_mul_vector(
    const std::vector<EigVec1>& y_buff, const EigVec2& alpha) {
  Eigen::VectorXd ret(y_buff.size());
  for (Eigen::Index i = 0; i < y_buff.size(); ++i) {
    ret(i) = y_buff[i].dot(alpha);
  }
  return ret;
}

/**
 * Check the curvature of the LBFGS optimization path is convex.
 */
template <typename EigMat, stan::require_matrix_t<EigMat>* = nullptr,
          typename Logger>
inline Eigen::Array<bool, -1, 1> check_curve(const EigMat& Yk, const EigMat& Sk,
                                             Logger&& logger) {
  auto Dk = ((Yk.array()) * Sk.array()).colwise().sum();
  auto thetak = (Yk.array().square().colwise().sum() / Dk).abs();
  debug::curve_check(logger, Dk, thetak);
  return ((Dk > 0) && (thetak <= 1e12));
}

/**
 * eq 4.9
 * Gilbert, J.C., Lemaréchal, C. Some numerical experiments with
 * variable-storage quasi-Newton algorithms. Mathematical Programming 45,
 * 407–435 (1989). https://doi.org/10.1007/BF01589113
 */
template <typename EigVec1, typename EigVec2, typename EigVec3>
inline auto form_diag(const EigVec1& alpha_init, const EigVec2& Yk,
                      const EigVec3& Sk) {
  double y_alpha_y = (Yk.dot(alpha_init.asDiagonal() * Yk));
  double y_s = Yk.dot(Sk);
  double s_inv_alpha_s
      = Sk.dot(alpha_init.array().inverse().matrix().asDiagonal() * Sk);
  return y_s
         / (y_alpha_y / alpha_init.array() + Yk.array().square()
            - (y_alpha_y / s_inv_alpha_s)
                  * (Sk.array() / alpha_init.array()).square());
}

/**
 * The information from running the taylor approximation
 */
struct taylor_approx_t {
  Eigen::VectorXd x_center;
  double logdetcholHk;       // Log deteriminant of the cholesky
  Eigen::MatrixXd L_approx;  // Approximate choleskly
  Eigen::MatrixXd Qk;  // Q of the QR decompositon. Only used for sparse approx
  bool use_full;       // boolean indicationg if full or sparse approx was used.
};

struct elbo_est_t {
  double elbo{-std::numeric_limits<double>::infinity()};
  size_t fn_calls{0};
  Eigen::MatrixXd repeat_draws;
  Eigen::Array<double, -1, -1> lp_mat;
  Eigen::Array<double, -1, 1> lp_ratio;
};

/**
 * Generate approximate draws using either the full or sparse taylor
 * approximation.
 * @tparam EigMat A type inheriting from `Eigen::DenseBase` with dynamic rows
 * and columns.
 * @tparam EigVec A type inheriting from `Eigen::DenseBase` with the compile
 * time number of columns equal to 1.
 * @param u A matrix of gaussian IID samples with rows equal to the size of the
 * number of samples to be made and columns equal to the number of parameters.
 * @param taylor_approx Approximation from `construct_taylor_approximation`.
 * @param alpha TODO: Define this
 * @return A matrix with rows equal to the number of samples and columns equal
 * to the number of parameters.
 */
template <typename EigMat, typename EigVec,
          require_eigen_matrix_dynamic_t<EigMat>* = nullptr>
inline Eigen::MatrixXd gen_draws(EigMat&& u,
                                 const taylor_approx_t& taylor_approx,
                                 const EigVec& alpha) {
  if (taylor_approx.use_full) {
    return crossprod(taylor_approx.L_approx, u).colwise()
           + taylor_approx.x_center;
  } else {
    Eigen::MatrixXd u1 = (taylor_approx.Qk.transpose() * u);
    return (alpha.array().sqrt().matrix().asDiagonal()
            * (taylor_approx.Qk * crossprod(taylor_approx.L_approx, u1)
               + (u - taylor_approx.Qk * u1)))
               .colwise()
           + taylor_approx.x_center;
  }
}

/**
 * Generate approximate draws using either the full or sparse taylor
 * approximation.
 * @tparam EigVec1 A type inheriting from `Eigen::DenseBase` with the compile
 * time number of columns equal to 1.
 * @tparam EigVec2 A type inheriting from `Eigen::DenseBase` with the compile
 * time number of columns equal to 1.
 * @param u A matrix of gaussian IID samples with columns equal to the size of
 * the number of samples to be made and rows equal to the number of parameters.
 * @param taylor_approx Approximation from `construct_taylor_approximation`.
 * @param alpha TODO: Define this
 * @return A matrix with columns equal to the number of samples and rows equal
 * to the number of parameters.
 */
template <typename EigVec1, typename EigVec2,
          require_eigen_vector_t<EigVec1>* = nullptr>
inline Eigen::VectorXd gen_draws(EigVec1&& u,
                                 const taylor_approx_t& taylor_approx,
                                 const EigVec2& alpha) {
  if (taylor_approx.use_full) {
    return crossprod(taylor_approx.L_approx, u) + taylor_approx.x_center;
  } else {
    Eigen::VectorXd u1 = (taylor_approx.Qk.transpose() * u);
    return (alpha.array().sqrt().matrix().asDiagonal()
            * (taylor_approx.Qk * crossprod(taylor_approx.L_approx, u1)
               + (u - taylor_approx.Qk * u1)))
           + taylor_approx.x_center;
  }
}

/**
 * Generate an Eigen matrix of from an rng generator.
 * @tparam RowsAtCompileTime The number of compile time rows for the result
 * matrix.
 * @tparam ColsAtCompileTime The number of compile time cols for the result
 * matrix.
 * @tparam Generator A functor with a valid `operator()` used to generate the
 * samples.
 * @param variate_generator An rng generator
 * @param num_params The runtime number of parameters
 * @param num_samples The runtime number of samples.
 *
 */
template <Eigen::Index RowsAtCompileTime = -1,
          Eigen::Index ColsAtCompileTime = -1, typename Generator>
inline Eigen::Matrix<double, RowsAtCompileTime, ColsAtCompileTime>
gen_eigen_matrix(Generator&& variate_generator, const Eigen::Index num_params,
                 const Eigen::Index num_samples) {
  return Eigen::Matrix<double, RowsAtCompileTime, ColsAtCompileTime>::
      NullaryExpr(num_params, num_samples,
                  [&variate_generator]() { return variate_generator(); });
}

/**
 *
 */
template <bool ReturnElbo = true, typename LPF, typename ConstrainF,
          typename RNG, typename EigVec, typename Logger>
inline elbo_est_t est_approx_draws(LPF&& lp_fun, ConstrainF&& constrain_fun,
                                   RNG&& rng,
                                   const taylor_approx_t& taylor_approx,
                                   size_t num_samples, const EigVec& alpha,
                                   Logger&& logger, int num_eval_attempts,
                                   const std::string& iter_msg) {
  boost::variate_generator<boost::ecuyer1988&, boost::normal_distribution<>>
      rand_unit_gaus(rng, boost::normal_distribution<>());

  const auto num_params = taylor_approx.x_center.size();
  size_t lp_fun_calls = 0;
  Eigen::MatrixXd uniform_samps_tmp
      = gen_eigen_matrix(rand_unit_gaus, num_params, num_samples);
  Eigen::MatrixXd approx_samples_tmp(num_params, num_samples);
  approx_samples_tmp = gen_draws(uniform_samps_tmp, taylor_approx, alpha);
  debug::rnorm_draws(logger, approx_samples_tmp);
  Eigen::Array<double, -1, -1> lp_mat_tmp(num_samples, 2);
  Eigen::VectorXd approx_samples_tmp_col;
  Eigen::VectorXd approx_samples_constrained_col;
  std::stringstream pathfinder_ss;
  auto log_stream = [](auto& logger, auto& pathfinder_ss) mutable {
    if (pathfinder_ss.str().length() > 0) {
      logger.info(pathfinder_ss);
      pathfinder_ss.str(std::string());
    }
  };
  bool at_least_one_failed = false;
  for (Eigen::Index i = 0; i < num_samples; ++i) {
    for (size_t fail_trys = 0; fail_trys <= num_eval_attempts; ++fail_trys) {
      try {
        approx_samples_tmp_col = approx_samples_tmp.col(i);
        ++lp_fun_calls;
        lp_mat_tmp.coeffRef(i, 1)
            = lp_fun(approx_samples_tmp_col, pathfinder_ss);
        if (std::isfinite(lp_mat_tmp.coeff(i, 1))) {
          log_stream(logger, pathfinder_ss);
          break;
        } else {
          if (fail_trys == num_eval_attempts) {
            lp_mat_tmp.coeffRef(i, 1)
                = -std::numeric_limits<double>::infinity();
            at_least_one_failed = true;
            log_stream(logger, pathfinder_ss);
          }
          uniform_samps_tmp.col(i)
              = gen_eigen_matrix<-1, 1>(rand_unit_gaus, num_params, 1);
          approx_samples_tmp.col(i)
              = gen_draws(uniform_samps_tmp.col(i), taylor_approx, alpha);
        }
      } catch (const std::exception& e) {
        if (fail_trys == num_eval_attempts) {
          lp_mat_tmp.coeffRef(i, 1) = -std::numeric_limits<double>::infinity();
          at_least_one_failed = true;
          log_stream(logger, pathfinder_ss);
        }
        uniform_samps_tmp.col(i)
            = gen_eigen_matrix<-1, 1>(rand_unit_gaus, num_params, 1);
        approx_samples_tmp.col(i)
            = gen_draws(uniform_samps_tmp.col(i), taylor_approx, alpha);
      }
    }
  }
  // Cleanup for -inf values
  Eigen::Array<double, -1, -1> lp_mat;
  Eigen::MatrixXd approx_samples;
  Eigen::MatrixXd uniform_samps;
  if (at_least_one_failed) {
    std::vector<Eigen::Index> success_rows;
    success_rows.reserve(lp_mat_tmp.rows());
    for (Eigen::Index i = 0; i < lp_mat_tmp.rows(); ++i) {
      if (std::isfinite(lp_mat_tmp(i, 1))) {
        success_rows.push_back(i);
      }
    }
    if (success_rows.size() == 0) {
      approx_samples_tmp_col = approx_samples_tmp.col(0);
      try {
        double test_val = lp_fun(approx_samples_tmp_col, pathfinder_ss);
        if (!std::isfinite(test_val)) {
          throw std::domain_error(iter_msg +
             "Approximate estimation failed after " +
             std::to_string(num_eval_attempts) +
            " attempts because the approximated samples returned back log(0)"
            " from calling lp calculation.");
        }
      } catch (const std::exception& e) {
        throw std::domain_error(iter_msg +
          "Approximate samples failed to create any samples with final error"
          " message: " + e.what());
      }
    } else {
      lp_mat = Eigen::Array<double, -1, -1>(success_rows.size(), 2);
      approx_samples = Eigen::MatrixXd(num_params, success_rows.size());
      uniform_samps = Eigen::MatrixXd(num_params, success_rows.size());
      for (Eigen::Index i = 0; i < success_rows.size(); ++i) {
        lp_mat(i, 1) = lp_mat_tmp(success_rows[i], 1);
        approx_samples.col(i) = approx_samples_tmp.col(success_rows[i]);
        uniform_samps.col(i) = uniform_samps_tmp.col(success_rows[i]);
      }
    }
  } else {
    lp_mat = std::move(lp_mat_tmp);
    approx_samples = std::move(approx_samples_tmp);
    uniform_samps = std::move(uniform_samps_tmp);
  }

  lp_mat.col(0) = (-taylor_approx.logdetcholHk)
                  + -0.5
                        * (uniform_samps.array().square().colwise().sum()
                           + num_params * stan::math::LOG_TWO_PI);
  Eigen::Array<double, -1, 1> lp_ratio = (lp_mat.col(1)) - lp_mat.col(0);
  if (ReturnElbo) {
    double ELBO = lp_ratio.mean();
    debug::elbo_draws(logger, taylor_approx, approx_samples, lp_mat, ELBO);
    return elbo_est_t{ELBO, lp_fun_calls, std::move(approx_samples),
                      std::move(lp_mat), std::move(lp_ratio)};
  } else {
    return elbo_est_t{-std::numeric_limits<double>::infinity(), lp_fun_calls,
                      std::move(approx_samples), std::move(lp_mat),
                      std::move(lp_ratio)};
  }
}

/**
 * Construct the full taylor approximation
 * @tparam EigVec Type inheriting from `Eigen::DenseBase` with 1 compile time
 * column.
 * @tparam Buff An std::vector holding column views of an Eigen Matrix
 * @tparam AlphaVec Type inheriting from `Eigen::DenseBase` with 1 compile time
 * column.
 * @tparam DkVec Type inheriting from `Eigen::DenseBase` with 1 compile time
 * column.
 * @tparam InvMat Type inheriting from `Eigen::DenseBase` with dynamic compile
 * time rows and columns
 * @tparam Logger Type inheriting from `stan::io::logger`
 * @param Ykt_mat std vector of length equal to history size containing the
 * gradients from the iterations of LBFGS
 * @param alpha The diagonal of the approximate hessian
 * @param Dk vector of Columnwise products of parameter and gradients with size
 * equal to history size
 * @param ninvRST
 * @param point_est The parameters for the given iteration of LBFGS
 * @param grad_est The gradients for the given iteration of LBFGS
 * @param logger used for printing out debug values.
 */
template <typename EigVec, typename Buff, typename AlphaVec, typename DkVec,
          typename InvMat, typename Logger>
inline taylor_approx_t construct_taylor_approximation_full(
    const Buff& Ykt_mat, const AlphaVec& alpha, const DkVec& Dk,
    const InvMat& ninvRST, const EigVec& point_est, const EigVec& grad_est,
    Logger&& logger) {
  debug::taylor_appx_full1(logger, alpha, ninvRST, Dk, point_est, grad_est);
  Eigen::MatrixXd y_tcrossprod_alpha = tcrossprod(std_vec_matrix_times_diagonal(
      Ykt_mat, alpha.array().sqrt().matrix().eval()));
  y_tcrossprod_alpha += Dk.asDiagonal();
  const auto dk_min_size
      = std::min(y_tcrossprod_alpha.rows(), y_tcrossprod_alpha.cols());
  Eigen::MatrixXd y_mul_alpha = std_vec_matrix_times_diagonal(Ykt_mat, alpha);
  Eigen::MatrixXd Hk = crossprod(y_mul_alpha, ninvRST)
                       + crossprod(ninvRST, y_mul_alpha)
                       + crossprod(ninvRST, y_tcrossprod_alpha * ninvRST);
  Hk += alpha.asDiagonal();
  Eigen::MatrixXd L_hk = Hk.llt().matrixL().transpose();
  double logdetcholHk = L_hk.diagonal().array().abs().log().sum();
  Eigen::VectorXd x_center = point_est - Hk * grad_est;
  debug::taylor_appx_full2(logger, Hk, L_hk, logdetcholHk, x_center);
  return taylor_approx_t{std::move(x_center), logdetcholHk, std::move(L_hk),
                         Eigen::MatrixXd(0, 0), true};
}

/**
 * Construct the sparse taylor approximation
 * @tparam EigVec Type inheriting from `Eigen::DenseBase` with 1 compile time
 * column.
 * @tparam Buff An std::vector holding column views of an Eigen Matrix
 * @tparam AlphaVec Type inheriting from `Eigen::DenseBase` with 1 compile time
 * column.
 * @tparam DkVec Type inheriting from `Eigen::DenseBase` with 1 compile time
 * column.
 * @tparam InvMat Type inheriting from `Eigen::DenseBase` with dynamic compile
 * time rows and columns
 * @tparam Logger Type inheriting from `stan::io::logger`
 * @param Ykt_mat std vector of length equal to history size containing the
 * gradients from the iterations of LBFGS
 * @param alpha The diagonal of the approximate hessian
 * @param Dk vector of Columnwise products of parameter and gradients with size
 * equal to history size
 * @param ninvRST
 * @param point_est The parameters for the given iteration of LBFGS
 * @param grad_est The gradients for the given iteration of LBFGS
 * @param logger used for printing out debug values.
 */
template <typename EigVec, typename Buff, typename AlphaVec, typename DkVec,
          typename InvMat, typename Logger>
inline auto construct_taylor_approximation_sparse(
    const Buff& Ykt_mat, const AlphaVec& alpha, const DkVec& Dk,
    const InvMat& ninvRST, const EigVec& point_est, const EigVec& grad_est,
    Logger&& logger) {
  const Eigen::Index history_size = Ykt_mat.size();
  const Eigen::Index history_size_times_2 = history_size * 2;
  const Eigen::Index num_params = alpha.size();
  Eigen::MatrixXd y_mul_sqrt_alpha = std_vec_matrix_times_diagonal(
      Ykt_mat, alpha.array().sqrt().matrix().eval());
  Eigen::MatrixXd Wkbart(history_size_times_2, num_params);
  Wkbart.topRows(history_size) = y_mul_sqrt_alpha;
  Wkbart.bottomRows(history_size)
      = ninvRST * alpha.array().inverse().sqrt().matrix().asDiagonal();
  debug::taylor_appx_sparse1(logger, Wkbart);
  Eigen::MatrixXd Mkbar(history_size_times_2, history_size_times_2);
  Mkbar.topLeftCorner(history_size, history_size).setZero();
  Mkbar.topRightCorner(history_size, history_size)
      = Eigen::MatrixXd::Identity(history_size, history_size);
  Mkbar.bottomLeftCorner(history_size, history_size)
      = Eigen::MatrixXd::Identity(history_size, history_size);
  Eigen::MatrixXd y_tcrossprod_alpha = tcrossprod(y_mul_sqrt_alpha);
  y_tcrossprod_alpha += Dk.asDiagonal();
  Mkbar.bottomRightCorner(history_size, history_size) = y_tcrossprod_alpha;
  Wkbart.transposeInPlace();
  const auto min_size = std::min(num_params, history_size_times_2);
  // Note: This is doing the QR decomp inplace using Wkbart's memory
  Eigen::HouseholderQR<Eigen::Ref<decltype(Wkbart)>> qr(Wkbart);
  Eigen::MatrixXd Rkbar
      = qr.matrixQR().topLeftCorner(min_size, history_size_times_2);
  Rkbar.triangularView<Eigen::StrictlyLower>().setZero();
  Eigen::MatrixXd Qk
      = qr.householderQ() * Eigen::MatrixXd::Identity(num_params, min_size);
  Eigen::MatrixXd L_approx = (Rkbar * Mkbar * Rkbar.transpose()
                              + Eigen::MatrixXd::Identity(min_size, min_size))
                                 .llt()
                                 .matrixL()
                                 .transpose();
  double logdetcholHk = L_approx.diagonal().array().abs().log().sum()
                        + 0.5 * alpha.array().log().sum();
  Eigen::VectorXd ninvRSTg = ninvRST * grad_est;
  Eigen::VectorXd alpha_mul_grad = (alpha.array() * grad_est.array()).matrix();
  Eigen::VectorXd x_center
      = point_est
        - (alpha_mul_grad
           + (alpha.array()
              * std_vec_matrix_crossprod_vector(Ykt_mat, ninvRSTg).array())
                 .matrix()
           + crossprod(ninvRST,
                       std_vec_matrix_mul_vector(Ykt_mat, alpha_mul_grad))
           + crossprod(ninvRST, y_tcrossprod_alpha * ninvRSTg));
  debug::taylor_appx_sparse2(logger, qr, alpha, Qk, L_approx, logdetcholHk,
                             Mkbar, Wkbart, x_center, ninvRST, ninvRSTg, Rkbar);
  return taylor_approx_t{std::move(x_center), logdetcholHk, std::move(L_approx),
                         std::move(Qk), false};
}

/**
 * Construct the taylor approximation.
 * @tparam EigVec Type inheriting from `Eigen::DenseBase` with 1 compile time
 * column.
 * @tparam Buff An std::vector holding column views of an Eigen Matrix
 * @tparam AlphaVec Type inheriting from `Eigen::DenseBase` with 1 compile time
 * column.
 * @tparam DkVec Type inheriting from `Eigen::DenseBase` with 1 compile time
 * column.
 * @tparam InvMat Type inheriting from `Eigen::DenseBase` with dynamic compile
 * time rows and columns
 * @tparam Logger Type inheriting from `stan::io::logger`
 * @param Ykt_mat std vector of length equal to history size containing the
 * gradients from the iterations of LBFGS
 * @param alpha The diagonal of the approximate hessian
 * @param Dk vector of Columnwise products of parameter and gradients with size
 * equal to history size
 * @param ninvRST
 * @param point_est The parameters for the given iteration of LBFGS
 * @param grad_est The gradients for the given iteration of LBFGS
 * @param logger used for printing out debug values.
 */
template <typename EigVec, typename Buff, typename AlphaVec, typename DkVec,
          typename InvMat, typename Logger>
inline taylor_approx_t construct_taylor_approximation(
    const Buff& Ykt_mat, const AlphaVec& alpha, const DkVec& Dk,
    const InvMat& ninvRST, const EigVec& point_est, const EigVec& grad_est,
    Logger&& logger) {
  // If twice the current history size is larger than the number of params
  // use a sparse approximation
  if (2 * Ykt_mat.size() >= Ykt_mat[0].size()) {
    return construct_taylor_approximation_full(Ykt_mat, alpha, Dk, ninvRST,
                                               point_est, grad_est, logger);
  } else {
    return construct_taylor_approximation_sparse(Ykt_mat, alpha, Dk, ninvRST,
                                                 point_est, grad_est, logger);
  }
}

/**
 * Construct the return for directly calling single pathfinder or
 *  calling single pathfinder from multi pathfinder.
 * @tparam ReturnLpSamples if `true` then this function returns the lp_ratio
 *  and samples. If false then only the return code is returned
 * @tparam EigMat An Eigen matrix
 * @tparam EigVec An Eigen Vector
 */
template <bool ReturnLpSamples, typename EigMat, typename EigVec,
          std::enable_if_t<ReturnLpSamples>* = nullptr>
inline auto ret_pathfinder(int return_code, EigVec&& lp_ratio, EigMat&& samples,
                           const std::atomic<size_t>& lp_calls) {
  return std::make_tuple(return_code, std::forward<EigVec>(lp_ratio),
                         std::forward<EigMat>(samples), lp_calls.load());
}

template <bool ReturnLpSamples, typename EigMat, typename EigVec,
          std::enable_if_t<!ReturnLpSamples>* = nullptr>
inline auto ret_pathfinder(int return_code, EigVec&& lp_ratio, EigMat&& samples,
                           const std::atomic<size_t>& lp_calls) noexcept {
  return return_code;
}
}  // namespace internal
/**
 * Runs a single pathfinder
 * @tparam ReturnLpSamples if `true` single pathfinder returns the lp_ratio
 * vector and approximate samples. If `false` only gives gives a return code.
 * @tparam Model A model implementation
 * @tparam DiagnosticWriter Type inheriting from stan::callbacks::writer
 * @tparam ParamWriter Type inheriting from stan::callbacks::writer
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
 * @param[in] num_eval_attempts Number of times to attempt to calculate
 * the log probability of an MC sample while calculating the ELBO.
 * @param[in,out] logger Logger for messages
 * @param[in,out] init_writer Writer callback for unconstrained inits
 * @param[in,out] parameter_writer output for parameter values
 * @param[in,out] diagnostic_writer output for diagnostics values
 * @return error_codes::OK if successful
 */
template <bool ReturnLpSamples = false, class Model, typename DiagnosticWriter,
          typename ParamWriter>       //, typename XVal, typename GVal>
inline auto pathfinder_lbfgs_single(  // XVal&& x_val, GVal&& g_val,
    Model& model, const stan::io::var_context& init, unsigned int random_seed,
    unsigned int path, double init_radius, int history_size, double init_alpha,
    double tol_obj, double tol_rel_obj, double tol_grad, double tol_rel_grad,
    double tol_param, int num_iterations, bool save_iterations, int refresh,
    callbacks::interrupt& interrupt, int num_elbo_draws, int num_draws,
    int num_eval_attempts, callbacks::logger& logger,
    callbacks::writer& init_writer, ParamWriter& parameter_writer,
    DiagnosticWriter& diagnostic_writer) {
  const auto start_optim_time = std::chrono::steady_clock::now();
  boost::ecuyer1988 rng
      = util::create_rng<boost::ecuyer1988>(random_seed, path);
  std::vector<int> disc_vector;
  std::vector<double> cont_vector = util::initialize<false>(
      model, init, rng, init_radius, false, logger, init_writer);
  const auto param_size = cont_vector.size();
  // Setup LBFGS
  std::stringstream lbfgs_ss;
  stan::optimization::LSOptions<double> ls_opts;
  ls_opts.alpha0 = init_alpha;
  stan::optimization::ConvergenceOptions<double> conv_opts;
  conv_opts.tolAbsF = tol_obj;
  conv_opts.tolRelF = tol_rel_obj;
  conv_opts.tolAbsGrad = tol_grad;
  conv_opts.tolRelGrad = tol_rel_grad;
  conv_opts.tolAbsX = tol_param;
  conv_opts.maxIts = num_iterations;
  using lbfgs_update_t
      = stan::optimization::LBFGSUpdate<double, Eigen::Dynamic>;
  lbfgs_update_t lbfgs_update(history_size);
  using Optimizer
      = stan::optimization::BFGSLineSearch<Model, lbfgs_update_t, true>;
  Optimizer lbfgs(model, cont_vector, disc_vector, std::move(ls_opts),
                  std::move(conv_opts), std::move(lbfgs_update), &lbfgs_ss);
  const std::string path_num("Path: [" + std::to_string(path) + "] ");
  if (refresh != 0) {
    logger.info(path_num + "Initial log joint probability = "
                + std::to_string(lbfgs.logp()));
  }
  std::vector<std::string> names;
  model.constrained_param_names(names, true, true);
  names.push_back("lp_approx__");
  names.push_back("lp__");
  parameter_writer(names);
  int ret = 0;
  std::vector<Eigen::VectorXd> param_vecs;
  param_vecs.reserve(num_iterations);
  std::vector<Eigen::VectorXd> grad_vecs;
  grad_vecs.reserve(num_iterations);
  {
    std::vector<double> g1;
    double lp = stan::model::log_prob_grad<true, true>(model, cont_vector,
                                                       disc_vector, g1);
    param_vecs.emplace_back(
        Eigen::Map<Eigen::VectorXd>(cont_vector.data(), param_size));
    grad_vecs.emplace_back(Eigen::Map<Eigen::VectorXd>(g1.data(), param_size));
    if (save_iterations) {
      diagnostic_writer(std::make_tuple(
          Eigen::Map<Eigen::VectorXd>(cont_vector.data(), param_size).eval(),
          Eigen::Map<Eigen::VectorXd>(g1.data(), param_size).eval()));
    }
  }
  auto constrain_fun = [&model](auto&& rng, auto&& unconstrained_draws,
                                auto&& constrained_draws) {
    model.write_array(rng, unconstrained_draws, constrained_draws);
    return constrained_draws;
  };
  int param_cols_filled = 0;
  while (ret == 0) {
    std::stringstream msg;
    interrupt();
    ret = lbfgs.step();
    double lp = lbfgs.logp();
    if (refresh > 0
        && (ret != 0 || !lbfgs.note().empty() || lbfgs.iter_num() == 0
            || ((lbfgs.iter_num() + 1) % refresh == 0))) {
      std::stringstream msg;
      msg << path_num +
          "    Iter"
          "      log prob"
          "        ||dx||"
          "      ||grad||"
          "       alpha"
          "      alpha0"
          "  # evals"
          "  Notes \n";
      msg << path_num << " " << std::setw(7) << lbfgs.iter_num() << " ";
      msg << " " << std::setw(12) << std::setprecision(6) << lp << " ";
      msg << " " << std::setw(12) << std::setprecision(6)
          << lbfgs.prev_step_size() << " ";
      msg << " " << std::setw(12) << std::setprecision(6)
          << lbfgs.curr_g().norm() << " ";
      msg << " " << std::setw(10) << std::setprecision(4) << lbfgs.alpha()
          << " ";
      msg << " " << std::setw(10) << std::setprecision(4) << lbfgs.alpha0()
          << " ";
      msg << " " << std::setw(7) << lbfgs.grad_evals() << " ";
      msg << " " << lbfgs.note() << " ";
      logger.info(msg.str());
    }

    if (lbfgs_ss.str().length() > 0) {
      logger.info(lbfgs_ss);
      lbfgs_ss.str("");
    }
    /*
     * If the retcode is -1 then linesearch failed even with a hessian reset
     * so the current vals and grads are the same as the previous iter
     * and we are exiting
     */
    if (likely(ret != -1)) {
      param_vecs.emplace_back(lbfgs.curr_x());
      grad_vecs.emplace_back(lbfgs.curr_g());
      ++param_cols_filled;
    }
    if (msg.str().length() > 0) {
      logger.info(msg);
    }
    if (save_iterations) {
      diagnostic_writer(std::make_tuple(lbfgs.curr_x(), lbfgs.curr_g()));
    }
  }
  int return_code;
  if (ret >= 0) {
    logger.info("Optimization terminated normally: ");
  } else {
    logger.info("Optimization terminated with error: ");
    logger.info("  " + lbfgs.get_code_string(ret));
    if (param_vecs.size() == 1) {
      logger.info("Optimization failed to start, pathfinder cannot be run.");
      return internal::ret_pathfinder<ReturnLpSamples>(
          error_codes::SOFTWARE, Eigen::Array<double, -1, 1>(0),
          Eigen::Array<double, -1, -1>(0, 0), std::atomic<size_t>{0});
    } else {
      logger.info(
          "Stan will still attempt pathfinder but may fail or produce "
          "incorrect "
          "results.");
    }
    return_code = error_codes::OK;
  }
  const auto end_optim_time = std::chrono::steady_clock::now();
  const double optim_delta_time
      = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_optim_time - start_optim_time)
            .count()
        / 1000.0;
  const auto start_pathfinder_time = std::chrono::steady_clock::now();
  Eigen::MatrixXd Ykt_diff(param_size, param_cols_filled);
  Eigen::MatrixXd Skt_diff(param_size, param_cols_filled);
  for (Eigen::Index i = 0; i < param_cols_filled; ++i) {
    Ykt_diff.col(i) = grad_vecs[i + 1] - grad_vecs[i];
    Skt_diff.col(i) = param_vecs[i + 1] - param_vecs[i];
  }
  const auto diff_size = Ykt_diff.cols();
  Eigen::MatrixXd alpha_mat(param_size, diff_size);
  Eigen::Matrix<bool, -1, 1> check_curve_vec
      = internal::check_curve(Ykt_diff, Skt_diff, logger);
  if (check_curve_vec[0]) {
    alpha_mat.col(0) = internal::form_diag(Eigen::VectorXd::Ones(param_size),
                                           Ykt_diff.col(0), Skt_diff.col(0));
  } else {
    alpha_mat.col(0).setOnes();
  }
  for (Eigen::Index iter = 1; iter < diff_size; iter++) {
    if (check_curve_vec[iter]) {
      alpha_mat.col(iter) = internal::form_diag(
          alpha_mat.col(iter - 1), Ykt_diff.col(iter), Skt_diff.col(iter));
    } else {
      alpha_mat.col(iter) = alpha_mat.col(iter - 1);
    }
  }
  std::mutex update_best_mutex;
  internal::debug::post_lbfgs(logger, update_best_mutex, param_size,
                              num_elbo_draws, alpha_mat, Ykt_diff, Skt_diff);
  auto lp_fun = [&model](auto&& u, auto&& streamer) {
    return model.template log_prob<false, true>(u, &streamer);
  };
  // NOTE: We always push the first one no matter what
  check_curve_vec[0] = true;
  std::vector<boost::ecuyer1988> rng_vec;
  const int tbb_threads = tbb::this_task_arena::max_concurrency();
  rng_vec.reserve(tbb_threads);
  for (Eigen::Index i = 0; i < tbb_threads + 1; i++) {
    rng_vec.emplace_back(
        util::create_rng<boost::ecuyer1988>(random_seed, path + i));
  }
  Eigen::Index best_E = -1;
  internal::elbo_est_t elbo_best;
  internal::taylor_approx_t taylor_approx_best;
  std::atomic<size_t> num_evals{lbfgs.grad_evals()};
  tbb::parallel_for(
      tbb::blocked_range<Eigen::Index>(0, diff_size),
      [&](tbb::blocked_range<Eigen::Index> r) {
        auto&& thread_rng
            = rng_vec[tbb::this_task_arena::current_thread_index()];
        for (int iter = r.begin(); iter < r.end(); ++iter) {
          std::string iter_msg(path_num + "Iter: [" + std::to_string(iter)
                               + "] ");
          if (STAN_DEBUG_PATH_ITERS) {
            logger.info(iter_msg + "\n------------ Iter: "
                        + std::to_string(iter) + "------------\n");
          }
          auto alpha = alpha_mat.col(iter);
          std::vector<Eigen::Index> ys_cols;
          ys_cols.reserve(history_size);
          {
            for (Eigen::Index end_iter = iter; end_iter >= 0; --end_iter) {
              if (check_curve_vec[end_iter]) {
                ys_cols.push_back(end_iter);
              }
              if (ys_cols.size() == history_size) {
                break;
              }
            }
          }
          const auto current_history_size = ys_cols.size();
          std::vector<decltype(Ykt_diff.col(0))> Ykt_h;
          Ykt_h.reserve(current_history_size);
          Eigen::MatrixXd Skt_mat(Skt_diff.rows(), current_history_size);
          for (Eigen::Index i = 0; i < current_history_size; ++i) {
            Ykt_h.push_back(Ykt_diff.col(ys_cols[i]));
            Skt_mat.col(i) = Skt_diff.col(ys_cols[i]);
          }
          Eigen::VectorXd Dk(current_history_size);
          for (Eigen::Index i = 0; i < current_history_size; i++) {
            Dk.coeffRef(i) = Ykt_h[i].dot(Skt_mat.col(i));
          }
          Eigen::MatrixXd Rk = Eigen::MatrixXd::Zero(current_history_size,
                                                     current_history_size);
          for (Eigen::Index s = 0; s < current_history_size; s++) {
            for (Eigen::Index i = 0; i <= s; i++) {
              Rk.coeffRef(i, s) = Skt_mat.col(i).dot(Ykt_h[s]);
            }
          }
          Eigen::MatrixXd ninvRST;
          {
            Skt_mat.transposeInPlace();
            Rk.triangularView<Eigen::Upper>().solveInPlace(Skt_mat);
            ninvRST = std::move(-Skt_mat);
          }
          auto taylor_appx_tuple = internal::construct_taylor_approximation(
              Ykt_h, alpha, Dk, ninvRST, param_vecs[iter + 1],
              grad_vecs[iter + 1], logger);
          internal::elbo_est_t elbo_est;
          try {
            elbo_est = internal::est_approx_draws<true>(
                lp_fun, constrain_fun, thread_rng, taylor_appx_tuple,
                num_elbo_draws, alpha, logger, num_eval_attempts, iter_msg);
            num_evals += elbo_est.fn_calls;
          } catch (const std::exception& e) {
            logger.info(iter_msg + "ELBO estimation failed "
                        + " with error: " + e.what());
            continue;
          }
          if (refresh > 0 && (iter == 0 || (iter % refresh == 0))) {
            logger.info(iter_msg + ": ELBO (" + std::to_string(elbo_est.elbo)
                        + ")");
          }
          {
            std::lock_guard<std::mutex> guard(update_best_mutex);
            if (elbo_est.elbo > elbo_best.elbo) {
              elbo_best = std::move(elbo_est);
              taylor_approx_best = std::move(taylor_appx_tuple);
              best_E = iter;
            }
          }
        }
      });
  if (best_E == -1) {
    logger.info(path_num +
        "Failure: None of the LBFGS iterations completed "
        "successfully");
    return internal::ret_pathfinder<ReturnLpSamples>(
        error_codes::SOFTWARE, Eigen::Array<double, -1, 1>(0),
        Eigen::Array<double, -1, -1>(0, 0), num_evals);
  } else {
    if (refresh != 0) {
      logger.info(path_num + "Best Iter: [" + std::to_string(best_E)
                  + "] ELBO (" + std::to_string(elbo_best.elbo) + ")"
                  + " evalutions: (" + std::to_string(num_evals) + ")");
    }
  }
  Eigen::Array<double, -1, -1> constrained_draws_mat;
  Eigen::Array<double, -1, 1> lp_ratio;
  auto&& elbo_draws = elbo_best.repeat_draws;
  auto&& elbo_lp_ratio = elbo_best.lp_ratio;
  auto&& elbo_lp_mat = elbo_best.lp_mat;
  const int remaining_draws = num_draws - elbo_lp_ratio.rows();
  const Eigen::Index num_unconstrained_params = names.size() - 2;
  if (remaining_draws > 0) {
    try {
      auto&& thread_rng = rng_vec[tbb::this_task_arena::current_thread_index()];
      internal::elbo_est_t est_draws = internal::est_approx_draws<false>(
          lp_fun, constrain_fun, thread_rng, taylor_approx_best,
          remaining_draws, alpha_mat.col(best_E), logger, num_eval_attempts,
          path_num);
      num_evals += est_draws.fn_calls;
      auto&& new_lp_ratio = est_draws.lp_ratio;
      auto&& lp_draws = est_draws.lp_mat;
      auto&& new_draws = est_draws.repeat_draws;
      lp_ratio = Eigen::Array<double, -1, 1>(new_lp_ratio.size()
                                             + elbo_lp_ratio.size());
      lp_ratio.head(elbo_lp_ratio.size()) = elbo_lp_ratio.array();
      lp_ratio.tail(new_lp_ratio.size()) = new_lp_ratio.array();
      const auto total_size = elbo_draws.cols() + new_draws.cols();
      constrained_draws_mat
          = Eigen::Array<double, -1, -1>(names.size(), total_size);
      Eigen::VectorXd unconstrained_col;
      Eigen::VectorXd approx_samples_constrained_col;
      for (Eigen::Index i = 0; i < elbo_draws.cols(); ++i) {
        unconstrained_col = elbo_draws.col(i);
        constrained_draws_mat.col(i).head(num_unconstrained_params)
            = constrain_fun(rng, unconstrained_col,
                            approx_samples_constrained_col);
        constrained_draws_mat.col(i).tail(2) = elbo_lp_mat.row(i);
      }
      for (Eigen::Index i = elbo_draws.cols(), j = 0; i < total_size;
           ++i, ++j) {
        unconstrained_col = new_draws.col(j);
        constrained_draws_mat.col(i).head(num_unconstrained_params)
            = constrain_fun(rng, unconstrained_col,
                            approx_samples_constrained_col);
        constrained_draws_mat.col(i).tail(2) = lp_draws.row(j);
      }
    } catch (const std::exception& e) {
      logger.info(path_num + "Final sampling approximation failed with error: "
                  + e.what());
      logger.info(
          path_num
          + "Returning the approximate samples used for ELBO calculation: "
          + e.what());
      constrained_draws_mat
          = Eigen::Array<double, -1, -1>(names.size(), elbo_draws.cols());
      Eigen::VectorXd approx_samples_constrained_col;
      Eigen::VectorXd unconstrained_col;
      for (Eigen::Index i = 0; i < elbo_draws.cols(); ++i) {
        unconstrained_col = elbo_draws.col(i);
        constrained_draws_mat.col(i).head(num_unconstrained_params)
            = constrain_fun(rng, unconstrained_col,
                            approx_samples_constrained_col);
        constrained_draws_mat.col(i).tail(2) = elbo_lp_mat.row(i);
      }
      lp_ratio = std::move(elbo_best.lp_ratio);
    }
  } else {
    constrained_draws_mat
        = Eigen::Array<double, -1, -1>(names.size(), elbo_draws.cols());
    Eigen::VectorXd approx_samples_constrained_col;
    Eigen::VectorXd unconstrained_col;
    for (Eigen::Index i = 0; i < elbo_draws.cols(); ++i) {
      unconstrained_col = elbo_draws.col(i);
      constrained_draws_mat.col(i).head(num_unconstrained_params)
          = constrain_fun(rng, unconstrained_col,
                          approx_samples_constrained_col);
      constrained_draws_mat.col(i).tail(2) = elbo_lp_mat.row(i);
    }
    lp_ratio = std::move(elbo_best.lp_ratio);
  }
  parameter_writer(constrained_draws_mat.matrix());
  auto end_pathfinder_time = std::chrono::steady_clock::now();
  double pathfinder_delta_time
      = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_pathfinder_time - start_pathfinder_time)
            .count()
        / 1000.0;
  parameter_writer();
  const auto time_header = std::string("Elapsed Time: ");
  std::string optim_time_str
      = time_header + std::to_string(optim_delta_time) + " seconds (lbfgs)";
  parameter_writer(optim_time_str);
  std::string pathfinder_time_str = std::string(time_header.size(), ' ')
                                    + std::to_string(pathfinder_delta_time)
                                    + " seconds (Pathfinder)";
  parameter_writer(pathfinder_time_str);
  std::string total_time_str
      = std::string(time_header.size(), ' ')
        + std::to_string(optim_delta_time + pathfinder_delta_time)
        + " seconds (Total)";
  parameter_writer(total_time_str);
  parameter_writer();
  return internal::ret_pathfinder<ReturnLpSamples>(
      error_codes::OK, std::move(lp_ratio), std::move(constrained_draws_mat),
      num_evals);
}

}  // namespace pathfinder
}  // namespace services
}  // namespace stan
#endif
