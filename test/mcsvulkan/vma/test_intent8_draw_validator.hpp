#pragma once
// ============================================================================
// test_intent8_draw_validator.hpp
//
// Incremental validity checks for the draw pipeline. It enforces the reality
// constraints from the blueprint:
//
//   - a draw needs an active render target;
//   - the pipeline layout / descriptor set layout / push range must match;
//   - pipeline rendering formats/samples must match the render target;
//   - dynamic state used by the draw must be declared by the pipeline;
//   - indexed draws need an index buffer; indirect draws need a valid buffer;
//   - a descriptor set must be bound before draw.
//
// This is not a full Vulkan validator. It is the drawing layer's own contract.
// ============================================================================

#include "test_intent8_draw_pipeline.hpp"
#include "test_intent8_draw_ops.hpp"

#include <print>

namespace intent8
{

class DrawValidator
{
  public:
    [[nodiscard]] bool valid() const noexcept { return valid_; }

    bool beginTarget(const RenderTarget &target) noexcept
    {
        if (targetActive_)
            return fail("beginTarget while a target is already active");
        if (target.config.renderArea.extent.width == 0 ||
            target.config.renderArea.extent.height == 0)
            return fail("render target has zero render area");
        if (target.colorCount == 0 && !target.hasDepth && !target.hasStencil)
            return fail("render target has no attachments");
        if (target.config.samples == 0)
            return fail("render target has no sample count");
        target_ = &target;
        targetActive_ = true;
        pipeline_ = nullptr;
        descriptorsBound_ = false;
        indexBuffer_ = VK_NULL_HANDLE;
        return true;
    }

    bool beginTargetInherited(const RenderTarget &inherited) noexcept
    {
        if (targetActive_)
            return fail("beginTargetInherited while a target is active");
        target_ = &inherited;
        targetActive_ = true;
        pipeline_ = nullptr;
        descriptorsBound_ = false;
        indexBuffer_ = VK_NULL_HANDLE;
        inherited_ = true;
        return true;
    }

    bool endTarget() noexcept
    {
        if (!targetActive_)
            return fail("endTarget without an active target");
        targetActive_ = false;
        target_ = nullptr;
        pipeline_ = nullptr;
        descriptorsBound_ = false;
        inherited_ = false;
        return true;
    }

    bool bindPipeline(const DrawPipeline &pipeline) noexcept
    {
        if (!pipeline.valid())
            return fail("bindPipeline with an incomplete pipeline");
        if (pipeline.config->bindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS)
            return fail("bindPipeline with a non-graphics pipeline");
        if (targetActive_ &&
            !pipeline.config->rendering.matchesTargetFormats(target_->config.rendering()))
            return fail("pipeline rendering formats do not match the target");
        pipeline_ = &pipeline;
        descriptorsBound_ = false;
        indexBuffer_ = VK_NULL_HANDLE;
        return true;
    }

    bool bindDescriptorSet(VkPipelineLayout layout, VkDescriptorSet set) noexcept
    {
        if (set == VK_NULL_HANDLE)
            return fail("bindDescriptorSet with VK_NULL_HANDLE");
        if (pipeline_ != nullptr && layout != pipeline_->layout())
            return fail("descriptor set layout is not the pipeline layout");
        descriptorsBound_ = true;
        return true;
    }

    bool pushConstants(VkPipelineLayout layout, VkShaderStageFlags stages,
                       std::uint32_t offset, std::uint32_t size) noexcept
    {
        if (pipeline_ == nullptr)
            return true;
        if (layout != pipeline_->layout())
            return fail("push constants with a different pipeline layout");
        const auto &program = pipeline_->config->shaders;
        if ((stages & ~program.pushConstantStages) != 0)
            return fail("push constant stages exceed the declared push range");
        if (offset < program.pushConstantOffset ||
            offset + size > program.pushConstantOffset + program.pushConstantSize)
            return fail("push constant bytes exceed the declared push range");
        return true;
    }

