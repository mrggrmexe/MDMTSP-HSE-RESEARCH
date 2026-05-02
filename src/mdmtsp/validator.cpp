#include "validator.hpp"

#include <sstream>
#include <vector>

namespace mdmtsp {

std::string validation_report(
    const MDMTSPInstance& instance,
    const MDMTSPSolution& solution
) {
    try {
        instance.validate_basic();
    } catch (const std::exception& e) {
        return e.what();
    }

    if (solution.routes.size() != instance.salesman_count) {
        std::ostringstream out;
        out << "route count must equal salesman_count: expected "
            << instance.salesman_count << ", got " << solution.routes.size();
        return out.str();
    }

    std::vector<std::size_t> customer_visits(instance.customer_count(), 0);

    for (std::size_t route_idx = 0; route_idx < solution.routes.size(); ++route_idx) {
        const auto& route = solution.routes[route_idx];

        if (route.depot_id >= instance.depot_count()) {
            std::ostringstream out;
            out << "route " << route_idx << " has depot_id out of range";
            return out.str();
        }

        if (route.nodes.empty()) {
            std::ostringstream out;
            out << "route " << route_idx << " is empty";
            return out.str();
        }

        const auto assigned_depot = static_cast<node_id_t>(route.depot_id);
        const auto last_pos = route.nodes.size() - 1;

        if (route.nodes.front() != assigned_depot) {
            std::ostringstream out;
            out << "route " << route_idx << " does not start at assigned depot";
            return out.str();
        }

        if (instance.return_to_depot && route.nodes.back() != assigned_depot) {
            std::ostringstream out;
            out << "route " << route_idx << " does not end at assigned depot";
            return out.str();
        }

        for (std::size_t pos = 0; pos < route.nodes.size(); ++pos) {
            const auto node = route.nodes[pos];

            if (node >= instance.node_count()) {
                std::ostringstream out;
                out << "route " << route_idx << " contains node out of range";
                return out.str();
            }

            const bool boundary = (pos == 0) || (pos == last_pos);

            if (instance.is_depot_node(node)) {
                if (!boundary) {
                    std::ostringstream out;
                    out << "route " << route_idx << " contains depot in internal position";
                    return out.str();
                }

                if (node != assigned_depot) {
                    std::ostringstream out;
                    if (pos == 0) {
                        out << "route " << route_idx << " does not start at assigned depot";
                    } else {
                        out << "route " << route_idx << " does not end at assigned depot";
                    }
                    return out.str();
                }

                continue;
            }

            const auto customer_idx = instance.customer_index_from_node(node);
            ++customer_visits[customer_idx];

            if (customer_visits[customer_idx] > 1) {
                std::ostringstream out;
                out << "customer visited more than once: customer_index=" << customer_idx;
                return out.str();
            }
        }
    }

    for (const auto visits : customer_visits) {
        if (visits != 1) {
            return "not all customers are visited exactly once";
        }
    }

    return "ok";
}

bool is_solution_feasible(
    const MDMTSPInstance& instance,
    const MDMTSPSolution& solution
) {
    return validation_report(instance, solution) == "ok";
}

}  // namespace mdmtsp