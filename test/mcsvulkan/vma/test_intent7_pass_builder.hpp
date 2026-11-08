#pragma once
// ============================================================================
// test_intent7_pass_builder.hpp
//
// FrameBuilder = the linear recorder/compiler:
//   beginPass / state ops / action ops / sync ops / endPass
//   -> OpStream (data)
//   -> PassValidator (incremental constraints)
//   -> VkEmitter (explicit vkCmd*)
//
// Methods intentionally match shader_data::emit's recorder surface so the
// existing UI CommandStream can be lowered into this builder without changing
// the DAO/shader generator.
// ============================================================================

#include "test_intent7_pass_emitter.hpp"
#include "test_intent7_pass_validator.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <span>
#include <string>

namespace pass_data
{

// ============================================================================
// SecondaryBuilder
//
// Same data/operation/emitter/validator path as FrameBuilder, but without
// BeginRendering/EndRendering. It is used to record a secondary command buffer
// with VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT.
//
// The inherited pass target comes from InheritanceRenderingInfo, so the same
// PassValidator checks pipeline-target compatibility, descriptor layout,
// dynamic-state ownership and feature gates for the secondary body.
// ============================================================================
class SecondaryBuilder
{
  public:
    SecondaryBuilder(mcs::vulkan::CommandBufferView cmd,
                     const InheritanceRenderingInfo &inheritance,
                     const PipelineRenderingInfo &pipelineRendering,
                     VkPipelineLayout pipelineLayout,
                     const StateOwnershipMap &stateOwnership,
                     const FeatureSet &features)
        : emitter_{cmd}, pipelineRenderingInfo_{pipelineRendering},
          pipelineLayout_{pipelineLayout}, stateOwnership_{stateOwnership},
          features_{&features}
    {
        stream_.reserve(16);
        data_.target.flags = inheritance.flags;
        data_.target.viewMask = inheritance.viewMask;
        data_.target.colorCount = inheritance.colorAttachmentCount;
        data_.target.colorFormats = inheritance.colorFormats;
        data_.target.depthFormat = inheritance.depthFormat;
        data_.target.stencilFormat = inheritance.stencilFormat;
        data_.target.samples = inheritance.rasterizationSamples;
        data_.passActive = true;
        data_.features = features_;
        data_.pipeline.rendering = pipelineRenderingInfo_;
        data_.pipeline.layout = pipelineLayout_;
        data_.pipeline.ownership = stateOwnership_;
        validator_.beginInherited(data_.target);
    }

    void bindPipeline(VkPipelineBindPoint bindPoint, VkPipeline pipeline)
    {
        assert(bindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS);
        data_.pipeline.bindPoint = bindPoint;
        data_.pipeline.pipeline = pipeline;

        Op op{};
        op.opcode = Opcode::BindPipeline;
        op.scope = Scope::Inside;
        op.lifecycle = Lifecycle::CommandBuffer;
        op.payload = BindPipelinePayload{.bindPoint = bindPoint,
                                         .pipeline = pipeline,
                                         .layout = data_.pipeline.layout};
        push(std::move(op));
    }

    void bindDescriptorSet(VkPipelineBindPoint bindPoint, VkPipelineLayout layout,
                           std::uint32_t firstSet, VkDescriptorSet set)
    {
        assert(bindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS);
        assert(firstSet == 0);
        data_.descriptors.layout = layout;
        data_.descriptors.sets[0] = set;
        data_.descriptors.firstSet = firstSet;
        data_.descriptors.setCount = 1;

        Op op{};
        op.opcode = Opcode::BindDescriptorSet;
        op.scope = Scope::Inside;
        op.lifecycle = Lifecycle::CommandBuffer;
        op.payload = BindDescriptorSetPayload{.bindPoint = bindPoint,
                                              .layout = layout,
                                              .set = set};
        push(std::move(op));
    }

    void pushConstants(VkPipelineLayout layout, VkShaderStageFlags stages,
                       std::uint32_t offset, std::uint32_t size, const void *data)
    {
        assert(size <= PushConstants::MAX_BYTES);
        data_.pushConstants.layout = layout;
        data_.pushConstants.stages = stages;
        data_.pushConstants.offset = offset;
        data_.pushConstants.setBytes(data, size);

        PushConstantsPayload payload{};
        payload.layout = layout;
        payload.stages = stages;
        payload.offset = offset;
        payload.size = size;
        if (data != nullptr && size != 0)
            std::memcpy(payload.data.data(), data, size);

        Op op{};
        op.opcode = Opcode::PushConstants;
        op.scope = Scope::Inside;
        op.lifecycle = Lifecycle::Draw;
        op.payload = std::move(payload);
        push(std::move(op));
    }

