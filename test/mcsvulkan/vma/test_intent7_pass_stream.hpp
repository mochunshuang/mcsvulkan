#pragma once
// ============================================================================
// test_intent7_command_stream.hpp
//
// 命令流 = 数据（句柄表/状态寄存器/指令流/元数据）+ 算法（校验/发射）。
// 指令是瘦的：draw 只带计数/偏移；pipeline/DS/VB/IB/dynamic state 在状态寄存器。
//
// 顶层仍是 [pass...pass]；Pass = PassDesc(target/subpass) + 这段 CommandStream。
// PassCompiler 把每条 Op 翻译成对应的 vkCmd*（C API 列表）。
//
// UI 的 7 个 DAO row / CommandBuilder 只是本流的一个 generator，不是框架本身。
// ============================================================================

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace pass_data
{

// 覆盖 Vulkan 绘制路径的 opcode；每条都对应一个 vkCmd*。
enum class Op : std::uint16_t
{
    // pass 边界
    BeginRenderPass,
    EndRenderPass,
    NextSubpass,
    BeginRendering,
    EndRendering,

    // 状态
    BindPipeline,
    BindDescriptorSets,
    PushConstants,
    PushDescriptorSet,
    BindVertexBuffers,
    BindIndexBuffer,
    SetViewport,
    SetScissor,
    SetLineWidth,
    SetDepthBias,
    SetBlendConstants,
    SetDepthBounds,
    SetStencilCompareMask,
    SetStencilWriteMask,
    SetStencilReference,
    SetCullMode,
    SetFrontFace,
    SetPrimitiveTopology,
    SetDepthTestEnable,
    SetDepthWriteEnable,
    SetDepthCompareOp,
    SetStencilTestEnable,
    SetStencilOp,
    SetRasterizerDiscard,
    SetPatchControlPoints,
    SetLogicOp,
    SetColorBlendEnable,
    SetColorBlendEquation,
    SetColorWriteMask,
    SetSampleMask,
    SetAlphaToCoverage,
    SetVertexInput,
    SetFragmentShadingRate,

    // draw / dispatch
    Draw,
    DrawIndexed,
    DrawIndirect,
    DrawIndexedIndirect,
    DrawIndirectCount,
    DrawIndexedIndirectCount,
    DrawMeshTasks,
    DrawMeshTasksIndirect,
    DrawMeshTasksIndirectCount,
    Dispatch,
    DispatchIndirect,

    // 次级 / 查询 / 条件 / 变换反馈
    ExecuteCommands,
    BeginQuery,
    EndQuery,
    WriteTimestamp,
    ResetQuery,
    CopyQueryResults,
    BeginConditionalRendering,
    EndConditionalRendering,
    BeginTransformFeedback,
    EndTransformFeedback,

    // 附件 / 同步 / 拷贝
    ClearAttachments,
    PipelineBarrier,
    SetEvent,
    WaitEvent,
    ResetEvent,
    CopyBuffer,
    CopyBufferToImage,
    CopyImage,
    BlitImage,
    ResolveImage,
    FillBuffer,
    UpdateBuffer,
    DebugMarker,
};

// 瘦指令 + payload。完整 payload 用 generic 承载，编译器按 Op 解释。
struct Cmd
{
    Op op{Op::Draw};
    union
    {
        struct
        {
            VkPipeline pipeline;
            VkPipelineLayout layout;
        } bindPipeline;
        struct
        {
            VkPipelineLayout layout;
            VkDescriptorSet set;
        } bindDescriptors;
        struct
        {
            VkPipelineLayout layout;
            VkShaderStageFlags stages;
            std::uint32_t offset;
            std::uint32_t size;
            std::array<std::uint8_t, 128> data;
        } pushConstants;
        struct
        {
            std::uint32_t first;
            std::uint32_t count;
            VkBuffer buffers[8];
            VkDeviceSize offsets[8];
        } bindVertexBuffers;
        struct
        {
            VkBuffer buffer;
            VkDeviceSize offset;
            VkIndexType type;
        } bindIndexBuffer;
        struct
        {
            VkViewport viewport;
        } setViewport;
        struct
        {
            VkRect2D scissor;
        } setScissor;
        struct
        {
            float lineWidth;
        } setLineWidth;
        struct
        {
            float constant;
            float clamp;
            float slope;
        } setDepthBias;
        struct
        {
            float values[4];
        } setBlendConstants;
        struct
        {
            float minDepth;
            float maxDepth;
        } setDepthBounds;
        struct
        {
            std::uint32_t compareMask;
            std::uint32_t writeMask;
            std::uint32_t reference;
        } setStencil;
        struct
        {
            VkCullModeFlags cullMode;
        } setCullMode;
        struct
        {
            VkFrontFace frontFace;
        } setFrontFace;
        struct
        {
            VkPrimitiveTopology topology;
        } setPrimitiveTopology;
        struct
        {
            VkBool32 enable;
        } setBool;
        struct
        {
            VkCompareOp compareOp;
        } setCompareOp;
        struct
        {
            VkStencilOp failOp;
            VkStencilOp passOp;
            VkStencilOp depthFailOp;
            VkCompareOp compareOp;
        } setStencilOp;
        struct
        {
            std::uint32_t patchControlPoints;
        } setPatchControlPoints;
        struct
        {
            VkLogicOp logicOp;
        } setLogicOp;
        struct
        {
            VkColorBlendEquationEXT equation;
        } setColorBlendEquation;
        struct
        {
            VkColorComponentFlags writeMask;
        } setColorWriteMask;
        struct
        {
            std::uint32_t mask;
        } setSampleMask;
        struct
        {
            VkFragmentShadingRateCombinerOpKHR combinerOps[2];
            VkExtent2D fragmentSize;
        } setFragmentShadingRate;
        struct
        {
            std::uint32_t vertexCount;
            std::uint32_t instanceCount;
            std::uint32_t firstVertex;
            std::uint32_t firstInstance;
        } draw;
        struct
        {
            std::uint32_t indexCount;
            std::uint32_t instanceCount;
            std::uint32_t firstIndex;
            std::int32_t vertexOffset;
            std::uint32_t firstInstance;
        } drawIndexed;
        struct
        {
            VkBuffer buffer;
            VkDeviceSize offset;
            std::uint32_t drawCount;
            std::uint32_t stride;
        } drawIndirect;
        struct
        {
            VkBuffer buffer;
            VkDeviceSize offset;
            VkBuffer countBuffer;
            VkDeviceSize countOffset;
            std::uint32_t maxDrawCount;
            std::uint32_t stride;
        } drawIndirectCount;
        struct
        {
            std::uint32_t groupCountX;
            std::uint32_t groupCountY;
            std::uint32_t groupCountZ;
        } dispatch;
        struct
        {
            VkBuffer buffer;
            VkDeviceSize offset;
        } dispatchIndirect;
        struct
        {
            std::uint32_t count;
            VkCommandBuffer commandBuffers[8];
        } execute;
        struct
        {
            VkQueryPool pool;
            std::uint32_t query;
            VkQueryControlFlags flags;
        } beginQuery;
        struct
        {
            VkQueryPool pool;
            std::uint32_t query;
        } endQuery;
        struct
        {
            VkQueryPool pool;
            std::uint32_t query;
            VkPipelineStageFlags2 stage;
        } writeTimestamp;
        struct
        {
            VkQueryPool pool;
            std::uint32_t first;
            std::uint32_t count;
            VkQueryResultFlags flags;
            VkBuffer buffer;
            VkDeviceSize offset;
            VkDeviceSize stride;
        } copyQueryResults;
        struct
        {
            VkBuffer buffer;
            VkDeviceSize offset;
            VkConditionalRenderingFlagsEXT flags;
        } beginConditionalRendering;
        struct
        {
            std::uint32_t count;
            VkBuffer buffers[4];
            VkDeviceSize offsets[4];
        } beginTransformFeedback;
        struct
        {
            std::uint32_t attachmentCount;
            const VkClearAttachment *attachments;
            std::uint32_t rectCount;
            const VkClearRect *rects;
        } clearAttachments;
        struct
        {
            VkDependencyInfo dependency;
        } barrier;
        struct
        {
            std::uint8_t bytes[256];
        } generic;
    } u{};
};

// append-only 指令流。
class CommandStream
{
  public:
    void add(Cmd cmd) { cmds_.push_back(cmd); }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return cmds_.size(); }
    [[nodiscard]] constexpr bool empty() const noexcept { return cmds_.empty(); }
    [[nodiscard]] constexpr const Cmd &operator[](std::size_t i) const noexcept
    {
        return cmds_[i];
    }
    [[nodiscard]] constexpr const std::vector<Cmd> &commands() const noexcept
    {
        return cmds_;
    }

    void clear() noexcept { cmds_.clear(); }

  private:
    std::vector<Cmd> cmds_{};
};

} // namespace pass_data
