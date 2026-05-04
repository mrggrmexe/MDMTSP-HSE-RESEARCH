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

[[nodiscard]] Route build_cheapest_insertion_route(
    const node_id_t depot_node,
    std::vector<node_id_t> customers,
    const DistanceMatrix& matrix,
    const bool return_to_depot
) {
    Route route;
    route.reserve(customers.size() + (return_to_depot ? 2U : 1U));
    route.push_back(depot_node);

    if (customers.empty()) {
        if (return_to_depot) {
            route.push_back(depot_node);
        }
        return route;
    }

    route.push_back(customers.back());
    customers.pop_back();

    if (return_to_depot) {
        route.push_back(depot_node);
    }

    while (!customers.empty()) {
        std::size_t best_customer_idx = 0;
        std::size_t best_insert_pos = 1;
        cost_t best_delta = std::numeric_limits<cost_t>::infinity();
        bool found = false;

        const std::size_t last_insert_pos =
            return_to_depot ? route.size() - 1 : route.size();

        for (std::size_t customer_idx = 0; customer_idx < customers.size(); ++customer_idx) {
            const auto customer = customers[customer_idx];

            for (std::size_t insert_pos = 1; insert_pos <= last_insert_pos; ++insert_pos) {
                const auto delta = insertion_delta(route, customer, insert_pos, matrix);

                if (!found || delta < best_delta) {
                    found = true;
                    best_delta = delta;
                    best_customer_idx = customer_idx;
                    best_insert_pos = insert_pos;
                }
            }
        }

        if (!found) {
            throw std::logic_error("build_cheapest_insertion_route: no insertion found");
        }

        const auto customer = customers[best_customer_idx];
        route.insert(route.begin() + static_cast<std::ptrdiff_t>(best_insert_pos), customer);
        customers.erase(customers.begin() + static_cast<std::ptrdiff_t>(best_customer_idx));
    }

    return route;
}

}  // namespace

MDMTSPSolution solve_mdmtsp_cheapest_insertion(
    const MDMTSPInstance& instance,
    Random& rng
) {
    instance.validate_basic();

    const auto matrix = instance.build_distance_matrix();
    const auto customer_to_depot = assign_customers_to_nearest_depots(instance);
    const auto salesman_to_depot = assign_salesmen_to_depots_round_robin(instance);
    const auto depot_groups = group_customers_by_depot(instance, customer_to_depot);
    const auto chunks = distribute_customers_across_salesmen(depot_groups, salesman_to_depot, rng);

    MDMTSPSolution solution;
    solution.routes.reserve(chunks.size());

    for (salesman_id_t salesman = 0; salesman < chunks.size(); ++salesman) {
        const auto depot = salesman_to_depot[salesman];
        const auto depot_node = static_cast<node_id_t>(depot);

        auto route_nodes = build_cheapest_insertion_route(
            depot_node,
            chunks[salesman],
            matrix,
            instance.return_to_depot
        );

        solution.routes.push_back(MDMTSPSalesmanRoute{
            salesman,
            depot,
            std::move(route_nodes)
        });
    }

    solution.objective = compute_objective(solution, instance);
    solution.feasible = is_solution_feasible(instance, solution);
    solution.status = solution.feasible ? "ok" : validation_report(instance, solution);

    return solution;
}

}  // namespace mdmtsp