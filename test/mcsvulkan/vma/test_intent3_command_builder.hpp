#pragma once

// ============================================================================
// test_intent3_command_builder.hpp
//
// 数据 + 算法，尽量少的状态：
//   - cur_      正在构建的段（唯一会变的数据）
//   - pushBase_ 客户端给的推送常量（cur_.pushData 里的地址是解析后的，含义不同）
//   - 3 个写游标（位置，不是状态）
//
// 段的边界由“状态值变了”直接决定，没有隐式规则表。
//
// 骨架（顺序不许变）：
//   builder.reset();
//   builder.setPipeline / setLayout / setPushData / setMesh
//   builder.setViewport / setScissor
//   builder.addInstances(...)
//   builder.end();
//   command_emitter::emit(cmd, buffers, builder.stream());
// ============================================================================

#include "test_intent3_command_stream.hpp"
#include "test_intent3_shader_buffers.hpp"

#include <cassert>
#include <cstdint>
#include <span>

namespace shader_data
{

    class CommandBuilder
    {
      public:
        mesh_data currentMesh{};

        constexpr explicit CommandBuilder(ShaderBuffers &buffers,
                                          DynamicStateCaps caps) noexcept
            : buffers_{&buffers}, caps_{caps}
        {
            segments_.reserve(128);
        }

        void bind(ShaderBuffers &buffers) noexcept
        {
            buffers_ = &buffers;
        }

        // 客户端可读：直接给值，不暴露存储
        // 状态是否“已定义”（对应 md 里的 defined 位掩码）
        [[nodiscard]] constexpr bool hasViewport() const noexcept
        {
            return cur_.regs.viewportCount != 0;
        }
        [[nodiscard]] constexpr bool hasScissor() const noexcept
        {
            return cur_.regs.scissorCount != 0;
        }

        [[nodiscard]] constexpr VkViewport currentViewport() const noexcept
        {
            return cur_.regs.viewports[0];
        }
        [[nodiscard]] constexpr VkRect2D currentScissor() const noexcept
        {
            return cur_.regs.scissors[0];
        }

        [[nodiscard]] constexpr CommandStream stream() const noexcept
        {
            return {.segments = &segments_};
        }

        // ── 每帧开始 ────────────────────────────────────────────
        // ── 录制重建：reset + 环境，一次做完 ────────────────────
        constexpr void begin(const FrameState &state)
        {
            assert(state.pipeline != VK_NULL_HANDLE);
            assert(state.layout != VK_NULL_HANDLE);
            assert(state.pushData.valid());

            reset();

            cur_.handles.pipeline = state.pipeline;
            cur_.handles.layout = state.layout;
            cur_.handles.dynamicStateMask = state.pipelineDynMask;

            pushBase_ = state.pushData;
            currentMesh = state.mesh;

            cur_.regs.viewports[0] = state.viewport;
            cur_.regs.viewportCount = 1;
            cur_.regs.scissors[0] = state.scissor;
            cur_.regs.scissorCount = 1;
        }

      private:
        constexpr void reset() noexcept
        {
            heapCursor = 0;
            indirectCursor = 0;
            constantCursor = 0;

            segments_.clear();
            currentMesh = {};
            pushBase_ = {};
            cur_ = {};
        }

      public:
        // ── bind（状态变化 = 段边界）────────────────────────────
        constexpr void setPipeline(VkPipeline pipeline, DynamicStateMask dynMask = 0)
        {
            if (cur_.handles.pipeline == pipeline)
                return;
            flush();
            cur_.handles.pipeline = pipeline;
        }

        constexpr void setLayout(VkPipelineLayout layout)
        {
            if (cur_.handles.layout == layout)
                return;
            flush(); // 推送常量带 layout
            cur_.handles.layout = layout;
        }

        constexpr void setPushData(const PushData &pushData)
        {
            if (pushBase_ == pushData)
                return;
            flush(); // 每段开头会重新推送
            pushBase_ = pushData;
        }

        // Vulkan 没有“当前 mesh”这个状态；这里只是 addInstances(span) 的缺省值
        constexpr void setMesh(const mesh_data &mesh)
        {
            currentMesh = mesh;
        }

