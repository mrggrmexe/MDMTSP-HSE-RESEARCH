#include "mdmtsp_solver.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
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

[[nodiscard]] MDMTSPSolution make_empty_solution(
    const MDMTSPInstance& instance,
    const std::vector<depot_id_t>& salesman_to_depot
) {
    MDMTSPSolution solution;
    solution.routes.reserve(salesman_to_depot.size());

    for (salesman_id_t salesman = 0; salesman < salesman_to_depot.size(); ++salesman) {
        const auto depot = salesman_to_depot[salesman];
        const auto depot_node = static_cast<node_id_t>(depot);

        Route nodes;
        nodes.push_back(depot_node);

        if (instance.return_to_depot) {
            nodes.push_back(depot_node);
        }

        solution.routes.push_back(MDMTSPSalesmanRoute{
            salesman,
            depot,
            std::move(nodes)
        });
    }

    return solution;
}

[[nodiscard]] cost_t insertion_delta(
    const Route& route,
    const node_id_t customer,
    const std::size_t insert_pos,
    const DistanceMatrix& matrix
) {
    if (insert_pos == 0 || insert_pos > route.size()) {
        throw std::out_of_range("insertion_delta: invalid insert position");
    }

    const auto prev = route[insert_pos - 1];

    if (insert_pos == route.size()) {
        return matrix[prev][customer];
    }

    const auto next = route[insert_pos];
    return matrix[prev][customer] + matrix[customer][next] - matrix[prev][next];
}

void insert_customer_best_position(
    MDMTSPSolution& solution,
    const node_id_t customer,
    const depot_id_t depot,
    const DistanceMatrix& matrix
) {
    std::size_t best_route_idx = 0;
    std::size_t best_insert_pos = 0;
    cost_t best_delta = std::numeric_limits<cost_t>::infinity();
    bool found = false;

    for (std::size_t route_idx = 0; route_idx < solution.routes.size(); ++route_idx) {
        auto& route = solution.routes[route_idx];

        if (route.depot_id != depot) {
            continue;
        }

        const std::size_t first_insert_pos = 1;
        const std::size_t last_insert_pos = route.nodes.size();

        for (std::size_t insert_pos = first_insert_pos; insert_pos <= last_insert_pos; ++insert_pos) {
            const auto delta = insertion_delta(route.nodes, customer, insert_pos, matrix);

            if (!found || delta < best_delta) {
                found = true;
                best_delta = delta;
                best_route_idx = route_idx;
                best_insert_pos = insert_pos;
            }
        }
    }

    if (!found) {
        throw std::logic_error("insert_customer_best_position: depot has no assigned route");
    }

    auto& target_nodes = solution.routes[best_route_idx].nodes;
    target_nodes.insert(target_nodes.begin() + static_cast<std::ptrdiff_t>(best_insert_pos), customer);
}

}  // namespace

MDMTSPSolution solve_mdmtsp_random_insertion(
    const MDMTSPInstance& instance,
    Random& rng
) {
    instance.validate_basic();

    const auto matrix = instance.build_distance_matrix();
    const auto customer_to_depot = assign_customers_to_nearest_depots(instance);
    const auto salesman_to_depot = assign_salesmen_to_depots_round_robin(instance);

    auto depot_groups = group_customers_by_depot(instance, customer_to_depot);
    auto solution = make_empty_solution(instance, salesman_to_depot);

    for (depot_id_t depot = 0; depot < depot_groups.size(); ++depot) {
        auto customers = depot_groups[depot];
        rng.shuffle(customers);

        for (const auto customer : customers) {
            insert_customer_best_position(solution, customer, depot, matrix);
        }
    }

    solution.objective = compute_objective(solution, instance);
    solution.feasible = is_solution_feasible(instance, solution);
    solution.status = solution.feasible ? "ok" : validation_report(instance, solution);

    return solution;
}

}  // namespace mdmtsp