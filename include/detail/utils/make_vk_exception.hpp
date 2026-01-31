#pragma once

#include <format>
#include <source_location>
#include <stdexcept>
#include <string>

#include "extract_filename.hpp"

namespace mcs::vulkan
{
    struct vk_exception : public std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    template <typename ExceptionT = vk_exception>
    static constexpr ExceptionT make_vk_exception(
        const std::string &message,
        const std::source_location &location = std::source_location::current())
    {
        return ExceptionT(std::format("{}:\n  At: {}:{}\n  Function: {}", message,
                                      extract_filename(location.file_name()),
                                      location.line(), location.function_name()));
    }

}; // namespace mcs::vulkan