    void setViewport(std::uint32_t first, std::span<const VkViewport> viewports)
    {
        assert(first == 0 && !viewports.empty());
        data_.dynamicState.setValue(VK_DYNAMIC_STATE_VIEWPORT, &viewports.front(),
                                    sizeof(VkViewport));

        Op op{};
        op.opcode = Opcode::SetViewport;
        op.kind = OpKind::Viewport;
        op.scope = Scope::Inside;
        op.lifecycle = Lifecycle::CommandBuffer;
        op.payload = SetViewportPayload{.viewport = viewports.front()};
        push(std::move(op));
    }

    void setScissor(std::uint32_t first, std::span<const VkRect2D> scissors)
    {
        assert(first == 0 && !scissors.empty());
        data_.dynamicState.setValue(VK_DYNAMIC_STATE_SCISSOR, &scissors.front(),
                                    sizeof(VkRect2D));

        Op op{};
        op.opcode = Opcode::SetScissor;
        op.kind = OpKind::Scissor;
        op.scope = Scope::Inside;
        op.lifecycle = Lifecycle::CommandBuffer;
        op.payload = SetScissorPayload{.scissor = scissors.front()};
        push(std::move(op));
    }

    void bindIndexBuffer(VkBuffer buffer, VkDeviceSize offset, VkIndexType type)
    {
        data_.vertexInput.indexBuffer = buffer;
        data_.vertexInput.indexOffset = offset;
        data_.vertexInput.indexType = type;

        Op op{};
        op.opcode = Opcode::BindIndexBuffer;
        op.scope = Scope::Inside;
        op.lifecycle = Lifecycle::CommandBuffer;
        op.payload = BindIndexBufferPayload{.buffer = buffer, .offset = offset,
                                            .type = type};
        push(std::move(op));
    }

    void drawIndexedIndirect(VkBuffer buffer, VkDeviceSize offset,
                             std::uint32_t drawCount, std::uint32_t stride)
    {
        data_.draw.kind = DrawKind::DrawIndexedIndirect;
        data_.draw.buffer = buffer;
        data_.draw.offset = offset;
        data_.draw.drawCount = drawCount;
        data_.draw.stride = stride;

        Op op{};
        op.opcode = Opcode::DrawIndexedIndirect;
        op.kind = OpKind::DrawIndexedIndirect;
        op.scope = Scope::Inside;
        op.lifecycle = Lifecycle::Draw;
        op.payload = DrawIndexedIndirectPayload{.buffer = buffer,
                                                .offset = offset,
                                                .drawCount = drawCount,
                                                .stride = stride};
        push(std::move(op));
    }

    [[nodiscard]] const OpStream &stream() const noexcept { return stream_; }
    [[nodiscard]] bool valid() const noexcept { return validator_.valid(); }
    [[nodiscard]] std::string dump() const { return pass_data::dump(stream_); }

  private:
    void push(Op op)
    {
        const bool ok = validator_.validate(op, data_);
        assert(ok && "SecondaryBuilder validator rejected an op");
        (void)ok;
        stream_.add(op);
        emitter_.emit(op);
    }

    VkEmitter emitter_;
    PassValidator validator_;
    PipelineRenderingInfo pipelineRenderingInfo_{};
    VkPipelineLayout pipelineLayout_{};
    StateOwnershipMap stateOwnership_{};
    const FeatureSet *features_{nullptr};
    PassData data_{};
    OpStream stream_{};
};

class FrameBuilder
{
  public:
    explicit FrameBuilder(mcs::vulkan::CommandBufferView cmd,
                          bool deferEmit = false) noexcept
        : emitter_{cmd}, deferred_{deferEmit}
    {
        stream_.reserve(64);
    }

    // Pipeline rendering compatibility metadata (formats/viewMask/samples).
    // Set once per pipeline family before recording passes.
    void setPipelineRenderingInfo(const PipelineRenderingInfo &info)
    {
        pipelineRenderingInfo_ = info;
    }

    void setPipelineLayout(VkPipelineLayout layout)
    {
        pipelineLayout_ = layout;
    }

