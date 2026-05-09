#pragma once

#include <string>

#include "../common/random.hpp"
#include "assignment.hpp"
#include "instance.hpp"
#include "interroute_local_search.hpp"
#include "objective.hpp"
#include "solution.hpp"
#include "validator.hpp"

namespace mdmtsp {

class MDMTSPSolver {
public:
    virtual ~MDMTSPSolver() = default;

    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual MDMTSPSolution solve(const MDMTSPInstance& instance, Random& rng) const = 0;
};

[[nodiscard]] MDMTSPSolution solve_mdmtsp_nearest_neighbour(
    const MDMTSPInstance& instance,
    Random& rng
);
[[nodiscard]] MDMTSPSolution solve_mdmtsp_random_insertion(
    const MDMTSPInstance& instance,
    Random& rng
);

[[nodiscard]] MDMTSPSolution solve_mdmtsp_cheapest_insertion(
    const MDMTSPInstance& instance,
    Random& rng
);

[[nodiscard]] MDMTSPSolution solve_mdmtsp_nearest_neighbour_2opt(
    const MDMTSPInstance& instance,
    Random& rng
);

[[nodiscard]] MDMTSPSolution solve_mdmtsp_nearest_neighbour_2opt_v2(
    const MDMTSPInstance& instance,
    Random& rng
);

[[nodiscard]] MDMTSPSolution solve_mdmtsp_cheapest_insertion_2opt(
    const MDMTSPInstance& instance,
    Random& rng
);

[[nodiscard]] MDMTSPSolution solve_mdmtsp_cheapest_insertion_vnd(
    const MDMTSPInstance& instance,
    Random& rng
);

[[nodiscard]] MDMTSPSolution solve_mdmtsp_cheapest_insertion_tabu(
    const MDMTSPInstance& instance,
    Random& rng
);

[[nodiscard]] MDMTSPSolution solve_mdmtsp_lkh_mdmtsp(
    const MDMTSPInstance& instance,
    Random& rng
);

[[nodiscard]] MDMTSPSolution solve_mdmtsp_lkh_mdmtsp_v2(
    const MDMTSPInstance& instance,
    Random& rng
);

[[nodiscard]] MDMTSPSolution solve_mdmtsp_lkh_mdmtsp_v3(
    const MDMTSPInstance& instance,
    Random& rng
);

[[nodiscard]] MDMTSPSolution solve_mdmtsp_lkh_mdmtsp_v4_portfolio(
    const MDMTSPInstance& instance,
    Random& rng
);

[[nodiscard]] MDMTSPSolution solve_mdmtsp_lkh_mdmtsp_v5_portfolio(
    const MDMTSPInstance& instance,
    Random& rng
);

[[nodiscard]] MDMTSPSolution solve_mdmtsp_simulated_annealing_v1(
    const MDMTSPInstance& instance,
    Random& rng
);

[[nodiscard]] MDMTSPSolution solve_mdmtsp_hgs_light_weight_v1(
    const MDMTSPInstance& instance,
    Random& rng
);

[[nodiscard]] MDMTSPSolution solve_mdmtsp_hgs_lightweight_v1(
    const MDMTSPInstance& instance,
    Random& rng
);

[[nodiscard]] MDMTSPSolution solve_mdmtsp_simulated_annealing_v2(
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

[[nodiscard]] MDMTSPSolution solve_mdmtsp_simulated_annealing_v5(
    const MDMTSPInstance& instance,
    Random& rng
);

[[nodiscard]] MDMTSPSolution solve_mdmtsp_hgs_lightweight_v2(
    const MDMTSPInstance& instance,
    Random& rng
);

[[nodiscard]] MDMTSPSolution solve_mdmtsp_hgs_lightweight_v3(
    const MDMTSPInstance& instance,
    Random& rng
);

[[nodiscard]] MDMTSPSolution solve_mdmtsp_hgs_lightweight_v4(
    const MDMTSPInstance& instance,
    Random& rng
);

[[nodiscard]] MDMTSPSolution solve_mdmtsp_hgs_lightweight_v5(
    const MDMTSPInstance& instance,
    Random& rng
);

}  // namespace mdmtsp