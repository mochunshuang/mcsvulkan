#pragma once
// ============================================================================
// test_intent7_pass_abi.hpp
//
// Pass = PassDesc(target/attachments/load-store) + FrameContext(自己的
//        buffers/CommandBuilder) + PipelineState(base pipeline/layout/set/...)
//
// 这是“数据层”：一个 pass 拥有它要画的实例数据、indirect draw 表和 push
// constants. Recording is done by FrameBuilder / SecondaryBuilder.
//
// [pass ... pass] 是顶层：main pass 和每个 extra pass 都只是 PassSource，
// 按顺序 beginPass()/emit()/end()。没有编译器二次解释，也没有“先开 main
// pass 再结束它、重开它”的特殊路径。
// ============================================================================

#include "test_intent7_pass_stream.hpp"
#include "test_intent7_frame_context.hpp"

#include <cstdint>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

namespace pass_data
{

struct PipelineState
{
    VkPipeline pipeline{};
    VkPipelineLayout layout{};
    VkDescriptorSet set{};
    VkViewport viewport{};
    VkRect2D scissor{};
    PushData pushData{};
    mesh_data mesh{};
};

// 一个可复用的 pass 槽位：FrameContext 只分配一次，后续帧 reset() 复用。
struct Pass
{
    PassDesc desc{};
    PipelineState base{};
    shader_data::FrameContext frame;

    Pass(const LogicalDevice &device, VkDeviceSize vertexCapacity,
         VkDeviceSize indexCapacity, VkDeviceSize heapCapacity,
         VkDeviceSize indirectDrawCapacity, VkDeviceSize commandConstantsCapacity,
         PassDesc d, PipelineState b)
        : desc{std::move(d)}, base{b},
          frame{device, vertexCapacity, indexCapacity, heapCapacity, indirectDrawCapacity,
                commandConstantsCapacity}
    {
        reset(desc, base);
    }

    Pass(const Pass &) = delete;
    Pass &operator=(const Pass &) = delete;

    Pass(Pass &&other) noexcept
        : desc{std::move(other.desc)}, base{other.base}, frame{std::move(other.frame)}
    {
        syncAddresses();
    }

    Pass &operator=(Pass &&) = delete;

    // 复用槽位：重置 builder 的游标/segments，但保留 GPU buffer 分配。
    void reset(PassDesc d, PipelineState b)
    {
        desc = std::move(d);
        base = b;
        syncAddresses();
        frame.builder.begin({.pipeline = base.pipeline,
                             .layout = base.layout,
                             .pushData = base.pushData,
                             .mesh = base.mesh,
                             .viewport = base.viewport,
                             .scissor = base.scissor});
    }

    // 生成器（UI CommandBuilder）结束；此时 segments 已完整。
    void end() { frame.builder.end(); }

    [[nodiscard]] shader_data::CommandStream stream() const noexcept
    {
        return frame.builder.stream();
    }

    [[nodiscard]] const shader_data::ShaderBuffers &buffers() const noexcept
    {
        return frame.buffers;
    }

    [[nodiscard]] std::size_t command_count() const noexcept
    {
        const auto s = frame.builder.stream();
        return s.segments == nullptr ? 0 : s.segments->size();
    }

  private:
    void syncAddresses() noexcept
    {
        base.pushData.vertexAddress = frame.buffers.globalVertexBuffer.address;
        base.pushData.instanceAddress = frame.buffers.globalHeapBuffer.address;
        base.pushData.commandConstantsAddress =
            frame.buffers.commandConstantsBuffer.address;
    }
};

class PassManager
{
  public:
    PassManager(const LogicalDevice &device, VkDeviceSize vertexCapacity,
                VkDeviceSize indexCapacity, VkDeviceSize heapCapacity,
                VkDeviceSize indirectDrawCapacity, VkDeviceSize commandConstantsCapacity)
        : device_{device}, vertexCapacity_{vertexCapacity}, indexCapacity_{indexCapacity},
          heapCapacity_{heapCapacity}, indirectDrawCapacity_{indirectDrawCapacity},
          commandConstantsCapacity_{commandConstantsCapacity}
    {
        passes_.reserve(8);
    }

    Pass &beginPass(PassDesc desc, PipelineState base)
    {
        if (count_ == passes_.size())
        {
            passes_.emplace_back(device_, vertexCapacity_, indexCapacity_, heapCapacity_,
                                 indirectDrawCapacity_, commandConstantsCapacity_,
                                 std::move(desc), base);
        }
        else
        {
            passes_[count_].reset(std::move(desc), base);
        }
        return passes_[count_++];
    }

    void endPass()
    {
        if (count_ != 0)
            passes_[count_ - 1].end();
    }

    void reset()
    {
        count_ = 0;
    }

    [[nodiscard]] std::span<const Pass> passes() const noexcept
    {
        return {passes_.data(), count_};
    }

    [[nodiscard]] bool empty() const noexcept { return count_ == 0; }
    [[nodiscard]] std::size_t size() const noexcept { return count_; }

  private:
    const LogicalDevice &device_;
    VkDeviceSize vertexCapacity_{};
    VkDeviceSize indexCapacity_{};
    VkDeviceSize heapCapacity_{};
    VkDeviceSize indirectDrawCapacity_{};
    VkDeviceSize commandConstantsCapacity_{};
    std::vector<Pass> passes_{};
    std::size_t count_{0};
};

// 顶层 [pass ... pass] 的一项：可以是 app 的 main pass，也可以是
// PassManager 里的 extra pass。它只引用数据，不拥有数据。
struct PassSource
{
    const PassDesc *desc{};
    const shader_data::ShaderBuffers *buffers{};
    shader_data::CommandStream stream{};
    VkDescriptorSet descriptorSet{};
    // When true, the pass body is recorded into `secondary` and the primary
    // pass only calls vkCmdExecuteCommands.
    bool executeSecondary{false};
    VkCommandBuffer secondary{};
    InheritanceRenderingInfo inheritance{};
};

} // namespace pass_data
