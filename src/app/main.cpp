#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "../common/random.hpp"
#include "../common/utils.hpp"
#include "../mdmtsp/mdmtsp_solver.hpp"

namespace fs = std::filesystem;

namespace {

struct AppConfig {
    std::string instance_name = "synthetic_mdmtsp";
    mdmtsp::seed_t seed = 42;
    std::size_t depot_count = 2;
    std::size_t customer_count = 20;
    std::size_t salesman_count = 4;
    double width = 100.0;
    double height = 100.0;
    bool return_to_depot = true;
    std::size_t improve_iterations = 50;
    bool output_json = false;
    std::string output_path;
};

[[nodiscard]] bool starts_with(const std::string_view text, const std::string_view prefix) {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] std::string require_value(
    const int argc,
    const char* const* argv,
    int& index,
    const std::string_view option
) {
    if (index + 1 >= argc) {
        throw std::invalid_argument("missing value for option " + std::string(option));
    }
    ++index;
    return argv[index];
}

template <class UInt>
[[nodiscard]] UInt parse_unsigned(const std::string& value, const std::string_view option) {
    std::size_t pos = 0;
    const auto parsed = std::stoull(value, &pos);
    if (pos != value.size()) {
        throw std::invalid_argument("invalid numeric value for option " + std::string(option));
    }
    return static_cast<UInt>(parsed);
}

[[nodiscard]] double parse_double(const std::string& value, const std::string_view option) {
    std::size_t pos = 0;
    const auto parsed = std::stod(value, &pos);
    if (pos != value.size()) {
        throw std::invalid_argument("invalid floating-point value for option " + std::string(option));
    }
    return parsed;
}

void print_usage(std::ostream& out, const char* program_name) {
    out
        << "Usage: " << program_name << " [options]\n"
        << "Options:\n"
        << "  --instance-name <name>\n"
        << "  --seed <value>\n"
        << "  --depots <count>\n"
        << "  --customers <count>\n"
        << "  --salesmen <count>\n"
        << "  --width <value>\n"
        << "  --height <value>\n"
        << "  --open\n"
        << "  --closed\n"
        << "  --improve-iters <count>\n"
        << "  --json\n"
        << "  --output <path>\n"
        << "  --help\n";
}

[[nodiscard]] AppConfig parse_arguments(const int argc, const char* const* argv) {
    AppConfig config;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "--help") {
            print_usage(std::cout, argv[0]);
            std::exit(EXIT_SUCCESS);
        } else if (arg == "--instance-name") {
            config.instance_name = require_value(argc, argv, i, "--instance-name");
        } else if (arg == "--seed") {
            config.seed = parse_unsigned<mdmtsp::seed_t>(require_value(argc, argv, i, "--seed"), "--seed");
        } else if (arg == "--depots") {
            config.depot_count = parse_unsigned<std::size_t>(require_value(argc, argv, i, "--depots"), "--depots");
        } else if (arg == "--customers") {
            config.customer_count = parse_unsigned<std::size_t>(require_value(argc, argv, i, "--customers"), "--customers");
        } else if (arg == "--salesmen") {
            config.salesman_count = parse_unsigned<std::size_t>(require_value(argc, argv, i, "--salesmen"), "--salesmen");
        } else if (arg == "--width") {
            config.width = parse_double(require_value(argc, argv, i, "--width"), "--width");
        } else if (arg == "--height") {
            config.height = parse_double(require_value(argc, argv, i, "--height"), "--height");
        } else if (arg == "--open") {
            config.return_to_depot = false;
        } else if (arg == "--closed") {
            config.return_to_depot = true;
        } else if (arg == "--improve-iters") {
            config.improve_iterations = parse_unsigned<std::size_t>(
                require_value(argc, argv, i, "--improve-iters"),
                "--improve-iters"
            );
        } else if (arg == "--json") {
            config.output_json = true;
        } else if (arg == "--output") {
            config.output_path = require_value(argc, argv, i, "--output");
        } else {
            throw std::invalid_argument("unknown option: " + arg);
        }
    }

    if (config.depot_count == 0) {
        throw std::invalid_argument("depots must be positive");
    }
    if (config.customer_count == 0) {
        throw std::invalid_argument("customers must be positive");
    }
    if (config.salesman_count == 0) {
        throw std::invalid_argument("salesmen must be positive");
    }
    if (config.width <= 0.0 || config.height <= 0.0) {
        throw std::invalid_argument("width and height must be positive");
    }

    return config;
}

[[nodiscard]] mdmtsp::MDMTSPInstance make_random_instance(
    const AppConfig& config,
    mdmtsp::Random& rng
) {
    mdmtsp::MDMTSPInstance instance;
    instance.name = config.instance_name;
    instance.salesman_count = config.salesman_count;
    instance.return_to_depot = config.return_to_depot;

    instance.depots.reserve(config.depot_count);
    instance.customers.reserve(config.customer_count);

    for (std::size_t i = 0; i < config.depot_count; ++i) {
        instance.depots.push_back({
            rng.uniform_real<double>(0.0, config.width),
            rng.uniform_real<double>(0.0, config.height)
        });
    }

    for (std::size_t i = 0; i < config.customer_count; ++i) {
        instance.customers.push_back({
            rng.uniform_real<double>(0.0, config.width),
            rng.uniform_real<double>(0.0, config.height)
        });
    }

    return instance;
}

