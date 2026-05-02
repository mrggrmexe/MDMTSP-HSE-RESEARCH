#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "common/instance.hpp"
#include "mdmtsp/instance_io.hpp"
#include "mdmtsp/mdmtsp_solver.hpp"

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

struct GenerateSpec {
    bool requested{false};

    std::string instance_name;
    std::size_t depots{0};
    std::size_t customers{0};
    std::size_t salesmen{0};
    double width{0.0};
    double height{0.0};

    bool has_instance_name{false};
    bool has_depots{false};
    bool has_customers{false};
    bool has_salesmen{false};
    bool has_width{false};
    bool has_height{false};
};

struct CliOptions {
    fs::path instance_path;
    bool has_instance_path{false};

    fs::path normalized_output_path;
    bool write_normalized{false};

    bool json_output{false};
    fs::path output_path;

    std::optional<std::uint64_t> seed;
    std::optional<bool> return_to_depot_override;
    std::size_t improve_iterations{0};

    GenerateSpec generate;
};

[[noreturn]] void print_usage_and_exit(const char* argv0, int exit_code) {
    std::ostream& out = exit_code == 0 ? std::cout : std::cerr;
    out
        << "Usage:\n"
        << "  " << argv0 << " --instance <path> [--seed <n>] [--improve-iters <n>]\n"
        << "           [--json] [--output <path>] [--write-normalized <path>] [--open|--closed]\n"
        << "  " << argv0 << " --instance-name <name> --seed <n> --depots <n> --customers <n>\n"
        << "           --salesmen <n> --width <x> --height <y> [--open|--closed]\n"
        << "           [--improve-iters <n>] [--json] [--output <path>]\n";
    std::exit(exit_code);
}

template <typename T>
T parse_number(const char* value, const std::string& flag) {
    std::istringstream in(value);
    T parsed{};
    in >> parsed;

    if (!in || !in.eof()) {
        throw std::invalid_argument("invalid value for " + flag + ": " + value);
    }

    return parsed;
}

