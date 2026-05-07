#include "mdmtsp_solver.hpp"

#include <algorithm>
#include <cstddef>

namespace mdmtsp {

namespace {

constexpr cost_t kEps = static_cast<cost_t>(1e-9);
constexpr int kMaxPassesPerRoute = 12;
constexpr std::size_t kMaxChecksPerRoute = 6'000'000;

[[nodiscard]] bool try_two_opt_pass(Route& route,
                                    const DistanceMatrix& matrix,
                                    const bool return_to_depot,
                                    std::size_t& checks) {
    if (route.size() < 4) {
        return false;
    }

    const std::size_t last = return_to_depot ? route.size() - 1 : route.size();

    for (std::size_t i = 1; i + 1 < last; ++i) {
        for (std::size_t k = i + 1; k < last; ++k) {
            if (++checks > kMaxChecksPerRoute) {
                return false;
            }

            const node_id_t a = route[i - 1];
            const node_id_t b = route[i];
            const node_id_t c = route[k];

            cost_t delta = static_cast<cost_t>(0);

            if (k + 1 < route.size()) {
                const node_id_t d = route[k + 1];
                delta = matrix[a][c] + matrix[b][d] - matrix[a][b] - matrix[c][d];
            } else {
                if (return_to_depot) {
                    continue;
                }
                delta = matrix[a][c] - matrix[a][b];
            }

            if (delta < -kEps) {
                std::reverse(route.begin() + static_cast<std::ptrdiff_t>(i),
                             route.begin() + static_cast<std::ptrdiff_t>(k + 1));
                return true;
            }
        }
    }

    return false;
}

void improve_route_with_limited_two_opt(Route& route,
                                        const DistanceMatrix& matrix,
                                        const bool return_to_depot) {
    std::size_t checks = 0;

    for (int pass = 0; pass < kMaxPassesPerRoute; ++pass) {
        if (!try_two_opt_pass(route, matrix, return_to_depot, checks)) {
            break;
        }
        if (checks > kMaxChecksPerRoute) {
            break;
        }
    }
}

}  // namespace

[[nodiscard]] MDMTSPSolution solve_mdmtsp_nearest_neighbour_2opt_v2(
    const MDMTSPInstance& instance,
    Random& rng
) {
    instance.validate_basic();

    auto solution = solve_mdmtsp_nearest_neighbour(instance, rng);
    const auto matrix = instance.build_distance_matrix();

    for (auto& route : solution.routes) {
        improve_route_with_limited_two_opt(route.nodes, matrix, instance.return_to_depot);
    }

    solution.objective = compute_objective(solution, instance, matrix);
    solution.feasible = is_solution_feasible(instance, solution);
    solution.status = solution.feasible ? "ok" : validation_report(instance, solution);

    return solution;
}

}  // namespace mdmtsp