
#include "mdmtsp_solver.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "../tsp/tsp_solver.hpp"

namespace mdmtsp::tsp {
[[nodiscard]] std::unique_ptr<TSPSolver> make_lkh_solver();
}

namespace mdmtsp {

namespace {

constexpr cost_t kEps = static_cast<cost_t>(1e-9);
constexpr std::size_t kSmallRouteThreshold = 5;
constexpr int kClusteringRefinementPasses = 3;
constexpr int kMaxTwoOptPasses = 6;

[[nodiscard]] bool improving(const cost_t delta) noexcept {
    return delta < -kEps;
}

[[nodiscard]] std::vector<std::size_t> distribute_evenly(
    const std::size_t total,
    const std::size_t buckets
) {
    std::vector<std::size_t> out(buckets, 0);
    if (buckets == 0) {
        return out;
    }

    const std::size_t base = total / buckets;
    const std::size_t rem = total % buckets;

    for (std::size_t i = 0; i < buckets; ++i) {
        out[i] = base + (i < rem ? 1U : 0U);
    }

    return out;
}

[[nodiscard]] std::vector<std::size_t> allocate_salesmen_to_depots_by_pull(
    const MDMTSPInstance& instance,
    const DistanceMatrix& matrix
) {
    const std::size_t depot_count = instance.depot_count();
    std::vector<std::size_t> nearest_pull(depot_count, 0);

    for (std::size_t customer_idx = 0; customer_idx < instance.customer_count(); ++customer_idx) {
        const node_id_t customer_node = instance.customer_node_from_index(customer_idx);

        depot_id_t best_depot = 0;
        cost_t best_distance = std::numeric_limits<cost_t>::infinity();

        for (depot_id_t depot = 0; depot < depot_count; ++depot) {
            const cost_t d = matrix[depot][customer_node];
            if (d < best_distance) {
                best_distance = d;
                best_depot = depot;
            }
        }

        ++nearest_pull[best_depot];
    }

    std::vector<std::size_t> assigned(depot_count, 0);
    std::size_t remaining = instance.salesman_count;

    if (instance.salesman_count >= depot_count) {
        std::fill(assigned.begin(), assigned.end(), 1);
        remaining -= depot_count;
    }

    while (remaining > 0) {
        depot_id_t best_depot = 0;
        double best_score = -std::numeric_limits<double>::infinity();

        for (depot_id_t depot = 0; depot < depot_count; ++depot) {
            const double demand = static_cast<double>(nearest_pull[depot] + 1U);
            const double capacity = static_cast<double>(assigned[depot] + 1U);
            const double score = demand / capacity;

            if (score > best_score + 1e-12) {
                best_score = score;
                best_depot = depot;
            }
        }

        ++assigned[best_depot];
        --remaining;
    }

    return assigned;
}

[[nodiscard]] std::vector<depot_id_t> expand_salesman_to_depot(
    const std::vector<std::size_t>& salesmen_per_depot
) {
    std::vector<depot_id_t> out;
    const std::size_t total = std::accumulate(
        salesmen_per_depot.begin(),
        salesmen_per_depot.end(),
        static_cast<std::size_t>(0)
    );
    out.reserve(total);

    for (depot_id_t depot = 0; depot < salesmen_per_depot.size(); ++depot) {
        for (std::size_t i = 0; i < salesmen_per_depot[depot]; ++i) {
            out.push_back(depot);
        }
    }

    return out;
}

[[nodiscard]] std::vector<std::size_t> derive_customer_caps_from_salesmen(
    const std::size_t customer_count,
    const std::vector<std::size_t>& salesmen_per_depot
) {
    const std::size_t depot_count = salesmen_per_depot.size();
    std::vector<std::size_t> caps(depot_count, 0);

    const std::size_t total_salesmen = std::accumulate(
        salesmen_per_depot.begin(),
        salesmen_per_depot.end(),
        static_cast<std::size_t>(0)
    );
    if (total_salesmen == 0 || depot_count == 0) {
        return caps;
    }

    std::vector<double> fractional(depot_count, 0.0);
    std::size_t assigned = 0;

    for (depot_id_t depot = 0; depot < depot_count; ++depot) {
        const double exact =
            static_cast<double>(customer_count) *
            static_cast<double>(salesmen_per_depot[depot]) /
            static_cast<double>(total_salesmen);

        const auto floored = static_cast<std::size_t>(std::floor(exact));
        caps[depot] = floored;
        fractional[depot] = exact - static_cast<double>(floored);
        assigned += floored;
    }

    while (assigned < customer_count) {
        depot_id_t best_depot = 0;
        double best_frac = -1.0;

        for (depot_id_t depot = 0; depot < depot_count; ++depot) {
            if (fractional[depot] > best_frac + 1e-12) {
                best_frac = fractional[depot];
                best_depot = depot;
            }
        }

        ++caps[best_depot];
        fractional[best_depot] = -1.0;
        ++assigned;
    }

    return caps;
}

struct DepotRanking {
    node_id_t customer_node = invalid_node_id;
    std::vector<depot_id_t> ranked_depots;
    cost_t margin = 0.0;
};

[[nodiscard]] cost_t mean_nearest_depot_distance(
    const MDMTSPInstance& instance,
    const DistanceMatrix& matrix
) {
    if (instance.customer_count() == 0 || instance.depot_count() == 0) {
        return 1.0;
    }

    cost_t total = 0.0;
    for (std::size_t customer_idx = 0; customer_idx < instance.customer_count(); ++customer_idx) {
        const node_id_t customer_node = instance.customer_node_from_index(customer_idx);
        cost_t best = std::numeric_limits<cost_t>::infinity();
        for (depot_id_t depot = 0; depot < instance.depot_count(); ++depot) {
            best = std::min(best, matrix[depot][customer_node]);
        }
        total += best;
    }

    const cost_t mean = total / static_cast<cost_t>(instance.customer_count());
    return std::max<cost_t>(mean, 1.0);
}

[[nodiscard]] cost_t soft_load_penalty(
    const std::size_t load_after,
    const std::size_t target,
    const cost_t distance_scale
) noexcept {
    if (target == 0) {
        return std::numeric_limits<cost_t>::infinity();
    }
    if (load_after <= target) {
        return 0.0;
    }

    const cost_t overload =
        static_cast<cost_t>(load_after - target) / static_cast<cost_t>(target);
    return static_cast<cost_t>(0.65) * distance_scale * overload * overload;
}

[[nodiscard]] cost_t depot_assignment_score(
    const node_id_t customer_node,
    const depot_id_t depot,
    const std::size_t load_after,
    const std::size_t target,
    const DistanceMatrix& matrix,
    const cost_t distance_scale
) noexcept {
    return matrix[depot][customer_node] +
           soft_load_penalty(load_after, target, distance_scale);
}

void refine_depot_assignment_by_soft_relocation(
    const MDMTSPInstance& instance,
    const DistanceMatrix& matrix,
    const std::vector<std::size_t>& depot_customer_targets,
    std::vector<depot_id_t>& assignment
) {
    const std::size_t depot_count = instance.depot_count();
    if (depot_count <= 1 || assignment.empty()) {
        return;
    }

    const cost_t distance_scale = mean_nearest_depot_distance(instance, matrix);
    std::vector<std::size_t> load(depot_count, 0);
    for (const depot_id_t depot : assignment) {
        if (depot < depot_count) {
            ++load[depot];
        }
    }

    for (int pass = 0; pass < 4; ++pass) {
        bool changed = false;

        for (std::size_t customer_idx = 0; customer_idx < assignment.size(); ++customer_idx) {
            const node_id_t customer_node = instance.customer_node_from_index(customer_idx);
            const depot_id_t current = assignment[customer_idx];
            if (current >= depot_count || load[current] == 0) {
                continue;
            }

            const cost_t current_score = depot_assignment_score(
                customer_node,
                current,
                load[current],
                depot_customer_targets[current],
                matrix,
                distance_scale
            );

            depot_id_t best_depot = current;
            cost_t best_delta = 0.0;

            for (depot_id_t candidate = 0; candidate < depot_count; ++candidate) {
                if (candidate == current) {
                    continue;
                }

                const cost_t candidate_score = depot_assignment_score(
                    customer_node,
                    candidate,
                    load[candidate] + 1U,
                    depot_customer_targets[candidate],
                    matrix,
                    distance_scale
                );

                const cost_t release_gain = soft_load_penalty(
                    load[current],
                    depot_customer_targets[current],
                    distance_scale
                ) - soft_load_penalty(
                    load[current] - 1U,
                    depot_customer_targets[current],
                    distance_scale
                );

                const cost_t delta = candidate_score - current_score - release_gain;
                if (delta < best_delta - kEps) {
                    best_delta = delta;
                    best_depot = candidate;
                }
            }

            if (best_depot != current) {
                --load[current];
                ++load[best_depot];
                assignment[customer_idx] = best_depot;
                changed = true;
            }
        }

        if (!changed) {
            break;
        }
    }
}

[[nodiscard]] std::vector<depot_id_t> assign_customers_to_depots_balanced(
    const MDMTSPInstance& instance,
    const DistanceMatrix& matrix,
    const std::vector<std::size_t>& depot_customer_targets,
    Random& rng
) {
    const std::size_t depot_count = instance.depot_count();
    std::vector<DepotRanking> rankings;
    rankings.reserve(instance.customer_count());

    for (std::size_t customer_idx = 0; customer_idx < instance.customer_count(); ++customer_idx) {
        const node_id_t customer_node = instance.customer_node_from_index(customer_idx);

        std::vector<std::pair<cost_t, depot_id_t>> distances;
        distances.reserve(depot_count);

        for (depot_id_t depot = 0; depot < depot_count; ++depot) {
            distances.emplace_back(matrix[depot][customer_node], depot);
        }

        std::sort(distances.begin(), distances.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.first != rhs.first) {
                return lhs.first < rhs.first;
            }
            return lhs.second < rhs.second;
        });

