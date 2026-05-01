#pragma once

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

#include "types.hpp"

namespace mdmtsp {

[[nodiscard]] inline cost_t squared_euclidean_distance(const Point2D& a, const Point2D& b) {
    const auto dx = a.x - b.x;
    const auto dy = a.y - b.y;
    return dx * dx + dy * dy;
}

[[nodiscard]] inline cost_t euclidean_distance(const Point2D& a, const Point2D& b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

[[nodiscard]] inline DistanceMatrix build_distance_matrix(const std::vector<Point2D>& points) {
    const auto n = points.size();
    DistanceMatrix matrix(n, std::vector<cost_t>(n, 0.0));

    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            const auto d = euclidean_distance(points[i], points[j]);
            matrix[i][j] = d;
            matrix[j][i] = d;
        }
    }

    return matrix;
}

[[nodiscard]] inline cost_t route_cost(const Route& route, const DistanceMatrix& matrix) {
    if (route.size() < 2) {
        return 0.0;
    }

    cost_t total = 0.0;
    for (std::size_t i = 1; i < route.size(); ++i) {
        const auto from = route[i - 1];
        const auto to = route[i];

        if (from >= matrix.size() || to >= matrix.size()) {
            throw std::out_of_range("route_cost: node index is out of range");
        }

        total += matrix[from][to];
    }

    return total;
}

[[nodiscard]] inline cost_t closed_route_cost(const Route& route, const DistanceMatrix& matrix) {
    if (route.empty()) {
        return 0.0;
    }

    if (route.size() == 1) {
        const auto v = route.front();
        if (v >= matrix.size()) {
            throw std::out_of_range("closed_route_cost: node index is out of range");
        }
        return 0.0;
    }

    return route_cost(route, matrix) + matrix[route.back()][route.front()];
}

template <class RouteContainer>
[[nodiscard]] inline cost_t total_cost(const RouteContainer& routes, const DistanceMatrix& matrix) {
    cost_t total = 0.0;
    for (const auto& route : routes) {
        total += route_cost(route, matrix);
    }
    return total;
}

template <class RouteContainer>
[[nodiscard]] inline cost_t total_closed_cost(const RouteContainer& routes, const DistanceMatrix& matrix) {
    cost_t total = 0.0;
    for (const auto& route : routes) {
        total += closed_route_cost(route, matrix);
    }
    return total;
}

}  // namespace mdmtsp