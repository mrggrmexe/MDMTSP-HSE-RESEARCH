#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#include "../src/mdmtsp/instance.hpp"

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_counts_and_node_mapping() {
    mdmtsp::MDMTSPInstance instance;
    instance.name = "toy";
    instance.depots = {{0.0, 0.0}, {10.0, 0.0}};
    instance.customers = {{1.0, 1.0}, {2.0, 2.0}, {11.0, 1.0}};
    instance.salesman_count = 3;
    instance.return_to_depot = true;

    require(instance.depot_count() == 2, "depot_count mismatch");
    require(instance.customer_count() == 3, "customer_count mismatch");
    require(instance.node_count() == 5, "node_count mismatch");
    require(instance.first_customer_node() == 2, "first_customer_node mismatch");

    require(instance.is_depot_node(0), "node 0 must be depot");
    require(instance.is_depot_node(1), "node 1 must be depot");
    require(instance.is_customer_node(2), "node 2 must be customer");
    require(instance.is_customer_node(4), "node 4 must be customer");
    require(!instance.is_customer_node(1), "node 1 must not be customer");

    require(instance.depot_id_from_node(1) == 1, "depot_id_from_node mismatch");
    require(instance.customer_index_from_node(2) == 0, "customer_index_from_node mismatch");
    require(instance.customer_index_from_node(4) == 2, "customer_index_from_node mismatch");
    require(instance.customer_node_from_index(1) == 3, "customer_node_from_index mismatch");
}

void test_point_access_and_matrix() {
    mdmtsp::MDMTSPInstance instance;
    instance.depots = {{0.0, 0.0}};
    instance.customers = {{3.0, 4.0}};
    instance.salesman_count = 1;

    const auto depot = instance.point_of(0);
    const auto customer = instance.point_of(1);

    require(std::abs(depot.x - 0.0) < 1e-12, "depot x mismatch");
    require(std::abs(depot.y - 0.0) < 1e-12, "depot y mismatch");
    require(std::abs(customer.x - 3.0) < 1e-12, "customer x mismatch");
    require(std::abs(customer.y - 4.0) < 1e-12, "customer y mismatch");

    const auto matrix = instance.build_distance_matrix();
    require(matrix.size() == 2, "matrix row count mismatch");
    require(matrix[0].size() == 2, "matrix col count mismatch");
    require(std::abs(matrix[0][1] - 5.0) < 1e-12, "distance mismatch");
    require(std::abs(matrix[1][0] - 5.0) < 1e-12, "distance symmetry mismatch");
    require(std::abs(matrix[0][0]) < 1e-12, "distance diagonal mismatch");
}

void test_basic_validation() {
    {
        mdmtsp::MDMTSPInstance instance;
        instance.customers = {{1.0, 1.0}};
        instance.salesman_count = 1;

        bool thrown = false;
        try {
            instance.validate_basic();
        } catch (const std::invalid_argument&) {
            thrown = true;
        }
        require(thrown, "validate_basic must fail without depots");
    }

    {
        mdmtsp::MDMTSPInstance instance;
        instance.depots = {{0.0, 0.0}};
        instance.customers = {{1.0, 1.0}};
        instance.salesman_count = 0;

        bool thrown = false;
        try {
            instance.validate_basic();
        } catch (const std::invalid_argument&) {
            thrown = true;
        }
        require(thrown, "validate_basic must fail with salesman_count == 0");
    }

    {
        mdmtsp::MDMTSPInstance instance;
        instance.depots = {{0.0, 0.0}};
        instance.customers = {{1.0, 1.0}};
        instance.salesman_count = 1;

        instance.validate_basic();
    }
}

}  // namespace

int main() {
    try {
        test_counts_and_node_mapping();
        test_point_access_and_matrix();
        test_basic_validation();
    } catch (const std::exception& ex) {
        std::cerr << "test_instance failed: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}