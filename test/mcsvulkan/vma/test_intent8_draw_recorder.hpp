#pragma once
// ============================================================================
// test_intent8_draw_recorder.hpp
//
// The command recording layer:
//
//   DrawCall (pipeline data + resources + draw operands)
//     -> OpStream (bind pipeline / bind descriptor set / push constants /
//                  bind index / bind vertex / draw)
//     -> VkEmitter (explicit vkCmd*)
//     -> c_api_list (the same op list)
//
// Passes are grouping: beginPass(target) ... endPass(). The recorder does not
// reorder or inject anything; barriers are explicit ops.
// ============================================================================

#include "test_intent8_draw_emitter.hpp"
#include "test_intent8_draw_validator.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace intent8
{

// Memory barrier between adjacent passes: the previous pass stores the
// attachments, the next one loads them. Layouts do not change.
inline VkMemoryBarrier2 pass_boundary_barrier() noexcept
{
    return VkMemoryBarrier2{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .pNext = nullptr,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                         VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                         VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    };
}

class FrameRecorder
{
  public:
    FrameRecorder(mcs::vulkan::CommandBufferView cmd, const DrawContext &context,
                  bool deferEmit = false)
        : emitter_{cmd}, context_{context}, deferred_{deferEmit}
    {
        stream_.reserve(64);
    }

    FrameRecorder(const FrameRecorder &) = delete;
    FrameRecorder &operator=(const FrameRecorder &) = delete;

    // ---- pass boundary ---------------------------------------------------
    void beginPass(const RenderTarget &target)
    {
        lastTarget_ = target;
        if (!validator_.beginTarget(target))
            valid_ = false;
        Op op{};
        op.code = OpCode::BeginTarget;
        op.passIndex = passIndex_;
        op.payload = BeginTargetPayload{.target = target};
        push(std::move(op));
        activeTarget_ = true;
        inherited_ = false;
    }

    // Secondary command buffers with VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE
    // inherit the target; they must not call BeginRendering themselves.
    void beginInheritedPass(const RenderTarget &target)
    {
        lastTarget_ = target;
        if (!validator_.beginTargetInherited(target))
            valid_ = false;
        activeTarget_ = true;
        inherited_ = true;
    }

    // Classic render pass only: advance to the next subpass.
    void nextSubpass(VkSubpassContents contents = VK_SUBPASS_CONTENTS_INLINE)
    {
        assert(activeTarget_ && !inherited_ &&
               lastTarget_.config.kind == TargetKind::ClassicRenderPass);
        Op op{};
        op.code = OpCode::NextSubpass;
        op.passIndex = passIndex_;
        op.payload = NextSubpassPayload{.contents = contents};
        push(std::move(op));
    }

    void endPass()
    {
        if (!activeTarget_)
            return;
        if (!inherited_)
        {
            Op op{};
            op.code = OpCode::EndTarget;
            op.passIndex = passIndex_;
            op.payload = EndTargetPayload{.target = lastTarget_};
            push(std::move(op));
        }
        validator_.endTarget();
        activeTarget_ = false;
        inherited_ = false;
        ++passIndex_;
        passCount_ = passIndex_;
    }

    // ---- draw ------------------------------------------------------------
    // DrawCall is the operation group from the blueprint:
    //   bind pipeline
    //   bind descriptor set
    //   push constants
    //   bind index / vertex / indirect
    //   draw
    void draw(const DrawCall &call)
    {
        if (!validateDraw(call))
            return;

        // Complete the logical view at draw time: the active RenderTarget is
        // the one part of DrawPipelineData the UI segment does not know.
        DrawPipelineData resolved = *call.pipelineData;
        if (resolved.target == nullptr)
            resolved.target = &lastTarget_;

        DrawArgs args = call.args;
        const DrawPipeline &pipeline = *resolved.pipeline;
        const DrawResources &res = *resolved.resources;
        if (args.kind != DrawKind::Draw && args.kind != DrawKind::DrawIndexed)
        {
            if (args.indirectBuffer == VK_NULL_HANDLE)
                args.indirectBuffer = res.indirectBuffer;
            if (args.indirectOffset == 0)
                args.indirectOffset = res.indirectOffset;
            if (args.drawCount == 0)
                args.drawCount = res.drawCount;
            if (args.stride == 0)
                args.stride = res.stride;
        }

        // 1. bind pipeline
        {
            Op op{};
            op.code = OpCode::BindPipeline;
            op.passIndex = passIndex_;
            op.payload = BindPipelinePayload{
                .pipeline = &pipeline, .bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS};
            push(std::move(op));
        }

        // 2. bind descriptor set
        {
            Op op{};
            op.code = OpCode::BindDescriptorSet;
            op.passIndex = passIndex_;
            op.payload = BindDescriptorSetPayload{
                .layout = pipeline.layout(),
                .set = res.descriptors.set,
                .firstSet = 0,
            };
            push(std::move(op));
        }

        // 3. dynamic viewport / scissor state
        if (res.dynamicViewport)
        {
            Op op{};
            op.code = OpCode::SetViewport;
            op.passIndex = passIndex_;
            op.payload = SetViewportPayload{.viewport = res.viewport};
            push(std::move(op));
        }
        if (res.dynamicScissor)
        {
            Op op{};
            op.code = OpCode::SetScissor;
            op.passIndex = passIndex_;
            op.payload = SetScissorPayload{.scissor = res.scissor};
            push(std::move(op));
        }

        // 4. push constants
        if (res.pushConstants.size != 0)
        {
            PushConstantsPayload payload{};
            payload.layout = res.pushConstants.layout;
            payload.stages = res.pushConstants.stages;
            payload.offset = res.pushConstants.offset;
            payload.size = res.pushConstants.size;
            std::memcpy(payload.data.data(), res.pushConstants.bytes.data(),
                        res.pushConstants.size);
            Op op{};
            op.code = OpCode::PushConstants;
            op.passIndex = passIndex_;
            op.payload = std::move(payload);
            push(std::move(op));
        }

        // 5. bind index / vertex buffers
        if (res.indexBuffer != VK_NULL_HANDLE)
        {
            Op op{};
            op.code = OpCode::BindIndexBuffer;
            op.passIndex = passIndex_;
            op.payload = BindIndexBufferPayload{.buffer = res.indexBuffer,
                                                .offset = res.indexOffset,
                                                .type = res.indexType};
            push(std::move(op));
        }
        if (res.vertexBuffer != VK_NULL_HANDLE)
        {
            Op op{};
            op.code = OpCode::BindVertexBuffers;
            op.passIndex = passIndex_;
            BindVertexBuffersPayload payload{};
            payload.firstBinding = 0;
            payload.bindingCount = 1;
            payload.buffers[0] = res.vertexBuffer;
            payload.offsets[0] = res.vertexOffset;
            op.payload = std::move(payload);
            push(std::move(op));
        }

        // 6. draw
        {
            Op op{};
            op.code = OpCode::Draw;
            op.passIndex = passIndex_;
            op.payload = DrawPayload{.args = args};
            push(std::move(op));
        }
        ++drawCalls_;
    }

    // ---- explicit state ops (used by non-UI consumers) -------------------
    void bindPipeline(const DrawPipeline &pipeline)
    {
        if (!validator_.bindPipeline(pipeline))
        {
            valid_ = false;
            return;
        }
        Op op{};
        op.code = OpCode::BindPipeline;
        op.passIndex = passIndex_;
        op.payload = BindPipelinePayload{
            .pipeline = &pipeline, .bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS};
        push(std::move(op));
    }

    void bindDescriptorSet(VkPipelineLayout layout, VkDescriptorSet set,
                           std::uint32_t firstSet = 0)
    {
        if (!validator_.bindDescriptorSet(layout, set))
        {
            valid_ = false;
            return;
        }
        Op op{};
        op.code = OpCode::BindDescriptorSet;
        op.passIndex = passIndex_;
        op.payload = BindDescriptorSetPayload{
            .layout = layout, .set = set, .firstSet = firstSet};
        push(std::move(op));
    }

    void pushConstants(VkPipelineLayout layout, VkShaderStageFlags stages,
                       std::uint32_t offset, std::uint32_t size, const void *data)
    {
        if (!validator_.pushConstants(layout, stages, offset, size))
        {
            valid_ = false;
            return;
        }
        PushConstantsPayload payload{};
        payload.layout = layout;
        payload.stages = stages;
        payload.offset = offset;
        payload.size = size;
        if (data != nullptr && size != 0)
            std::memcpy(payload.data.data(), data, size);
        Op op{};
        op.code = OpCode::PushConstants;
        op.passIndex = passIndex_;
        op.payload = std::move(payload);
        push(std::move(op));
    }

    void setViewport(const VkViewport &viewport)
    {
        if (!validator_.setViewport())
            valid_ = false;
        Op op{};
        op.code = OpCode::SetViewport;
        op.passIndex = passIndex_;
        op.payload = SetViewportPayload{.viewport = viewport};
        push(std::move(op));
    }

    void setScissor(const VkRect2D &scissor)
    {
        if (!validator_.setScissor())
            valid_ = false;
        Op op{};
        op.code = OpCode::SetScissor;
        op.passIndex = passIndex_;
        op.payload = SetScissorPayload{.scissor = scissor};
        push(std::move(op));
    }

    void bindIndexBuffer(VkBuffer buffer, VkDeviceSize offset, VkIndexType type)
    {
        if (!validator_.bindIndexBuffer(buffer))
            valid_ = false;
        Op op{};
        op.code = OpCode::BindIndexBuffer;
        op.passIndex = passIndex_;
        op.payload = BindIndexBufferPayload{.buffer = buffer, .offset = offset,
                                            .type = type};
        push(std::move(op));
    }

    void bindVertexBuffers(std::span<const VkBuffer> buffers,
                           std::span<const VkDeviceSize> offsets,
                           std::uint32_t firstBinding = 0)
    {
        assert(buffers.size() == offsets.size());
        BindVertexBuffersPayload payload{};
        payload.firstBinding = firstBinding;
        payload.bindingCount = static_cast<std::uint32_t>(
            std::min<std::size_t>(buffers.size(), MAX_VERTEX_BINDINGS));
        for (std::uint32_t i = 0; i < payload.bindingCount; ++i)
        {
            payload.buffers[i] = buffers[i];
            payload.offsets[i] = offsets[i];
        }
        Op op{};
        op.code = OpCode::BindVertexBuffers;
        op.passIndex = passIndex_;
        op.payload = std::move(payload);
        push(std::move(op));
    }

    // ---- synchronization ------------------------------------------------
    void barrier(const VkDependencyInfo &dependency)
    {
        assert(dependency.memoryBarrierCount <= MAX_BARRIERS);
        assert(dependency.bufferMemoryBarrierCount <= MAX_BARRIERS);
        assert(dependency.imageMemoryBarrierCount <= MAX_BARRIERS);
        BarrierPayload payload{};
        payload.dependencyFlags = dependency.dependencyFlags;
        payload.memoryBarrierCount = dependency.memoryBarrierCount;
        payload.bufferBarrierCount = dependency.bufferMemoryBarrierCount;
        payload.imageBarrierCount = dependency.imageMemoryBarrierCount;
        for (std::uint32_t i = 0; i < payload.memoryBarrierCount; ++i)
            payload.memoryBarriers[i] = dependency.pMemoryBarriers[i];
        for (std::uint32_t i = 0; i < payload.bufferBarrierCount; ++i)
            payload.bufferBarriers[i] = dependency.pBufferMemoryBarriers[i];
        for (std::uint32_t i = 0; i < payload.imageBarrierCount; ++i)
            payload.imageBarriers[i] = dependency.pImageMemoryBarriers[i];
        Op op{};
        op.code = OpCode::Barrier;
        op.passIndex = passIndex_;
        op.payload = std::move(payload);
        push(std::move(op));
    }

    void barrier(const VkMemoryBarrier2 &memoryBarrier)
    {
        const VkDependencyInfo dependency{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = nullptr,
            .dependencyFlags = 0,
            .memoryBarrierCount = 1,
            .pMemoryBarriers = &memoryBarrier,
        };
        barrier(dependency);
    }

    void barrier(const VkImageMemoryBarrier2 &imageBarrier)
    {
        const VkDependencyInfo dependency{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = nullptr,
            .dependencyFlags = 0,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &imageBarrier,
        };
        barrier(dependency);
    }

    void barrier(std::span<const VkImageMemoryBarrier2> imageBarriers)
    {
        if (imageBarriers.empty())
            return;
        const VkDependencyInfo dependency{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = nullptr,
            .dependencyFlags = 0,
            .imageMemoryBarrierCount =
                static_cast<std::uint32_t>(imageBarriers.size()),
            .pImageMemoryBarriers = imageBarriers.data(),
        };
        barrier(dependency);
    }

    // ---- transfers ------------------------------------------------------
    void fillBuffer(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size,
                    std::uint32_t data)
    {
        Op op{};
        op.code = OpCode::FillBuffer;
        op.passIndex = passIndex_;
        op.payload = FillBufferPayload{.buffer = buffer, .offset = offset,
                                       .size = size, .data = data};
        push(std::move(op));
    }

    void copyBuffer(VkBuffer src, VkBuffer dst,
                    std::span<const VkBufferCopy> regions)
    {
        assert(regions.size() <= MAX_BARRIERS);
        CopyBufferPayload payload{};
        payload.src = src;
        payload.dst = dst;
        payload.regionCount = static_cast<std::uint32_t>(regions.size());
        for (std::uint32_t i = 0; i < payload.regionCount; ++i)
            payload.regions[i] = regions[i];
        Op op{};
        op.code = OpCode::CopyBuffer;
        op.passIndex = passIndex_;
        op.payload = std::move(payload);
        push(std::move(op));
    }

    void copyImageToBuffer(VkImage image, VkImageLayout layout, VkBuffer buffer,
                           std::span<const VkBufferImageCopy> regions)
    {
        assert(regions.size() <= MAX_BARRIERS);
        CopyImageToBufferPayload payload{};
        payload.image = image;
        payload.imageLayout = layout;
        payload.buffer = buffer;
        payload.regionCount = static_cast<std::uint32_t>(regions.size());
        for (std::uint32_t i = 0; i < payload.regionCount; ++i)
            payload.regions[i] = regions[i];
        Op op{};
        op.code = OpCode::CopyImageToBuffer;
        op.passIndex = passIndex_;
        op.payload = std::move(payload);
        push(std::move(op));
    }

    void executeCommands(std::span<const VkCommandBuffer> commandBuffers)
    {
        assert(!commandBuffers.empty());
        ExecuteCommandsPayload payload{};
        payload.commandBufferCount = static_cast<std::uint32_t>(
            std::min<std::size_t>(commandBuffers.size(), MAX_BARRIERS));
        for (std::uint32_t i = 0; i < payload.commandBufferCount; ++i)
            payload.commandBuffers[i] = commandBuffers[i];
        Op op{};
        op.code = OpCode::ExecuteCommands;
        op.passIndex = passIndex_;
        op.payload = std::move(payload);
        push(std::move(op));
    }

    // ---- inspection ------------------------------------------------------
    [[nodiscard]] const OpStream &stream() const noexcept { return stream_; }
    [[nodiscard]] const DrawContext &context() const noexcept { return context_; }
    [[nodiscard]] bool valid() const noexcept
    {
        return valid_ && validator_.valid();
    }
    [[nodiscard]] std::uint32_t passCount() const noexcept { return passCount_; }
    [[nodiscard]] std::uint32_t drawCount() const noexcept { return drawCalls_; }
    [[nodiscard]] std::uint32_t emittedCalls() const noexcept
    {
        return emitter_.calls();
    }

    [[nodiscard]] std::string dump() const { return intent8::dump(stream_); }

    [[nodiscard]] std::vector<std::string> c_api_list() const
    {
        return intent8::c_api_list(stream_);
    }

    void emitAll()
    {
        if (!deferred_ || emitted_)
            return;
        for (const auto &op : stream_.ops())
            emitter_.emit(op);
        emitted_ = true;
    }

    void end() { emitAll(); }

  private:
    bool validateDraw(const DrawCall &call)
    {
        if (call.pipelineData == nullptr || !call.pipelineData->valid())
        {
            valid_ = false;
            return false;
        }
        // Feed the validator the state the expansion is about to establish so
        // the final draw check sees exactly the recorded state.
        const DrawPipeline &pipeline = *call.pipelineData->pipeline;
        const DrawResources &res = *call.pipelineData->resources;
        const bool ok =
            validator_.bindPipeline(pipeline) &&
            validator_.bindDescriptorSet(pipeline.layout(), res.descriptors.set) &&
            (res.pushConstants.size == 0 ||
             validator_.pushConstants(res.pushConstants.layout,
                                      res.pushConstants.stages,
                                      res.pushConstants.offset,
                                      res.pushConstants.size)) &&
            (!res.dynamicViewport || validator_.setViewport()) &&
            (!res.dynamicScissor || validator_.setScissor()) &&
            (res.indexBuffer == VK_NULL_HANDLE ||
             validator_.bindIndexBuffer(res.indexBuffer)) &&
            validator_.draw(call);
        if (!ok)
        {
            valid_ = false;
            assert(false && "FrameRecorder rejected a DrawCall");
        }
        return ok;
    }

    void push(Op op)
    {
        stream_.add(std::move(op));
        if (!deferred_)
            emitter_.emit(stream_.ops().back());
    }

    VkEmitter emitter_;
    DrawValidator validator_;
    DrawContext context_{};
    OpStream stream_{};
    RenderTarget lastTarget_{};
    bool deferred_{false};
    bool emitted_{false};
    bool valid_{true};
    bool activeTarget_{false};
    bool inherited_{false};
    std::uint32_t passIndex_{0};
    std::uint32_t passCount_{0};
    std::uint32_t drawCalls_{0};
};

} // namespace intent8
