#pragma once

#include "PhysicalDevice.hpp"

namespace mcs::vulkan
{
    class LogicalDevice
    {
        using value_type = VkDevice;
        const PhysicalDevice *physicalDevice_{};
        value_type value_{};
        ::VolkDeviceTable table_{};

      public:
        constexpr operator bool() const noexcept // NOLINT
        {
            return value_ != nullptr;
        }
        constexpr value_type &operator*() noexcept
        {
            return value_;
        }
        constexpr const value_type &operator*() const noexcept
        {
            return value_;
        }
        [[nodiscard]] auto allocator() const noexcept
        {
            return physicalDevice_->allocator();
        }
        LogicalDevice(const LogicalDevice &) = delete;
        constexpr LogicalDevice(LogicalDevice &&o) noexcept
            : physicalDevice_{std::exchange(o.physicalDevice_, {})},
              value_{std::exchange(o.value_, {})}, table_{std::exchange(o.table_, {})} {};
        LogicalDevice &operator=(const LogicalDevice &) = delete;
        constexpr LogicalDevice &operator=(LogicalDevice &&o) noexcept
        {
            if (&o != this)
            {
                destroy();
                physicalDevice_ = std::exchange(o.physicalDevice_, {});
                value_ = std::exchange(o.value_, {});
                table_ = std::exchange(o.table_, {});
            }
            return *this;
        }
        LogicalDevice(const PhysicalDevice &physicalDevice, value_type value) noexcept
            : physicalDevice_{&physicalDevice}, value_{value}
        {
            ::volkLoadDeviceTable(&table_, value_);
        }
        constexpr void destroy() noexcept
        {
            if (value_ != nullptr)
            {
                table_.vkDestroyDevice(value_, physicalDevice_->allocator());
            }
        }
        constexpr ~LogicalDevice() noexcept
        {
            destroy();
        }

        [[nodiscard]] constexpr VkQueue getDeviceQueue(uint32_t queueFamilyIndex,
                                                       uint32_t queueIndex) const noexcept
        {
            MCS_ASSERT(table_.vkGetDeviceQueue != nullptr);
            VkQueue queue; // NOLINT
            table_.vkGetDeviceQueue(value_, queueFamilyIndex, queueIndex, &queue);
            return queue;
        }

        constexpr void destroyDevice(
            const VkAllocationCallbacks *pAllocator) const noexcept
        {
            MCS_ASSERT(table_.vkDestroyDevice != nullptr);
            table_.vkDestroyDevice(value_, pAllocator);
        }

        constexpr void destroyImageView(
            VkImageView view, const VkAllocationCallbacks *pAllocator) const noexcept
        {
            MCS_ASSERT(table_.vkDestroyImageView != nullptr);
            table_.vkDestroyImageView(value_, view, pAllocator);
        }

        [[nodiscard]] constexpr auto createImageView(
            const VkImageViewCreateInfo &createInfo,
            const VkAllocationCallbacks *pAllocator) const
        {
            MCS_ASSERT(table_.vkCreateImageView != nullptr);
            VkImageView view; // NOLINT
            check_vkresult(
                table_.vkCreateImageView(value_, &createInfo, pAllocator, &view));
            return view;
        }

        constexpr void destroySwapchainKHR(
            VkSwapchainKHR swapChain,
            const VkAllocationCallbacks *pAllocator) const noexcept
        {
            MCS_ASSERT(table_.vkDestroySwapchainKHR != nullptr);
            table_.vkDestroySwapchainKHR(value_, swapChain, pAllocator);
        }

        [[nodiscard]] constexpr VkSwapchainKHR createSwapchainKHR(
            const VkSwapchainCreateInfoKHR &createInfo,
            const VkAllocationCallbacks *pAllocator) const
        {
            MCS_ASSERT(table_.vkCreateSwapchainKHR != nullptr);
            VkSwapchainKHR swapChain; // NOLINT
            check_vkresult(
                table_.vkCreateSwapchainKHR(value_, &createInfo, pAllocator, &swapChain));
            return swapChain;
        }

