#pragma once

#include <type_traits>
#include <exception>

#include "../__vulkan.hpp"

namespace mcs::vulkan::tool
{
    template <typename T>
    static consteval auto sType() noexcept // NOLINT
    {
        if constexpr (std::is_same_v<T, VkApplicationInfo>)
            return VK_STRUCTURE_TYPE_APPLICATION_INFO;
        else if constexpr (std::is_same_v<T, VkInstanceCreateInfo>)
            return VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        else if constexpr (std::is_same_v<T, VkDeviceCreateInfo>)
            return VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        else if constexpr (std::is_same_v<T, VkDeviceQueueCreateInfo>)
            return VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        else if constexpr (std::is_same_v<T, VkSwapchainCreateInfoKHR>)
            return VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        else if constexpr (std::is_same_v<T, VkImageCreateInfo>)
            return VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        else if constexpr (std::is_same_v<T, VkImageViewCreateInfo>)
            return VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        else if constexpr (std::is_same_v<T, VkBufferCreateInfo>)
            return VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        else if constexpr (std::is_same_v<T, VkMemoryAllocateInfo>)
            return VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        else if constexpr (std::is_same_v<T, VkCommandBufferAllocateInfo>)
            return VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        else if constexpr (std::is_same_v<T, VkCommandPoolCreateInfo>)
            return VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        else if constexpr (std::is_same_v<T, VkRenderPassCreateInfo>)
            return VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        else if constexpr (std::is_same_v<T, VkFramebufferCreateInfo>)
            return VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        else if constexpr (std::is_same_v<T, VkPipelineLayoutCreateInfo>)
            return VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        else if constexpr (std::is_same_v<T, VkGraphicsPipelineCreateInfo>)
            return VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        else if constexpr (std::is_same_v<T, VkShaderModuleCreateInfo>)
            return VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        else if constexpr (std::is_same_v<T, VkSemaphoreCreateInfo>)
            return VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        else if constexpr (std::is_same_v<T, VkFenceCreateInfo>)
            return VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        else if constexpr (std::is_same_v<T, VkSubmitInfo>)
            return VK_STRUCTURE_TYPE_SUBMIT_INFO;
        else if constexpr (std::is_same_v<T, VkPresentInfoKHR>)
            return VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        else if constexpr (std::is_same_v<T, VkDebugUtilsMessengerCreateInfoEXT>)
            return VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        else if constexpr (std::is_same_v<T, VkPhysicalDeviceFeatures2>)
            return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        else if constexpr (std::is_same_v<T, VkSamplerCreateInfo>)
            return VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        else if constexpr (std::is_same_v<T, VkDescriptorSetLayoutCreateInfo>)
            return VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        else if constexpr (std::is_same_v<T, VkPipelineCacheCreateInfo>)
            return VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        else if (std::is_same_v<VkPhysicalDeviceVulkan11Features, T>)
            return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        else if constexpr (std::is_same_v<T, VkPhysicalDeviceVulkan13Features>)
            return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        else if constexpr (std::is_same_v<
                               T, VkPhysicalDeviceExtendedDynamicStateFeaturesEXT>)
            return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
        else if constexpr (std::is_same_v<T, VkPipelineShaderStageCreateInfo>)
            return VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        else if constexpr (std::is_same_v<T, VkPipelineVertexInputStateCreateInfo>)
            return VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        else if constexpr (std::is_same_v<T, VkPipelineInputAssemblyStateCreateInfo>)
            return VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        else if constexpr (std::is_same_v<T, VkPipelineViewportStateCreateInfo>)
            return VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        else if constexpr (std::is_same_v<T, VkPipelineRasterizationStateCreateInfo>)
            return VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        else if constexpr (std::is_same_v<T, VkPipelineMultisampleStateCreateInfo>)
            return VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        else if constexpr (std::is_same_v<T, VkPipelineColorBlendStateCreateInfo>)
            return VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        else if constexpr (std::is_same_v<T, VkPipelineDynamicStateCreateInfo>)
            return VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        else if constexpr (std::is_same_v<T, VkPipelineRenderingCreateInfo>)
            return VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        else if constexpr (std::is_same_v<T, VkCommandBufferBeginInfo>)
            return VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        else if constexpr (std::is_same_v<T, VkRenderingInfo>)
            return VK_STRUCTURE_TYPE_RENDERING_INFO;
        else if constexpr (std::is_same_v<T, VkRenderingAttachmentInfo>)
            return VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        else if constexpr (std::is_same_v<T, VkImageMemoryBarrier>)
            return VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        else if constexpr (std::is_same_v<T, VkDescriptorPoolCreateInfo>)
            return VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        else if constexpr (std::is_same_v<T, VkDescriptorSetAllocateInfo>)
            return VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        else if constexpr (std::is_same_v<T, VkPipelineDepthStencilStateCreateInfo>)
            return VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        else if constexpr (std::is_same_v<T, VkWriteDescriptorSet>)
            return VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        else if constexpr (std::is_same_v<T, VkImageMemoryBarrier2>)
            return VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        else if constexpr (std::is_same_v<T, VkDependencyInfo>)
            return VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        else if constexpr (std::is_same_v<T, VkSemaphoreSubmitInfo>)
            return VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        else if constexpr (std::is_same_v<T, VkCommandBufferSubmitInfo>)
            return VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        else if constexpr (std::is_same_v<T, VkSubmitInfo2>)
            return VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        else if constexpr (std::is_same_v<T, VkSemaphoreTypeCreateInfo>)
            return VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        else if constexpr (std::is_same_v<T, VkSemaphoreWaitInfo>)
            return VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        else if constexpr (std::is_same_v<T, VkPhysicalDeviceVulkan12Features>)
            return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        else if constexpr (std::is_same_v<
                               T, VkPhysicalDeviceExtendedDynamicState2FeaturesEXT>)
            return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT;
        else if constexpr (std::is_same_v<
                               T, VkPhysicalDeviceExtendedDynamicState3FeaturesEXT>)
            return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT;
        else if constexpr (std::is_same_v<T, VkPhysicalDeviceProperties2>)
            return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        else if constexpr (std::is_same_v<
                               T, VkPhysicalDeviceExtendedDynamicState3PropertiesEXT>)
            return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_PROPERTIES_EXT;
        else if constexpr (std::is_same_v<T, VkPhysicalDeviceVulkan12Properties>)
            return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;
        else if constexpr (std::is_same_v<T, VkBufferDeviceAddressInfo>)
            return VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        else if constexpr (std::is_same_v<T, VkMemoryAllocateFlagsInfo>)
            return VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        else if constexpr (
            std::is_same_v<T,
                           VkPhysicalDeviceDynamicRenderingUnusedAttachmentsFeaturesEXT>)
            return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_UNUSED_ATTACHMENTS_FEATURES_EXT;
        else if constexpr (std::is_same_v<T, VkPipelineTessellationStateCreateInfo>)
            return VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
        else
            // static_assert(false, "Unknown Vulkan structure type");
            std::terminate();
    }

}; // namespace mcs::vulkan::tool
