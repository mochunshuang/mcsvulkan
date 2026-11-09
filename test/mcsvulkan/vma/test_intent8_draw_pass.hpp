#pragma once
// ============================================================================
// test_intent8_draw_pass.hpp
//
// Pass = RenderTarget + sequence<DrawCall ops>.
//
// This header is the grouping layer above the draw pipeline:
//   PipelineState   one pass's base pipeline + dynamic state + push mesh
//   DrawPass        target + per-pass resource slots + pre/post barriers
//   PassManager     reuses per-frame pass slots (GPU buffers are not rebuilt)
//   PassRecord      a non-owning view used by the frame recorder
//
// The pass layer does not define a second command model. It only groups
// DrawCalls under one RenderTarget.
// ============================================================================

#include "test_intent8_draw_pipeline.hpp"
#include "test_intent8_draw_recorder.hpp"
#include "test_intent8_frame_context.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace intent8
{

struct PipelineState
{
    const DrawPipeline *pipeline{nullptr};
    VkDescriptorSet set{};
    VkViewport viewport{};
    VkRect2D scissor{};
    PushData pushData{};
    mesh_data mesh{};
};

struct PassBarriers
{
    inline static constexpr std::uint32_t CAPACITY = 4;

    std::array<VkImageMemoryBarrier2, CAPACITY> items{};
    std::uint32_t count{0};

    void add(const VkImageMemoryBarrier2 &barrier) noexcept
    {
        assert(count < CAPACITY);
        items[count++] = barrier;
    }

    void clear() noexcept { count = 0; }
    [[nodiscard]] std::span<const VkImageMemoryBarrier2> view() const noexcept
    {
        return {items.data(), count};
    }
};

struct DrawPass
{
    RenderTarget target{};
    PipelineState base{};
    shader_data::FrameContext frame;

    PassBarriers preBarriers{};
    PassBarriers postBarriers{};

    // When true the pass body lives in `secondary`; the primary target only
    // receives vkCmdExecuteCommands.
    bool executeSecondary{false};
    VkCommandBuffer secondary{};

    DrawPass(const LogicalDevice &device, VkDeviceSize vertexCapacity,
             VkDeviceSize indexCapacity, VkDeviceSize heapCapacity,
             VkDeviceSize indirectDrawCapacity, VkDeviceSize commandConstantsCapacity,
             RenderTarget target, PipelineState base)
        : target{std::move(target)}, base{base},
          frame{device, vertexCapacity, indexCapacity, heapCapacity,
                indirectDrawCapacity, commandConstantsCapacity}
    {
        reset(this->target, this->base);
    }

    DrawPass(const DrawPass &) = delete;
    DrawPass &operator=(const DrawPass &) = delete;

    DrawPass(DrawPass &&other) noexcept
        : target{std::move(other.target)}, base{other.base},
          frame{std::move(other.frame)}, preBarriers{other.preBarriers},
          postBarriers{other.postBarriers}, executeSecondary{other.executeSecondary},
          secondary{other.secondary}
    {
        syncAddresses();
    }

    DrawPass &operator=(DrawPass &&) = delete;

    // Reuse the slot: reset builder cursors/segments, keep GPU allocations.
    void reset(RenderTarget newTarget, PipelineState newBase)
    {
        target = std::move(newTarget);
        base = newBase;
        preBarriers.clear();
        postBarriers.clear();
        executeSecondary = false;
        secondary = VK_NULL_HANDLE;
        syncAddresses();
        frame.builder.begin({.pipeline = base.pipeline,
                             .pushData = base.pushData,
                             .mesh = base.mesh,
                             .viewport = base.viewport,
                             .scissor = base.scissor});
    }

    void end() { frame.builder.end(); }

    [[nodiscard]] shader_data::CommandStream stream() const noexcept
    {
        return frame.builder.stream();
    }

    [[nodiscard]] const shader_data::ShaderBuffers &buffers() const noexcept
    {
        return frame.buffers;
    }

    [[nodiscard]] std::size_t command_count() const noexcept
    {
        const auto s = frame.builder.stream();
        return s.segments == nullptr ? 0 : s.segments->size();
    }

    void registerBuffers(ResourceTable &table, std::uint64_t idBase) const noexcept
    {
        table.addBuffer(idBase + 0, buffers().globalVertexBuffer.buffer.buffer());
        table.addBuffer(idBase + 1, buffers().globalIndexBuffer.buffer.buffer());
        table.addBuffer(idBase + 2, buffers().globalHeapBuffer.buffer.buffer());
        table.addBuffer(idBase + 3, buffers().indirectDrawBuffer.buffer.buffer());
        table.addBuffer(idBase + 4,
                        buffers().commandConstantsBuffer.buffer.buffer());
    }

  private:
    void syncAddresses() noexcept
    {
        base.pushData.vertexAddress = frame.buffers.globalVertexBuffer.address;
        base.pushData.instanceAddress = frame.buffers.globalHeapBuffer.address;
        base.pushData.commandConstantsAddress =
            frame.buffers.commandConstantsBuffer.address;
    }
};

class PassManager
{
  public:
    PassManager(const LogicalDevice &device, VkDeviceSize vertexCapacity,
                VkDeviceSize indexCapacity, VkDeviceSize heapCapacity,
                VkDeviceSize indirectDrawCapacity, VkDeviceSize commandConstantsCapacity)
        : device_{device}, vertexCapacity_{vertexCapacity},
          indexCapacity_{indexCapacity}, heapCapacity_{heapCapacity},
          indirectDrawCapacity_{indirectDrawCapacity},
          commandConstantsCapacity_{commandConstantsCapacity}
    {
        passes_.reserve(8);
    }

    DrawPass &beginPass(RenderTarget target, PipelineState base)
    {
        if (count_ == passes_.size())
        {
            passes_.emplace_back(device_, vertexCapacity_, indexCapacity_, heapCapacity_,
                                 indirectDrawCapacity_, commandConstantsCapacity_,
                                 std::move(target), base);
        }
        else
        {
            passes_[count_].reset(std::move(target), base);
        }
        return passes_[count_++];
    }

    void endPass()
    {
        if (count_ != 0)
            passes_[count_ - 1].end();
    }

    void reset() { count_ = 0; }

    [[nodiscard]] std::span<const DrawPass> passes() const noexcept
    {
        return {passes_.data(), count_};
    }

    [[nodiscard]] bool empty() const noexcept { return count_ == 0; }
    [[nodiscard]] std::size_t size() const noexcept { return count_; }

  private:
    const LogicalDevice &device_;
    VkDeviceSize vertexCapacity_{};
    VkDeviceSize indexCapacity_{};
    VkDeviceSize heapCapacity_{};
    VkDeviceSize indirectDrawCapacity_{};
    VkDeviceSize commandConstantsCapacity_{};
    std::vector<DrawPass> passes_{};
    std::size_t count_{0};
};

// A non-owning view of one pass for the recording loop.
struct PassRecord
{
    const RenderTarget *target{nullptr};
    const shader_data::ShaderBuffers *buffers{nullptr};
    shader_data::CommandStream stream{};
    VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
    std::span<const VkImageMemoryBarrier2> preBarriers{};
    std::span<const VkImageMemoryBarrier2> postBarriers{};
    bool executeSecondary{false};
    VkCommandBuffer secondary{VK_NULL_HANDLE};
};

inline void recordPass(FrameRecorder &recorder, const PassRecord &pass)
{
    assert(pass.target != nullptr);
    recorder.barrier(pass.preBarriers);
    if (pass.executeSecondary)
    {
        RenderTarget target = *pass.target;
        target.config.flags |= VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT;
        recorder.beginPass(target);
        const VkCommandBuffer one[1]{pass.secondary};
        recorder.executeCommands(one);
        recorder.endPass();
    }
    else
    {
        assert(pass.buffers != nullptr);
        recorder.beginPass(*pass.target);
        shader_data::emit(recorder, *pass.buffers, pass.stream, pass.descriptorSet);
        recorder.endPass();
    }
    recorder.barrier(pass.postBarriers);
}

inline void recordPasses(FrameRecorder &recorder, std::span<const PassRecord> passes)
{
    for (std::size_t i = 0; i < passes.size(); ++i)
    {
        if (i != 0)
            recorder.barrier(pass_boundary_barrier());
        recordPass(recorder, passes[i]);
    }
}

} // namespace intent8
