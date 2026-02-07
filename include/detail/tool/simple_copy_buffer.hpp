#pragma once

#include "../Fence.hpp"
#include "../CommandBuffer.hpp"
#include "../Queue.hpp"

namespace mcs::vulkan::tool
{
    static constexpr auto simple_copy_buffer(const CommandPool &commandpool,
                                             const Queue &queue, VkBuffer srcBuffer,
                                             VkBuffer dstBuffer,
                                             const VkBufferCopy &region)
    {
        const auto *logicalDevice = commandpool.device();
        CommandBuffer commandCopyBuffer = commandpool.allocateOneCommandBuffer(
            {.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY});

        commandCopyBuffer.begin({.sType = sType<VkCommandBufferBeginInfo>(),
                                 .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT});
        commandCopyBuffer.copyBuffer(srcBuffer, dstBuffer, {&region, 1});
        commandCopyBuffer.end();

        Fence fence{*logicalDevice, {.sType = sType<VkFenceCreateInfo>(), .flags = 0}};
        queue.submit(1,
                     {.sType = sType<VkSubmitInfo>(),
                      .commandBufferCount = 1,
                      .pCommandBuffers = &*commandCopyBuffer},
                     *fence);
        check_vkresult(logicalDevice->waitForFences(1, *fence, VK_TRUE, UINT64_MAX));
    }
}; // namespace mcs::vulkan::tool