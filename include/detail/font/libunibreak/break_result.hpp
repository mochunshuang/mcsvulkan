#pragma once

#include <vector>
#include "../__libunibreak.hpp"

namespace mcs::vulkan::font::libunibreak
{

    struct break_result // NOLINTBEGIN
    {
        // 每个字符后的断行类型，直接来自
        // libunibreak（0=强制换行，1=允许换行，2=禁止换行，3=不完整）
        std::vector<char> types;

        // 便捷查询
        [[nodiscard]] constexpr bool must_break(size_t idx) const noexcept
        {
            return types[idx] == LINEBREAK_MUSTBREAK;
        }
        [[nodiscard]] constexpr bool allow_break(size_t idx) const noexcept
        {
            return types[idx] == LINEBREAK_ALLOWBREAK;
        }
        [[nodiscard]] constexpr bool no_break(size_t idx) const noexcept
        {
            return types[idx] == LINEBREAK_NOBREAK;
        }
        // 是否可以换行（包括强制和允许）
        [[nodiscard]] constexpr bool can_break(size_t idx) const noexcept
        {
            char t = types[idx];
            return t == LINEBREAK_MUSTBREAK || t == LINEBREAK_ALLOWBREAK;
        }

    }; // NOLINTEND

}; // namespace mcs::vulkan::font::libunibreak