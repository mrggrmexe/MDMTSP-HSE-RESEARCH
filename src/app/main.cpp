#include <chrono>
#include <cctype>
#include <cstdint>
#include <ctime>
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
#include "common/random.hpp"
#include "mdmtsp/instance_io.hpp"
#include "mdmtsp/interroute_local_search.hpp"
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

    std::string algorithm_id{"nearest_neighbour"};
    std::optional<std::string> suite_name;
    std::optional<std::string> run_id;

    GenerateSpec generate;
};

[[noreturn]] void print_usage_and_exit(const char* argv0, int exit_code) {
    std::ostream& out = exit_code == 0 ? std::cout : std::cerr;
    out
        << "Usage:\n"
        << "  " << argv0 << " --instance <path> [--algorithm <id>] [--seed <n>] [--improve-iters <n>]\n"
        << "           [--json] [--output <path>] [--suite <name>] [--run-id <id>]\n"
        << "           [--write-normalized <path>] [--open|--closed]\n"
        << "  " << argv0 << " --instance-name <name> --seed <n> --depots <n> --customers <n>\n"
        << "           --salesmen <n> --width <x> --height <y> [--algorithm <id>]\n"
        << "           [--open|--closed] [--improve-iters <n>] [--json] [--output <path>]\n"
        << "           [--suite <name>] [--run-id <id>]\n";
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

std::string canonical_algorithm_id(std::string_view value) {
    if (value == "nearest_neighbour" || value == "nearest-neighbour" || value == "nn") {
        return "nearest_neighbour";
    }

    if (value == "random_insertion" || value == "random-insertion" || value == "ri") {
        return "random_insertion";
    }

    if (value == "cheapest_insertion" || value == "cheapest-insertion" || value == "ci") {
        return "cheapest_insertion";
    }

    throw std::invalid_argument("unsupported algorithm: " + std::string(value));
}

std::string make_safe_token(std::string_view value) {
    std::string out;
    out.reserve(value.size());

    bool prev_underscore = false;
    for (const char ch : value) {
        const auto uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) != 0U) {
            out.push_back(static_cast<char>(uch));
            prev_underscore = false;
        } else if (!prev_underscore) {
            out.push_back('_');
            prev_underscore = true;
        }
    }

    while (!out.empty() && out.front() == '_') {
        out.erase(out.begin());
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }

    if (out.empty()) {
        out = "run";
    }

    return out;
}

std::tm utc_tm_from_time_t(const std::time_t value) {
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &value);
#else
    gmtime_r(&value, &tm);
#endif
    return tm;
}

std::string current_timestamp_utc() {
    const auto now = std::chrono::system_clock::now();
    const auto time_value = std::chrono::system_clock::to_time_t(now);
    const auto tm = utc_tm_from_time_t(time_value);

    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::string compact_timestamp_from_iso(std::string_view iso) {
    std::string out;
    out.reserve(iso.size());

    for (const char ch : iso) {
        const auto uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) != 0U) {
            out.push_back(static_cast<char>(uch));
        }
    }

    return out;
}

