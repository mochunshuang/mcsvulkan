#pragma once

#include "./utils/check_vkresult.hpp"

namespace mcs::vulkan
{
    struct raii_vulkan
    {
        constexpr raii_vulkan()
        {
            check_vkresult(::volkInitialize(), "init vulkan env error.");
        }
        raii_vulkan(const raii_vulkan &) = delete;
        raii_vulkan(raii_vulkan &&) = delete;
        raii_vulkan &operator=(const raii_vulkan &) = delete;
        raii_vulkan &operator=(raii_vulkan &&) = delete;
        constexpr ~raii_vulkan() noexcept
        {
            ::volkFinalize();
        }
    };

}; // namespace mcs::vulkan