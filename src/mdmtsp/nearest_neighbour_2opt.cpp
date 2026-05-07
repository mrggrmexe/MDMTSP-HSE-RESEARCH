#include "mdmtsp_solver.hpp"

#include <algorithm>
#include <vector>

namespace mdmtsp {

namespace {

[[nodiscard]] bool improve_two_opt_closed(Route& route, const DistanceMatrix& matrix) {
    if (route.size() < 4) {
        return false;
    }

    bool improved_any = false;

    for (;;) {
        cost_t best_delta = static_cast<cost_t>(0);
        std::size_t best_i = 0;
        std::size_t best_k = 0;

        for (std::size_t i = 1; i + 2 < route.size(); ++i) {
            for (std::size_t k = i + 1; k + 1 < route.size(); ++k) {
                const node_id_t a = route[i - 1];
                const node_id_t b = route[i];
                const node_id_t c = route[k];
                const node_id_t d = route[k + 1];

                const cost_t delta =
                    matrix[a][c] + matrix[b][d] - matrix[a][b] - matrix[c][d];

                if (delta < best_delta) {
                    best_delta = delta;
                    best_i = i;
                    best_k = k;
                }
            }
        }

        if (!(best_delta < static_cast<cost_t>(0))) {
            break;
        }

        std::reverse(route.begin() + static_cast<std::ptrdiff_t>(best_i),
                     route.begin() + static_cast<std::ptrdiff_t>(best_k + 1));
        improved_any = true;
    }

    return improved_any;
}

[[nodiscard]] bool improve_two_opt_open(Route& route, const DistanceMatrix& matrix) {
    if (route.size() < 3) {
        return false;
    }

    bool improved_any = false;

    for (;;) {
        cost_t best_delta = static_cast<cost_t>(0);
        std::size_t best_i = 0;
        std::size_t best_k = 0;

        for (std::size_t i = 1; i + 1 < route.size(); ++i) {
            for (std::size_t k = i + 1; k < route.size(); ++k) {
                const node_id_t a = route[i - 1];
                const node_id_t b = route[i];
                const node_id_t c = route[k];

                cost_t delta = static_cast<cost_t>(0);

                if (k + 1 < route.size()) {
                    const node_id_t d = route[k + 1];
                    delta = matrix[a][c] + matrix[b][d] - matrix[a][b] - matrix[c][d];
                } else {
                    delta = matrix[a][c] - matrix[a][b];
                }

                if (delta < best_delta) {
                    best_delta = delta;
                    best_i = i;
                    best_k = k;
                }
            }
        }

        if (!(best_delta < static_cast<cost_t>(0))) {
            break;
        }

        std::reverse(route.begin() + static_cast<std::ptrdiff_t>(best_i),
                     route.begin() + static_cast<std::ptrdiff_t>(best_k + 1));
        improved_any = true;
    }

    return improved_any;
}

void improve_route_with_two_opt(Route& route,
                                const DistanceMatrix& matrix,
                                const bool return_to_depot) {
    if (route.empty()) {
        return;
    }

    if (return_to_depot) {
        improve_two_opt_closed(route, matrix);
    } else {
        improve_two_opt_open(route, matrix);
    }
}

}  // namespace

[[nodiscard]] MDMTSPSolution solve_mdmtsp_nearest_neighbour_2opt(
    const MDMTSPInstance& instance,
    Random& rng
) {
    instance.validate_basic();

    auto solution = solve_mdmtsp_nearest_neighbour(instance, rng);
    const auto matrix = instance.build_distance_matrix();

    for (auto& route : solution.routes) {
        improve_route_with_two_opt(route.nodes, matrix, instance.return_to_depot);
    }

    solution.objective = compute_objective(solution, instance, matrix);
    solution.feasible = is_solution_feasible(instance, solution);
    solution.status = solution.feasible ? "ok" : validation_report(instance, solution);

    return solution;
}

}  // namespace mdmtsp