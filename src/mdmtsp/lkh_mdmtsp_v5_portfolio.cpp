
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
constexpr std::size_t kMinMeaningfulChunkSize = 8;
constexpr int kSplitRefinementPasses = 4;
constexpr int kRouteMergePasses = 3;
constexpr cost_t kBoundedStretchMultiplier = static_cast<cost_t>(1.20);
constexpr cost_t kBoundedStretchAdditiveScale = static_cast<cost_t>(1.75);
constexpr cost_t kRouteOpeningPenaltyFactor = static_cast<cost_t>(1.10);
constexpr std::size_t kExactClusterProxyThreshold = 180;
constexpr std::size_t kExactRouteThreshold = 12;
constexpr cost_t kPortfolioSegmentPenaltyFactor = static_cast<cost_t>(0.45);
constexpr int kBoundaryRefinementPasses = 6;
constexpr std::size_t kDominantDepotCandidateLimit = 2;
constexpr std::size_t kSmallInstanceDominantThreshold = 32;
constexpr std::size_t kAssignmentRefinementCustomerThreshold = 1500;
constexpr std::size_t kAssignmentNeighborhoodSampleLimit = 24;
constexpr int kAssignmentGvnsPasses = 3;

[[nodiscard]] bool improving(const cost_t delta) noexcept {
    return delta < -kEps;
}


[[nodiscard]] std::optional<Route> exact_optimize_small_route(
    const depot_id_t depot_id,
    const std::vector<node_id_t>& customers,
    const DistanceMatrix& matrix,
    const bool return_to_depot
) {
    if (customers.empty()) {
        Route trivial{static_cast<node_id_t>(depot_id)};
        if (return_to_depot) {
            trivial.push_back(static_cast<node_id_t>(depot_id));
        }
        return trivial;
    }

    if (customers.size() > kExactRouteThreshold) {
        return std::nullopt;
    }

    const std::size_t n = customers.size();
    const std::size_t state_count = static_cast<std::size_t>(1ULL << n);
    const cost_t inf = std::numeric_limits<cost_t>::infinity();

    std::vector<cost_t> dp(state_count * n, inf);
    std::vector<int> parent(state_count * n, -1);

    auto idx = [n](const std::size_t mask, const std::size_t last) {
        return mask * n + last;
    };

    for (std::size_t j = 0; j < n; ++j) {
        const std::size_t mask = static_cast<std::size_t>(1ULL << j);
        dp[idx(mask, j)] = matrix[depot_id][customers[j]];
    }

    for (std::size_t mask = 1; mask < state_count; ++mask) {
        for (std::size_t last = 0; last < n; ++last) {
            if ((mask & (static_cast<std::size_t>(1ULL << last))) == 0U) {
                continue;
            }

            const cost_t cur = dp[idx(mask, last)];
            if (!std::isfinite(cur)) {
                continue;
            }

            for (std::size_t nxt = 0; nxt < n; ++nxt) {
                if ((mask & (static_cast<std::size_t>(1ULL << nxt))) != 0U) {
                    continue;
                }

                const std::size_t next_mask = mask | static_cast<std::size_t>(1ULL << nxt);
                const cost_t cand = cur + matrix[customers[last]][customers[nxt]];
                auto& cell = dp[idx(next_mask, nxt)];
                if (cand < cell - kEps) {
                    cell = cand;
                    parent[idx(next_mask, nxt)] = static_cast<int>(last);
                }
            }
        }
    }

    const std::size_t full_mask = state_count - 1U;
    cost_t best_cost = inf;
    int best_last = -1;

    for (std::size_t last = 0; last < n; ++last) {
        cost_t cand = dp[idx(full_mask, last)];
        if (!std::isfinite(cand)) {
            continue;
        }
        if (return_to_depot) {
            cand += matrix[customers[last]][depot_id];
        }
        if (cand < best_cost - kEps) {
            best_cost = cand;
            best_last = static_cast<int>(last);
        }
    }

    if (best_last < 0) {
        return std::nullopt;
    }

    std::vector<node_id_t> ordered(n, invalid_node_id);
    std::size_t mask = full_mask;
    int last = best_last;
    for (std::size_t pos = n; pos-- > 0;) {
        ordered[pos] = customers[static_cast<std::size_t>(last)];
        const int prev = parent[idx(mask, static_cast<std::size_t>(last))];
        mask &= ~static_cast<std::size_t>(1ULL << static_cast<std::size_t>(last));
        last = prev;
        if (mask == 0) {
            break;
        }
    }

    Route route;
    route.reserve(n + (return_to_depot ? 2U : 1U));
    route.push_back(static_cast<node_id_t>(depot_id));
    route.insert(route.end(), ordered.begin(), ordered.end());
    if (return_to_depot) {
        route.push_back(static_cast<node_id_t>(depot_id));
    }
    return route;
}

