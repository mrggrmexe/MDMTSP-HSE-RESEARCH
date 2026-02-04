#include "mdmtsp_solver.hpp"

#include <memory>
#include <vector>
#include <algorithm>
#include <limits>
#include <cstdint>
#include <chrono>
#include <string>

namespace mdmtsp::mdmtsp {

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

inline Index pick_tie(Index cur, Index cand, std::uint64_t& tie_count, XorShift64& rng, bool use_rng) noexcept {
    ++tie_count;
    if (use_rng) {
        if ((rng.next() % tie_count) == 0) return cand;
        return cur;
    }
    return std::min(cur, cand);
}

inline bool dist_ok_count(const MDMTSPProblem& p, const SolveOptions& opt,
                          Index u, Index v, Cost& out, std::uint64_t& eval_edges) noexcept {
    if (!dist_ok(p, opt, u, v, out)) return false;
    ++eval_edges;
    return true;
}

inline std::vector<Index> nearest_neighbor_order(const MDMTSPProblem& p,
                                                 const SolveOptions& opt,
                                                 Index depot,
                                                 const std::vector<Index>& customers,
                                                 TimeBudget& budget,
                                                 std::uint64_t& eval_edges,
                                                 std::uint64_t iter_limit,
                                                 std::uint64_t& iters,
                                                 XorShift64& rng,
                                                 bool use_rng,
                                                 Status& out_status) {
    out_status = Status::Ok;

    if (customers.empty()) return {};

    const std::size_t n = customers.size();
    std::vector<Index> order;
    order.reserve(n);

    std::vector<std::uint8_t> used(n, 0);

    Index current = depot;

    for (std::size_t step = 0; step < n; ++step) {
        if (budget.expired()) {
            out_status = Status::TimeLimit;
            return {};
        }
        if (iter_limit != 0 && iters >= iter_limit) {
            out_status = Status::IterationLimit;
            return {};
        }

        bool found = false;
        Cost best_c = std::numeric_limits<Cost>::infinity();
        std::size_t best_idx = 0;
        std::uint64_t tie_count = 0;

        for (std::size_t i = 0; i < n; ++i) {
            if (used[i]) continue;
            Cost c;
            if (!dist_ok_count(p, opt, current, customers[i], c, eval_edges)) continue;

            if (!found || c + 1e-12 * std::max<Cost>(1.0, std::abs(best_c)) < best_c) {
                found = true;
                best_c = c;
                best_idx = i;
                tie_count = 1;
            } else if (approx_equal(c, best_c)) {
                const Index chosen = pick_tie(customers[best_idx], customers[i], tie_count, rng, use_rng);
                if (chosen == customers[i]) best_idx = i;
            }
        }

        if (!found) {
            out_status = Status::InvalidDistance;
            return {};
        }

        used[best_idx] = 1;
        const Index nxt = customers[best_idx];
        order.push_back(nxt);
        current = nxt;
        ++iters;
    }

    return order;
}

}  // namespace

class NearestDepotSolver final : public MDMTSPResolver {
public:
    std::string name() const override { return "nearest_depot"; }

