#pragma once

#include "sType.hpp"
#include "pNext.hpp"
#include "Flags.hpp"
#include "../utils/read_file.hpp"
#include "../ShaderModule.hpp"
#include "../Pipeline.hpp"
#include <concepts>
#include <optional>
#include <vector>
#include <ranges>

namespace mcs::vulkan::tool
{
    struct create_graphics_pipeline
    {
        /*
        typedef struct VkPipelineShaderStageCreateInfo {
            VkStructureType                     sType;
            const void*                         pNext;
            VkPipelineShaderStageCreateFlags    flags;
            VkShaderStageFlagBits               stage;
            VkShaderModule                      module;
            const char*                         pName;
            const VkSpecializationInfo*         pSpecializationInfo;
        } VkPipelineShaderStageCreateInfo;
        */
        struct stage_info // NOLINTBEGIN
        {
            struct result_type
            {
                constexpr result_type(ShaderModule &&moudle,
                                      VkPipelineShaderStageCreateInfo createInfo) noexcept
                    : moudle_{std::move(moudle)}, createInfo_{createInfo}
                {
                    createInfo_.module = *moudle_;
                }
                [[nodiscard]] constexpr auto createInfo() const noexcept
                {
                    return createInfo_;
                }

              private:
                ShaderModule moudle_;
                VkPipelineShaderStageCreateInfo createInfo_;
            };
            constexpr auto operator()(const LogicalDevice &device) const
            {
                auto data = read_file(filePath);
                MCS_ASSERT(not data.empty());
                return result_type{
                    ShaderModule(device, device.createShaderModule(
                                             {.sType = sType<VkShaderModuleCreateInfo>(),
                                              .codeSize = data.size(),
                                              .pCode = std::bit_cast<const uint32_t *>(
                                                  data.data())},
                                             device.allocator())),
                    {.sType = sType<VkPipelineShaderStageCreateInfo>(),
                     .pNext = pNext.value(),
                     .flags = flags,
                     .stage = stage,
                     .pName = pName,
                     .pSpecializationInfo = pSpecializationInfo}};
            }
            pNext pNext;
            Flags<VkPipelineShaderStageCreateFlagBits> flags;
            VkShaderStageFlagBits stage{};
            std::string filePath;
            const char *pName{};
            const VkSpecializationInfo *pSpecializationInfo{};
        }; // NOLINTEND

        template <std::same_as<stage_info>... Args>
        static constexpr auto makeStages(Args &&...args)
        {
            std::vector<stage_info> result;
            result.reserve(sizeof...(args));
            (result.emplace_back(std::forward<Args>(args)), ...);
            return result;
        }

        /*
        typedef struct VkPipelineVertexInputStateCreateInfo {
            VkStructureType                             sType;
            const void*                                 pNext;
            VkPipelineVertexInputStateCreateFlags       flags;
            uint32_t                                    vertexBindingDescriptionCount;
            const VkVertexInputBindingDescription*      pVertexBindingDescriptions;
            uint32_t                                    vertexAttributeDescriptionCount;
            const VkVertexInputAttributeDescription*    pVertexAttributeDescriptions;
        } VkPipelineVertexInputStateCreateInfo;
        */
        struct vertex_input_state // NOLINTBEGIN
        {
            constexpr VkPipelineVertexInputStateCreateInfo operator()() const noexcept
            {
                return {.sType = sType<VkPipelineVertexInputStateCreateInfo>(),
                        .pNext = pNext.value(),
                        .flags = flags,
                        .vertexBindingDescriptionCount =
                            static_cast<uint32_t>(vertexBindingDescriptions.size()),
                        .pVertexBindingDescriptions =
                            vertexBindingDescriptions.empty()
                                ? nullptr
                                : vertexBindingDescriptions.data(),
                        .vertexAttributeDescriptionCount =
                            static_cast<uint32_t>(vertexAttributeDescriptions.size()),
                        .pVertexAttributeDescriptions =
                            vertexAttributeDescriptions.empty()
                                ? nullptr
                                : vertexAttributeDescriptions.data()};
            }
            pNext pNext;
            VkPipelineVertexInputStateCreateFlags flags{};
            std::vector<VkVertexInputBindingDescription> vertexBindingDescriptions;
            std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions;
        }; // NOLINTEND

