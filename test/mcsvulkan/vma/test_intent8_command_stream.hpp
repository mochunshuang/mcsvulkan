#pragma once

// ============================================================================
// test_intent8_command_stream.hpp
//
// The only contract between the UI CommandBuilder and the draw recorder.
//
// A DrawSegment is one draw operation: which DrawPipelineData to use, the
// viewport/scissor state, the indirect draw range and the push constants.
// It is data, not a command.
// ============================================================================

#include "test_intent8_shader_abi.hpp"

#include <cstdint>
#include <vector>

namespace intent8
{
struct DrawPipeline;
}

// ── mesh description: draw operands, not state ──────────────────────────────
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

    // One segment needs a real DrawPipeline (config + built resource).
    struct SegmentHandles
    {
        const intent8::DrawPipeline *pipeline{nullptr};
    };

    // Per-segment dynamic state.
    struct SegmentRegisters
    {
        VkViewport viewport{};
        VkRect2D scissor{};
    };

    // Initial environment for builder.begin(), pure data.
    struct FrameState
    {
        const intent8::DrawPipeline *pipeline{nullptr};

        PushData pushData{};
        mesh_data mesh{};

        VkViewport viewport{};
        VkRect2D scissor{};
    };

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
        PushData pushData{}; // commandConstantsAddress resolved per segment
    };

    struct CommandStream
    {
        const std::vector<DrawSegment> *segments{};
    };

} // namespace shader_data
