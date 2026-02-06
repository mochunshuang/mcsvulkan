#pragma once
#include "CommandPool.hpp"
#include "./tool/sType.hpp"
#include <optional>
#include <span>
#include <ranges>

namespace mcs::vulkan
{
    class CommandBufferView
    {
        friend class CommandBuffer;

        using value_type = VkCommandBuffer;
        const CommandPool *pool_{};
        VkCommandBuffer value_{};

      public:
        constexpr explicit operator bool() const noexcept
        {
            return value_ != nullptr;
        }
        constexpr value_type &operator*() noexcept
        {
            return value_;
        }
        constexpr const value_type &operator*() const noexcept
        {
            return value_;
        }

        CommandBufferView() = default;
        CommandBufferView(const CommandBufferView &) = default;
        CommandBufferView(CommandBufferView &&) = default;
        CommandBufferView &operator=(const CommandBufferView &) = default;
        CommandBufferView &operator=(CommandBufferView &&) = default;
        constexpr explicit CommandBufferView(const CommandPool *pool,
                                             VkCommandBuffer value) noexcept
            : pool_{pool}, value_{value}
        {
        }
        ~CommandBufferView() = default;

        struct begin_info
        {
            /*
            typedef struct VkCommandBufferInheritanceInfo {
                VkStructureType                  sType;
                const void*                      pNext;
                VkRenderPass                     renderPass;
                uint32_t                         subpass;
                VkFramebuffer                    framebuffer;
                VkBool32                         occlusionQueryEnable;
                VkQueryControlFlags              queryFlags;
                VkQueryPipelineStatisticFlags    pipelineStatistics;
            } VkCommandBufferInheritanceInfo;
            */
            struct command_buffer_inheritance_info // NOLINTBEGIN
            {
                constexpr VkCommandBufferInheritanceInfo operator()() const noexcept
                {
                    using tool::sType;
                    return {.sType = sType<VkCommandBufferInheritanceInfo>(),
                            .pNext = pNext,
                            .renderPass = renderPass,
                            .subpass = subpass,
                            .framebuffer = framebuffer,
                            .occlusionQueryEnable = occlusionQueryEnable,
                            .queryFlags = queryFlags,
                            .pipelineStatistics = pipelineStatistics};
                }
                const void *pNext{};
                VkRenderPass renderPass{};
                uint32_t subpass{};
                VkFramebuffer framebuffer{};
                VkBool32 occlusionQueryEnable{};
                VkQueryControlFlags queryFlags{};
                VkQueryPipelineStatisticFlags pipelineStatistics{};
            }; // NOLINTEND

            struct result_type
            {
                constexpr result_type(
                    std::optional<VkCommandBufferInheritanceInfo> inheritance,
                    VkCommandBufferBeginInfo beginInfo) noexcept
                    : inheritance_{inheritance}, beginInfo_{beginInfo}
                {
                    beginInfo_.pInheritanceInfo =
                        inheritance_ ? &(*inheritance_) : nullptr;
                }

                [[nodiscard]] auto &beginInfo() const noexcept
                {
                    return beginInfo_;
                }

              private:
                std::optional<VkCommandBufferInheritanceInfo> inheritance_;
                VkCommandBufferBeginInfo beginInfo_;
            };

            /*
            typedef struct VkCommandBufferBeginInfo {
                VkStructureType                          sType;
                const void*                              pNext;
                VkCommandBufferUsageFlags                flags;
                const VkCommandBufferInheritanceInfo*    pInheritanceInfo;
            } VkCommandBufferBeginInfo;
            */
            [[nodiscard]] result_type operator()() const noexcept
            {
                using tool::sType;
                return result_type{
                    inheritanceInfo ? std::make_optional<VkCommandBufferInheritanceInfo>(
                                          (*inheritanceInfo)())
                                    : std::nullopt,
                    {.sType = sType<VkCommandBufferBeginInfo>(),
                     .pNext = pNext,
                     .flags = flags}};
            } // NOLINTBEGIN
            const void *pNext{};
            VkCommandBufferUsageFlags flags{};
            std::optional<command_buffer_inheritance_info> inheritanceInfo;
        }; // NOLINTEND
        constexpr void begin(const VkCommandBufferBeginInfo &beginInfo) const
        {
            pool_->device()->beginCommandBuffer(value_, beginInfo);
        }
        // constexpr void begin(const begin_info &beginInfo) const
        // {
        //     auto ret = beginInfo();
        //     pool_->device()->beginCommandBuffer(value_, ret.beginInfo());
        // }