        /*
        typedef struct VkPipelineInputAssemblyStateCreateInfo {
            VkStructureType                            sType;
            const void*                                pNext;
            VkPipelineInputAssemblyStateCreateFlags    flags;
            VkPrimitiveTopology                        topology;
            VkBool32                                   primitiveRestartEnable;
        } VkPipelineInputAssemblyStateCreateInfo;
        */
        struct input_assembly_state // NOLINTBEGIN
        {
            constexpr VkPipelineInputAssemblyStateCreateInfo operator()() const noexcept
            {
                return {.sType = sType<VkPipelineInputAssemblyStateCreateInfo>(),
                        .pNext = pNext.value(),
                        .flags = flags,
                        .topology = topology,
                        .primitiveRestartEnable = primitiveRestartEnable};
            }
            pNext pNext;
            VkPipelineInputAssemblyStateCreateFlags flags;
            VkPrimitiveTopology topology{};
            VkBool32 primitiveRestartEnable{};
        }; // NOLINTEND

        /*
        typedef struct VkPipelineTessellationStateCreateInfo {
            VkStructureType                           sType;
            const void*                               pNext;
            VkPipelineTessellationStateCreateFlags    flags;
            uint32_t                                  patchControlPoints;
        } VkPipelineTessellationStateCreateInfo;
        */
        struct tessellation_state // NOLINTBEGIN
        {
            constexpr VkPipelineTessellationStateCreateInfo operator()() const noexcept
            {
                return {.sType = sType<VkPipelineTessellationStateCreateInfo>(),
                        .pNext = pNext.value(),
                        .flags = flags,
                        .patchControlPoints = patchControlPoints};
            }
            pNext pNext;
            VkPipelineTessellationStateCreateFlags flags;
            uint32_t patchControlPoints{};
        }; // NOLINTEND

        /*
       typedef struct VkPipelineViewportStateCreateInfo {
           VkStructureType                       sType;
           const void*                           pNext;
           VkPipelineViewportStateCreateFlags    flags;
           uint32_t                              viewportCount;
           const VkViewport*                     pViewports;
           uint32_t                              scissorCount;
           const VkRect2D*                       pScissors;
       } VkPipelineViewportStateCreateInfo;
       */
        struct viewport_state // NOLINTBEGIN
        {
            constexpr VkPipelineViewportStateCreateInfo operator()() const noexcept
            {
                return {.sType = sType<VkPipelineViewportStateCreateInfo>(),
                        .pNext = pNext.value(),
                        .flags = flags,
                        .viewportCount = static_cast<uint32_t>(viewports.size()),
                        .pViewports = viewports.empty() ? nullptr : viewports.data(),
                        .scissorCount = static_cast<uint32_t>(scissors.size()),
                        .pScissors = scissors.empty() ? nullptr : scissors.data()};
            }
            pNext pNext;
            VkPipelineViewportStateCreateFlags flags;
            std::vector<VkViewport> viewports;
            std::vector<VkRect2D> scissors;
        }; // NOLINTEND

