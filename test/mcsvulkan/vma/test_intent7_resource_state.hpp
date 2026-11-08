#pragma once
// ============================================================================
// test_intent7_resource_state.hpp
//
// System layer: resource state tracking + explicit barrier planning.
//
// This is deliberately a tracker/planner, not a hidden barrier injector:
//   registerImage(...)     records the current layout/access/stage
//   transitionImage(...)   returns one VkImageMemoryBarrier2 and updates state
//   BarrierPlanner::flush  emits the collected barriers through a sink
//
// The sink is FrameBuilder (or any object with barrier(VkDependencyInfo)).
// No barrier is inserted behind the pass layer's back.
// ============================================================================

#include "../head.hpp"

#include <cstdint>
#include <vector>

namespace pass_data
{

struct ImageState
{
    VkImage image{};
    VkImageAspectFlags aspect{VK_IMAGE_ASPECT_COLOR_BIT};
    VkImageLayout layout{VK_IMAGE_LAYOUT_UNDEFINED};
    VkAccessFlags2 access{0};
    VkPipelineStageFlags2 stage{VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT};
    std::uint32_t baseMip{0};
    std::uint32_t levelCount{1};
    std::uint32_t baseLayer{0};
    std::uint32_t layerCount{1};
    std::uint32_t queueFamilyIndex{VK_QUEUE_FAMILY_IGNORED};
};

struct BufferState
{
    VkBuffer buffer{};
    VkAccessFlags2 access{0};
    VkPipelineStageFlags2 stage{VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT};
    std::uint32_t queueFamilyIndex{VK_QUEUE_FAMILY_IGNORED};
};

class ResourceStateTracker
{
  public:
    void registerImage(const ImageState &state)
    {
        for (auto &existing : images_)
        {
            if (existing.image == state.image && existing.aspect == state.aspect)
            {
                existing = state;
                return;
            }
        }
        images_.push_back(state);
    }

    [[nodiscard]] VkImageMemoryBarrier2 transitionImage(
        VkImage image, VkImageAspectFlags aspect, VkImageLayout newLayout,
        VkAccessFlags2 dstAccess, VkPipelineStageFlags2 dstStage,
        std::uint32_t srcQueueFamily = VK_QUEUE_FAMILY_IGNORED,
        std::uint32_t dstQueueFamily = VK_QUEUE_FAMILY_IGNORED)
    {
        ImageState *state = find(image, aspect);
        if (state == nullptr)
        {
            registerImage({.image = image,
                           .aspect = aspect,
                           .layout = VK_IMAGE_LAYOUT_UNDEFINED,
                           .access = 0,
                           .stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT});
            state = find(image, aspect);
        }

        const VkImageMemoryBarrier2 barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = state->stage,
            .srcAccessMask = state->access,
            .dstStageMask = dstStage,
            .dstAccessMask = dstAccess,
            .oldLayout = state->layout,
            .newLayout = newLayout,
            .srcQueueFamilyIndex = srcQueueFamily,
            .dstQueueFamilyIndex = dstQueueFamily,
            .image = image,
            .subresourceRange = {.aspectMask = aspect,
                                 .baseMipLevel = state->baseMip,
                                 .levelCount = state->levelCount,
                                 .baseArrayLayer = state->baseLayer,
                                 .layerCount = state->layerCount}};

        state->layout = newLayout;
        state->access = dstAccess;
        state->stage = dstStage;
        return barrier;
    }

    [[nodiscard]] const ImageState *find(VkImage image,
                                         VkImageAspectFlags aspect) const noexcept
    {
        for (const auto &state : images_)
            if (state.image == image && state.aspect == aspect)
                return &state;
        return nullptr;
    }

    void registerBuffer(const BufferState &state)
    {
        for (auto &existing : buffers_)
        {
            if (existing.buffer == state.buffer)
            {
                existing = state;
                return;
            }
        }
        buffers_.push_back(state);
    }

    [[nodiscard]] VkBufferMemoryBarrier2 transitionBuffer(
        VkBuffer buffer, VkAccessFlags2 dstAccess,
        VkPipelineStageFlags2 dstStage,
        std::uint32_t srcQueueFamily = VK_QUEUE_FAMILY_IGNORED,
        std::uint32_t dstQueueFamily = VK_QUEUE_FAMILY_IGNORED)
    {
        BufferState *state = findBuffer(buffer);
        if (state == nullptr)
        {
            registerBuffer({.buffer = buffer,
                            .access = 0,
                            .stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT});
            state = findBuffer(buffer);
        }

        const VkBufferMemoryBarrier2 barrier{
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = state->stage,
            .srcAccessMask = state->access,
            .dstStageMask = dstStage,
            .dstAccessMask = dstAccess,
            .srcQueueFamilyIndex = srcQueueFamily,
            .dstQueueFamilyIndex = dstQueueFamily,
            .buffer = buffer,
            .offset = 0,
            .size = VK_WHOLE_SIZE};

        state->access = dstAccess;
        state->stage = dstStage;
        if (dstQueueFamily != VK_QUEUE_FAMILY_IGNORED)
            state->queueFamilyIndex = dstQueueFamily;
        return barrier;
    }

  private:
    ImageState *find(VkImage image, VkImageAspectFlags aspect) noexcept
    {
        for (auto &state : images_)
            if (state.image == image && state.aspect == aspect)
                return &state;
        return nullptr;
    }

    BufferState *findBuffer(VkBuffer buffer) noexcept
    {
        for (auto &state : buffers_)
            if (state.buffer == buffer)
                return &state;
        return nullptr;
    }

    std::vector<ImageState> images_{};
    std::vector<BufferState> buffers_{};
};

class BarrierPlanner
{
  public:
    void add(const VkImageMemoryBarrier2 &barrier)
    {
        imageBarriers_.push_back(barrier);
    }

    void addBuffer(const VkBufferMemoryBarrier2 &barrier)
    {
        bufferBarriers_.push_back(barrier);
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return imageBarriers_.empty() && bufferBarriers_.empty();
    }

    // The sink only needs barrier(const VkDependencyInfo&), which is the same
    // explicit entry point FrameBuilder exposes.
    template <typename Sink> void flush(Sink &sink)
    {
        if (empty())
            return;
        const VkDependencyInfo dependency{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = nullptr,
            .dependencyFlags = 0,
            .bufferMemoryBarrierCount =
                static_cast<std::uint32_t>(bufferBarriers_.size()),
            .pBufferMemoryBarriers = bufferBarriers_.data(),
            .imageMemoryBarrierCount =
                static_cast<std::uint32_t>(imageBarriers_.size()),
            .pImageMemoryBarriers = imageBarriers_.data()};
        sink.barrier(dependency);
    }

  private:
    std::vector<VkBufferMemoryBarrier2> bufferBarriers_{};
    std::vector<VkImageMemoryBarrier2> imageBarriers_{};
};

} // namespace pass_data
