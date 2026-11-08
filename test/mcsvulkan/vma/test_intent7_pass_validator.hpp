#pragma once
// ============================================================================
// test_intent7_pass_validator.hpp
//
// Incremental validator, not a full state-space compiler.
//
// Layers:
//   scope        : Begin/End/draw are Inside; barrier is Both.
//   lifecycle    : pass target, pipeline, descriptor binding must be alive.
//   compatibility: basic non-null and active-pass checks (deep format checks
//                  can be added without changing the Op/PassData shape).
//   feature      : reserved.
// ============================================================================

#include "test_intent7_pass_opcode.hpp"

#include <cstdio>
#include <print>

namespace pass_data
{

class PassValidator
{
  public:
    [[nodiscard]] bool valid() const noexcept { return valid_; }

    // Start validation for a secondary command buffer recorded with
    // VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT. There is no
    // BeginRendering op in the secondary stream, but the inherited target is
    // already active for validation purposes.
    void beginInherited(const PassTarget &target) noexcept
    {
        (void)target;
        beginScope(target);
    }

    void beginScope(const PassTarget &target) noexcept
    {
        (void)target;
        passActive_ = true;
        pipelineBound_ = false;
        descriptorsBound_ = false;
    }

    void endScope() noexcept
    {
        passActive_ = false;
        pipelineBound_ = false;
        descriptorsBound_ = false;
    }

