#ifndef MDMTSP_TSP_SOLVER_HPP
#define MDMTSP_TSP_SOLVER_HPP

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <limits>
#include <functional>
#include <chrono>
#include <utility>
#include <type_traits>
#include <algorithm>

namespace mdmtsp::tsp {

using Index = std::uint32_t;
using Cost  = double;

static_assert(std::is_unsigned<Index>::value, "Index must be unsigned");

enum class TourType : std::uint8_t {
    Cycle = 0,
    Path  = 1
};

enum class Status : std::uint8_t {
    Ok = 0,
    InvalidArgument,
    InvalidDistance,
    Infeasible,
    TimeLimit,
    IterationLimit,
    NotImplemented,
    InternalError
};

struct DistanceFunction {
    using Fn = std::function<Cost(Index, Index)>;
    Fn fn;

    DistanceFunction() = default;
    explicit DistanceFunction(Fn f) : fn(std::move(f)) {}

    Cost operator()(Index i, Index j) const {
        return fn ? fn(i, j) : std::numeric_limits<Cost>::quiet_NaN();
    }

    explicit operator bool() const noexcept { return static_cast<bool>(fn); }
};

struct TSPProblem {
    Index n = 0;
    TourType type = TourType::Cycle;
    Index start = std::numeric_limits<Index>::max();
    Index end   = std::numeric_limits<Index>::max();
    DistanceFunction dist;

    bool has_start() const noexcept { return start != std::numeric_limits<Index>::max(); }
    bool has_end()   const noexcept { return end   != std::numeric_limits<Index>::max(); }

    Status validate_basic() const noexcept {
        if (n == 0) return Status::InvalidArgument;
        if (!dist)  return Status::InvalidArgument;

        if (type == TourType::Cycle) {
            if (has_end()) return Status::InvalidArgument;
        } else {
            if (has_start() && start >= n) return Status::InvalidArgument;
            if (has_end()   && end   >= n) return Status::InvalidArgument;
            if (has_start() && has_end() && start == end && n > 1) return Status::InvalidArgument;
        }
        if (has_start() && start >= n) return Status::InvalidArgument;
        if (type == TourType::Cycle && has_start() && start >= n) return Status::InvalidArgument;

        return Status::Ok;
    }
};

struct TSPSolveOptions {
    std::uint64_t seed = 0;
    std::uint64_t time_limit_ms = 0;
    std::uint64_t iteration_limit = 0;
    bool require_finite_costs = true;
    bool require_non_negative_costs = false;
};

struct TSPSolveStats {
    std::uint64_t iterations = 0;
    std::uint64_t evaluated_moves = 0;
    std::uint64_t elapsed_ms = 0;
    std::string solver_name;
};

struct TSPSolution {
    Status status = Status::InternalError;
    std::vector<Index> order;
    Cost cost = std::numeric_limits<Cost>::infinity();
    TSPSolveStats stats;
    std::string message;