        DepotRanking ranking;
        ranking.customer_node = customer_node;
        ranking.ranked_depots.reserve(depot_count);
        for (const auto& [_, depot] : distances) {
            ranking.ranked_depots.push_back(depot);
        }

        const cost_t best = distances.front().first;
        const cost_t second = distances.size() > 1 ? distances[1].first : best;
        ranking.margin = second - best;
        rankings.push_back(std::move(ranking));
    }

    rng.shuffle(rankings);
    std::sort(rankings.begin(), rankings.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.margin != rhs.margin) {
            return lhs.margin > rhs.margin;
        }
        return lhs.customer_node < rhs.customer_node;
    });

    const cost_t distance_scale = mean_nearest_depot_distance(instance, matrix);
    std::vector<std::size_t> load(depot_count, 0);
    std::vector<depot_id_t> assignment(instance.customer_count(), invalid_depot_id);

    for (const auto& ranking : rankings) {
        depot_id_t chosen = ranking.ranked_depots.front();
        cost_t best_score = std::numeric_limits<cost_t>::infinity();

        for (const depot_id_t depot : ranking.ranked_depots) {
            const cost_t score = depot_assignment_score(
                ranking.customer_node,
                depot,
                load[depot] + 1U,
                depot_customer_targets[depot],
                matrix,
                distance_scale
            );
            if (score < best_score - kEps) {
                best_score = score;
                chosen = depot;
            }
        }

        ++load[chosen];
        assignment[instance.customer_index_from_node(ranking.customer_node)] = chosen;
    }

    refine_depot_assignment_by_soft_relocation(
        instance,
        matrix,
        depot_customer_targets,
        assignment
    );

    return assignment;
}