        /*
        typedef struct VkRenderingInfo {
            VkStructureType                     sType;
            const void*                         pNext;
            VkRenderingFlags                    flags;
            VkRect2D                            renderArea;
            uint32_t                            layerCount;
            uint32_t                            viewMask;
            uint32_t                            colorAttachmentCount;
            const VkRenderingAttachmentInfo*    pColorAttachments;
            const VkRenderingAttachmentInfo*    pDepthAttachment;
            const VkRenderingAttachmentInfo*    pStencilAttachment;
        } VkRenderingInfo;
        */
        struct rendering_info
        {
            /*
            typedef struct VkRenderingAttachmentInfo {
                VkStructureType          sType;
                const void*              pNext;
                VkImageView              imageView;
                VkImageLayout            imageLayout;
                VkResolveModeFlagBits    resolveMode;
                VkImageView              resolveImageView;
                VkImageLayout            resolveImageLayout;
                VkAttachmentLoadOp       loadOp;
                VkAttachmentStoreOp      storeOp;
                VkClearValue             clearValue;
            } VkRenderingAttachmentInfo;
            */
            struct rendering_attachment // NOLINTBEGIN
            {
                [[nodiscard]] VkRenderingAttachmentInfo operator()() const noexcept
                {
                    using tool::sType;
                    return {.sType = sType<VkRenderingAttachmentInfo>(),
                            .pNext = pNext,
                            .imageView = imageView,
                            .imageLayout = imageLayout,
                            .resolveMode = resolveMode,
                            .resolveImageView = resolveImageView,
                            .resolveImageLayout = resolveImageLayout,
                            .loadOp = loadOp,
                            .storeOp = storeOp,
                            .clearValue = clearValue};
                }
                const void *pNext{};
                VkImageView imageView{};
                VkImageLayout imageLayout{};
                VkResolveModeFlagBits resolveMode{};
                VkImageView resolveImageView{};
                VkImageLayout resolveImageLayout{};
                VkAttachmentLoadOp loadOp{};
                VkAttachmentStoreOp storeOp{};
                VkClearValue clearValue{};
            }; // NOLINTEND
            [[nodiscard]] VkRenderingInfo operator()() const noexcept
            {
                using tool::sType;
                return {.sType = sType<VkRenderingInfo>(),
                        .pNext = pNext,
                        .renderArea = renderArea,
                        .layerCount = layerCount,
                        .viewMask = viewMask};
            } // NOLINTBEGIN
            const void *pNext{};
            VkRenderingFlags flags{};
            VkRect2D renderArea{};
            uint32_t layerCount{};
            uint32_t viewMask{};
            std::span<const rendering_attachment> colorAttachments;
            rendering_attachment depthAttachment;
            rendering_attachment stencilAttachment;
        }; // NOLINTEND
        constexpr void beginRendering(const VkRenderingInfo &info) const noexcept
        {
            pool_->device()->cmdBeginRendering(value_, info);
        }
        // constexpr void beginRendering(const rendering_info &info) const noexcept
        // {
        //     auto base = info();
        //     auto colorAttachments =
        //         info.colorAttachments |
        //         std::views::transform(
        //             [](const rendering_info::rendering_attachment
        //                    &create) constexpr noexcept { return create(); }) |
        //         std::ranges::to<std::vector<VkRenderingAttachmentInfo>>();
        //     base.colorAttachmentCount = colorAttachments.size();
        //     base.pColorAttachments =
        //         colorAttachments.empty() ? nullptr : colorAttachments.data();
        //     auto depthAttachment = info.depthAttachment();
        //     base.pDepthAttachment = &depthAttachment;
        //     auto stencilAttachment = info.stencilAttachment();
        //     base.pStencilAttachment = &stencilAttachment;
        //     beginRendering(base);
        // }
        constexpr void bindPipeline(VkPipelineBindPoint pipelineBindPoint,
                                    VkPipeline pipeline) const noexcept
        {
            pool_->device()->cmdBindPipeline(value_, pipelineBindPoint, pipeline);
        }
        constexpr void setViewport(uint32_t firstViewport,
                                   std::span<const VkViewport> viewports) const noexcept
        {
            pool_->device()->cmdSetViewport(value_, firstViewport, viewports.size(),
                                            viewports.data());
        }
        constexpr void setScissor(uint32_t firstScissor,
                                  std::span<const VkRect2D> scissors) const noexcept
        {
            pool_->device()->cmdSetScissor(value_, firstScissor, scissors.size(),
                                           scissors.data());
        }
        constexpr void draw(uint32_t vertexCount, uint32_t instanceCount,
                            uint32_t firstVertex, uint32_t firstInstance) const noexcept
        {
            pool_->device()->cmdDraw(value_, vertexCount, instanceCount, firstVertex,
                                     firstInstance);
        }
        constexpr void endRendering() const noexcept
        {
            pool_->device()->cmdEndRendering(value_);
        }
        constexpr void end() const
        {
            pool_->device()->endCommandBuffer(value_);
        }
        constexpr void reset(VkCommandBufferResetFlagBits flags) const
        {
            pool_->device()->resetCommandBuffer(value_, flags);
        }
        constexpr void pipelineBarrier2(
            const VkDependencyInfo &dependencyInfo) const noexcept
        {
            pool_->device()->cmdPipelineBarrier2(value_, dependencyInfo);
        }
        constexpr void copyBuffer(
            VkBuffer srcBuffer, VkBuffer dstBuffer,
            const std::span<const VkBufferCopy> &regions) const noexcept
        {
            pool_->device()->cmdCopyBuffer(value_, srcBuffer, dstBuffer, regions.size(),
                                           regions.data());
        }
        constexpr void bindIndexBuffer(VkBuffer buffer, VkDeviceSize offset,
                                       VkIndexType indexType) const noexcept
        {
            pool_->device()->cmdBindIndexBuffer(value_, buffer, offset, indexType);
        }
        constexpr void drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                   uint32_t firstIndex, int32_t vertexOffset,
                                   uint32_t firstInstance) const noexcept
        {
            pool_->device()->cmdDrawIndexed(value_, indexCount, instanceCount, firstIndex,
                                            vertexOffset, firstInstance);
        }
        constexpr void pushConstants(VkPipelineLayout layout,
                                     VkShaderStageFlagBits stageFlags, uint32_t offset,
                                     uint32_t size, const void *pValues) const noexcept
        {
            pool_->device()->cmdPushConstants(value_, layout, stageFlags, offset, size,
                                              pValues);
        }

        constexpr void bindDescriptorSets(VkPipelineBindPoint pipelineBindPoint,
                                          VkPipelineLayout layout, uint32_t firstSet,
                                          uint32_t descriptorSetCount,
                                          const VkDescriptorSet *pDescriptorSets,
                                          uint32_t dynamicOffsetCount,
                                          const uint32_t *pDynamicOffsets) const noexcept
        {
            pool_->device()->cmdBindDescriptorSets(
                value_, pipelineBindPoint, layout, firstSet, descriptorSetCount,
                pDescriptorSets, dynamicOffsetCount, pDynamicOffsets);
        }
        constexpr void copyBufferToImage(VkBuffer srcBuffer, VkImage dstImage,
                                         VkImageLayout dstImageLayout,
                                         uint32_t regionCount,
                                         const VkBufferImageCopy *pRegions) const noexcept
        {
            pool_->device()->cmdCopyBufferToImage(value_, srcBuffer, dstImage,
                                                  dstImageLayout, regionCount, pRegions);
        }
    };

}; // namespace mcs::vulkan
