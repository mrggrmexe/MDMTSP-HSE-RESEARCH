#include "mdmtsp_solver.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

namespace mdmtsp {

namespace {

constexpr cost_t kEps = static_cast<cost_t>(1e-9);
constexpr int kMaxTabuIterations = 28;
constexpr int kMaxStagnationIterations = 10;
constexpr int kTabuTenure = 7;
constexpr std::size_t kMaxChecksPerRoute = 500'000;
constexpr std::size_t kMinWindow = 8;
constexpr std::size_t kMaxWindow = 24;

struct TabuEntry {
    node_id_t u{};
    node_id_t v{};
    int ttl{0};
};

[[nodiscard]] std::size_t tabu_window_for_route(const Route& route) {
    if (route.size() <= 16) {
        return route.size();
    }
    const std::size_t scaled = route.size() / 64;
    return std::min<std::size_t>(kMaxWindow, std::max<std::size_t>(kMinWindow, scaled));
}

[[nodiscard]] cost_t compute_route_cost(const Route& route, const DistanceMatrix& matrix) {
    cost_t cost = static_cast<cost_t>(0);
    for (std::size_t i = 1; i < route.size(); ++i) {
        cost += matrix[route[i - 1]][route[i]];
    }
    return cost;
}

void age_tabu_list(std::vector<TabuEntry>& tabu_list) {
    for (auto& entry : tabu_list) {
        --entry.ttl;
    }

    tabu_list.erase(
        std::remove_if(
            tabu_list.begin(),
            tabu_list.end(),
            [](const TabuEntry& entry) { return entry.ttl <= 0; }
        ),
        tabu_list.end()
    );
}

void add_tabu(std::vector<TabuEntry>& tabu_list, const node_id_t u, const node_id_t v) {
    const node_id_t a = std::min(u, v);
    const node_id_t b = std::max(u, v);
    tabu_list.push_back(TabuEntry{a, b, kTabuTenure});
}

[[nodiscard]] bool is_tabu(const std::vector<TabuEntry>& tabu_list,
                           const node_id_t u,
                           const node_id_t v) {
    const node_id_t a = std::min(u, v);
    const node_id_t b = std::max(u, v);

    for (const auto& entry : tabu_list) {
        if (entry.u == a && entry.v == b) {
            return true;
        }
    }

    return false;
}

struct BestMove {
    bool found{false};
    std::size_t i{0};
    std::size_t k{0};
    cost_t delta{static_cast<cost_t>(0)};
    node_id_t tabu_u{0};
    node_id_t tabu_v{0};
};

[[nodiscard]] BestMove find_best_tabu_two_opt_move(const Route& route,
                                                   const DistanceMatrix& matrix,
                                                   const bool return_to_depot,
                                                   const std::vector<TabuEntry>& tabu_list,
                                                   const cost_t current_cost,
                                                   const cost_t best_cost,
                                                   const std::size_t window,
                                                   std::size_t& checks) {
    BestMove best_move;
    best_move.delta = std::numeric_limits<cost_t>::max();

    if (route.size() < 4) {
        return best_move;
    }

    const std::size_t last = return_to_depot ? route.size() - 1 : route.size();

    for (std::size_t i = 1; i + 1 < last; ++i) {
        const std::size_t k_limit = std::min<std::size_t>(last - 1, i + window);

        for (std::size_t k = i + 1; k <= k_limit; ++k) {
            if (++checks > kMaxChecksPerRoute) {
                return best_move;
            }

            const node_id_t a = route[i - 1];
            const node_id_t b = route[i];
            const node_id_t c = route[k];

            cost_t delta = static_cast<cost_t>(0);
            node_id_t tabu_u = b;
            node_id_t tabu_v = c;

            if (k + 1 < route.size()) {
                const node_id_t d = route[k + 1];
                delta = matrix[a][c] + matrix[b][d] - matrix[a][b] - matrix[c][d];
                tabu_u = a;
                tabu_v = c;
            } else {
                if (return_to_depot) {
                    continue;
                }
                delta = matrix[a][c] - matrix[a][b];
                tabu_u = a;
                tabu_v = c;
            }

            const cost_t candidate_cost = current_cost + delta;
            const bool tabu = is_tabu(tabu_list, tabu_u, tabu_v);
            const bool aspiration = candidate_cost + kEps < best_cost;

            if (tabu && !aspiration) {
                continue;
            }

            if (!best_move.found || delta < best_move.delta) {
                best_move.found = true;
                best_move.i = i;
                best_move.k = k;
                best_move.delta = delta;
                best_move.tabu_u = tabu_u;
                best_move.tabu_v = tabu_v;
            }
        }
    }

    return best_move;
}

[[nodiscard]] bool improve_route_with_tabu(Route& route,
                                           const DistanceMatrix& matrix,
                                           const bool return_to_depot) {
    if (route.size() < 4) {
        return false;
    }

    Route current_route = route;
    cost_t current_cost = compute_route_cost(current_route, matrix);

    Route best_route = current_route;
    cost_t best_cost = current_cost;

    std::vector<TabuEntry> tabu_list;
    tabu_list.reserve(static_cast<std::size_t>(kTabuTenure) * 2U);

    const std::size_t window = tabu_window_for_route(route);
    std::size_t checks = 0;
    int stagnation = 0;
    bool improved = false;

    for (int iter = 0; iter < kMaxTabuIterations; ++iter) {
        const BestMove move = find_best_tabu_two_opt_move(
            current_route,
            matrix,
            return_to_depot,
            tabu_list,
            current_cost,
            best_cost,
            window,
            checks
        );

        if (!move.found) {
            break;
        }

        std::reverse(current_route.begin() + static_cast<std::ptrdiff_t>(move.i),
                     current_route.begin() + static_cast<std::ptrdiff_t>(move.k + 1));
        current_cost += move.delta;

        age_tabu_list(tabu_list);
        add_tabu(tabu_list, move.tabu_u, move.tabu_v);

        if (current_cost + kEps < best_cost) {
            best_cost = current_cost;
            best_route = current_route;
            improved = true;
            stagnation = 0;
        } else {
            ++stagnation;
        }

        if (checks > kMaxChecksPerRoute || stagnation >= kMaxStagnationIterations) {
            break;
        }
    }

    if (improved) {
        route = std::move(best_route);
    }

    return improved;
}

}  // namespace

[[nodiscard]] MDMTSPSolution solve_mdmtsp_cheapest_insertion_tabu(
    const MDMTSPInstance& instance,
    Random& rng
) {
    instance.validate_basic();

    auto solution = solve_mdmtsp_cheapest_insertion(instance, rng);
    const auto matrix = instance.build_distance_matrix();

    for (auto& route : solution.routes) {
        improve_route_with_tabu(route.nodes, matrix, instance.return_to_depot);
    }

    solution.objective = compute_objective(solution, instance, matrix);
    solution.feasible = is_solution_feasible(instance, solution);
    solution.status = solution.feasible ? "ok" : validation_report(instance, solution);

    return solution;
}

}  // namespace mdmtsp