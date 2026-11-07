#pragma once
// ============================================================================
// test_intent7_pass_tests.hpp
//
// pass 测试入口：每个 case 就是一次 beginPass(desc, base) + generator draws +
// endPass()；endPass() 展开成 CommandStream，PassCompiler 再逐条 -> vkCmd*。
//
// 现在可以直接在这里填 case：改 case 的 PassDesc / PipelineState / draws /
// 可选 Cmd（barrier/query/execute...）即可；不需要碰 app/render loop。
// ============================================================================

#include "test_intent7_pass_abi.hpp"
#include "test_intent7_ui_dao.hpp"

#include <cstdint>
#include <print>
#include <span>
#include <vector>

namespace ui_new
{

struct pass_test_scene final : RenderObject
{
    pass_data::PassManager *manager{};
    VkPipeline pipeline{};
    VkPipelineLayout layout{};
    mesh_data mesh{};
    VkExtent2D extent{};
    VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_4_BIT};

    std::vector<shader_data::TexQuad> text_{};

    pass_test_scene(pass_data::PassManager *m, VkPipeline p, VkPipelineLayout l,
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
        const glm::vec4 text{0.90F, 0.93F, 0.98F, 1.0F};
        const glm::vec4 dim{0.55F, 0.60F, 0.70F, 1.0F};
        const glm::vec4 colors[3] = {
            {0.33F, 0.68F, 1.0F, 1.0F},
            {1.0F, 0.42F, 0.38F, 1.0F},
            {0.36F, 0.86F, 0.55F, 1.0F},
        };
        const char *labels[3] = {"CASE 0", "CASE 1", "CASE 2"};

        // 主 pass：背景 + 标题（app 的 main pass）
        b.addInstance(ui_dao::sdf_shape({24.0F, 24.0F, W - 48.0F, H - 48.0F}, view, 20.0F,
                                        bg, glm::vec4{0.0F, 0.0F, 0.0F, 0.5F}, 8.0F, 24.0F));
        text_.clear();
        emit_text(ctx, "PASS TESTS", view, 30.0F, 60.0F, 90.0F, text);
        emit_text(ctx, "each case = PassDesc + CommandStream", view, 18.0F, 60.0F,
                  130.0F, dim);

        for (int i = 0; i < 3; ++i)
        {
            const float x = 80.0F + static_cast<float>(i) * 390.0F;
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
                ui_dao::sdf_shape({x, 170.0F, 340.0F, 220.0F}, view, 18.0F, colors[i]));
            manager->endPass();

            emit_text(ctx, labels[i], view, 20.0F, x + 24.0F, 210.0F, text);
            std::println("[pass-test] case={} cmds={}", i,
                         manager->passes().back().stream.size());
        }

        if (!text_.empty())
            b.addInstances(std::span<const shader_data::TexQuad>{text_});
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
