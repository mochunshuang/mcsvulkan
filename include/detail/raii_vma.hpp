#pragma once

#include "__vulkan.hpp"
#include "./utils/make_vk_exception.hpp"

namespace mcs::vulkan
{
    struct raii_vma
    {
        raii_vma(const raii_vma &) = delete;
        raii_vma(raii_vma &&) = delete;
        raii_vma &operator=(const raii_vma &) = delete;
        raii_vma &operator=(raii_vma &&) = delete;

        struct create_info // NOLINTBEGIN
        {
            VmaAllocatorCreateInfo operator()() const noexcept
            {
                return {
                    .flags = flags,
                    .physicalDevice = physicalDevice,
                    .device = device,
                    .instance = instance,
                    .vulkanApiVersion = vulkanApiVersion,
                };
            }
            VmaAllocatorCreateFlags flags;
            VkPhysicalDevice physicalDevice;
            VkDevice device;
            VkInstance instance;
            uint32_t vulkanApiVersion;
        }; // NOLINTEND

        constexpr explicit raii_vma(create_info config)
        {
            VmaAllocatorCreateInfo allocatorCreateInfo = config();
            VkResult ret =
                ::vmaImportVulkanFunctionsFromVolk(&allocatorCreateInfo, &vkFunctions_);
            if (ret != VK_SUCCESS)
                throw make_vk_exception("vmaImportVulkanFunctionsFromVolk error.");
            allocatorCreateInfo.pVulkanFunctions = &vkFunctions_;
            ret = ::vmaCreateAllocator(&allocatorCreateInfo, &allocator_);
            if (ret != VK_SUCCESS)
            {
                allocator_ = {};
                throw make_vk_exception("vmaCreateAllocator error.");
            }
        }
        constexpr void destroy() noexcept
        {
            if (allocator_ != nullptr)
            {
                ::vmaDestroyAllocator(allocator_);
                allocator_ = nullptr;
            }
        }
        constexpr ~raii_vma() noexcept
        {
            destroy();
        }

        [[nodiscard]] VmaAllocator allocator() const noexcept
        {
            return allocator_;
        }

      private:
        VmaVulkanFunctions vkFunctions_{};
        VmaAllocator allocator_{};
    };

}; // namespace mcs::vulkan