    bool validate(const Op &op, const PassData &data)
    {
        switch (op.opcode)
        {
        case Opcode::BeginRendering: {
            const auto &p = std::get<BeginRenderingPayload>(op.payload);
            if (passActive_)
                return fail("BeginRendering while a pass is already active");
            if (p.target.renderArea.extent.width == 0 ||
                p.target.renderArea.extent.height == 0)
                return fail("BeginRendering with zero render area");
            if (p.target.colorCount == 0 && !p.target.hasDepth &&
                !p.target.hasStencil)
                return fail("BeginRendering with no attachments");
            passActive_ = true;
            pipelineBound_ = false;
            descriptorsBound_ = false;
            return true;
        }
        case Opcode::EndRendering:
            if (!passActive_)
                return fail("EndRendering without an active pass");
            passActive_ = false;
            return true;
        case Opcode::BindPipeline: {
            const auto &p = std::get<BindPipelinePayload>(op.payload);
            if (p.pipeline == VK_NULL_HANDLE)
                return fail("BindPipeline with VK_NULL_HANDLE");
            if (graphics_scope(data))
            {
                if (p.bindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS)
                    return fail("graphics pass with non-graphics pipeline");
                if (!compatible(data.pipeline.rendering, data.target))
                    return fail("pipeline rendering info does not match pass target");
            }
            else
            {
                if (p.bindPoint != VK_PIPELINE_BIND_POINT_COMPUTE)
                    return fail("compute pass with non-compute pipeline");
            }
            pipelineBound_ = true;
            return true;
        }
        case Opcode::BindDescriptorSet: {
            const auto &p = std::get<BindDescriptorSetPayload>(op.payload);
            if (p.set == VK_NULL_HANDLE)
                return fail("BindDescriptorSet with VK_NULL_HANDLE");
            const VkPipelineBindPoint expected =
                graphics_scope(data) ? VK_PIPELINE_BIND_POINT_GRAPHICS
                                     : VK_PIPELINE_BIND_POINT_COMPUTE;
            if (p.bindPoint != expected)
                return fail("descriptor bind point does not match pass kind");
            if (data.pipeline.layout != VK_NULL_HANDLE &&
                data.pipeline.layout != p.layout)
                return fail("descriptor set layout is not the pipeline layout");
            descriptorsBound_ = true;
            return true;
        }
        case Opcode::BindDescriptorBuffer:
            if (data.features == nullptr || !data.features->descriptorBuffer)
                return fail("descriptorBuffer feature is not enabled");
            if (std::get<BindDescriptorBufferPayload>(op.payload).binding.address ==
                0)
                return fail("BindDescriptorBuffer with address 0");
            descriptorsBound_ = true;
            return true;
        case Opcode::BindResourceHeap:
        case Opcode::BindSamplerHeap:
            if (data.features == nullptr || !data.features->descriptorHeap)
                return fail("descriptorHeap feature is not enabled");
            if (std::get<BindHeapPayload>(op.payload).info.heapRange.address == 0)
                return fail("BindHeap with address 0");
            descriptorsBound_ = true;
            return true;
        case Opcode::PushConstants:
            return true;
        case Opcode::SetViewport:
            if (passActive_ && !graphics_scope(data))
                return fail("SetViewport outside the pass");
            if (!state_dynamic(data, DynamicStateKey::Viewport))
                return fail("SetViewport but pipeline viewport is static");
            return true;
        case Opcode::SetScissor:
            if (passActive_ && !graphics_scope(data))
                return fail("SetScissor outside the pass");
            if (!state_dynamic(data, DynamicStateKey::Scissor))
                return fail("SetScissor but pipeline scissor is static");
            return true;
        case Opcode::SetDepthTestEnable:
            if (passActive_ && !graphics_scope(data))
                return fail("dynamic state outside the pass");
            if (!state_dynamic(data, DynamicStateKey::DepthTestEnable))
                return fail("pipeline depth-test state is static");
            return true;
        case Opcode::SetDepthWriteEnable:
            if (passActive_ && !graphics_scope(data))
                return fail("dynamic state outside the pass");
            if (!state_dynamic(data, DynamicStateKey::DepthWriteEnable))
                return fail("pipeline depth-write state is static");
            return true;
        case Opcode::SetCullMode:
            if (passActive_ && !graphics_scope(data))
                return fail("dynamic state outside the pass");
            if (!state_dynamic(data, DynamicStateKey::CullMode))
                return fail("pipeline cull state is static");
            return true;
        case Opcode::SetFrontFace:
            if (passActive_ && !graphics_scope(data))
                return fail("dynamic state outside the pass");
            if (!state_dynamic(data, DynamicStateKey::FrontFace))
                return fail("pipeline front-face state is static");
            return true;
        case Opcode::SetPrimitiveTopology:
            if (passActive_ && !graphics_scope(data))
                return fail("dynamic state outside the pass");
            if (!state_dynamic(data, DynamicStateKey::PrimitiveTopology))
                return fail("pipeline topology state is static");
            return true;
        case Opcode::SetStencilReference:
            if (passActive_ && !graphics_scope(data))
                return fail("SetStencilReference outside a graphics pass");
            if (!state_dynamic(data, VK_DYNAMIC_STATE_STENCIL_REFERENCE))
                return fail("pipeline stencil-reference state is static");
            return true;
        case Opcode::BindIndexBuffer:
            if (passActive_ && !graphics_scope(data))
                return fail("BindIndexBuffer outside the pass");
            return true;
        case Opcode::Draw:
        case Opcode::DrawIndexed:
            if (passActive_ && !graphics_scope(data))
                return fail("draw outside the pass");
            if (!pipelineBound_)
                return fail("draw without a bound pipeline");
            if (!descriptorsBound_)
                return fail("draw without a bound descriptor set");
            return true;
        case Opcode::DrawIndirect:
            if (passActive_ && !graphics_scope(data))
                return fail("DrawIndirect outside the pass");
            if (!pipelineBound_)
                return fail("DrawIndirect without a bound pipeline");
            if (!descriptorsBound_)
                return fail("DrawIndirect without a bound descriptor set");
            if (std::get<DrawIndirectPayload>(op.payload).buffer == VK_NULL_HANDLE)
                return fail("DrawIndirect with VK_NULL_HANDLE buffer");
            return true;
        case Opcode::DrawIndexedIndirect:
            if (passActive_ && !graphics_scope(data))
                return fail("DrawIndexedIndirect outside the pass");
            if (!pipelineBound_)
                return fail("DrawIndexedIndirect without a bound pipeline");
            if (!descriptorsBound_)
                return fail("DrawIndexedIndirect without a bound descriptor set");
            if (std::get<DrawIndexedIndirectPayload>(op.payload).buffer ==
                VK_NULL_HANDLE)
                return fail("DrawIndexedIndirect with VK_NULL_HANDLE buffer");
            return true;
        case Opcode::CopyImageToBuffer: {
            if (passActive_)
                return fail("CopyImageToBuffer inside the pass");
            const auto &p = std::get<CopyImageToBufferPayload>(op.payload);
            if (p.image == VK_NULL_HANDLE || p.buffer == VK_NULL_HANDLE ||
                p.regionCount == 0)
                return fail("CopyImageToBuffer with empty/null operands");
            return true;
        }
        case Opcode::ClearAttachments: {
            if (passActive_ && !graphics_scope(data))
                return fail("ClearAttachments outside the pass");
            const auto &p = std::get<ClearAttachmentsPayload>(op.payload);
            if (p.attachments.empty() || p.rects.empty())
                return fail("ClearAttachments with empty arrays");
            return true;
        }
        case Opcode::BeginQuery:
        case Opcode::EndQuery: {
            if (std::get<QueryPayload>(op.payload).pool == VK_NULL_HANDLE)
                return fail("query with VK_NULL_HANDLE pool");
            return true;
        }
        case Opcode::ExecuteCommands: {
            if (passActive_ && !graphics_scope(data))
                return fail("ExecuteCommands outside the pass");
            if ((data.target.flags &
                 VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT) == 0)
                return fail("ExecuteCommands without secondary-contents flag");
            const auto &p = std::get<ExecuteCommandsPayload>(op.payload);
            if (p.commandBufferCount == 0)
                return fail("ExecuteCommands with empty command buffer list");
            if (!inheritance_compatible(p.inheritance, data.target))
                return fail("secondary inheritance does not match pass target");
            return true;
        }
        case Opcode::CopyBuffer: {
            if (passActive_ && !transfer_scope(data))
                return fail("vkCmdCopyBuffer inside the pass");
            const auto &p = std::get<CopyBufferPayload>(op.payload);
            if (p.src == VK_NULL_HANDLE || p.dst == VK_NULL_HANDLE ||
                p.regionCount == 0)
                return fail("vkCmdCopyBuffer with empty/null operands");
            return true;
        }
        case Opcode::CopyBufferToImage: {
            if (passActive_ && !transfer_scope(data))
                return fail("vkCmdCopyBufferToImage inside the pass");
            const auto &p = std::get<CopyBufferToImagePayload>(op.payload);
            if (p.src == VK_NULL_HANDLE || p.dst == VK_NULL_HANDLE ||
                p.regions.empty())
                return fail("vkCmdCopyBufferToImage with empty/null operands");
            return true;
        }
        case Opcode::CopyImage: {
            if (passActive_ && !transfer_scope(data))
                return fail("vkCmdCopyImage inside the pass");
            const auto &p = std::get<CopyImagePayload>(op.payload);
            if (p.src == VK_NULL_HANDLE || p.dst == VK_NULL_HANDLE ||
                p.regions.empty())
                return fail("vkCmdCopyImage with empty/null operands");
            return true;
        }
        case Opcode::BlitImage: {
            if (passActive_ && !transfer_scope(data))
                return fail("vkCmdBlitImage inside the pass");
            const auto &p = std::get<BlitImagePayload>(op.payload);
            if (p.src == VK_NULL_HANDLE || p.dst == VK_NULL_HANDLE ||
                p.regionCount == 0)
                return fail("vkCmdBlitImage with empty/null operands");
            return true;
        }
        case Opcode::FillBuffer: {
            if (passActive_ && !transfer_scope(data))
                return fail("vkCmdFillBuffer inside the pass");
            if (std::get<FillBufferPayload>(op.payload).buffer == VK_NULL_HANDLE)
                return fail("vkCmdFillBuffer with VK_NULL_HANDLE");
            return true;
        }
        case Opcode::UpdateBuffer: {
            if (passActive_ && !transfer_scope(data))
                return fail("vkCmdUpdateBuffer inside the pass");
            const auto &p = std::get<UpdateBufferPayload>(op.payload);
            if (p.buffer == VK_NULL_HANDLE || p.data.empty())
                return fail("vkCmdUpdateBuffer with empty/null operands");
            return true;
        }
        case Opcode::ClearColorImage: {
            if (passActive_ && !transfer_scope(data))
                return fail("vkCmdClearColorImage inside the pass");
            const auto &p = std::get<ClearColorImagePayload>(op.payload);
            if (p.image == VK_NULL_HANDLE || p.ranges.empty())
                return fail("vkCmdClearColorImage with empty/null operands");
            return true;
        }
        case Opcode::ClearDepthStencilImage: {
            if (passActive_ && !transfer_scope(data))
                return fail("vkCmdClearDepthStencilImage inside the pass");
            const auto &p = std::get<ClearDepthStencilImagePayload>(op.payload);
            if (p.image == VK_NULL_HANDLE || p.ranges.empty())
                return fail("vkCmdClearDepthStencilImage with empty/null operands");
            return true;
        }
        case Opcode::Dispatch:
            if (passActive_ && !compute_scope(data))
                return fail("vkCmdDispatch inside the pass");
            return true;
        case Opcode::DispatchIndirect:
            if (passActive_ && !compute_scope(data))
                return fail("vkCmdDispatchIndirect inside the pass");
            if (std::get<DispatchIndirectPayload>(op.payload).buffer ==
                VK_NULL_HANDLE)
                return fail("vkCmdDispatchIndirect with VK_NULL_HANDLE");
            return true;
        case Opcode::Barrier:
            if (passActive_ && graphics_scope(data) &&
                (data.features == nullptr ||
                 !data.features->dynamicRenderingLocalRead))
                return fail("barrier inside dynamic rendering requires "
                            "dynamicRenderingLocalRead");
            return true;
        }
        return fail("unknown opcode");
    }

