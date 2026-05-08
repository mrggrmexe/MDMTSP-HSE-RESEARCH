#include "mdmtsp_solver.hpp"

#include <stdexcept>

namespace mdmtsp {

MDMTSPSolution solve_mdmtsp_lkh_mdmtsp_v4_portfolio(
    const MDMTSPInstance& instance,
    Random& rng
) {
    (void)rng;

    instance.validate_basic();

    throw std::logic_error("algorithm 'lkh_mdmtsp_v4_portfolio' is not implemented yet");
}

}  // namespace mdmtsp
