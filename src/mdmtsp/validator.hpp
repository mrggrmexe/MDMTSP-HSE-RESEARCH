#pragma once

#include <string>

#include "instance.hpp"
#include "solution.hpp"

namespace mdmtsp {

[[nodiscard]] bool is_solution_feasible(
    const MDMTSPInstance& instance,
    const MDMTSPSolution& solution
);

[[nodiscard]] std::string validation_report(
    const MDMTSPInstance& instance,
    const MDMTSPSolution& solution
);

}  // namespace mdmtsp