[[nodiscard]] std::vector<std::vector<node_id_t>> group_customers_by_depot(
    const MDMTSPInstance& instance,
    const std::vector<depot_id_t>& customer_to_depot
) {
    std::vector<std::vector<node_id_t>> groups(instance.depot_count());

    for (std::size_t customer_idx = 0; customer_idx < customer_to_depot.size(); ++customer_idx) {
        const depot_id_t depot = customer_to_depot[customer_idx];
        if (depot >= instance.depot_count()) {
            throw std::out_of_range("group_customers_by_depot: depot id out of range");
        }
        groups[depot].push_back(instance.customer_node_from_index(customer_idx));
    }

    return groups;
}

[[nodiscard]] std::vector<node_id_t> select_farthest_seeds(
    const MDMTSPInstance& instance,
    const std::vector<node_id_t>& customers,
    const std::size_t seed_count,
    const depot_id_t depot_id,
    const DistanceMatrix& matrix
) {
    std::vector<node_id_t> seeds;
    if (seed_count == 0 || customers.empty()) {
        return seeds;
    }

    seeds.reserve(seed_count);

    auto farthest_from_depot = customers.front();
    cost_t farthest_distance = -1.0;
    for (const node_id_t customer : customers) {
        const cost_t d = matrix[depot_id][customer];
        if (d > farthest_distance) {
            farthest_distance = d;
            farthest_from_depot = customer;
        }
    }
    seeds.push_back(farthest_from_depot);

    while (seeds.size() < seed_count) {
        node_id_t best_customer = invalid_node_id;
        cost_t best_score = -1.0;

        for (const node_id_t customer : customers) {
            if (std::find(seeds.begin(), seeds.end(), customer) != seeds.end()) {
                continue;
            }

            cost_t nearest_seed = std::numeric_limits<cost_t>::infinity();
            for (const node_id_t seed : seeds) {
                nearest_seed = std::min(nearest_seed, matrix[customer][seed]);
            }

            if (nearest_seed > best_score) {
                best_score = nearest_seed;
                best_customer = customer;
            }
        }

        if (best_customer == invalid_node_id) {
            break;
        }
        seeds.push_back(best_customer);
    }

    return seeds;
}