CliOptions parse_args(int argc, char** argv) {
    CliOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};

        if (arg == "--help" || arg == "-h") {
            print_usage_and_exit(argv[0], 0);
        }

        if (arg == "--instance") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("missing value after --instance");
            }
            options.instance_path = argv[++i];
            options.has_instance_path = true;
            continue;
        }

        if (arg == "--write-normalized") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("missing value after --write-normalized");
            }
            options.normalized_output_path = argv[++i];
            options.write_normalized = true;
            continue;
        }

        if (arg == "--json") {
            options.json_output = true;
            continue;
        }

        if (arg == "--output") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("missing value after --output");
            }
            options.output_path = argv[++i];
            continue;
        }

        if (arg == "--seed") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("missing value after --seed");
            }
            options.seed = parse_number<std::uint64_t>(argv[++i], "--seed");
            continue;
        }

        if (arg == "--improve-iters") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("missing value after --improve-iters");
            }
            options.improve_iterations = parse_number<std::size_t>(argv[++i], "--improve-iters");
            continue;
        }

        if (arg == "--open") {
            options.return_to_depot_override = false;
            continue;
        }

        if (arg == "--closed") {
            options.return_to_depot_override = true;
            continue;
        }

        if (arg == "--instance-name") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("missing value after --instance-name");
            }
            options.generate.requested = true;
            options.generate.instance_name = argv[++i];
            options.generate.has_instance_name = true;
            continue;
        }

        if (arg == "--depots") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("missing value after --depots");
            }
            options.generate.requested = true;
            options.generate.depots = parse_number<std::size_t>(argv[++i], "--depots");
            options.generate.has_depots = true;
            continue;
        }

        if (arg == "--customers") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("missing value after --customers");
            }
            options.generate.requested = true;
            options.generate.customers = parse_number<std::size_t>(argv[++i], "--customers");
            options.generate.has_customers = true;
            continue;
        }

        if (arg == "--salesmen") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("missing value after --salesmen");
            }
            options.generate.requested = true;
            options.generate.salesmen = parse_number<std::size_t>(argv[++i], "--salesmen");
            options.generate.has_salesmen = true;
            continue;
        }

        if (arg == "--width") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("missing value after --width");
            }
            options.generate.requested = true;
            options.generate.width = parse_number<double>(argv[++i], "--width");
            options.generate.has_width = true;
            continue;
        }

        if (arg == "--height") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("missing value after --height");
            }
            options.generate.requested = true;
            options.generate.height = parse_number<double>(argv[++i], "--height");
            options.generate.has_height = true;
            continue;
        }

        throw std::invalid_argument("unknown argument: " + std::string(arg));
    }

    if (options.has_instance_path && options.generate.requested) {
        throw std::invalid_argument("use either --instance or generation flags, not both");
    }

    if (!options.has_instance_path && !options.generate.requested) {
        throw std::invalid_argument("either --instance or generation flags are required");
    }

    if (options.write_normalized && !options.has_instance_path) {
        throw std::invalid_argument("--write-normalized can only be used with --instance");
    }

    if (!options.has_instance_path) {
        if (!options.generate.has_instance_name ||
            !options.generate.has_depots ||
            !options.generate.has_customers ||
            !options.generate.has_salesmen ||
            !options.generate.has_width ||
            !options.generate.has_height) {
            throw std::invalid_argument("generation mode requires --instance-name, --depots, --customers, --salesmen, --width, --height");
        }

        if (!options.seed.has_value()) {
            throw std::invalid_argument("generation mode requires --seed");
        }

        if (options.generate.depots == 0 || options.generate.customers == 0 || options.generate.salesmen == 0) {
            throw std::invalid_argument("depots, customers and salesmen must be positive");
        }

        if (!(options.generate.width > 0.0) || !(options.generate.height > 0.0)) {
            throw std::invalid_argument("width and height must be positive");
        }
    }

    return options;
}

void ensure_parent_directory(const fs::path& path) {
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        fs::create_directories(parent);
    }
}

void write_text_file_atomic(const fs::path& path, const std::string& content) {
    ensure_parent_directory(path);

    const auto tmp = fs::path(path.string() + ".tmp");
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) {
            throw std::runtime_error("cannot open file for writing: " + path.string());
        }
        out << content;
        if (!out) {
            throw std::runtime_error("failed to write file: " + path.string());
        }
    }

    std::error_code ec;
    fs::remove(path, ec);
    ec.clear();
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(tmp);
        throw std::runtime_error("cannot replace file: " + path.string());
    }
}

mdmtsp::MDMTSPInstance to_solver_instance(const mdmtsp::Instance& input) {
    mdmtsp::MDMTSPInstance output;
    output.name = input.name;
    output.salesman_count = input.salesmen_count();
    output.return_to_depot = input.return_to_depot;

    output.depots.reserve(input.depots.size());
    for (const auto& depot : input.depots) {
        output.depots.push_back(mdmtsp::Point2D{depot.point.x, depot.point.y});
    }

    output.customers.reserve(input.customers.size());
    for (const auto& customer : input.customers) {
        output.customers.push_back(mdmtsp::Point2D{customer.point.x, customer.point.y});
    }

    output.validate_basic();
    return output;
}

mdmtsp::MDMTSPInstance make_generated_instance(const GenerateSpec& spec,
                                               std::uint64_t seed,
                                               bool return_to_depot) {
    mdmtsp::Random rng(seed);

    mdmtsp::MDMTSPInstance instance;
    instance.name = spec.instance_name;
    instance.salesman_count = spec.salesmen;
    instance.return_to_depot = return_to_depot;

    instance.depots.reserve(spec.depots);
    for (std::size_t i = 0; i < spec.depots; ++i) {
        instance.depots.push_back(mdmtsp::Point2D{
            rng.uniform_real<double>(0.0, spec.width),
            rng.uniform_real<double>(0.0, spec.height)
        });
    }

    instance.customers.reserve(spec.customers);
    for (std::size_t i = 0; i < spec.customers; ++i) {
        instance.customers.push_back(mdmtsp::Point2D{
            rng.uniform_real<double>(0.0, spec.width),
            rng.uniform_real<double>(0.0, spec.height)
        });
    }

    instance.validate_basic();
    return instance;
}

