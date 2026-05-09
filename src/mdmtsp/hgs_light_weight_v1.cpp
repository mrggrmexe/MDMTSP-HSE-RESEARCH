#include "mdmtsp_solver.hpp"

#include <stdexcept>

namespace mdmtsp {

MDMTSPSolution solve_mdmtsp_hgs_light_weight_v1(
    const MDMTSPInstance& instance,
    Random& rng
) {
    (void)rng;

    instance.validate_basic();

    throw std::logic_error("algorithm 'hgs_light_weight_v1' is not implemented yet");
}

}  // namespace mdmtsp
