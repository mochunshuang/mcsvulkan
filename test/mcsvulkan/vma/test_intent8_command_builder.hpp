#pragma once

#include "test_intent8_command_stream.hpp"
#include "test_intent8_draw_pipeline.hpp"
#include "test_intent8_shader_buffers.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <span>
#include <vector>

namespace shader_data
{
    class CommandBuilder
    {
      public:
        mesh_data currentMesh{};

        explicit constexpr CommandBuilder(ShaderBuffers &buffers) noexcept
            : buffers_{&buffers}
        {
            segments_.reserve(128);
            scissorStack_.reserve(8);
        }

        void bind(ShaderBuffers &buffers) noexcept
        {
            buffers_ = &buffers;
        }

        [[nodiscard]] constexpr bool hasViewport() const noexcept
        {
            return hasViewport_;
        }
        [[nodiscard]] constexpr VkViewport currentViewport() const noexcept
        {
            return cur_.regs.viewport;
        }

        [[nodiscard]] constexpr CommandStream stream() const noexcept
        {
            return {.segments = &segments_};
        }

        constexpr void begin(const FrameState &state)
        {
            assert(state.pipeline != nullptr);
            assert(state.pipeline->valid());
            assert(state.pushData.valid());

            reset();

            cur_.handles.pipeline = state.pipeline;
            pushBase_ = state.pushData;
            currentMesh = state.mesh;
            cur_.regs.viewport = state.viewport;
            cur_.regs.scissor = state.scissor;
            hasViewport_ = true;
        }

        constexpr void setPipeline(const intent8::DrawPipeline *pipeline)
        {
            assert(pipeline != nullptr);
            assert(pipeline->valid());
            flush();
            cur_.handles.pipeline = pipeline;
        }

        constexpr void setScissor(const VkRect2D &scissor)
        {
            flush();
            cur_.regs.scissor = scissor;
        }

        constexpr void pushScissor(const VkRect2D &scissor)
        {
            flush();
            scissorStack_.push_back(cur_.regs.scissor);
            cur_.regs.scissor = intersect(cur_.regs.scissor, scissor);
        }

        constexpr void popScissor()
        {
            if (scissorStack_.empty())
                return;
            flush();
            cur_.regs.scissor = scissorStack_.back();
            scissorStack_.pop_back();
        }

        constexpr void setViewport(const VkViewport &viewport)
        {
            flush();
            cur_.regs.viewport = viewport;
        }

        constexpr void addInstances(std::span<const Shape> instances)
        {
            addBatch(Program::Shape, currentMesh, instances.data(),
                     static_cast<std::uint32_t>(instances.size()));
        }
        constexpr void addInstances(std::span<const PathFill> instances)
        {
            addBatch(Program::PathFill, currentMesh, instances.data(),
                     static_cast<std::uint32_t>(instances.size()));
        }
        constexpr void addInstances(std::span<const PathStroke> instances)
        {
            addBatch(Program::PathStroke, currentMesh, instances.data(),
                     static_cast<std::uint32_t>(instances.size()));
        }
        constexpr void addInstances(std::span<const TexQuad> instances)
        {
            addBatch(Program::Texture, currentMesh, instances.data(),
                     static_cast<std::uint32_t>(instances.size()));
        }
        constexpr void addInstances(std::span<const BlurQuad> instances)
        {
            addBatch(Program::Blur, currentMesh, instances.data(),
                     static_cast<std::uint32_t>(instances.size()));
        }
        constexpr void addInstances(std::span<const ColorMatrix> instances)
        {
            addBatch(Program::ColorMatrix, currentMesh, instances.data(),
                     static_cast<std::uint32_t>(instances.size()));
        }
        constexpr void addInstances(std::span<const NoiseQuad> instances)
        {
            addBatch(Program::Noise, currentMesh, instances.data(),
                     static_cast<std::uint32_t>(instances.size()));
        }

