#pragma once
#include "create_buffer.hpp"
#include "../tool/pNext.hpp"
#include "find_memory_type_index.hpp"

namespace mcs::vulkan::memory
{
    static constexpr auto create_simple_buffer(const LogicalDevice &device,
                                               create_buffer::create_info info,
                                               VkMemoryPropertyFlags properties) // NOLINT
    {
        auto usage = info.usage;
        return create_buffer{
            std::move(info),
            [&](VkMemoryRequirements memRequirements,
                VkPhysicalDeviceMemoryProperties memoryProperties)
                -> memory_allocate_info {
                return {.pNext =
                            (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
                                ? tool::make_pNext(
                                      tool::structure_chain<VkMemoryAllocateFlagsInfo>{
                                          VkMemoryAllocateFlagsInfo{
                                              .pNext = nullptr,
                                              .flags =
                                                  VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT}})
                                : nullptr,
                        .allocationSize = memRequirements.size,
                        .memoryTypeIndex =
                            find_memory_type_index(memRequirements.memoryTypeBits,
                                                   memoryProperties, properties)};
            }}
            .build(device);
    };
}; // namespace mcs::vulkan::memory