#pragma once
// ============================================================================
// test_intent8_draw_ops.hpp
//
// O = operation. The recorded command program is a linear list of these ops.
//
// Every op maps 1:1 to one official Vulkan C entry point:
//
//   vkCmdBeginRendering / vkCmdBeginRenderPass2
//   vkCmdBindPipeline
//   vkCmdBindDescriptorSets
//   vkCmdPushConstants
//   vkCmdSetViewport / vkCmdSetScissor
//   vkCmdBindIndexBuffer / vkCmdBindVertexBuffers
//   vkCmdDraw / vkCmdDrawIndexed / vkCmdDrawIndirect /
//   vkCmdDrawIndexedIndirect / vkCmdDrawIndexedIndirectCount
//   vkCmdPipelineBarrier2
//   vkCmdFillBuffer / vkCmdCopyBuffer / vkCmdCopyImageToBuffer
//   vkCmdExecuteCommands
//   vkCmdEndRendering / vkCmdEndRenderPass2
//
// c_api_list() walks exactly this list; the emitter walks exactly this list.
// There is no second metadata table and no synthetic call.
// ============================================================================

#include "test_intent8_draw_pipeline.hpp"

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace intent8
{

enum class OpCode : std::uint16_t
{
    BeginTarget,
    NextSubpass,
    EndTarget,
    BindPipeline,
    BindDescriptorSet,
    PushConstants,
    SetViewport,
    SetScissor,
    BindIndexBuffer,
    BindVertexBuffers,
    Draw,
    Barrier,
    FillBuffer,
    CopyBuffer,
    CopyImageToBuffer,
    ExecuteCommands,
};

inline constexpr std::size_t OPCODE_COUNT =
    static_cast<std::size_t>(OpCode::ExecuteCommands) + 1;

// The target is stored by value: clear values, formats, render area and
// attachment configuration must outlive emission, and the op stream is the
// owner that guarantees it.
struct BeginTargetPayload
{
    RenderTarget target{};
};

struct NextSubpassPayload
{
    VkSubpassContents contents{VK_SUBPASS_CONTENTS_INLINE};
};

struct EndTargetPayload
{
    RenderTarget target{};
};

struct BindPipelinePayload
{
    const DrawPipeline *pipeline{nullptr};
    VkPipelineBindPoint bindPoint{VK_PIPELINE_BIND_POINT_GRAPHICS};
};

struct BindDescriptorSetPayload
{
    VkPipelineLayout layout{};
    VkDescriptorSet set{};
    std::uint32_t firstSet{0};
};

struct PushConstantsPayload
{
    VkPipelineLayout layout{};
    VkShaderStageFlags stages{0};
    std::uint32_t offset{0};
    std::uint32_t size{0};
    std::array<std::uint8_t, MAX_PUSH_CONSTANT_BYTES> data{};
};

struct SetViewportPayload
{
    VkViewport viewport{};
};

struct SetScissorPayload
{
    VkRect2D scissor{};
};

struct BindIndexBufferPayload
{
    VkBuffer buffer{};
    VkDeviceSize offset{0};
    VkIndexType type{VK_INDEX_TYPE_UINT32};
};

struct BindVertexBuffersPayload
{
    std::uint32_t firstBinding{0};
    std::uint32_t bindingCount{0};
    std::array<VkBuffer, MAX_VERTEX_BINDINGS> buffers{};
    std::array<VkDeviceSize, MAX_VERTEX_BINDINGS> offsets{};
};

struct DrawPayload
{
    DrawArgs args{};
};

struct BarrierPayload
{
    VkDependencyFlags dependencyFlags{0};
    std::array<VkMemoryBarrier2, MAX_BARRIERS> memoryBarriers{};
    std::array<VkBufferMemoryBarrier2, MAX_BARRIERS> bufferBarriers{};
    std::array<VkImageMemoryBarrier2, MAX_BARRIERS> imageBarriers{};
    std::uint32_t memoryBarrierCount{0};
    std::uint32_t bufferBarrierCount{0};
    std::uint32_t imageBarrierCount{0};
};

struct FillBufferPayload
{
    VkBuffer buffer{};
    VkDeviceSize offset{0};
    VkDeviceSize size{0};
    std::uint32_t data{0};
};

struct CopyBufferPayload
{
    VkBuffer src{};
    VkBuffer dst{};
    std::array<VkBufferCopy, MAX_BARRIERS> regions{};
    std::uint32_t regionCount{0};
};

struct CopyImageToBufferPayload
{
    VkImage image{};
    VkImageLayout imageLayout{VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
    VkBuffer buffer{};
    std::array<VkBufferImageCopy, MAX_BARRIERS> regions{};
    std::uint32_t regionCount{0};
};

struct ExecuteCommandsPayload
{
    std::array<VkCommandBuffer, MAX_BARRIERS> commandBuffers{};
    std::uint32_t commandBufferCount{0};
};

using OpPayload =
    std::variant<std::monostate, BeginTargetPayload, NextSubpassPayload,
                 EndTargetPayload,
                 BindPipelinePayload, BindDescriptorSetPayload, PushConstantsPayload,
                 SetViewportPayload, SetScissorPayload, BindIndexBufferPayload,
                 BindVertexBuffersPayload, DrawPayload, BarrierPayload,
                 FillBufferPayload, CopyBufferPayload, CopyImageToBufferPayload,
                 ExecuteCommandsPayload>;

struct Op
{
    OpCode code{OpCode::BeginTarget};
    std::uint32_t passIndex{0};
    OpPayload payload{};
};

class OpStream
{
  public:
    void reserve(std::size_t count) { ops_.reserve(count); }
    void add(Op op) { ops_.push_back(std::move(op)); }

    [[nodiscard]] bool empty() const noexcept { return ops_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return ops_.size(); }
    [[nodiscard]] const Op &operator[](std::size_t i) const noexcept { return ops_[i]; }
    [[nodiscard]] const std::vector<Op> &ops() const noexcept { return ops_; }

    void clear() noexcept { ops_.clear(); }

  private:
    std::vector<Op> ops_{};
};

// ---------------------------------------------------------------------------
// C API list: a projection of the recorded ops, never a separate registry.
// ---------------------------------------------------------------------------
inline const char *c_api_name(const Op &op) noexcept
{
    switch (op.code)
    {
    case OpCode::BeginTarget: {
        const auto *p = std::get_if<BeginTargetPayload>(&op.payload);
        if (p != nullptr && p->target.config.kind == TargetKind::ClassicRenderPass)
            return "vkCmdBeginRenderPass2";
        return "vkCmdBeginRendering";
    }
    case OpCode::NextSubpass:
        return "vkCmdNextSubpass2";
    case OpCode::EndTarget: {
        const auto *p = std::get_if<EndTargetPayload>(&op.payload);
        if (p != nullptr && p->target.config.kind == TargetKind::ClassicRenderPass)
            return "vkCmdEndRenderPass2";
        return "vkCmdEndRendering";
    }
    case OpCode::BindPipeline:
        return "vkCmdBindPipeline";
    case OpCode::BindDescriptorSet:
        return "vkCmdBindDescriptorSets";
    case OpCode::PushConstants:
        return "vkCmdPushConstants";
    case OpCode::SetViewport:
        return "vkCmdSetViewport";
    case OpCode::SetScissor:
        return "vkCmdSetScissor";
    case OpCode::BindIndexBuffer:
        return "vkCmdBindIndexBuffer";
    case OpCode::BindVertexBuffers:
        return "vkCmdBindVertexBuffers";
    case OpCode::Draw: {
        const auto &p = std::get<DrawPayload>(op.payload);
        switch (p.args.kind)
        {
        case DrawKind::Draw:
            return "vkCmdDraw";
        case DrawKind::DrawIndexed:
            return "vkCmdDrawIndexed";
        case DrawKind::DrawIndirect:
            return "vkCmdDrawIndirect";
        case DrawKind::DrawIndexedIndirect:
            return "vkCmdDrawIndexedIndirect";
        case DrawKind::DrawIndexedIndirectCount:
            return "vkCmdDrawIndexedIndirectCount";
        }
        return "vkCmdDraw";
    }
    case OpCode::Barrier:
        return "vkCmdPipelineBarrier2";
    case OpCode::FillBuffer:
        return "vkCmdFillBuffer";
    case OpCode::CopyBuffer:
        return "vkCmdCopyBuffer";
    case OpCode::CopyImageToBuffer:
        return "vkCmdCopyImageToBuffer";
    case OpCode::ExecuteCommands:
        return "vkCmdExecuteCommands";
    }
    return "vkCmdUnknown";
}

inline std::vector<std::string> c_api_list(const OpStream &stream)
{
    std::vector<std::string> calls;
    calls.reserve(stream.size());
    for (const auto &op : stream.ops())
        calls.emplace_back(c_api_name(op));
    return calls;
}

inline bool c_api_coverage_ok() noexcept
{
    // Every enum value is covered by c_api_name(); the check is structural:
    // all opcodes have a non-unknown name for a well-formed op.
    OpStream probe{};
    for (std::size_t i = 0; i < OPCODE_COUNT; ++i)
    {
        Op op{};
        op.code = static_cast<OpCode>(i);
        if (op.code == OpCode::Draw)
            op.payload = DrawPayload{};
        if (std::string_view(c_api_name(op)) == "vkCmdUnknown")
            return false;
    }
    return true;
}

inline std::string dump(const OpStream &stream)
{
    std::ostringstream os;
    for (std::size_t i = 0; i < stream.size(); ++i)
    {
        const Op &op = stream[i];
        os << "#" << i << " [" << c_api_name(op) << "] ";
        switch (op.code)
        {
        case OpCode::BeginTarget: {
            const auto &p = std::get<BeginTargetPayload>(op.payload);
            os << "area=" << p.target.config.renderArea.extent.width << "x"
               << p.target.config.renderArea.extent.height
               << " colors=" << p.target.colorCount
               << " depth=" << (p.target.hasDepth ? 1 : 0);
            break;
        }
        case OpCode::NextSubpass: {
            const auto &p = std::get<NextSubpassPayload>(op.payload);
            os << "contents=" << p.contents;
            break;
        }
        case OpCode::EndTarget: {
            const auto &p = std::get<EndTargetPayload>(op.payload);
            os << "area=" << p.target.config.renderArea.extent.width << "x"
               << p.target.config.renderArea.extent.height;
            break;
        }
        case OpCode::BindPipeline: {
            const auto &p = std::get<BindPipelinePayload>(op.payload);
            os << "pipeline=" << (p.pipeline == nullptr ? VK_NULL_HANDLE
                                                        : p.pipeline->pipeline());
            break;
        }
        case OpCode::BindDescriptorSet: {
            const auto &p = std::get<BindDescriptorSetPayload>(op.payload);
            os << "layout=" << p.layout << " firstSet=" << p.firstSet
               << " set=" << p.set;
            break;
        }
        case OpCode::PushConstants: {
            const auto &p = std::get<PushConstantsPayload>(op.payload);
            os << "layout=" << p.layout << " stages=" << p.stages
               << " offset=" << p.offset << " size=" << p.size;
            break;
        }
        case OpCode::SetViewport: {
            const auto &p = std::get<SetViewportPayload>(op.payload);
            os << p.viewport.x << "," << p.viewport.y << " " << p.viewport.width
               << "x" << p.viewport.height;
            break;
        }
        case OpCode::SetScissor: {
            const auto &p = std::get<SetScissorPayload>(op.payload);
            os << p.scissor.offset.x << "," << p.scissor.offset.y << " "
               << p.scissor.extent.width << "x" << p.scissor.extent.height;
            break;
        }
        case OpCode::BindIndexBuffer: {
            const auto &p = std::get<BindIndexBufferPayload>(op.payload);
            os << "buffer=" << p.buffer << " offset=" << p.offset
               << " type=" << p.type;
            break;
        }
        case OpCode::BindVertexBuffers: {
            const auto &p = std::get<BindVertexBuffersPayload>(op.payload);
            os << "first=" << p.firstBinding << " count=" << p.bindingCount;
            break;
        }
        case OpCode::Draw: {
            const auto &p = std::get<DrawPayload>(op.payload);
            os << "kind=" << static_cast<int>(p.args.kind)
               << " count=" << p.args.drawCount;
            break;
        }
        case OpCode::Barrier: {
            const auto &p = std::get<BarrierPayload>(op.payload);
            os << "memory=" << p.memoryBarrierCount
               << " buffer=" << p.bufferBarrierCount
               << " image=" << p.imageBarrierCount;
            break;
        }
        case OpCode::FillBuffer: {
            const auto &p = std::get<FillBufferPayload>(op.payload);
            os << "buffer=" << p.buffer << " size=" << p.size;
            break;
        }
        case OpCode::CopyBuffer: {
            const auto &p = std::get<CopyBufferPayload>(op.payload);
            os << "src=" << p.src << " dst=" << p.dst
               << " regions=" << p.regionCount;
            break;
        }
        case OpCode::CopyImageToBuffer: {
            const auto &p = std::get<CopyImageToBufferPayload>(op.payload);
            os << "image=" << p.image << " buffer=" << p.buffer
               << " regions=" << p.regionCount;
            break;
        }
        case OpCode::ExecuteCommands: {
            const auto &p = std::get<ExecuteCommandsPayload>(op.payload);
            os << "count=" << p.commandBufferCount;
            break;
        }
        }
        os << "\n";
    }
    return os.str();
}

} // namespace intent8