    bool setViewport() noexcept
    {
        if (pipeline_ != nullptr &&
            !pipeline_->config->dynamicState.has(VK_DYNAMIC_STATE_VIEWPORT))
            return fail("viewport is set but not declared dynamic");
        return true;
    }

    bool setScissor() noexcept
    {
        if (pipeline_ != nullptr &&
            !pipeline_->config->dynamicState.has(VK_DYNAMIC_STATE_SCISSOR))
            return fail("scissor is set but not declared dynamic");
        return true;
    }

    bool bindIndexBuffer(VkBuffer buffer) noexcept
    {
        if (buffer == VK_NULL_HANDLE)
            return fail("bindIndexBuffer with VK_NULL_HANDLE");
        indexBuffer_ = buffer;
        return true;
    }

    bool draw(const DrawCall &call) noexcept
    {
        if (!targetActive_ || target_ == nullptr)
            return fail("draw without an active render target");
        if (call.pipelineData == nullptr || !call.pipelineData->valid())
            return fail("draw without a valid DrawPipelineData");
        const DrawPipeline &pipeline = *call.pipelineData->pipeline;
        const DrawResources &resources = *call.pipelineData->resources;
        if (!pipeline.config->rendering.matchesTargetFormats(
                target_->config.rendering()))
            return fail("draw pipeline formats do not match the target");
        if (pipeline_ != &pipeline)
            return fail("draw pipeline is not the bound pipeline");
        if (!descriptorsBound_)
            return fail("draw without a bound descriptor set");
        if (resources.descriptors.layout != pipeline.descriptorLayout)
            return fail("draw descriptor set layout is not the pipeline's layout");

        const auto &args = call.args;
        switch (args.kind)
        {
        case DrawKind::Draw:
        case DrawKind::DrawIndexed:
            if (args.kind == DrawKind::DrawIndexed &&
                resources.indexBuffer == VK_NULL_HANDLE)
                return fail("indexed draw without an index buffer");
            break;
        case DrawKind::DrawIndirect:
        case DrawKind::DrawIndexedIndirect: {
            if (resources.indexBuffer == VK_NULL_HANDLE)
                return fail("indexed indirect draw without an index buffer");
            if (args.indirectBuffer == VK_NULL_HANDLE)
                return fail("indirect draw with VK_NULL_HANDLE indirect buffer");
            if (args.drawCount == 0)
                return fail("indirect draw with drawCount 0");
            if (args.stride == 0 || (args.stride % 4) != 0)
                return fail("indirect draw with invalid stride");
            break;
        }
        case DrawKind::DrawIndexedIndirectCount:
            if (args.indirectBuffer == VK_NULL_HANDLE ||
                args.countBuffer == VK_NULL_HANDLE)
                return fail("indirect-count draw with a null buffer");
            break;
        }

        const auto &push = resources.pushConstants;
        if (push.size != 0)
        {
            if (push.layout != pipeline.layout())
                return fail("draw push constants use a different pipeline layout");
            if (push.offset < pipeline.config->shaders.pushConstantOffset ||
                push.offset + push.size >
                    pipeline.config->shaders.pushConstantOffset +
                        pipeline.config->shaders.pushConstantSize)
                return fail("draw push constants exceed the declared push range");
        }

        if (resources.dynamicViewport &&
            !pipeline.config->dynamicState.has(VK_DYNAMIC_STATE_VIEWPORT))
            return fail("draw viewport is not declared dynamic");
        if (resources.dynamicScissor &&
            !pipeline.config->dynamicState.has(VK_DYNAMIC_STATE_SCISSOR))
            return fail("draw scissor is not declared dynamic");

        return true;
    }

  private:
    bool fail(const char *message) noexcept
    {
        std::println(stderr, "[intent8-validator] {}", message);
        valid_ = false;
        return false;
    }

    bool valid_{true};
    bool targetActive_{false};
    bool inherited_{false};
    bool descriptorsBound_{false};
    const RenderTarget *target_{nullptr};
    const DrawPipeline *pipeline_{nullptr};
    VkBuffer indexBuffer_{VK_NULL_HANDLE};
};

} // namespace intent8
