#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include "../common/distance.hpp"
#include "../common/types.hpp"

namespace mdmtsp {

struct MDMTSPInstance {
    std::string name;
    std::vector<Point2D> depots;
    std::vector<Point2D> customers;
    std::size_t salesman_count = 0;
    bool return_to_depot = true;

    [[nodiscard]] std::size_t depot_count() const noexcept {
        return depots.size();
    }

    [[nodiscard]] std::size_t customer_count() const noexcept {
        return customers.size();
    }

    [[nodiscard]] std::size_t node_count() const noexcept {
        return depots.size() + customers.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return depots.empty() && customers.empty();
    }

    [[nodiscard]] node_id_t first_customer_node() const noexcept {
        return depots.size();
    }

    [[nodiscard]] bool is_depot_node(const node_id_t node) const noexcept {
        return node < depots.size();
    }

    [[nodiscard]] bool is_customer_node(const node_id_t node) const noexcept {
        return node >= first_customer_node() && node < node_count();
    }

    [[nodiscard]] depot_id_t depot_id_from_node(const node_id_t node) const {
        if (!is_depot_node(node)) {
            throw std::out_of_range("depot_id_from_node: node is not a depot");
        }
        return static_cast<depot_id_t>(node);
    }

    [[nodiscard]] std::size_t customer_index_from_node(const node_id_t node) const {
        if (!is_customer_node(node)) {
            throw std::out_of_range("customer_index_from_node: node is not a customer");
        }
        return node - first_customer_node();
    }

    [[nodiscard]] node_id_t customer_node_from_index(const std::size_t index) const {
        if (index >= customers.size()) {
            throw std::out_of_range("customer_node_from_index: customer index out of range");
        }
        return first_customer_node() + index;
    }

    [[nodiscard]] Point2D point_of(const node_id_t node) const {
        if (is_depot_node(node)) {
            return depots[node];
        }
        if (is_customer_node(node)) {
            return customers[customer_index_from_node(node)];
        }
        throw std::out_of_range("point_of: node index out of range");
    }

    [[nodiscard]] std::vector<Point2D> all_points() const {
        std::vector<Point2D> points;
        points.reserve(node_count());
        points.insert(points.end(), depots.begin(), depots.end());
        points.insert(points.end(), customers.begin(), customers.end());
        return points;
    }

    [[nodiscard]] DistanceMatrix build_distance_matrix() const {
        return mdmtsp::build_distance_matrix(all_points());
    }

    void validate_basic() const {
        if (depots.empty()) {
            throw std::invalid_argument("MDMTSPInstance: at least one depot is required");
        }
        if (salesman_count == 0) {
            throw std::invalid_argument("MDMTSPInstance: salesman_count must be positive");
        }
    }
};

}  // namespace mdmtsp