json make_result_json(const mdmtsp::MDMTSPInstance& instance,
                      const mdmtsp::MDMTSPSolution& solution,
                      std::uint64_t seed) {
    json result;
    result["instance_name"] = instance.name;
    result["seed"] = seed;
    result["depot_count"] = instance.depot_count();
    result["customer_count"] = instance.customer_count();
    result["salesman_count"] = instance.salesman_count;
    result["return_to_depot"] = instance.return_to_depot;
    result["objective"] = solution.objective;
    result["feasible"] = solution.feasible;
    result["status"] = solution.status;
    result["route_count"] = solution.routes.size();
    result["routes"] = json::array();

    for (const auto& route : solution.routes) {
        result["routes"].push_back({
            {"salesman_id", route.salesman_id},
            {"depot_id", route.depot_id},
            {"nodes", route.nodes}
        });
    }

    return result;
}

void print_text_summary(const mdmtsp::MDMTSPInstance& instance,
                        const mdmtsp::MDMTSPSolution& solution,
                        std::optional<fs::path> source_path) {
    std::cout
        << "Solved MDMTSP instance\n"
        << "  name: " << instance.name << '\n';

    if (source_path.has_value()) {
        std::cout << "  source: " << *source_path << '\n';
    }

    std::cout
        << "  depots: " << instance.depot_count() << '\n'
        << "  customers: " << instance.customer_count() << '\n'
        << "  salesmen: " << instance.salesman_count << '\n'
        << "  return_to_depot: " << (instance.return_to_depot ? "true" : "false") << '\n'
        << "  objective: " << std::fixed << std::setprecision(6) << solution.objective << '\n'
        << "  feasible: " << (solution.feasible ? "true" : "false") << '\n'
        << "  status: " << solution.status << '\n'
        << "  routes: " << solution.routes.size() << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const CliOptions options = parse_args(argc, argv);

        mdmtsp::MDMTSPInstance instance;
        std::optional<fs::path> source_path;
        std::uint64_t effective_seed = options.seed.value_or(0);

        if (options.has_instance_path) {
            auto loaded = mdmtsp::load_instance_from_json(options.instance_path);

            if (options.return_to_depot_override.has_value()) {
                loaded.return_to_depot = *options.return_to_depot_override;
            }

            if (options.write_normalized) {
                mdmtsp::save_instance_to_json(loaded, options.normalized_output_path);
            }

            if (!options.seed.has_value()) {
                effective_seed = loaded.seed;
            }

            instance = to_solver_instance(loaded);
            source_path = options.instance_path;
        } else {
            const bool return_to_depot = options.return_to_depot_override.value_or(true);
            effective_seed = *options.seed;
            instance = make_generated_instance(options.generate, effective_seed, return_to_depot);
        }

        mdmtsp::Random rng(effective_seed);
        auto solution = mdmtsp::solve_mdmtsp_nearest_neighbour(instance, rng);

        if (options.improve_iterations > 0) {
            mdmtsp::improve_interroute_by_relocation(solution, instance, options.improve_iterations);
        }

        const bool emit_json = options.json_output || !options.output_path.empty();
        if (emit_json) {
            const auto result = make_result_json(instance, solution, effective_seed);
            const std::string rendered = result.dump(2);

            if (!options.output_path.empty()) {
                write_text_file_atomic(options.output_path, rendered + '\n');
            } else {
                std::cout << rendered << '\n';
            }
        } else {
            print_text_summary(instance, solution, source_path);
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}