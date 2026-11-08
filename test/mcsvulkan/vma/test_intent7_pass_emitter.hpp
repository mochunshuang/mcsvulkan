#pragma once
// ============================================================================
// test_intent7_pass_emitter.hpp
//
// Transparent lowering: Op -> explicit Vulkan C command.
//
// This is deliberately a 1:1 switch.  It does not reorder, deduplicate, or
// insert hidden commands.  The emitted sequence is exactly the OpStream order.
// ============================================================================

#include "test_intent7_pass_opcode.hpp"

#include <cassert>

namespace pass_data
{

class VkEmitter
{
  public:
    explicit VkEmitter(mcs::vulkan::CommandBufferView cmd) noexcept : cmd_{cmd} {}

    void emit(const Op &op) const noexcept
    {
        auto &vk = cmd_.functions();
        const VkCommandBuffer cmd = *cmd_;

        switch (op.opcode)
        {
        case Opcode::BeginRendering: {
            const auto &p = std::get<BeginRenderingPayload>(op.payload);
            const VkRenderingInfo info = p.target.info();
            vk.vkCmdBeginRendering(cmd, &info);
            break;
        }
        case Opcode::EndRendering:
            vk.vkCmdEndRendering(cmd);
            break;
        case Opcode::BindPipeline: {
            const auto &p = std::get<BindPipelinePayload>(op.payload);
            vk.vkCmdBindPipeline(cmd, p.bindPoint, p.pipeline);
            break;
        }
        case Opcode::BindDescriptorSet: {
            const auto &p = std::get<BindDescriptorSetPayload>(op.payload);
            vk.vkCmdBindDescriptorSets(cmd, p.bindPoint, p.layout, 0, 1, &p.set, 0,
                                       nullptr);
            break;
        }
        case Opcode::BindDescriptorBuffer: {
            const auto &p = std::get<BindDescriptorBufferPayload>(op.payload);
            vk.vkCmdBindDescriptorBuffersEXT(cmd, 1, &p.binding);
            const std::uint32_t bufferIndex = 0;
            const VkDeviceSize offset = p.offset;
            vk.vkCmdSetDescriptorBufferOffsetsEXT(
                cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.layout, 0, 1, &bufferIndex,
                &offset);
            break;
        }
        case Opcode::BindResourceHeap: {
            const auto &p = std::get<BindHeapPayload>(op.payload);
            // Descriptor heap is newer than this SDK's VolkDeviceTable; resolve
            // it through vkGetDeviceProcAddr when the extension is loaded.
            if (::vkGetDeviceProcAddr != nullptr)
            {
                const VkDevice device = **cmd_.device();
                const auto fn = reinterpret_cast<PFN_vkCmdBindResourceHeapEXT>(
                    ::vkGetDeviceProcAddr(device, "vkCmdBindResourceHeapEXT"));
                if (fn != nullptr)
                    fn(cmd, &p.info);
            }
            break;
        }
        case Opcode::BindSamplerHeap: {
            const auto &p = std::get<BindHeapPayload>(op.payload);
            if (::vkGetDeviceProcAddr != nullptr)
            {
                const VkDevice device = **cmd_.device();
                const auto fn = reinterpret_cast<PFN_vkCmdBindSamplerHeapEXT>(
                    ::vkGetDeviceProcAddr(device, "vkCmdBindSamplerHeapEXT"));
                if (fn != nullptr)
                    fn(cmd, &p.info);
            }
            break;
        }
        case Opcode::PushConstants: {
            const auto &p = std::get<PushConstantsPayload>(op.payload);
            vk.vkCmdPushConstants(cmd, p.layout, p.stages, p.offset, p.size,
                                  p.data.data());
            break;
        }
        case Opcode::SetViewport: {
            const auto &p = std::get<SetViewportPayload>(op.payload);
            vk.vkCmdSetViewport(cmd, 0, 1, &p.viewport);
            break;
        }
        case Opcode::SetScissor: {
            const auto &p = std::get<SetScissorPayload>(op.payload);
            vk.vkCmdSetScissor(cmd, 0, 1, &p.scissor);
            break;
        }
        case Opcode::SetDepthTestEnable:
            vk.vkCmdSetDepthTestEnable(cmd,
                                       std::get<SetBoolPayload>(op.payload).enable);
            break;
        case Opcode::SetDepthWriteEnable:
            vk.vkCmdSetDepthWriteEnable(cmd,
                                        std::get<SetBoolPayload>(op.payload).enable);
            break;
        case Opcode::SetCullMode:
            vk.vkCmdSetCullMode(cmd,
                                std::get<SetCullModePayload>(op.payload).cullMode);
            break;
        case Opcode::SetFrontFace:
            vk.vkCmdSetFrontFace(cmd,
                                 std::get<SetFrontFacePayload>(op.payload).frontFace);
            break;
        case Opcode::SetPrimitiveTopology:
            vk.vkCmdSetPrimitiveTopology(
                cmd, std::get<SetPrimitiveTopologyPayload>(op.payload).topology);
            break;
        case Opcode::SetStencilReference: {
            const auto &p = std::get<SetStencilReferencePayload>(op.payload);
            vk.vkCmdSetStencilReference(cmd, p.faceMask, p.reference);
            break;
        }
        case Opcode::BindIndexBuffer: {
            const auto &p = std::get<BindIndexBufferPayload>(op.payload);
            vk.vkCmdBindIndexBuffer(cmd, p.buffer, p.offset, p.type);
            break;
        }
        case Opcode::Draw: {
            const auto &p = std::get<DrawPayload>(op.payload);
            vk.vkCmdDraw(cmd, p.vertexCount, p.instanceCount, p.firstVertex,
                         p.firstInstance);
            break;
        }
        case Opcode::DrawIndexed: {
            const auto &p = std::get<DrawIndexedPayload>(op.payload);
            vk.vkCmdDrawIndexed(cmd, p.indexCount, p.instanceCount, p.firstIndex,
                                p.vertexOffset, p.firstInstance);
            break;
        }
        case Opcode::DrawIndirect: {
            const auto &p = std::get<DrawIndirectPayload>(op.payload);
            vk.vkCmdDrawIndirect(cmd, p.buffer, p.offset, p.drawCount, p.stride);
            break;
        }
        case Opcode::DrawIndexedIndirect: {
            const auto &p = std::get<DrawIndexedIndirectPayload>(op.payload);
            vk.vkCmdDrawIndexedIndirect(cmd, p.buffer, p.offset, p.drawCount,
                                       p.stride);
            break;
        }
        case Opcode::CopyImageToBuffer: {
            const auto &p = std::get<CopyImageToBufferPayload>(op.payload);
            vk.vkCmdCopyImageToBuffer(cmd, p.image, p.imageLayout, p.buffer,
                                      p.regionCount, p.regions.data());
            break;
        }
        case Opcode::CopyBuffer: {
            const auto &p = std::get<CopyBufferPayload>(op.payload);
            vk.vkCmdCopyBuffer(cmd, p.src, p.dst, p.regionCount, p.regions.data());
            break;
        }
        case Opcode::CopyBufferToImage: {
            const auto &p = std::get<CopyBufferToImagePayload>(op.payload);
            vk.vkCmdCopyBufferToImage(
                cmd, p.src, p.dst, p.dstLayout,
                static_cast<std::uint32_t>(p.regions.size()), p.regions.data());
            break;
        }
        case Opcode::CopyImage: {
            const auto &p = std::get<CopyImagePayload>(op.payload);
            vk.vkCmdCopyImage(cmd, p.src, p.srcLayout, p.dst, p.dstLayout,
                              static_cast<std::uint32_t>(p.regions.size()),
                              p.regions.data());
            break;
        }
        case Opcode::BlitImage: {
            const auto &p = std::get<BlitImagePayload>(op.payload);
            vk.vkCmdBlitImage(cmd, p.src, p.srcLayout, p.dst, p.dstLayout,
                              p.regionCount, p.regions.data(), p.filter);
            break;
        }
        case Opcode::FillBuffer: {
            const auto &p = std::get<FillBufferPayload>(op.payload);
            vk.vkCmdFillBuffer(cmd, p.buffer, p.offset, p.size, p.data);
            break;
        }
        case Opcode::UpdateBuffer: {
            const auto &p = std::get<UpdateBufferPayload>(op.payload);
            vk.vkCmdUpdateBuffer(cmd, p.buffer, p.offset, p.data.size(),
                                 p.data.data());
            break;
        }
        case Opcode::ClearColorImage: {
            const auto &p = std::get<ClearColorImagePayload>(op.payload);
            vk.vkCmdClearColorImage(
                cmd, p.image, p.layout, &p.color,
                static_cast<std::uint32_t>(p.ranges.size()), p.ranges.data());
            break;
        }
        case Opcode::ClearDepthStencilImage: {
            const auto &p = std::get<ClearDepthStencilImagePayload>(op.payload);
            vk.vkCmdClearDepthStencilImage(
                cmd, p.image, p.layout, &p.value,
                static_cast<std::uint32_t>(p.ranges.size()), p.ranges.data());
            break;
        }
        case Opcode::Dispatch: {
            const auto &p = std::get<DispatchPayload>(op.payload);
            vk.vkCmdDispatch(cmd, p.x, p.y, p.z);
            break;
        }
        case Opcode::DispatchIndirect: {
            const auto &p = std::get<DispatchIndirectPayload>(op.payload);
            vk.vkCmdDispatchIndirect(cmd, p.buffer, p.offset);
            break;
        }
        case Opcode::Barrier: {
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
            break;
        }
        case Opcode::ClearAttachments: {
            const auto &p = std::get<ClearAttachmentsPayload>(op.payload);
            vk.vkCmdClearAttachments(
                cmd, static_cast<std::uint32_t>(p.attachments.size()),
                p.attachments.data(), static_cast<std::uint32_t>(p.rects.size()),
                p.rects.data());
            break;
        }
        case Opcode::BeginQuery: {
            const auto &p = std::get<QueryPayload>(op.payload);
            vk.vkCmdBeginQuery(cmd, p.pool, p.query, p.flags);
            break;
        }
        case Opcode::EndQuery: {
            const auto &p = std::get<QueryPayload>(op.payload);
            vk.vkCmdEndQuery(cmd, p.pool, p.query);
            break;
        }
        case Opcode::ExecuteCommands: {
            const auto &p = std::get<ExecuteCommandsPayload>(op.payload);
            vk.vkCmdExecuteCommands(cmd, p.commandBufferCount,
                                    p.commandBuffers.data());
            break;
        }
        default:
            // No abstract-only operation may reach the emitter without a
            // corresponding official Vulkan C entry point.
            assert(false && "VkEmitter: unhandled opcode");
            break;
        }
    }

    void emit(const OpStream &stream) const noexcept
    {
        for (const auto &op : stream.commands())
            emit(op);
    }

  private:
    mcs::vulkan::CommandBufferView cmd_{};
};

} // namespace pass_data
