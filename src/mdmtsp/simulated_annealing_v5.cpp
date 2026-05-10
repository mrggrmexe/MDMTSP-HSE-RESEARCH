#include "mdmtsp_solver.hpp"

#include <cstddef>
#include <utility>

namespace mdmtsp {

[[nodiscard]] MDMTSPSolution solve_mdmtsp_simulated_annealing_v1(
    const MDMTSPInstance& instance,
    Random& rng
);

[[nodiscard]] MDMTSPSolution solve_mdmtsp_simulated_annealing_v3(
    const MDMTSPInstance& instance,
    Random& rng
);

[[nodiscard]] MDMTSPSolution solve_mdmtsp_simulated_annealing_v4(
    const MDMTSPInstance& instance,
    Random& rng
);

namespace {

constexpr cost_t kSaV5Eps = static_cast<cost_t>(1e-9);
constexpr std::size_t kSaV5SmallDelegationThreshold = 120;
constexpr std::size_t kSaV5SecondV3Threshold = 1200;
constexpr std::size_t kSaV5TryV4Threshold = 1800;

[[nodiscard]] bool solution_better_than(
    const MDMTSPSolution& lhs,
    const MDMTSPSolution& rhs
) noexcept {
    if (lhs.feasible != rhs.feasible) {
        return lhs.feasible && !rhs.feasible;
    }
    if (lhs.objective != rhs.objective) {
        return lhs.objective < rhs.objective - kSaV5Eps;
    }
    return lhs.routes.size() < rhs.routes.size();
}

[[nodiscard]] MDMTSPSolution keep_best(
    MDMTSPSolution incumbent,
    MDMTSPSolution candidate
) {
    if (solution_better_than(candidate, incumbent)) {
        return candidate;
    }
    return incumbent;
}

}  // namespace

[[nodiscard]] MDMTSPSolution solve_mdmtsp_simulated_annealing_v5(
    const MDMTSPInstance& instance,
    Random& rng
) {
    instance.validate_basic();

    auto best = solve_mdmtsp_simulated_annealing_v1(instance, rng);
    if (instance.customer_count() <= kSaV5SmallDelegationThreshold) {
        return best;
    }

    best = keep_best(std::move(best), solve_mdmtsp_simulated_annealing_v3(instance, rng));

    if (instance.customer_count() <= kSaV5SecondV3Threshold) {
        best = keep_best(std::move(best), solve_mdmtsp_simulated_annealing_v3(instance, rng));
    }

    if (instance.customer_count() <= kSaV5TryV4Threshold) {
        best = keep_best(std::move(best), solve_mdmtsp_simulated_annealing_v4(instance, rng));
    }

    return best;
}

}  // namespace mdmtsp
