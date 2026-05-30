#pragma once

#include "has_all_annotations.hpp"

namespace mcs::vulkan::meta
{

    // consteval bool contains_annotations(
    //     const std::meta::info &info, std::initializer_list<std::meta::info> annotations)
    // {
    //     constexpr auto is_class_info = is_info<std::meta::is_class_type>{};
    //     if (not is_class_info(info))
    //         return false;
    //     return has_all_annotations(std::meta::annotations_of(info),
    //                                std::span{annotations.begin(), annotations.end()});
    // }

}; // namespace mcs::vulkan::meta