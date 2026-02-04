#ifndef MDMTSP_MDMTSP_SOLVER_HPP
#define MDMTSP_MDMTSP_SOLVER_HPP

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
#include <cmath>

namespace mdmtsp::mdmtsp {

using Index = std::uint32_t;
using Cost  = double;

static_assert(std::is_unsigned<Index>::value, "Index must be unsigned");

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

enum class RouteClosure : std::uint8_t {
    Open  = 0,
    Closed = 1
};

enum class ObjectiveType : std::uint8_t {
    MinTotal = 0,
    MinMaxRoute = 1,
    MinTotalPlusBalance = 2
};

struct Objective {
    ObjectiveType type = ObjectiveType::MinTotal;
    double balance_lambda = 0.0;

    bool valid() const noexcept {
        if (type != ObjectiveType::MinTotalPlusBalance) return true;
        return std::isfinite(balance_lambda) && balance_lambda >= 0.0;
    }
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

inline bool is_finite(Cost x) noexcept {
    return x == x && x != std::numeric_limits<Cost>::infinity() && x != -std::numeric_limits<Cost>::infinity();
}

struct MDMTSPProblem {
    Index num_depots = 0;
    Index num_customers = 0;
    Index num_agents = 0;

    RouteClosure closure = RouteClosure::Closed;
    Objective objective{};

    DistanceFunction dist;

    Index num_nodes() const noexcept { return num_depots + num_customers; }

    bool depot_index(Index v) const noexcept { return v < num_depots; }
    bool customer_index(Index v) const noexcept { return v >= num_depots && v < num_nodes(); }

    Status validate_basic() const noexcept {
        if (num_depots == 0) return Status::InvalidArgument;
        if (num_agents == 0) return Status::InvalidArgument;
        if (!dist) return Status::InvalidArgument;
        if (!objective.valid()) return Status::InvalidArgument;
        if (num_nodes() < num_depots) return Status::InvalidArgument;
        return Status::Ok;
    }
};

struct SolveOptions {
    std::uint64_t seed = 0;
    std::uint64_t time_limit_ms = 0;
    std::uint64_t iteration_limit = 0;

    bool require_finite_costs = true;
    bool require_non_negative_costs = false;

    bool allow_empty_routes = true;
};

struct SolveStats {
    std::uint64_t iterations = 0;
    std::uint64_t evaluated_edges = 0;
    std::uint64_t elapsed_ms = 0;
    std::string solver_name;
};

struct Route {
    Index start_depot = std::numeric_limits<Index>::max();
    Index end_depot   = std::numeric_limits<Index>::max();
    std::vector<Index> customers;

    bool has_start() const noexcept { return start_depot != std::numeric_limits<Index>::max(); }
    bool has_end() const noexcept { return end_depot   != std::numeric_limits<Index>::max(); }
};

struct Metrics {
    Cost total_cost = std::numeric_limits<Cost>::infinity();
    Cost max_route_cost = std::numeric_limits<Cost>::infinity();
    Cost min_route_cost = std::numeric_limits<Cost>::infinity();
    Cost mean_route_cost = std::numeric_limits<Cost>::infinity();
    Cost std_route_cost = std::numeric_limits<Cost>::infinity();
    Cost imbalance_ratio = std::numeric_limits<Cost>::infinity();
    std::uint32_t used_agents = 0;
};

struct Solution {
    Status status = Status::InternalError;
    std::vector<Route> routes;
    Metrics metrics{};
    SolveStats stats{};
    std::string message;