[[nodiscard]] std::string escape_json(const std::string& value) {
    std::ostringstream out;

    for (const char ch : value) {
        switch (ch) {
            case '\"':
                out << "\\\"";
                break;
            case '\\':
                out << "\\\\";
                break;
            case '\b':
                out << "\\b";
                break;
            case '\f':
                out << "\\f";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                out << ch;
                break;
        }
    }

    return out.str();
}

[[nodiscard]] std::string route_to_text(const mdmtsp::MDMTSPSalesmanRoute& route) {
    std::ostringstream out;
    out << "salesman=" << route.salesman_id
        << " depot=" << route.depot_id
        << " nodes=[";

    for (std::size_t i = 0; i < route.nodes.size(); ++i) {
        if (i > 0) {
            out << ' ';
        }
        out << route.nodes[i];
    }

    out << ']';
    return out.str();
}

[[nodiscard]] std::string solution_to_text(
    const AppConfig& config,
    const mdmtsp::MDMTSPInstance& instance,
    const mdmtsp::MDMTSPSolution& solution
) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(6);
    out << "instance: " << instance.name << '\n';
    out << "seed: " << config.seed << '\n';
    out << "depots: " << instance.depot_count() << '\n';
    out << "customers: " << instance.customer_count() << '\n';
    out << "salesmen: " << instance.salesman_count << '\n';
    out << "closed_routes: " << (instance.return_to_depot ? "true" : "false") << '\n';
    out << "objective: " << solution.objective << '\n';
    out << "feasible: " << (solution.feasible ? "true" : "false") << '\n';
    out << "status: " << solution.status << '\n';
    out << "routes:\n";

    for (const auto& route : solution.routes) {
        out << "  - " << route_to_text(route) << '\n';
    }

    return out.str();
}

[[nodiscard]] std::string solution_to_json(
    const AppConfig& config,
    const mdmtsp::MDMTSPInstance& instance,
    const mdmtsp::MDMTSPSolution& solution
) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(6);
    out << "{\n";
    out << "  \"instance_name\": \"" << escape_json(instance.name) << "\",\n";
    out << "  \"seed\": " << config.seed << ",\n";
    out << "  \"depot_count\": " << instance.depot_count() << ",\n";
    out << "  \"customer_count\": " << instance.customer_count() << ",\n";
    out << "  \"salesman_count\": " << instance.salesman_count << ",\n";
    out << "  \"return_to_depot\": " << (instance.return_to_depot ? "true" : "false") << ",\n";
    out << "  \"objective\": " << solution.objective << ",\n";
    out << "  \"feasible\": " << (solution.feasible ? "true" : "false") << ",\n";
    out << "  \"status\": \"" << escape_json(solution.status) << "\",\n";
    out << "  \"routes\": [\n";

    for (std::size_t i = 0; i < solution.routes.size(); ++i) {
        const auto& route = solution.routes[i];
        out << "    {\n";
        out << "      \"salesman_id\": " << route.salesman_id << ",\n";
        out << "      \"depot_id\": " << route.depot_id << ",\n";
        out << "      \"nodes\": [";

        for (std::size_t j = 0; j < route.nodes.size(); ++j) {
            if (j > 0) {
                out << ", ";
            }
            out << route.nodes[j];
        }

        out << "]\n";
        out << "    }";
        if (i + 1 != solution.routes.size()) {
            out << ",";
        }
        out << '\n';
    }

    out << "  ]\n";
    out << "}\n";

    return out.str();
}

void write_output_if_requested(const std::string& content, const std::string& output_path) {
    if (output_path.empty()) {
        return;
    }

    const fs::path path(output_path);
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path());
    }

    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("failed to open output file: " + output_path);
    }

    out << content;
    if (!out) {
        throw std::runtime_error("failed to write output file: " + output_path);
    }
}

}  // namespace

int main(int argc, const char* argv[]) {
    try {
        const auto config = parse_arguments(argc, argv);

        mdmtsp::Random rng(config.seed);
        auto instance = make_random_instance(config, rng);

        auto solution = mdmtsp::solve_mdmtsp_nearest_neighbour(instance, rng);

        if (config.improve_iterations > 0) {
            mdmtsp::improve_interroute_by_relocation(
                solution,
                instance,
                config.improve_iterations
            );
        }

        solution.objective = mdmtsp::compute_objective(solution, instance);
        solution.feasible = mdmtsp::is_solution_feasible(instance, solution);
        solution.status = solution.feasible
            ? "ok"
            : mdmtsp::validation_report(instance, solution);

        const auto content = config.output_json
            ? solution_to_json(config, instance, solution)
            : solution_to_text(config, instance, solution);

        std::cout << content;
        write_output_if_requested(content, config.output_path);

        return solution.feasible ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }
}