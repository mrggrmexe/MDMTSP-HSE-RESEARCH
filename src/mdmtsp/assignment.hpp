#pragma once

#include <vector>

#include "instance.hpp"

namespace mdmtsp {

[[nodiscard]] std::vector<depot_id_t> assign_customers_to_nearest_depots(
    const MDMTSPInstance& instance
);

[[nodiscard]] std::vector<depot_id_t> assign_salesmen_to_depots_round_robin(
    const MDMTSPInstance& instance
);

}  // namespace mdmtsp