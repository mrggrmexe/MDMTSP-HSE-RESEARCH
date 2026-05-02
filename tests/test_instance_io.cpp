#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "mdmtsp/instance_io.hpp"

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void write_text_file(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        throw std::runtime_error("cannot create test file: " + path.string());
    }
    out << content;
    if (!out) {
        throw std::runtime_error("cannot write test file: " + path.string());
    }
}

template <typename Fn>
void expect_throw(Fn&& fn, std::string_view message) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

void test_load_valid_instance(const std::filesystem::path& root) {
    const auto path = root / "valid_instance.json";
    write_text_file(path, R"json(
{
  "name": "unit_valid",
  "type": "euclidean",
  "seed": 7,
  "depots": [
    { "id": 0, "x": 0.0, "y": 0.0, "salesmen": 2 },
    { "id": 1, "x": 10.0, "y": 10.0, "salesmen": 1 }
  ],
  "customers": [
    { "id": 2, "x": 1.5, "y": 2.5 },
    { "id": 3, "x": 3.0, "y": 4.0 },
    { "id": 4, "x": 5.0, "y": 6.0 }
  ]
}
)json");

    const mdmtsp::Instance instance = mdmtsp::load_instance_from_json(path);

    require(instance.name == "unit_valid", "invalid name");
    require(instance.seed == 7, "invalid seed");
    require(instance.depots.size() == 2, "invalid depots count");
    require(instance.customers.size() == 3, "invalid customers count");
    require(instance.salesmen_count() == 3, "invalid salesmen count");
    require(instance.node_count() == 5, "invalid node count");
}

void test_duplicate_id_rejected(const std::filesystem::path& root) {
    const auto path = root / "duplicate_id.json";
    write_text_file(path, R"json(
{
  "name": "unit_duplicate",
  "type": "euclidean",
  "depots": [
    { "id": 0, "x": 0.0, "y": 0.0, "salesmen": 1 }
  ],
  "customers": [
    { "id": 0, "x": 1.0, "y": 1.0 }
  ]
}
)json");

    expect_throw([&] { (void)mdmtsp::load_instance_from_json(path); },
                 "duplicate id must throw");
}

void test_zero_salesmen_rejected(const std::filesystem::path& root) {
    const auto path = root / "zero_salesmen.json";
    write_text_file(path, R"json(
{
  "name": "unit_zero_salesmen",
  "type": "euclidean",
  "depots": [
    { "id": 0, "x": 0.0, "y": 0.0, "salesmen": 0 }
  ],
  "customers": [
    { "id": 1, "x": 1.0, "y": 1.0 }
  ]
}
)json");

    expect_throw([&] { (void)mdmtsp::load_instance_from_json(path); },
                 "zero salesmen must throw");
}

void test_roundtrip_save_load(const std::filesystem::path& root) {
    mdmtsp::Instance instance;
    instance.name = "roundtrip";
    instance.seed = 42;
    instance.depots = {
        mdmtsp::Depot{0, mdmtsp::Point{0.0, 0.0}, 2},
        mdmtsp::Depot{1, mdmtsp::Point{10.0, 10.0}, 1},
    };
    instance.customers = {
        mdmtsp::Customer{2, mdmtsp::Point{1.0, 1.0}},
        mdmtsp::Customer{3, mdmtsp::Point{2.0, 2.0}},
    };

    const auto path = root / "roundtrip.json";
    mdmtsp::save_instance_to_json(instance, path);
    const mdmtsp::Instance loaded = mdmtsp::load_instance_from_json(path);

    require(loaded.name == instance.name, "roundtrip name mismatch");
    require(loaded.seed == instance.seed, "roundtrip seed mismatch");
    require(loaded.depots.size() == instance.depots.size(), "roundtrip depots mismatch");
    require(loaded.customers.size() == instance.customers.size(), "roundtrip customers mismatch");
    require(loaded.salesmen_count() == instance.salesmen_count(), "roundtrip salesmen mismatch");
}

}  // namespace

int main() {
    const auto root =
        std::filesystem::temp_directory_path() / "mdmtsp_test_instance_io";

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    if (ec) {
        throw std::runtime_error("cannot prepare temporary test directory");
    }

    test_load_valid_instance(root);
    test_duplicate_id_rejected(root);
    test_zero_salesmen_rejected(root);
    test_roundtrip_save_load(root);

    std::filesystem::remove_all(root, ec);
    return 0;
}