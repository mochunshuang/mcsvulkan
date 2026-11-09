#pragma once
// ============================================================================
// test_intent8_draw_emitter.hpp
//
// Transparent lowering: Op -> explicit vkCmd*.
//
// This is a 1:1 switch over the recorded op list. It does not reorder,
// deduplicate, or insert hidden commands. The emitted call sequence is exactly
// the recorded op sequence, and c_api_list() reports the same sequence.
// ============================================================================

#include "test_intent8_draw_ops.hpp"

#include <cassert>

namespace intent8
{

class VkEmitter
{
  public:
    explicit VkEmitter(mcs::vulkan::CommandBufferView cmd) noexcept : cmd_{cmd} {}

    void emit(const Op &op) const noexcept
    {
        auto &vk = cmd_.functions();
        const VkCommandBuffer cmd = *cmd_;

        switch (op.code)
        {
        case OpCode::BeginTarget: {
            const auto &p = std::get<BeginTargetPayload>(op.payload);
            if (p.target.config.kind == TargetKind::ClassicRenderPass)
            {
                const VkRenderPassBeginInfo info = p.target.classicInfo();
                const VkSubpassBeginInfo subpass{
                    .sType = VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO,
                    .pNext = nullptr,
                    .contents = p.target.config.contents};
                vk.vkCmdBeginRenderPass2(cmd, &info, &subpass);
            }
            else
            {
                const VkRenderingInfo info = p.target.info();
                vk.vkCmdBeginRendering(cmd, &info);
            }
            ++calls_;
            break;
        }
        case OpCode::NextSubpass: {
            const auto &p = std::get<NextSubpassPayload>(op.payload);
            const VkSubpassBeginInfo beginInfo{
                .sType = VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO,
                .pNext = nullptr,
                .contents = p.contents};
            const VkSubpassEndInfo endInfo{
                .sType = VK_STRUCTURE_TYPE_SUBPASS_END_INFO, .pNext = nullptr};
            vk.vkCmdNextSubpass2(cmd, &beginInfo, &endInfo);
            ++calls_;
            break;
        }
        case OpCode::EndTarget: {
            const auto &p = std::get<EndTargetPayload>(op.payload);
            if (p.target.config.kind == TargetKind::ClassicRenderPass)
            {
                const VkSubpassEndInfo endInfo{
                    .sType = VK_STRUCTURE_TYPE_SUBPASS_END_INFO, .pNext = nullptr};
                vk.vkCmdEndRenderPass2(cmd, &endInfo);
            }
            else
                vk.vkCmdEndRendering(cmd);
            ++calls_;
            break;
        }
        case OpCode::BindPipeline: {
            const auto &p = std::get<BindPipelinePayload>(op.payload);
            assert(p.pipeline != nullptr);
            vk.vkCmdBindPipeline(cmd, p.bindPoint, p.pipeline->pipeline());
            ++calls_;
            break;
        }
        case OpCode::BindDescriptorSet: {
            const auto &p = std::get<BindDescriptorSetPayload>(op.payload);
            vk.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.layout,
                                       p.firstSet, 1, &p.set, 0, nullptr);
            ++calls_;
            break;
        }
        case OpCode::PushConstants: {
            const auto &p = std::get<PushConstantsPayload>(op.payload);
            vk.vkCmdPushConstants(cmd, p.layout, p.stages, p.offset, p.size,
                                  p.data.data());
            ++calls_;
            break;
        }
        case OpCode::SetViewport: {
            const auto &p = std::get<SetViewportPayload>(op.payload);
            vk.vkCmdSetViewport(cmd, 0, 1, &p.viewport);
            ++calls_;
            break;
        }
        case OpCode::SetScissor: {
            const auto &p = std::get<SetScissorPayload>(op.payload);
            vk.vkCmdSetScissor(cmd, 0, 1, &p.scissor);
            ++calls_;
            break;
        }
        case OpCode::BindIndexBuffer: {
            const auto &p = std::get<BindIndexBufferPayload>(op.payload);
            vk.vkCmdBindIndexBuffer(cmd, p.buffer, p.offset, p.type);
            ++calls_;
            break;
        }
        case OpCode::BindVertexBuffers: {
            const auto &p = std::get<BindVertexBuffersPayload>(op.payload);
            vk.vkCmdBindVertexBuffers(cmd, p.firstBinding, p.bindingCount,
                                      p.buffers.data(), p.offsets.data());
            ++calls_;
            break;
        }
        case OpCode::Draw: {
            const auto &p = std::get<DrawPayload>(op.payload);
            switch (p.args.kind)
            {
            case DrawKind::Draw:
                vk.vkCmdDraw(cmd, p.args.vertexCount, p.args.instanceCount,
                             p.args.firstVertex, p.args.firstInstance);
                break;
            case DrawKind::DrawIndexed:
                vk.vkCmdDrawIndexed(cmd, p.args.indexCount, p.args.instanceCount,
                                    p.args.firstIndex, p.args.vertexOffset,
                                    p.args.firstInstance);
                break;
            case DrawKind::DrawIndirect:
                vk.vkCmdDrawIndirect(cmd, p.args.indirectBuffer, p.args.indirectOffset,
                                     p.args.drawCount, p.args.stride);
                break;
            case DrawKind::DrawIndexedIndirect:
                vk.vkCmdDrawIndexedIndirect(cmd, p.args.indirectBuffer,
                                            p.args.indirectOffset, p.args.drawCount,
                                            p.args.stride);
                break;
            case DrawKind::DrawIndexedIndirectCount:
                vk.vkCmdDrawIndexedIndirectCount(
                    cmd, p.args.indirectBuffer, p.args.indirectOffset,
                    p.args.countBuffer, p.args.countOffset, p.args.maxDrawCount,
                    p.args.stride);
                break;
            }
            ++calls_;
            break;
        }
        case OpCode::Barrier: {
            const auto &p = std::get<BarrierPayload>(op.payload);
            const VkDependencyInfo dependency{
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .pNext = nullptr,
                .dependencyFlags = p.dependencyFlags,
                .memoryBarrierCount = p.memoryBarrierCount,
                .pMemoryBarriers =
                    p.memoryBarrierCount == 0 ? nullptr : p.memoryBarriers.data(),
                .bufferMemoryBarrierCount = p.bufferBarrierCount,
                .pBufferMemoryBarriers = p.bufferBarrierCount == 0
                                             ? nullptr
                                             : p.bufferBarriers.data(),
                .imageMemoryBarrierCount = p.imageBarrierCount,
                .pImageMemoryBarriers = p.imageBarrierCount == 0
                                            ? nullptr
                                            : p.imageBarriers.data(),
            };
            vk.vkCmdPipelineBarrier2(cmd, &dependency);
            ++calls_;
            break;
        }
        case OpCode::FillBuffer: {
            const auto &p = std::get<FillBufferPayload>(op.payload);
            vk.vkCmdFillBuffer(cmd, p.buffer, p.offset, p.size, p.data);
            ++calls_;
            break;
        }
        case OpCode::CopyBuffer: {
            const auto &p = std::get<CopyBufferPayload>(op.payload);
            vk.vkCmdCopyBuffer(cmd, p.src, p.dst, p.regionCount, p.regions.data());
            ++calls_;
            break;
        }
        case OpCode::CopyImageToBuffer: {
            const auto &p = std::get<CopyImageToBufferPayload>(op.payload);
            vk.vkCmdCopyImageToBuffer(cmd, p.image, p.imageLayout, p.buffer,
                                      p.regionCount, p.regions.data());
            ++calls_;
            break;
        }
        case OpCode::ExecuteCommands: {
            const auto &p = std::get<ExecuteCommandsPayload>(op.payload);
            vk.vkCmdExecuteCommands(cmd, p.commandBufferCount,
                                    p.commandBuffers.data());
            ++calls_;
            break;
        }
        }
    }

    void emit(const OpStream &stream) const noexcept
    {
        for (const auto &op : stream.ops())
            emit(op);
    }

    [[nodiscard]] std::uint32_t calls() const noexcept { return calls_; }

  private:
    mcs::vulkan::CommandBufferView cmd_{};
    mutable std::uint32_t calls_{0};
};

} // namespace intent8
