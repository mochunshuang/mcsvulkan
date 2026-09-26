#pragma once
#include <iostream>
#include <cstdlib>

namespace mcs::vulkan
{
    [[noreturn]] static constexpr void fatal_error(const std::string &msg) noexcept
    {
        std::cerr << "Fatal error: " << msg << std::endl; // NOLINT
        std::exit(EXIT_FAILURE);                          // NOLINT
    }
}; // namespace mcs::vulkan