        /*
        typedef struct VkPipelineRasterizationStateCreateInfo {
            VkStructureType                            sType;
            const void*                                pNext;
            VkPipelineRasterizationStateCreateFlags    flags;
            VkBool32                                   depthClampEnable;
            VkBool32                                   rasterizerDiscardEnable;
            VkPolygonMode                              polygonMode;
            VkCullModeFlags                            cullMode;
            VkFrontFace                                frontFace;
            VkBool32                                   depthBiasEnable;
            float                                      depthBiasConstantFactor;
            float                                      depthBiasClamp;
            float                                      depthBiasSlopeFactor;
            float                                      lineWidth;
        } VkPipelineRasterizationStateCreateInfo;
        */
        struct rasterization_state // NOLINTBEGIN
        {
            constexpr VkPipelineRasterizationStateCreateInfo operator()() const noexcept
            {
                return {.sType = sType<VkPipelineRasterizationStateCreateInfo>(),
                        .pNext = pNext.value(),
                        .flags = flags,
                        .depthClampEnable = depthClampEnable,
                        .rasterizerDiscardEnable = rasterizerDiscardEnable,
                        .polygonMode = polygonMode,
                        .cullMode = cullMode,
                        .frontFace = frontFace,
                        .depthBiasEnable = depthBiasEnable,
                        .depthBiasConstantFactor = depthBiasConstantFactor,
                        .depthBiasClamp = depthBiasClamp,
                        .depthBiasSlopeFactor = depthBiasSlopeFactor,
                        .lineWidth = lineWidth};
            }
            pNext pNext;
            VkPipelineRasterizationStateCreateFlags flags{};
            VkBool32 depthClampEnable{};
            VkBool32 rasterizerDiscardEnable{};
            VkPolygonMode polygonMode{};
            VkCullModeFlags cullMode{};
            VkFrontFace frontFace{};
            VkBool32 depthBiasEnable{};
            float depthBiasConstantFactor{};
            float depthBiasClamp{};
            float depthBiasSlopeFactor{};
            float lineWidth{};
        }; // NOLINTEND

        /*
        typedef struct VkPipelineMultisampleStateCreateInfo {
            VkStructureType                          sType;
            const void*                              pNext;
            VkPipelineMultisampleStateCreateFlags    flags;
            VkSampleCountFlagBits                    rasterizationSamples;
            VkBool32                                 sampleShadingEnable;
            float                                    minSampleShading;
            const VkSampleMask*                      pSampleMask;
            VkBool32                                 alphaToCoverageEnable;
            VkBool32                                 alphaToOneEnable;
        } VkPipelineMultisampleStateCreateInfo;
        */
        struct multisample_state // NOLINTBEGIN
        {
            constexpr VkPipelineMultisampleStateCreateInfo operator()() const noexcept
            {
                return {.sType = sType<VkPipelineMultisampleStateCreateInfo>(),
                        .pNext = pNext.value(),
                        .flags = flags,
                        .rasterizationSamples = rasterizationSamples,
                        .sampleShadingEnable = sampleShadingEnable,
                        .minSampleShading = minSampleShading,
                        .pSampleMask = sampleMask ? &(*sampleMask) : nullptr,
                        .alphaToCoverageEnable = alphaToCoverageEnable,
                        .alphaToOneEnable = alphaToOneEnable};
            }
            pNext pNext;
            VkPipelineMultisampleStateCreateFlags flags{};
            VkSampleCountFlagBits rasterizationSamples{};
            VkBool32 sampleShadingEnable{};
            float minSampleShading{};
            std::optional<VkSampleMask> sampleMask;
            VkBool32 alphaToCoverageEnable{};
            VkBool32 alphaToOneEnable{};
        }; // NOLINTEND

        /*
        typedef struct VkPipelineDepthStencilStateCreateInfo {
            VkStructureType                           sType;
            const void*                               pNext;
            VkPipelineDepthStencilStateCreateFlags    flags;
            VkBool32                                  depthTestEnable;
            VkBool32                                  depthWriteEnable;
            VkCompareOp                               depthCompareOp;
            VkBool32                                  depthBoundsTestEnable;
            VkBool32                                  stencilTestEnable;
            VkStencilOpState                          front;
            VkStencilOpState                          back;
            float                                     minDepthBounds;
            float                                     maxDepthBounds;
        } VkPipelineDepthStencilStateCreateInfo;
        */
        struct depth_stencil_state // NOLINTBEGIN
        {
            constexpr VkPipelineDepthStencilStateCreateInfo operator()() const noexcept
            {
                return {.sType = sType<VkPipelineDepthStencilStateCreateInfo>(),
                        .pNext = pNext.value(),
                        .flags = flags,
                        .depthTestEnable = depthTestEnable,
                        .depthWriteEnable = depthWriteEnable,
                        .depthCompareOp = depthCompareOp,
                        .depthBoundsTestEnable = depthBoundsTestEnable,
                        .stencilTestEnable = stencilTestEnable,
                        .front = front,
                        .back = back,
                        .minDepthBounds = minDepthBounds,
                        .maxDepthBounds = maxDepthBounds};
            }
            pNext pNext;
            Flags<VkPipelineDepthStencilStateCreateFlagBits> flags;
            VkBool32 depthTestEnable{};
            VkBool32 depthWriteEnable{};
            VkCompareOp depthCompareOp{};
            VkBool32 depthBoundsTestEnable{};
            VkBool32 stencilTestEnable{};
            VkStencilOpState front{};
            VkStencilOpState back{};
            float minDepthBounds{};
            float maxDepthBounds{};
        }; // NOLINTEND