        [[nodiscard]] constexpr std::vector<VkImage> getSwapchainImagesKHR(
            VkSwapchainKHR swapchain) const
        {
            MCS_ASSERT(table_.vkGetSwapchainImagesKHR != nullptr);
            uint32_t imageCount; // NOLINT
            check_vkresult(
                table_.vkGetSwapchainImagesKHR(value_, swapchain, &imageCount, nullptr));
            std::vector<VkImage> associatedImages{imageCount};
            check_vkresult(table_.vkGetSwapchainImagesKHR(value_, swapchain, &imageCount,
                                                          associatedImages.data()));
            return associatedImages;
        }

        [[nodiscard]] constexpr auto createPipelineLayout(
            const VkPipelineLayoutCreateInfo &createInfo,
            const VkAllocationCallbacks *pAllocator) const
        {
            MCS_ASSERT(table_.vkCreatePipelineLayout != nullptr);
            VkPipelineLayout pipelineLayout; // NOLINT
            check_vkresult(table_.vkCreatePipelineLayout(value_, &createInfo, pAllocator,
                                                         &pipelineLayout));
            return pipelineLayout;
        }

        constexpr void destroyPipelineLayout(
            VkPipelineLayout pipelineLayout,
            const VkAllocationCallbacks *pAllocator) const noexcept
        {
            MCS_ASSERT(table_.vkDestroyPipelineLayout != nullptr);
            table_.vkDestroyPipelineLayout(value_, pipelineLayout, pAllocator);
        }

        [[nodiscard]] constexpr VkShaderModule createShaderModule(
            const VkShaderModuleCreateInfo &createInfo,
            const VkAllocationCallbacks *allocator) const
        {
            MCS_ASSERT(table_.vkCreateShaderModule != nullptr);
            VkShaderModule shaderModule; // NOLINT
            check_vkresult(table_.vkCreateShaderModule(value_, &createInfo, allocator,
                                                       &shaderModule));
            return shaderModule;
        }

        constexpr void destroyShaderModule(VkShaderModule shaderModule,
                                           const VkAllocationCallbacks *allocator) const
        {
            MCS_ASSERT(table_.vkDestroyShaderModule != nullptr);
            table_.vkDestroyShaderModule(value_, shaderModule, allocator);
        }

        [[nodiscard]] constexpr VkPipeline createGraphicsPipelines(
            VkPipelineCache pipelineCache, uint32_t createInfoCount,
            const VkGraphicsPipelineCreateInfo &createInfos,
            const VkAllocationCallbacks *allocator) const
        {
            MCS_ASSERT(table_.vkCreateGraphicsPipelines != nullptr);
            VkPipeline pipelines; // NOLINT
            check_vkresult(table_.vkCreateGraphicsPipelines(value_, pipelineCache,
                                                            createInfoCount, &createInfos,
                                                            allocator, &pipelines));
            return pipelines;
        }

        constexpr void destroyPipeline(
            VkPipeline pipelines, const VkAllocationCallbacks *allocator) const noexcept
        {
            MCS_ASSERT(table_.vkDestroyPipeline != nullptr);
            table_.vkDestroyPipeline(value_, pipelines, allocator);
        }

        constexpr void waitIdle() const
        {
            MCS_ASSERT(table_.vkDeviceWaitIdle != nullptr);
            check_vkresult(table_.vkDeviceWaitIdle(value_));
        }

        [[nodiscard]] constexpr VkCommandPool createCommandPool(
            const VkCommandPoolCreateInfo &poolInfo,
            const VkAllocationCallbacks *pAllocator) const
        {
            MCS_ASSERT(table_.vkCreateCommandPool != nullptr);
            VkCommandPool command_pool; // NOLINT
            check_vkresult(
                table_.vkCreateCommandPool(value_, &poolInfo, pAllocator, &command_pool));
            return command_pool;
        }

