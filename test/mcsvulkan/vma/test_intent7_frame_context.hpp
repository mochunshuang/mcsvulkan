#pragma once

// ============================================================================
// test_intent7_frame_context.hpp
//
// 每帧一个的上下文：只负责把「资源」和「构建器」绑在一起，**不含逻辑**。
//   ShaderBuffers   —— 资源层
//   CommandBuilder  —— 解析 + 构建（有状态）
//   emit            —— 录制（无状态）
//
// 当前 dao→shader 测试只需要 pipeline/layout/viewport/scissor + push constants；
// 没有动态深度/拓扑/线宽/stencil、没有描述符集重绑。
// ============================================================================

#include "test_intent7_command_builder.hpp"

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

        for (const auto &seg : *stream.segments)
        {
            if (seg.handles.pipeline != lastPipeline)
            {
                cmd.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, seg.handles.pipeline);
                lastPipeline = seg.handles.pipeline;
            }

            cmd.setViewport(0, std::span{&seg.regs.viewport, 1});
            cmd.setScissor(0, std::span{&seg.regs.scissor, 1});

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
