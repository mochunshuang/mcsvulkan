#pragma once
// ============================================================================
// test_intent7_pass_compiler.hpp
//
// PassCompiler = 逐条 Op -> vkCmd*。
//   pass 级：Target::Classic -> vkCmdBeginRenderPass/EndRenderPass
//            Target::Dynamic -> vkCmdBeginRendering/EndRendering
//            Target::Main    -> 使用 app 的主 target（LOAD）
//   stream 级：BindPipeline / BindDescriptorSets / PushConstants /
//              BindVertexBuffers / BindIndexBuffer / SetViewport / SetScissor /
//              Draw* / DrawIndexed* / DrawIndirectCount / Dispatch* /
//              ExecuteCommands / Query / Conditional / TransformFeedback /
//              ClearAttachments / PipelineBarrier / Copy*
//
// 当前 UI 生成器只产出其中的 BindPipeline/BindDescriptors/PushConstants/
// SetViewport/SetScissor/BindIndexBuffer/DrawIndexedIndirect；
// 其余 Op 在枚举和 Stream 里已留好，C API 映射可逐项补齐。
// ============================================================================

#include "test_intent7_pass_abi.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <span>

namespace pass_data
{

class PassCompiler
{
  public:
    void execute(mcs::vulkan::CommandBufferView cmd, VkPipelineLayout mainLayout,
                 VkDescriptorSet mainSet, VkImageView color, VkImageView colorResolve,
                 VkImageView depth, VkImageView pick, VkImageView pickResolve,
                 VkExtent2D extent, VkSampleCountFlagBits samples,
                 PassManager &manager) const
    {
        if (manager.empty())
            return;

        VkRenderingAttachmentInfo colorAttachment{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = color,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT,
            .resolveImageView = colorResolve,
            .resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        };
        VkRenderingAttachmentInfo pickAttachment{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = pick,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT,
            .resolveImageView = pickResolve,
            .resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        };
        std::array<VkRenderingAttachmentInfo, 2> colorAttachments{colorAttachment,
                                                                  pickAttachment};
        VkRenderingAttachmentInfo depthAttachment{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = depth,
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        };
        const VkRenderingInfo mainLoad{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {.offset = {.x = 0, .y = 0}, .extent = extent},
            .layerCount = 1,
            .colorAttachmentCount = static_cast<std::uint32_t>(colorAttachments.size()),
            .pColorAttachments = colorAttachments.data(),
            .pDepthAttachment = &depthAttachment,
        };

        // 结束 app 已开始的 main pass
        cmd.endRendering();

        for (const auto &pass : manager.passes())
        {
            begin(cmd, pass, mainLoad);
            for (const auto &c : pass.stream.commands())
                replay(cmd, c);
            end(cmd, pass);
        }

        // 重新开始 main LOAD，给 app 的 shader_data::emit 画主行
        cmd.beginRendering(mainLoad);
        cmd.bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, mainLayout, 0, 1, &mainSet, 0,
                               nullptr);
        (void)samples;
    }

  private:
    static void begin(mcs::vulkan::CommandBufferView cmd, const Pass &pass,
                      const VkRenderingInfo &mainLoad)
    {
        switch (pass.desc.target)
        {
        case Target::Classic:
            // 经典 render pass：vkCmdBeginRenderPass(pass.desc.classic, contents)
            // 需要 CommandBufferView 暴露 VkCommandBuffer/设备表；当前路径未使用。
            assert(false && "classic render pass not wired in this build");
            break;
        case Target::Dynamic:
            cmd.beginRendering(pass.desc.dynamic);
            break;
        case Target::Main:
            cmd.beginRendering(mainLoad);
            break;
        }
    }

    static void end(mcs::vulkan::CommandBufferView cmd, const Pass &pass)
    {
        switch (pass.desc.target)
        {
        case Target::Classic:
            // vkCmdEndRenderPass
            assert(false && "classic render pass not wired in this build");
            break;
        case Target::Dynamic:
        case Target::Main:
            cmd.endRendering();
            break;
        }
    }

    static void replay(mcs::vulkan::CommandBufferView cmd, const Cmd &c)
    {
        switch (c.op)
        {
        case Op::BindPipeline:
            cmd.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, c.u.bindPipeline.pipeline);
            break;
        case Op::BindDescriptorSets:
            cmd.bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, c.u.bindDescriptors.layout,
                                   0, 1, &c.u.bindDescriptors.set, 0, nullptr);
            break;
        case Op::PushConstants:
            cmd.pushConstants(c.u.pushConstants.layout, c.u.pushConstants.stages,
                              c.u.pushConstants.offset, c.u.pushConstants.size,
                              c.u.pushConstants.data.data());
            break;
        case Op::BindVertexBuffers:
            // CommandBufferView 目前只包装单个 VB；完整多 VB 用设备层 vkCmdBindVertexBuffers。
            assert(c.u.bindVertexBuffers.count == 1);
            cmd.bindVertexBuffers(c.u.bindVertexBuffers.first, c.u.bindVertexBuffers.count,
                                  c.u.bindVertexBuffers.buffers[0],
                                  c.u.bindVertexBuffers.offsets[0]);
            break;
        case Op::BindIndexBuffer:
            cmd.bindIndexBuffer(c.u.bindIndexBuffer.buffer, c.u.bindIndexBuffer.offset,
                                c.u.bindIndexBuffer.type);
            break;
        case Op::SetViewport:
            cmd.setViewport(0, std::span{&c.u.setViewport.viewport, 1});
            break;
        case Op::SetScissor:
            cmd.setScissor(0, std::span{&c.u.setScissor.scissor, 1});
            break;
        case Op::SetLineWidth:
            cmd.setLineWidth(c.u.setLineWidth.lineWidth);
            break;
        case Op::SetStencilReference:
            cmd.setStencilReference(VK_STENCIL_FACE_FRONT_AND_BACK, c.u.setStencil.reference);
            break;
        case Op::SetPrimitiveTopology:
            cmd.setPrimitiveTopology(c.u.setPrimitiveTopology.topology);
            break;
        case Op::SetDepthTestEnable:
            cmd.setDepthTestEnable(c.u.setBool.enable == VK_TRUE);
            break;
        case Op::SetDepthWriteEnable:
            cmd.setDepthWriteEnable(c.u.setBool.enable == VK_TRUE);
            break;
        case Op::Draw:
            cmd.draw(c.u.draw.vertexCount, c.u.draw.instanceCount, c.u.draw.firstVertex,
                     c.u.draw.firstInstance);
            break;
        case Op::DrawIndexed:
            cmd.drawIndexed(c.u.drawIndexed.indexCount, c.u.drawIndexed.instanceCount,
                            c.u.drawIndexed.firstIndex, c.u.drawIndexed.vertexOffset,
                            c.u.drawIndexed.firstInstance);
            break;
        case Op::DrawIndexedIndirect:
            cmd.drawIndexedIndirect(c.u.drawIndirect.buffer, c.u.drawIndirect.offset,
                                    c.u.drawIndirect.drawCount, c.u.drawIndirect.stride);
            break;
        case Op::PipelineBarrier:
            cmd.pipelineBarrier2(c.u.barrier.dependency);
            break;
        case Op::CopyBuffer:
            // cmd.copyBuffer(...)
            assert(false && "CopyBuffer payload needs raw region storage");
            break;
        default:
            assert(false && "Op not wired in this build");
            break;
        }
    }
};

} // namespace pass_data
