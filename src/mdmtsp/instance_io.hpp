#pragma once

#include <filesystem>
#include <stdexcept>

#include "common/instance.hpp"

namespace mdmtsp {

class InstanceIoError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] Instance load_instance_from_json(const std::filesystem::path& path);
void save_instance_to_json(const Instance& instance, const std::filesystem::path& path);

}  // namespace mdmtsp