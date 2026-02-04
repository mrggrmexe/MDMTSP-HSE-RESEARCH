#include "tsp_solver.hpp"

#include <memory>
#include <cmath>
#include <chrono>
#include <stdexcept>
#include <utility>

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

inline bool dist_ok(const TSPProblem& p, const TSPSolveOptions& opt, Index u, Index v, Cost& out) noexcept {
    try {
        out = p.dist(u, v);
    } catch (...) {
        return false;
    }
    if (!is_finite(out)) return false;
    if (opt.require_non_negative_costs && out < 0) return false;
    return true;
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

Status build_initial_nn(const TSPProblem& p,
                        const TSPSolveOptions& opt,
                        TimeBudget& budget,
                        std::vector<Index>& order,
                        std::uint64_t& evaluated) {
    const Index n = p.n;
    const auto maxv = std::numeric_limits<Index>::max();
    const bool has_end = (p.type == TourType::Path) && (p.end != maxv);
    const Index end_node = has_end ? p.end : maxv;

    Index start = pick_default_start(p);
    if (n > 1 && has_end && start == end_node) start = (end_node == 0) ? 1 : 0;
    if (start >= n) return Status::InvalidArgument;
    if (has_end && end_node >= n) return Status::InvalidArgument;

    order.clear();
    order.reserve(static_cast<std::size_t>(n));

    std::vector<std::uint8_t> visited(static_cast<std::size_t>(n), 0);
    visited[static_cast<std::size_t>(start)] = 1;
    order.push_back(start);

    XorShift64 rng(opt.seed ^ (static_cast<std::uint64_t>(n) << 1) ^ 0xD1B54A32D192ED03ULL);
    const bool use_rng = opt.seed != 0;

    std::size_t remaining = static_cast<std::size_t>(n) - 1;

    while (remaining > 0) {
        if (budget.expired()) return Status::TimeLimit;

        const Index curr = order.back();

        if (has_end && remaining == 1) {
            if (visited[static_cast<std::size_t>(end_node)] != 0) return Status::Infeasible;
            order.push_back(end_node);
            visited[static_cast<std::size_t>(end_node)] = 1;
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
            if (!dist_ok(p, opt, curr, v, c)) continue;
            ++evaluated;

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

        if (!found) return Status::Infeasible;

        order.push_back(best_v);
        visited[static_cast<std::size_t>(best_v)] = 1;
        --remaining;
    }

    return Status::Ok;
}

struct BestMove {
    std::size_t i = 0;
    std::size_t k = 0;
    Cost delta = 0.0;
    bool found = false;
};

inline bool is_improving_delta(Cost delta) noexcept {
    const Cost eps = 1e-12 * std::max<Cost>(1.0, std::abs(delta));
    return delta + eps < 0.0;
}

BestMove find_best_2opt_move_cycle(const TSPProblem& p,
                                  const TSPSolveOptions& opt,
                                  const std::vector<Index>& order,
                                  TimeBudget& budget,
                                  std::uint64_t iteration_limit,
                                  std::uint64_t iterations_done,
                                  std::uint64_t& evaluated) {
    BestMove best;
    const std::size_t n = order.size();
    if (n < 4) return best;

    for (std::size_t i = 0; i + 2 < n; ++i) {
        if (budget.expired()) return best;
        if (iteration_limit != 0 && iterations_done >= iteration_limit) return best;

        const Index a = order[i];
        const Index b = order[i + 1];

        for (std::size_t k = i + 2; k < n; ++k) {
            if (budget.expired()) return best;

            if (i == 0 && k == n - 1) continue;

            const Index c = order[k];
            const Index d = order[(k + 1) % n];

            Cost ab, cd, ac, bd;
            if (!dist_ok(p, opt, a, b, ab)) continue;
            if (!dist_ok(p, opt, c, d, cd)) continue;
            if (!dist_ok(p, opt, a, c, ac)) continue;
            if (!dist_ok(p, opt, b, d, bd)) continue;

            evaluated += 4;

            const Cost oldc = ab + cd;
            const Cost newc = ac + bd;
            if (!is_finite(oldc) || !is_finite(newc)) continue;

            const Cost delta = newc - oldc;
            if (!is_improving_delta(delta)) continue;

            if (!best.found || strictly_better(delta, best.delta)) {
                best.found = true;
                best.i = i;
                best.k = k;
                best.delta = delta;
            }
        }
    }

    return best;
}

BestMove find_best_2opt_move_path(const TSPProblem& p,
                                 const TSPSolveOptions& opt,
                                 const std::vector<Index>& order,
                                 TimeBudget& budget,
                                 std::uint64_t iteration_limit,
                                 std::uint64_t iterations_done,
                                 std::uint64_t& evaluated) {
    BestMove best;
    const std::size_t n = order.size();
    if (n < 4) return best;

    const auto maxv = std::numeric_limits<Index>::max();
    const bool end_fixed = (p.type == TourType::Path) && (p.end != maxv);

    const std::size_t max_k = end_fixed ? (n - 2) : (n - 1);

    for (std::size_t i = 0; i + 2 < n; ++i) {
        if (budget.expired()) return best;
        if (iteration_limit != 0 && iterations_done >= iteration_limit) return best;

        if (i + 2 > max_k) break;

        const Index a = order[i];
        const Index b = order[i + 1];

        for (std::size_t k = i + 2; k <= max_k; ++k) {
            if (budget.expired()) return best;

            const Index c = order[k];
            const bool has_d = (k + 1 < n);
            const Index d = has_d ? order[k + 1] : 0;

            Cost ab, ac;
            if (!dist_ok(p, opt, a, b, ab)) continue;
            if (!dist_ok(p, opt, a, c, ac)) continue;

            evaluated += 2;

            Cost oldc = ab;
            Cost newc = ac;

            if (has_d) {
                Cost cd, bd;
                if (!dist_ok(p, opt, c, d, cd)) continue;
                if (!dist_ok(p, opt, b, d, bd)) continue;
                evaluated += 2;
                oldc += cd;
                newc += bd;
            }

            if (!is_finite(oldc) || !is_finite(newc)) continue;

            const Cost delta = newc - oldc;
            if (!is_improving_delta(delta)) continue;

            if (!best.found || strictly_better(delta, best.delta)) {
                best.found = true;
                best.i = i;
                best.k = k;
                best.delta = delta;
            }
        }
    }

    return best;
}

inline void apply_2opt(std::vector<Index>& order, std::size_t i, std::size_t k) {
    std::reverse(order.begin() + static_cast<std::ptrdiff_t>(i + 1),
                 order.begin() + static_cast<std::ptrdiff_t>(k + 1));
}

}  // namespace

class TwoOptSolver final : public TSPSolver {
public:
    std::string name() const override { return "two_opt"; }

    TSPSolution solve(const TSPProblem& problem, const TSPSolveOptions& options) override {
        const auto t0 = std::chrono::steady_clock::now();
        TimeBudget budget(options.time_limit_ms);

        const Status basic = problem.validate_basic();
        if (basic != Status::Ok) return make_failure(basic, name(), "invalid problem");

        const Index n = problem.n;
        if (n == 0) return make_failure(Status::InvalidArgument, name(), "empty instance");

        std::vector<Index> order;
        std::uint64_t evaluated = 0;

        if (n == 1) {
            order = {0};
        } else {
            const Status init_st = build_initial_nn(problem, options, budget, order, evaluated);
            if (init_st != Status::Ok) {
                TSPSolution out = make_failure(init_st, name(), "failed to build initial tour");
                return out;
            }
        }

        Status cost_st = Status::Ok;
        Cost current_cost = compute_cost(problem, order, options, &cost_st);
        if (cost_st != Status::Ok) return make_failure(cost_st, name(), "invalid initial tour");

        std::uint64_t improve_steps = 0;
        std::uint64_t evaluated_moves = 0;

        Status final_status = Status::Ok;
        std::string final_message;

        while (true) {
            if (budget.expired()) {
                final_status = Status::TimeLimit;
                final_message = "time limit";
                break;
            }
            if (options.iteration_limit != 0 && improve_steps >= options.iteration_limit) {
                final_status = Status::IterationLimit;
                final_message = "iteration limit";
                break;
            }

            BestMove best;
            if (problem.type == TourType::Cycle) {
                best = find_best_2opt_move_cycle(problem, options, order, budget,
                                                options.iteration_limit, improve_steps, evaluated_moves);
            } else {
                best = find_best_2opt_move_path(problem, options, order, budget,
                                               options.iteration_limit, improve_steps, evaluated_moves);
            }

            if (!best.found) break;

            apply_2opt(order, best.i, best.k);
            if (is_finite(current_cost)) current_cost += best.delta;

            ++improve_steps;

            if ((improve_steps & 63ULL) == 0ULL) {
                Status st = Status::Ok;
                Cost c = compute_cost(problem, order, options, &st);
                if (st != Status::Ok) return make_failure(st, name(), "tour became invalid");
                current_cost = c;
            }
        }

        const auto t1 = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

        TSPSolution out = finalize_solution(problem, options, std::move(order), name(),
                                            static_cast<std::uint64_t>(elapsed),
                                            improve_steps, evaluated_moves);
        if (out.status == Status::Ok && final_status != Status::Ok) {
            out.status = final_status;
            out.message = std::move(final_message);
        }
        return out;
    }
};

std::unique_ptr<TSPSolver> make_two_opt_solver() {
    return std::make_unique<TwoOptSolver>();
}

}  // namespace mdmtsp::tsp