    explicit operator bool() const noexcept { return status == Status::Ok; }
};

inline bool is_finite(Cost x) noexcept {
    return x == x && x != std::numeric_limits<Cost>::infinity() && x != -std::numeric_limits<Cost>::infinity();
}

inline Status validate_order(Index n, TourType type, const std::vector<Index>& order, Index start, Index end) noexcept {
    if (n == 0) return Status::InvalidArgument;
    if (order.size() != static_cast<std::size_t>(n)) return Status::InvalidArgument;

    std::vector<std::uint8_t> seen(static_cast<std::size_t>(n), 0);
    for (Index v : order) {
        if (v >= n) return Status::InvalidArgument;
        auto& s = seen[static_cast<std::size_t>(v)];
        if (s) return Status::InvalidArgument;
        s = 1;
    }

    const auto maxv = std::numeric_limits<Index>::max();
    const bool has_start = (start != maxv);
    const bool has_end   = (end   != maxv);

    if (type == TourType::Cycle) {
        if (has_end) return Status::InvalidArgument;
        if (has_start && order.front() != start) return Status::InvalidArgument;
    } else {
        if (has_start && order.front() != start) return Status::InvalidArgument;
        if (has_end   && order.back()  != end)   return Status::InvalidArgument;
        if (has_start && has_end && start == end && n > 1) return Status::InvalidArgument;
    }

    return Status::Ok;
}

inline Cost compute_cost(const TSPProblem& p, const std::vector<Index>& order, const TSPSolveOptions& opt, Status* out_status = nullptr) noexcept {
    if (out_status) *out_status = Status::Ok;

    const auto basic = p.validate_basic();
    if (basic != Status::Ok) {
        if (out_status) *out_status = basic;
        return std::numeric_limits<Cost>::infinity();
    }

    const auto order_ok = validate_order(p.n, p.type, order, p.start, p.end);
    if (order_ok != Status::Ok) {
        if (out_status) *out_status = order_ok;
        return std::numeric_limits<Cost>::infinity();
    }

    Cost total = 0.0;
    const std::size_t N = order.size();
    for (std::size_t i = 0; i + 1 < N; ++i) {
        const Cost c = p.dist(order[static_cast<std::size_t>(i)], order[static_cast<std::size_t>(i + 1)]);
        if (!is_finite(c)) {
            if (out_status) *out_status = Status::InvalidDistance;
            return std::numeric_limits<Cost>::infinity();
        }
        if (opt.require_non_negative_costs && c < 0) {
            if (out_status) *out_status = Status::InvalidDistance;
            return std::numeric_limits<Cost>::infinity();
        }
        total += c;
        if (!is_finite(total)) {
            if (out_status) *out_status = Status::InvalidDistance;
            return std::numeric_limits<Cost>::infinity();
        }
    }

    if (p.type == TourType::Cycle) {
        const Cost c = p.dist(order.back(), order.front());
        if (!is_finite(c)) {
            if (out_status) *out_status = Status::InvalidDistance;
            return std::numeric_limits<Cost>::infinity();
        }
        if (opt.require_non_negative_costs && c < 0) {
            if (out_status) *out_status = Status::InvalidDistance;
            return std::numeric_limits<Cost>::infinity();
        }
        total += c;
        if (!is_finite(total)) {
            if (out_status) *out_status = Status::InvalidDistance;
            return std::numeric_limits<Cost>::infinity();
        }
    }

    if (opt.require_finite_costs && !is_finite(total)) {
        if (out_status) *out_status = Status::InvalidDistance;
        return std::numeric_limits<Cost>::infinity();
    }

    return total;
}

class TSPSolver {
public:
    virtual ~TSPSolver() = default;

    virtual std::string name() const = 0;

    virtual TSPSolution solve(const TSPProblem& problem, const TSPSolveOptions& options) = 0;
};

class TimeBudget {
public:
    explicit TimeBudget(std::uint64_t limit_ms)
        : limit_ms_(limit_ms),
          start_(std::chrono::steady_clock::now()) {}

    bool has_limit() const noexcept { return limit_ms_ != 0; }

    std::uint64_t elapsed_ms() const noexcept {
        const auto now = std::chrono::steady_clock::now();
        const auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_);
        return static_cast<std::uint64_t>(diff.count());
    }

    bool expired() const noexcept {
        return has_limit() && elapsed_ms() >= limit_ms_;
    }

private:
    std::uint64_t limit_ms_ = 0;
    std::chrono::steady_clock::time_point start_;
};

inline TSPSolution make_failure(Status s, std::string solver_name, std::string message = {}) {
    TSPSolution out;
    out.status = s;
    out.stats.solver_name = std::move(solver_name);
    out.message = std::move(message);
    out.cost = std::numeric_limits<Cost>::infinity();
    out.order.clear();
    return out;
}

inline TSPSolution finalize_solution(const TSPProblem& p, const TSPSolveOptions& opt, std::vector<Index> order, std::string solver_name, std::uint64_t elapsed_ms, std::uint64_t iterations = 0, std::uint64_t evaluated_moves = 0) {
    TSPSolution out;
    out.stats.solver_name = std::move(solver_name);
    out.stats.elapsed_ms = elapsed_ms;
    out.stats.iterations = iterations;
    out.stats.evaluated_moves = evaluated_moves;
    out.order = std::move(order);

    Status st = Status::Ok;
    const Cost c = compute_cost(p, out.order, opt, &st);
    out.status = st;
    out.cost = c;
    if (st != Status::Ok) {
        out.message = "invalid solution";
        out.order.clear();
        out.cost = std::numeric_limits<Cost>::infinity();
    }
    return out;
}

}  // namespace mdmtsp::tsp

#endif