[[nodiscard]] Route build_greedy_route(
    const depot_id_t depot_id,
    std::vector<node_id_t> customers,
    const DistanceMatrix& matrix,
    const bool return_to_depot
);

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
        const cost_t nearest_distance = matrix[ranking.ranked_depots.front()][ranking.customer_node];
        const cost_t admissible_distance = std::max(
            nearest_distance * kBoundedStretchMultiplier,
            nearest_distance + kBoundedStretchAdditiveScale * distance_scale
        );

        depot_id_t chosen = ranking.ranked_depots.front();
        cost_t best_score = std::numeric_limits<cost_t>::infinity();
        bool seen_candidate = false;

        for (const depot_id_t depot : ranking.ranked_depots) {
            const cost_t distance = matrix[depot][ranking.customer_node];
            if (seen_candidate && distance > admissible_distance + kEps) {
                break;
            }
            seen_candidate = true;

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

[[nodiscard]] cost_t mean_distance_to_depot(
    const std::vector<node_id_t>& customers,
    const depot_id_t depot_id,
    const DistanceMatrix& matrix
) {
    if (customers.empty()) {
        return 0.0;
    }

    cost_t total = 0.0;
    for (const node_id_t customer : customers) {
        total += matrix[depot_id][customer];
    }
    return total / static_cast<cost_t>(customers.size());
}

[[nodiscard]] cost_t cluster_opening_penalty(
    const std::vector<node_id_t>& customers,
    const depot_id_t depot_id,
    const DistanceMatrix& matrix
) {
    return kRouteOpeningPenaltyFactor * 2.0 * mean_distance_to_depot(customers, depot_id, matrix);
}

[[nodiscard]] cost_t estimate_cluster_service_cost(
    const MDMTSPInstance& instance,
    const std::vector<node_id_t>& customers,
    const depot_id_t depot_id,
    const DistanceMatrix& matrix
) {
    if (customers.empty()) {
        return 0.0;
    }

    if (customers.size() <= kExactClusterProxyThreshold) {
        const Route route = build_greedy_route(depot_id, customers, matrix, instance.return_to_depot);
        return route_cost(route, matrix);
    }

    const Point2D depot_point = instance.depots[depot_id];
    const Point2D centroid = centroid_of_nodes(instance, customers, depot_point);

    cost_t sum_centroid = 0.0;
    cost_t max_centroid = 0.0;
    cost_t sum_depot = 0.0;

    for (const node_id_t customer : customers) {
        const Point2D p = instance.point_of(customer);
        const cost_t centroid_d = euclidean_distance(p, centroid);
        sum_centroid += centroid_d;
        max_centroid = std::max(max_centroid, centroid_d);
        sum_depot += matrix[depot_id][customer];
    }

    const cost_t mean_centroid = sum_centroid / static_cast<cost_t>(customers.size());
    const cost_t mean_depot = sum_depot / static_cast<cost_t>(customers.size());
    const cost_t scale = std::sqrt(static_cast<cost_t>(customers.size()));

    return 2.0 * mean_depot + scale * mean_centroid + 0.35 * max_centroid;
}

[[nodiscard]] std::optional<std::array<std::vector<node_id_t>, 2>> split_cluster_two_way(
    const MDMTSPInstance& instance,
    const std::vector<node_id_t>& customers,
    const depot_id_t depot_id,
    const DistanceMatrix& matrix
) {
    if (customers.size() < 2 * kMinMeaningfulChunkSize) {
        return std::nullopt;
    }

    node_id_t seed_a = customers.front();
    cost_t farthest_from_depot = -1.0;
    for (const node_id_t customer : customers) {
        const cost_t d = matrix[depot_id][customer];
        if (d > farthest_from_depot) {
            farthest_from_depot = d;
            seed_a = customer;
        }
    }

    node_id_t seed_b = seed_a;
    cost_t farthest_from_a = -1.0;
    for (const node_id_t customer : customers) {
        const cost_t d = matrix[seed_a][customer];
        if (d > farthest_from_a) {
            farthest_from_a = d;
            seed_b = customer;
        }
    }

    if (seed_a == seed_b) {
        return std::nullopt;
    }

    std::array<Point2D, 2> centers{
        instance.point_of(seed_a),
        instance.point_of(seed_b)
    };
    const auto targets = distribute_evenly(customers.size(), 2);
    std::array<std::vector<node_id_t>, 2> chunks;

    for (int pass = 0; pass < kSplitRefinementPasses; ++pass) {
        chunks[0].clear();
        chunks[1].clear();

        std::vector<node_id_t> ordered = customers;
        std::sort(ordered.begin(), ordered.end(), [&](const node_id_t lhs, const node_id_t rhs) {
            const Point2D pl = instance.point_of(lhs);
            const Point2D pr = instance.point_of(rhs);
            const cost_t diff_l = std::abs(euclidean_distance(pl, centers[0]) - euclidean_distance(pl, centers[1]));
            const cost_t diff_r = std::abs(euclidean_distance(pr, centers[0]) - euclidean_distance(pr, centers[1]));
            if (diff_l != diff_r) {
                return diff_l > diff_r;
            }
            return lhs < rhs;
        });

        for (const node_id_t customer : ordered) {
            const Point2D p = instance.point_of(customer);

            cost_t best_score = std::numeric_limits<cost_t>::infinity();
            std::size_t best_chunk = 0;

            for (std::size_t cluster = 0; cluster < 2; ++cluster) {
                const cost_t d = euclidean_distance(p, centers[cluster]);
                const double overload =
                    static_cast<double>(chunks[cluster].size() + 1U) /
                    static_cast<double>(std::max<std::size_t>(1U, targets[cluster]));
                const cost_t penalty =
                    overload > 1.0 ? static_cast<cost_t>(overload - 1.0) * matrix[depot_id][customer] * 0.25 : 0.0;
                const cost_t score = d + penalty;

                if (score < best_score - kEps) {
                    best_score = score;
                    best_chunk = cluster;
                }
            }

            chunks[best_chunk].push_back(customer);
        }

        if (chunks[0].empty() || chunks[1].empty()) {
            return std::nullopt;
        }

        centers[0] = centroid_of_nodes(instance, chunks[0], instance.depots[depot_id]);
        centers[1] = centroid_of_nodes(instance, chunks[1], instance.depots[depot_id]);
    }

    if (chunks[0].size() < kMinMeaningfulChunkSize || chunks[1].size() < kMinMeaningfulChunkSize) {
        return std::nullopt;
    }

    return chunks;
}

struct BeneficialSplit {
    std::size_t cluster_index = 0;
    cost_t gain = 0.0;
    std::array<std::vector<node_id_t>, 2> chunks;
};

[[nodiscard]] std::optional<BeneficialSplit> find_best_cluster_split(
    const MDMTSPInstance& instance,
    const std::vector<std::vector<node_id_t>>& clusters,
    const depot_id_t depot_id,
    const DistanceMatrix& matrix
) {
    std::optional<BeneficialSplit> best;

    for (std::size_t idx = 0; idx < clusters.size(); ++idx) {
        const auto maybe_split = split_cluster_two_way(instance, clusters[idx], depot_id, matrix);
        if (!maybe_split.has_value()) {
            continue;
        }

        const cost_t old_cost = estimate_cluster_service_cost(instance, clusters[idx], depot_id, matrix);
        const cost_t new_cost =
            estimate_cluster_service_cost(instance, (*maybe_split)[0], depot_id, matrix) +
            estimate_cluster_service_cost(instance, (*maybe_split)[1], depot_id, matrix) +
            cluster_opening_penalty(clusters[idx], depot_id, matrix);
        const cost_t gain = old_cost - new_cost;

        if (gain > kEps && (!best.has_value() || gain > best->gain + kEps)) {
            best = BeneficialSplit{idx, gain, std::move(*maybe_split)};
        }
    }

    return best;
}

void refine_clusters_with_fixed_k(
    const MDMTSPInstance& instance,
    std::vector<std::vector<node_id_t>>& clusters,
    const depot_id_t depot_id,
    const DistanceMatrix& matrix
) {
    if (clusters.size() <= 1) {
        return;
    }

    std::vector<node_id_t> all_customers;
    for (const auto& cluster : clusters) {
        all_customers.insert(all_customers.end(), cluster.begin(), cluster.end());
    }

    const auto targets = distribute_evenly(all_customers.size(), clusters.size());
    std::vector<Point2D> centers;
    centers.reserve(clusters.size());
    for (const auto& cluster : clusters) {
        centers.push_back(centroid_of_nodes(instance, cluster, instance.depots[depot_id]));
    }

    for (int pass = 0; pass < 3; ++pass) {
        for (auto& cluster : clusters) {
            cluster.clear();
        }

        std::vector<node_id_t> ordered = all_customers;
        std::sort(ordered.begin(), ordered.end(), [&](const node_id_t lhs, const node_id_t rhs) {
            const Point2D pl = instance.point_of(lhs);
            const Point2D pr = instance.point_of(rhs);

            auto best_second_gap = [&](const Point2D& p) {
                cost_t best = std::numeric_limits<cost_t>::infinity();
                cost_t second = std::numeric_limits<cost_t>::infinity();
                for (const auto& center : centers) {
                    const cost_t d = euclidean_distance(p, center);
                    if (d < best) {
                        second = best;
                        best = d;
                    } else if (d < second) {
                        second = d;
                    }
                }
                return second - best;
            };

            const cost_t gap_l = best_second_gap(pl);
            const cost_t gap_r = best_second_gap(pr);
            if (gap_l != gap_r) {
                return gap_l > gap_r;
            }
            return lhs < rhs;
        });

        for (const node_id_t customer : ordered) {
            const Point2D p = instance.point_of(customer);
            std::size_t best_cluster = 0;
            cost_t best_score = std::numeric_limits<cost_t>::infinity();

            for (std::size_t cluster_idx = 0; cluster_idx < clusters.size(); ++cluster_idx) {
                const cost_t d = euclidean_distance(p, centers[cluster_idx]);
                const double overload =
                    static_cast<double>(clusters[cluster_idx].size() + 1U) /
                    static_cast<double>(std::max<std::size_t>(1U, targets[cluster_idx]));
                const cost_t penalty =
                    overload > 1.0 ? static_cast<cost_t>(overload - 1.0) * matrix[depot_id][customer] * 0.20 : 0.0;
                const cost_t score = d + penalty;
                if (score < best_score - kEps) {
                    best_score = score;
                    best_cluster = cluster_idx;
                }
            }

            clusters[best_cluster].push_back(customer);
        }

        for (std::size_t cluster_idx = 0; cluster_idx < clusters.size(); ++cluster_idx) {
            centers[cluster_idx] = centroid_of_nodes(
                instance,
                clusters[cluster_idx],
                instance.depots[depot_id]
            );
        }
    }
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

    std::vector<std::vector<node_id_t>> active_clusters;
    active_clusters.push_back(customers);

    while (active_clusters.size() < salesman_count_for_depot) {
        const auto best_split = find_best_cluster_split(
            instance,
            active_clusters,
            depot_id,
            matrix
        );
        if (!best_split.has_value()) {
            break;
        }

        auto replacement = active_clusters;
        replacement[best_split->cluster_index] = std::move(best_split->chunks[0]);
        replacement.insert(
            replacement.begin() + static_cast<std::ptrdiff_t>(best_split->cluster_index + 1U),
            std::move(best_split->chunks[1])
        );
        active_clusters = std::move(replacement);
    }

    refine_clusters_with_fixed_k(instance, active_clusters, depot_id, matrix);

    for (auto& cluster : active_clusters) {
        rng.shuffle(cluster);
        std::sort(cluster.begin(), cluster.end(), [&](const node_id_t lhs, const node_id_t rhs) {
            const cost_t dl = matrix[depot_id][lhs];
            const cost_t dr = matrix[depot_id][rhs];
            if (dl != dr) {
                return dl > dr;
            }
            return lhs < rhs;
        });
    }

    std::sort(active_clusters.begin(), active_clusters.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.size() > rhs.size();
    });

    for (std::size_t i = 0; i < active_clusters.size(); ++i) {
        chunks[i] = std::move(active_clusters[i]);
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


enum class DepotAssignmentPolicy {
    Nearest,
    SoftBalanced,
};

enum class RouteSplitPolicy {
    ClusterSplit,
    PolarOrderSplit,
    NearestChainSplit,
    LocalBest,
};

[[nodiscard]] std::vector<depot_id_t> assign_customers_to_nearest_depots(
    const MDMTSPInstance& instance,
    const DistanceMatrix& matrix
) {
    std::vector<depot_id_t> assignment(instance.customer_count(), invalid_depot_id);
    for (std::size_t customer_idx = 0; customer_idx < instance.customer_count(); ++customer_idx) {
        const node_id_t customer = instance.customer_node_from_index(customer_idx);
        depot_id_t best_depot = 0;
        cost_t best_distance = std::numeric_limits<cost_t>::infinity();
        for (depot_id_t depot = 0; depot < instance.depot_count(); ++depot) {
            const cost_t d = matrix[depot][customer];
            if (d < best_distance) {
                best_distance = d;
                best_depot = depot;
            }
        }
        assignment[customer_idx] = best_depot;
    }
    return assignment;
}

[[nodiscard]] std::vector<node_id_t> order_customers_by_polar_angle(
    const MDMTSPInstance& instance,
    const std::vector<node_id_t>& customers,
    const depot_id_t depot_id,
    const DistanceMatrix& matrix,
    Random& rng
) {
    std::vector<node_id_t> ordered = customers;
    rng.shuffle(ordered);
    const Point2D depot = instance.depots[depot_id];
    std::sort(ordered.begin(), ordered.end(), [&](const node_id_t lhs, const node_id_t rhs) {
        const Point2D pl = instance.point_of(lhs);
        const Point2D pr = instance.point_of(rhs);
        const cost_t al = std::atan2(pl.y - depot.y, pl.x - depot.x);
        const cost_t ar = std::atan2(pr.y - depot.y, pr.x - depot.x);
        if (al != ar) {
            return al < ar;
        }
        const cost_t dl = matrix[depot_id][lhs];
        const cost_t dr = matrix[depot_id][rhs];
        if (dl != dr) {
            return dl < dr;
        }
        return lhs < rhs;
    });
    return ordered;
}

[[nodiscard]] std::vector<node_id_t> order_customers_by_nearest_chain(
    const std::vector<node_id_t>& customers,
    const depot_id_t depot_id,
    const DistanceMatrix& matrix
) {
    std::vector<node_id_t> remaining = customers;
    std::vector<node_id_t> ordered;
    ordered.reserve(customers.size());

    node_id_t current = static_cast<node_id_t>(depot_id);
    while (!remaining.empty()) {
        auto best_it = remaining.begin();
        cost_t best_distance = matrix[current][*best_it];
        for (auto it = std::next(remaining.begin()); it != remaining.end(); ++it) {
            const cost_t d = matrix[current][*it];
            if (d < best_distance) {
                best_distance = d;
                best_it = it;
            }
        }
        current = *best_it;
        ordered.push_back(current);
        remaining.erase(best_it);
    }
    return ordered;
}

[[nodiscard]] cost_t penalized_segment_proxy_cost(
    const MDMTSPInstance& instance,
    const std::vector<node_id_t>& customers,
    const depot_id_t depot_id,
    const DistanceMatrix& matrix
) {
    if (customers.empty()) {
        return 0.0;
    }
    return estimate_cluster_service_cost(instance, customers, depot_id, matrix) +
           kPortfolioSegmentPenaltyFactor * cluster_opening_penalty(customers, depot_id, matrix);
}

[[nodiscard]] std::vector<std::vector<node_id_t>> split_order_evenly(
    const std::vector<node_id_t>& order,
    const std::size_t active_routes
) {
    std::vector<std::vector<node_id_t>> chunks(active_routes);
    if (active_routes == 0 || order.empty()) {
        return chunks;
    }

    const auto targets = distribute_evenly(order.size(), active_routes);
    std::size_t pos = 0;
    for (std::size_t r = 0; r < active_routes; ++r) {
        const std::size_t take = targets[r];
        chunks[r].insert(
            chunks[r].end(),
            order.begin() + static_cast<std::ptrdiff_t>(pos),
            order.begin() + static_cast<std::ptrdiff_t>(pos + take)
        );
        pos += take;
    }
    return chunks;
}

void refine_contiguous_boundaries(
    const MDMTSPInstance& instance,
    std::vector<std::vector<node_id_t>>& chunks,
    const depot_id_t depot_id,
    const DistanceMatrix& matrix
) {
    if (chunks.size() <= 1) {
        return;
    }

    for (int pass = 0; pass < kBoundaryRefinementPasses; ++pass) {
        bool changed = false;

        for (std::size_t i = 0; i + 1 < chunks.size(); ++i) {
            auto score_pair = [&]() {
                return penalized_segment_proxy_cost(instance, chunks[i], depot_id, matrix) +
                       penalized_segment_proxy_cost(instance, chunks[i + 1], depot_id, matrix);
            };

            cost_t best_score = score_pair();
            bool local_change = false;

            if (chunks[i].size() > 1) {
                const node_id_t moved = chunks[i].back();
                chunks[i].pop_back();
                chunks[i + 1].insert(chunks[i + 1].begin(), moved);
                const cost_t cand = score_pair();
                if (cand < best_score - kEps) {
                    best_score = cand;
                    local_change = true;
                } else {
                    chunks[i + 1].erase(chunks[i + 1].begin());
                    chunks[i].push_back(moved);
                }
            }

            if (!local_change && chunks[i + 1].size() > 1) {
                const node_id_t moved = chunks[i + 1].front();
                chunks[i + 1].erase(chunks[i + 1].begin());
                chunks[i].push_back(moved);
                const cost_t cand = score_pair();
                if (cand < best_score - kEps) {
                    best_score = cand;
                    local_change = true;
                } else {
                    chunks[i].pop_back();
                    chunks[i + 1].insert(chunks[i + 1].begin(), moved);
                }
            }

            changed = changed || local_change;
        }

        if (!changed) {
            break;
        }
    }
}

[[nodiscard]] cost_t score_chunking_proxy(
    const MDMTSPInstance& instance,
    const std::vector<std::vector<node_id_t>>& chunks,
    const depot_id_t depot_id,
    const DistanceMatrix& matrix
) {
    cost_t total = 0.0;
    for (const auto& chunk : chunks) {
        total += penalized_segment_proxy_cost(instance, chunk, depot_id, matrix);
    }
    return total;
}

[[nodiscard]] std::vector<std::vector<node_id_t>> split_depot_group_by_order_policy(
    const MDMTSPInstance& instance,
    const std::vector<node_id_t>& customers,
    const depot_id_t depot_id,
    const std::size_t salesman_count_for_depot,
    const DistanceMatrix& matrix,
    Random& rng,
    const RouteSplitPolicy split_policy
) {
    std::vector<std::vector<node_id_t>> chunks(salesman_count_for_depot);
    if (salesman_count_for_depot == 0) {
        return chunks;
    }
    if (customers.empty()) {
        return chunks;
    }

    std::vector<node_id_t> order;
    if (split_policy == RouteSplitPolicy::PolarOrderSplit) {
        order = order_customers_by_polar_angle(instance, customers, depot_id, matrix, rng);
    } else {
        order = order_customers_by_nearest_chain(customers, depot_id, matrix);
    }

    std::vector<std::vector<node_id_t>> best_active;
    cost_t best_score = std::numeric_limits<cost_t>::infinity();

    const std::size_t max_active = std::min<std::size_t>(salesman_count_for_depot, customers.size());
    for (std::size_t active = 1; active <= max_active; ++active) {
        auto candidate = split_order_evenly(order, active);
        refine_contiguous_boundaries(instance, candidate, depot_id, matrix);
        const cost_t score = score_chunking_proxy(instance, candidate, depot_id, matrix);
        if (score < best_score - kEps) {
            best_score = score;
            best_active = std::move(candidate);
        }
    }

    for (std::size_t i = 0; i < best_active.size(); ++i) {
        chunks[i] = std::move(best_active[i]);
    }
    return chunks;
}

[[nodiscard]] std::vector<std::vector<node_id_t>> build_depot_chunks_for_policy(
    const MDMTSPInstance& instance,
    const std::vector<node_id_t>& customers,
    const depot_id_t depot_id,
    const std::size_t salesman_count_for_depot,
    const DistanceMatrix& matrix,
    Random& rng,
    const RouteSplitPolicy split_policy
) {
    if (split_policy == RouteSplitPolicy::ClusterSplit) {
        return split_depot_group_into_salesmen(
            instance,
            customers,
            depot_id,
            salesman_count_for_depot,
            matrix,
            rng
        );
    }

    if (split_policy == RouteSplitPolicy::PolarOrderSplit ||
        split_policy == RouteSplitPolicy::NearestChainSplit) {
        return split_depot_group_by_order_policy(
            instance,
            customers,
            depot_id,
            salesman_count_for_depot,
            matrix,
            rng,
            split_policy
        );
    }

    const auto cluster_chunks = split_depot_group_into_salesmen(
        instance,
        customers,
        depot_id,
        salesman_count_for_depot,
        matrix,
        rng
    );
    const auto polar_chunks = split_depot_group_by_order_policy(
        instance,
        customers,
        depot_id,
        salesman_count_for_depot,
        matrix,
        rng,
        RouteSplitPolicy::PolarOrderSplit
    );
    const auto nn_chunks = split_depot_group_by_order_policy(
        instance,
        customers,
        depot_id,
        salesman_count_for_depot,
        matrix,
        rng,
        RouteSplitPolicy::NearestChainSplit
    );

    const cost_t cluster_score = score_chunking_proxy(instance, cluster_chunks, depot_id, matrix);
    const cost_t polar_score = score_chunking_proxy(instance, polar_chunks, depot_id, matrix);
    const cost_t nn_score = score_chunking_proxy(instance, nn_chunks, depot_id, matrix);

    if (cluster_score <= polar_score + kEps && cluster_score <= nn_score + kEps) {
        return cluster_chunks;
    }
    if (polar_score <= nn_score + kEps) {
        return polar_chunks;
    }
    return nn_chunks;
}

[[nodiscard]] Route optimize_route(
    const depot_id_t depot_id,
    const std::vector<node_id_t>& customers,
    const DistanceMatrix& matrix,
    const bool return_to_depot,
    bool& lkh_enabled
) {
    Route best_route = build_greedy_route(depot_id, customers, matrix, return_to_depot);
    cost_t best_cost = route_cost(best_route, matrix);

    if (const auto exact = exact_optimize_small_route(depot_id, customers, matrix, return_to_depot);
        exact.has_value()) {
        const cost_t exact_cost = route_cost(*exact, matrix);
        if (exact_cost < best_cost - kEps) {
            best_cost = exact_cost;
            best_route = *exact;
        }
    }

    if (!lkh_enabled || customers.size() < kSmallRouteThreshold) {
        return best_route;
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
        return best_route;
    }

    const cost_t optimized_cost = route_cost(*optimized, matrix);
    if (optimized_cost < best_cost - kEps) {
        return std::move(*optimized);
    }
    return best_route;
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


[[nodiscard]] Route make_trivial_route(
    const depot_id_t depot_id,
    const bool return_to_depot
) {
    Route route{static_cast<node_id_t>(depot_id)};
    if (return_to_depot) {
        route.push_back(static_cast<node_id_t>(depot_id));
    }
    return route;
}

[[nodiscard]] std::vector<node_id_t> extract_customers_from_route(
    const MDMTSPSalesmanRoute& route,
    const MDMTSPInstance& instance
) {
    std::vector<node_id_t> customers;
    customers.reserve(route.nodes.size());
    for (const node_id_t node : route.nodes) {
        if (instance.is_customer_node(node)) {
            customers.push_back(node);
        }
    }
    return customers;
}

[[nodiscard]] std::size_t route_merge_customer_limit(
    const std::size_t customer_count
) noexcept {
    if (customer_count <= 1200) {
        return 900;
    }
    if (customer_count <= 5000) {
        return 420;
    }
    if (customer_count <= 12000) {
        return 240;
    }
    return 120;
}

void consolidate_same_depot_routes(
    MDMTSPSolution& solution,
    const MDMTSPInstance& instance,
    const DistanceMatrix& matrix,
    bool& lkh_enabled
) {
    const std::size_t merge_limit = route_merge_customer_limit(instance.customer_count());

    for (int pass = 0; pass < kRouteMergePasses; ++pass) {
        bool changed = false;
        std::optional<std::size_t> best_i;
        std::optional<std::size_t> best_j;
        Route best_route;
        cost_t best_delta = 0.0;

        for (std::size_t i = 0; i < solution.routes.size(); ++i) {
            const auto& route_i = solution.routes[i];
            auto customers_i = extract_customers_from_route(route_i, instance);
            if (customers_i.empty()) {
                continue;
            }

            for (std::size_t j = i + 1; j < solution.routes.size(); ++j) {
                const auto& route_j = solution.routes[j];
                if (route_i.depot_id != route_j.depot_id) {
                    continue;
                }

                auto customers_j = extract_customers_from_route(route_j, instance);
                if (customers_j.empty()) {
                    continue;
                }

                if (customers_i.size() + customers_j.size() > merge_limit) {
                    continue;
                }

                std::vector<node_id_t> merged = customers_i;
                merged.insert(merged.end(), customers_j.begin(), customers_j.end());

                Route merged_route = optimize_route(
                    route_i.depot_id,
                    merged,
                    matrix,
                    instance.return_to_depot,
                    lkh_enabled
                );

                const cost_t current_cost = route_cost(route_i.nodes, matrix) + route_cost(route_j.nodes, matrix);
                const cost_t merged_cost = route_cost(merged_route, matrix);
                const cost_t delta = merged_cost - current_cost;

                if (delta < best_delta - kEps) {
                    best_delta = delta;
                    best_i = i;
                    best_j = j;
                    best_route = std::move(merged_route);
                }
            }
        }

        if (!best_i.has_value() || !best_j.has_value()) {
            break;
        }

        solution.routes[*best_i].nodes = std::move(best_route);
        solution.routes[*best_j].nodes = make_trivial_route(
            solution.routes[*best_j].depot_id,
            instance.return_to_depot
        );
        changed = true;

        if (!changed) {
            break;
        }
    }
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

struct CandidateSolution {
    MDMTSPSolution solution;
    bool feasible = false;
};

[[nodiscard]] MDMTSPSolution finalize_candidate_solution(
    MDMTSPSolution solution,
    const MDMTSPInstance& instance,
    const DistanceMatrix& matrix,
    bool& lkh_enabled
) {
    if (solution.feasible) {
        consolidate_same_depot_routes(solution, instance, matrix, lkh_enabled);
        reroute_all_routes_with_lkh(solution, instance, matrix, lkh_enabled);
        solution.objective = compute_objective(solution, instance, matrix);
        solution.feasible = is_solution_feasible(instance, solution);
        solution.status = solution.feasible ? "ok" : validation_report(instance, solution);
    }

    const std::size_t relocate_iterations = recommended_relocation_iterations(instance.customer_count());
    if (relocate_iterations > 0 && solution.feasible) {
        improve_interroute_by_relocation(solution, instance, relocate_iterations);
        consolidate_same_depot_routes(solution, instance, matrix, lkh_enabled);
        reroute_all_routes_with_lkh(solution, instance, matrix, lkh_enabled);
        solution.objective = compute_objective(solution, instance, matrix);
        solution.feasible = is_solution_feasible(instance, solution);
        solution.status = solution.feasible ? "ok" : validation_report(instance, solution);
    }

    return solution;
}

[[nodiscard]] std::vector<std::vector<node_id_t>> build_chunks_for_global_policy(
    const MDMTSPInstance& instance,
    const std::vector<std::vector<node_id_t>>& grouped_by_depot,
    const std::vector<std::size_t>& salesmen_per_depot,
    const DistanceMatrix& matrix,
    Random& rng,
    const RouteSplitPolicy split_policy
) {
    std::vector<std::vector<node_id_t>> chunks;
    chunks.reserve(instance.salesman_count);

    for (depot_id_t depot = 0; depot < instance.depot_count(); ++depot) {
        const auto depot_chunks = build_depot_chunks_for_policy(
            instance,
            grouped_by_depot[depot],
            depot,
            salesmen_per_depot[depot],
            matrix,
            rng,
            split_policy
        );
        for (auto chunk : depot_chunks) {
            chunks.push_back(std::move(chunk));
        }
    }

    return chunks;
}

[[nodiscard]] std::optional<MDMTSPSolution> build_candidate_solution(
    const MDMTSPInstance& instance,
    const DistanceMatrix& matrix,
    const std::vector<std::size_t>& salesmen_per_depot,
    const std::vector<depot_id_t>& customer_to_depot,
    Random& rng,
    const RouteSplitPolicy split_policy
) {
    const auto grouped_by_depot = group_customers_by_depot(instance, customer_to_depot);
    const auto salesman_to_depot = expand_salesman_to_depot(salesmen_per_depot);
    auto chunks = build_chunks_for_global_policy(
        instance,
        grouped_by_depot,
        salesmen_per_depot,
        matrix,
        rng,
        split_policy
    );

    if (chunks.size() != instance.salesman_count || salesman_to_depot.size() != instance.salesman_count) {
        return std::nullopt;
    }

    bool lkh_enabled = true;
    auto solution = build_solution_from_partition(
        instance,
        salesman_to_depot,
        chunks,
        matrix,
        lkh_enabled
    );
    solution = finalize_candidate_solution(std::move(solution), instance, matrix, lkh_enabled);
    return solution;
}


[[nodiscard]] std::array<RouteSplitPolicy, 4> portfolio_split_policies() {
    return {
        RouteSplitPolicy::LocalBest,
        RouteSplitPolicy::ClusterSplit,
        RouteSplitPolicy::PolarOrderSplit,
        RouteSplitPolicy::NearestChainSplit
    };
}

[[nodiscard]] std::vector<depot_id_t> assign_all_customers_to_single_depot(
    const MDMTSPInstance& instance,
    const depot_id_t depot
) {
    return std::vector<depot_id_t>(instance.customer_count(), depot);
}

[[nodiscard]] cost_t total_distance_from_depot_to_all_customers(
    const MDMTSPInstance& instance,
    const DistanceMatrix& matrix,
    const depot_id_t depot
) {
    cost_t total = 0.0;
    for (std::size_t customer_idx = 0; customer_idx < instance.customer_count(); ++customer_idx) {
        total += matrix[depot][instance.customer_node_from_index(customer_idx)];
    }
    return total;
}

[[nodiscard]] std::vector<depot_id_t> dominant_depot_sequence(
    const MDMTSPInstance& instance,
    const DistanceMatrix& matrix
) {
    std::vector<std::pair<cost_t, depot_id_t>> ranked;
    ranked.reserve(instance.depot_count());
    for (depot_id_t depot = 0; depot < instance.depot_count(); ++depot) {
        ranked.emplace_back(total_distance_from_depot_to_all_customers(instance, matrix, depot), depot);
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.first != rhs.first) {
            return lhs.first < rhs.first;
        }
        return lhs.second < rhs.second;
    });

    const std::size_t limit = instance.customer_count() <= kSmallInstanceDominantThreshold
        ? instance.depot_count()
        : std::min<std::size_t>(instance.depot_count(), kDominantDepotCandidateLimit);

    std::vector<depot_id_t> out;
    out.reserve(limit);
    for (std::size_t i = 0; i < limit; ++i) {
        out.push_back(ranked[i].second);
    }
    return out;
}

[[nodiscard]] std::vector<std::size_t> build_customer_order_by_xy(const MDMTSPInstance& instance) {
    std::vector<std::size_t> order(instance.customer_count());
    std::iota(order.begin(), order.end(), static_cast<std::size_t>(0));
    std::sort(order.begin(), order.end(), [&](const std::size_t lhs, const std::size_t rhs) {
        const Point2D pl = instance.point_of(instance.customer_node_from_index(lhs));
        const Point2D pr = instance.point_of(instance.customer_node_from_index(rhs));
        if (pl.x != pr.x) {
            return pl.x < pr.x;
        }
        if (pl.y != pr.y) {
            return pl.y < pr.y;
        }
        return lhs < rhs;
    });
    return order;
}

[[nodiscard]] std::vector<std::size_t> build_customer_order_by_yx(const MDMTSPInstance& instance) {
    std::vector<std::size_t> order(instance.customer_count());
    std::iota(order.begin(), order.end(), static_cast<std::size_t>(0));
    std::sort(order.begin(), order.end(), [&](const std::size_t lhs, const std::size_t rhs) {
        const Point2D pl = instance.point_of(instance.customer_node_from_index(lhs));
        const Point2D pr = instance.point_of(instance.customer_node_from_index(rhs));
        if (pl.y != pr.y) {
            return pl.y < pr.y;
        }
        if (pl.x != pr.x) {
            return pl.x < pr.x;
        }
        return lhs < rhs;
    });
    return order;
}

[[nodiscard]] std::vector<std::size_t> build_customer_order_by_polar(const MDMTSPInstance& instance) {
    std::vector<node_id_t> nodes;
    nodes.reserve(instance.customer_count());
    for (std::size_t customer_idx = 0; customer_idx < instance.customer_count(); ++customer_idx) {
        nodes.push_back(instance.customer_node_from_index(customer_idx));
    }
    const Point2D global_centroid = centroid_of_nodes(instance, nodes, Point2D{});

    std::vector<std::size_t> order(instance.customer_count());
    std::iota(order.begin(), order.end(), static_cast<std::size_t>(0));
    std::sort(order.begin(), order.end(), [&](const std::size_t lhs, const std::size_t rhs) {
        const Point2D pl = instance.point_of(instance.customer_node_from_index(lhs));
        const Point2D pr = instance.point_of(instance.customer_node_from_index(rhs));
        const double al = std::atan2(pl.y - global_centroid.y, pl.x - global_centroid.x);
        const double ar = std::atan2(pr.y - global_centroid.y, pr.x - global_centroid.x);
        if (al != ar) {
            return al < ar;
        }
        const cost_t rl = euclidean_distance(pl, global_centroid);
        const cost_t rr = euclidean_distance(pr, global_centroid);
        if (rl != rr) {
            return rl < rr;
        }
        return lhs < rhs;
    });
    return order;
}

[[nodiscard]] std::vector<std::size_t> build_customer_order_by_nearest_chain(
    const MDMTSPInstance& instance,
    const DistanceMatrix& matrix
) {
    std::vector<std::size_t> order;
    order.reserve(instance.customer_count());
    if (instance.customer_count() == 0) {
        return order;
    }

    std::vector<bool> used(instance.customer_count(), false);
    std::size_t current_idx = 0;
    cost_t best_start = std::numeric_limits<cost_t>::infinity();
    for (std::size_t customer_idx = 0; customer_idx < instance.customer_count(); ++customer_idx) {
        const node_id_t node = instance.customer_node_from_index(customer_idx);
        cost_t nearest = std::numeric_limits<cost_t>::infinity();
        for (depot_id_t depot = 0; depot < instance.depot_count(); ++depot) {
            nearest = std::min(nearest, matrix[depot][node]);
        }
        if (nearest < best_start) {
            best_start = nearest;
            current_idx = customer_idx;
        }
    }

    order.push_back(current_idx);
    used[current_idx] = true;

    while (order.size() < instance.customer_count()) {
        const node_id_t current_node = instance.customer_node_from_index(current_idx);
        std::size_t best_next = current_idx;
        cost_t best_distance = std::numeric_limits<cost_t>::infinity();
        for (std::size_t customer_idx = 0; customer_idx < instance.customer_count(); ++customer_idx) {
            if (used[customer_idx]) {
                continue;
            }
            const node_id_t node = instance.customer_node_from_index(customer_idx);
            const cost_t distance = matrix[current_node][node];
            if (distance < best_distance) {
                best_distance = distance;
                best_next = customer_idx;
            }
        }
        current_idx = best_next;
        order.push_back(current_idx);
        used[current_idx] = true;
    }

    return order;
}

[[nodiscard]] std::vector<std::size_t> sampled_block_starts(
    const std::size_t order_size,
    const std::size_t block_size,
    const std::size_t max_samples
) {
    std::vector<std::size_t> starts;
    if (order_size == 0 || block_size == 0 || block_size > order_size) {
        return starts;
    }

    const std::size_t max_start = order_size - block_size + 1U;
    if (max_start <= max_samples) {
        starts.resize(max_start);
        std::iota(starts.begin(), starts.end(), static_cast<std::size_t>(0));
        return starts;
    }

    starts.reserve(max_samples);
    const double step = static_cast<double>(max_start - 1U) / static_cast<double>(max_samples - 1U);
    for (std::size_t i = 0; i < max_samples; ++i) {
        std::size_t pos = static_cast<std::size_t>(std::llround(step * static_cast<double>(i)));
        if (pos >= max_start) {
            pos = max_start - 1U;
        }
        if (starts.empty() || starts.back() != pos) {
            starts.push_back(pos);
        }
    }
    return starts;
}

[[nodiscard]] std::vector<depot_id_t> assignment_same_as_previous(
    const std::vector<depot_id_t>& assignment,
    const std::vector<std::size_t>& order,
    const std::size_t start,
    const std::size_t block_size
) {
    auto out = assignment;
    if (order.empty() || start == 0 || start + block_size > order.size()) {
        return out;
    }
    const depot_id_t depot = assignment[order[start - 1U]];
    for (std::size_t i = 0; i < block_size; ++i) {
        out[order[start + i]] = depot;
    }
    return out;
}

[[nodiscard]] std::vector<depot_id_t> assignment_same_as_next(
    const std::vector<depot_id_t>& assignment,
    const std::vector<std::size_t>& order,
    const std::size_t start,
    const std::size_t block_size
) {
    auto out = assignment;
    if (order.empty() || start + block_size >= order.size()) {
        return out;
    }
    const depot_id_t depot = assignment[order[start + block_size]];
    for (std::size_t i = 0; i < block_size; ++i) {
        out[order[start + i]] = depot;
    }
    return out;
}

[[nodiscard]] std::vector<depot_id_t> assignment_reverse_block(
    const std::vector<depot_id_t>& assignment,
    const std::vector<std::size_t>& order,
    const std::size_t start,
    const std::size_t block_size
) {
    auto out = assignment;
    if (order.empty() || start + block_size > order.size()) {
        return out;
    }
    for (std::size_t i = 0; i < block_size; ++i) {
        out[order[start + i]] = assignment[order[start + block_size - 1U - i]];
    }
    return out;
}

[[nodiscard]] bool assignment_equal(
    const std::vector<depot_id_t>& lhs,
    const std::vector<depot_id_t>& rhs
) {
    return lhs == rhs;
}

[[nodiscard]] bool solution_better_than(
    const MDMTSPSolution& lhs,
    const MDMTSPSolution& rhs
) noexcept;

[[nodiscard]] std::optional<MDMTSPSolution> evaluate_assignment_portfolio(
    const MDMTSPInstance& instance,
    const DistanceMatrix& matrix,
    const std::vector<std::size_t>& salesmen_per_depot,
    const std::vector<depot_id_t>& customer_to_depot,
    Random& rng
) {
    std::optional<MDMTSPSolution> best;
    for (const auto split_policy : portfolio_split_policies()) {
        auto candidate = build_candidate_solution(
            instance,
            matrix,
            salesmen_per_depot,
            customer_to_depot,
            rng,
            split_policy
        );
        if (!candidate.has_value()) {
            continue;
        }
        if (!best.has_value() || solution_better_than(*candidate, *best)) {
            best = std::move(*candidate);
        }
    }
    return best;
}

struct AssignmentIncumbent {
    std::vector<depot_id_t> assignment;
    MDMTSPSolution solution;
};

[[nodiscard]] std::optional<AssignmentIncumbent> improve_assignment_by_gvns(
    const MDMTSPInstance& instance,
    const DistanceMatrix& matrix,
    const std::vector<std::size_t>& salesmen_per_depot,
    std::vector<depot_id_t> start_assignment,
    Random& rng
) {
    auto incumbent_solution = evaluate_assignment_portfolio(
        instance,
        matrix,
        salesmen_per_depot,
        start_assignment,
        rng
    );
    if (!incumbent_solution.has_value()) {
        return std::nullopt;
    }

    AssignmentIncumbent incumbent{std::move(start_assignment), std::move(*incumbent_solution)};

    if (instance.customer_count() == 0 || instance.customer_count() > kAssignmentRefinementCustomerThreshold) {
        return incumbent;
    }

    std::vector<std::vector<std::size_t>> orderings;
    orderings.push_back(build_customer_order_by_xy(instance));
    orderings.push_back(build_customer_order_by_yx(instance));
    orderings.push_back(build_customer_order_by_polar(instance));
    orderings.push_back(build_customer_order_by_nearest_chain(instance, matrix));

    for (int pass = 0; pass < kAssignmentGvnsPasses; ++pass) {
        bool improved = false;

        for (const auto& order : orderings) {
            for (std::size_t block_size = 1; block_size <= 3; ++block_size) {
                const auto starts = sampled_block_starts(order.size(), block_size, kAssignmentNeighborhoodSampleLimit);
                for (const std::size_t start : starts) {
                    const std::array<std::vector<depot_id_t>, 3> neighbours{
                        assignment_same_as_previous(incumbent.assignment, order, start, block_size),
                        assignment_same_as_next(incumbent.assignment, order, start, block_size),
                        assignment_reverse_block(incumbent.assignment, order, start, block_size)
                    };

                    for (const auto& neighbour_assignment : neighbours) {
                        if (assignment_equal(neighbour_assignment, incumbent.assignment)) {
                            continue;
                        }

                        auto candidate_solution = evaluate_assignment_portfolio(
                            instance,
                            matrix,
                            salesmen_per_depot,
                            neighbour_assignment,
                            rng
                        );
                        if (!candidate_solution.has_value()) {
                            continue;
                        }

                        if (solution_better_than(*candidate_solution, incumbent.solution)) {
                            incumbent.assignment = neighbour_assignment;
                            incumbent.solution = std::move(*candidate_solution);
                            improved = true;
                            goto restart_pass;
                        }
                    }
                }
            }
        }

restart_pass:
        if (!improved) {
            break;
        }
    }

    return incumbent;
}

[[nodiscard]] bool solution_better_than(
    const MDMTSPSolution& lhs,
    const MDMTSPSolution& rhs
) noexcept {
    if (lhs.feasible != rhs.feasible) {
        return lhs.feasible && !rhs.feasible;
    }
    if (lhs.objective != rhs.objective) {
        return lhs.objective < rhs.objective - kEps;
    }
    return lhs.routes.size() < rhs.routes.size();
}

}  // namespace

[[nodiscard]] MDMTSPSolution solve_mdmtsp_lkh_mdmtsp_v5_portfolio(
    const MDMTSPInstance& instance,
    Random& rng
) {
    instance.validate_basic();

    const DistanceMatrix matrix = instance.build_distance_matrix();
    const auto salesmen_per_depot = allocate_salesmen_to_depots_by_pull(instance, matrix);
    const auto depot_customer_caps =
        derive_customer_caps_from_salesmen(instance.customer_count(), salesmen_per_depot);

    std::vector<std::vector<depot_id_t>> assignment_candidates;
    assignment_candidates.push_back(assign_customers_to_nearest_depots(instance, matrix));
    assignment_candidates.push_back(assign_customers_to_depots_balanced(instance, matrix, depot_customer_caps, rng));

    for (const depot_id_t depot : dominant_depot_sequence(instance, matrix)) {
        assignment_candidates.push_back(assign_all_customers_to_single_depot(instance, depot));
    }

    std::vector<std::vector<depot_id_t>> unique_assignments;
    for (const auto& assignment : assignment_candidates) {
        if (std::find(unique_assignments.begin(), unique_assignments.end(), assignment) == unique_assignments.end()) {
            unique_assignments.push_back(assignment);
        }
    }

    std::optional<MDMTSPSolution> best_solution;

    for (const auto& assignment : unique_assignments) {
        auto improved = improve_assignment_by_gvns(
            instance,
            matrix,
            salesmen_per_depot,
            assignment,
            rng
        );
        if (!improved.has_value()) {
            continue;
        }

        if (!best_solution.has_value() || solution_better_than(improved->solution, *best_solution)) {
            best_solution = std::move(improved->solution);
        }
    }

    if (!best_solution.has_value()) {
        throw std::logic_error("solve_mdmtsp_lkh_mdmtsp_v5_portfolio: failed to build any candidate");
    }

    return *best_solution;
}

}  // namespace mdmtsp
