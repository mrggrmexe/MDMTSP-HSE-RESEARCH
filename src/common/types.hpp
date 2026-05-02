#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace mdmtsp {

using node_id_t = std::size_t;
using depot_id_t = std::size_t;
using salesman_id_t = std::size_t;
using route_id_t = std::size_t;

using coord_t = double;
using cost_t = double;
using seed_t = std::uint64_t;

inline constexpr node_id_t invalid_node_id = std::numeric_limits<node_id_t>::max();
inline constexpr depot_id_t invalid_depot_id = std::numeric_limits<depot_id_t>::max();
inline constexpr salesman_id_t invalid_salesman_id = std::numeric_limits<salesman_id_t>::max();
inline constexpr route_id_t invalid_route_id = std::numeric_limits<route_id_t>::max();

struct Point2D {
    coord_t x = 0.0;
    coord_t y = 0.0;

    [[nodiscard]] friend bool operator==(const Point2D&, const Point2D&) = default;
};

struct Node {
    node_id_t id = invalid_node_id;
    Point2D point{};

    [[nodiscard]] friend bool operator==(const Node&, const Node&) = default;
};

using Route = std::vector<node_id_t>;
using DistanceMatrix = std::vector<std::vector<cost_t>>;

enum class NodeKind : std::uint8_t {
    Depot,
    Customer
};

enum class ObjectiveSense : std::uint8_t {
    Minimize,
    Maximize
};

struct RunMetadata {
    seed_t seed = 0;
    std::string instance_name;
    std::string algorithm_name;
};

}  // namespace mdmtsp