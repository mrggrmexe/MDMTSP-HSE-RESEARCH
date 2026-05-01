#include "mdmtsp_solver.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mdmtsp {

namespace {

[[nodiscard]] std::vector<std::vector<node_id_t>> group_customers_by_depot(
    const MDMTSPInstance& instance,
    const std::vector<depot_id_t>& customer_to_depot
) {
    std::vector<std::vector<node_id_t>> groups(instance.depot_count());

    for (std::size_t customer_idx = 0; customer_idx < customer_to_depot.size(); ++customer_idx) {
        const auto depot = customer_to_depot[customer_idx];
        if (depot >= instance.depot_count()) {
            throw std::out_of_range("group_customers_by_depot: depot_id out of range");
        }
        groups[depot].push_back(instance.customer_node_from_index(customer_idx));
    }

    return groups;
}

[[nodiscard]] std::vector<std::vector<node_id_t>> distribute_customers_across_salesmen(
    const std::vector<std::vector<node_id_t>>& depot_groups,
    const std::vector<depot_id_t>& salesman_to_depot,
    Random& rng
) {
    std::vector<std::vector<node_id_t>> chunks(salesman_to_depot.size());

    for (depot_id_t depot = 0; depot < depot_groups.size(); ++depot) {
        std::vector<salesman_id_t> eligible_salesmen;
        eligible_salesmen.reserve(salesman_to_depot.size());

        for (salesman_id_t salesman = 0; salesman < salesman_to_depot.size(); ++salesman) {
            if (salesman_to_depot[salesman] == depot) {
                eligible_salesmen.push_back(salesman);
            }
        }

        if (eligible_salesmen.empty()) {
            throw std::logic_error("distribute_customers_across_salesmen: depot has no assigned salesmen");
        }

        auto customers = depot_groups[depot];
        rng.shuffle(customers);

        for (std::size_t i = 0; i < customers.size(); ++i) {
            const auto salesman = eligible_salesmen[i % eligible_salesmen.size()];
            chunks[salesman].push_back(customers[i]);
        }
    }

    return chunks;
}

[[nodiscard]] Route build_nearest_neighbour_route(
    const node_id_t depot_node,
    std::vector<node_id_t> customers,
    const DistanceMatrix& matrix,
    const bool return_to_depot
) {
    Route route;
    route.reserve(customers.size() + (return_to_depot ? 2U : 1U));
    route.push_back(depot_node);

    node_id_t current = depot_node;

    while (!customers.empty()) {
        auto best_it = customers.begin();
        cost_t best_distance = matrix[current][*best_it];

        for (auto it = customers.begin() + 1; it != customers.end(); ++it) {
            const auto distance = matrix[current][*it];
            if (distance < best_distance) {
                best_distance = distance;
                best_it = it;
            }
        }

        current = *best_it;
        route.push_back(current);
        customers.erase(best_it);
    }

    if (return_to_depot) {
        route.push_back(depot_node);
    }

    return route;
}

[[nodiscard]] MDMTSPSalesmanRoute make_empty_route(
    const salesman_id_t salesman,
    const depot_id_t depot,
    const bool return_to_depot
) {
    MDMTSPSalesmanRoute route;
    route.salesman_id = salesman;
    route.depot_id = depot;
    route.nodes.push_back(static_cast<node_id_t>(depot));
    if (return_to_depot) {
        route.nodes.push_back(static_cast<node_id_t>(depot));
    }
    return route;
}

}  // namespace

[[nodiscard]] MDMTSPSolution solve_mdmtsp_nearest_neighbour(
    const MDMTSPInstance& instance,
    Random& rng
) {
    instance.validate_basic();

    const auto matrix = instance.build_distance_matrix();
    const auto customer_to_depot = assign_customers_to_nearest_depots(instance);
    const auto salesman_to_depot = assign_salesmen_to_depots_round_robin(instance);

    auto depot_groups = group_customers_by_depot(instance, customer_to_depot);
    auto salesman_chunks = distribute_customers_across_salesmen(depot_groups, salesman_to_depot, rng);

    MDMTSPSolution solution;
    solution.routes.reserve(instance.salesman_count);

    for (salesman_id_t salesman = 0; salesman < instance.salesman_count; ++salesman) {
        const auto depot = salesman_to_depot[salesman];
        const auto depot_node = static_cast<node_id_t>(depot);

        MDMTSPSalesmanRoute route;
        route.salesman_id = salesman;
        route.depot_id = depot;

        if (salesman_chunks[salesman].empty()) {
            route = make_empty_route(salesman, depot, instance.return_to_depot);
        } else {
            route.nodes = build_nearest_neighbour_route(
                depot_node,
                std::move(salesman_chunks[salesman]),
                matrix,
                instance.return_to_depot
            );
        }

        solution.routes.push_back(std::move(route));
    }

    solution.objective = compute_objective(solution, instance, matrix);
    solution.feasible = is_solution_feasible(instance, solution);
    solution.status = solution.feasible ? "ok" : validation_report(instance, solution);

    return solution;
}

}  // namespace mdmtsp