[[nodiscard]] Point2D centroid_of_nodes(
    const MDMTSPInstance& instance,
    const std::vector<node_id_t>& nodes,
    const Point2D& fallback
) {
    if (nodes.empty()) {
        return fallback;
    }

    Point2D c{};
    for (const node_id_t node : nodes) {
        const auto p = instance.point_of(node);
        c.x += p.x;
        c.y += p.y;
    }

    const double inv = 1.0 / static_cast<double>(nodes.size());
    c.x *= inv;
    c.y *= inv;
    return c;
}

[[nodiscard]] std::vector<std::vector<node_id_t>> split_depot_group_into_salesmen(
    const MDMTSPInstance& instance,
    const std::vector<node_id_t>& customers,
    const depot_id_t depot_id,
    const std::size_t salesman_count_for_depot,
    const DistanceMatrix& matrix,
    Random& rng
) {
    std::vector<std::vector<node_id_t>> chunks(salesman_count_for_depot);
    if (salesman_count_for_depot == 0 || customers.empty()) {
        return chunks;
    }

    const std::size_t active = std::min<std::size_t>(salesman_count_for_depot, customers.size());
    if (active == 1) {
        chunks.front() = customers;
        return chunks;
    }

    auto shuffled = customers;
    rng.shuffle(shuffled);

    auto centers_as_nodes = select_farthest_seeds(instance, shuffled, active, depot_id, matrix);
    if (centers_as_nodes.size() < active) {
        centers_as_nodes.resize(active, shuffled.front());
    }

    const auto targets = distribute_evenly(customers.size(), active);
    std::vector<Point2D> centers(active, instance.depots[depot_id]);

    for (std::size_t i = 0; i < active; ++i) {
        centers[i] = instance.point_of(centers_as_nodes[i]);
    }

    std::vector<node_id_t> ordered = customers;
    std::sort(ordered.begin(), ordered.end(), [&](const node_id_t lhs, const node_id_t rhs) {
        const cost_t dl = matrix[depot_id][lhs];
        const cost_t dr = matrix[depot_id][rhs];
        if (dl != dr) {
            return dl > dr;
        }
        return lhs < rhs;
    });

    for (int pass = 0; pass < kClusteringRefinementPasses; ++pass) {
        for (std::size_t i = 0; i < active; ++i) {
            chunks[i].clear();
        }

        for (const node_id_t customer : ordered) {
            const auto point = instance.point_of(customer);

            std::size_t best_cluster = 0;
            cost_t best_score = std::numeric_limits<cost_t>::infinity();

            for (std::size_t cluster = 0; cluster < active; ++cluster) {
                const cost_t d = euclidean_distance(point, centers[cluster]);
                const double overload =
                    static_cast<double>(chunks[cluster].size() + 1U) /
                    static_cast<double>(std::max<std::size_t>(1U, targets[cluster]));
                const cost_t penalty =
                    overload > 1.0 ? static_cast<cost_t>(overload - 1.0) * matrix[depot_id][customer] : 0.0;
                const cost_t score = d + penalty;

                if (score < best_score) {
                    best_score = score;
                    best_cluster = cluster;
                }
            }

            chunks[best_cluster].push_back(customer);
        }

        for (std::size_t cluster = 0; cluster < active; ++cluster) {
            centers[cluster] = centroid_of_nodes(instance, chunks[cluster], instance.depots[depot_id]);
        }
    }

    for (std::size_t cluster = active; cluster < salesman_count_for_depot; ++cluster) {
        chunks[cluster].clear();
    }

    return chunks;
}

