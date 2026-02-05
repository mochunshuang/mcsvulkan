#pragma once

#include "../LogicalDevice.hpp"
#include "sType.hpp"
#include <array>

namespace mcs::vulkan::tool
{
    template <size_t MAX_FRAMES_IN_FLIGHT>
    struct frame_context
    {
        // NOLINTBEGIN
        const LogicalDevice *device_{};
        std::vector<VkSemaphore> presentCompleteSemaphore;
        std::vector<VkSemaphore> renderFinishedSemaphore;
        std::array<VkFence, MAX_FRAMES_IN_FLIGHT> inFlightFences{};
        uint32_t semaphoreIndex = 0;
        uint32_t currentFrame = 0;
        // NOLINTEND

        explicit frame_context(const LogicalDevice &device, size_t swapChainImagesSize)
            : device_{&device}
        {
            presentCompleteSemaphore.resize(swapChainImagesSize);
            renderFinishedSemaphore.resize(swapChainImagesSize);
            // inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
            createSyncObjects(device);
        }
        ~frame_context() noexcept
        {
            destroy();
        }
        frame_context(const frame_context &) = delete;
        frame_context(frame_context &&) = delete;
        frame_context &operator=(const frame_context &) = delete;
        frame_context &operator=(frame_context &&) = delete;

      private:
        void createSyncObjects(const LogicalDevice &device)
        {
            using tool::sType;

            const auto SWAP_CHAIN_IMAGES_SIZE = presentCompleteSemaphore.size();
            // const auto MAX_FRAMES_IN_FLIGHT = inFlightFences.size();

            constexpr VkSemaphoreCreateInfo SEMAPHORE_INFO = {
                .sType = sType<VkSemaphoreCreateInfo>()};
            for (size_t i = 0; i < SWAP_CHAIN_IMAGES_SIZE; i++)
            {
                presentCompleteSemaphore[i] =
                    device.createSemaphore(SEMAPHORE_INFO, device.allocator());
                renderFinishedSemaphore[i] =
                    device.createSemaphore(SEMAPHORE_INFO, device.allocator());
            }

            constexpr VkFenceCreateInfo FENCE_INFO = {.sType = sType<VkFenceCreateInfo>(),
                                                      .flags =
                                                          VK_FENCE_CREATE_SIGNALED_BIT};
            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
                inFlightFences[i] = device.createFence(FENCE_INFO, device.allocator());
            }
        }

        constexpr void destroySyncObject() noexcept
        {
            for (auto *semaphore : presentCompleteSemaphore)
                device_->destroySemaphore(semaphore, device_->allocator());
            presentCompleteSemaphore.clear();

            for (auto *semaphore : renderFinishedSemaphore)
                device_->destroySemaphore(semaphore, device_->allocator());
            renderFinishedSemaphore.clear();

            for (auto *fence : inFlightFences)
                device_->destroyFence(fence, device_->allocator());
            inFlightFences = {};
        }

        constexpr void destroy() noexcept
        {
            if (device_ != nullptr)
            {
                destroySyncObject();
                device_ = nullptr;
            }
        }
    };
}; // namespace mcs::vulkan::tool