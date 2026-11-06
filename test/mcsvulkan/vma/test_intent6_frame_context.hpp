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

#include "test_intent5_command_builder.hpp"
#include "test_intent5_shader_buffers.hpp"

#include <utility>

namespace shader_data
{

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
