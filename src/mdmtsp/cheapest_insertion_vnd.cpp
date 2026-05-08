#include "mdmtsp/interroute_local_search.hpp"
#include "mdmtsp_solver.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace mdmtsp {

namespace {

constexpr cost_t kEps = static_cast<cost_t>(1e-9);
constexpr int kMaxVndRounds = 3;
constexpr int kTwoOptPassesPerRound = 4;
constexpr std::size_t kTwoOptChecksPerRound = 600'000;
constexpr std::size_t kMinWindow = 8;
constexpr std::size_t kMaxWindow = 40;
constexpr std::size_t kRelocationIterations = 8;

[[nodiscard]] std::size_t two_opt_window_for_route(const Route& route) {
    if (route.size() <= 16) {
        return route.size();
    }
    const std::size_t scaled = route.size() / 40;
    return std::min<std::size_t>(kMaxWindow, std::max<std::size_t>(kMinWindow, scaled));
}

[[nodiscard]] bool try_two_opt_pass(Route& route,
                                    const DistanceMatrix& matrix,
                                    const bool return_to_depot,
                                    const std::size_t window,
                                    std::size_t& checks) {
    if (route.size() < 4) {
        return false;
    }

    const std::size_t last = return_to_depot ? route.size() - 1 : route.size();

    for (std::size_t i = 1; i + 1 < last; ++i) {
        const std::size_t k_limit = std::min<std::size_t>(last - 1, i + window);

        for (std::size_t k = i + 1; k <= k_limit; ++k) {
            if (++checks > kTwoOptChecksPerRound) {
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

[[nodiscard]] bool improve_all_routes_with_two_opt(MDMTSPSolution& solution,
                                                   const DistanceMatrix& matrix,
                                                   const bool return_to_depot) {
    bool improved_any = false;

    for (auto& salesman_route : solution.routes) {
        auto& route = salesman_route.nodes;
        if (route.size() < 4) {
            continue;
        }

        const std::size_t window = two_opt_window_for_route(route);
        std::size_t checks = 0;

        for (int pass = 0; pass < kTwoOptPassesPerRound; ++pass) {
            if (!try_two_opt_pass(route, matrix, return_to_depot, window, checks)) {
                break;
            }
            improved_any = true;
            if (checks > kTwoOptChecksPerRound) {
                break;
            }
        }
    }

    return improved_any;
}

}  // namespace

[[nodiscard]] MDMTSPSolution solve_mdmtsp_cheapest_insertion_vnd(
    const MDMTSPInstance& instance,
    Random& rng
) {
    instance.validate_basic();

    auto solution = solve_mdmtsp_cheapest_insertion(instance, rng);
    const auto matrix = instance.build_distance_matrix();

    solution.objective = compute_objective(solution, instance, matrix);

    for (int round = 0; round < kMaxVndRounds; ++round) {
        bool changed = false;

        if (improve_all_routes_with_two_opt(solution, matrix, instance.return_to_depot)) {
            changed = true;
        }

        const cost_t before_relocation = compute_objective(solution, instance, matrix);

        improve_interroute_by_relocation(
            solution,
            instance,
            kRelocationIterations
        );

        const cost_t after_relocation = compute_objective(solution, instance, matrix);
        if (after_relocation + kEps < before_relocation) {
            changed = true;
        }

        if (improve_all_routes_with_two_opt(solution, matrix, instance.return_to_depot)) {
            changed = true;
        }

        solution.objective = compute_objective(solution, instance, matrix);

        if (!changed) {
            break;
        }
    }

    solution.objective = compute_objective(solution, instance, matrix);
    solution.feasible = is_solution_feasible(instance, solution);
    solution.status = solution.feasible ? "ok" : validation_report(instance, solution);

    return solution;
}

}  // namespace mdmtsp