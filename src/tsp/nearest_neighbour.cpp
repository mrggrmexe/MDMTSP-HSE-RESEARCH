#include "tsp_solver.hpp"

#include <memory>
#include <cmath>
#include <stdexcept>

namespace mdmtsp::tsp {

namespace {

struct XorShift64 {
    std::uint64_t s;
    explicit XorShift64(std::uint64_t seed) : s(seed ? seed : 0x9e3779b97f4a7c15ULL) {}
    std::uint64_t next() noexcept {
        std::uint64_t x = s;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        s = x;
        return x;
    }
};

inline bool approx_equal(Cost a, Cost b) noexcept {
    const Cost da = std::abs(a);
    const Cost db = std::abs(b);
    const Cost scale = std::max<Cost>(1.0, std::max(da, db));
    const Cost eps = 1e-12 * scale;
    return std::abs(a - b) <= eps;
}

inline bool strictly_better(Cost a, Cost b) noexcept {
    const Cost da = std::abs(a);
    const Cost db = std::abs(b);
    const Cost scale = std::max<Cost>(1.0, std::max(da, db));
    const Cost eps = 1e-12 * scale;
    return a + eps < b;
}

inline Index pick_default_start(const TSPProblem& p) noexcept {
    const auto maxv = std::numeric_limits<Index>::max();
    if (p.start != maxv) return p.start;
    if (p.type == TourType::Path && p.end != maxv) {
        if (p.n == 1) return 0;
        return (p.end == 0) ? 1 : 0;
    }
    return 0;
}

}  // namespace

class NearestNeighborSolver final : public TSPSolver {
public:
    std::string name() const override { return "nearest_neighbor"; }

    TSPSolution solve(const TSPProblem& problem, const TSPSolveOptions& options) override {
        const auto t0 = std::chrono::steady_clock::now();
        TimeBudget budget(options.time_limit_ms);

        const Status basic = problem.validate_basic();
        if (basic != Status::Ok) return make_failure(basic, name(), "invalid problem");

        const Index n = problem.n;
        if (n == 0) return make_failure(Status::InvalidArgument, name(), "empty instance");

        const auto maxv = std::numeric_limits<Index>::max();
        const bool has_start = problem.start != maxv;
        const bool has_end   = (problem.type == TourType::Path) && (problem.end != maxv);
        const Index end_node = has_end ? problem.end : maxv;

        Index start_node = pick_default_start(problem);
        if (n > 1 && has_end && start_node == end_node) {
            start_node = (end_node == 0) ? 1 : 0;
        }
        if (start_node >= n) return make_failure(Status::InvalidArgument, name(), "start out of range");
        if (has_end && end_node >= n) return make_failure(Status::InvalidArgument, name(), "end out of range");

        if (n == 1) {
            std::vector<Index> order;
            order.reserve(1);
            if (has_start && problem.start != 0) return make_failure(Status::InvalidArgument, name(), "start mismatch");
            if (has_end && problem.end != 0) return make_failure(Status::InvalidArgument, name(), "end mismatch");
            order.push_back(0);
            const auto t1 = std::chrono::steady_clock::now();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
            return finalize_solution(problem, options, std::move(order), name(), static_cast<std::uint64_t>(elapsed));
        }

        std::vector<std::uint8_t> visited(static_cast<std::size_t>(n), 0);
        std::vector<Index> order;
        order.reserve(static_cast<std::size_t>(n));

        visited[static_cast<std::size_t>(start_node)] = 1;
        order.push_back(start_node);

        std::uint64_t iterations = 0;
        std::uint64_t evaluated = 0;

        XorShift64 rng(options.seed ^ (static_cast<std::uint64_t>(n) << 1) ^ 0xD1B54A32D192ED03ULL);
        const bool use_rng = options.seed != 0;

        std::size_t remaining = static_cast<std::size_t>(n) - 1;

        while (remaining > 0) {
            if (budget.expired()) return make_failure(Status::TimeLimit, name(), "time limit");
            if (options.iteration_limit != 0 && iterations >= options.iteration_limit)
                return make_failure(Status::IterationLimit, name(), "iteration limit");

            const Index curr = order.back();

            if (has_end && remaining == 1) {
                if (visited[static_cast<std::size_t>(end_node)] != 0)
                    return make_failure(Status::Infeasible, name(), "end already visited");
                order.push_back(end_node);
                visited[static_cast<std::size_t>(end_node)] = 1;
                remaining = 0;
                ++iterations;
                break;
            }

            bool found = false;
            Index best_v = 0;
            Cost best_c = std::numeric_limits<Cost>::infinity();
            std::uint64_t tie_count = 0;

            for (Index v = 0; v < n; ++v) {
                if (visited[static_cast<std::size_t>(v)] != 0) continue;
                if (has_end && v == end_node) continue;

                Cost c;
                try {
                    c = problem.dist(curr, v);
                } catch (...) {
                    return make_failure(Status::InternalError, name(), "distance function threw");
                }
                ++evaluated;

                if (!is_finite(c)) continue;
                if (options.require_non_negative_costs && c < 0) continue;

                if (!found || strictly_better(c, best_c)) {
                    found = true;
                    best_c = c;
                    best_v = v;
                    tie_count = 1;
                } else if (approx_equal(c, best_c)) {
                    ++tie_count;
                    if (use_rng) {
                        if ((rng.next() % tie_count) == 0) best_v = v;
                    } else {
                        if (v < best_v) best_v = v;
                    }
                }
            }

            if (!found) {
                if (has_end) {
                    bool only_end_left = true;
                    for (Index v = 0; v < n; ++v) {
                        if (visited[static_cast<std::size_t>(v)] == 0 && v != end_node) {
                            only_end_left = false;
                            break;
                        }
                    }
                    if (only_end_left && visited[static_cast<std::size_t>(end_node)] == 0) {
                        order.push_back(end_node);
                        visited[static_cast<std::size_t>(end_node)] = 1;
                        --remaining;
                        ++iterations;
                        continue;
                    }
                }
                return make_failure(Status::Infeasible, name(), "no feasible next node");
            }

            order.push_back(best_v);
            visited[static_cast<std::size_t>(best_v)] = 1;
            --remaining;
            ++iterations;
        }

        const auto t1 = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        return finalize_solution(problem, options, std::move(order), name(),
                                static_cast<std::uint64_t>(elapsed), iterations, evaluated);
    }
};

std::unique_ptr<TSPSolver> make_nearest_neighbor_solver() {
    return std::make_unique<NearestNeighborSolver>();
}

}  // namespace mdmtsp::tsp