        // 描述符绑定：sets 是资源，span 引用调用方的存储（须活到 emit）。
        // 动态偏移是值，按份存。
        constexpr void bindDescriptorSets(VkPipelineLayout layout,
                                          std::span<const VkDescriptorSet> sets,
                                          std::uint32_t firstSet = 0)
        {
            flush();
            cur_.handles.layout = layout;
            cur_.handles.sets = sets;
            cur_.handles.firstSet = firstSet;
            cur_.regs.dynamicOffset = 0;
            cur_.regs.dynamicOffsetCount = 0;
        }

        constexpr void bindDescriptorSets(VkPipelineLayout layout,
                                          std::span<const VkDescriptorSet> sets,
                                          std::uint32_t firstSet,
                                          std::uint32_t dynamicOffset)
        {
            flush();
            cur_.handles.layout = layout;
            cur_.handles.sets = sets;
            cur_.handles.firstSet = firstSet;
            cur_.regs.dynamicOffset = dynamicOffset;
            cur_.regs.dynamicOffsetCount = 1;
        }

        // ── setDynamic（状态变化 = 段边界）──────────────────────
        constexpr void setViewport(const VkViewport &value)
        {
            setViewports(std::span{&value, 1});
        }

        constexpr void setScissor(const VkRect2D &value)
        {
            setScissors(std::span{&value, 1});
        }

        constexpr void setViewports(std::span<const VkViewport> value)
        {
            const std::uint32_t n = static_cast<std::uint32_t>(
                std::min<std::size_t>(value.size(), kMaxViewports));
            assert(value.size() <= kMaxViewports && "capacity: bump kMaxViewports");
            if (same_viewports(cur_.regs.viewports, cur_.regs.viewportCount, value))
                return;
            flush();
            cur_.regs.viewportCount = n;
            for (std::uint32_t i = 0; i < n; ++i)
                cur_.regs.viewports[i] = value[i];
        }

        constexpr void setScissors(std::span<const VkRect2D> value)
        {
            const std::uint32_t n = static_cast<std::uint32_t>(
                std::min<std::size_t>(value.size(), kMaxViewports));
            assert(value.size() <= kMaxViewports && "capacity: bump kMaxViewports");
            if (same_scissors(cur_.regs.scissors, cur_.regs.scissorCount, value))
                return;
            flush();
            cur_.regs.scissorCount = n;
            for (std::uint32_t i = 0; i < n; ++i)
                cur_.regs.scissors[i] = value[i];
        }

        // ── 扩展动态状态：设备没开就断言并忽略；值变才 flush ─────
        constexpr void setDepthTestEnable(bool value)
        {
            if (!caps_.has(kDynDepthTest))
            {
                assert(false && "device: extendedDynamicState 未启用");
                return;
            }
            const std::uint32_t v = value ? 1U : 0U;
            const DynamicStateMask bit = kDynDepthTest;
            if (cur_.regs.depthTestEnable == v && (cur_.regs.dynamicMask & bit))
                return;
            flush();
            cur_.regs.depthTestEnable = v;
            cur_.regs.dynamicMask |= bit;
        }

        constexpr void setDepthWriteEnable(bool value)
        {
            if (!caps_.has(kDynDepthWrite))
            {
                assert(false && "device: extendedDynamicState 未启用");
                return;
            }
            const std::uint32_t v = value ? 1U : 0U;
            const DynamicStateMask bit = kDynDepthWrite;
            if (cur_.regs.depthWriteEnable == v && (cur_.regs.dynamicMask & bit))
                return;
            flush();
            cur_.regs.depthWriteEnable = v;
            cur_.regs.dynamicMask |= bit;
        }

        constexpr void setPrimitiveTopology(VkPrimitiveTopology value)
        {
            if (!caps_.has(kDynTopology))
            {
                assert(false && "device: extendedDynamicState 未启用");
                return;
            }
            const DynamicStateMask bit = kDynTopology;
            if (cur_.regs.topology == value && (cur_.regs.dynamicMask & bit))
                return;
            flush();
            cur_.regs.topology = value;
            cur_.regs.dynamicMask |= bit;
        }

