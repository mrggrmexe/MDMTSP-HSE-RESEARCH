#include "interroute_local_search.hpp"

#include <optional>
#include <utility>

#include "objective.hpp"
#include "validator.hpp"

namespace mdmtsp {

namespace {

struct RelocateMove {
    std::size_t from_route = 0;
    std::size_t from_pos = 0;
    std::size_t to_route = 0;
    std::size_t to_pos = 0;
    cost_t delta = 0.0;
};

[[nodiscard]] std::size_t route_internal_begin() noexcept {
    return 1;
}

[[nodiscard]] std::size_t route_internal_end(
    const MDMTSPSalesmanRoute& route,
    const bool return_to_depot
) noexcept {
    if (route.nodes.empty()) {
        return 0;
    }
    if (return_to_depot) {
        return route.nodes.size() > 0 ? route.nodes.size() - 1 : 0;
    }
    return route.nodes.size();
}

[[nodiscard]] bool can_relocate_from(
    const MDMTSPSalesmanRoute& route,
    const bool return_to_depot
) noexcept {
    return route_internal_begin() < route_internal_end(route, return_to_depot);
}

[[nodiscard]] std::optional<RelocateMove> find_best_relocate_move(
    const MDMTSPSolution& solution,
    const MDMTSPInstance& instance,
    const DistanceMatrix& matrix
) {
    std::optional<RelocateMove> best_move;

    for (std::size_t from_route_idx = 0; from_route_idx < solution.routes.size(); ++from_route_idx) {
        const auto& from_route = solution.routes[from_route_idx];

        if (!can_relocate_from(from_route, instance.return_to_depot)) {
            continue;
        }

        const auto from_begin = route_internal_begin();
        const auto from_end = route_internal_end(from_route, instance.return_to_depot);

        for (std::size_t from_pos = from_begin; from_pos < from_end; ++from_pos) {
            for (std::size_t to_route_idx = 0; to_route_idx < solution.routes.size(); ++to_route_idx) {
                const auto& to_route = solution.routes[to_route_idx];
                const auto to_begin = route_internal_begin();
                const auto to_end = route_internal_end(to_route, instance.return_to_depot);

                for (std::size_t to_pos = to_begin; to_pos <= to_end; ++to_pos) {
                    if (from_route_idx == to_route_idx &&
                        (to_pos == from_pos || to_pos == from_pos + 1)) {
                        continue;
                    }

                    auto candidate = solution;

                    auto& candidate_from = candidate.routes[from_route_idx].nodes;
                    const auto moved_node = candidate_from[from_pos];
                    candidate_from.erase(
                        candidate_from.begin() + static_cast<std::ptrdiff_t>(from_pos)
                    );

                    auto& candidate_to = candidate.routes[to_route_idx].nodes;
                    std::size_t adjusted_to_pos = to_pos;
                    if (from_route_idx == to_route_idx && to_pos > from_pos) {
                        --adjusted_to_pos;
                    }

                    candidate_to.insert(
                        candidate_to.begin() + static_cast<std::ptrdiff_t>(adjusted_to_pos),
                        moved_node
                    );

                    if (!is_solution_feasible(instance, candidate)) {
                        continue;
                    }

                    const auto old_cost = solution.objective;
                    const auto new_cost = compute_objective(candidate, instance, matrix);
                    const auto delta = new_cost - old_cost;

                    if (delta < -1e-12 && (!best_move.has_value() || delta < best_move->delta)) {
                        best_move = RelocateMove{
                            from_route_idx,
                            from_pos,
                            to_route_idx,
                            to_pos,
                            delta
                        };
                    }
                }
            }
        }
    }

    return best_move;
}

void apply_relocate_move(
    MDMTSPSolution& solution,
    const RelocateMove& move
) {
    auto& from_nodes = solution.routes[move.from_route].nodes;
    const auto moved_node = from_nodes[move.from_pos];

    from_nodes.erase(from_nodes.begin() + static_cast<std::ptrdiff_t>(move.from_pos));

    auto& to_nodes = solution.routes[move.to_route].nodes;
    std::size_t adjusted_to_pos = move.to_pos;
    if (move.from_route == move.to_route && move.to_pos > move.from_pos) {
        --adjusted_to_pos;
    }

    to_nodes.insert(
        to_nodes.begin() + static_cast<std::ptrdiff_t>(adjusted_to_pos),
        moved_node
    );
}

}  // namespace

void improve_interroute_by_relocation(
    MDMTSPSolution& solution,
    const MDMTSPInstance& instance,
    const std::size_t max_iterations
) {
    instance.validate_basic();

    if (solution.routes.empty()) {
        solution.objective = 0.0;
        solution.feasible = false;
        solution.status = "empty solution";
        return;
    }

    const auto matrix = instance.build_distance_matrix();

    if (!is_solution_feasible(instance, solution)) {
        solution.objective = compute_objective(solution, instance, matrix);
        solution.feasible = false;
        solution.status = validation_report(instance, solution);
        return;
    }

    solution.objective = compute_objective(solution, instance, matrix);

    for (std::size_t iteration = 0; iteration < max_iterations; ++iteration) {
        const auto move = find_best_relocate_move(solution, instance, matrix);
        if (!move.has_value()) {
            break;
        }

        apply_relocate_move(solution, *move);
        solution.objective = compute_objective(solution, instance, matrix);
    }

    solution.feasible = is_solution_feasible(instance, solution);
    solution.status = solution.feasible ? "ok" : validation_report(instance, solution);
}

}  // namespace mdmtsp