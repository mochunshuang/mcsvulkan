#pragma once

// ============================================================================
// test_intent3_command_stream.hpp
//
// Builder 与 Emitter 之间唯一的契约。
// 表元素是**段**：一次 drawIndexedIndirect 的全部输入，动态状态按值存一份。
//
//   CommandBuilder ──flush──► CommandStream ──顺序重放──► CommandEmitter
//
// 没有 arena、没有 slice：viewport/scissor 就是段自己的字段。
// ============================================================================

#include "test_intent3_shader_abi.hpp"

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

    // 动态状态位：自己编号（VkDynamicState 在 1.4 里是 64 位扩展值，不能当位下标）
    using DynamicStateMask = std::uint64_t;
    enum DynamicStateBit : std::uint32_t
    {
        kDynViewport = 1u << 0,
        kDynScissor = 1u << 1,
        kDynDepthTest = 1u << 2,
        kDynDepthWrite = 1u << 3,
        kDynTopology = 1u << 4,
        kDynLineWidth = 1u << 5,
        kDynStencilRef = 1u << 6,
    };

    // 设备打开时录制一次：哪些动态状态可用（feature 决定）
    struct DynamicStateCaps
    {
        DynamicStateMask supported{};

        [[nodiscard]] constexpr bool has(DynamicStateBit bit) const noexcept
        {
            return (supported & bit) != 0;
        }
        constexpr DynamicStateCaps &add(DynamicStateBit bit) noexcept
        {
            supported |= bit;
            return *this;
        }
    };

    // ① 句柄表：只读引用
    // VkPipeline / VkPipelineLayout / VkDescriptorSet 都是 NON_DISPATCHABLE_HANDLE
    struct SegmentHandles
    {
        VkPipeline pipeline{};
        VkPipelineLayout layout{};

        // 描述符集是资源：引用调用方的存储，不拷贝、不设容量。
        // 约定：这块存储必须活到 emit（描述符集本来就按帧常驻）。
        std::span<const VkDescriptorSet> sets{};
        std::uint32_t firstSet{0};

        // 这条管线声明了哪些动态状态（VkPipelineDynamicStateCreateInfo）。
        // 没声明的状态，vkCmdSetXxx 会被忽略，值是管线里烘焙的静态值。
        DynamicStateMask dynamicStateMask{0};
    };

    // ③ 状态寄存器：本段生效值
    inline constexpr std::uint32_t kMaxViewports = 1;

    struct SegmentRegisters
    {
        VkViewport viewports[kMaxViewports]{};
        std::uint32_t viewportCount{0};

        VkRect2D scissors[kMaxViewports]{};
        std::uint32_t scissorCount{0};

        // 动态偏移是值：通常临时算出来，按值存一份，避免悬垂。
        std::uint32_t dynamicOffset{0};
        std::uint32_t dynamicOffsetCount{0};

        // 扩展动态状态：仓库里高频的五个
        std::uint32_t depthTestEnable{0};
        std::uint32_t depthWriteEnable{0};
        VkPrimitiveTopology topology{VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
        float lineWidth{1.0F};
        VkStencilFaceFlags stencilFace{VK_STENCIL_FACE_FRONT_AND_BACK};
        std::uint32_t stencilRef{0};

        // 哪些动态状态“已定义”（对应 md 里的 defined 位掩码）
        DynamicStateMask dynamicMask{0};
    };

    [[nodiscard]] constexpr bool same_viewports(const VkViewport *a, std::uint32_t count,
                                                std::span<const VkViewport> b) noexcept
    {
        if (count != b.size())
            return false;
        for (std::uint32_t i = 0; i < count; ++i)
            if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].width != b[i].width ||
                a[i].height != b[i].height || a[i].minDepth != b[i].minDepth ||
                a[i].maxDepth != b[i].maxDepth)
                return false;
        return true;
    }

    [[nodiscard]] constexpr bool same_scissors(const VkRect2D *a, std::uint32_t count,
                                               std::span<const VkRect2D> b) noexcept
    {
        if (count != b.size())
            return false;
        for (std::uint32_t i = 0; i < count; ++i)
            if (a[i].offset.x != b[i].offset.x || a[i].offset.y != b[i].offset.y ||
                a[i].extent.width != b[i].extent.width ||
                a[i].extent.height != b[i].extent.height)
                return false;
        return true;
    }

    // 一次录制重建的初始环境：builder.begin() 的输入，纯数据。
    struct FrameState
    {
        VkPipeline pipeline{};
        VkPipelineLayout layout{};
        DynamicStateMask pipelineDynMask{0};

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
        SegmentHandles handles; // ①
        SegmentRegisters regs;  // ③
        SegmentDraw draw;       // ④
        PushData pushData{};    // ⑤ commandConstantsAddress 已按段解析
    };

    struct CommandStream
    {
        const std::vector<DrawSegment> *segments{};
    };

} // namespace shader_data
