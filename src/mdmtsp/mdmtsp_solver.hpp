#pragma once

#include <string>

#include "../common/random.hpp"
#include "instance.hpp"
#include "solution.hpp"

namespace mdmtsp {

class MDMTSPSolver {
public:
    virtual ~MDMTSPSolver() = default;

    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual MDMTSPSolution solve(const MDMTSPInstance& instance, Random& rng) const = 0;
};

[[nodiscard]] MDMTSPSolution solve_mdmtsp_nearest_neighbour(
    const MDMTSPInstance& instance,
    Random& rng
);

}  // namespace mdmtsp