    void setPipelineStateOwnership(const StateOwnershipMap &ownership)
    {
        stateOwnership_ = ownership;
    }

    void setFeatures(const FeatureSet &features)
    {
        features_ = &features;
    }

    // ---- pass boundary ---------------------------------------------------
    void beginPass(PassTarget target)
    {
        target.kind = PassKind::Graphics;
        data_ = PassData{};
        data_.features = features_;
        data_.target = std::move(target);
        data_.passActive = true;

        Op op{};
        op.opcode = Opcode::BeginRendering;
        op.scope = Scope::Outside; // beginRendering itself is outside
        op.lifecycle = Lifecycle::Pass;
        op.payload = BeginRenderingPayload{data_.target};
        push(std::move(op));
    }

    void endPass()
    {
        Op op{};
        op.opcode = Opcode::EndRendering;
        op.scope = Scope::Inside;
        op.lifecycle = Lifecycle::Pass;
        op.payload = std::monostate{};
        push(std::move(op));
        data_.passActive = false;
    }

    // ---- non-graphics pass scopes ---------------------------------------
    // Compute/transfer have no vkCmdBeginRendering/EndRendering; the scope is
    // a logical recording scope carried by PassData + PassValidator.
    void beginComputePass()
    {
        data_ = PassData{};
        data_.features = features_;
        data_.target.kind = PassKind::Compute;
        data_.passActive = true;
        validator_.beginScope(data_.target);
    }

    void endComputePass()
    {
        data_.passActive = false;
        validator_.endScope();
    }

    void beginTransferPass()
    {
        data_ = PassData{};
        data_.features = features_;
        data_.target.kind = PassKind::Transfer;
        data_.passActive = true;
        validator_.beginScope(data_.target);
    }

    void endTransferPass()
    {
        data_.passActive = false;
        validator_.endScope();
    }

    // ---- state -----------------------------------------------------------
    void bindPipeline(VkPipelineBindPoint bindPoint, VkPipeline pipeline)
    {
        if (data_.target.kind == PassKind::Graphics)
            assert(bindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS);
        else
            assert(bindPoint == VK_PIPELINE_BIND_POINT_COMPUTE);
        data_.pipeline.bindPoint = bindPoint;
        data_.pipeline.pipeline = pipeline;
        data_.pipeline.layout = pipelineLayout_;
        data_.pipeline.rendering = pipelineRenderingInfo_;
        data_.pipeline.ownership = stateOwnership_;

        Op op{};
        op.opcode = Opcode::BindPipeline;
        op.scope = Scope::Inside;
        op.lifecycle = Lifecycle::CommandBuffer;
        op.payload = BindPipelinePayload{.bindPoint = bindPoint,
                                         .pipeline = pipeline,
                                         .layout = data_.pipeline.layout};
        push(std::move(op));
    }

    void bindDescriptorSet(VkPipelineBindPoint bindPoint, VkPipelineLayout layout,
                           std::uint32_t firstSet, VkDescriptorSet set)
    {
        if (data_.target.kind == PassKind::Graphics)
            assert(bindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS);
        else
            assert(bindPoint == VK_PIPELINE_BIND_POINT_COMPUTE);
        assert(firstSet == 0);
        data_.descriptors.layout = layout;
        data_.descriptors.sets[0] = set;
        data_.descriptors.firstSet = firstSet;
        data_.descriptors.setCount = 1;

        Op op{};
        op.opcode = Opcode::BindDescriptorSet;
        op.scope = Scope::Inside;
        op.lifecycle = Lifecycle::CommandBuffer;
        op.payload = BindDescriptorSetPayload{.bindPoint = bindPoint,
                                              .layout = layout,
                                              .set = set};
        push(std::move(op));
    }

    void bindDescriptorBuffer(VkPipelineLayout layout,
                              const VkDescriptorBufferBindingInfoEXT &binding,
                              VkDeviceSize offset)
    {
        data_.pipeline.layout = layout;
        data_.descriptors.kind = DescriptorBinding::Kind::Buffer;
        data_.descriptors.layout = layout;
        data_.descriptors.bufferBindings[0] = binding;
        data_.descriptors.bufferBindingCount = 1;
        data_.descriptors.bufferOffsets[0] = static_cast<std::uint32_t>(offset);
        data_.descriptors.bufferOffsetCount = 1;

        Op op{};
        op.opcode = Opcode::BindDescriptorBuffer;
        op.scope = Scope::Inside;
        op.lifecycle = Lifecycle::CommandBuffer;
        op.payload = BindDescriptorBufferPayload{.layout = layout,
                                                 .binding = binding,
                                                 .offset = offset};
        push(std::move(op));
    }