        constexpr void setLineWidth(float value)
        {
            if (!caps_.has(kDynLineWidth))
            {
                assert(false && "device: lineWidth 动态状态未启用");
                return;
            }
            const DynamicStateMask bit = kDynLineWidth;
            if (cur_.regs.lineWidth == value && (cur_.regs.dynamicMask & bit))
                return;
            flush();
            cur_.regs.lineWidth = value;
            cur_.regs.dynamicMask |= bit;
        }

        constexpr void setStencilReference(VkStencilFaceFlags face, std::uint32_t value)
        {
            if (!caps_.has(kDynStencilRef))
            {
                assert(false && "device: stencilReference 动态状态未启用");
                return;
            }
            const DynamicStateMask bit = kDynStencilRef;
            if (cur_.regs.stencilRef == value && cur_.regs.stencilFace == face &&
                (cur_.regs.dynamicMask & bit))
                return;
            flush();
            cur_.regs.stencilFace = face;
            cur_.regs.stencilRef = value;
            cur_.regs.dynamicMask |= bit;
        }

        // ── draw（只改 cur_ 和 buffer，不提交）──────────────────
        constexpr void addInstances(std::span<const Glyph> instances)
        {
            addBatch(InstanceKind::Glyph, currentMesh, instances.data(),
                     static_cast<std::uint32_t>(instances.size()));
        }
        constexpr void addInstances(std::span<const UiRect> instances)
        {
            addBatch(InstanceKind::UiRect, currentMesh, instances.data(),
                     static_cast<std::uint32_t>(instances.size()));
        }
        constexpr void addInstances(std::span<const Rectangle> instances)
        {
            addBatch(InstanceKind::RoundRect, currentMesh, instances.data(),
                     static_cast<std::uint32_t>(instances.size()));
        }
        constexpr void addInstances(std::span<const VertexAttr> instances)
        {
            addBatch(InstanceKind::Mesh, currentMesh, instances.data(),
                     static_cast<std::uint32_t>(instances.size()));
        }

        constexpr void addInstances(const mesh_data &mesh,
                                    std::span<const Glyph> instances)
        {
            addBatch(InstanceKind::Glyph, mesh, instances.data(),
                     static_cast<std::uint32_t>(instances.size()));
        }
        constexpr void addInstances(const mesh_data &mesh,
                                    std::span<const UiRect> instances)
        {
            addBatch(InstanceKind::UiRect, mesh, instances.data(),
                     static_cast<std::uint32_t>(instances.size()));
        }
        constexpr void addInstances(const mesh_data &mesh,
                                    std::span<const Rectangle> instances)
        {
            addBatch(InstanceKind::RoundRect, mesh, instances.data(),
                     static_cast<std::uint32_t>(instances.size()));
        }
        constexpr void addInstances(const mesh_data &mesh,
                                    std::span<const VertexAttr> instances)
        {
            addBatch(InstanceKind::Mesh, mesh, instances.data(),
                     static_cast<std::uint32_t>(instances.size()));
        }

        constexpr void addInstance(const Glyph &v)
        {
            addInstances(std::span{&v, 1});
        }
        constexpr void addInstance(const UiRect &v)
        {
            addInstances(std::span{&v, 1});
        }
        constexpr void addInstance(const Rectangle &v)
        {
            addInstances(std::span{&v, 1});
        }
        constexpr void addInstance(const VertexAttr &v)
        {
            addInstances(std::span{&v, 1});
        }

