#include "tsp_solver.hpp"

#include <memory>
#include <cmath>
#include <chrono>
#include <utility>
#include <algorithm>

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

inline bool is_improving(Cost new_cost, Cost old_cost) noexcept {
    const Cost scale = std::max<Cost>(1.0, std::max(std::abs(new_cost), std::abs(old_cost)));
    const Cost eps = 1e-12 * scale;
    return new_cost + eps < old_cost;
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

enum class MoveType : std::uint8_t {
    RevS1 = 0,
    RevS2 = 1,
    RevS12 = 2,
    Swap = 3,
    SwapRevS1 = 4,
    SwapRevS2 = 5
};

inline void apply_move(std::vector<Index>& order, std::size_t i, std::size_t j, std::size_t k, MoveType mt) {
    auto it1 = order.begin() + static_cast<std::ptrdiff_t>(i + 1);
    auto it2 = order.begin() + static_cast<std::ptrdiff_t>(j + 1);
    auto it3 = order.begin() + static_cast<std::ptrdiff_t>(k + 1);

    switch (mt) {
        case MoveType::RevS1:
            std::reverse(it1, it2);
            break;
        case MoveType::RevS2:
            std::reverse(it2, it3);
            break;
        case MoveType::RevS12:
            std::reverse(it1, it3);
            break;
        case MoveType::Swap:
            std::rotate(it1, it2, it3);
            break;
        case MoveType::SwapRevS1:
            std::reverse(it1, it2);
            std::rotate(it1, it2, it3);
            break;
        case MoveType::SwapRevS2:
            std::reverse(it2, it3);
            std::rotate(it1, it2, it3);
            break;
    }
}

inline void undo_move(std::vector<Index>& order, std::size_t i, std::size_t j, std::size_t k, MoveType mt) {
    auto it1 = order.begin() + static_cast<std::ptrdiff_t>(i + 1);
    auto it2 = order.begin() + static_cast<std::ptrdiff_t>(j + 1);
    auto it3 = order.begin() + static_cast<std::ptrdiff_t>(k + 1);

    const std::ptrdiff_t len1 = it2 - it1;
    const std::ptrdiff_t len2 = it3 - it2;

    switch (mt) {
        case MoveType::RevS1:
            std::reverse(it1, it2);
            break;
        case MoveType::RevS2:
            std::reverse(it2, it3);
            break;
        case MoveType::RevS12:
            std::reverse(it1, it3);
            break;
        case MoveType::Swap: {
            auto mid = it1 + len2;
            std::rotate(it1, mid, it3);
            break;
        }
        case MoveType::SwapRevS1: {
            auto mid = it1 + len2;
            std::rotate(it1, mid, it3);
            std::reverse(it1, it2);
            break;
        }
        case MoveType::SwapRevS2: {
            auto mid = it1 + len2;
            std::rotate(it1, mid, it3);
            std::reverse(it2, it3);
            break;
        }
    }
}

inline bool estimate_delta(const TSPProblem& p,
                           const TSPSolveOptions& opt,
                           const std::vector<Index>& order,
                           std::size_t i, std::size_t j, std::size_t k,
                           MoveType mt,
                           Cost& out_delta,
                           std::uint64_t& evaluated) {
    const Index a = order[i];
    const Index b = order[i + 1];
    const Index c = order[j];
    const Index d = order[j + 1];
    const Index e = order[k];
    const Index f = order[k + 1];

    Cost ab, cd, ef;
    if (!dist_ok(p, opt, a, b, ab)) return false;
    if (!dist_ok(p, opt, c, d, cd)) return false;
    if (!dist_ok(p, opt, e, f, ef)) return false;
    evaluated += 3;

    Cost new_sum = std::numeric_limits<Cost>::infinity();

    auto need = [&](Index u, Index v, Cost& x) -> bool {
        if (!dist_ok(p, opt, u, v, x)) return false;
        ++evaluated;
        return true;
    };

    Cost x1, x2, x3;

    switch (mt) {
        case MoveType::RevS1:
            if (!need(a, c, x1)) return false;
            if (!need(b, d, x2)) return false;
            new_sum = x1 + x2 + ef;
            break;
        case MoveType::RevS2:
            if (!need(c, e, x1)) return false;
            if (!need(d, f, x2)) return false;
            new_sum = ab + x1 + x2;
            break;
        case MoveType::RevS12:
            if (!need(a, e, x1)) return false;
            if (!need(d, c, x2)) return false;
            if (!need(b, f, x3)) return false;
            new_sum = x1 + x2 + x3;
            break;
        case MoveType::Swap:
            if (!need(a, d, x1)) return false;
            if (!need(e, b, x2)) return false;
            if (!need(c, f, x3)) return false;
            new_sum = x1 + x2 + x3;
            break;
        case MoveType::SwapRevS1:
            if (!need(a, d, x1)) return false;
            if (!need(e, c, x2)) return false;
            if (!need(b, f, x3)) return false;
            new_sum = x1 + x2 + x3;
            break;
        case MoveType::SwapRevS2:
            if (!need(a, e, x1)) return false;
            if (!need(d, b, x2)) return false;
            if (!need(c, f, x3)) return false;
            new_sum = x1 + x2 + x3;
            break;
    }

    const Cost old_sum = ab + cd + ef;
    if (!is_finite(new_sum) || !is_finite(old_sum)) return false;

    out_delta = new_sum - old_sum;
    return true;
}

inline std::pair<std::size_t, std::size_t> spans_for_n(std::size_t n) noexcept {
    if (n <= 150) return {n, n};
    if (n <= 400) return {96, 96};
    return {64, 64};
}

}  // namespace