        /*
        typedef struct VkPipelineColorBlendStateCreateInfo {
            VkStructureType                               sType;
            const void*                                   pNext;
            VkPipelineColorBlendStateCreateFlags          flags;
            VkBool32                                      logicOpEnable;
            VkLogicOp                                     logicOp;
            uint32_t                                      attachmentCount;
            const VkPipelineColorBlendAttachmentState*    pAttachments;
            float                                         blendConstants[4];
        } VkPipelineColorBlendStateCreateInfo;
        */
        struct color_blend_state // NOLINTBEGIN
        {
            constexpr VkPipelineColorBlendStateCreateInfo operator()() const noexcept
            {
                return {.sType = sType<VkPipelineColorBlendStateCreateInfo>(),
                        .pNext = pNext.value(),
                        .flags = flags,
                        .logicOpEnable = logicOpEnable,
                        .logicOp = logicOp,
                        .attachmentCount = static_cast<uint32_t>(attachments.size()),
                        .pAttachments =
                            attachments.empty() ? nullptr : attachments.data(),
                        .blendConstants = {blendConstants[0], blendConstants[1],
                                           blendConstants[2], blendConstants[3]}};
            }
            pNext pNext;
            Flags<VkPipelineColorBlendStateCreateFlagBits> flags;
            VkBool32 logicOpEnable{};
            VkLogicOp logicOp{};
            std::vector<VkPipelineColorBlendAttachmentState> attachments;
            float blendConstants[4]{};
        }; // NOLINTEND

        /*
        typedef struct VkPipelineDynamicStateCreateInfo {
            VkStructureType                      sType;
            const void*                          pNext;
            VkPipelineDynamicStateCreateFlags    flags;
            uint32_t                             dynamicStateCount;
            const VkDynamicState*                pDynamicStates;
        } VkPipelineDynamicStateCreateInfo;
        */
        struct dynamic_state // NOLINTBEGIN
        {
            constexpr VkPipelineDynamicStateCreateInfo operator()() const noexcept
            {
                return {.sType = sType<VkPipelineDynamicStateCreateInfo>(),
                        .pNext = pNext.value(),
                        .flags = flags,
                        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
                        .pDynamicStates =
                            dynamicStates.empty() ? nullptr : dynamicStates.data()};
            }
            pNext pNext;
            VkPipelineDynamicStateCreateFlags flags{};
            std::vector<VkDynamicState> dynamicStates;
        }; // NOLINTEND

        /*
        typedef struct VkGraphicsPipelineCreateInfo {
            VkStructureType                                  sType;
            const void*                                      pNext;
            VkPipelineCreateFlags                            flags;
            uint32_t                                         stageCount;
            const VkPipelineShaderStageCreateInfo*           pStages;
            const VkPipelineVertexInputStateCreateInfo*      pVertexInputState;
            const VkPipelineInputAssemblyStateCreateInfo*    pInputAssemblyState;
            const VkPipelineTessellationStateCreateInfo*     pTessellationState;
            const VkPipelineViewportStateCreateInfo*         pViewportState;
            const VkPipelineRasterizationStateCreateInfo*    pRasterizationState;
            const VkPipelineMultisampleStateCreateInfo*      pMultisampleState;
            const VkPipelineDepthStencilStateCreateInfo*     pDepthStencilState;
            const VkPipelineColorBlendStateCreateInfo*       pColorBlendState;
            const VkPipelineDynamicStateCreateInfo*          pDynamicState;
            VkPipelineLayout                                 layout;
            VkRenderPass                                     renderPass;
            uint32_t                                         subpass;
            VkPipeline                                       basePipelineHandle;
            int32_t                                          basePipelineIndex;
        } VkGraphicsPipelineCreateInfo;
        */
        struct create_info // NOLINTBEGIN
        {
            pNext pNext;
            Flags<VkPipelineCreateFlagBits> flags;
            std::vector<stage_info> stages;
            vertex_input_state vertexInputState{};
            input_assembly_state inputAssemblyState{};
            tessellation_state tessellationState{};
            viewport_state viewportState{};
            rasterization_state rasterizationState{};
            multisample_state multisampleState{};
            depth_stencil_state depthStencilState{};
            color_blend_state colorBlendState{};
            dynamic_state dynamicState{};
            VkPipelineLayout layout{};
            VkRenderPass renderPass{};
            uint32_t subpass{};
            VkPipeline basePipelineHandle{};
            int32_t basePipelineIndex{};
        }; // NOLINTEND
        auto &setCreateInfo(create_info &&createInfo)
        {
            createInfo_ = std::move(createInfo);
            return *this;
        }

