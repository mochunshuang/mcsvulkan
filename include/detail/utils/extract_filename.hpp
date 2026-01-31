#pragma once

#include <string_view>

namespace mcs::vulkan
{
    static constexpr std::string_view extract_filename(std::string_view path)
    {
        // E:/0_github_project/vulkan/test/framework/wsi/../../../include/detail/wsi/glfw.hpp
        // -> glfw.hpp
        size_t pos = path.find_last_of('/');
        return pos != std::string_view::npos ? path.substr(pos + 1) : path;
    }
}; // namespace mcs::vulkan