  private:
    static bool graphics_scope(const PassData &data) noexcept
    {
        return data.target.kind == PassKind::Graphics;
    }

    static bool compute_scope(const PassData &data) noexcept
    {
        return data.target.kind == PassKind::Compute;
    }

    static bool transfer_scope(const PassData &data) noexcept
    {
        return data.target.kind == PassKind::Transfer;
    }

    static VkDynamicState to_vk(DynamicStateKey key) noexcept
    {
        switch (key)
        {
        case DynamicStateKey::Viewport:
            return VK_DYNAMIC_STATE_VIEWPORT;
        case DynamicStateKey::Scissor:
            return VK_DYNAMIC_STATE_SCISSOR;
        case DynamicStateKey::DepthTestEnable:
            return VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE;
        case DynamicStateKey::DepthWriteEnable:
            return VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE;
        case DynamicStateKey::CullMode:
            return VK_DYNAMIC_STATE_CULL_MODE;
        case DynamicStateKey::FrontFace:
            return VK_DYNAMIC_STATE_FRONT_FACE;
        case DynamicStateKey::PrimitiveTopology:
            return VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY;
        }
        return VK_DYNAMIC_STATE_VIEWPORT;
    }

    static bool state_dynamic(const PassData &data, DynamicStateKey key) noexcept
    {
        // Dynamic state may be set before a pipeline is bound; it must be
        // declared Dynamic/Inherited once a pipeline is active.
        if (data.pipeline.pipeline == VK_NULL_HANDLE)
            return true;
        const auto ownership = data.pipeline.ownership.get(to_vk(key));
        return ownership == StateOwnership::Dynamic ||
               ownership == StateOwnership::Inherited;
    }