        [[nodiscard]] Pipeline build(const LogicalDevice &device,
                                     VkPipelineCache pipelineCache = nullptr) const
        {
            // c0: pNext,flags
            VkGraphicsPipelineCreateInfo CI{.sType =
                                                sType<VkGraphicsPipelineCreateInfo>(),
                                            .pNext = createInfo_.pNext.value(),
                                            .flags = createInfo_.flags};
            // c1: stageCount,pStages
            auto stages_createInfos =
                createInfo_.stages |
                std::views::transform(
                    [&](const stage_info &create) constexpr { return create(device); }) |
                std::ranges::to<std::vector<stage_info::result_type>>();
            auto stages =
                stages_createInfos |
                std::views::transform(
                    [&](const stage_info::result_type &create) constexpr noexcept {
                        return create.createInfo();
                    }) |
                std::ranges::to<std::vector<VkPipelineShaderStageCreateInfo>>();
            CI.stageCount = stages.size();
            CI.pStages = stages.data();

            // c2: pVertexInputState
            VkPipelineVertexInputStateCreateInfo vertexInputState =
                createInfo_.vertexInputState();
            CI.pVertexInputState = &vertexInputState;

            // c3: pInputAssemblyState
            VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
                createInfo_.inputAssemblyState();
            CI.pInputAssemblyState = &inputAssemblyState;

            // c4: pTessellationState
            VkPipelineTessellationStateCreateInfo tessellationState =
                createInfo_.tessellationState();
            CI.pTessellationState = &tessellationState;

            // c5:pViewportState
            VkPipelineViewportStateCreateInfo viewportState = createInfo_.viewportState();
            CI.pViewportState = &viewportState;

            // c6: pRasterizationState
            VkPipelineRasterizationStateCreateInfo rasterizationState =
                createInfo_.rasterizationState();
            CI.pRasterizationState = &rasterizationState;

            // c7: pMultisampleState
            VkPipelineMultisampleStateCreateInfo multisampleState =
                createInfo_.multisampleState();
            CI.pMultisampleState = &multisampleState;

            // c8: pDepthStencilState
            auto depthStencilState = createInfo_.depthStencilState();
            CI.pDepthStencilState = &depthStencilState;

            // c9: pColorBlendState
            VkPipelineColorBlendStateCreateInfo colorBlendState =
                createInfo_.colorBlendState();
            CI.pColorBlendState = &colorBlendState;

            // c10: pDynamicState
            auto dynamicState = createInfo_.dynamicState();
            CI.pDynamicState = &dynamicState;

            // c11: layout,renderPass,subpass,basePipelineHandle,basePipelineIndex
            CI.layout = createInfo_.layout;
            CI.renderPass = createInfo_.renderPass;
            CI.subpass = createInfo_.subpass;
            CI.basePipelineHandle = createInfo_.basePipelineHandle;
            CI.basePipelineIndex = createInfo_.basePipelineIndex;

            return Pipeline{device, device.createGraphicsPipelines(pipelineCache, 1, CI,
                                                                   device.allocator())};
        }

      private:
        create_info createInfo_;
    };

}; // namespace mcs::vulkan::tool