void two_opt_improve_closed(Route& route, const DistanceMatrix& matrix) {
    if (route.size() < 5) {
        return;
    }

    const std::size_t last = route.size() - 1U;

    for (int pass = 0; pass < kMaxTwoOptPasses; ++pass) {
        bool improved = false;

        for (std::size_t i = 1; i + 2 < last; ++i) {
            for (std::size_t k = i + 1; k < last; ++k) {
                const node_id_t a = route[i - 1];
                const node_id_t b = route[i];
                const node_id_t c = route[k];
                const node_id_t d = route[k + 1];

                const cost_t delta =
                    matrix[a][c] + matrix[b][d] - matrix[a][b] - matrix[c][d];

                if (!improving(delta)) {
                    continue;
                }

                std::reverse(
                    route.begin() + static_cast<std::ptrdiff_t>(i),
                    route.begin() + static_cast<std::ptrdiff_t>(k + 1U)
                );
                improved = true;
                break;
            }
            if (improved) {
                break;
            }
        }

        if (!improved) {
            break;
        }
    }
}

void two_opt_improve_open(Route& route, const DistanceMatrix& matrix) {
    if (route.size() < 4) {
        return;
    }

    const std::size_t n = route.size();

    for (int pass = 0; pass < kMaxTwoOptPasses; ++pass) {
        bool improved = false;

        for (std::size_t i = 1; i + 1 < n; ++i) {
            for (std::size_t k = i + 1; k < n; ++k) {
                const node_id_t a = route[i - 1];
                const node_id_t b = route[i];
                const node_id_t c = route[k];

                cost_t old_cost = matrix[a][b];
                cost_t new_cost = matrix[a][c];

                if (k + 1 < n) {
                    const node_id_t d = route[k + 1];
                    old_cost += matrix[c][d];
                    new_cost += matrix[b][d];
                }

                const cost_t delta = new_cost - old_cost;
                if (!improving(delta)) {
                    continue;
                }

                std::reverse(
                    route.begin() + static_cast<std::ptrdiff_t>(i),
                    route.begin() + static_cast<std::ptrdiff_t>(k + 1U)
                );
                improved = true;
                break;
            }
            if (improved) {
                break;
            }
        }

        if (!improved) {
            break;
        }
    }
}

