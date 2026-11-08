#pragma once
// ============================================================================
// test_intent7_pass_opcode.hpp
//
// The horizontal half of the three-axis model: the linear OpStream.
//
// Opcode roles follow the Vulkan Command Properties "Command Type":
//   Control      BeginRendering / EndRendering
//   State        BindPipeline / BindDescriptorSet / PushConstants / SetViewport / SetScissor
//   Action       DrawIndexedIndirect (and future Draw* variants)
//   Sync         Barrier (and future query/event/conditional/TF)
//   Indirection  ExecuteCommands (reserved)
//
// Every Op carries (opcode, kind, scope, lifecycle, payload).  The emitter maps
// opcode+kind to an explicit vkCmd*; dump() prints the same sequence as C-like
// pseudocode so it can be compared line-by-line with hand-written Vulkan.
// ============================================================================

#include "test_intent7_pass_data.hpp"

#include <array>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace pass_data
{

enum class Opcode : std::uint16_t
{
    BeginRendering,
    EndRendering,
    BindPipeline,
    BindDescriptorSet,
    BindDescriptorBuffer,
    BindResourceHeap,
    BindSamplerHeap,
    PushConstants,
    SetViewport,
    SetScissor,
    SetDepthTestEnable,
    SetDepthWriteEnable,
    SetCullMode,
    SetFrontFace,
    SetPrimitiveTopology,
    SetStencilReference,
    BindIndexBuffer,
    Draw,
    DrawIndexed,
    DrawIndirect,
    DrawIndexedIndirect,
    CopyImageToBuffer,
    CopyBuffer,
    CopyBufferToImage,
    CopyImage,
    BlitImage,
    FillBuffer,
    UpdateBuffer,
    ClearColorImage,
    ClearDepthStencilImage,
    Dispatch,
    DispatchIndirect,
    Barrier,
    ClearAttachments,
    BeginQuery,
    EndQuery,
    ExecuteCommands,
};

enum class OpKind : std::uint16_t
{
    None,
    Viewport,
    Scissor,
    DrawIndexedIndirect,
    BarrierMemory,
};

enum class Scope : std::uint8_t
{
    Inside,
    Outside,
    Both,
};

enum class Lifecycle : std::uint8_t
{
    Device,
    Frame,
    Pass,
    CommandBuffer,
    Draw,
    Sync,
    Inheritance,
};

struct BeginRenderingPayload
{
    PassTarget target{};
};

struct BindPipelinePayload
{
    VkPipelineBindPoint bindPoint{VK_PIPELINE_BIND_POINT_GRAPHICS};
    VkPipeline pipeline{};
    VkPipelineLayout layout{};
};

struct BindDescriptorSetPayload
{
    VkPipelineBindPoint bindPoint{VK_PIPELINE_BIND_POINT_GRAPHICS};
    VkPipelineLayout layout{};
    VkDescriptorSet set{};
};

struct BindDescriptorBufferPayload
{
    VkPipelineLayout layout{};
    VkDescriptorBufferBindingInfoEXT binding{};
    VkDeviceSize offset{0};
};

struct BindHeapPayload
{
    VkBindHeapInfoEXT info{};
    bool sampler{false};
};

struct PushConstantsPayload
{
    VkPipelineLayout layout{};
    VkShaderStageFlags stages{0};
    std::uint32_t offset{0};
    std::uint32_t size{0};
    std::array<std::uint8_t, PushConstants::MAX_BYTES> data{};
};

struct SetViewportPayload
{
    VkViewport viewport{};
};

struct SetScissorPayload
{
    VkRect2D scissor{};
};

struct SetBoolPayload
{
    VkBool32 enable{VK_FALSE};
};

struct SetCullModePayload
{
    VkCullModeFlags cullMode{0};
};

struct SetFrontFacePayload
{
    VkFrontFace frontFace{VK_FRONT_FACE_COUNTER_CLOCKWISE};
};

struct SetPrimitiveTopologyPayload
{
    VkPrimitiveTopology topology{VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
};

struct SetStencilReferencePayload
{
    VkStencilFaceFlags faceMask{VK_STENCIL_FACE_FRONT_AND_BACK};
    std::uint32_t reference{0};
};

struct BindIndexBufferPayload
{
    VkBuffer buffer{};
    VkDeviceSize offset{0};
    VkIndexType type{VK_INDEX_TYPE_UINT32};
};

struct DrawPayload
{
    std::uint32_t vertexCount{0};
    std::uint32_t instanceCount{1};
    std::uint32_t firstVertex{0};
    std::uint32_t firstInstance{0};
};

struct DrawIndexedPayload
{
    std::uint32_t indexCount{0};
    std::uint32_t instanceCount{1};
    std::uint32_t firstIndex{0};
    std::int32_t vertexOffset{0};
    std::uint32_t firstInstance{0};
};

struct DrawIndirectPayload
{
    VkBuffer buffer{};
    VkDeviceSize offset{0};
    std::uint32_t drawCount{0};
    std::uint32_t stride{0};
};

struct DrawIndexedIndirectPayload
{
    VkBuffer buffer{};
    VkDeviceSize offset{0};
    std::uint32_t drawCount{0};
    std::uint32_t stride{0};
};

struct CopyImageToBufferPayload
{
    inline static constexpr std::uint32_t CAPACITY = 4;

    VkImage image{};
    VkImageLayout imageLayout{VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
    VkBuffer buffer{};
    std::array<VkBufferImageCopy, CAPACITY> regions{};
    std::uint32_t regionCount{0};
};

struct CopyBufferPayload
{
    inline static constexpr std::uint32_t CAPACITY = 4;

    VkBuffer src{};
    VkBuffer dst{};
    std::array<VkBufferCopy, CAPACITY> regions{};
    std::uint32_t regionCount{0};
};

struct CopyBufferToImagePayload
{
    VkBuffer src{};
    VkImage dst{};
    VkImageLayout dstLayout{VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
    std::vector<VkBufferImageCopy> regions{};
};

struct CopyImagePayload
{
    VkImage src{};
    VkImageLayout srcLayout{VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
    VkImage dst{};
    VkImageLayout dstLayout{VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
    std::vector<VkImageCopy> regions{};
};

struct BlitImagePayload
{
    inline static constexpr std::uint32_t CAPACITY = 4;

    VkImage src{};
    VkImageLayout srcLayout{VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
    VkImage dst{};
    VkImageLayout dstLayout{VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
    std::array<VkImageBlit, CAPACITY> regions{};
    std::uint32_t regionCount{0};
    VkFilter filter{VK_FILTER_LINEAR};
};

struct FillBufferPayload
{
    VkBuffer buffer{};
    VkDeviceSize offset{0};
    VkDeviceSize size{0};
    std::uint32_t data{0};
};

struct UpdateBufferPayload
{
    VkBuffer buffer{};
    VkDeviceSize offset{0};
    std::vector<std::uint8_t> data{};
};

struct ClearColorImagePayload
{
    VkImage image{};
    VkImageLayout layout{VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
    VkClearColorValue color{};
    std::vector<VkImageSubresourceRange> ranges{};
};

struct ClearDepthStencilImagePayload
{
    VkImage image{};
    VkImageLayout layout{VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
    VkClearDepthStencilValue value{};
    std::vector<VkImageSubresourceRange> ranges{};
};

struct DispatchPayload
{
    std::uint32_t x{1};
    std::uint32_t y{1};
    std::uint32_t z{1};
};

struct DispatchIndirectPayload
{
    VkBuffer buffer{};
    VkDeviceSize offset{0};
};

struct ClearAttachmentsPayload
{
    std::vector<VkClearAttachment> attachments{};
    std::vector<VkClearRect> rects{};
};

struct QueryPayload
{
    VkQueryPool pool{};
    std::uint32_t query{0};
    VkQueryControlFlags flags{0};
};

struct ExecuteCommandsPayload
{
    inline static constexpr std::uint32_t CAPACITY = 4;

    std::array<VkCommandBuffer, CAPACITY> commandBuffers{};
    std::uint32_t commandBufferCount{0};
    InheritanceRenderingInfo inheritance{};
};

struct BarrierPayload
{
    inline static constexpr std::uint32_t CAPACITY = 8;

    // Inline small-buffer storage: a pass boundary or a layout transition
    // normally carries one barrier. No heap allocation for the common case.
    VkDependencyFlags dependencyFlags{0};
    std::array<VkMemoryBarrier2, CAPACITY> memoryBarriers{};
    std::array<VkBufferMemoryBarrier2, CAPACITY> bufferBarriers{};
    std::array<VkImageMemoryBarrier2, CAPACITY> imageBarriers{};
    std::uint32_t memoryBarrierCount{0};
    std::uint32_t bufferBarrierCount{0};
    std::uint32_t imageBarrierCount{0};
};

using OpPayload = std::variant<std::monostate, BeginRenderingPayload,
                               BindPipelinePayload, BindDescriptorSetPayload,
                               BindDescriptorBufferPayload, BindHeapPayload,
                               PushConstantsPayload,
                               SetViewportPayload, SetScissorPayload,
                               SetBoolPayload, SetCullModePayload,
                               SetFrontFacePayload, SetPrimitiveTopologyPayload,
                               SetStencilReferencePayload,
                               BindIndexBufferPayload, DrawPayload,
                               DrawIndexedPayload, DrawIndirectPayload,
                               DrawIndexedIndirectPayload,
                               CopyImageToBufferPayload, CopyBufferPayload,
                               CopyBufferToImagePayload, CopyImagePayload,
                               BlitImagePayload,
                               FillBufferPayload, UpdateBufferPayload,
                               ClearColorImagePayload, ClearDepthStencilImagePayload,
                               DispatchPayload, DispatchIndirectPayload,
                               ClearAttachmentsPayload, QueryPayload,
                               ExecuteCommandsPayload, BarrierPayload>;

struct Op
{
    Opcode opcode{Opcode::BeginRendering};
    OpKind kind{OpKind::None};
    Scope scope{Scope::Inside};
    Lifecycle lifecycle{Lifecycle::Pass};
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
    [[nodiscard]] const std::vector<Op> &commands() const noexcept { return ops_; }

    void clear() noexcept { ops_.clear(); }

  private:
    std::vector<Op> ops_{};
};

inline const char *to_string(Opcode opcode) noexcept
{
    switch (opcode)
    {
    case Opcode::BeginRendering:
        return "vkCmdBeginRendering";
    case Opcode::EndRendering:
        return "vkCmdEndRendering";
    case Opcode::BindPipeline:
        return "vkCmdBindPipeline";
    case Opcode::BindDescriptorSet:
        return "vkCmdBindDescriptorSets";
    case Opcode::BindDescriptorBuffer:
        return "vkCmdBindDescriptorBuffersEXT";
    case Opcode::BindResourceHeap:
        return "vkCmdBindResourceHeapEXT";
    case Opcode::BindSamplerHeap:
        return "vkCmdBindSamplerHeapEXT";
    case Opcode::PushConstants:
        return "vkCmdPushConstants";
    case Opcode::SetViewport:
        return "vkCmdSetViewport";
    case Opcode::SetScissor:
        return "vkCmdSetScissor";
    case Opcode::SetDepthTestEnable:
        return "vkCmdSetDepthTestEnable";
    case Opcode::SetDepthWriteEnable:
        return "vkCmdSetDepthWriteEnable";
    case Opcode::SetCullMode:
        return "vkCmdSetCullMode";
    case Opcode::SetFrontFace:
        return "vkCmdSetFrontFace";
    case Opcode::SetPrimitiveTopology:
        return "vkCmdSetPrimitiveTopology";
    case Opcode::SetStencilReference:
        return "vkCmdSetStencilReference";
    case Opcode::BindIndexBuffer:
        return "vkCmdBindIndexBuffer";
    case Opcode::Draw:
        return "vkCmdDraw";
    case Opcode::DrawIndexed:
        return "vkCmdDrawIndexed";
    case Opcode::DrawIndirect:
        return "vkCmdDrawIndirect";
    case Opcode::DrawIndexedIndirect:
        return "vkCmdDrawIndexedIndirect";
    case Opcode::CopyImageToBuffer:
        return "vkCmdCopyImageToBuffer";
    case Opcode::CopyBuffer:
        return "vkCmdCopyBuffer";
    case Opcode::CopyBufferToImage:
        return "vkCmdCopyBufferToImage";
    case Opcode::CopyImage:
        return "vkCmdCopyImage";
    case Opcode::BlitImage:
        return "vkCmdBlitImage";
    case Opcode::FillBuffer:
        return "vkCmdFillBuffer";
    case Opcode::UpdateBuffer:
        return "vkCmdUpdateBuffer";
    case Opcode::ClearColorImage:
        return "vkCmdClearColorImage";
    case Opcode::ClearDepthStencilImage:
        return "vkCmdClearDepthStencilImage";
    case Opcode::Dispatch:
        return "vkCmdDispatch";
    case Opcode::DispatchIndirect:
        return "vkCmdDispatchIndirect";
    case Opcode::ClearAttachments:
        return "vkCmdClearAttachments";
    case Opcode::BeginQuery:
        return "vkCmdBeginQuery";
    case Opcode::EndQuery:
        return "vkCmdEndQuery";
    case Opcode::Barrier:
        return "vkCmdPipelineBarrier2";
    case Opcode::ExecuteCommands:
        return "vkCmdExecuteCommands";
    }
    return "vkCmdUnknown";
}

// [pass ... pass] -> [Vulkan C API list]
// This is the bottom-layer contract: every Opcode must map to one official
// vkCmd* entry point; no abstract-only operation is allowed.
inline constexpr std::size_t OPCODE_COUNT =
    static_cast<std::size_t>(Opcode::ExecuteCommands) + 1;

inline std::string_view c_api_name(Opcode opcode) noexcept
{
    return to_string(opcode);
}

inline std::vector<std::string> c_api_list(const OpStream &stream)
{
    std::vector<std::string> calls;
    calls.reserve(stream.size());
    for (const auto &op : stream.commands())
        calls.emplace_back(c_api_name(op.opcode));
    return calls;
}

inline bool c_api_coverage_ok() noexcept
{
    for (std::size_t i = 0; i < OPCODE_COUNT; ++i)
    {
        const auto name = c_api_name(static_cast<Opcode>(i));
        if (name.empty() || name == "vkCmdUnknown")
            return false;
    }
    return true;
}

inline const char *to_string(Scope scope) noexcept
{
    switch (scope)
    {
    case Scope::Inside:
        return "Inside";
    case Scope::Outside:
        return "Outside";
    case Scope::Both:
        return "Both";
    }
    return "?";
}

inline const char *to_string(Lifecycle lifecycle) noexcept
{
    switch (lifecycle)
    {
    case Lifecycle::Device:
        return "Device";
    case Lifecycle::Frame:
        return "Frame";
    case Lifecycle::Pass:
        return "Pass";
    case Lifecycle::CommandBuffer:
        return "CommandBuffer";
    case Lifecycle::Draw:
        return "Draw";
    case Lifecycle::Sync:
        return "Sync";
    case Lifecycle::Inheritance:
        return "Inheritance";
    }
    return "?";
}

inline std::string dump(const Op &op)
{
    std::ostringstream os;
    os << "// [" << to_string(op.scope) << " / " << to_string(op.lifecycle)
       << "] " << to_string(op.opcode) << "\n";

    switch (op.opcode)
    {
    case Opcode::BeginRendering: {
        const auto &p = std::get<BeginRenderingPayload>(op.payload);
        os << "vkCmdBeginRendering(cmd, &rendering_info{"
           << "area=" << p.target.renderArea.extent.width << "x"
           << p.target.renderArea.extent.height
           << ", colorCount=" << p.target.colorCount
           << ", hasDepth=" << (p.target.hasDepth ? "true" : "false") << "});";
        break;
    }
    case Opcode::EndRendering:
        os << "vkCmdEndRendering(cmd);";
        break;
    case Opcode::BindPipeline: {
        const auto &p = std::get<BindPipelinePayload>(op.payload);
        os << "vkCmdBindPipeline(cmd, "
           << (p.bindPoint == VK_PIPELINE_BIND_POINT_COMPUTE
                   ? "VK_PIPELINE_BIND_POINT_COMPUTE"
                   : "VK_PIPELINE_BIND_POINT_GRAPHICS")
           << ", pipeline=" << p.pipeline << ");";
        break;
    }
    case Opcode::BindDescriptorSet: {
        const auto &p = std::get<BindDescriptorSetPayload>(op.payload);
        os << "vkCmdBindDescriptorSets(cmd, "
           << (p.bindPoint == VK_PIPELINE_BIND_POINT_COMPUTE
                   ? "VK_PIPELINE_BIND_POINT_COMPUTE"
                   : "VK_PIPELINE_BIND_POINT_GRAPHICS")
           << ", layout=" << p.layout << ", 0, 1, &set=" << p.set
           << ", 0, nullptr);";
        break;
    }
    case Opcode::BindDescriptorBuffer: {
        const auto &p = std::get<BindDescriptorBufferPayload>(op.payload);
        os << "vkCmdBindDescriptorBuffersEXT(cmd, 1, &binding{address="
           << p.binding.address << "}); vkCmdSetDescriptorBufferOffsetsEXT(cmd, "
           << "VK_PIPELINE_BIND_POINT_GRAPHICS, layout=" << p.layout
           << ", 0, 1, &index=0, &offset=" << p.offset << ");";
        break;
    }
    case Opcode::BindResourceHeap:
    case Opcode::BindSamplerHeap: {
        const auto &p = std::get<BindHeapPayload>(op.payload);
        os << (p.sampler ? "vkCmdBindSamplerHeapEXT" : "vkCmdBindResourceHeapEXT")
           << "(cmd, &heap{address=" << p.info.heapRange.address
           << ", size=" << p.info.heapRange.size << "});";
        break;
    }
    case Opcode::PushConstants: {
        const auto &p = std::get<PushConstantsPayload>(op.payload);
        os << "vkCmdPushConstants(cmd, layout=" << p.layout << ", stages="
           << p.stages << ", offset=" << p.offset << ", size=" << p.size
           << ", data);";
        break;
    }
    case Opcode::SetViewport: {
        const auto &p = std::get<SetViewportPayload>(op.payload);
        os << "vkCmdSetViewport(cmd, 0, 1, &viewport{"
           << p.viewport.x << "," << p.viewport.y << "," << p.viewport.width << ","
           << p.viewport.height << "});";
        break;
    }
    case Opcode::SetScissor: {
        const auto &p = std::get<SetScissorPayload>(op.payload);
        os << "vkCmdSetScissor(cmd, 0, 1, &scissor{{"
           << p.scissor.offset.x << "," << p.scissor.offset.y << "},{"
           << p.scissor.extent.width << "," << p.scissor.extent.height << "}});";
        break;
    }
    case Opcode::SetDepthTestEnable:
        os << "vkCmdSetDepthTestEnable(cmd, "
           << (std::get<SetBoolPayload>(op.payload).enable ? "VK_TRUE" : "VK_FALSE")
           << ");";
        break;
    case Opcode::SetDepthWriteEnable:
        os << "vkCmdSetDepthWriteEnable(cmd, "
           << (std::get<SetBoolPayload>(op.payload).enable ? "VK_TRUE" : "VK_FALSE")
           << ");";
        break;
    case Opcode::SetCullMode:
        os << "vkCmdSetCullMode(cmd, "
           << std::get<SetCullModePayload>(op.payload).cullMode << ");";
        break;
    case Opcode::SetFrontFace:
        os << "vkCmdSetFrontFace(cmd, "
           << std::get<SetFrontFacePayload>(op.payload).frontFace << ");";
        break;
    case Opcode::SetPrimitiveTopology:
        os << "vkCmdSetPrimitiveTopology(cmd, "
           << std::get<SetPrimitiveTopologyPayload>(op.payload).topology << ");";
        break;
    case Opcode::SetStencilReference: {
        const auto &p = std::get<SetStencilReferencePayload>(op.payload);
        os << "vkCmdSetStencilReference(cmd, " << p.faceMask << ", " << p.reference
           << ");";
        break;
    }
    case Opcode::BindIndexBuffer: {
        const auto &p = std::get<BindIndexBufferPayload>(op.payload);
        os << "vkCmdBindIndexBuffer(cmd, buffer=" << p.buffer
           << ", offset=" << p.offset << ", type=" << p.type << ");";
        break;
    }
    case Opcode::Draw: {
        const auto &p = std::get<DrawPayload>(op.payload);
        os << "vkCmdDraw(cmd, " << p.vertexCount << ", " << p.instanceCount << ", "
           << p.firstVertex << ", " << p.firstInstance << ");";
        break;
    }
    case Opcode::DrawIndexed: {
        const auto &p = std::get<DrawIndexedPayload>(op.payload);
        os << "vkCmdDrawIndexed(cmd, " << p.indexCount << ", " << p.instanceCount
           << ", " << p.firstIndex << ", " << p.vertexOffset << ", "
           << p.firstInstance << ");";
        break;
    }
    case Opcode::DrawIndirect: {
        const auto &p = std::get<DrawIndirectPayload>(op.payload);
        os << "vkCmdDrawIndirect(cmd, buffer=" << p.buffer << ", offset=" << p.offset
           << ", drawCount=" << p.drawCount << ", stride=" << p.stride << ");";
        break;
    }
    case Opcode::DrawIndexedIndirect: {
        const auto &p = std::get<DrawIndexedIndirectPayload>(op.payload);
        os << "vkCmdDrawIndexedIndirect(cmd, buffer=" << p.buffer
           << ", offset=" << p.offset << ", drawCount=" << p.drawCount
           << ", stride=" << p.stride << ");";
        break;
    }
    case Opcode::CopyImageToBuffer: {
        const auto &p = std::get<CopyImageToBufferPayload>(op.payload);
        os << "vkCmdCopyImageToBuffer(cmd, image=" << p.image
           << ", layout=" << p.imageLayout << ", buffer=" << p.buffer
           << ", regions=" << p.regionCount << ");";
        break;
    }
    case Opcode::CopyBuffer: {
        const auto &p = std::get<CopyBufferPayload>(op.payload);
        os << "vkCmdCopyBuffer(cmd, src=" << p.src << ", dst=" << p.dst
           << ", regions=" << p.regionCount << ");";
        break;
    }
    case Opcode::CopyBufferToImage: {
        const auto &p = std::get<CopyBufferToImagePayload>(op.payload);
        os << "vkCmdCopyBufferToImage(cmd, src=" << p.src << ", dst=" << p.dst
           << ", regions=" << p.regions.size() << ");";
        break;
    }
    case Opcode::CopyImage: {
        const auto &p = std::get<CopyImagePayload>(op.payload);
        os << "vkCmdCopyImage(cmd, src=" << p.src << ", dst=" << p.dst
           << ", regions=" << p.regions.size() << ");";
        break;
    }
    case Opcode::BlitImage: {
        const auto &p = std::get<BlitImagePayload>(op.payload);
        os << "vkCmdBlitImage(cmd, src=" << p.src << ", dst=" << p.dst
           << ", regions=" << p.regionCount << ", filter=" << p.filter << ");";
        break;
    }
    case Opcode::FillBuffer: {
        const auto &p = std::get<FillBufferPayload>(op.payload);
        os << "vkCmdFillBuffer(cmd, buffer=" << p.buffer << ", offset=" << p.offset
           << ", size=" << p.size << ", data=" << p.data << ");";
        break;
    }
    case Opcode::UpdateBuffer: {
        const auto &p = std::get<UpdateBufferPayload>(op.payload);
        os << "vkCmdUpdateBuffer(cmd, buffer=" << p.buffer << ", offset=" << p.offset
           << ", size=" << p.data.size() << ");";
        break;
    }
    case Opcode::ClearColorImage: {
        const auto &p = std::get<ClearColorImagePayload>(op.payload);
        os << "vkCmdClearColorImage(cmd, image=" << p.image << ", ranges="
           << p.ranges.size() << ");";
        break;
    }
    case Opcode::ClearDepthStencilImage: {
        const auto &p = std::get<ClearDepthStencilImagePayload>(op.payload);
        os << "vkCmdClearDepthStencilImage(cmd, image=" << p.image << ", ranges="
           << p.ranges.size() << ");";
        break;
    }
    case Opcode::Dispatch: {
        const auto &p = std::get<DispatchPayload>(op.payload);
        os << "vkCmdDispatch(cmd, " << p.x << ", " << p.y << ", " << p.z << ");";
        break;
    }
    case Opcode::DispatchIndirect: {
        const auto &p = std::get<DispatchIndirectPayload>(op.payload);
        os << "vkCmdDispatchIndirect(cmd, buffer=" << p.buffer
           << ", offset=" << p.offset << ");";
        break;
    }
    case Opcode::Barrier: {
        const auto &p = std::get<BarrierPayload>(op.payload);
        os << "vkCmdPipelineBarrier2(cmd, &dependency{memory="
           << p.memoryBarrierCount << ", buffer=" << p.bufferBarrierCount
           << ", image=" << p.imageBarrierCount << "});";
        break;
    }
    case Opcode::ClearAttachments: {
        const auto &p = std::get<ClearAttachmentsPayload>(op.payload);
        os << "vkCmdClearAttachments(cmd, " << p.attachments.size()
           << " attachments, " << p.rects.size() << " rects);";
        break;
    }
    case Opcode::BeginQuery: {
        const auto &p = std::get<QueryPayload>(op.payload);
        os << "vkCmdBeginQuery(cmd, pool=" << p.pool << ", query=" << p.query
           << ", flags=" << p.flags << ");";
        break;
    }
    case Opcode::EndQuery: {
        const auto &p = std::get<QueryPayload>(op.payload);
        os << "vkCmdEndQuery(cmd, pool=" << p.pool << ", query=" << p.query << ");";
        break;
    }
    case Opcode::ExecuteCommands: {
        const auto &p = std::get<ExecuteCommandsPayload>(op.payload);
        os << "vkCmdExecuteCommands(cmd, " << p.commandBufferCount
           << " secondary command buffers);";
        break;
    }
    }
    return os.str();
}

inline std::string dump(const OpStream &stream)
{
    std::ostringstream os;
    for (std::size_t i = 0; i < stream.size(); ++i)
    {
        os << "#" << i << " " << dump(stream[i]) << "\n";
    }
    return os.str();
}

} // namespace pass_data
