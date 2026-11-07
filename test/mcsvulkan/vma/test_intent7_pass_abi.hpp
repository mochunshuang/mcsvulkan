#pragma once
// ============================================================================
// test_intent7_pass_abi.hpp
//
// Pass = PassDesc(target/load-store/subpass) + CommandStream(完整 vkCmd* 指令) +
//        FrameContext(UI 生成器的 buffers/CommandBuilder)
//
// Pass 只是指令流里的一段；[pass...pass] 是顶层。UI 的 CommandBuilder 是
// generator：它把 7 个 DAO row 生成 BindPipeline/BindDS/PushConstants/
// DrawIndexedIndirect 等 Cmd；PassCompiler 再逐条翻译成 vkCmd*。
// ============================================================================

#include "test_intent7_pass_stream.hpp"
#include "test_intent7_frame_context.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

namespace pass_data
{

enum class Target : std::uint8_t
{
    Main,
    Classic,
    Dynamic,
};

enum class LoadOp : std::uint8_t
{
    Load,
    Clear,
    DontCare,
};

enum class StoreOp : std::uint8_t
{
    Store,
    DontCare,
};

struct PassDesc
{
    Target target{Target::Main};
    LoadOp colorLoad{LoadOp::Load};
    StoreOp colorStore{StoreOp::DontCare};
    LoadOp depthLoad{LoadOp::Load};
    VkRenderingInfo dynamic{};
    VkRenderPassBeginInfo classic{};
    VkSubpassContents contents{VK_SUBPASS_CONTENTS_INLINE};
    VkRect2D renderArea{};
};

// UI 生成器的 base state；真正的状态以 stream 里的 Cmd 为准。
struct PipelineState
{
    VkPipeline pipeline{};
    VkPipelineLayout layout{};
    VkDescriptorSet set{};
    VkViewport viewport{};
    VkRect2D scissor{};
    PushData pushData{};
    mesh_data mesh{};
};

struct Pass
{
    PassDesc desc{};
    PipelineState base{};
    shader_data::FrameContext frame;
    CommandStream stream{};

    Pass(const LogicalDevice &device, VkDeviceSize vertexCapacity,
         VkDeviceSize indexCapacity, VkDeviceSize heapCapacity,
         VkDeviceSize indirectDrawCapacity, VkDeviceSize commandConstantsCapacity,
         PassDesc d, PipelineState b)
        : desc{d}, base{b},
          frame{device, vertexCapacity, indexCapacity, heapCapacity, indirectDrawCapacity,
                commandConstantsCapacity}
    {
        base.pushData.vertexAddress = frame.buffers.globalVertexBuffer.address;
        base.pushData.instanceAddress = frame.buffers.globalHeapBuffer.address;
        base.pushData.commandConstantsAddress = frame.buffers.commandConstantsBuffer.address;

        frame.builder.begin({.pipeline = base.pipeline,
                             .layout = base.layout,
                             .pushData = base.pushData,
                             .mesh = base.mesh,
                             .viewport = base.viewport,
                             .scissor = base.scissor});
    }

    // 生成器（UI CommandBuilder）结束 -> 把 segments 展开为通用指令流。
    void end()
    {
        frame.builder.end();
        build();
    }

    void build()
    {
        stream.clear();
        const auto *segments = frame.builder.stream().segments;
        if (segments == nullptr)
            return;

        for (const auto &seg : *segments)
        {
            Cmd bindPipeline{};
            bindPipeline.op = Op::BindPipeline;
            bindPipeline.u.bindPipeline.pipeline = seg.handles.pipeline;
            bindPipeline.u.bindPipeline.layout = seg.handles.layout;
            stream.add(bindPipeline);

            Cmd bindDescriptors{};
            bindDescriptors.op = Op::BindDescriptorSets;
            bindDescriptors.u.bindDescriptors.layout = seg.handles.layout;
            bindDescriptors.u.bindDescriptors.set = base.set;
            stream.add(bindDescriptors);

            Cmd push{};
            push.op = Op::PushConstants;
            push.u.pushConstants.layout = seg.handles.layout;
            push.u.pushConstants.stages =
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            push.u.pushConstants.offset = 0;
            push.u.pushConstants.size = sizeof(PushData);
            std::memcpy(push.u.pushConstants.data.data(), &seg.pushData, sizeof(PushData));
            stream.add(push);

            Cmd viewport{};
            viewport.op = Op::SetViewport;
            viewport.u.setViewport.viewport = seg.regs.viewport;
            stream.add(viewport);

            Cmd scissor{};
            scissor.op = Op::SetScissor;
            scissor.u.setScissor.scissor = seg.regs.scissor;
            stream.add(scissor);

            Cmd index{};
            index.op = Op::BindIndexBuffer;
            index.u.bindIndexBuffer.buffer = frame.buffers.globalIndexBuffer.buffer.buffer();
            index.u.bindIndexBuffer.offset = 0;
            index.u.bindIndexBuffer.type = VK_INDEX_TYPE_UINT32;
            stream.add(index);

            Cmd draw{};
            draw.op = Op::DrawIndexedIndirect;
            draw.u.drawIndirect.buffer = frame.buffers.indirectDrawBuffer.buffer.buffer();
            draw.u.drawIndirect.offset = seg.draw.indirectOffset;
            draw.u.drawIndirect.drawCount = seg.draw.commandCount;
            draw.u.drawIndirect.stride = sizeof(VkDrawIndexedIndirectCommand);
            stream.add(draw);
        }
    }

    [[nodiscard]] constexpr shader_data::CommandBuilder &builder() noexcept
    {
        return frame.builder;
    }
};

class PassManager
{
  public:
    PassManager(const LogicalDevice &device, VkDeviceSize vertexCapacity,
                VkDeviceSize indexCapacity, VkDeviceSize heapCapacity,
                VkDeviceSize indirectDrawCapacity, VkDeviceSize commandConstantsCapacity)
        : device_{device}, vertexCapacity_{vertexCapacity}, indexCapacity_{indexCapacity},
          heapCapacity_{heapCapacity}, indirectDrawCapacity_{indirectDrawCapacity},
          commandConstantsCapacity_{commandConstantsCapacity}
    {
    }

    Pass &beginPass(PassDesc desc, PipelineState base)
    {
        passes_.emplace_back(device_, vertexCapacity_, indexCapacity_, heapCapacity_,
                             indirectDrawCapacity_, commandConstantsCapacity_, desc, base);
        current_ = &passes_.back();
        return *current_;
    }

    void endPass()
    {
        if (current_ != nullptr)
            current_->end();
        current_ = nullptr;
    }

    void reset()
    {
        passes_.clear();
        current_ = nullptr;
    }

    [[nodiscard]] constexpr Pass *current() noexcept { return current_; }
    [[nodiscard]] constexpr const std::vector<Pass> &passes() const noexcept
    {
        return passes_;
    }
    [[nodiscard]] constexpr bool empty() const noexcept { return passes_.empty(); }

  private:
    const LogicalDevice &device_;
    VkDeviceSize vertexCapacity_{};
    VkDeviceSize indexCapacity_{};
    VkDeviceSize heapCapacity_{};
    VkDeviceSize indirectDrawCapacity_{};
    VkDeviceSize commandConstantsCapacity_{};
    std::vector<Pass> passes_{};
    Pass *current_{nullptr};
};

} // namespace pass_data
