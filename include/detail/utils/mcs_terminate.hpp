#pragma once

#include <exception>
#include <string_view>
#include "mcslog.hpp"

namespace mcs::vulkan
{
    constexpr static void mcs_terminate(std::string_view msg)
    {
        MCSLOG_FATAL("{}", msg);
        std::terminate();
    }

}; // namespace mcs::vulkan