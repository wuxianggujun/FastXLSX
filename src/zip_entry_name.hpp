#pragma once

#include <fastxlsx/workbook.hpp>

#include <string_view>

namespace fastxlsx::detail {

inline void validate_zip_entry_name(std::string_view name)
{
    if (name.empty()) {
        throw FastXlsxError("ZIP entry name cannot be empty");
    }
    if (name.front() == '/' || name.back() == '/') {
        throw FastXlsxError("ZIP entry name must be a relative file path");
    }

    std::size_t segment_start = 0;
    for (std::size_t index = 0; index <= name.size(); ++index) {
        const bool at_end = index == name.size();
        if (!at_end && name[index] == '\0') {
            throw FastXlsxError("ZIP entry name cannot contain null bytes");
        }
        if (!at_end && name[index] == '\\') {
            throw FastXlsxError("ZIP entry name cannot contain backslashes");
        }
        if (!at_end && (name[index] == '?' || name[index] == '#')) {
            throw FastXlsxError(
                "ZIP entry name cannot contain query or fragment components");
        }
        if (!at_end && name[index] != '/') {
            continue;
        }

        const std::string_view segment = name.substr(segment_start, index - segment_start);
        if (segment.empty() || segment == "." || segment == "..") {
            throw FastXlsxError("ZIP entry name cannot contain empty or parent path segments");
        }
        segment_start = index + 1;
    }
}

} // namespace fastxlsx::detail
