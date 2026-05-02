#include "assignment.hpp"

#include <limits>
#include <stdexcept>
#include <vector>

namespace mdmtsp {

std::vector<depot_id_t> assign_customers_to_nearest_depots(
    const MDMTSPInstance& instance
) {
    instance.validate_basic();

    std::vector<depot_id_t> assignment(instance.customer_count(), invalid_depot_id);

    for (std::size_t customer_idx = 0; customer_idx < instance.customer_count(); ++customer_idx) {
        const auto customer_node = instance.customer_node_from_index(customer_idx);
        const auto customer_point = instance.point_of(customer_node);

        cost_t best_distance = std::numeric_limits<cost_t>::infinity();
        depot_id_t best_depot = invalid_depot_id;

        for (depot_id_t depot = 0; depot < instance.depot_count(); ++depot) {
            const auto depot_point = instance.depots[depot];
            const auto distance = euclidean_distance(customer_point, depot_point);

            if (distance < best_distance) {
                best_distance = distance;
                best_depot = depot;
            }
        }

        assignment[customer_idx] = best_depot;
    }

    return assignment;
}

std::vector<depot_id_t> assign_salesmen_to_depots_round_robin(
    const MDMTSPInstance& instance
) {
    instance.validate_basic();

    std::vector<depot_id_t> assignment(instance.salesman_count, invalid_depot_id);

    for (salesman_id_t salesman = 0; salesman < instance.salesman_count; ++salesman) {
        assignment[salesman] = salesman % instance.depot_count();
    }

    return assignment;
}

}  // namespace mdmtsp