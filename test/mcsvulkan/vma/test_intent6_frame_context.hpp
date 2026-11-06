#pragma once

// ============================================================================
// test_intent5_frame_context.hpp
//
// 每帧一个的上下文：只负责把「资源」和「构建器」绑在一起，**不含逻辑**。
// 原来那个 DrawRecorder 被拆成：
//   ShaderBuffers   —— 资源层
//   CommandBuilder  —— 解析 + 构建（有状态）
//   command_emitter —— 录制（无状态）
// ============================================================================

#include "test_intent6_command_builder.hpp"

#include <utility>

namespace shader_data
{
    static constexpr void emit(CommandBufferView cmd, const ShaderBuffers &buffers,
                               const CommandStream &stream)
    {
        assert(stream.segments != nullptr);

        if (stream.segments->empty())
            return;

        cmd.bindIndexBuffer(buffers.globalIndexBuffer.buffer.buffer(), 0,
                            VK_INDEX_TYPE_UINT32);

        VkPipeline lastPipeline = VK_NULL_HANDLE;
        DynamicStateMask lastDynMask = 0;
        std::uint32_t lastDepthTest = 0, lastDepthWrite = 0, lastStencilRef = 0;
        VkPrimitiveTopology lastTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        float lastLineWidth = 1.0F;
        VkViewport lastViewports[kMaxViewports]{};
        std::uint32_t lastViewportCount = 0;
        VkRect2D lastScissors[kMaxViewports]{};
        std::uint32_t lastScissorCount = 0;

        for (const auto &seg : *stream.segments)
        {
            if (seg.handles.pipeline != lastPipeline)
            {
                cmd.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, seg.handles.pipeline);
                lastPipeline = seg.handles.pipeline;
            }

            if (!seg.handles.sets.empty())
            {
                std::uint32_t offset = seg.regs.dynamicOffset;
                cmd.bindDescriptorSets(
                    VK_PIPELINE_BIND_POINT_GRAPHICS, seg.handles.layout,
                    seg.handles.firstSet,
                    static_cast<std::uint32_t>(seg.handles.sets.size()),
                    seg.handles.sets.data(), seg.regs.dynamicOffsetCount,
                    seg.regs.dynamicOffsetCount != 0 ? &offset : nullptr);
            }

            // 扩展动态状态：设备支持 ∩ 管线声明 ∩ 应用设置
            const auto &r = seg.regs;
            const DynamicStateMask activeDyn =
                r.dynamicMask & seg.handles.dynamicStateMask;
            if ((activeDyn & kDynDepthTest) &&
                (!(lastDynMask & kDynDepthTest) || r.depthTestEnable != lastDepthTest))
            {
                cmd.setDepthTestEnable(r.depthTestEnable != 0);
                lastDepthTest = r.depthTestEnable;
            }
            if ((activeDyn & kDynDepthWrite) &&
                (!(lastDynMask & kDynDepthWrite) || r.depthWriteEnable != lastDepthWrite))
            {
                cmd.setDepthWriteEnable(r.depthWriteEnable != 0);
                lastDepthWrite = r.depthWriteEnable;
            }
            if ((activeDyn & kDynTopology) &&
                (!(lastDynMask & kDynTopology) || r.topology != lastTopology))
            {
                cmd.setPrimitiveTopology(r.topology);
                lastTopology = r.topology;
            }
            if ((activeDyn & kDynLineWidth) &&
                (!(lastDynMask & kDynLineWidth) || r.lineWidth != lastLineWidth))
            {
                cmd.setLineWidth(r.lineWidth);
                lastLineWidth = r.lineWidth;
            }
            if ((activeDyn & kDynStencilRef) &&
                (!(lastDynMask & kDynStencilRef) || r.stencilRef != lastStencilRef))
            {
                cmd.setStencilReference(r.stencilFace, r.stencilRef);
                lastStencilRef = r.stencilRef;
            }
            lastDynMask |= activeDyn;

            if (r.viewportCount != 0 &&
                !(r.viewportCount == lastViewportCount &&
                  same_viewports(lastViewports, lastViewportCount,
                                 std::span{r.viewports, r.viewportCount})))
            {
                cmd.setViewport(0, std::span{r.viewports, r.viewportCount});
                lastViewportCount = r.viewportCount;
                for (std::uint32_t i = 0; i < r.viewportCount; ++i)
                    lastViewports[i] = r.viewports[i];
            }
            if (r.scissorCount != 0 &&
                !(r.scissorCount == lastScissorCount &&
                  same_scissors(lastScissors, lastScissorCount,
                                std::span{r.scissors, r.scissorCount})))
            {
                cmd.setScissor(0, std::span{r.scissors, r.scissorCount});
                lastScissorCount = r.scissorCount;
                for (std::uint32_t i = 0; i < r.scissorCount; ++i)
                    lastScissors[i] = r.scissors[i];
            }

            cmd.pushConstants(seg.handles.layout,
                              VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                              0, sizeof(PushData), &seg.pushData);

            cmd.drawIndexedIndirect(buffers.indirectDrawBuffer.buffer.buffer(),
                                    static_cast<VkDeviceSize>(seg.draw.indirectOffset),
                                    seg.draw.commandCount,
                                    sizeof(VkDrawIndexedIndirectCommand));
        }
    }

    struct FrameContext
    {

        ShaderBuffers buffers;
        CommandBuilder builder;

        constexpr FrameContext(const LogicalDevice &device, DynamicStateCaps caps,
                               VkDeviceSize vertexCapacity, VkDeviceSize indexCapacity,
                               VkDeviceSize heapCapacity,
                               VkDeviceSize indirectDrawCapacity,
                               VkDeviceSize commandConstantsCapacity)
            : buffers{device,       vertexCapacity,       indexCapacity,
                      heapCapacity, indirectDrawCapacity, commandConstantsCapacity},
              builder{buffers, caps}
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
