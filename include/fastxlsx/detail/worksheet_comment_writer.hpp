#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace fastxlsx::detail {

struct ClassicNote {
    std::uint32_t row = 1;
    std::uint32_t column = 1;
    std::string author;
    std::string text;
};

[[nodiscard]] std::string serialize_classic_comments(std::span<const ClassicNote> notes);
[[nodiscard]] std::string serialize_classic_note_vml(std::span<const ClassicNote> notes);

} // namespace fastxlsx::detail
