#pragma once

#include "buffer_base.hpp"
#include "../tool/sType.hpp"
#include "../tool/pNext.hpp"
#include "../tool/Flags.hpp"
#include <functional>
#include <vector>

namespace mcs::vulkan::memory
{
    struct create_buffer
    {

        /*
        typedef struct VkBufferCreateInfo {
            VkStructureType        sType;
            const void*            pNext;
            VkBufferCreateFlags    flags;
            VkDeviceSize           size;
            VkBufferUsageFlags     usage;
            VkSharingMode          sharingMode;
            uint32_t               queueFamilyIndexCount;
            const uint32_t*        pQueueFamilyIndices;
        } VkBufferCreateInfo;
        */
        struct create_info
        {
            constexpr VkBufferCreateInfo operator()() const noexcept
            {
                using tool::sType;
                return VkBufferCreateInfo{
                    .sType = sType<VkBufferCreateInfo>(),
                    .pNext = pNext.value(),
                    .flags = flags,
                    .size = size,
                    .usage = usage,
                    .sharingMode = sharingMode,
                    .queueFamilyIndexCount =
                        static_cast<uint32_t>(queueFamilyIndices.size()),
                    .pQueueFamilyIndices = queueFamilyIndices.data()};
            }
            tool::pNext pNext;
            VkBufferCreateFlags flags;
            VkDeviceSize size;
            VkBufferUsageFlags usage;
            VkSharingMode sharingMode;
            std::vector<uint32_t> queueFamilyIndices;
        };
        static_assert(std::is_default_constructible_v<create_info>);
        auto &&setCreateInfo(this auto &&self, create_info createInfo)
        {
            self.createInfo_ = std::move(createInfo);
            return std::forward<decltype(self)>(self);
        }

        /*
        typedef struct VkMemoryAllocateInfo {
            VkStructureType    sType;
            const void*        pNext;
            VkDeviceSize       allocationSize;
            uint32_t           memoryTypeIndex;
        } VkMemoryAllocateInfo;
        */
        struct memory_allocate_info
        {
            constexpr VkMemoryAllocateInfo operator()() const noexcept
            {
                using tool::sType;
                return {.sType = sType<VkMemoryAllocateInfo>(),
                        .pNext = pNext.value(),
                        .allocationSize = allocationSize,
                        .memoryTypeIndex = memoryTypeIndex};
            }
            tool::pNext pNext;
            VkDeviceSize allocationSize;
            uint32_t memoryTypeIndex;
        };
        static_assert(std::is_default_constructible_v<memory_allocate_info>);
        using GenMemoryAllocateInfo = std::move_only_function<memory_allocate_info(
            VkMemoryRequirements memRequirements,
            VkPhysicalDeviceMemoryProperties memoryProperties)>;
        auto &&setGenMemoryAllocateInfo(this auto &&self,
                                        GenMemoryAllocateInfo genMemoryAllocateInfo)
        {
            self.genMemoryAllocateInfo_ = std::move(genMemoryAllocateInfo);
            return std::forward<decltype(self)>(self);
        }

        [[nodiscard]] static constexpr auto findMemoryTypeIndex(
            uint32_t typeFilter, // NOLINTNEXTLINE
            VkPhysicalDeviceMemoryProperties memProperties,
            VkMemoryPropertyFlags properties)
        {
            for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
            {
                if (((typeFilter & (1 << i)) != 0U) && // NOLINTNEXTLINE
                    ((memProperties.memoryTypes[i].propertyFlags) & properties) ==
                        properties)
                    return i;
            }
            throw std::runtime_error("failed to find suitable memory type!");
        }

        constexpr auto build(LogicalDevice &device, VkDeviceSize memoryOffset = 0)
        {
            VkBuffer buffer = nullptr;
            VkDeviceMemory bufferMemory = nullptr;
            try
            {
                buffer = device.createBuffer(createInfo_(), device.allocator());
                memory_allocate_info genAllocateInfo = genMemoryAllocateInfo_(
                    device.getBufferMemoryRequirements(buffer),
                    device.physicalDevice()->getMemoryProperties());
                bufferMemory =
                    device.allocateMemory(genAllocateInfo(), device.allocator());

                device.bindBufferMemory(buffer, bufferMemory, memoryOffset);
                return buffer_base{device, buffer, bufferMemory};
            }
            catch (...)
            {
                if (bufferMemory != nullptr)
                    device.freeMemory(bufferMemory, device.allocator());
                if (buffer != nullptr)
                    device.destroyBuffer(buffer, device.allocator());
                throw;
            }
        }

      private:
        create_info createInfo_;
        GenMemoryAllocateInfo genMemoryAllocateInfo_;
    };
}; // namespace mcs::vulkan::memory