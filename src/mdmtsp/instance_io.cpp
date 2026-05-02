#include "mdmtsp/instance_io.hpp"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

namespace mdmtsp {

namespace {

using json = nlohmann::json;

[[nodiscard]] std::string make_error_prefix(const std::filesystem::path& path) {
    return "instance_io(" + path.string() + "): ";
}

[[noreturn]] void throw_parse_error(const std::filesystem::path& path, const std::string& message) {
    throw InstanceIoError(make_error_prefix(path) + message);
}

[[nodiscard]] const json& require_field(const json& object,
                                        std::string_view field,
                                        const std::filesystem::path& path,
                                        std::string_view context) {
    if (!object.is_object()) {
        throw_parse_error(path, std::string(context) + " must be a JSON object");
    }

    const auto it = object.find(std::string(field));
    if (it == object.end()) {
        throw_parse_error(path,
                          std::string("missing field '") + std::string(field) + "' in " +
                              std::string(context));
    }
    return *it;
}

[[nodiscard]] std::size_t read_size_t(const json& object,
                                      std::string_view field,
                                      const std::filesystem::path& path,
                                      std::string_view context) {
    const json& value = require_field(object, field, path, context);

    if (value.is_number_unsigned()) {
        const auto v = value.get<std::uint64_t>();
        if (v > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
            throw_parse_error(path,
                              std::string(context) + "." + std::string(field) +
                                  " does not fit into size_t");
        }
        return static_cast<std::size_t>(v);
    }

    if (value.is_number_integer()) {
        const auto v = value.get<long long>();
        if (v < 0) {
            throw_parse_error(path,
                              std::string(context) + "." + std::string(field) +
                                  " must be non-negative");
        }
        return static_cast<std::size_t>(v);
    }

    throw_parse_error(path,
                      std::string(context) + "." + std::string(field) +
                          " must be an integer");
}

[[nodiscard]] std::uint64_t read_uint64_with_default(const json& object,
                                                     std::string_view field,
                                                     std::uint64_t default_value,
                                                     const std::filesystem::path& path,
                                                     std::string_view context) {
    const auto it = object.find(std::string(field));
    if (it == object.end()) {
        return default_value;
    }

    if (it->is_number_unsigned()) {
        return it->get<std::uint64_t>();
    }

    if (it->is_number_integer()) {
        const auto v = it->get<long long>();
        if (v < 0) {
            throw_parse_error(path,
                              std::string(context) + "." + std::string(field) +
                                  " must be non-negative");
        }
        return static_cast<std::uint64_t>(v);
    }

    throw_parse_error(path,
                      std::string(context) + "." + std::string(field) +
                          " must be an integer");
}

[[nodiscard]] bool read_bool_with_default(const json& object,
                                          std::string_view field,
                                          bool default_value,
                                          const std::filesystem::path& path,
                                          std::string_view context) {
    const auto it = object.find(std::string(field));
    if (it == object.end()) {
        return default_value;
    }

    if (!it->is_boolean()) {
        throw_parse_error(path,
                          std::string(context) + "." + std::string(field) +
                              " must be a boolean");
    }

    return it->get<bool>();
}

[[nodiscard]] double read_finite_double(const json& object,
                                        std::string_view field,
                                        const std::filesystem::path& path,
                                        std::string_view context) {
    const json& value = require_field(object, field, path, context);

    if (!value.is_number()) {
        throw_parse_error(path,
                          std::string(context) + "." + std::string(field) +
                              " must be numeric");
    }

    const double v = value.get<double>();
    if (!std::isfinite(v)) {
        throw_parse_error(path,
                          std::string(context) + "." + std::string(field) +
                              " must be finite");
    }
    return v;
}

[[nodiscard]] std::string read_string_with_default(const json& object,
                                                   std::string_view field,
                                                   std::string default_value,
                                                   const std::filesystem::path& path,
                                                   std::string_view context) {
    const auto it = object.find(std::string(field));
    if (it == object.end()) {
        return default_value;
    }

    if (!it->is_string()) {
        throw_parse_error(path,
                          std::string(context) + "." + std::string(field) +
                              " must be a string");
    }

    return it->get<std::string>();
}

void ensure_unique_id(std::unordered_set<std::size_t>& used_ids,
                      std::size_t id,
                      const std::filesystem::path& path,
                      std::string_view context) {
    const auto [_, inserted] = used_ids.insert(id);
    if (!inserted) {
        throw_parse_error(path, std::string("duplicate node id in ") + std::string(context) +
                                    ": " + std::to_string(id));
    }
}

[[nodiscard]] Point parse_point(const json& value,
                                const std::filesystem::path& path,
                                std::string_view context) {
    Point point;
    point.x = read_finite_double(value, "x", path, context);
    point.y = read_finite_double(value, "y", path, context);
    return point;
}

[[nodiscard]] Depot parse_new_depot(const json& value,
                                    std::size_t index,
                                    std::unordered_set<std::size_t>& used_ids,
                                    const std::filesystem::path& path) {
    const std::string context = "depots[" + std::to_string(index) + "]";

    Depot depot;
    depot.id = read_size_t(value, "id", path, context);
    depot.point = parse_point(value, path, context);
    depot.salesmen = read_size_t(value, "salesmen", path, context);

    ensure_unique_id(used_ids, depot.id, path, context);
    return depot;
}

[[nodiscard]] Customer parse_new_customer(const json& value,
                                          std::size_t index,
                                          std::unordered_set<std::size_t>& used_ids,
                                          const std::filesystem::path& path) {
    const std::string context = "customers[" + std::to_string(index) + "]";

    Customer customer;
    customer.id = read_size_t(value, "id", path, context);
    customer.point = parse_point(value, path, context);

    ensure_unique_id(used_ids, customer.id, path, context);
    return customer;
}

[[nodiscard]] bool looks_like_new_schema(const json& root) {
    const auto depots_it = root.find("depots");
    if (depots_it == root.end() || !depots_it->is_array() || depots_it->empty()) {
        return false;
    }

    const auto& first = (*depots_it)[0];
    if (!first.is_object()) {
        return false;
    }

    return first.contains("id") || first.contains("salesmen") || root.contains("type") || root.contains("seed");
}

[[nodiscard]] std::vector<std::size_t> distribute_salesmen_evenly(std::size_t total,
                                                                  std::size_t depot_count) {
    std::vector<std::size_t> counts(depot_count, 0);

    if (depot_count == 0) {
        return counts;
    }

    const auto base = total / depot_count;
    const auto rem = total % depot_count;

    for (std::size_t i = 0; i < depot_count; ++i) {
        counts[i] = base + (i < rem ? 1U : 0U);
    }

    return counts;
}

void parse_new_schema(const json& root,
                      const std::filesystem::path& path,
                      Instance& instance) {
    instance.name = read_string_with_default(root, "name", path.stem().string(), path, "root");
    instance.seed = read_uint64_with_default(root, "seed", 0U, path, "root");
    instance.return_to_depot = read_bool_with_default(root, "return_to_depot", true, path, "root");
    instance.distance_type =
        distance_type_from_string(read_string_with_default(root, "type", "euclidean", path, "root"));

    const json& depots = root.at("depots");
    const json& customers = root.at("customers");

    instance.depots.reserve(depots.size());
    instance.customers.reserve(customers.size());

    std::unordered_set<std::size_t> used_ids;
    used_ids.reserve(depots.size() + customers.size());

    for (std::size_t i = 0; i < depots.size(); ++i) {
        instance.depots.push_back(parse_new_depot(depots[i], i, used_ids, path));
    }

    for (std::size_t i = 0; i < customers.size(); ++i) {
        instance.customers.push_back(parse_new_customer(customers[i], i, used_ids, path));
    }
}

void parse_legacy_schema(const json& root,
                         const std::filesystem::path& path,
                         Instance& instance) {
    instance.name = read_string_with_default(root, "name", path.stem().string(), path, "root");
    instance.seed = read_uint64_with_default(root, "seed", 0U, path, "root");
    instance.return_to_depot = read_bool_with_default(root, "return_to_depot", true, path, "root");
    instance.distance_type = DistanceType::Euclidean2D;

    const auto salesman_count = read_size_t(root, "salesman_count", path, "root");
    const json& depots = root.at("depots");
    const json& customers = root.at("customers");

    const auto depot_salesmen = distribute_salesmen_evenly(salesman_count, depots.size());

    instance.depots.reserve(depots.size());
    instance.customers.reserve(customers.size());

    for (std::size_t i = 0; i < depots.size(); ++i) {
        const std::string context = "depots[" + std::to_string(i) + "]";
        instance.depots.push_back(Depot{
            i,
            parse_point(depots[i], path, context),
            depot_salesmen[i]
        });
    }

    for (std::size_t i = 0; i < customers.size(); ++i) {
        const std::string context = "customers[" + std::to_string(i) + "]";
        instance.customers.push_back(Customer{
            depots.size() + i,
            parse_point(customers[i], path, context)
        });
    }
}

void validate_root_arrays(const json& root, const std::filesystem::path& path) {
    const json& depots = require_field(root, "depots", path, "root");
    const json& customers = require_field(root, "customers", path, "root");

    if (!depots.is_array()) {
        throw_parse_error(path, "root.depots must be an array");
    }
    if (!customers.is_array()) {
        throw_parse_error(path, "root.customers must be an array");
    }
    if (depots.empty()) {
        throw_parse_error(path, "root.depots must not be empty");
    }
    if (customers.empty()) {
        throw_parse_error(path, "root.customers must not be empty");
    }
}

void validate_instance_or_throw(const Instance& instance, const std::filesystem::path& path) {
    if (instance.depots.empty()) {
        throw InstanceIoError(make_error_prefix(path) + "instance must contain at least one depot");
    }
    if (instance.customers.empty()) {
        throw InstanceIoError(make_error_prefix(path) + "instance must contain at least one customer");
    }
    if (instance.salesmen_count() == 0) {
        throw InstanceIoError(make_error_prefix(path) + "total number of salesmen must be positive");
    }
}

}  // namespace

Instance load_instance_from_json(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        throw InstanceIoError(make_error_prefix(path) + "file does not exist");
    }
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
        throw InstanceIoError(make_error_prefix(path) + "path is not a regular file");
    }

    std::ifstream input(path);
    if (!input) {
        throw InstanceIoError(make_error_prefix(path) + "cannot open file for reading");
    }

    json root;
    try {
        input >> root;
    } catch (const std::exception& e) {
        throw InstanceIoError(make_error_prefix(path) + "invalid JSON: " + e.what());
    }

    if (!root.is_object()) {
        throw InstanceIoError(make_error_prefix(path) + "root must be a JSON object");
    }

    validate_root_arrays(root, path);

    Instance instance;
    if (looks_like_new_schema(root)) {
        parse_new_schema(root, path, instance);
    } else {
        parse_legacy_schema(root, path, instance);
    }

    validate_instance_or_throw(instance, path);
    return instance;
}

