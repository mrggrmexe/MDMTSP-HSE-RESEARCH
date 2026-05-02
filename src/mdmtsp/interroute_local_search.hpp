#pragma once

#include <cstddef>

#include "instance.hpp"
#include "solution.hpp"

namespace mdmtsp {

void improve_interroute_by_relocation(
    MDMTSPSolution& solution,
    const MDMTSPInstance& instance,
    std::size_t max_iterations
);

}  // namespace mdmtsp