#pragma once
#include "create_resources.hpp"
#include "gen_memory_allocate_info.hpp"
#include <utility>

namespace mcs::vulkan::memory
{
    static constexpr auto build_simple_resource(
        create_resources::create_info info, VkMemoryPropertyFlags properties,
        create_resources::GenViewCreateInfo genViewCreateInfo)
    {
        return create_resources{std::move(info), gen_memory_allocate_info(properties),
                                std::move(genViewCreateInfo)};
    }
}; // namespace mcs::vulkan::memory