    void bindResourceHeap(const VkBindHeapInfoEXT &info)
    {
        data_.descriptors.kind = DescriptorBinding::Kind::Heap;
        data_.descriptors.resourceHeap = info;
        data_.descriptors.resourceHeapBound = true;

        Op op{};
        op.opcode = Opcode::BindResourceHeap;
        op.scope = Scope::Inside;
        op.lifecycle = Lifecycle::CommandBuffer;
        op.payload = BindHeapPayload{.info = info, .sampler = false};
        push(std::move(op));
    }

    void bindSamplerHeap(const VkBindHeapInfoEXT &info)
    {
        data_.descriptors.samplerHeap = info;
        data_.descriptors.samplerHeapBound = true;

        Op op{};
        op.opcode = Opcode::BindSamplerHeap;
        op.scope = Scope::Inside;
        op.lifecycle = Lifecycle::CommandBuffer;
        op.payload = BindHeapPayload{.info = info, .sampler = true};
        push(std::move(op));
    }

    void pushConstants(VkPipelineLayout layout, VkShaderStageFlags stages,
                       std::uint32_t offset, std::uint32_t size, const void *data)
    {
        assert(size <= PushConstants::MAX_BYTES);
        data_.pushConstants.layout = layout;
        data_.pushConstants.stages = stages;
        data_.pushConstants.offset = offset;
        data_.pushConstants.setBytes(data, size);

        PushConstantsPayload payload{};
        payload.layout = layout;
        payload.stages = stages;
        payload.offset = offset;
        payload.size = size;
        if (data != nullptr && size != 0)
            std::memcpy(payload.data.data(), data, size);

        Op op{};
        op.opcode = Opcode::PushConstants;
        op.scope = Scope::Inside;
        op.lifecycle = Lifecycle::Draw;
        op.payload = payload;
        push(std::move(op));
    }

    // ---- dynamic state ---------------------------------------------------
    void setViewport(std::uint32_t first, std::span<const VkViewport> viewports)
    {
        assert(first == 0 && !viewports.empty());
        data_.dynamicState.setValue(VK_DYNAMIC_STATE_VIEWPORT, &viewports.front(),
                                    sizeof(VkViewport));

        Op op{};
        op.opcode = Opcode::SetViewport;
        op.kind = OpKind::Viewport;
        op.scope = Scope::Inside;
        op.lifecycle = Lifecycle::CommandBuffer;
        op.payload = SetViewportPayload{.viewport = viewports.front()};
        push(std::move(op));
    }

    void setScissor(std::uint32_t first, std::span<const VkRect2D> scissors)
    {
        assert(first == 0 && !scissors.empty());
        data_.dynamicState.setValue(VK_DYNAMIC_STATE_SCISSOR, &scissors.front(),
                                    sizeof(VkRect2D));

        Op op{};
        op.opcode = Opcode::SetScissor;
        op.kind = OpKind::Scissor;
        op.scope = Scope::Inside;
        op.lifecycle = Lifecycle::CommandBuffer;
        op.payload = SetScissorPayload{.scissor = scissors.front()};
        push(std::move(op));
    }

    void setDepthTestEnable(bool enable)
    {
        const VkBool32 value = static_cast<VkBool32>(enable);
        data_.dynamicState.setValue(VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE, &value,
                                    sizeof(value));

        Op op{};
        op.opcode = Opcode::SetDepthTestEnable;
        op.scope = Scope::Inside;
        op.lifecycle = Lifecycle::CommandBuffer;
        op.payload = SetBoolPayload{.enable = value};
        push(std::move(op));
    }

    void setDepthWriteEnable(bool enable)
    {
        const VkBool32 value = static_cast<VkBool32>(enable);
        data_.dynamicState.setValue(VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, &value,
                                    sizeof(value));

        Op op{};
        op.opcode = Opcode::SetDepthWriteEnable;
        op.scope = Scope::Inside;
        op.lifecycle = Lifecycle::CommandBuffer;
        op.payload = SetBoolPayload{.enable = value};
        push(std::move(op));
    }