    Solution solve(const MDMTSPProblem& p, const SolveOptions& opt) override {
        const auto t0 = std::chrono::steady_clock::now();
        TimeBudget budget(opt.time_limit_ms);

        const Status basic = p.validate_basic();
        if (basic != Status::Ok) return make_failure(basic, name(), "invalid problem");

        const Index D = p.num_depots;
        const Index C = p.num_customers;
        const Index M = p.num_agents;
        const Index base = p.num_depots;
        const Index N = p.num_nodes();

        if (N != D + C) return make_failure(Status::InvalidArgument, name(), "inconsistent sizes");
        if (M == 0 || D == 0) return make_failure(Status::InvalidArgument, name(), "invalid agents/depots");

        if (p.closure != RouteClosure::Closed) {
            return make_failure(Status::NotImplemented, name(), "only closed routes supported");
        }

        std::uint64_t eval_edges = 0;
        std::uint64_t iters = 0;

        XorShift64 rng(opt.seed ^ (static_cast<std::uint64_t>(N) << 1) ^ 0xD1B54A32D192ED03ULL);
        const bool use_rng = (opt.seed != 0);

        std::vector<std::vector<Index>> depot_customers(static_cast<std::size_t>(D));
        depot_customers.shrink_to_fit();
        depot_customers.resize(static_cast<std::size_t>(D));

        for (Index cust = base; cust < N; ++cust) {
            if (budget.expired()) return make_failure(Status::TimeLimit, name(), "time limit");
            if (!p.customer_index(cust)) return make_failure(Status::InvalidArgument, name(), "bad customer index");

            bool found = false;
            Cost best = std::numeric_limits<Cost>::infinity();
            Index best_depot = 0;
            std::uint64_t tie_count = 0;

            for (Index dep = 0; dep < D; ++dep) {
                Cost c;
                if (!dist_ok_count(p, opt, dep, cust, c, eval_edges)) continue;

                if (!found || c + 1e-12 * std::max<Cost>(1.0, std::abs(best)) < best) {
                    found = true;
                    best = c;
                    best_depot = dep;
                    tie_count = 1;
                } else if (approx_equal(c, best)) {
                    const Index chosen = pick_tie(best_depot, dep, tie_count, rng, use_rng);
                    best_depot = chosen;
                }
            }

            if (!found) return make_failure(Status::InvalidDistance, name(), "no finite depot->customer distance");

            depot_customers[static_cast<std::size_t>(best_depot)].push_back(cust);
            ++iters;
            if (opt.iteration_limit != 0 && iters >= opt.iteration_limit)
                return make_failure(Status::IterationLimit, name(), "iteration limit");
        }

        std::vector<Index> agent_depot(static_cast<std::size_t>(M), 0);
        for (Index a = 0; a < M; ++a) agent_depot[static_cast<std::size_t>(a)] = static_cast<Index>(a % D);

        std::vector<std::vector<Index>> agent_customers(static_cast<std::size_t>(M));
        std::vector<std::size_t> agent_load(static_cast<std::size_t>(M), 0);

        for (Index dep = 0; dep < D; ++dep) {
            if (budget.expired()) return make_failure(Status::TimeLimit, name(), "time limit");

            auto& vec = depot_customers[static_cast<std::size_t>(dep)];
            if (vec.empty()) continue;

            std::stable_sort(vec.begin(), vec.end(), [&](Index a, Index b) {
                Cost da = std::numeric_limits<Cost>::infinity();
                Cost db = std::numeric_limits<Cost>::infinity();
                Cost tmp;
                if (dist_ok(p, opt, dep, a, tmp)) da = tmp;
                if (dist_ok(p, opt, dep, b, tmp)) db = tmp;
                if (approx_equal(da, db)) return a < b;
                return da < db;
            });

            std::vector<Index> agents_for_dep;
            agents_for_dep.reserve(static_cast<std::size_t>(M));
            for (Index a = 0; a < M; ++a) {
                if (agent_depot[static_cast<std::size_t>(a)] == dep) agents_for_dep.push_back(a);
            }
            if (agents_for_dep.empty()) {
                agents_for_dep.push_back(dep % M);
                agent_depot[static_cast<std::size_t>(agents_for_dep[0])] = dep;
            }

            for (Index cust : vec) {
                if (budget.expired()) return make_failure(Status::TimeLimit, name(), "time limit");
                if (opt.iteration_limit != 0 && iters >= opt.iteration_limit)
                    return make_failure(Status::IterationLimit, name(), "iteration limit");

                Index best_agent = agents_for_dep[0];
                std::size_t best_load = agent_load[static_cast<std::size_t>(best_agent)];
                for (Index a : agents_for_dep) {
                    const std::size_t l = agent_load[static_cast<std::size_t>(a)];
                    if (l < best_load) {
                        best_load = l;
                        best_agent = a;
                    } else if (l == best_load && use_rng) {
                        if ((rng.next() & 1ULL) == 0ULL) best_agent = a;
                    }
                }

                agent_customers[static_cast<std::size_t>(best_agent)].push_back(cust);
                ++agent_load[static_cast<std::size_t>(best_agent)];
                ++iters;
            }
        }

        std::vector<Route> routes;
        routes.resize(static_cast<std::size_t>(M));

        for (Index a = 0; a < M; ++a) {
            if (budget.expired()) return make_failure(Status::TimeLimit, name(), "time limit");

            Route rt;
            const Index dep = agent_depot[static_cast<std::size_t>(a)];
            rt.start_depot = dep;
            rt.end_depot = dep;

            const auto& assigned = agent_customers[static_cast<std::size_t>(a)];

            if (!assigned.empty()) {
                Status st = Status::Ok;
                rt.customers = nearest_neighbor_order(
                    p, opt, dep, assigned, budget, eval_edges,
                    opt.iteration_limit, iters, rng, use_rng, st
                );
                if (st != Status::Ok) {
                    return make_failure(st, name(), "failed building route order");
                }
            } else {
                if (!opt.allow_empty_routes) {
                    return make_failure(Status::Infeasible, name(), "empty routes not allowed");
                }
            }

            routes[static_cast<std::size_t>(a)] = std::move(rt);
        }

        const auto t1 = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

        return finalize_solution(p, opt, std::move(routes), name(),
                                 static_cast<std::uint64_t>(elapsed),
                                 iters, eval_edges);
    }
};

std::unique_ptr<MDMTSPResolver> make_nearest_depot_solver() {
    return std::make_unique<NearestDepotSolver>();
}

}  // namespace mdmtsp::mdmtsp