[[nodiscard]] Route build_greedy_route(
    const depot_id_t depot_id,
    std::vector<node_id_t> customers,
    const DistanceMatrix& matrix,
    const bool return_to_depot
) {
    Route route;
    route.reserve(customers.size() + (return_to_depot ? 2U : 1U));
    route.push_back(static_cast<node_id_t>(depot_id));

    if (customers.empty()) {
        if (return_to_depot) {
            route.push_back(static_cast<node_id_t>(depot_id));
        }
        return route;
    }

    node_id_t current = static_cast<node_id_t>(depot_id);

    while (!customers.empty()) {
        auto best_it = customers.begin();
        cost_t best_distance = matrix[current][*best_it];

        for (auto it = std::next(customers.begin()); it != customers.end(); ++it) {
            const cost_t d = matrix[current][*it];
            if (d < best_distance) {
                best_distance = d;
                best_it = it;
            }
        }

        current = *best_it;
        route.push_back(current);
        customers.erase(best_it);
    }

    if (return_to_depot) {
        route.push_back(static_cast<node_id_t>(depot_id));
        two_opt_improve_closed(route, matrix);
    } else {
        two_opt_improve_open(route, matrix);
    }

    return route;
}

[[nodiscard]] std::uint64_t stable_route_seed(
    const depot_id_t depot_id,
    const std::vector<node_id_t>& customers
) {
    std::uint64_t h = 1469598103934665603ULL;
    auto mix = [&](const std::uint64_t value) {
        h ^= value + 0x9e3779b97f4a7c15ULL + (h << 6U) + (h >> 2U);
    };

    mix(static_cast<std::uint64_t>(depot_id));
    mix(static_cast<std::uint64_t>(customers.size()));

    for (const auto customer : customers) {
        mix(static_cast<std::uint64_t>(customer));
    }

    return h == 0 ? 1ULL : h;
}

