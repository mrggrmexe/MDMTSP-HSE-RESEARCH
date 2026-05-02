#include "solution.hpp"

namespace mdmtsp {

std::size_t MDMTSPSolution::route_count() const noexcept {
    return routes.size();
}

std::size_t MDMTSPSolution::visited_customer_count() const noexcept {
    std::size_t total = 0;

    for (const auto& route : routes) {
        const auto depot_node = static_cast<node_id_t>(route.depot_id);
        for (const auto node : route.nodes) {
            if (node != depot_node) {
                ++total;
            }
        }
    }

    return total;
}

void MDMTSPSolution::clear() {
    routes.clear();
    objective = 0.0;
    feasible = false;
    status.clear();
}

}  // namespace mdmtsp