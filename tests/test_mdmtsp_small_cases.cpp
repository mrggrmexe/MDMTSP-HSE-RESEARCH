#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/mdmtsp/mdmtsp_solver.hpp"

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

mdmtsp::MDMTSPInstance make_single_depot_instance() {
    mdmtsp::MDMTSPInstance instance;
    instance.name = "single_depot";
    instance.depots = {{0.0, 0.0}};
    instance.customers = {{1.0, 0.0}, {2.0, 0.0}, {3.0, 0.0}};
    instance.salesman_count = 1;
    instance.return_to_depot = true;
    return instance;
}

mdmtsp::MDMTSPInstance make_multi_depot_instance() {
    mdmtsp::MDMTSPInstance instance;
    instance.name = "multi_depot";
    instance.depots = {{0.0, 0.0}, {10.0, 0.0}};
    instance.customers = {{1.0, 0.0}, {2.0, 0.0}, {11.0, 0.0}, {12.0, 0.0}};
    instance.salesman_count = 2;
    instance.return_to_depot = true;
    return instance;
}

void test_nearest_depot_assignment() {
    const auto instance = make_multi_depot_instance();
    const auto assignment = mdmtsp::assign_customers_to_nearest_depots(instance);

    require(assignment.size() == instance.customer_count(), "assignment size mismatch");
    require(assignment[0] == 0, "customer 0 depot mismatch");
    require(assignment[1] == 0, "customer 1 depot mismatch");
    require(assignment[2] == 1, "customer 2 depot mismatch");
    require(assignment[3] == 1, "customer 3 depot mismatch");
}

void test_round_robin_salesmen_assignment() {
    mdmtsp::MDMTSPInstance instance;
    instance.depots = {{0.0, 0.0}, {10.0, 0.0}, {20.0, 0.0}};
    instance.customers = {{1.0, 0.0}};
    instance.salesman_count = 5;

    const auto assignment = mdmtsp::assign_salesmen_to_depots_round_robin(instance);

    require(assignment.size() == 5, "salesman assignment size mismatch");
    require(assignment[0] == 0, "salesman 0 mismatch");
    require(assignment[1] == 1, "salesman 1 mismatch");
    require(assignment[2] == 2, "salesman 2 mismatch");
    require(assignment[3] == 0, "salesman 3 mismatch");
    require(assignment[4] == 1, "salesman 4 mismatch");
}

void test_nearest_neighbour_solver_single_depot() {
    const auto instance = make_single_depot_instance();
    mdmtsp::Random rng(123);

    auto solution = mdmtsp::solve_mdmtsp_nearest_neighbour(instance, rng);

    require(solution.routes.size() == 1, "single-depot solver route count mismatch");
    require(mdmtsp::is_solution_feasible(instance, solution), "single-depot solution must be feasible");
    require(solution.routes[0].nodes.front() == 0, "single-depot route must start at depot");
    require(solution.routes[0].nodes.back() == 0, "single-depot route must end at depot");
    require(std::abs(solution.objective - 6.0) < 1e-9, "single-depot objective mismatch");
}

void test_nearest_neighbour_solver_multi_depot() {
    const auto instance = make_multi_depot_instance();
    mdmtsp::Random rng(777);

    auto solution = mdmtsp::solve_mdmtsp_nearest_neighbour(instance, rng);

    require(solution.routes.size() == instance.salesman_count, "multi-depot solver route count mismatch");
    require(mdmtsp::is_solution_feasible(instance, solution), "multi-depot solution must be feasible");
    require(solution.objective >= 0.0, "objective must be non-negative");

    for (const auto& route : solution.routes) {
        require(route.depot_id < instance.depot_count(), "route depot_id out of range");
        require(!route.nodes.empty(), "route must not be empty");
        require(route.nodes.front() == static_cast<mdmtsp::node_id_t>(route.depot_id), "route must start at assigned depot");
        require(route.nodes.back() == static_cast<mdmtsp::node_id_t>(route.depot_id), "route must end at assigned depot");
    }
}

void test_interroute_improvement_preserves_feasibility() {
    const auto instance = make_multi_depot_instance();
    mdmtsp::Random rng(5);

    auto solution = mdmtsp::solve_mdmtsp_nearest_neighbour(instance, rng);
    const auto before = solution.objective;

    mdmtsp::improve_interroute_by_relocation(solution, instance, 20);

    require(mdmtsp::is_solution_feasible(instance, solution), "improved solution must remain feasible");
    require(solution.objective <= before + 1e-9, "interroute improvement must not worsen objective");
}

}  // namespace

int main() {
    try {
        test_nearest_depot_assignment();
        test_round_robin_salesmen_assignment();
        test_nearest_neighbour_solver_single_depot();
        test_nearest_neighbour_solver_multi_depot();
        test_interroute_improvement_preserves_feasibility();
    } catch (const std::exception& ex) {
        std::cerr << "test_mdmtsp_small_cases failed: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}