    void setCullMode(VkCullModeFlags cullMode)
    {
        data_.dynamicState.setValue(VK_DYNAMIC_STATE_CULL_MODE, &cullMode,
                                    sizeof(cullMode));

        Op op{};
        op.opcode = Opcode::SetCullMode;
        op.scope = Scope::Inside;
        op.lifecycle = Lifecycle::CommandBuffer;
        op.payload = SetCullModePayload{.cullMode = cullMode};
        push(std::move(op));
    }

    void setFrontFace(VkFrontFace frontFace)
    {
        data_.dynamicState.setValue(VK_DYNAMIC_STATE_FRONT_FACE, &frontFace,
                                    sizeof(frontFace));

        Op op{};
        op.opcode = Opcode::SetFrontFace;
        op.scope = Scope::Inside;
        op.lifecycle = Lifecycle::CommandBuffer;
        op.payload = SetFrontFacePayload{.frontFace = frontFace};
        push(std::move(op));
    }

    void setPrimitiveTopology(VkPrimitiveTopology topology)
    {
        data_.dynamicState.setValue(VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY, &topology,
                                    sizeof(topology));

        Op op{};
        op.opcode = Opcode::SetPrimitiveTopology;
        op.scope = Scope::Inside;
        op.lifecycle = Lifecycle::CommandBuffer;
        op.payload = SetPrimitiveTopologyPayload{.topology = topology};
        push(std::move(op));
    }

    void setStencilReference(VkStencilFaceFlags faceMask, std::uint32_t reference)
    {
        const struct
        {
            VkStencilFaceFlags faceMask;
            std::uint32_t reference;
        } value{faceMask, reference};
        data_.dynamicState.setValue(VK_DYNAMIC_STATE_STENCIL_REFERENCE, &value,
                                    sizeof(value));

        Op op{};
        op.opcode = Opcode::SetStencilReference;
        op.scope = Scope::Inside;
        op.lifecycle = Lifecycle::CommandBuffer;
        op.payload = SetStencilReferencePayload{.faceMask = faceMask,
                                                .reference = reference};
        push(std::move(op));
    }

    // ---- vertex / index input -------------------------------------------
    void bindIndexBuffer(VkBuffer buffer, VkDeviceSize offset, VkIndexType type)
    {
        data_.vertexInput.indexBuffer = buffer;
        data_.vertexInput.indexOffset = offset;
        data_.vertexInput.indexType = type;

        Op op{};
        op.opcode = Opcode::BindIndexBuffer;
        op.scope = Scope::Inside;
        op.lifecycle = Lifecycle::CommandBuffer;
        op.payload = BindIndexBufferPayload{.buffer = buffer, .offset = offset,
                                            .type = type};
        push(std::move(op));
    }

    // ---- action ----------------------------------------------------------
    void draw(std::uint32_t vertexCount, std::uint32_t instanceCount = 1,
              std::uint32_t firstVertex = 0, std::uint32_t firstInstance = 0)
    {
        data_.draw.kind = DrawKind::Draw;
        data_.draw.vertexCount = vertexCount;
        data_.draw.instanceCount = instanceCount;
        data_.draw.firstVertex = firstVertex;
        data_.draw.firstInstance = firstInstance;

        Op op{};
        op.opcode = Opcode::Draw;
        op.kind = OpKind::None;
        op.scope = Scope::Inside;
        op.lifecycle = Lifecycle::Draw;
        op.payload = DrawPayload{.vertexCount = vertexCount,
                                 .instanceCount = instanceCount,
                                 .firstVertex = firstVertex,
                                 .firstInstance = firstInstance};
        push(std::move(op));
    }

    void drawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount = 1,
                     std::uint32_t firstIndex = 0, std::int32_t vertexOffset = 0,
                     std::uint32_t firstInstance = 0)
    {
        data_.draw.kind = DrawKind::DrawIndexed;
        data_.draw.indexCount = indexCount;
        data_.draw.instanceCount = instanceCount;
        data_.draw.firstIndex = firstIndex;
        data_.draw.vertexOffset = vertexOffset;
        data_.draw.firstInstance = firstInstance;

        Op op{};
        op.opcode = Opcode::DrawIndexed;
        op.scope = Scope::Inside;
        op.lifecycle = Lifecycle::Draw;
        op.payload = DrawIndexedPayload{.indexCount = indexCount,
                                        .instanceCount = instanceCount,
                                        .firstIndex = firstIndex,
                                        .vertexOffset = vertexOffset,
                                        .firstInstance = firstInstance};
        push(std::move(op));
    }