[[nodiscard]] std::optional<Route> try_optimize_route_with_lkh(
    const depot_id_t depot_id,
    const std::vector<node_id_t>& customers,
    const DistanceMatrix& matrix,
    const bool return_to_depot,
    bool& permanently_disable_lkh
) {
    if (customers.empty()) {
        Route trivial{static_cast<node_id_t>(depot_id)};
        if (return_to_depot) {
            trivial.push_back(static_cast<node_id_t>(depot_id));
        }
        return trivial;
    }

    std::unique_ptr<tsp::TSPSolver> solver = tsp::make_lkh_solver();
    if (!solver) {
        permanently_disable_lkh = true;
        return std::nullopt;
    }

    std::vector<node_id_t> local_to_global;
    local_to_global.reserve(customers.size() + 1U);
    local_to_global.push_back(static_cast<node_id_t>(depot_id));
    local_to_global.insert(local_to_global.end(), customers.begin(), customers.end());

    tsp::TSPProblem problem;
    problem.type = tsp::TourType::Cycle;
    problem.start = 0;

    if (return_to_depot) {
        problem.n = static_cast<tsp::Index>(customers.size() + 1U);
        problem.dist = tsp::DistanceFunction([&](const tsp::Index u, const tsp::Index v) -> tsp::Cost {
            return matrix[local_to_global[static_cast<std::size_t>(u)]]
                         [local_to_global[static_cast<std::size_t>(v)]];
        });
    } else {
        const tsp::Index dummy = static_cast<tsp::Index>(customers.size() + 1U);
        problem.n = static_cast<tsp::Index>(customers.size() + 2U);

        cost_t max_edge = 0.0;
        for (const auto customer : customers) {
            max_edge = std::max(max_edge, matrix[depot_id][customer]);
            for (const auto other : customers) {
                max_edge = std::max(max_edge, matrix[customer][other]);
            }
        }
        const cost_t big_m =
            std::max<cost_t>(1.0, max_edge * static_cast<cost_t>(customers.size() + 2U) + 1.0);

        problem.dist = tsp::DistanceFunction([&, dummy, big_m](const tsp::Index u, const tsp::Index v) -> tsp::Cost {
            if (u == dummy) {
                return v == 0 ? 0.0 : big_m;
            }
            if (v == dummy) {
                return u == 0 ? big_m : 0.0;
            }
            return matrix[local_to_global[static_cast<std::size_t>(u)]]
                         [local_to_global[static_cast<std::size_t>(v)]];
        });
    }

    tsp::TSPSolveOptions options;
    options.seed = stable_route_seed(depot_id, customers);
    options.iteration_limit = std::max<std::uint64_t>(
        200ULL,
        static_cast<std::uint64_t>(problem.n) * 25ULL
    );
    options.require_finite_costs = true;
    options.require_non_negative_costs = true;

    const auto tsp_solution = solver->solve(problem, options);
    if (tsp_solution.status != tsp::Status::Ok) {
        if (tsp_solution.status == tsp::Status::InvalidArgument ||
            tsp_solution.status == tsp::Status::NotImplemented) {
            permanently_disable_lkh = true;
        }
        return std::nullopt;
    }

    if (tsp_solution.order.empty() || tsp_solution.order.front() != 0U) {
        return std::nullopt;
    }

    Route route;
    route.reserve(customers.size() + (return_to_depot ? 2U : 1U));
    route.push_back(static_cast<node_id_t>(depot_id));

    if (return_to_depot) {
        for (std::size_t pos = 1; pos < tsp_solution.order.size(); ++pos) {
            const auto local = tsp_solution.order[pos];
            route.push_back(local_to_global[static_cast<std::size_t>(local)]);
        }
        route.push_back(static_cast<node_id_t>(depot_id));
    } else {
        const tsp::Index dummy = static_cast<tsp::Index>(customers.size() + 1U);
        if (tsp_solution.order.back() != dummy) {
            return std::nullopt;
        }

        for (std::size_t pos = 1; pos + 1 < tsp_solution.order.size(); ++pos) {
            const auto local = tsp_solution.order[pos];
            if (local == dummy) {
                return std::nullopt;
            }
            route.push_back(local_to_global[static_cast<std::size_t>(local)]);
        }
    }

    return route;
}

[[nodiscard]] Route optimize_route(
    const depot_id_t depot_id,
    const std::vector<node_id_t>& customers,
    const DistanceMatrix& matrix,
    const bool return_to_depot,
    bool& lkh_enabled
) {
    Route greedy = build_greedy_route(depot_id, customers, matrix, return_to_depot);

    if (!lkh_enabled || customers.size() < kSmallRouteThreshold) {
        return greedy;
    }

    bool permanently_disable_lkh = false;
    auto optimized = try_optimize_route_with_lkh(
        depot_id,
        customers,
        matrix,
        return_to_depot,
        permanently_disable_lkh
    );

    if (permanently_disable_lkh) {
        lkh_enabled = false;
    }

    if (!optimized.has_value()) {
        return greedy;
    }

    return std::move(*optimized);
}

[[nodiscard]] std::size_t recommended_relocation_iterations(
    const std::size_t customer_count
) noexcept {
    if (customer_count <= 300) {
        return 32;
    }
    if (customer_count <= 1500) {
        return 16;
    }
    if (customer_count <= 5000) {
        return 8;
    }
    if (customer_count <= 12000) {
        return 3;
    }
    return 0;
}

