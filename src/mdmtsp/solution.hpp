#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "../common/types.hpp"

namespace mdmtsp {

struct MDMTSPSalesmanRoute {
    salesman_id_t salesman_id = invalid_salesman_id;
    depot_id_t depot_id = invalid_depot_id;
    Route nodes;

    [[nodiscard]] bool empty() const noexcept {
        return nodes.empty();
    }
};

struct MDMTSPSolution {
    std::vector<MDMTSPSalesmanRoute> routes;
    cost_t objective = 0.0;
    bool feasible = false;
    std::string status;

    [[nodiscard]] std::size_t route_count() const noexcept;
    [[nodiscard]] std::size_t visited_customer_count() const noexcept;
    void clear();
};

}  // namespace mdmtsp