    void drawIndirect(VkBuffer buffer, VkDeviceSize offset, std::uint32_t drawCount,
                      std::uint32_t stride)
    {
        data_.draw.kind = DrawKind::DrawIndirect;
        data_.draw.buffer = buffer;
        data_.draw.offset = offset;
        data_.draw.drawCount = drawCount;
        data_.draw.stride = stride;

        Op op{};
        op.opcode = Opcode::DrawIndirect;
        op.scope = Scope::Inside;
        op.lifecycle = Lifecycle::Draw;
        op.payload = DrawIndirectPayload{.buffer = buffer,
                                         .offset = offset,
                                         .drawCount = drawCount,
                                         .stride = stride};
        push(std::move(op));
    }

    void drawIndexedIndirect(VkBuffer buffer, VkDeviceSize offset,
                             std::uint32_t drawCount, std::uint32_t stride)
    {
        data_.draw.kind = DrawKind::DrawIndexedIndirect;
        data_.draw.buffer = buffer;
        data_.draw.offset = offset;
        data_.draw.drawCount = drawCount;
        data_.draw.stride = stride;

        Op op{};
        op.opcode = Opcode::DrawIndexedIndirect;
        op.kind = OpKind::DrawIndexedIndirect;
        op.scope = Scope::Inside;
        op.lifecycle = Lifecycle::Draw;
        op.payload = DrawIndexedIndirectPayload{.buffer = buffer,
                                                .offset = offset,
                                                .drawCount = drawCount,
                                                .stride = stride};
        push(std::move(op));
    }

    // ---- transfer --------------------------------------------------------
    void copyImageToBuffer(VkImage image, VkImageLayout imageLayout,
                           VkBuffer buffer,
                           std::span<const VkBufferImageCopy> regions)
    {
        CopyImageToBufferPayload payload{};
        payload.image = image;
        payload.imageLayout = imageLayout;
        payload.buffer = buffer;
        assert(regions.size() <= CopyImageToBufferPayload::CAPACITY);
        payload.regionCount = static_cast<std::uint32_t>(regions.size());
        std::copy(regions.begin(), regions.end(), payload.regions.begin());

        Op op{};
        op.opcode = Opcode::CopyImageToBuffer;
        op.scope = Scope::Outside;
        op.lifecycle = Lifecycle::Sync;
        op.payload = std::move(payload);
        push(std::move(op));
    }

    // ---- clear / query / indirection ------------------------------------
    void clearAttachments(std::span<const VkClearAttachment> attachments,
                          std::span<const VkClearRect> rects)
    {
        ClearAttachmentsPayload payload{};
        payload.attachments.assign(attachments.begin(), attachments.end());
        payload.rects.assign(rects.begin(), rects.end());

        Op op{};
        op.opcode = Opcode::ClearAttachments;
        op.scope = Scope::Inside;
        op.lifecycle = Lifecycle::Draw;
        op.payload = std::move(payload);
        push(std::move(op));
    }

    void beginQuery(VkQueryPool pool, std::uint32_t query,
                    VkQueryControlFlags flags = 0)
    {
        Op op{};
        op.opcode = Opcode::BeginQuery;
        op.scope = Scope::Inside;
        op.lifecycle = Lifecycle::Sync;
        op.payload = QueryPayload{.pool = pool, .query = query, .flags = flags};
        push(std::move(op));
    }

    void endQuery(VkQueryPool pool, std::uint32_t query)
    {
        Op op{};
        op.opcode = Opcode::EndQuery;
        op.scope = Scope::Inside;
        op.lifecycle = Lifecycle::Sync;
        op.payload = QueryPayload{.pool = pool, .query = query, .flags = 0};
        push(std::move(op));
    }

    void executeCommands(std::span<const VkCommandBuffer> commandBuffers,
                         const InheritanceRenderingInfo &inheritance = {})
    {
        ExecuteCommandsPayload payload{};
        assert(commandBuffers.size() <= ExecuteCommandsPayload::CAPACITY);
        payload.commandBufferCount =
            static_cast<std::uint32_t>(commandBuffers.size());
        std::copy(commandBuffers.begin(), commandBuffers.end(),
                  payload.commandBuffers.begin());
        payload.inheritance = inheritance;

        Op op{};
        op.opcode = Opcode::ExecuteCommands;
        op.scope = Scope::Inside;
        op.lifecycle = Lifecycle::Inheritance;
        op.payload = std::move(payload);
        push(std::move(op));
    }