std::string make_fallback_run_id(const std::string& algorithm_id,
                                 const std::string& instance_name,
                                 const std::uint64_t seed,
                                 std::string_view timestamp_utc) {
    std::ostringstream out;
    out << compact_timestamp_from_iso(timestamp_utc)
        << "__"
        << make_safe_token(algorithm_id)
        << "__"
        << make_safe_token(instance_name)
        << "__seed_"
        << seed;
    return out.str();
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

        if (arg == "--algorithm") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("missing value after --algorithm");
            }
            options.algorithm_id = canonical_algorithm_id(argv[++i]);
            continue;
        }

        if (arg == "--suite") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("missing value after --suite");
            }
            const std::string value = argv[++i];
            if (value.empty()) {
                throw std::invalid_argument("--suite must not be empty");
            }
            options.suite_name = value;
            continue;
        }

        if (arg == "--run-id") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("missing value after --run-id");
            }
            const std::string value = argv[++i];
            if (value.empty()) {
                throw std::invalid_argument("--run-id must not be empty");
            }
            options.run_id = value;
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
            throw std::invalid_argument(
                "generation mode requires --instance-name, --depots, --customers, "
                "--salesmen, --width, --height"
            );
        }

        if (!options.seed.has_value()) {
            throw std::invalid_argument("generation mode requires --seed");
        }

        if (options.generate.depots == 0 ||
            options.generate.customers == 0 ||
            options.generate.salesmen == 0) {
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
                                               const std::uint64_t seed,
                                               const bool return_to_depot) {
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

mdmtsp::MDMTSPSolution solve_with_algorithm(const std::string& algorithm_id,
                                            const mdmtsp::MDMTSPInstance& instance,
                                            mdmtsp::Random& rng) {
    if (algorithm_id == "nearest_neighbour") {
        return mdmtsp::solve_mdmtsp_nearest_neighbour(instance, rng);
    }

    if (algorithm_id == "random_insertion") {
        return mdmtsp::solve_mdmtsp_random_insertion(instance, rng);
    }

    if (algorithm_id == "cheapest_insertion") {
        return mdmtsp::solve_mdmtsp_cheapest_insertion(instance, rng);
    }

    throw std::invalid_argument("unsupported algorithm: " + algorithm_id);
}

json make_result_json(const mdmtsp::MDMTSPInstance& instance,
                      const mdmtsp::MDMTSPSolution& solution,
                      const std::uint64_t seed,
                      const std::string& algorithm_id,
                      const std::size_t improve_iterations,
                      const std::uint64_t wall_time_us,
                      const std::string& timestamp_utc,
                      const std::string& run_id,
                      const std::optional<std::string>& suite_name,
                      const std::optional<fs::path>& source_path) {
    const double wall_time_ms = static_cast<double>(wall_time_us) / 1000.0;
    const double wall_time_s = static_cast<double>(wall_time_us) / 1000000.0;

    json result;
    result["schema_version"] = 2;
    result["run_id"] = run_id;
    result["timestamp_utc"] = timestamp_utc;
    result["algorithm_id"] = algorithm_id;
    result["suite_name"] = suite_name.has_value() ? json(*suite_name) : json(nullptr);

    result["instance_name"] = instance.name;
    result["instance_path"] = source_path.has_value() ? json(source_path->string()) : json(nullptr);
    result["seed"] = seed;
    result["improve_iterations"] = improve_iterations;
    result["depot_count"] = instance.depot_count();
    result["customer_count"] = instance.customer_count();
    result["salesman_count"] = instance.salesman_count;
    result["return_to_depot"] = instance.return_to_depot;
    result["objective"] = solution.objective;
    result["feasible"] = solution.feasible;
    result["status"] = solution.status;
    result["route_count"] = solution.routes.size();

    result["wall_time_us"] = wall_time_us;
    result["wall_time_ms"] = wall_time_ms;
    result["wall_time_s"] = wall_time_s;

    result["algorithm"] = {
        {"id", algorithm_id},
        {"parameters", {
            {"improve_iterations", improve_iterations}
        }}
    };

    result["instance"] = {
        {"name", instance.name},
        {"path", source_path.has_value() ? json(source_path->string()) : json(nullptr)},
        {"depot_count", instance.depot_count()},
        {"customer_count", instance.customer_count()},
        {"salesman_count", instance.salesman_count},
        {"return_to_depot", instance.return_to_depot}
    };

    result["execution"] = {
        {"seed", seed},
        {"wall_time_us", wall_time_us},
        {"wall_time_ms", wall_time_ms},
        {"wall_time_s", wall_time_s},
        {"timestamp_utc", timestamp_utc}
    };

    result["source"] = {
        {"mode", source_path.has_value() ? "instance_file" : "generated"},
        {"instance_path", source_path.has_value() ? json(source_path->string()) : json(nullptr)}
    };

    result["result"] = {
        {"objective", solution.objective},
        {"feasible", solution.feasible},
        {"status", solution.status},
        {"route_count", solution.routes.size()}
    };

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
                        const std::string& algorithm_id,
                        const std::uint64_t seed,
                        const std::uint64_t wall_time_us,
                        const std::string& run_id,
                        const std::optional<fs::path>& source_path,
                        const std::optional<fs::path>& normalized_output_path) {
    const double wall_time_ms = static_cast<double>(wall_time_us) / 1000.0;

    std::cout
        << "Solved MDMTSP instance\n"
        << "  run_id: " << run_id << '\n'
        << "  algorithm: " << algorithm_id << '\n'
        << "  name: " << instance.name << '\n';

    if (source_path.has_value()) {
        std::cout << "  source: " << *source_path << '\n';
    }

    if (normalized_output_path.has_value()) {
        std::cout << "  normalized_copy: " << *normalized_output_path << '\n';
    }

    std::cout
        << "  seed: " << seed << '\n'
        << "  depots: " << instance.depot_count() << '\n'
        << "  customers: " << instance.customer_count() << '\n'
        << "  salesmen: " << instance.salesman_count << '\n'
        << "  return_to_depot: " << (instance.return_to_depot ? "true" : "false") << '\n'
        << "  objective: " << std::fixed << std::setprecision(6) << solution.objective << '\n'
        << "  feasible: " << (solution.feasible ? "true" : "false") << '\n'
        << "  status: " << solution.status << '\n'
        << "  routes: " << solution.routes.size() << '\n'
        << "  wall_time_ms: " << std::fixed << std::setprecision(3) << wall_time_ms << '\n'
        << "  wall_time_us: " << wall_time_us << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const CliOptions options = parse_args(argc, argv);

        mdmtsp::MDMTSPInstance instance;
        std::optional<fs::path> source_path;
        std::optional<fs::path> normalized_output_path;
        std::uint64_t effective_seed = options.seed.value_or(0);

        if (options.has_instance_path) {
            auto loaded = mdmtsp::load_instance_from_json(options.instance_path);

            if (options.return_to_depot_override.has_value()) {
                loaded.return_to_depot = *options.return_to_depot_override;
            }

            if (options.write_normalized) {
                mdmtsp::save_instance_to_json(loaded, options.normalized_output_path);
                normalized_output_path = options.normalized_output_path;
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

        const std::string timestamp_utc = current_timestamp_utc();
        const std::string run_id = options.run_id.value_or(
            make_fallback_run_id(options.algorithm_id, instance.name, effective_seed, timestamp_utc)
        );

        mdmtsp::Random rng(effective_seed);

        const auto started = std::chrono::steady_clock::now();
        auto solution = solve_with_algorithm(options.algorithm_id, instance, rng);

        if (options.improve_iterations > 0) {
            mdmtsp::improve_interroute_by_relocation(
                solution,
                instance,
                options.improve_iterations
            );
        }

        const auto finished = std::chrono::steady_clock::now();
        const auto wall_time_us = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(finished - started).count()
        );

        const bool emit_json = options.json_output || !options.output_path.empty();
        if (emit_json) {
            const auto result = make_result_json(
                instance,
                solution,
                effective_seed,
                options.algorithm_id,
                options.improve_iterations,
                wall_time_us,
                timestamp_utc,
                run_id,
                options.suite_name,
                source_path
            );

            const std::string rendered = result.dump(2);

            if (!options.output_path.empty()) {
                write_text_file_atomic(options.output_path, rendered + '\n');
            } else {
                std::cout << rendered << '\n';
            }
        } else {
            print_text_summary(
                instance,
                solution,
                options.algorithm_id,
                effective_seed,
                wall_time_us,
                run_id,
                source_path,
                normalized_output_path
            );
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}