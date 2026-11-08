#pragma once
// ============================================================================
// test_intent7_pass_stream.hpp
//
// Kept only for the pass-boundary barrier. The legacy immediate PassRecorder /
// FrameRecorder path was removed: FrameBuilder / SecondaryBuilder are the only
// pass recording paths now.
// ============================================================================

#include "../head.hpp"
#include "test_intent7_pass_data.hpp"

namespace pass_data
{

// Default memory barrier between adjacent passes:
// - previous pass storeOp writes the attachments
// - next pass loadOp reads the attachments
// - layouts do not change, so only VkMemoryBarrier2 is needed.
inline VkMemoryBarrier2 pass_boundary_barrier() noexcept
{
    return VkMemoryBarrier2{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .pNext = nullptr,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                         VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                         VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    };
}

} // namespace pass_data
