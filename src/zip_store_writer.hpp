#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fastxlsx::detail {

struct ZipEntry {
    std::string name;
    std::string data;
};

// Phase 1 bootstrap ZIP writer. It writes stored entries without compression so
// OpenXML generation and compatibility tests can move forward before minizip-ng
// is wired into the project.
void write_stored_zip(const std::filesystem::path& path, const std::vector<ZipEntry>& entries);

[[nodiscard]] std::uint32_t crc32(std::string_view data);

} // namespace fastxlsx::detail
