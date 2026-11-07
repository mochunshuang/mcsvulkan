#pragma once

// ============================================================================
// test_intent6_command_stream.hpp
//
// Builder 与 Emitter 之间唯一的契约。
// 当前测试只需要：一条 indirect draw + 一个 viewport/scissor + push constants。
// 状态切换（动态深度/拓扑/线宽/stencil、描述符集）在这个 dao→shader 测试里
// 没有使用，全部不保留。
// ============================================================================

#include "test_intent6_shader_abi.hpp"

#include <cstdint>
#include <span>
#include <vector>

// ── 网格描述：draw 参数，不是状态 ─────────────────────────────
struct mesh_data
{
    std::uint32_t vertexCount{0};
    std::uint32_t vertexOffset{0};
    std::uint32_t indexOffset{0};
    std::uint32_t indexCount{0};

    [[nodiscard]] constexpr VkDrawIndexedIndirectCommand getDrawCommand(
        std::uint32_t instanceCount, std::uint32_t firstInstance = 0) const noexcept
    {
        return {.indexCount = indexCount,
                .instanceCount = instanceCount,
                .firstIndex = indexOffset,
                .vertexOffset = static_cast<std::int32_t>(vertexOffset),
                .firstInstance = firstInstance};
    }

    bool operator==(const mesh_data &) const = default;

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return vertexCount != 0 && indexCount != 0;
    }
};

namespace shader_data
{

    // 一次 drawIndexedIndirect 的句柄
    struct SegmentHandles
    {
        VkPipeline pipeline{};
        VkPipelineLayout layout{};
    };

    // 一次 drawIndexedIndirect 的 viewport/scissor（测试只有一个）
    struct SegmentRegisters
    {
        VkViewport viewport{};
        VkRect2D scissor{};
    };

    // 一次录制重建的初始环境：builder.begin() 的输入，纯数据。
    struct FrameState
    {
        VkPipeline pipeline{};
        VkPipelineLayout layout{};

        PushData pushData{};
        mesh_data mesh{};

        VkViewport viewport{};
        VkRect2D scissor{};
    };

    // ④ 指令流：瘦指令，只有两个整数
    struct SegmentDraw
    {
        std::uint32_t indirectOffset{0};
        std::uint32_t commandCount{0};
    };

    struct DrawSegment
    {
        SegmentHandles handles;
        SegmentRegisters regs;
        SegmentDraw draw;
        PushData pushData{}; // commandConstantsAddress 已按段解析
    };

    struct CommandStream
    {
        const std::vector<DrawSegment> *segments{};
    };

} // namespace shader_data