    void copyBuffer(VkBuffer src, VkBuffer dst,
                    std::span<const VkBufferCopy> regions)
    {
        CopyBufferPayload payload{};
        payload.src = src;
        payload.dst = dst;
        assert(regions.size() <= CopyBufferPayload::CAPACITY);
        payload.regionCount = static_cast<std::uint32_t>(regions.size());
        std::copy(regions.begin(), regions.end(), payload.regions.begin());
        push_transfer(Opcode::CopyBuffer, std::move(payload));
    }

    void copyBufferToImage(VkBuffer src, VkImage dst, VkImageLayout dstLayout,
                           std::span<const VkBufferImageCopy> regions)
    {
        CopyBufferToImagePayload payload{};
        payload.src = src;
        payload.dst = dst;
        payload.dstLayout = dstLayout;
        payload.regions.assign(regions.begin(), regions.end());
        push_transfer(Opcode::CopyBufferToImage, std::move(payload));
    }

    void copyImage(VkImage src, VkImageLayout srcLayout, VkImage dst,
                   VkImageLayout dstLayout, std::span<const VkImageCopy> regions)
    {
        CopyImagePayload payload{};
        payload.src = src;
        payload.srcLayout = srcLayout;
        payload.dst = dst;
        payload.dstLayout = dstLayout;
        payload.regions.assign(regions.begin(), regions.end());
        push_transfer(Opcode::CopyImage, std::move(payload));
    }

    void blitImage(VkImage src, VkImageLayout srcLayout, VkImage dst,
                   VkImageLayout dstLayout, std::span<const VkImageBlit> regions,
                   VkFilter filter)
    {
        BlitImagePayload payload{};
        payload.src = src;
        payload.srcLayout = srcLayout;
        payload.dst = dst;
        payload.dstLayout = dstLayout;
        assert(regions.size() <= BlitImagePayload::CAPACITY);
        payload.regionCount = static_cast<std::uint32_t>(regions.size());
        std::copy(regions.begin(), regions.end(), payload.regions.begin());
        payload.filter = filter;
        push_transfer(Opcode::BlitImage, std::move(payload));
    }

    void fillBuffer(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size,
                    std::uint32_t data)
    {
        FillBufferPayload payload{};
        payload.buffer = buffer;
        payload.offset = offset;
        payload.size = size;
        payload.data = data;
        push_transfer(Opcode::FillBuffer, std::move(payload));
    }

    void updateBuffer(VkBuffer buffer, VkDeviceSize offset, const void *data,
                      std::size_t size)
    {
        UpdateBufferPayload payload{};
        payload.buffer = buffer;
        payload.offset = offset;
        const auto *bytes = static_cast<const std::uint8_t *>(data);
        payload.data.assign(bytes, bytes + size);
        push_transfer(Opcode::UpdateBuffer, std::move(payload));
    }

    void clearColorImage(VkImage image, VkImageLayout layout,
                         const VkClearColorValue &color,
                         std::span<const VkImageSubresourceRange> ranges)
    {
        ClearColorImagePayload payload{};
        payload.image = image;
        payload.layout = layout;
        payload.color = color;
        payload.ranges.assign(ranges.begin(), ranges.end());
        push_transfer(Opcode::ClearColorImage, std::move(payload));
    }

    void clearDepthStencilImage(VkImage image, VkImageLayout layout,
                                const VkClearDepthStencilValue &value,
                                std::span<const VkImageSubresourceRange> ranges)
    {
        ClearDepthStencilImagePayload payload{};
        payload.image = image;
        payload.layout = layout;
        payload.value = value;
        payload.ranges.assign(ranges.begin(), ranges.end());
        push_transfer(Opcode::ClearDepthStencilImage, std::move(payload));
    }

    void dispatch(std::uint32_t x, std::uint32_t y = 1, std::uint32_t z = 1)
    {
        DispatchPayload payload{.x = x, .y = y, .z = z};
        push_transfer(Opcode::Dispatch, std::move(payload));
    }