        // ── 基座：非模板 ────────────────────────────────────────
        constexpr void addBatch(InstanceKind kind, const mesh_data &mesh,
                                const void *data, std::uint32_t count,
                                std::uint32_t stride = 0)
        {
            if (count == 0)
                return;

            assert(cur_.handles.pipeline != VK_NULL_HANDLE);
            assert(cur_.handles.layout != VK_NULL_HANDLE);
            assert(pushBase_.valid());
            assert(mesh.valid());
            if (stride == 0)
                stride = sizeOf(kind);
            assert(stride == sizeOf(kind));

            // 1) 实例数据直接写 heap（合不合并都连续）
            const std::uint32_t addressOffset = heapCursor;
            buffers_->globalHeapBuffer.write(heapCursor, data,
                                             static_cast<std::size_t>(count) * stride);
            heapCursor += count * stride;

            // 2) 段内最后一条能合并就只改 instanceCount
            if (cur_.draw.commandCount > 0 && tryMergeLast(mesh, kind, count))
                return;

            // 3) 段的第一条命令：记下命令区/常量区起点，解析推送常量地址
            if (cur_.draw.commandCount == 0)
            {
                cur_.draw.indirectOffset = indirectCursor;
                cur_.pushData = pushBase_;
                cur_.pushData.commandConstantsAddress =
                    buffers_->commandConstantsBuffer.address + constantCursor;
            }

            // 4) 追加命令 + 常量
            const auto cmd = mesh.getDrawCommand(count);
            buffers_->indirectDrawBuffer.write(indirectCursor, &cmd, sizeof cmd);
            const CommandConstant constant{.type_id = static_cast<std::uint32_t>(kind),
                                           .address_offset = addressOffset,
                                           .slot_count = 0};
            buffers_->commandConstantsBuffer.write(constantCursor, &constant,
                                                   sizeof constant);
            indirectCursor += sizeof(VkDrawIndexedIndirectCommand);
            constantCursor += sizeof(CommandConstant);
            ++cur_.draw.commandCount;
        }

        constexpr void addBatch(InstanceKind kind, const void *data, std::uint32_t count,
                                std::uint32_t stride = 0)
        {
            addBatch(kind, currentMesh, data, count, stride);
        }

        // ── 收口 ────────────────────────────────────────────────
        constexpr void end()
        {
            flush();
        }

      private:
        ShaderBuffers *buffers_{};
        DynamicStateCaps caps_{};

        DrawSegment cur_{};                 // 正在构建的段
        PushData pushBase_{};               // 客户端给的推送常量（地址未解析）
        std::vector<DrawSegment> segments_; // 已完成的段

        std::uint32_t heapCursor{0};
        std::uint32_t indirectCursor{0};
        std::uint32_t constantCursor{0};

        constexpr void flush()
        {
            if (cur_.draw.commandCount == 0)
                return;
            segments_.push_back(cur_);
            cur_.draw.commandCount = 0;
        }

        [[nodiscard]] constexpr VkDrawIndexedIndirectCommand *commandAt(
            std::uint32_t byteOffset)
        {
            auto *base = static_cast<char *>(buffers_->indirectDrawBuffer.data());
            return reinterpret_cast<VkDrawIndexedIndirectCommand *>(base + byteOffset);
        }

        [[nodiscard]] constexpr CommandConstant *constantAt(std::uint32_t byteOffset)
        {
            auto *base = static_cast<char *>(buffers_->commandConstantsBuffer.data());
            return reinterpret_cast<CommandConstant *>(base + byteOffset);
        }

        constexpr bool tryMergeLast(const mesh_data &mesh, InstanceKind kind,
                                    std::uint32_t count)
        {
            const std::uint32_t cmdOffset =
                cur_.draw.indirectOffset +
                (cur_.draw.commandCount - 1) * sizeof(VkDrawIndexedIndirectCommand);
            const std::uint32_t constBase =
                static_cast<std::uint32_t>(cur_.pushData.commandConstantsAddress -
                                           buffers_->commandConstantsBuffer.address);
            const std::uint32_t constOffset =
                constBase + (cur_.draw.commandCount - 1) * sizeof(CommandConstant);

            auto *lastCmd = commandAt(cmdOffset);
            auto *lastConst = constantAt(constOffset);
            const auto candidate = mesh.getDrawCommand(count);

            if (lastCmd->indexCount == candidate.indexCount &&
                lastCmd->firstIndex == candidate.firstIndex &&
                lastCmd->vertexOffset == candidate.vertexOffset &&
                lastCmd->firstInstance == candidate.firstInstance &&
                lastConst->type_id == static_cast<std::uint32_t>(kind))
            {
                lastCmd->instanceCount += count;
                return true;
            }
            return false;
        }
    };

} // namespace shader_data
