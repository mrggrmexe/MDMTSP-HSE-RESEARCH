#include "instance.hpp"
#include "solution.hpp"

#include <stdexcept>

#include "../common/distance.hpp"

namespace mdmtsp {

namespace {

[[nodiscard]] cost_t route_objective(
    const MDMTSPSalesmanRoute& route,
    const MDMTSPInstance& instance,
    const DistanceMatrix& matrix
) {
    if (route.depot_id >= instance.depot_count()) {
        throw std::out_of_range("route_objective: depot_id out of range");
    }

    const auto depot_node = static_cast<node_id_t>(route.depot_id);

    if (route.nodes.empty()) {
        return 0.0;
    }

    if (route.nodes.front() != depot_node) {
        throw std::invalid_argument("route_objective: route must start at its depot");
    }

    if (instance.return_to_depot) {
        if (route.nodes.back() != depot_node) {
            throw std::invalid_argument("route_objective: closed route must end at its depot");
        }
        return mdmtsp::route_cost(route.nodes, matrix);
    }

    return mdmtsp::route_cost(route.nodes, matrix);
}

}  // namespace

[[nodiscard]] cost_t compute_objective(
    const MDMTSPSolution& solution,
    const MDMTSPInstance& instance,
    const DistanceMatrix& matrix
) {
    cost_t total = 0.0;
    for (const auto& route : solution.routes) {
        total += route_objective(route, instance, matrix);
    }
    return total;
}

[[nodiscard]] cost_t compute_objective(
    const MDMTSPSolution& solution,
    const MDMTSPInstance& instance
) {
    return compute_objective(solution, instance, instance.build_distance_matrix());
}

}  // namespace mdmtsp