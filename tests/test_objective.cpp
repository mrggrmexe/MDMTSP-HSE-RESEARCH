#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#include "../src/mdmtsp/mdmtsp_solver.hpp"

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

mdmtsp::MDMTSPInstance make_instance(const bool return_to_depot = true) {
    mdmtsp::MDMTSPInstance instance;
    instance.name = "objective_case";
    instance.depots = {{0.0, 0.0}, {10.0, 0.0}};
    instance.customers = {{3.0, 4.0}, {6.0, 8.0}, {13.0, 4.0}};
    instance.salesman_count = 2;
    instance.return_to_depot = return_to_depot;
    return instance;
}

void test_single_route_objective() {
    const auto instance = make_instance(true);

    mdmtsp::MDMTSPSolution solution;
    solution.routes = {
        mdmtsp::MDMTSPSalesmanRoute{0, 0, {0, 2, 3, 0}},
        mdmtsp::MDMTSPSalesmanRoute{1, 1, {1, 4, 1}}
    };

    const auto value = mdmtsp::compute_objective(solution, instance);
    require(std::abs(value - 30.0) < 1e-9, "objective mismatch for closed routes");
}

void test_open_route_objective() {
    const auto instance = make_instance(false);

    mdmtsp::MDMTSPSolution solution;
    solution.routes = {
        mdmtsp::MDMTSPSalesmanRoute{0, 0, {0, 2, 3}},
        mdmtsp::MDMTSPSalesmanRoute{1, 1, {1, 4}}
    };

    const auto value = mdmtsp::compute_objective(solution, instance);
    require(std::abs(value - 20.0) < 1e-9, "objective mismatch for open routes");
}

void test_empty_route_objective() {
    const auto instance = make_instance(true);

    mdmtsp::MDMTSPSolution solution;
    solution.routes = {
        mdmtsp::MDMTSPSalesmanRoute{0, 0, {}},
        mdmtsp::MDMTSPSalesmanRoute{1, 1, {1, 4, 1}}
    };

    const auto value = mdmtsp::compute_objective(solution, instance);
    require(std::abs(value - 10.0) < 1e-9, "objective mismatch for empty route case");
}

void test_invalid_route_start_throws() {
    const auto instance = make_instance(true);

    mdmtsp::MDMTSPSolution solution;
    solution.routes = {
        mdmtsp::MDMTSPSalesmanRoute{0, 0, {2, 3, 0}},
        mdmtsp::MDMTSPSalesmanRoute{1, 1, {1, 4, 1}}
    };

    bool thrown = false;
    try {
        (void)mdmtsp::compute_objective(solution, instance);
    } catch (const std::invalid_argument&) {
        thrown = true;
    }

    require(thrown, "compute_objective must throw on invalid route start");
}

void test_invalid_closed_route_end_throws() {
    const auto instance = make_instance(true);

    mdmtsp::MDMTSPSolution solution;
    solution.routes = {
        mdmtsp::MDMTSPSalesmanRoute{0, 0, {0, 2, 3}},
        mdmtsp::MDMTSPSalesmanRoute{1, 1, {1, 4, 1}}
    };

    bool thrown = false;
    try {
        (void)mdmtsp::compute_objective(solution, instance);
    } catch (const std::invalid_argument&) {
        thrown = true;
    }

    require(thrown, "compute_objective must throw on invalid closed route end");
}

}  // namespace

int main() {
    try {
        test_single_route_objective();
        test_open_route_objective();
        test_empty_route_objective();
        test_invalid_route_start_throws();
        test_invalid_closed_route_end_throws();
    } catch (const std::exception& ex) {
        std::cerr << "test_objective failed: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}