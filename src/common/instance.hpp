#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mdmtsp {

struct Point {
    double x{};
    double y{};
};

struct Depot {
    std::size_t id{};
    Point point{};
    std::size_t salesmen{1};
};

struct Customer {
    std::size_t id{};
    Point point{};
};

enum class DistanceType {
    Euclidean2D
};

struct Instance {
    std::string name;
    std::uint64_t seed{};
    bool return_to_depot{true};
    DistanceType distance_type{DistanceType::Euclidean2D};
    std::vector<Depot> depots;
    std::vector<Customer> customers;

    [[nodiscard]] std::size_t salesmen_count() const noexcept {
        std::size_t total = 0;
        for (const Depot& depot : depots) {
            total += depot.salesmen;
        }
        return total;
    }

    [[nodiscard]] std::size_t node_count() const noexcept {
        return depots.size() + customers.size();
    }
};

[[nodiscard]] inline std::string to_string(DistanceType type) {
    switch (type) {
        case DistanceType::Euclidean2D:
            return "euclidean";
    }
    return "euclidean";
}

[[nodiscard]] inline DistanceType distance_type_from_string(std::string_view value) {
    if (value == "euclidean" || value == "euclidean2d" || value == "euc_2d") {
        return DistanceType::Euclidean2D;
    }
    throw std::invalid_argument("unsupported distance type: " + std::string(value));
}

}  // namespace mdmtsp