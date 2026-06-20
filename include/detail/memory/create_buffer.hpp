#pragma once

#include "buffer_base.hpp"
#include "../tool/sType.hpp"
#include "../tool/pNext.hpp"
#include "../tool/Flags.hpp"
#include "memory_allocate_info.hpp"
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
        struct create_info // NOLINTBEGIN
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
            tool::Flags<VkBufferCreateFlagBits> flags;
            VkDeviceSize size;
            VkBufferUsageFlags usage;
            VkSharingMode sharingMode;
            std::vector<uint32_t> queueFamilyIndices;
        }; // NOLINTEND
        static_assert(std::is_default_constructible_v<create_info>);
        constexpr auto &&setCreateInfo(this auto &&self, create_info createInfo) noexcept
        {
            self.createInfo_ = std::move(createInfo);
            return std::forward<decltype(self)>(self);
        }
        using memory_allocate_info = memory_allocate_info;
        static_assert(std::is_default_constructible_v<memory_allocate_info>);
        using GenMemoryAllocateInfo = std::move_only_function<memory_allocate_info(
            VkMemoryRequirements memRequirements,
            VkPhysicalDeviceMemoryProperties memoryProperties)>;
        constexpr auto &&setGenMemoryAllocateInfo(
            this auto &&self, GenMemoryAllocateInfo genMemoryAllocateInfo) noexcept
        {
            self.genMemoryAllocateInfo_ = std::move(genMemoryAllocateInfo);
            return std::forward<decltype(self)>(self);
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
        constexpr create_buffer() noexcept : createInfo_{} {}
        constexpr create_buffer(create_info createInfo,
                                GenMemoryAllocateInfo genMemoryAllocateInfo) noexcept
            : createInfo_{std::move(createInfo)},
              genMemoryAllocateInfo_{std::move(genMemoryAllocateInfo)}
        {
        }

      private:
        create_info createInfo_;
        GenMemoryAllocateInfo genMemoryAllocateInfo_;
    };
}; // namespace mcs::vulkan::memory