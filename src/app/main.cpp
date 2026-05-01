#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "mdmtsp/instance_io.hpp"

namespace {

struct CliOptions {
    std::filesystem::path instance_path;
    std::filesystem::path normalized_output_path;
    bool write_normalized{false};
};

[[noreturn]] void print_usage_and_exit(const char* argv0, int exit_code) {
    std::ostream& out = exit_code == 0 ? std::cout : std::cerr;
    out << "Usage:\n"
        << "  " << argv0 << " --instance <path> [--write-normalized <path>]\n";
    std::exit(exit_code);
}

[[nodiscard]] CliOptions parse_args(int argc, char** argv) {
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

        throw std::invalid_argument("unknown argument: " + std::string(arg));
    }

    if (options.instance_path.empty()) {
        throw std::invalid_argument("argument --instance is required");
    }

    return options;
}

void print_summary(const mdmtsp::Instance& instance,
                   const std::filesystem::path& source_path) {
    std::cout << "Instance loaded successfully\n"
              << "  file: " << source_path << '\n'
              << "  name: " << instance.name << '\n'
              << "  type: " << mdmtsp::to_string(instance.distance_type) << '\n'
              << "  seed: " << instance.seed << '\n'
              << "  depots: " << instance.depots.size() << '\n'
              << "  customers: " << instance.customers.size() << '\n'
              << "  salesmen: " << instance.salesmen_count() << '\n'
              << "  nodes: " << instance.node_count() << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const CliOptions options = parse_args(argc, argv);
        const mdmtsp::Instance instance = mdmtsp::load_instance_from_json(options.instance_path);

        print_summary(instance, options.instance_path);

        if (options.write_normalized) {
            mdmtsp::save_instance_to_json(instance, options.normalized_output_path);
            std::cout << "  normalized_copy: " << options.normalized_output_path << '\n';
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}