        constexpr void destroyCommandPool(
            VkCommandPool command_pool,
            const VkAllocationCallbacks *pAllocator) const noexcept
        {
            MCS_ASSERT(table_.vkDestroyCommandPool != nullptr);
            table_.vkDestroyCommandPool(value_, command_pool, pAllocator);
        }

        constexpr void allocateCommandBuffers(
            VkCommandBuffer &commandBuffer,
            const VkCommandBufferAllocateInfo &allocInfo) const
        {
            MCS_ASSERT(table_.vkAllocateCommandBuffers != nullptr);
            check_vkresult(
                table_.vkAllocateCommandBuffers(value_, &allocInfo, &commandBuffer));
        }

        [[nodiscard]] constexpr auto allocateCommandBuffers(
            const VkCommandBufferAllocateInfo &allocInfo) const
        {
            MCS_ASSERT(table_.vkAllocateCommandBuffers != nullptr);
            std::vector<VkCommandBuffer> commandBuffer{allocInfo.commandBufferCount};
            check_vkresult(table_.vkAllocateCommandBuffers(value_, &allocInfo,
                                                           commandBuffer.data()));
            return commandBuffer;
        }

        constexpr void freeCommandBuffers(VkCommandPool commandPool,
                                          uint32_t commandBufferCount,
                                          const VkCommandBuffer *data) const noexcept
        {
            MCS_ASSERT(table_.vkFreeCommandBuffers != nullptr);
            table_.vkFreeCommandBuffers(value_, commandPool, commandBufferCount, data);
        }
        constexpr void freeOneCommandBuffer(
            VkCommandPool commandPool,
            const VkCommandBuffer &commandBuffers) const noexcept
        {
            freeCommandBuffers(commandPool, 1, &commandBuffers);
        }

        [[nodiscard]] constexpr auto createSemaphore(
            const VkSemaphoreCreateInfo &createInfo,
            const VkAllocationCallbacks *pAllocator) const
        {
            MCS_ASSERT(table_.vkCreateSemaphore != nullptr);
            VkSemaphore semaphore; // NOLINT
            check_vkresult(
                table_.vkCreateSemaphore(value_, &createInfo, pAllocator, &semaphore));
            return semaphore;
        }

        constexpr void destroySemaphore(
            VkSemaphore semaphore, const VkAllocationCallbacks *pAllocator) const noexcept
        {
            MCS_ASSERT(table_.vkDestroySemaphore != nullptr);
            table_.vkDestroySemaphore(value_, semaphore, pAllocator);
        }

        [[nodiscard]] constexpr auto createFence(
            const VkFenceCreateInfo &createInfo,
            const VkAllocationCallbacks *pAllocator) const
        {
            MCS_ASSERT(table_.vkCreateFence != nullptr);
            VkFence fence; // NOLINT
            check_vkresult(table_.vkCreateFence(value_, &createInfo, pAllocator, &fence));
            return fence;
        }

        constexpr void destroyFence(
            VkFence fence, const VkAllocationCallbacks *pAllocator) const noexcept
        {
            MCS_ASSERT(table_.vkDestroyFence != nullptr);
            table_.vkDestroyFence(value_, fence, pAllocator);
        }

        constexpr void beginCommandBuffer(VkCommandBuffer cmb,
                                          const VkCommandBufferBeginInfo &info) const
        {
            MCS_ASSERT(table_.vkBeginCommandBuffer != nullptr);
            check_vkresult(table_.vkBeginCommandBuffer(cmb, &info));
        }

        constexpr void cmdBeginRendering(
            VkCommandBuffer commandBuffer,
            const VkRenderingInfo &renderingInfo) const noexcept
        {
            MCS_ASSERT(table_.vkCmdBeginRendering != nullptr);
            table_.vkCmdBeginRendering(commandBuffer, &renderingInfo);
        }

