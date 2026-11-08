#pragma once
// ============================================================================
// test_intent7_frame_scheduler.hpp
//
// System layer: queue roles + acquire/submit/present lifecycle.
//
// The pass layer records command lists; this layer decides which queue role
// owns the frame and performs the explicit Vulkan queue operations. It is a
// thin, explicit coordinator, not a hidden scheduler:
//
//   acquire  -> vkAcquireNextImageKHR
//   submit   -> vkQueueSubmit
//   present  -> vkQueuePresentKHR
//
// Compute/transfer queue roles are represented by QueueBinding so the same
// scheduler can grow into multi-queue submission without changing PassBuilder.
// ============================================================================

#include "../head.hpp"

#include <cstdint>

namespace pass_data
{

enum class QueueRole : std::uint8_t
{
    Graphics,
    Compute,
    Transfer,
    Present,
    Count,
};

struct QueueBinding
{
    QueueRole role{QueueRole::Graphics};
    std::uint32_t familyIndex{0};
    std::uint32_t queueIndex{0};
    mcs::vulkan::Queue *queue{nullptr};
};

class FrameScheduler
{
  public:
    FrameScheduler(QueueBinding graphics, QueueBinding present) noexcept
        : graphics_{graphics}, present_{present}
    {
    }

    // Acquire through the device function table, not a global loader symbol.
    VkResult acquire(const mcs::vulkan::LogicalDevice &device,
                     VkSwapchainKHR swapchain, VkSemaphore semaphore,
                     std::uint32_t &imageIndex) const noexcept
    {
        return device.table().vkAcquireNextImageKHR(
            *device, swapchain, UINT64_MAX, semaphore, VK_NULL_HANDLE,
            &imageIndex);
    }

    void submitGraphics(const VkSubmitInfo &submit, VkFence fence) const
    {
        if (graphics_.queue == nullptr)
            return;
        graphics_.queue->submit(1, submit, fence);
    }

    VkResult present(const VkPresentInfoKHR &presentInfo) const
    {
        if (present_.queue == nullptr)
            return VK_ERROR_INITIALIZATION_FAILED;
        return present_.queue->presentKHR(presentInfo);
    }

    void advanceFrame() noexcept { ++frameNumber_; }
    [[nodiscard]] std::uint64_t frameNumber() const noexcept
    {
        return frameNumber_;
    }

    [[nodiscard]] const QueueBinding &graphicsBinding() const noexcept
    {
        return graphics_;
    }
    [[nodiscard]] const QueueBinding &presentBinding() const noexcept
    {
        return present_;
    }

  private:
    QueueBinding graphics_{};
    QueueBinding present_{};
    std::uint64_t frameNumber_{0};
};

} // namespace pass_data
