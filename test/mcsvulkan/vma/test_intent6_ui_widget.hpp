#pragma once
// ═══════════════════════════════════════════════════════════════════════════
// test_intent6_ui_widget.hpp —— 前端 Widget：把特性组合成一个 Widget
//
//   位置：在 test_intent6.cpp 里、ScreenWidget/render_context/文本管线之后
//   包含。前端只做两件事：
//     1. 调 controller 的特性接口（panel/button/cell/…）；
//     2. 需要文字时，用现有文本管线整形，再把 Glyph 交给 controller。
//   前端不碰 service::canvas、不碰 DAO、不碰 ABI。
// ═══════════════════════════════════════════════════════════════════════════

namespace ui_new
{

struct arch_render_object final : RenderObject
{
    void render(ScreenWidget *screen, Widget *, render_context &ctx) override
    {
        const float w = static_cast<float>(screen->size().width);
        const float h = static_cast<float>(screen->size().height);

        // 前端 → controller（后端接口）
        ui_controller::client ui{ctx.drawRecorder, w, h};
        ui.reference();

        // 文字：前端整形（复用现有 pipeline），结果缓存，避免每帧重排
        if (text_w_ != w || text_h_ != h)
        {
            text_.clear();
            auto textResult =
                run_text_pipeline(ctx.fontSelect, "三层 UI：controller → service → dao", "zh-CN");
            const float px = 24.0F;
            const float baselineY = 380.0F;
            float cursorX = 60.0F;
            const glm::vec4 color{0.86F, 0.88F, 0.93F, 1.0F};
            for (const auto &run : textResult.shape_result)
            {
                for (const auto &g : run)
                {
                    text_.push_back(make_glyph(g, w, h, px, cursorX, baselineY, 0, 0, ~0U,
                                               color, 1U));
                    cursorX += g.advance_x * px;
                }
            }
            text_w_ = w;
            text_h_ = h;
            std::println("[arch] viewport={}x{} text_glyphs={}", w, h, text_.size());
        }
        ui.glyphs(text_);
    }

  private:
    std::vector<shader_data::Glyph> text_;
    float text_w_ = -1.0F;
    float text_h_ = -1.0F;
};

} // namespace ui_new