        constexpr void cmdBindPipeline(VkCommandBuffer commandBuffer,
                                       VkPipelineBindPoint pipelineBindPoint,
                                       VkPipeline pipeline) const noexcept
        {
            MCS_ASSERT(table_.vkCmdBindPipeline != nullptr);
            table_.vkCmdBindPipeline(commandBuffer, pipelineBindPoint, pipeline);
        }

        constexpr void cmdSetViewport(VkCommandBuffer commandBuffer,
                                      uint32_t firstViewport, uint32_t viewportCount,
                                      const VkViewport *pViewports) const noexcept
        {
            MCS_ASSERT(table_.vkCmdSetViewport != nullptr);
            table_.vkCmdSetViewport(commandBuffer, firstViewport, viewportCount,
                                    pViewports);
        }

        constexpr void cmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor,
                                     uint32_t scissorCount,
                                     const VkRect2D *pScissors) const noexcept
        {
            MCS_ASSERT(table_.vkCmdSetScissor != nullptr);
            table_.vkCmdSetScissor(commandBuffer, firstScissor, scissorCount, pScissors);
        }

        constexpr void cmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount,
                               uint32_t instanceCount, uint32_t firstVertex,
                               uint32_t firstInstance) const noexcept
        {
            MCS_ASSERT(table_.vkCmdDraw != nullptr);
            table_.vkCmdDraw(commandBuffer, vertexCount, instanceCount, firstVertex,
                             firstInstance);
        }

        constexpr void cmdEndRendering(VkCommandBuffer commandBuffer) const noexcept
        {
            MCS_ASSERT(table_.vkCmdEndRendering != nullptr);
            table_.vkCmdEndRendering(commandBuffer);
        }

        constexpr void endCommandBuffer(VkCommandBuffer commandBuffer) const
        {
            MCS_ASSERT(table_.vkEndCommandBuffer != nullptr);
            check_vkresult(table_.vkEndCommandBuffer(commandBuffer));
        }

        [[nodiscard]] constexpr auto waitForFences(uint32_t fenceCount,
                                                   const VkFence &fences,
                                                   VkBool32 waitAll,
                                                   uint64_t timeout) const noexcept
        {
            MCS_ASSERT(table_.vkWaitForFences != nullptr);
            return table_.vkWaitForFences(value_, fenceCount, &fences, waitAll, timeout);
        }

        [[nodiscard]] constexpr auto acquireNextImageKHR(VkSwapchainKHR swapchain,
                                                         uint64_t timeout,
                                                         VkSemaphore semaphore,
                                                         VkFence fence) const noexcept
        {
            MCS_ASSERT(table_.vkAcquireNextImageKHR != nullptr);
            uint32_t index; // NOLINT
            return std::make_pair(table_.vkAcquireNextImageKHR(value_, swapchain, timeout,
                                                               semaphore, fence, &index),
                                  index);
        }

        constexpr auto resetFences(uint32_t fenceCount, const VkFence &pFences) const
        {
            MCS_ASSERT(table_.vkResetFences != nullptr);
            check_vkresult(table_.vkResetFences(value_, fenceCount, &pFences));
        }

        constexpr auto resetCommandBuffer(VkCommandBuffer commandBuffer,
                                          VkCommandBufferResetFlagBits flags) const
        {
            MCS_ASSERT(table_.vkResetCommandBuffer != nullptr);
            check_vkresult(table_.vkResetCommandBuffer(commandBuffer, flags));
        }

        constexpr auto queueSubmit(VkQueue queue, uint32_t submitCount,
                                   const VkSubmitInfo &submits, VkFence fence) const
        {
            MCS_ASSERT(table_.vkQueueSubmit != nullptr);
            check_vkresult(table_.vkQueueSubmit(queue, submitCount, &submits, fence));
        }

        constexpr auto queuePresentKHR(VkQueue queue,
                                       const VkPresentInfoKHR &presentInfo) const
        {
            MCS_ASSERT(table_.vkQueuePresentKHR != nullptr);
            return table_.vkQueuePresentKHR(queue, &presentInfo);
        }

        [[nodiscard]] constexpr auto createBuffer(
            const VkBufferCreateInfo &createInfo,
            const VkAllocationCallbacks *pAllocator) const
        {
            MCS_ASSERT(table_.vkCreateBuffer != nullptr);
            VkBuffer buffer; // NOLINT
            check_vkresult(
                table_.vkCreateBuffer(value_, &createInfo, pAllocator, &buffer));
            return buffer;
        }

        [[nodiscard]] constexpr auto getBufferMemoryRequirements(
            VkBuffer buffer) const noexcept
        {
            MCS_ASSERT(table_.vkGetBufferMemoryRequirements != nullptr);
            VkMemoryRequirements requirements;
            table_.vkGetBufferMemoryRequirements(value_, buffer, &requirements);
            return requirements;
        }

        [[nodiscard]] constexpr auto allocateMemory(
            const VkMemoryAllocateInfo &allocateInfo,
            const VkAllocationCallbacks *pAllocator) const
        {
            MCS_ASSERT(table_.vkAllocateMemory != nullptr);
            VkDeviceMemory memory; // NOLINT
            check_vkresult(
                table_.vkAllocateMemory(value_, &allocateInfo, pAllocator, &memory));
            return memory;
        }

        constexpr void destroyBuffer(
            VkBuffer buffer, const VkAllocationCallbacks *pAllocator) const noexcept
        {
            MCS_ASSERT(table_.vkDestroyBuffer != nullptr);
            table_.vkDestroyBuffer(value_, buffer, pAllocator);
        }

        constexpr void bindBufferMemory(VkBuffer buffer, VkDeviceMemory memory,
                                        VkDeviceSize memoryOffset) const
        {
            MCS_ASSERT(table_.vkBindBufferMemory != nullptr);
            check_vkresult(
                table_.vkBindBufferMemory(value_, buffer, memory, memoryOffset));
        }

        constexpr void freeMemory(VkDeviceMemory memory,
                                  const VkAllocationCallbacks *pAllocator) const noexcept
        {
            MCS_ASSERT(table_.vkFreeMemory != nullptr);
            table_.vkFreeMemory(value_, memory, pAllocator);
        }

        constexpr void mapMemory(VkDeviceMemory memory, VkDeviceSize offset,
                                 VkDeviceSize size, VkMemoryMapFlags flags,
                                 void **ppData) const
        {
            MCS_ASSERT(table_.vkMapMemory != nullptr);
            check_vkresult(
                table_.vkMapMemory(value_, memory, offset, size, flags, ppData));
        }

        constexpr void unmapMemory(VkDeviceMemory memory) const noexcept
        {
            MCS_ASSERT(table_.vkUnmapMemory != nullptr);
            table_.vkUnmapMemory(value_, memory);
        }

        constexpr void cmdPipelineBarrier2(
            VkCommandBuffer commandBuffer,
            const VkDependencyInfo &dependencyInfo) const noexcept
        {
            MCS_ASSERT(table_.vkCmdPipelineBarrier2 != nullptr);
            table_.vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
        }

        constexpr void cmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer,
                                     VkBuffer dstBuffer, uint32_t count,
                                     const VkBufferCopy *regions) const noexcept
        {
            MCS_ASSERT(table_.vkCmdCopyBuffer != nullptr);
            table_.vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, count, regions);
        }

        constexpr void queueWaitIdle(VkQueue queue) const noexcept
        {
            MCS_ASSERT(table_.vkQueueWaitIdle != nullptr);
            table_.vkQueueWaitIdle(queue);
        }

        [[nodiscard]] constexpr auto getBufferDeviceAddress(
            const VkBufferDeviceAddressInfo &Info) const noexcept
        {
            MCS_ASSERT(table_.vkGetBufferDeviceAddress != nullptr);
            return table_.vkGetBufferDeviceAddress(value_, &Info);
        }

        constexpr void cmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer,
                                          VkDeviceSize offset,
                                          VkIndexType indexType) const noexcept
        {
            MCS_ASSERT(table_.vkCmdBindIndexBuffer != nullptr);
            table_.vkCmdBindIndexBuffer(commandBuffer, buffer, offset, indexType);
        }

        constexpr void cmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount,
                                      uint32_t instanceCount, uint32_t firstIndex,
                                      int32_t vertexOffset,
                                      uint32_t firstInstance) const noexcept
        {
            MCS_ASSERT(table_.vkCmdDrawIndexed != nullptr);
            table_.vkCmdDrawIndexed(commandBuffer, indexCount, instanceCount, firstIndex,
                                    vertexOffset, firstInstance);
        }

        constexpr void cmdPushConstants(VkCommandBuffer commandBuffer,
                                        VkPipelineLayout layout,
                                        VkShaderStageFlags stageFlags, uint32_t offset,
                                        uint32_t size, const void *pValues) const noexcept
        {
            MCS_ASSERT(table_.vkCmdPushConstants != nullptr);
            table_.vkCmdPushConstants(commandBuffer, layout, stageFlags, offset, size,
                                      pValues);
        }

        [[nodiscard]] constexpr auto createDescriptorSetLayout(
            const VkDescriptorSetLayoutCreateInfo &createInfo,
            const VkAllocationCallbacks *pAllocator) const
        {
            MCS_ASSERT(table_.vkCreateDescriptorSetLayout != nullptr);
            VkDescriptorSetLayout setLayout; // NOLINT
            check_vkresult(table_.vkCreateDescriptorSetLayout(value_, &createInfo,
                                                              pAllocator, &setLayout));
            return setLayout;
        }

        [[nodiscard]] constexpr auto createDescriptorPool(
            const VkDescriptorPoolCreateInfo &createInfo,
            const VkAllocationCallbacks *pAllocator) const
        {
            MCS_ASSERT(table_.vkCreateDescriptorPool != nullptr);
            VkDescriptorPool descriptorPool; // NOLINT
            check_vkresult(table_.vkCreateDescriptorPool(value_, &createInfo, pAllocator,
                                                         &descriptorPool));
            return descriptorPool;
        }

        [[nodiscard]] constexpr std::vector<VkDescriptorSet> allocateDescriptorSets(
            const VkDescriptorSetAllocateInfo &allocateInfo) const
        {
            MCS_ASSERT(table_.vkAllocateDescriptorSets != nullptr);
            std::vector<VkDescriptorSet> descriptorSet{allocateInfo.descriptorSetCount};
            check_vkresult(table_.vkAllocateDescriptorSets(value_, &allocateInfo,
                                                           descriptorSet.data()));
            return descriptorSet;
        }

        constexpr void updateDescriptorSets(
            uint32_t descriptorWriteCount, const VkWriteDescriptorSet *descriptorWrites,
            uint32_t descriptorCopyCount,
            const VkCopyDescriptorSet *descriptorCopies) const noexcept
        {
            MCS_ASSERT(table_.vkUpdateDescriptorSets != nullptr);
            table_.vkUpdateDescriptorSets(value_, descriptorWriteCount, descriptorWrites,
                                          descriptorCopyCount, descriptorCopies);
        }

        constexpr void cmdBindDescriptorSets(
            VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
            VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount,
            const VkDescriptorSet *pDescriptorSets, uint32_t dynamicOffsetCount,
            const uint32_t *pDynamicOffsets) const noexcept
        {
            MCS_ASSERT(table_.vkCmdBindDescriptorSets != nullptr);
            table_.vkCmdBindDescriptorSets(commandBuffer, pipelineBindPoint, layout,
                                           firstSet, descriptorSetCount, pDescriptorSets,
                                           dynamicOffsetCount, pDynamicOffsets);
        }

        constexpr void destroyDescriptorPool(
            VkDescriptorPool descriptorPool,
            const VkAllocationCallbacks *pAllocator) const noexcept
        {
            MCS_ASSERT(table_.vkDestroyDescriptorPool != nullptr);
            table_.vkDestroyDescriptorPool(value_, descriptorPool, pAllocator);
        }

        constexpr void destroyDescriptorSetLayout(
            VkDescriptorSetLayout descriptorSetLayout,
            const VkAllocationCallbacks *pAllocator) const noexcept
        {
            MCS_ASSERT(table_.vkDestroyDescriptorSetLayout != nullptr);
            table_.vkDestroyDescriptorSetLayout(value_, descriptorSetLayout, pAllocator);
        }

        constexpr void freeDescriptorSets(
            VkDescriptorPool descriptorPool, uint32_t descriptorSetCount,
            const VkDescriptorSet *pDescriptorSets) const noexcept
        {
            MCS_ASSERT(table_.vkFreeDescriptorSets != nullptr);
            table_.vkFreeDescriptorSets(value_, descriptorPool, descriptorSetCount,
                                        pDescriptorSets);
        }

        [[nodiscard]] constexpr VkImage createImage(
            const VkImageCreateInfo &createInfo,
            const VkAllocationCallbacks *pAllocator) const
        {
            MCS_ASSERT(table_.vkCreateImage != nullptr);
            VkImage image; // NOLINT
            check_vkresult(table_.vkCreateImage(value_, &createInfo, pAllocator, &image));
            return image;
        }

        [[nodiscard]] constexpr VkMemoryRequirements getImageMemoryRequirements(
            VkImage image) const noexcept
        {
            MCS_ASSERT(table_.vkGetImageMemoryRequirements != nullptr);
            VkMemoryRequirements m;
            table_.vkGetImageMemoryRequirements(value_, image, &m);
            return m;
        }

        constexpr void cmdCopyBufferToImage(
            VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage,
            VkImageLayout dstImageLayout, uint32_t regionCount,
            const VkBufferImageCopy *pRegions) const noexcept
        {
            MCS_ASSERT(table_.vkCmdCopyBufferToImage != nullptr);
            table_.vkCmdCopyBufferToImage(commandBuffer, srcBuffer, dstImage,
                                          dstImageLayout, regionCount, pRegions);
        }

        [[nodiscard]] constexpr VkSampler createSampler(
            const VkSamplerCreateInfo &createInfo,
            const VkAllocationCallbacks *pAllocator) const
        {
            MCS_ASSERT(table_.vkCreateSampler != nullptr);
            VkSampler sampler; // NOLINT
            check_vkresult(
                table_.vkCreateSampler(value_, &createInfo, pAllocator, &sampler));
            return sampler;
        }

        constexpr void destroySampler(
            VkSampler sampler, const VkAllocationCallbacks *pAllocator) const noexcept
        {
            MCS_ASSERT(table_.vkDestroySampler != nullptr);
            table_.vkDestroySampler(value_, sampler, pAllocator);
        }

        constexpr void destroyImage(
            VkImage image, const VkAllocationCallbacks *pAllocator) const noexcept
        {
            MCS_ASSERT(table_.vkDestroyImage != nullptr);
            table_.vkDestroyImage(value_, image, pAllocator);
        }

        constexpr void bindImageMemory(VkImage image, VkDeviceMemory memory,
                                       VkDeviceSize memoryOffset) const noexcept
        {
            MCS_ASSERT(table_.vkBindImageMemory != nullptr);
            table_.vkBindImageMemory(value_, image, memory, memoryOffset);
        }
    };
}; // namespace mcs::vulkan