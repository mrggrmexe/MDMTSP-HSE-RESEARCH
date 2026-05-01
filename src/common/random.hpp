#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "types.hpp"

namespace mdmtsp {

class Random {
public:
    using engine_type = std::mt19937_64;
    using result_type = engine_type::result_type;

    Random()
        : engine_(std::random_device{}()) {}

    explicit Random(seed_t seed)
        : engine_(seed) {}

    void seed(seed_t value) {
        engine_.seed(value);
    }

    [[nodiscard]] seed_t next_seed() {
        return engine_();
    }

    [[nodiscard]] engine_type& engine() noexcept {
        return engine_;
    }

    [[nodiscard]] const engine_type& engine() const noexcept {
        return engine_;
    }

    template <class Int>
    [[nodiscard]] Int uniform_int(Int left, Int right) {
        static_assert(std::is_integral_v<Int>, "uniform_int requires an integral type");
        if (left > right) {
            throw std::invalid_argument("uniform_int: left bound is greater than right bound");
        }
        std::uniform_int_distribution<Int> dist(left, right);
        return dist(engine_);
    }

    template <class Real>
    [[nodiscard]] Real uniform_real(Real left, Real right) {
        static_assert(std::is_floating_point_v<Real>, "uniform_real requires a floating-point type");
        if (!(left <= right)) {
            throw std::invalid_argument("uniform_real: left bound is greater than right bound");
        }
        std::uniform_real_distribution<Real> dist(left, right);
        return dist(engine_);
    }

    [[nodiscard]] bool bernoulli(double probability = 0.5) {
        if (probability < 0.0 || probability > 1.0) {
            throw std::invalid_argument("bernoulli: probability must be in [0, 1]");
        }
        std::bernoulli_distribution dist(probability);
        return dist(engine_);
    }

    template <class Container>
    void shuffle(Container& container) {
        std::shuffle(container.begin(), container.end(), engine_);
    }

    template <class T>
    [[nodiscard]] const T& choice(const std::vector<T>& values) {
        if (values.empty()) {
            throw std::invalid_argument("choice: container is empty");
        }
        const auto index = uniform_int<std::size_t>(0, values.size() - 1);
        return values[index];
    }

    template <class Weight>
    [[nodiscard]] std::size_t weighted_index(const std::vector<Weight>& weights) {
        if (weights.empty()) {
            throw std::invalid_argument("weighted_index: weights are empty");
        }

        std::vector<double> normalized;
        normalized.reserve(weights.size());

        for (const auto& weight : weights) {
            const auto value = static_cast<double>(weight);
            if (value < 0.0) {
                throw std::invalid_argument("weighted_index: negative weight");
            }
            normalized.push_back(value);
        }

        std::discrete_distribution<std::size_t> dist(normalized.begin(), normalized.end());
        return dist(engine_);
    }

private:
    engine_type engine_;
};

}  // namespace mdmtsp