void save_instance_to_json(const Instance& instance, const std::filesystem::path& path) {
    validate_instance_or_throw(instance, path);

    std::unordered_set<std::size_t> used_ids;
    used_ids.reserve(instance.node_count());

    for (const Depot& depot : instance.depots) {
        if (!std::isfinite(depot.point.x) || !std::isfinite(depot.point.y)) {
            throw InstanceIoError(make_error_prefix(path) + "depot coordinates must be finite");
        }
        if (!used_ids.insert(depot.id).second) {
            throw InstanceIoError(make_error_prefix(path) + "duplicate depot id: " +
                                  std::to_string(depot.id));
        }
    }

    for (const Customer& customer : instance.customers) {
        if (!std::isfinite(customer.point.x) || !std::isfinite(customer.point.y)) {
            throw InstanceIoError(make_error_prefix(path) + "customer coordinates must be finite");
        }
        if (!used_ids.insert(customer.id).second) {
            throw InstanceIoError(make_error_prefix(path) + "duplicate customer id: " +
                                  std::to_string(customer.id));
        }
    }

    json root;
    root["name"] = instance.name;
    root["type"] = to_string(instance.distance_type);
    root["seed"] = instance.seed;
    root["return_to_depot"] = instance.return_to_depot;
    root["salesman_count"] = instance.salesmen_count();
    root["depots"] = json::array();
    root["customers"] = json::array();

    for (const Depot& depot : instance.depots) {
        root["depots"].push_back(
            {{"id", depot.id},
             {"x", depot.point.x},
             {"y", depot.point.y},
             {"salesmen", depot.salesmen}});
    }

    for (const Customer& customer : instance.customers) {
        root["customers"].push_back(
            {{"id", customer.id},
             {"x", customer.point.x},
             {"y", customer.point.y}});
    }

    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            throw InstanceIoError(make_error_prefix(path) +
                                  "cannot create parent directories: " + ec.message());
        }
    }

    const auto tmp_path = std::filesystem::path(path.string() + ".tmp");

    {
        std::ofstream output(tmp_path, std::ios::trunc);
        if (!output) {
            throw InstanceIoError(make_error_prefix(path) + "cannot open temp file for writing");
        }
        output << std::setw(2) << root << '\n';
        if (!output) {
            throw InstanceIoError(make_error_prefix(path) + "failed to write JSON");
        }
    }

    std::error_code ec;
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(tmp_path, path, ec);
    if (ec) {
        std::filesystem::remove(tmp_path);
        throw InstanceIoError(make_error_prefix(path) + "cannot replace file: " + ec.message());
    }
}

}  // namespace mdmtsp