#pragma once

// ============================================================================
// test_intent8_frame_context.hpp
//
// Per-frame context: binds resources and builders. No policy.
//   ShaderBuffers   - resource layer (per build slot)
//   CommandBuilder  - UI intent -> DrawSegment
//   emit            - DrawSegment -> DrawCall -> recorder ops
//
// emit takes a recorder (FrameRecorder), never a raw command buffer, so every
// state/action op stays inside one explicit render-target scope.
// ============================================================================

#include "test_intent8_command_builder.hpp"
#include "test_intent8_draw_pipeline.hpp"

#include <cstring>
#include <utility>

namespace shader_data
{
    // Emit one UI command stream as DrawCalls.
    //
    // A DrawCall is the blueprint's operation group:
    //   bind pipeline
    //   bind descriptor set
    //   push constants
    //   bind index / vertex / indirect
    //   draw
    //
    // The DrawResources view is stack-local; the recorder copies every value
    // it needs into the op payload, so no dangling pointer escapes.
    template <typename Recorder>
    static void emit(Recorder &recorder, const ShaderBuffers &buffers,
                     const CommandStream &stream, VkDescriptorSet descriptorSet)
    {
        assert(stream.segments != nullptr);
        if (stream.segments->empty())
            return;
        assert(descriptorSet != VK_NULL_HANDLE);

        for (const auto &seg : *stream.segments)
        {
            assert(seg.handles.pipeline != nullptr);
            assert(seg.handles.pipeline->valid());
            assert(seg.draw.commandCount != 0);

            intent8::DrawResources resources{};
            resources.descriptors = intent8::DescriptorSetResources{
                .set = descriptorSet,
                .layout = seg.handles.pipeline->descriptorLayout};
            resources.indexBuffer = buffers.globalIndexBuffer.buffer.buffer();
            resources.indexOffset = 0;
            resources.indexType = VK_INDEX_TYPE_UINT32;
            resources.indirectBuffer = buffers.indirectDrawBuffer.buffer.buffer();
            resources.indirectOffset =
                static_cast<VkDeviceSize>(seg.draw.indirectOffset);
            resources.drawCount = seg.draw.commandCount;
            resources.stride = sizeof(VkDrawIndexedIndirectCommand);
            resources.viewport = seg.regs.viewport;
            resources.scissor = seg.regs.scissor;
            resources.dynamicViewport = true;
            resources.dynamicScissor = true;
            resources.pushConstants.layout = seg.handles.pipeline->layout();
            resources.pushConstants.stages =
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            resources.pushConstants.offset = 0;
            resources.pushConstants.set(&seg.pushData, sizeof(PushData));

            const intent8::DrawArgs args{
                .kind = intent8::DrawKind::DrawIndexedIndirect,
                .indirectBuffer = resources.indirectBuffer,
                .indirectOffset = resources.indirectOffset,
                .drawCount = resources.drawCount,
                .stride = resources.stride,
            };
            const intent8::DrawPipelineData pipelineData{
                .pipeline = seg.handles.pipeline,
                .target = nullptr, // resolved by the recorder's active target
                .resources = &resources};
            const intent8::DrawCall call{.pipelineData = &pipelineData,
                                         .args = args};
            recorder.draw(call);
        }
    }

    struct FrameContext
    {
        ShaderBuffers buffers;
        CommandBuilder builder;

        constexpr FrameContext(const LogicalDevice &device, VkDeviceSize vertexCapacity,
                               VkDeviceSize indexCapacity, VkDeviceSize heapCapacity,
                               VkDeviceSize indirectDrawCapacity,
                               VkDeviceSize commandConstantsCapacity)
            : buffers{device,       vertexCapacity,       indexCapacity,
                      heapCapacity, indirectDrawCapacity, commandConstantsCapacity},
              builder{buffers}
        {
        }

        FrameContext(const FrameContext &) = delete;
        FrameContext &operator=(const FrameContext &) = delete;

        constexpr FrameContext(FrameContext &&other) noexcept
            : buffers{std::move(other.buffers)}, builder{std::move(other.builder)}
        {
            builder.bind(buffers); // the builder must point at the new address
        }
    };

} // namespace shader_data
