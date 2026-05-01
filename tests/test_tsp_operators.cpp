#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/common/distance.hpp"
#include "../src/common/random.hpp"
#include "../src/common/utils.hpp"

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_distance_functions() {
    const mdmtsp::Point2D a{0.0, 0.0};
    const mdmtsp::Point2D b{3.0, 4.0};

    require(std::abs(mdmtsp::squared_euclidean_distance(a, b) - 25.0) < 1e-12, "squared distance mismatch");
    require(std::abs(mdmtsp::euclidean_distance(a, b) - 5.0) < 1e-12, "euclidean distance mismatch");
}

void test_route_cost_functions() {
    const std::vector<mdmtsp::Point2D> points = {
        {0.0, 0.0},
        {3.0, 4.0},
        {6.0, 8.0}
    };

    const auto matrix = mdmtsp::build_distance_matrix(points);
    const mdmtsp::Route route = {0, 1, 2};

    require(std::abs(mdmtsp::route_cost(route, matrix) - 10.0) < 1e-12, "route_cost mismatch");
    require(std::abs(mdmtsp::closed_route_cost(route, matrix) - 20.0) < 1e-12, "closed_route_cost mismatch");
}

void test_random_reproducibility() {
    mdmtsp::Random rng1(42);
    mdmtsp::Random rng2(42);

    for (int i = 0; i < 20; ++i) {
        const auto a = rng1.uniform_int<int>(-100, 100);
        const auto b = rng2.uniform_int<int>(-100, 100);
        require(a == b, "uniform_int reproducibility mismatch");
    }

    for (int i = 0; i < 20; ++i) {
        const auto a = rng1.bernoulli(0.25);
        const auto b = rng2.bernoulli(0.25);
        require(a == b, "bernoulli reproducibility mismatch");
    }
}

void test_shuffle_and_choice() {
    mdmtsp::Random rng(7);
    std::vector<int> values = {1, 2, 3, 4, 5, 6};

    rng.shuffle(values);

    std::vector<int> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    require(sorted == std::vector<int>({1, 2, 3, 4, 5, 6}), "shuffle must preserve elements");

    const std::vector<int> options = {10, 20, 30};
    const auto chosen = rng.choice(options);
    require(chosen == 10 || chosen == 20 || chosen == 30, "choice must return existing element");
}

void test_utils() {
    require(mdmtsp::approx_equal(1.0, 1.0 + 1e-12), "approx_equal mismatch");
    require(mdmtsp::to_lower_ascii("HeLLo") == "hello", "to_lower_ascii mismatch");
    require(mdmtsp::trim("  abc  ") == "abc", "trim mismatch");

    const auto parts = mdmtsp::split("aa,bb,cc", ',');
    require(parts.size() == 3, "split size mismatch");
    require(parts[0] == "aa" && parts[1] == "bb" && parts[2] == "cc", "split content mismatch");

    require(mdmtsp::join(parts, "-") == "aa-bb-cc", "join mismatch");
    require(mdmtsp::contains(std::vector<int>{1, 2, 3}, 2), "contains mismatch");
}

}  // namespace

int main() {
    try {
        test_distance_functions();
        test_route_cost_functions();
        test_random_reproducibility();
        test_shuffle_and_choice();
        test_utils();
    } catch (const std::exception& ex) {
        std::cerr << "test_tsp_operators failed: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}