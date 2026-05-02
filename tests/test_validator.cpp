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

mdmtsp::MDMTSPInstance make_instance() {
    mdmtsp::MDMTSPInstance instance;
    instance.name = "validator_case";
    instance.depots = {{0.0, 0.0}, {10.0, 0.0}};
    instance.customers = {{1.0, 0.0}, {2.0, 0.0}, {11.0, 0.0}};
    instance.salesman_count = 2;
    instance.return_to_depot = true;
    return instance;
}

void test_feasible_solution() {
    const auto instance = make_instance();

    mdmtsp::MDMTSPSolution solution;
    solution.routes = {
        mdmtsp::MDMTSPSalesmanRoute{0, 0, {0, 2, 3, 0}},
        mdmtsp::MDMTSPSalesmanRoute{1, 1, {1, 4, 1}}
    };

    require(mdmtsp::is_solution_feasible(instance, solution), "solution must be feasible");
    require(mdmtsp::validation_report(instance, solution) == "ok", "report must be ok");
}

void test_duplicate_customer_detection() {
    const auto instance = make_instance();

    mdmtsp::MDMTSPSolution solution;
    solution.routes = {
        mdmtsp::MDMTSPSalesmanRoute{0, 0, {0, 2, 3, 0}},
        mdmtsp::MDMTSPSalesmanRoute{1, 1, {1, 3, 4, 1}}
    };

    require(!mdmtsp::is_solution_feasible(instance, solution), "duplicate customer must be infeasible");

    const auto report = mdmtsp::validation_report(instance, solution);
    require(report.find("customer visited more than once") != std::string::npos, "duplicate message missing");
}

void test_missing_customer_detection() {
    const auto instance = make_instance();

    mdmtsp::MDMTSPSolution solution;
    solution.routes = {
        mdmtsp::MDMTSPSalesmanRoute{0, 0, {0, 2, 0}},
        mdmtsp::MDMTSPSalesmanRoute{1, 1, {1, 1}}
    };

    require(!mdmtsp::is_solution_feasible(instance, solution), "missing customer must be infeasible");

    const auto report = mdmtsp::validation_report(instance, solution);
    require(report.find("not all customers are visited exactly once") != std::string::npos, "missing-customer message missing");
}

void test_invalid_depot_boundary() {
    const auto instance = make_instance();

    mdmtsp::MDMTSPSolution solution;
    solution.routes = {
        mdmtsp::MDMTSPSalesmanRoute{0, 0, {1, 2, 3, 0}},
        mdmtsp::MDMTSPSalesmanRoute{1, 1, {1, 4, 1}}
    };

    require(!mdmtsp::is_solution_feasible(instance, solution), "route must start at its depot");

    const auto report = mdmtsp::validation_report(instance, solution);
    require(report.find("does not start at assigned depot") != std::string::npos, "start-depot message missing");
}

void test_route_count_mismatch() {
    const auto instance = make_instance();

    mdmtsp::MDMTSPSolution solution;
    solution.routes = {
        mdmtsp::MDMTSPSalesmanRoute{0, 0, {0, 2, 3, 4, 0}}
    };

    require(!mdmtsp::is_solution_feasible(instance, solution), "route count mismatch must be infeasible");

    const auto report = mdmtsp::validation_report(instance, solution);
    require(report.find("route count must equal salesman_count") != std::string::npos, "route-count message missing");
}

}  // namespace

int main() {
    try {
        test_feasible_solution();
        test_duplicate_customer_detection();
        test_missing_customer_detection();
        test_invalid_depot_boundary();
        test_route_count_mismatch();
    } catch (const std::exception& ex) {
        std::cerr << "test_validator failed: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}