        constexpr void addInstance(const Shape &v)
        {
            addInstances(std::span{&v, 1});
        }
        constexpr void addInstance(const PathFill &v)
        {
            addInstances(std::span{&v, 1});
        }
        constexpr void addInstance(const PathStroke &v)
        {
            addInstances(std::span{&v, 1});
        }
        constexpr void addInstance(const TexQuad &v)
        {
            addInstances(std::span{&v, 1});
        }
        constexpr void addInstance(const BlurQuad &v)
        {
            addInstances(std::span{&v, 1});
        }
        constexpr void addInstance(const ColorMatrix &v)
        {
            addInstances(std::span{&v, 1});
        }
        constexpr void addInstance(const NoiseQuad &v)
        {
            addInstances(std::span{&v, 1});
        }

        constexpr void addBatch(Program program, const mesh_data &mesh,
                                const void *data, std::uint32_t count,
                                std::uint32_t stride = 0)
        {
            if (count == 0)
                return;

            assert(cur_.handles.pipeline != nullptr);
            assert(cur_.handles.pipeline->valid());
            assert(pushBase_.valid());
            assert(mesh.valid());
            if (stride == 0)
                stride = sizeOf(program);
            assert(stride == sizeOf(program));

            const std::uint32_t addressOffset = heapCursor;
            buffers_->globalHeapBuffer.write(heapCursor, data,
                                             static_cast<std::size_t>(count) * stride);
            heapCursor += count * stride;

            if (cur_.draw.commandCount > 0 && tryMergeLast(mesh, program, count))
                return;

            if (cur_.draw.commandCount == 0)
            {
                cur_.draw.indirectOffset = indirectCursor;
                cur_.pushData = pushBase_;
                cur_.pushData.commandConstantsAddress =
                    buffers_->commandConstantsBuffer.address + constantCursor;
            }

            const auto cmd = mesh.getDrawCommand(count);
            buffers_->indirectDrawBuffer.write(indirectCursor, &cmd, sizeof cmd);
            const CommandConstant constant{.program = static_cast<std::uint32_t>(program),
                                           .address_offset = addressOffset};
            buffers_->commandConstantsBuffer.write(constantCursor, &constant,
                                                   sizeof constant);
            indirectCursor += sizeof(VkDrawIndexedIndirectCommand);
            constantCursor += sizeof(CommandConstant);
            ++cur_.draw.commandCount;
        }

        constexpr void end()
        {
            flush();
        }

      private:
        ShaderBuffers *buffers_{};

        DrawSegment cur_{};
        PushData pushBase_{};
        std::vector<DrawSegment> segments_;
        std::vector<VkRect2D> scissorStack_;
        bool hasViewport_{false};

        std::uint32_t heapCursor{0};
        std::uint32_t indirectCursor{0};
        std::uint32_t constantCursor{0};

        constexpr void reset() noexcept
        {
            heapCursor = 0;
            indirectCursor = 0;
            constantCursor = 0;
            segments_.clear();
            scissorStack_.clear();
            currentMesh = {};
            pushBase_ = {};
            cur_ = {};
            hasViewport_ = false;
        }

        [[nodiscard]] static VkRect2D intersect(const VkRect2D &a,
                                                const VkRect2D &b) noexcept
        {
            const std::int32_t x = std::max(a.offset.x, b.offset.x);
            const std::int32_t y = std::max(a.offset.y, b.offset.y);
            const std::int32_t r = std::min(a.offset.x + std::int32_t(a.extent.width),
                                            b.offset.x + std::int32_t(b.extent.width));
            const std::int32_t bt = std::min(a.offset.y + std::int32_t(a.extent.height),
                                             b.offset.y + std::int32_t(b.extent.height));
            return VkRect2D{.offset = {x, y},
                            .extent = {static_cast<std::uint32_t>(std::max(r - x, 0)),
                                       static_cast<std::uint32_t>(std::max(bt - y, 0))}};
        }

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

        constexpr bool tryMergeLast(const mesh_data &mesh, Program program,
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
                lastConst->program == static_cast<std::uint32_t>(program))
            {
                lastCmd->instanceCount += count;
                return true;
            }
            return false;
        }
    };

} // namespace shader_data
