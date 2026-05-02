#pragma once

#include "instance.hpp"
#include "solution.hpp"

namespace mdmtsp {

[[nodiscard]] cost_t compute_objective(
    const MDMTSPSolution& solution,
    const MDMTSPInstance& instance,
    const DistanceMatrix& matrix
);

[[nodiscard]] cost_t compute_objective(
    const MDMTSPSolution& solution,
    const MDMTSPInstance& instance
);

}  // namespace mdmtsp