    void dispatchIndirect(VkBuffer buffer, VkDeviceSize offset)
    {
        DispatchIndirectPayload payload{.buffer = buffer, .offset = offset};
        push_transfer(Opcode::DispatchIndirect, std::move(payload));
    }

    // ---- sync ------------------------------------------------------------
    void barrier(const VkMemoryBarrier2 &barrier)
    {
        BarrierPayload payload{};
        payload.memoryBarriers[0] = barrier;
        payload.memoryBarrierCount = 1;
        push_barrier(std::move(payload));
    }

    // Frame-level barrier: copies the arrays out of the transient
    // VkDependencyInfo into owned payload storage.
    void barrier(const VkDependencyInfo &dependency)
    {
        // pNext chains are not owned by BarrierPayload; fail explicitly rather
        // than silently dropping extension data.
        assert(dependency.pNext == nullptr &&
               "VkDependencyInfo::pNext is not supported by BarrierPayload");
        BarrierPayload payload{};
        payload.dependencyFlags = dependency.dependencyFlags;
        assert(dependency.memoryBarrierCount <= BarrierPayload::CAPACITY);
        assert(dependency.bufferMemoryBarrierCount <= BarrierPayload::CAPACITY);
        assert(dependency.imageMemoryBarrierCount <= BarrierPayload::CAPACITY);
        payload.memoryBarrierCount = dependency.memoryBarrierCount;
        payload.bufferBarrierCount = dependency.bufferMemoryBarrierCount;
        payload.imageBarrierCount = dependency.imageMemoryBarrierCount;
        for (std::uint32_t i = 0; i < payload.memoryBarrierCount; ++i)
            payload.memoryBarriers[i] = dependency.pMemoryBarriers[i];
        for (std::uint32_t i = 0; i < payload.bufferBarrierCount; ++i)
            payload.bufferBarriers[i] = dependency.pBufferMemoryBarriers[i];
        for (std::uint32_t i = 0; i < payload.imageBarrierCount; ++i)
            payload.imageBarriers[i] = dependency.pImageMemoryBarriers[i];
        push_barrier(std::move(payload));
    }

    // ---- inspection ------------------------------------------------------
    [[nodiscard]] const OpStream &stream() const noexcept { return stream_; }
    [[nodiscard]] const PassData &data() const noexcept { return data_; }
    [[nodiscard]] bool valid() const noexcept { return validator_.valid(); }
    [[nodiscard]] std::string dump() const { return pass_data::dump(stream_); }

    // Optional pure build-then-emit mode. With immediate emission (default),
    // every push validates + records + emits. With deferEmit=true, the whole
    // OpStream is built and validated first; emitAll() is the single emission
    // point. This makes OpStream a real program instead of a recording log.
    void emitAll()
    {
        if (!deferred_ || emitted_)
            return;
        for (const auto &op : stream_.commands())
            emitter_.emit(op);
        emitted_ = true;
    }

  private:
    void push_transfer(Opcode opcode, OpPayload payload)
    {
        Op op{};
        op.opcode = opcode;
        op.scope = data_.passActive ? Scope::Inside : Scope::Outside;
        op.lifecycle = Lifecycle::Sync;
        op.payload = std::move(payload);
        push(std::move(op));
    }

    void push_barrier(BarrierPayload payload)
    {
        Op op{};
        op.opcode = Opcode::Barrier;
        op.kind = OpKind::BarrierMemory;
        op.scope = data_.passActive ? Scope::Inside : Scope::Outside;
        op.lifecycle = Lifecycle::Sync;
        op.payload = std::move(payload);
        push(std::move(op));
    }

    void push(Op op)
    {
        const bool ok = validator_.validate(op, data_);
        // In debug builds the validator is enforcing, not merely advisory.
        // The app is expected to satisfy scope/lifecycle/compatibility.
        assert(ok && "PassValidator rejected an op");
        (void)ok;
        stream_.add(op);
        if (!deferred_)
            emitter_.emit(op);
    }

    VkEmitter emitter_;
    PassValidator validator_;
    PipelineRenderingInfo pipelineRenderingInfo_{};
    VkPipelineLayout pipelineLayout_{};
    StateOwnershipMap stateOwnership_{};
    const FeatureSet *features_{nullptr};
    PassData data_{};
    OpStream stream_{};
    bool deferred_{false};
    bool emitted_{false};
};

} // namespace pass_data
