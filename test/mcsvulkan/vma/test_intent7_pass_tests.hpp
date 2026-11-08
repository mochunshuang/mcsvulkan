#pragma once
// ============================================================================
// test_intent7_pass_tests.hpp
//
// [pass ... pass] 的可视化测试：
//
//   pass 0 : app 的 main pass        —— 背景（clear + LOAD/STORE 语义由 app 给）
//   pass 1 : CASE 0 card             —— LOAD/STORE，独立 FrameContext
//   pass 2 : CASE 1 card             —— LOAD/STORE
//   pass 3 : CASE 2 card             —— LOAD/STORE
//   pass 4 : overlay text            —— LOAD/DONT_CARE，最后画，保证文字在卡片上
//
// 每个 pass 都显式写自己的 PassTarget（从 ctx.mainPassDesc 复制并改 load/store），
// 不再由 PassCompiler 猜 target 或替它重开 main pass。
// ============================================================================

#include "test_intent7_pass_abi.hpp"
#include "test_intent7_ui_dao.hpp"

#include <algorithm>
#include <cstdint>
#include <print>
#include <span>
#include <vector>

namespace ui_new
{

struct pass_test_scene final : RenderObject
{
    VkPipeline pipeline{};
    VkPipelineLayout layout{};
    mesh_data mesh{};
    VkExtent2D extent{};
    VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_4_BIT};

    std::vector<shader_data::TexQuad> text_{};

    pass_test_scene(VkPipeline p, VkPipelineLayout l, const mesh_data &mh,
                    VkExtent2D e, VkSampleCountFlagBits s) noexcept
        : pipeline{p}, layout{l}, mesh{mh}, extent{e}, samples{s}
    {
    }

    void render(ScreenWidget *screen, Widget *, render_context &ctx) override
    {
        auto *manager = ctx.passManager;
        assert(manager != nullptr &&
               "render_context must carry the current frame's PassManager");
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

        assert(ctx.mainPassDesc != nullptr &&
               "the app must provide the target PassDesc before recording [pass...pass]");

        const glm::vec4 bg{0.055F, 0.062F, 0.080F, 1.0F};
        const glm::vec4 text{0.90F, 0.93F, 0.98F, 1.0F};
        const glm::vec4 dim{0.55F, 0.60F, 0.70F, 1.0F};
        const glm::vec4 colors[3] = {
            {0.33F, 0.68F, 1.0F, 1.0F},
            {1.0F, 0.42F, 0.38F, 1.0F},
            {0.36F, 0.86F, 0.55F, 1.0F},
        };
        const char *labels[3] = {"CASE 0", "CASE 1", "CASE 2"};

        // 主 pass：背景 + 标题（title 在 overlay pass，随后画）
        b.addInstance(ui_dao::sdf_shape({24.0F, 24.0F, W - 48.0F, H - 48.0F}, view, 20.0F,
                                        bg, glm::vec4{0.0F, 0.0F, 0.0F, 0.5F}, 8.0F, 24.0F));

        text_.clear();
        emit_text(ctx, "PASS TESTS", view, 30.0F, 60.0F, 90.0F, text);
        emit_text(ctx, "each case = PassTarget + OpStream + Emitter", view, 18.0F,
                  60.0F, 130.0F, dim);

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

        const pass_data::PipelineState base{
            .pipeline = pipeline,
            .layout = layout,
            .set = ctx.mainSet,
            .viewport = vp,
            .scissor = sc,
            .pushData = push,
            .mesh = mesh,
        };

        // 每个 card 是一个独立 pass。LOAD/STORE 保证下一个 pass 能看见
        // 上一个 pass 的 MSAA/depth/pick 内容（pass 之间另有 memory barrier）。
        auto cardDesc =
            ctx.mainPassDesc->withLoad(VK_ATTACHMENT_LOAD_OP_LOAD)
                .withStore(VK_ATTACHMENT_STORE_OP_STORE);

        // 3 列自适应宽度：布局限制在 900 宽以内，避免高 DPI 窗口把
        // 第三张卡片裁到窗口外。
        const float layoutW = std::min(W, 900.0F);
        const float margin = 40.0F;
        const float gap = 24.0F;
        const float cardW = (layoutW - 2.0F * margin - 2.0F * gap) / 3.0F;
        const float cardH = 220.0F;

        for (int i = 0; i < 3; ++i)
        {
            const float x = margin + static_cast<float>(i) * (cardW + gap);
            auto &pass = manager->beginPass(cardDesc, base);
            upload_global_mesh(pass, ctx);
            pass.frame.builder.addInstance(
                ui_dao::sdf_shape({x, 170.0F, cardW, cardH}, view, 18.0F, colors[i]));
            manager->endPass();

            emit_text(ctx, labels[i], view, 20.0F, x + 24.0F, 210.0F, text);
        }

        // overlay pass 必须最后：把标题/副标题/CASE 标签画在卡片之上。
        // 这是最后一个 pass，所以 attachment store 用 DONT_CARE（resolve 仍执行）。
        auto overlayDesc =
            ctx.mainPassDesc->withLoad(VK_ATTACHMENT_LOAD_OP_LOAD)
                .withStore(VK_ATTACHMENT_STORE_OP_DONT_CARE);
        auto &overlay = manager->beginPass(overlayDesc, base);
        upload_global_mesh(overlay, ctx);
        if (!text_.empty())
            overlay.frame.builder.addInstances(
                std::span<const shader_data::TexQuad>{text_});
        manager->endPass();

        std::println("[pass-test] passes={} card_cmds={} overlay_cmds={}",
                     manager->passes().size(),
                     manager->passes().size() >= 4
                         ? manager->passes()[0].command_count()
                         : 0,
                     manager->passes().empty()
                         ? 0
                         : manager->passes().back().command_count());
    }

  private:
    static void upload_global_mesh(pass_data::Pass &pass, render_context &ctx)
    {
        if (!ctx.globalVertices.empty() && !ctx.globalIndices.empty())
        {
            pass.frame.buffers.globalVertexBuffer.write(
                0, ctx.globalVertices.data(), ctx.globalVertices.size_bytes());
            pass.frame.buffers.globalIndexBuffer.write(
                0, ctx.globalIndices.data(), ctx.globalIndices.size_bytes());
        }
    }

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
