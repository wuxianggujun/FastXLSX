#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "package_writer.hpp"

namespace fastxlsx::detail {

// Phase 1 bootstrap ZIP writer. It writes stored entries without compression so
// OpenXML generation and compatibility tests can move forward before minizip-ng
// is wired into the project.
void write_stored_zip(const std::filesystem::path& path, const std::vector<PackageEntry>& entries);

[[nodiscard]] std::uint32_t crc32(std::string_view data);

} // namespace fastxlsx::detail
