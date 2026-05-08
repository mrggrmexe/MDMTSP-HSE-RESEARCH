#include "mdmtsp_solver.hpp"

#include <stdexcept>

namespace mdmtsp {

MDMTSPSolution solve_mdmtsp_lkh_mdmtsp(
    const MDMTSPInstance& instance,
    Random& rng
) {
    (void)rng;

    instance.validate_basic();

    throw std::logic_error("algorithm 'lkh_mdmtsp' is not implemented yet");
}

}  // namespace mdmtsp
