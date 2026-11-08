#pragma once
// ============================================================================
// test_intent7_pass_compiler.hpp
//
// 顶层 [pass ... pass] 的录制循环。
//
// FrameBuilder 做三件事：
//   1. 把 beginPass/state/action/sync/endPass 追加成 OpStream；
//   2. 用 PassValidator 做增量校验；
//   3. 用 VkEmitter 把每个 Op 透明地翻译成显式 vkCmd*。
//
// 两个 pass 之间插入 pass_boundary_barrier()：
//   - 前一个 pass 的 storeOp 写 attachment
//   - 后一个 pass 的 loadOp 读 attachment
//   - 布局不变，所以只需要 VkMemoryBarrier2，不需要 image barrier
// ============================================================================

#include "test_intent7_pass_abi.hpp"
#include "test_intent7_pass_builder.hpp"

#include <array>
#include <cstddef>
#include <span>

namespace pass_data
{

inline void recordPass(FrameBuilder &frame, const PassSource &source)
{
    assert(source.desc != nullptr);

    PassTarget target = *source.desc;
    if (source.executeSecondary)
        target.flags |= VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT;

    frame.beginPass(target);
    if (source.executeSecondary)
    {
        const std::array<VkCommandBuffer, 1> secondary{source.secondary};
        frame.executeCommands(secondary, source.inheritance);
    }
    else
    {
        assert(source.buffers != nullptr);
        shader_data::emit(frame, *source.buffers, source.stream, source.descriptorSet);
    }
    frame.endPass();
}

inline void recordPasses(FrameBuilder &frame, std::span<const PassSource> passes)
{
    for (std::size_t i = 0; i < passes.size(); ++i)
    {
        if (i != 0)
            frame.barrier(pass_boundary_barrier());

        recordPass(frame, passes[i]);
    }
}

} // namespace pass_data
