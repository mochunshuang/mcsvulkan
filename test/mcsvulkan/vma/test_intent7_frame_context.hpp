#pragma once

// ============================================================================
// test_intent7_frame_context.hpp
//
// Per-frame context: binds resources and builders. No policy.
//   ShaderBuffers   - resource layer
//   CommandBuilder  - resolve + build (DrawSegment)
//   emit            - DrawSegment -> FrameBuilder ops
//
// emit takes a Recorder (FrameBuilder or SecondaryBuilder), never a raw
// command buffer, so state/action ops stay inside one explicit pass scope.
// ============================================================================

#include "test_intent7_command_builder.hpp"
#include "test_intent7_pass_data.hpp"

#include <utility>

namespace shader_data
{
    // Emit one UI CommandStream into a Recorder.
    // Recorder is FrameBuilder or SecondaryBuilder.
    //
    // 层次关系：
    //   DrawSegment/CommandStream = UI 的“意图”（要画什么、用哪些资源）
    //   emit(...)                = 意图 -> 操作（BindPipeline/BindResources/SetState/Draw）
    //   FrameBuilder/OpStream    = 操作 -> 线性 Op 程序
    //   VkEmitter                = Op -> 显式 vkCmd*
    //
    // 顺序 = 一个 pass 内完整的绘制状态：
    //   bindIndexBuffer
    //   bindPipeline (变化时)
    //   bindDescriptorSets
    //   setViewport / setScissor
    //   pushConstants
    //   drawIndexedIndirect
    template <typename Recorder>
    static void emit(Recorder &pass, const ShaderBuffers &buffers,
                     const CommandStream &stream, VkDescriptorSet descriptorSet)
    {
        assert(stream.segments != nullptr);
        if (stream.segments->empty())
            return;
        assert(descriptorSet != VK_NULL_HANDLE);

        pass.bindIndexBuffer(buffers.globalIndexBuffer.buffer.buffer(), 0,
                             VK_INDEX_TYPE_UINT32);

        VkPipeline lastPipeline = VK_NULL_HANDLE;

        for (const auto &seg : *stream.segments)
        {
            if (seg.handles.pipeline != lastPipeline)
            {
                pass.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, seg.handles.pipeline);
                lastPipeline = seg.handles.pipeline;
            }

            // 每个 segment 都重新绑定一次：pass 之间不共享“上一次 bind”的
            // 隐式状态。这是 Vulkan 命令缓冲区里显式、可验证的做法。
            pass.bindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS, seg.handles.layout,
                                   0, descriptorSet);

            pass.setViewport(0, std::span{&seg.regs.viewport, 1});
            pass.setScissor(0, std::span{&seg.regs.scissor, 1});

            pass.pushConstants(seg.handles.layout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(PushData), &seg.pushData);

            pass.drawIndexedIndirect(buffers.indirectDrawBuffer.buffer.buffer(),
                                     static_cast<VkDeviceSize>(seg.draw.indirectOffset),
                                     seg.draw.commandCount,
                                     sizeof(VkDrawIndexedIndirectCommand));
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
            builder.bind(buffers); // 指针必须指向新地址
        }
    };

} // namespace shader_data