    static bool state_dynamic(const PassData &data, VkDynamicState state) noexcept
    {
        if (data.pipeline.pipeline == VK_NULL_HANDLE)
            return true;
        const auto ownership = data.pipeline.ownership.get(state);
        return ownership == StateOwnership::Dynamic ||
               ownership == StateOwnership::Inherited;
    }

    static bool compatible(const PipelineRenderingInfo &rendering,
                           const PassTarget &target) noexcept
    {
        // If the app did not provide pipeline rendering metadata, skip the
        // compatibility layer rather than rejecting valid pipelines.
        if (rendering.colorCount == 0)
            return true;
        if (rendering.colorCount != target.colorCount)
            return false;
        for (std::uint32_t i = 0; i < rendering.colorCount; ++i)
        {
            const VkFormat a = rendering.colorFormats[i];
            const VkFormat b = target.colorFormats[i];
            if (a != VK_FORMAT_UNDEFINED && b != VK_FORMAT_UNDEFINED && a != b)
                return false;
        }
        if (rendering.depthFormat != VK_FORMAT_UNDEFINED &&
            target.depthFormat != VK_FORMAT_UNDEFINED &&
            rendering.depthFormat != target.depthFormat)
            return false;
        if (rendering.stencilFormat != VK_FORMAT_UNDEFINED &&
            target.stencilFormat != VK_FORMAT_UNDEFINED &&
            rendering.stencilFormat != target.stencilFormat)
            return false;
        if (rendering.viewMask != target.viewMask)
            return false;
        if (rendering.samples != target.samples)
            return false;
        return true;
    }

    static bool inheritance_compatible(const InheritanceRenderingInfo &inheritance,
                                       const PassTarget &target) noexcept
    {
        const VkRenderingFlags expectedFlags =
            target.flags & ~VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT;
        if (inheritance.flags != expectedFlags)
            return false;
        if (inheritance.viewMask != target.viewMask)
            return false;
        if (inheritance.colorAttachmentCount != target.colorCount)
            return false;
        for (std::uint32_t i = 0; i < inheritance.colorAttachmentCount; ++i)
        {
            const VkFormat a = inheritance.colorFormats[i];
            const VkFormat b = target.colorFormats[i];
            if (a != VK_FORMAT_UNDEFINED && b != VK_FORMAT_UNDEFINED && a != b)
                return false;
        }
        if (inheritance.depthFormat != VK_FORMAT_UNDEFINED &&
            target.depthFormat != VK_FORMAT_UNDEFINED &&
            inheritance.depthFormat != target.depthFormat)
            return false;
        if (inheritance.stencilFormat != VK_FORMAT_UNDEFINED &&
            target.stencilFormat != VK_FORMAT_UNDEFINED &&
            inheritance.stencilFormat != target.stencilFormat)
            return false;
        if (inheritance.rasterizationSamples != target.samples)
            return false;
        return true;
    }

    bool fail(const char *message)
    {
        std::println(stderr, "[pass-validator] {}", message);
        valid_ = false;
        return false;
    }

    bool valid_{true};
    bool passActive_{false};
    bool pipelineBound_{false};
    bool descriptorsBound_{false};
};

} // namespace pass_data
