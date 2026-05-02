#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "types.hpp"

namespace mdmtsp {

[[nodiscard]] inline bool approx_equal(
    const double a,
    const double b,
    const double abs_eps = 1e-9,
    const double rel_eps = 1e-9
) {
    const auto diff = std::abs(a - b);
    if (diff <= abs_eps) {
        return true;
    }
    return diff <= rel_eps * std::max(std::abs(a), std::abs(b));
}

[[nodiscard]] inline std::string to_lower_ascii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); }
    );
    return value;
}

[[nodiscard]] inline std::string trim(std::string_view text) {
    const auto first = std::find_if_not(
        text.begin(),
        text.end(),
        [](unsigned char ch) { return std::isspace(ch) != 0; }
    );

    const auto last = std::find_if_not(
        text.rbegin(),
        text.rend(),
        [](unsigned char ch) { return std::isspace(ch) != 0; }
    ).base();

    if (first >= last) {
        return {};
    }

    return std::string(first, last);
}

[[nodiscard]] inline std::vector<std::string> split(std::string_view text, const char delimiter) {
    std::vector<std::string> parts;
    std::string current;

    for (const char ch : text) {
        if (ch == delimiter) {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }

    parts.push_back(current);
    return parts;
}

template <class Range>
[[nodiscard]] inline std::string join(const Range& values, std::string_view delimiter) {
    std::ostringstream out;
    bool first = true;

    for (const auto& value : values) {
        if (!first) {
            out << delimiter;
        }
        first = false;
        out << value;
    }

    return out.str();
}

[[nodiscard]] inline std::string format_cost(const cost_t value, const int precision = 6) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

inline void ensure_directory_exists(const std::filesystem::path& path) {
    if (path.empty()) {
        throw std::invalid_argument("ensure_directory_exists: path is empty");
    }
    std::filesystem::create_directories(path);
}

template <class T>
[[nodiscard]] inline bool contains(const std::vector<T>& values, const T& target) {
    return std::find(values.begin(), values.end(), target) != values.end();
}

}  // namespace mdmtsp