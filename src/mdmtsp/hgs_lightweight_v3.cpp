
#include "mdmtsp_solver.hpp"

#include <algorithm>
#include <array>
#include <chrono>
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
constexpr std::size_t kSmallRouteThreshold = 8;
constexpr int kClusteringRefinementPasses = 1;
constexpr int kMaxTwoOptPasses = 2;
constexpr std::size_t kMinMeaningfulChunkSize = 8;
constexpr int kSplitRefinementPasses = 1;
constexpr int kRouteMergePasses = 1;
constexpr cost_t kBoundedStretchMultiplier = static_cast<cost_t>(1.20);
constexpr cost_t kBoundedStretchAdditiveScale = static_cast<cost_t>(1.75);
constexpr cost_t kRouteOpeningPenaltyFactor = static_cast<cost_t>(1.10);
constexpr std::size_t kExactClusterProxyThreshold = 180;
constexpr std::size_t kExactRouteThreshold = 12;
constexpr cost_t kPortfolioSegmentPenaltyFactor = static_cast<cost_t>(0.45);
constexpr int kBoundaryRefinementPasses = 2;
constexpr std::size_t kDominantDepotCandidateLimit = 2;
constexpr std::size_t kSmallInstanceDominantThreshold = 32;
constexpr std::size_t kAssignmentRefinementCustomerThreshold = 120;
constexpr std::size_t kAssignmentNeighborhoodSampleLimit = 6;
constexpr int kAssignmentGvnsPasses = 1;
constexpr std::size_t kLkhMinRouteSize = 24;
constexpr std::size_t kPortfolioTopPolicies = 2;
constexpr std::size_t kNeighbourEvaluationBudget = 12;
constexpr std::size_t kFastSmallInstanceThreshold = 80;
constexpr cost_t kHgsV2AssignedRegretWeight = static_cast<cost_t>(0.35);
constexpr cost_t kHgsV2MeanDistanceWeight = static_cast<cost_t>(1.10);
constexpr cost_t kHgsV2VarianceWeight = static_cast<cost_t>(0.025);
constexpr cost_t kHgsV2RoutePressureWeight = static_cast<cost_t>(0.08);
constexpr cost_t kHgsV2OverloadWeight = static_cast<cost_t>(0.04);
constexpr cost_t kHgsV2DiversityHammingWeight = static_cast<cost_t>(0.016);
constexpr cost_t kHgsV2DiversityLoadWeight = static_cast<cost_t>(0.012);
constexpr cost_t kHgsV2BoundaryPatchRatio = static_cast<cost_t>(0.04);
constexpr cost_t kHgsV3MateDiversityHammingWeight = static_cast<cost_t>(0.020);
constexpr cost_t kHgsV3MateDiversityLoadWeight = static_cast<cost_t>(0.015);
constexpr cost_t kHgsV3LocalOverloadWeight = static_cast<cost_t>(0.030);
constexpr cost_t kHgsV3LocalRoutePressureWeight = static_cast<cost_t>(0.055);
constexpr cost_t kHgsV3EliteCandidateBias = static_cast<cost_t>(0.975);
constexpr std::size_t kHgsV3MateSampleCount = 4;
constexpr std::size_t kHgsV3TargetedRepairBaseBudget = 6;
constexpr cost_t kHgsV3TargetedRepairRatio = static_cast<cost_t>(0.00045);
constexpr std::size_t kHgsV3TargetedRepairMaxBudget = 18;

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
        48ULL,
        static_cast<std::uint64_t>(problem.n) * 6ULL
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

    if (!lkh_enabled || customers.size() < kLkhMinRouteSize) {
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
    if (customer_count <= kFastSmallInstanceThreshold) {
        return 0;
    }
    if (customer_count <= 300) {
        return 8;
    }
    if (customer_count <= 1500) {
        return 4;
    }
    if (customer_count <= 5000) {
        return 2;
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
    if (!solution.feasible) {
        return solution;
    }

    const bool fast_small_mode = instance.customer_count() <= kFastSmallInstanceThreshold;

    consolidate_same_depot_routes(solution, instance, matrix, lkh_enabled);
    if (!fast_small_mode) {
        reroute_all_routes_with_lkh(solution, instance, matrix, lkh_enabled);
    }

    solution.objective = compute_objective(solution, instance, matrix);
    solution.feasible = is_solution_feasible(instance, solution);
    solution.status = solution.feasible ? "ok" : validation_report(instance, solution);

    const std::size_t relocate_iterations = fast_small_mode ? 0U : recommended_relocation_iterations(instance.customer_count());
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


[[nodiscard]] std::array<RouteSplitPolicy, 3> portfolio_split_policies() {
    return {
        RouteSplitPolicy::LocalBest,
        RouteSplitPolicy::NearestChainSplit,
        RouteSplitPolicy::ClusterSplit
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
    auto grouped_by_depot = group_customers_by_depot(instance, customer_to_depot);
    const auto salesman_to_depot = expand_salesman_to_depot(salesmen_per_depot);

    std::vector<std::pair<cost_t, RouteSplitPolicy>> ranked;
    ranked.reserve(portfolio_split_policies().size());
    for (const auto split_policy : portfolio_split_policies()) {
        auto chunks = build_chunks_for_global_policy(
            instance, grouped_by_depot, salesmen_per_depot, matrix, rng, split_policy
        );
        cost_t proxy = 0.0;
        for (std::size_t s = 0; s < chunks.size() && s < salesman_to_depot.size(); ++s) {
            proxy += penalized_segment_proxy_cost(instance, chunks[s], salesman_to_depot[s], matrix);
        }
        ranked.emplace_back(proxy, split_policy);
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first < b.first;
        return static_cast<int>(a.second) < static_cast<int>(b.second);
    });

    const std::size_t keep = instance.customer_count() <= kFastSmallInstanceThreshold ? 1U : std::min<std::size_t>(kPortfolioTopPolicies, ranked.size());

    std::optional<MDMTSPSolution> best;
    for (std::size_t i = 0; i < keep; ++i) {
        auto candidate = build_candidate_solution(
            instance, matrix, salesmen_per_depot, customer_to_depot, rng, ranked[i].second
        );
        if (!candidate.has_value()) continue;
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
        instance, matrix, salesmen_per_depot, start_assignment, rng
    );
    if (!incumbent_solution.has_value()) {
        return std::nullopt;
    }

    AssignmentIncumbent incumbent{std::move(start_assignment), std::move(*incumbent_solution)};

    if (instance.customer_count() == 0 || instance.customer_count() > kAssignmentRefinementCustomerThreshold) {
        return incumbent;
    }

    std::vector<std::vector<std::size_t>> orderings;
    orderings.push_back(build_customer_order_by_nearest_chain(instance, matrix));
    if (instance.customer_count() > kSmallInstanceDominantThreshold) {
        orderings.push_back(build_customer_order_by_xy(instance));
    }

    std::size_t eval_budget = instance.customer_count() <= kFastSmallInstanceThreshold ? 6U : kNeighbourEvaluationBudget;

    for (int pass = 0; pass < kAssignmentGvnsPasses && eval_budget > 0; ++pass) {
        bool improved = false;
        for (const auto& order : orderings) {
            const std::size_t max_block = instance.customer_count() <= kFastSmallInstanceThreshold ? 1U : 2U;
            for (std::size_t block_size = 1; block_size <= max_block && eval_budget > 0; ++block_size) {
                const auto starts = sampled_block_starts(order.size(), block_size, instance.customer_count() <= kFastSmallInstanceThreshold ? 3U : kAssignmentNeighborhoodSampleLimit);
                for (const std::size_t start : starts) {
                    const std::array<std::vector<depot_id_t>, 2> neighbours{
                        assignment_same_as_previous(incumbent.assignment, order, start, block_size),
                        assignment_same_as_next(incumbent.assignment, order, start, block_size)
                    };
                    for (const auto& neighbour_assignment : neighbours) {
                        if (eval_budget == 0) break;
                        if (assignment_equal(neighbour_assignment, incumbent.assignment)) continue;
                        --eval_budget;
                        auto candidate_solution = evaluate_assignment_portfolio(
                            instance, matrix, salesmen_per_depot, neighbour_assignment, rng
                        );
                        if (!candidate_solution.has_value()) continue;
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
        if (!improved) break;
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

namespace {

[[nodiscard]] std::uint64_t hgs_splitmix64(std::uint64_t x) noexcept {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27U)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31U);
}

struct HgsAnnealRng {
    std::uint64_t state = 0;

    [[nodiscard]] std::uint64_t next_u64() noexcept {
        state = hgs_splitmix64(state);
        return state;
    }

    [[nodiscard]] double next_unit() noexcept {
        constexpr double denom = static_cast<double>(std::numeric_limits<std::uint64_t>::max());
        return static_cast<double>(next_u64()) / denom;
    }

    [[nodiscard]] std::size_t next_index(const std::size_t upper_exclusive) noexcept {
        if (upper_exclusive == 0) {
            return 0;
        }
        return static_cast<std::size_t>(next_u64() % static_cast<std::uint64_t>(upper_exclusive));
    }
};

[[nodiscard]] std::uint64_t hgs_make_anneal_seed(
    const MDMTSPInstance& instance,
    const std::vector<depot_id_t>& assignment
) noexcept {
    std::uint64_t seed = 1469598103934665603ULL;
    seed ^= static_cast<std::uint64_t>(instance.depot_count()) + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
    seed ^= static_cast<std::uint64_t>(instance.customer_count()) + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
    seed ^= static_cast<std::uint64_t>(instance.salesman_count) + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
    for (std::size_t i = 0; i < assignment.size(); ++i) {
        seed ^= (static_cast<std::uint64_t>(assignment[i]) + 0x9e3779b97f4a7c15ULL +
                 (seed << 6U) + (seed >> 2U) + static_cast<std::uint64_t>(i));
    }
    return hgs_splitmix64(seed);
}

[[nodiscard]] std::int64_t hgs_sa_time_budget_ms(const std::size_t customer_count) noexcept {
    if (customer_count <= 50) {
        return 100;
    }
    if (customer_count <= 100) {
        return 250;
    }
    if (customer_count <= 250) {
        return 750;
    }
    if (customer_count <= 500) {
        return 1500;
    }
    if (customer_count <= 1000) {
        return 4000;
    }
    if (customer_count <= 5000) {
        return 15000;
    }
    if (customer_count <= 10000) {
        return 30000;
    }
    return 60000;
}

[[nodiscard]] std::size_t hgs_sa_block_limit(const std::size_t customer_count) noexcept {
    if (customer_count <= 50) {
        return 2;
    }
    if (customer_count <= 500) {
        return 3;
    }
    return 4;
}

[[nodiscard]] std::size_t hgs_sa_materialize_stride(const std::size_t customer_count) noexcept {
    if (customer_count <= 50) {
        return 1;
    }
    if (customer_count <= 150) {
        return 2;
    }
    if (customer_count <= 1000) {
        return 6;
    }
    if (customer_count <= 5000) {
        return 12;
    }
    return 24;
}

[[nodiscard]] std::size_t hgs_sa_max_attempts(const std::size_t customer_count) noexcept {
    if (customer_count <= 50) {
        return 2000;
    }
    if (customer_count <= 150) {
        return 4000;
    }
    if (customer_count <= 500) {
        return 7000;
    }
    if (customer_count <= 1000) {
        return 12000;
    }
    if (customer_count <= 5000) {
        return 18000;
    }
    return 24000;
}

[[nodiscard]] cost_t hgs_assignment_access_proxy(
    const MDMTSPInstance& instance,
    const DistanceMatrix& matrix,
    const std::vector<depot_id_t>& assignment,
    const std::vector<std::size_t>& salesmen_per_depot
) {
    std::vector<std::size_t> depot_loads(instance.depot_count(), 0);
    std::vector<cost_t> depot_sum_dist(instance.depot_count(), 0.0);
    std::vector<cost_t> depot_sum_sq_dist(instance.depot_count(), 0.0);

    cost_t total = 0.0;
    for (std::size_t customer_idx = 0; customer_idx < instance.customer_count(); ++customer_idx) {
        const depot_id_t assigned_depot = assignment[customer_idx];
        const node_id_t customer = instance.customer_node_from_index(customer_idx);

        cost_t best = std::numeric_limits<cost_t>::infinity();
        cost_t second = std::numeric_limits<cost_t>::infinity();
        cost_t assigned_dist = matrix[assigned_depot][customer];

        for (depot_id_t depot = 0; depot < instance.depot_count(); ++depot) {
            const cost_t d = matrix[depot][customer];
            if (d < best) {
                second = best;
                best = d;
            } else if (d < second) {
                second = d;
            }
        }

        total += assigned_dist;
        total += kHgsV2AssignedRegretWeight * std::max<cost_t>(0.0, assigned_dist - best);
        ++depot_loads[assigned_depot];
        depot_sum_dist[assigned_depot] += assigned_dist;
        depot_sum_sq_dist[assigned_depot] += assigned_dist * assigned_dist;
    }

    const std::size_t total_salesmen = std::max<std::size_t>(1U, instance.salesman_count);
    for (depot_id_t depot = 0; depot < instance.depot_count(); ++depot) {
        const std::size_t load = depot_loads[depot];
        if (load == 0) {
            continue;
        }

        const std::size_t depot_salesmen = std::max<std::size_t>(1U, salesmen_per_depot[depot]);
        const cost_t mean_dist = depot_sum_dist[depot] / static_cast<cost_t>(load);
        const cost_t mean_sq = depot_sum_sq_dist[depot] / static_cast<cost_t>(load);
        const cost_t variance = std::max<cost_t>(0.0, mean_sq - mean_dist * mean_dist);

        total += kHgsV2MeanDistanceWeight * mean_dist;
        total += kHgsV2VarianceWeight * variance;

        const cost_t ideal =
            static_cast<cost_t>(instance.customer_count()) *
            static_cast<cost_t>(salesmen_per_depot[depot]) /
            static_cast<cost_t>(total_salesmen);

        const cost_t overload = std::max<cost_t>(0.0, static_cast<cost_t>(load) - ideal);
        total += kHgsV2OverloadWeight * overload * overload;

        const cost_t route_pressure =
            static_cast<cost_t>(load) / static_cast<cost_t>(depot_salesmen);
        total += kHgsV2RoutePressureWeight * mean_dist * std::sqrt(std::max<cost_t>(1.0, route_pressure));
    }

    return total;
}

[[nodiscard]] std::vector<std::size_t> hgs_assignment_depot_loads(
    const MDMTSPInstance& instance,
    const std::vector<depot_id_t>& assignment
) {
    std::vector<std::size_t> loads(instance.depot_count(), 0);
    for (const depot_id_t depot : assignment) {
        if (depot < loads.size()) {
            ++loads[depot];
        }
    }
    return loads;
}

[[nodiscard]] std::size_t hgs_load_l1_distance(
    const std::vector<std::size_t>& a,
    const std::vector<std::size_t>& b
) noexcept {
    const std::size_t n = std::min(a.size(), b.size());
    std::size_t diff = 0;
    for (std::size_t i = 0; i < n; ++i) {
        diff += (a[i] > b[i] ? a[i] - b[i] : b[i] - a[i]);
    }
    diff += (a.size() > n ? a.size() - n : b.size() - n);
    return diff;
}

[[nodiscard]] std::array<depot_id_t, 2> hgs_nearest_two_depots_for_customer(
    const MDMTSPInstance& instance,
    const DistanceMatrix& matrix,
    const std::size_t customer_idx
) {
    const node_id_t customer = instance.customer_node_from_index(customer_idx);
    depot_id_t best = 0;
    depot_id_t second = 0;
    cost_t best_d = std::numeric_limits<cost_t>::infinity();
    cost_t second_d = std::numeric_limits<cost_t>::infinity();

    for (depot_id_t depot = 0; depot < instance.depot_count(); ++depot) {
        const cost_t d = matrix[depot][customer];
        if (d < best_d) {
            second_d = best_d;
            second = best;
            best_d = d;
            best = depot;
        } else if (d < second_d) {
            second_d = d;
            second = depot;
        }
    }
    return {best, second};
}

[[nodiscard]] std::vector<std::array<depot_id_t, 2>> hgs_precompute_nearest_two_depots(
    const MDMTSPInstance& instance,
    const DistanceMatrix& matrix
) {
    std::vector<std::array<depot_id_t, 2>> out(instance.customer_count());
    for (std::size_t customer_idx = 0; customer_idx < instance.customer_count(); ++customer_idx) {
        out[customer_idx] = hgs_nearest_two_depots_for_customer(instance, matrix, customer_idx);
    }
    return out;
}

[[nodiscard]] cost_t hgs_assignment_local_depot_score(
    const MDMTSPInstance& instance,
    const DistanceMatrix& matrix,
    const std::vector<std::size_t>& salesmen_per_depot,
    const std::vector<std::size_t>& depot_loads,
    const std::vector<std::array<depot_id_t, 2>>& nearest_two_depots,
    const std::size_t customer_idx,
    const depot_id_t depot
) {
    const node_id_t customer = instance.customer_node_from_index(customer_idx);
    const auto nearest = nearest_two_depots[customer_idx];
    const cost_t best_dist = matrix[nearest[0]][customer];
    const cost_t dist = matrix[depot][customer];

    const std::size_t total_salesmen = std::max<std::size_t>(1U, instance.salesman_count);
    const std::size_t depot_salesmen = std::max<std::size_t>(1U, salesmen_per_depot[depot]);
    const cost_t ideal =
        static_cast<cost_t>(instance.customer_count()) *
        static_cast<cost_t>(salesmen_per_depot[depot]) /
        static_cast<cost_t>(total_salesmen);

    const cost_t projected_load = static_cast<cost_t>(depot_loads[depot] + 1U);
    const cost_t overload = std::max<cost_t>(0.0, projected_load - ideal);
    const cost_t route_pressure =
        projected_load / static_cast<cost_t>(depot_salesmen);

    cost_t score = dist;
    score += kHgsV2AssignedRegretWeight * std::max<cost_t>(0.0, dist - best_dist);
    score += kHgsV3LocalOverloadWeight * overload * overload;
    score += kHgsV3LocalRoutePressureWeight * dist * std::sqrt(std::max<cost_t>(1.0, route_pressure));
    return score;
}

[[nodiscard]] std::vector<depot_id_t> hgs_assignment_set_block_to_depot(
    const std::vector<depot_id_t>& assignment,
    const std::vector<std::size_t>& order,
    const std::size_t start,
    const std::size_t block_size,
    const depot_id_t depot
) {
    auto out = assignment;
    if (order.empty() || start + block_size > order.size()) {
        return out;
    }
    for (std::size_t i = 0; i < block_size; ++i) {
        out[order[start + i]] = depot;
    }
    return out;
}

[[nodiscard]] std::vector<depot_id_t> hgs_assignment_swap_two(
    const std::vector<depot_id_t>& assignment,
    const std::size_t a,
    const std::size_t b
) {
    auto out = assignment;
    if (a >= out.size() || b >= out.size() || a == b) {
        return out;
    }
    std::swap(out[a], out[b]);
    return out;
}

[[nodiscard]] cost_t hgs_anneal_solution_value(const MDMTSPSolution& solution) noexcept {
    return solution.feasible ? solution.objective : solution.objective + static_cast<cost_t>(1e12);
}

[[nodiscard]] std::vector<std::vector<std::size_t>> hgs_sa_customer_orders(
    const MDMTSPInstance& instance,
    const DistanceMatrix& matrix
) {
    std::vector<std::vector<std::size_t>> orders;
    orders.push_back(build_customer_order_by_nearest_chain(instance, matrix));
    orders.push_back(build_customer_order_by_polar(instance));
    if (instance.customer_count() <= 1500) {
        orders.push_back(build_customer_order_by_xy(instance));
    }
    return orders;
}

[[nodiscard]] std::vector<depot_id_t> hgs_propose_assignment_sa_move(
    const MDMTSPInstance& instance,
    const DistanceMatrix& matrix,
    const std::vector<depot_id_t>& assignment,
    const std::vector<std::vector<std::size_t>>& orders,
    HgsAnnealRng& arng
) {
    if (assignment.empty()) {
        return assignment;
    }

    const auto& order = orders[arng.next_index(orders.size())];
    const std::size_t max_block = std::min<std::size_t>(hgs_sa_block_limit(instance.customer_count()), order.size());
    const std::size_t block_size = 1U + arng.next_index(std::max<std::size_t>(1U, max_block));
    const std::size_t start_limit = order.size() >= block_size ? order.size() - block_size + 1U : 1U;
    const std::size_t start = arng.next_index(start_limit);

    const std::size_t op = arng.next_index(5);
    switch (op) {
        case 0:
            return assignment_same_as_previous(assignment, order, start, block_size);
        case 1:
            return assignment_same_as_next(assignment, order, start, block_size);
        case 2: {
            const auto depots = hgs_nearest_two_depots_for_customer(instance, matrix, order[start]);
            return hgs_assignment_set_block_to_depot(assignment, order, start, block_size, depots[0]);
        }
        case 3: {
            const auto depots = hgs_nearest_two_depots_for_customer(instance, matrix, order[start]);
            return hgs_assignment_set_block_to_depot(assignment, order, start, block_size, depots[1]);
        }
        default: {
            const std::size_t a = order[start];
            const std::size_t b = order[arng.next_index(order.size())];
            return hgs_assignment_swap_two(assignment, a, b);
        }
    }
}

[[nodiscard]] RouteSplitPolicy choose_light_split_policy(
    const MDMTSPInstance& instance,
    const DistanceMatrix& matrix,
    const std::vector<std::size_t>& salesmen_per_depot,
    const std::vector<depot_id_t>& customer_to_depot,
    Random& rng
) {
    const auto grouped_by_depot = group_customers_by_depot(instance, customer_to_depot);
    const auto salesman_to_depot = expand_salesman_to_depot(salesmen_per_depot);

    std::array<RouteSplitPolicy, 2> candidates{
        RouteSplitPolicy::LocalBest,
        RouteSplitPolicy::NearestChainSplit
    };

    cost_t best_proxy = std::numeric_limits<cost_t>::infinity();
    RouteSplitPolicy best_policy = candidates[0];

    for (const auto policy : candidates) {
        auto chunks = build_chunks_for_global_policy(
            instance,
            grouped_by_depot,
            salesmen_per_depot,
            matrix,
            rng,
            policy
        );

        cost_t proxy = 0.0;
        for (std::size_t s = 0; s < chunks.size() && s < salesman_to_depot.size(); ++s) {
            proxy += penalized_segment_proxy_cost(instance, chunks[s], salesman_to_depot[s], matrix);
        }

        if (proxy < best_proxy - kEps) {
            best_proxy = proxy;
            best_policy = policy;
        }
    }

    return best_policy;
}

[[nodiscard]] std::optional<MDMTSPSolution> hgs_evaluate_assignment_light(
    const MDMTSPInstance& instance,
    const DistanceMatrix& matrix,
    const std::vector<std::size_t>& salesmen_per_depot,
    const std::vector<depot_id_t>& customer_to_depot,
    Random& rng
) {
    const auto grouped_by_depot = group_customers_by_depot(instance, customer_to_depot);
    const auto salesman_to_depot = expand_salesman_to_depot(salesmen_per_depot);

    const auto split_policy = choose_light_split_policy(
        instance, matrix, salesmen_per_depot, customer_to_depot, rng
    );

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

    bool lkh_enabled = false;
    auto solution = build_solution_from_partition(
        instance,
        salesman_to_depot,
        chunks,
        matrix,
        lkh_enabled
    );

    return solution;
}

[[nodiscard]] cost_t hgs_calibrate_initial_temperature(
    const MDMTSPInstance& instance,
    const DistanceMatrix& matrix,
    const std::vector<std::size_t>& salesmen_per_depot,
    const std::vector<depot_id_t>& start_assignment,
    HgsAnnealRng& arng
) {
    const auto orders = hgs_sa_customer_orders(instance, matrix);
    const cost_t start_proxy = hgs_assignment_access_proxy(instance, matrix, start_assignment, salesmen_per_depot);
    std::vector<cost_t> uphill;

    const std::size_t samples = std::min<std::size_t>(48, std::max<std::size_t>(8, instance.customer_count() / 8));
    for (std::size_t s = 0; s < samples; ++s) {
        const auto proposal = hgs_propose_assignment_sa_move(instance, matrix, start_assignment, orders, arng);
        if (proposal == start_assignment) {
            continue;
        }
        const cost_t delta =
            hgs_assignment_access_proxy(instance, matrix, proposal, salesmen_per_depot) - start_proxy;
        if (delta > kEps) {
            uphill.push_back(delta);
        }
    }

    if (uphill.empty()) {
        return std::max<cost_t>(1.0, static_cast<cost_t>(0.02) * std::max<cost_t>(1.0, start_proxy));
    }

    const cost_t mean_delta = std::accumulate(uphill.begin(), uphill.end(), static_cast<cost_t>(0.0)) /
                              static_cast<cost_t>(uphill.size());
    return std::max<cost_t>(1.0, mean_delta / static_cast<cost_t>(-std::log(0.35)));
}


struct HgsIndividual {
    std::vector<depot_id_t> assignment;
    std::vector<std::size_t> depot_loads;
    cost_t proxy = std::numeric_limits<cost_t>::infinity();
    cost_t value = std::numeric_limits<cost_t>::infinity();
    bool materialized = false;
    std::optional<MDMTSPSolution> solution;
};

[[nodiscard]] const std::vector<depot_id_t>* hgs_best_assignment_ptr(
    const std::vector<HgsIndividual>& population
) noexcept {
    if (population.empty()) {
        return nullptr;
    }

    std::size_t best_idx = 0;
    cost_t best_score = std::numeric_limits<cost_t>::infinity();
    for (std::size_t i = 0; i < population.size(); ++i) {
        const cost_t score = population[i].materialized ? population[i].value : population[i].proxy;
        if (score < best_score - kEps) {
            best_score = score;
            best_idx = i;
        }
    }
    return &population[best_idx].assignment;
}

[[nodiscard]] std::size_t hgs_population_size(const std::size_t customer_count) noexcept {
    if (customer_count <= 50) {
        return 8;
    }
    if (customer_count <= 250) {
        return 10;
    }
    if (customer_count <= 1000) {
        return 12;
    }
    return 14;
}

[[nodiscard]] std::size_t hgs_child_limit(const std::size_t customer_count) noexcept {
    if (customer_count <= 50) {
        return 128;
    }
    if (customer_count <= 250) {
        return 192;
    }
    if (customer_count <= 1000) {
        return 256;
    }
    if (customer_count <= 5000) {
        return 320;
    }
    return 384;
}

[[nodiscard]] std::size_t hgs_proxy_refine_steps(const std::size_t customer_count) noexcept {
    if (customer_count <= 50) {
        return 10;
    }
    if (customer_count <= 250) {
        return 8;
    }
    if (customer_count <= 1000) {
        return 6;
    }
    return 4;
}

[[nodiscard]] std::size_t hgs_materialize_stride(const std::size_t customer_count) noexcept {
    if (customer_count <= 50) {
        return 1;
    }
    if (customer_count <= 250) {
        return 2;
    }
    if (customer_count <= 1000) {
        return 3;
    }
    if (customer_count <= 5000) {
        return 5;
    }
    return 8;
}

[[nodiscard]] std::vector<std::vector<depot_id_t>> build_hgs_initial_assignments(
    const MDMTSPInstance& instance,
    const DistanceMatrix& matrix,
    const std::vector<std::size_t>& depot_customer_caps,
    HgsAnnealRng& arng,
    Random& rng
) {
    std::vector<std::vector<depot_id_t>> candidates;
    const auto nearest = assign_customers_to_nearest_depots(instance, matrix);
    const auto balanced = assign_customers_to_depots_balanced(instance, matrix, depot_customer_caps, rng);
    candidates.push_back(nearest);
    candidates.push_back(balanced);

    const auto orders = hgs_sa_customer_orders(instance, matrix);
    for (int i = 0; i < 3; ++i) {
        candidates.push_back(hgs_propose_assignment_sa_move(instance, matrix, nearest, orders, arng));
        candidates.push_back(hgs_propose_assignment_sa_move(instance, matrix, balanced, orders, arng));
    }

    if (instance.customer_count() <= 64) {
        for (const depot_id_t depot : dominant_depot_sequence(instance, matrix)) {
            candidates.push_back(assign_all_customers_to_single_depot(instance, depot));
        }
    }

    std::vector<std::vector<depot_id_t>> unique_assignments;
    for (auto& assignment : candidates) {
        if (std::find(unique_assignments.begin(), unique_assignments.end(), assignment) == unique_assignments.end()) {
            unique_assignments.push_back(std::move(assignment));
        }
    }
    return unique_assignments;
}

[[nodiscard]] std::size_t assignment_hamming_distance(
    const std::vector<depot_id_t>& a,
    const std::vector<depot_id_t>& b
) noexcept {
    const std::size_t n = std::min(a.size(), b.size());
    std::size_t diff = 0;
    for (std::size_t i = 0; i < n; ++i) {
        diff += static_cast<std::size_t>(a[i] != b[i]);
    }
    diff += (a.size() > n ? a.size() - n : b.size() - n);
    return diff;
}

[[nodiscard]] std::size_t tournament_pick(
    const std::vector<HgsIndividual>& population,
    HgsAnnealRng& arng
) noexcept {
    const std::size_t a = arng.next_index(population.size());
    const std::size_t b = arng.next_index(population.size());
    const auto score = [](const HgsIndividual& ind) {
        return ind.materialized ? ind.value : ind.proxy;
    };
    return score(population[a]) <= score(population[b]) ? a : b;
}

[[nodiscard]] std::size_t select_diverse_mate(
    const std::vector<HgsIndividual>& population,
    const std::size_t anchor_idx,
    HgsAnnealRng& arng
) noexcept {
    if (population.size() <= 1) {
        return anchor_idx;
    }

    std::size_t best_idx = anchor_idx == 0 ? 1 : 0;
    cost_t best_rank = std::numeric_limits<cost_t>::infinity();
    const std::size_t rounds = std::min<std::size_t>(kHgsV3MateSampleCount, population.size() - 1U);

    for (std::size_t r = 0; r < rounds * 2U; ++r) {
        const std::size_t cand = arng.next_index(population.size());
        if (cand == anchor_idx) {
            continue;
        }
        const cost_t quality = population[cand].materialized ? population[cand].value : population[cand].proxy;
        const std::size_t hamming = assignment_hamming_distance(population[anchor_idx].assignment, population[cand].assignment);
        const std::size_t load_dist = hgs_load_l1_distance(population[anchor_idx].depot_loads, population[cand].depot_loads);
        const cost_t rank = quality
            - kHgsV3MateDiversityHammingWeight * static_cast<cost_t>(hamming)
            - kHgsV3MateDiversityLoadWeight * static_cast<cost_t>(load_dist);
        if (rank < best_rank - kEps) {
            best_rank = rank;
            best_idx = cand;
        }
    }

    if (best_idx == anchor_idx) {
        best_idx = (anchor_idx + 1U) % population.size();
    }
    return best_idx;
}

[[nodiscard]] std::vector<depot_id_t> hgs_assignment_crossover(
    const MDMTSPInstance& instance,
    const DistanceMatrix& matrix,
    const std::vector<std::size_t>& salesmen_per_depot,
    const std::vector<std::array<depot_id_t, 2>>& nearest_two_depots,
    const std::vector<depot_id_t>& parent_a,
    const std::vector<depot_id_t>& parent_b,
    const std::vector<depot_id_t>* elite_assignment,
    const std::vector<std::vector<std::size_t>>& orders,
    HgsAnnealRng& arng
) {
    if (parent_a.empty()) {
        return parent_b;
    }
    if (parent_b.empty()) {
        return parent_a;
    }

    std::vector<depot_id_t> child = parent_b;
    const auto& order = orders[arng.next_index(orders.size())];
    if (order.empty()) {
        return child;
    }

    const std::size_t max_block = std::max<std::size_t>(
        1,
        std::min<std::size_t>(order.size(), hgs_sa_block_limit(instance.customer_count()) * 3)
    );
    const std::size_t block = 1U + arng.next_index(max_block);
    const std::size_t start_limit = order.size() >= block ? order.size() - block + 1U : 1U;
    const std::size_t start = arng.next_index(start_limit);

    for (std::size_t i = 0; i < block && start + i < order.size(); ++i) {
        const std::size_t customer_idx = order[start + i];
        child[customer_idx] = parent_a[customer_idx];
    }

    const std::size_t patch_count = std::min<std::size_t>(
        std::max<std::size_t>(1U, instance.customer_count() / 20U),
        8U
    );
    for (std::size_t p = 0; p < patch_count; ++p) {
        const std::size_t idx = arng.next_index(order.size());
        child[order[idx]] = parent_a[order[idx]];
    }

    auto child_loads = hgs_assignment_depot_loads(instance, child);
    const std::size_t guidance_budget = std::min<std::size_t>(
        std::max<std::size_t>(2U, static_cast<std::size_t>(std::ceil(kHgsV2BoundaryPatchRatio * static_cast<cost_t>(instance.customer_count())))),
        std::min<std::size_t>(32U, instance.customer_count())
    );

    std::size_t applied = 0;
    for (std::size_t t = 0; t < order.size() && applied < guidance_budget; ++t) {
        const std::size_t customer_idx = order[t];
        const bool parent_disagreement = parent_a[customer_idx] != parent_b[customer_idx];
        const bool elite_disagreement = elite_assignment != nullptr && (*elite_assignment)[customer_idx] != child[customer_idx];
        if (!parent_disagreement && !elite_disagreement) {
            continue;
        }

        const depot_id_t current_depot = child[customer_idx];
        if (current_depot < child_loads.size() && child_loads[current_depot] > 0U) {
            --child_loads[current_depot];
        }

        std::array<depot_id_t, 5> raw_candidates{
            parent_a[customer_idx],
            parent_b[customer_idx],
            nearest_two_depots[customer_idx][0],
            nearest_two_depots[customer_idx][1],
            elite_assignment != nullptr ? (*elite_assignment)[customer_idx] : current_depot
        };
        std::vector<depot_id_t> candidates;
        candidates.reserve(raw_candidates.size());
        for (const depot_id_t depot : raw_candidates) {
            if (depot >= instance.depot_count()) {
                continue;
            }
            if (std::find(candidates.begin(), candidates.end(), depot) == candidates.end()) {
                candidates.push_back(depot);
            }
        }
        if (std::find(candidates.begin(), candidates.end(), current_depot) == candidates.end()) {
            candidates.push_back(current_depot);
        }

        depot_id_t best_depot = current_depot;
        cost_t best_score = std::numeric_limits<cost_t>::infinity();
        for (const depot_id_t depot : candidates) {
            cost_t score = hgs_assignment_local_depot_score(
                instance,
                matrix,
                salesmen_per_depot,
                child_loads,
                nearest_two_depots,
                customer_idx,
                depot
            );
            if (elite_assignment != nullptr && depot == (*elite_assignment)[customer_idx]) {
                score *= kHgsV3EliteCandidateBias;
            }
            if (score < best_score - kEps) {
                best_score = score;
                best_depot = depot;
            }
        }

        child[customer_idx] = best_depot;
        ++child_loads[best_depot];
        ++applied;
    }

    return child;
}

[[nodiscard]] std::vector<depot_id_t> hgs_mutate_assignment(
    const MDMTSPInstance& instance,
    const DistanceMatrix& matrix,
    const std::vector<depot_id_t>& assignment,
    const std::vector<std::vector<std::size_t>>& orders,
    HgsAnnealRng& arng
) {
    auto out = assignment;
    const std::size_t moves = 1U + arng.next_index(instance.customer_count() <= 100 ? 2U : 3U);
    for (std::size_t m = 0; m < moves; ++m) {
        out = hgs_propose_assignment_sa_move(instance, matrix, out, orders, arng);
    }
    return out;
}

[[nodiscard]] std::vector<depot_id_t> hgs_targeted_assignment_repair(
    const MDMTSPInstance& instance,
    const DistanceMatrix& matrix,
    const std::vector<std::size_t>& salesmen_per_depot,
    const std::vector<std::array<depot_id_t, 2>>& nearest_two_depots,
    const std::vector<depot_id_t>& assignment,
    const std::vector<depot_id_t>* elite_assignment,
    const std::vector<std::vector<std::size_t>>& orders,
    HgsAnnealRng& arng
) {
    if (assignment.empty()) {
        return assignment;
    }

    auto out = assignment;
    auto depot_loads = hgs_assignment_depot_loads(instance, out);
    const auto& order = orders[arng.next_index(orders.size())];
    if (order.empty()) {
        return out;
    }

    const std::size_t budget = std::min<std::size_t>(
        std::max<std::size_t>(
            kHgsV3TargetedRepairBaseBudget,
            static_cast<std::size_t>(std::ceil(kHgsV3TargetedRepairRatio * static_cast<cost_t>(instance.customer_count())))
        ),
        std::min<std::size_t>(kHgsV3TargetedRepairMaxBudget, instance.customer_count())
    );

    for (std::size_t step = 0; step < budget; ++step) {
        const std::size_t customer_idx = order[arng.next_index(order.size())];
        const depot_id_t current_depot = out[customer_idx];
        if (current_depot >= instance.depot_count()) {
            continue;
        }

        if (depot_loads[current_depot] == 0U) {
            continue;
        }
        --depot_loads[current_depot];

        const cost_t current_score = hgs_assignment_local_depot_score(
            instance,
            matrix,
            salesmen_per_depot,
            depot_loads,
            nearest_two_depots,
            customer_idx,
            current_depot
        );

        std::array<depot_id_t, 4> raw_candidates{
            current_depot,
            nearest_two_depots[customer_idx][0],
            nearest_two_depots[customer_idx][1],
            elite_assignment != nullptr ? (*elite_assignment)[customer_idx] : current_depot
        };
        std::vector<depot_id_t> candidates;
        candidates.reserve(raw_candidates.size());
        for (const depot_id_t depot : raw_candidates) {
            if (depot >= instance.depot_count()) {
                continue;
            }
            if (std::find(candidates.begin(), candidates.end(), depot) == candidates.end()) {
                candidates.push_back(depot);
            }
        }

        depot_id_t best_depot = current_depot;
        cost_t best_score = current_score;
        for (const depot_id_t depot : candidates) {
            cost_t score = hgs_assignment_local_depot_score(
                instance,
                matrix,
                salesmen_per_depot,
                depot_loads,
                nearest_two_depots,
                customer_idx,
                depot
            );
            if (elite_assignment != nullptr && depot == (*elite_assignment)[customer_idx]) {
                score *= kHgsV3EliteCandidateBias;
            }
            if (score < best_score - kEps) {
                best_score = score;
                best_depot = depot;
            }
        }

        out[customer_idx] = best_depot;
        ++depot_loads[best_depot];
    }

    return out;
}

[[nodiscard]] std::vector<depot_id_t> hgs_proxy_refine_assignment(
    const MDMTSPInstance& instance,
    const DistanceMatrix& matrix,
    const std::vector<std::size_t>& salesmen_per_depot,
    const std::vector<depot_id_t>& start_assignment,
    const std::vector<std::vector<std::size_t>>& orders,
    HgsAnnealRng& arng
) {
    auto best = start_assignment;
    cost_t best_proxy = hgs_assignment_access_proxy(instance, matrix, best, salesmen_per_depot);
    auto current = best;

    const std::size_t steps = hgs_proxy_refine_steps(instance.customer_count());
    for (std::size_t step = 0; step < steps; ++step) {
        const auto proposal = hgs_propose_assignment_sa_move(instance, matrix, current, orders, arng);
        if (proposal == current) {
            continue;
        }
        const cost_t proxy = hgs_assignment_access_proxy(instance, matrix, proposal, salesmen_per_depot);
        if (proxy < best_proxy - kEps) {
            best_proxy = proxy;
            best = proposal;
            current = proposal;
        }
    }
    return best;
}

void materialize_individual(
    HgsIndividual& individual,
    const MDMTSPInstance& instance,
    const DistanceMatrix& matrix,
    const std::vector<std::size_t>& salesmen_per_depot,
    Random& rng
) {
    if (individual.materialized) {
        return;
    }
    auto candidate = hgs_evaluate_assignment_light(instance, matrix, salesmen_per_depot, individual.assignment, rng);
    if (candidate.has_value()) {
        individual.value = hgs_anneal_solution_value(*candidate);
        individual.solution = std::move(candidate);
    } else {
        individual.value = std::numeric_limits<cost_t>::infinity();
        individual.solution.reset();
    }
    individual.materialized = true;
}

void trim_population(
    std::vector<HgsIndividual>& population,
    const std::size_t population_limit
) {
    if (population.size() <= population_limit) {
        return;
    }

    std::vector<bool> keep(population.size(), false);

    std::size_t elite = 0;
    cost_t elite_score = std::numeric_limits<cost_t>::infinity();
    for (std::size_t i = 0; i < population.size(); ++i) {
        const cost_t score = population[i].materialized ? population[i].value : population[i].proxy;
        if (score < elite_score - kEps) {
            elite_score = score;
            elite = i;
        }
    }
    keep[elite] = true;

    std::vector<std::size_t> selected{elite};
    while (selected.size() < population_limit) {
        std::size_t best_idx = population.size();
        cost_t best_rank = std::numeric_limits<cost_t>::infinity();

        for (std::size_t i = 0; i < population.size(); ++i) {
            if (keep[i]) {
                continue;
            }
            const cost_t quality = population[i].materialized ? population[i].value : population[i].proxy;
            std::size_t min_hamming = std::numeric_limits<std::size_t>::max();
            std::size_t min_load_dist = std::numeric_limits<std::size_t>::max();
            for (const auto idx : selected) {
                min_hamming = std::min(
                    min_hamming,
                    assignment_hamming_distance(population[i].assignment, population[idx].assignment)
                );
                min_load_dist = std::min(
                    min_load_dist,
                    hgs_load_l1_distance(population[i].depot_loads, population[idx].depot_loads)
                );
            }
            const cost_t diversity_bonus =
                kHgsV2DiversityHammingWeight * static_cast<cost_t>(min_hamming) +
                kHgsV2DiversityLoadWeight * static_cast<cost_t>(min_load_dist);
            const cost_t rank = quality - diversity_bonus;
            if (rank < best_rank - kEps) {
                best_rank = rank;
                best_idx = i;
            }
        }

        if (best_idx >= population.size()) {
            break;
        }
        keep[best_idx] = true;
        selected.push_back(best_idx);
    }

    std::vector<HgsIndividual> trimmed;
    trimmed.reserve(selected.size());
    for (std::size_t i = 0; i < population.size(); ++i) {
        if (keep[i]) {
            trimmed.push_back(std::move(population[i]));
        }
    }
    population = std::move(trimmed);
}

}  // namespace

[[nodiscard]] MDMTSPSolution solve_mdmtsp_hgs_lightweight_v3(
    const MDMTSPInstance& instance,
    Random& rng
) {
    instance.validate_basic();

    const DistanceMatrix matrix = instance.build_distance_matrix();
    const auto salesmen_per_depot = allocate_salesmen_to_depots_by_pull(instance, matrix);
    const auto depot_customer_caps =
        derive_customer_caps_from_salesmen(instance.customer_count(), salesmen_per_depot);

    HgsAnnealRng arng{hgs_make_anneal_seed(instance, assign_customers_to_nearest_depots(instance, matrix))};
    const auto orders = hgs_sa_customer_orders(instance, matrix);
    const auto nearest_two_depots = hgs_precompute_nearest_two_depots(instance, matrix);

    auto seed_assignments = build_hgs_initial_assignments(
        instance,
        matrix,
        depot_customer_caps,
        arng,
        rng
    );

    std::vector<HgsIndividual> population;
    population.reserve(seed_assignments.size());

    std::optional<MDMTSPSolution> best_solution;
    cost_t best_proxy_seen = std::numeric_limits<cost_t>::infinity();
    const std::size_t pop_limit = hgs_population_size(instance.customer_count());
    const std::size_t materialize_stride = hgs_materialize_stride(instance.customer_count());

    for (auto& assignment : seed_assignments) {
        HgsIndividual ind;
        ind.assignment = hgs_proxy_refine_assignment(
            instance,
            matrix,
            salesmen_per_depot,
            assignment,
            orders,
            arng
        );
        ind.depot_loads = hgs_assignment_depot_loads(instance, ind.assignment);
        ind.proxy = hgs_assignment_access_proxy(instance, matrix, ind.assignment, salesmen_per_depot);
        best_proxy_seen = std::min(best_proxy_seen, ind.proxy);
        materialize_individual(ind, instance, matrix, salesmen_per_depot, rng);
        if (ind.solution.has_value() && (!best_solution.has_value() || solution_better_than(*ind.solution, *best_solution))) {
            best_solution = *ind.solution;
        }
        population.push_back(std::move(ind));
    }

    trim_population(population, pop_limit);

    if (population.empty()) {
        throw std::logic_error("solve_mdmtsp_hgs_lightweight_v3: empty initial population");
    }

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(hgs_sa_time_budget_ms(instance.customer_count()));
    const std::size_t child_limit = hgs_child_limit(instance.customer_count());

    std::size_t child_count = 0;
    while (child_count < child_limit && std::chrono::steady_clock::now() < deadline) {
        ++child_count;
        const std::size_t ia = tournament_pick(population, arng);
        const std::size_t ib = select_diverse_mate(population, ia, arng);
        const auto* elite_assignment = hgs_best_assignment_ptr(population);

        auto child_assignment = hgs_assignment_crossover(
            instance,
            matrix,
            salesmen_per_depot,
            nearest_two_depots,
            population[ia].assignment,
            population[ib].assignment,
            elite_assignment,
            orders,
            arng
        );
        child_assignment = hgs_mutate_assignment(instance, matrix, child_assignment, orders, arng);
        child_assignment = hgs_targeted_assignment_repair(
            instance,
            matrix,
            salesmen_per_depot,
            nearest_two_depots,
            child_assignment,
            elite_assignment,
            orders,
            arng
        );
        child_assignment = hgs_proxy_refine_assignment(
            instance,
            matrix,
            salesmen_per_depot,
            child_assignment,
            orders,
            arng
        );

        bool duplicate = false;
        for (const auto& ind : population) {
            if (ind.assignment == child_assignment) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }

        HgsIndividual child;
        child.assignment = std::move(child_assignment);
        child.depot_loads = hgs_assignment_depot_loads(instance, child.assignment);
        child.proxy = hgs_assignment_access_proxy(instance, matrix, child.assignment, salesmen_per_depot);
        best_proxy_seen = std::min(best_proxy_seen, child.proxy);

        const cost_t incumbent_value = best_solution.has_value()
            ? best_solution->objective
            : std::numeric_limits<cost_t>::infinity();
        const bool strong_proxy = child.proxy <= best_proxy_seen * static_cast<cost_t>(1.05);
        const bool proxy_close_to_incumbent =
            std::isfinite(incumbent_value) && child.proxy <= incumbent_value * static_cast<cost_t>(1.08);
        const bool should_materialize =
            (child_count % materialize_stride == 0) ||
            (population.size() < pop_limit) ||
            strong_proxy ||
            proxy_close_to_incumbent;

        if (should_materialize) {
            materialize_individual(child, instance, matrix, salesmen_per_depot, rng);
            if (child.solution.has_value() && (!best_solution.has_value() || solution_better_than(*child.solution, *best_solution))) {
                best_solution = *child.solution;
            }
        }

        population.push_back(std::move(child));
        trim_population(population, pop_limit);
    }

    if (!best_solution.has_value()) {
        for (auto& ind : population) {
            materialize_individual(ind, instance, matrix, salesmen_per_depot, rng);
            if (ind.solution.has_value() && (!best_solution.has_value() || solution_better_than(*ind.solution, *best_solution))) {
                best_solution = *ind.solution;
            }
        }
    }

    if (!best_solution.has_value()) {
        throw std::logic_error("solve_mdmtsp_hgs_lightweight_v3: failed to build any candidate");
    }

    return *best_solution;
}


}  // namespace mdmtsp