    explicit operator bool() const noexcept { return status == Status::Ok; }
};

class TimeBudget {
public:
    explicit TimeBudget(std::uint64_t limit_ms)
        : limit_ms_(limit_ms), start_(std::chrono::steady_clock::now()) {}

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

inline Status validate_routes_shape(const MDMTSPProblem& p, const SolveOptions& opt, const std::vector<Route>& routes) noexcept {
    const auto basic = p.validate_basic();
    if (basic != Status::Ok) return basic;

    if (routes.size() != static_cast<std::size_t>(p.num_agents)) return Status::InvalidArgument;

    for (std::size_t r = 0; r < routes.size(); ++r) {
        const Route& rt = routes[r];

        if (!rt.has_start()) return Status::InvalidArgument;
        if (!rt.has_end()) return Status::InvalidArgument;

        if (!p.depot_index(rt.start_depot)) return Status::InvalidArgument;
        if (!p.depot_index(rt.end_depot)) return Status::InvalidArgument;

        if (p.closure == RouteClosure::Closed && rt.start_depot != rt.end_depot) return Status::InvalidArgument;

        if (!opt.allow_empty_routes && rt.customers.empty()) return Status::InvalidArgument;

        for (Index c : rt.customers) {
            if (!p.customer_index(c)) return Status::InvalidArgument;
        }
    }

    return Status::Ok;
}

inline Status validate_customer_coverage(const MDMTSPProblem& p, const std::vector<Route>& routes) noexcept {
    const Index n = p.num_customers;
    const Index base = p.num_depots;

    std::vector<std::uint8_t> seen(static_cast<std::size_t>(n), 0);
    std::size_t count = 0;

    for (const auto& rt : routes) {
        for (Index v : rt.customers) {
            if (v < base) return Status::InvalidArgument;
            Index idx = v - base;
            if (idx >= n) return Status::InvalidArgument;
            auto& s = seen[static_cast<std::size_t>(idx)];
            if (s) return Status::InvalidArgument;
            s = 1;
            ++count;
        }
    }

    if (count != static_cast<std::size_t>(n)) return Status::Infeasible;
    return Status::Ok;
}

inline bool dist_ok(const MDMTSPProblem& p, const SolveOptions& opt, Index u, Index v, Cost& out) noexcept {
    try {
        out = p.dist(u, v);
    } catch (...) {
        return false;
    }
    if (!is_finite(out)) return false;
    if (opt.require_non_negative_costs && out < 0) return false;
    return true;
}

inline Cost route_cost(const MDMTSPProblem& p, const SolveOptions& opt, const Route& rt, Status* out_status = nullptr) noexcept {
    if (out_status) *out_status = Status::Ok;

    if (!rt.has_start() || !rt.has_end()) {
        if (out_status) *out_status = Status::InvalidArgument;
        return std::numeric_limits<Cost>::infinity();
    }
    if (!p.depot_index(rt.start_depot) || !p.depot_index(rt.end_depot)) {
        if (out_status) *out_status = Status::InvalidArgument;
        return std::numeric_limits<Cost>::infinity();
    }

    Cost total = 0.0;

    Index prev = rt.start_depot;
    for (Index c : rt.customers) {
        if (!p.customer_index(c)) {
            if (out_status) *out_status = Status::InvalidArgument;
            return std::numeric_limits<Cost>::infinity();
        }
        Cost w;
        if (!dist_ok(p, opt, prev, c, w)) {
            if (out_status) *out_status = Status::InvalidDistance;
            return std::numeric_limits<Cost>::infinity();
        }
        total += w;
        if (!is_finite(total)) {
            if (out_status) *out_status = Status::InvalidDistance;
            return std::numeric_limits<Cost>::infinity();
        }
        prev = c;
    }

    Cost w;
    if (!dist_ok(p, opt, prev, rt.end_depot, w)) {
        if (out_status) *out_status = Status::InvalidDistance;
        return std::numeric_limits<Cost>::infinity();
    }
    total += w;

    if (opt.require_finite_costs && !is_finite(total)) {
        if (out_status) *out_status = Status::InvalidDistance;
        return std::numeric_limits<Cost>::infinity();
    }

    return total;
}

inline Metrics compute_metrics(const MDMTSPProblem& p,
                              const SolveOptions& opt,
                              const std::vector<Route>& routes,
                              Status* out_status = nullptr) noexcept {
    if (out_status) *out_status = Status::Ok;
    Metrics m{};

    const auto shape_st = validate_routes_shape(p, opt, routes);
    if (shape_st != Status::Ok) {
        if (out_status) *out_status = shape_st;
        return m;
    }

    const auto cov_st = validate_customer_coverage(p, routes);
    if (cov_st != Status::Ok) {
        if (out_status) *out_status = cov_st;
        return m;
    }

    std::vector<Cost> rc;
    rc.reserve(routes.size());

    Cost total = 0.0;
    Cost maxc = -std::numeric_limits<Cost>::infinity();
    Cost minc = std::numeric_limits<Cost>::infinity();
    std::uint32_t used = 0;

    for (const auto& rt : routes) {
        Status st = Status::Ok;
        Cost c = route_cost(p, opt, rt, &st);
        if (st != Status::Ok) {
            if (out_status) *out_status = st;
            return m;
        }
        rc.push_back(c);
        total += c;
        if (!is_finite(total)) {
            if (out_status) *out_status = Status::InvalidDistance;
            return m;
        }
        maxc = std::max(maxc, c);
        minc = std::min(minc, c);
        if (!rt.customers.empty()) ++used;
    }

    const double n = static_cast<double>(rc.size());
    const double mean = (n > 0.0) ? static_cast<double>(total) / n : std::numeric_limits<double>::infinity();

    double var = 0.0;
    if (n > 0.0 && std::isfinite(mean)) {
        for (Cost c : rc) {
            const double d = static_cast<double>(c) - mean;
            var += d * d;
        }
        var /= n;
    } else {
        var = std::numeric_limits<double>::infinity();
    }

    const Cost stdv = std::isfinite(var) ? static_cast<Cost>(std::sqrt(var)) : std::numeric_limits<Cost>::infinity();

    Cost ratio = std::numeric_limits<Cost>::infinity();
    if (minc > 0.0 && is_finite(maxc) && is_finite(minc)) ratio = maxc / minc;

    m.total_cost = total;
    m.max_route_cost = maxc;
    m.min_route_cost = minc;
    m.mean_route_cost = static_cast<Cost>(mean);
    m.std_route_cost = stdv;
    m.imbalance_ratio = ratio;
    m.used_agents = used;

    return m;
}

inline Cost objective_value(const MDMTSPProblem& p, const Metrics& m) noexcept {
    switch (p.objective.type) {
        case ObjectiveType::MinTotal:
            return m.total_cost;
        case ObjectiveType::MinMaxRoute:
            return m.max_route_cost;
        case ObjectiveType::MinTotalPlusBalance:
            if (!is_finite(m.total_cost) || !is_finite(m.std_route_cost)) return std::numeric_limits<Cost>::infinity();
            return m.total_cost + static_cast<Cost>(p.objective.balance_lambda) * m.std_route_cost;
        default:
            return std::numeric_limits<Cost>::infinity();
    }
}

inline Solution make_failure(Status s, std::string solver_name, std::string message = {}) {
    Solution out;
    out.status = s;
    out.stats.solver_name = std::move(solver_name);
    out.message = std::move(message);
    out.routes.clear();
    out.metrics = Metrics{};
    return out;
}

inline Solution finalize_solution(const MDMTSPProblem& p,
                                 const SolveOptions& opt,
                                 std::vector<Route> routes,
                                 std::string solver_name,
                                 std::uint64_t elapsed_ms,
                                 std::uint64_t iterations = 0,
                                 std::uint64_t evaluated_edges = 0) {
    Solution out;
    out.stats.solver_name = std::move(solver_name);
    out.stats.elapsed_ms = elapsed_ms;
    out.stats.iterations = iterations;
    out.stats.evaluated_edges = evaluated_edges;
    out.routes = std::move(routes);

    Status st = Status::Ok;
    out.metrics = compute_metrics(p, opt, out.routes, &st);
    out.status = st;

    if (st != Status::Ok) {
        out.message = "invalid solution";
        out.routes.clear();
        out.metrics = Metrics{};
    }

    return out;
}

class MDMTSPResolver {
public:
    virtual ~MDMTSPResolver() = default;
    virtual std::string name() const = 0;
    virtual Solution solve(const MDMTSPProblem& problem, const SolveOptions& options) = 0;
};

}  // namespace mdmtsp::mdmtsp

#endif
