#pragma once
// ============================================================================
// test_intent7_scene.hpp
//
// 主 pass 行 -> app 的 main pass builder（b）
// extra pass  -> PassManager::beginPass(desc, base)；pass 拥有 FrameContext 和
//                CommandStream。UI CommandBuilder 是 generator；endPass() 时把
//                segments 展开成通用 Cmd 流，PassCompiler 再逐条 -> vkCmd*。
// ============================================================================

#include "test_intent7_pass_abi.hpp"
#include "test_intent7_ui_dao.hpp"

#include <cstdint>
#include <print>
#include <span>
#include <vector>

namespace ui_new
{

struct test_intent7_scene final : RenderObject
{
    pass_data::PassManager *manager{};
    VkPipeline pipeline{};
    VkPipelineLayout layout{};
    mesh_data mesh{};
    VkExtent2D extent{};
    VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_4_BIT};

    std::vector<shader_data::TexQuad> text_{};

    test_intent7_scene(pass_data::PassManager *m, VkPipeline p, VkPipelineLayout l,
                       const mesh_data &mh, VkExtent2D e, VkSampleCountFlagBits s) noexcept
        : manager{m}, pipeline{p}, layout{l}, mesh{mh}, extent{e}, samples{s}
    {
    }

    void render(ScreenWidget *screen, Widget *, render_context &ctx) override
    {
        float W = static_cast<float>(screen->size().width);
        float H = static_cast<float>(screen->size().height);
        if (ctx.drawRecorder.hasViewport())
        {
            const auto vp = ctx.drawRecorder.currentViewport();
            if (vp.width > 0.0F && vp.height > 0.0F)
            {
                W = vp.width;
                H = vp.height;
            }
        }
        const ui_dao::view view{W, H};
        manager->reset();
        auto &b = ctx.drawRecorder;

        const glm::vec4 bg{0.055F, 0.062F, 0.080F, 1.0F};
        const glm::vec4 card{0.10F, 0.11F, 0.145F, 1.0F};
        const glm::vec4 accent{0.33F, 0.68F, 1.0F, 1.0F};
        const glm::vec4 text{0.90F, 0.93F, 0.98F, 1.0F};
        const glm::vec4 dim{0.55F, 0.60F, 0.70F, 1.0F};

        // 主 pass（app 的 main pass）
        b.addInstance(ui_dao::sdf_shape({24.0F, 24.0F, W - 48.0F, H - 48.0F}, view, 20.0F,
                                        bg, glm::vec4{0.0F, 0.0F, 0.0F, 0.5F}, 8.0F, 24.0F));
        b.addInstance(ui_dao::sdf_shape({60.0F, 140.0F, 420.0F, 220.0F}, view, 18.0F, card));

        text_.clear();
        emit_text(ctx, "INTENT7 PASS COMPILER", view, 30.0F, 60.0F, 90.0F, text);
        emit_text(ctx, "MAIN PASS", view, 20.0F, 90.0F, 200.0F, text);
        emit_text(ctx, "EXTRA PASS", view, 20.0F, 560.0F, 200.0F, dim);

        // extra pass：desc + base，pass 拥有 FrameContext/CommandStream
        const PushData push{
            .cameraIndex = 1,
            .renderVersion = screen->renderVersion(),
        };
        const VkViewport vp{.x = 0.0F,
                            .y = 0.0F,
                            .width = W,
                            .height = H,
                            .minDepth = 0.0F,
                            .maxDepth = 1.0F};
        const VkRect2D sc{.offset = {.x = 0, .y = 0},
                          .extent = {static_cast<std::uint32_t>(W),
                                     static_cast<std::uint32_t>(H)}};
        auto &pass = manager->beginPass(
            {.target = pass_data::Target::Main,
             .colorLoad = pass_data::LoadOp::Load},
            {.pipeline = pipeline,
             .layout = layout,
             .set = ctx.mainSet,
             .viewport = vp,
             .scissor = sc,
             .pushData = push,
             .mesh = mesh});
        if (!ctx.globalVertices.empty() && !ctx.globalIndices.empty())
        {
            pass.frame.buffers.globalVertexBuffer.write(
                0, ctx.globalVertices.data(), ctx.globalVertices.size_bytes());
            pass.frame.buffers.globalIndexBuffer.write(
                0, ctx.globalIndices.data(), ctx.globalIndices.size_bytes());
        }
        pass.frame.builder.addInstance(
            ui_dao::sdf_shape({560.0F, 140.0F, 420.0F, 220.0F}, view, 18.0F, accent));
        manager->endPass();

        if (!text_.empty())
            b.addInstances(std::span<const shader_data::TexQuad>{text_});

        std::println("[intent7] passes={} main segs={} extra cmds={}", manager->passes().size(),
                     b.stream().segments ? b.stream().segments->size() : 0,
                     manager->passes().empty() ? 0 : manager->passes().front().stream.size());
    }

  private:
    void emit_text(render_context &ctx, const char *s, const ui_dao::view &view, float px,
                   float x, float y, const glm::vec4 &color)
    {
        auto result = run_text_pipeline(ctx.fontSelect, s, "zh-CN");
        float cursor = x;
        for (const auto &run : result.shape_result)
        {
            for (const auto &g : run)
            {
                if (!(g.plane_bounds == decltype(g.plane_bounds){}))
                    text_.push_back(ui_dao::glyph_from_shaped(g, view, px, cursor, y, color));
                cursor += static_cast<float>(g.advance_x) * px;
            }
        }
    }
};

} // namespace ui_new