class ThreeOptSolver final : public TSPSolver {
public:
    std::string name() const override { return "three_opt"; }

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
            if (init_st != Status::Ok) return make_failure(init_st, name(), "failed to build initial tour");
        }

        Status cost_st = Status::Ok;
        Cost current_cost = compute_cost(problem, order, options, &cost_st);
        if (cost_st != Status::Ok) return make_failure(cost_st, name(), "invalid initial tour");

        std::uint64_t accepted = 0;
        std::uint64_t evaluated_moves = 0;

        Status final_status = Status::Ok;
        std::string final_message;

        if (order.size() < 6) {
            const auto t1 = std::chrono::steady_clock::now();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
            TSPSolution out = finalize_solution(problem, options, std::move(order), name(),
                                                static_cast<std::uint64_t>(elapsed),
                                                accepted, evaluated + evaluated_moves);
            return out;
        }

        XorShift64 rng(options.seed ^ 0xA5A5A5A5A5A5A5A5ULL);
        const bool use_rng = options.seed != 0;

        while (true) {
            if (budget.expired()) {
                final_status = Status::TimeLimit;
                final_message = "time limit";
                break;
            }
            if (options.iteration_limit != 0 && accepted >= options.iteration_limit) {
                final_status = Status::IterationLimit;
                final_message = "iteration limit";
                break;
            }

            const std::size_t N = order.size();
            const auto [j_span, k_span] = spans_for_n(N);

            bool improved = false;

            const std::size_t i_start = use_rng ? (static_cast<std::size_t>(rng.next() % (N - 4))) : 0;

            for (std::size_t ii = 0; ii + 3 < N; ++ii) {
                if (budget.expired()) { final_status = Status::TimeLimit; final_message = "time limit"; break; }
                if (options.iteration_limit != 0 && accepted >= options.iteration_limit) { final_status = Status::IterationLimit; final_message = "iteration limit"; break; }

                const std::size_t i = (ii + i_start) % (N - 3);

                const std::size_t j_min = i + 1;
                const std::size_t j_max = std::min<std::size_t>(N - 3, i + j_span);

                for (std::size_t j = j_min; j <= j_max; ++j) {
                    if (budget.expired()) { final_status = Status::TimeLimit; final_message = "time limit"; break; }

                    const std::size_t k_min = j + 1;
                    const std::size_t k_max = std::min<std::size_t>(N - 2, j + k_span);

                    for (std::size_t k = k_min; k <= k_max; ++k) {
                        if (budget.expired()) { final_status = Status::TimeLimit; final_message = "time limit"; break; }
                        if (k + 1 >= N) break;

                        const MoveType moves[] = {
                            MoveType::RevS1, MoveType::RevS2, MoveType::RevS12,
                            MoveType::Swap, MoveType::SwapRevS1, MoveType::SwapRevS2
                        };

                        for (MoveType mt : moves) {
                            if (budget.expired()) { final_status = Status::TimeLimit; final_message = "time limit"; break; }

                            Cost est_delta = 0.0;
                            if (!estimate_delta(problem, options, order, i, j, k, mt, est_delta, evaluated_moves)) continue;

                            const Cost scale = std::max<Cost>(1.0, std::abs(current_cost));
                            const Cost eps = 1e-12 * scale;
                            if (!(est_delta + eps < 0.0)) continue;

                            apply_move(order, i, j, k, mt);

                            Status st = Status::Ok;
                            Cost new_cost = compute_cost(problem, order, options, &st);

                            if (st == Status::Ok && is_improving(new_cost, current_cost)) {
                                current_cost = new_cost;
                                ++accepted;
                                improved = true;

                                if ((accepted & 31ULL) == 0ULL) {
                                    Status st2 = Status::Ok;
                                    Cost c2 = compute_cost(problem, order, options, &st2);
                                    if (st2 == Status::Ok) current_cost = c2;
                                }
                                break;
                            }

                            undo_move(order, i, j, k, mt);
                        }

                        if (improved) break;
                    }

                    if (improved) break;
                }

                if (improved) break;
                if (final_status != Status::Ok) break;
            }

            if (!improved) break;
            if (final_status != Status::Ok && (final_status == Status::TimeLimit || final_status == Status::IterationLimit)) break;
        }

        const auto t1 = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

        TSPSolution out = finalize_solution(problem, options, std::move(order), name(),
                                            static_cast<std::uint64_t>(elapsed),
                                            accepted, evaluated + evaluated_moves);

        if (out.status == Status::Ok && final_status != Status::Ok) {
            out.status = final_status;
            out.message = std::move(final_message);
        }
        return out;
    }
};

std::unique_ptr<TSPSolver> make_three_opt_solver() {
    return std::make_unique<ThreeOptSolver>();
}

}  // namespace mdmtsp::tsp