[[nodiscard]] MDMTSPSolution build_solution_from_partition(
    const MDMTSPInstance& instance,
    const std::vector<depot_id_t>& salesman_to_depot,
    const std::vector<std::vector<node_id_t>>& chunks,
    const DistanceMatrix& matrix,
    bool& lkh_enabled
) {
    MDMTSPSolution solution;
    solution.routes.reserve(salesman_to_depot.size());

    for (salesman_id_t salesman = 0; salesman < salesman_to_depot.size(); ++salesman) {
        const depot_id_t depot_id = salesman_to_depot[salesman];
        Route route = optimize_route(
            depot_id,
            chunks[salesman],
            matrix,
            instance.return_to_depot,
            lkh_enabled
        );

        solution.routes.push_back(MDMTSPSalesmanRoute{
            salesman,
            depot_id,
            std::move(route)
        });
    }

    solution.objective = compute_objective(solution, instance, matrix);
    solution.feasible = is_solution_feasible(instance, solution);
    solution.status = solution.feasible ? "ok" : validation_report(instance, solution);
    return solution;
}

void reroute_all_routes_with_lkh(
    MDMTSPSolution& solution,
    const MDMTSPInstance& instance,
    const DistanceMatrix& matrix,
    bool& lkh_enabled
) {
    if (!lkh_enabled) {
        return;
    }

    for (auto& route : solution.routes) {
        std::vector<node_id_t> customers_only;
        customers_only.reserve(route.nodes.size());

        for (const auto node : route.nodes) {
            if (instance.is_customer_node(node)) {
                customers_only.push_back(node);
            }
        }

        route.nodes = optimize_route(
            route.depot_id,
            customers_only,
            matrix,
            instance.return_to_depot,
            lkh_enabled
        );
    }
}

}  // namespace

[[nodiscard]] MDMTSPSolution solve_mdmtsp_lkh_mdmtsp_v2(
    const MDMTSPInstance& instance,
    Random& rng
) {
    instance.validate_basic();

    const DistanceMatrix matrix = instance.build_distance_matrix();
    const auto salesmen_per_depot = allocate_salesmen_to_depots_by_pull(instance, matrix);
    const auto depot_customer_caps =
        derive_customer_caps_from_salesmen(instance.customer_count(), salesmen_per_depot);
    const auto customer_to_depot =
        assign_customers_to_depots_balanced(instance, matrix, depot_customer_caps, rng);
    const auto grouped_by_depot = group_customers_by_depot(instance, customer_to_depot);
    const auto salesman_to_depot = expand_salesman_to_depot(salesmen_per_depot);

    std::vector<std::vector<node_id_t>> chunks;
    chunks.reserve(instance.salesman_count);

    for (depot_id_t depot = 0; depot < instance.depot_count(); ++depot) {
        const std::size_t route_count = salesmen_per_depot[depot];
        auto depot_chunks = split_depot_group_into_salesmen(
            instance,
            grouped_by_depot[depot],
            depot,
            route_count,
            matrix,
            rng
        );

        for (auto& chunk : depot_chunks) {
            chunks.push_back(std::move(chunk));
        }
    }

    if (chunks.size() != instance.salesman_count || salesman_to_depot.size() != instance.salesman_count) {
        throw std::logic_error("solve_mdmtsp_lkh_mdmtsp_v2: inconsistent route partition");
    }

    bool lkh_enabled = true;
    auto solution = build_solution_from_partition(
        instance,
        salesman_to_depot,
        chunks,
        matrix,
        lkh_enabled
    );

    const std::size_t relocate_iterations =
        recommended_relocation_iterations(instance.customer_count());
    if (relocate_iterations > 0 && solution.feasible) {
        improve_interroute_by_relocation(solution, instance, relocate_iterations);
        reroute_all_routes_with_lkh(solution, instance, matrix, lkh_enabled);
        solution.objective = compute_objective(solution, instance, matrix);
        solution.feasible = is_solution_feasible(instance, solution);
        solution.status = solution.feasible ? "ok" : validation_report(instance, solution);
    }

    return solution;
}

}  // namespace mdmtsp
