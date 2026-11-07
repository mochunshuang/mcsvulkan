#pragma once
// ═══════════════════════════════════════════════════════════════════════════
// test_intent6_ability_test.hpp —— 独立的新测试。
//
// 本 render 即未来 service 的草稿：内部多次调 DAO；arc/九宫格/表格/文本的
// 数据转化在 ui_compose；clip/blend/layer/backdrop 是 pass，这里不伪造。
//
// 目标：只用 7 个 program 的「参数 + 多实例组合」，画出 test_ui_ability2
// 的绘制能力（fragment 级 24/34）。窗口 1280×900，每个功能按像素分配
// footprint：tile 236×181、table 482×181、文本 14~32px。
//
// 切换测试：test_intent6.cpp 里把 screen 的 renderObject 换成
// ability_compose_render_object 即可。
// ═══════════════════════════════════════════════════════════════════════════

#include "test_intent6_compose.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <print>
#include <span>
#include <vector>

namespace ui_new
{

struct ability_compose_render_object final : RenderObject
{
    static constexpr std::uint32_t kTexPhoto = 0;
    static constexpr std::uint32_t kTexGradient = 2;
    static constexpr std::uint32_t kTexChess = 3;
    static constexpr std::uint32_t kSamplerLinear = 0;

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
        if (W < 64.0F || H < 64.0F)
            return;

        const float s = std::max(W / 800.0F, 1.0F); // 800×600 为基准的像素缩放
        const ui_dao::view view{W, H};
        auto &b = ctx.drawRecorder;

        const float margin = 14.0F * s;
        const float top = 78.0F * s;
        const float bottom = 12.0F * s;
        const float gap = 6.0F * s;
        constexpr int cols = 5;
        constexpr int rows = 4;
        const float tileW = (W - 2.0F * margin - float(cols - 1) * gap) / float(cols);
        const float tileH = (H - top - bottom - float(rows - 1) * gap) / float(rows);
        if (tileW < 32.0F || tileH < 32.0F)
            return;

        const auto tile_at = [&](int c, int r) {
            return ui_dao::rect_px{margin + float(c) * (tileW + gap),
                                   top + float(r) * (tileH + gap), tileW, tileH};
        };
        const auto content_of = [&](const ui_dao::rect_px &t) {
            return ui_dao::rect_px{t.x + 7.0F * s, t.y + 20.0F * s,
                                   std::max(t.w - 14.0F * s, 8.0F),
                                   std::max(t.h - 27.0F * s, 8.0F)};
        };
        const auto wide = ui_dao::rect_px{margin + 3.0F * (tileW + gap),
                                          top + 3.0F * (tileH + gap),
                                          2.0F * tileW + gap, tileH};

        const glm::vec4 bg{0.055F, 0.062F, 0.080F, 1.0F};
        const glm::vec4 card{0.10F, 0.11F, 0.145F, 1.0F};
        const glm::vec4 card_alt{0.135F, 0.15F, 0.19F, 1.0F};
        const glm::vec4 line{1.0F, 1.0F, 1.0F, 0.10F};
        const glm::vec4 accent{0.33F, 0.68F, 1.0F, 1.0F};
        const glm::vec4 cyan{0.25F, 0.85F, 0.78F, 1.0F};
        const glm::vec4 orange{1.0F, 0.60F, 0.26F, 1.0F};
        const glm::vec4 pink{1.0F, 0.32F, 0.52F, 1.0F};
        const glm::vec4 green{0.45F, 0.88F, 0.52F, 1.0F};
        const glm::vec4 yellow{1.0F, 0.85F, 0.35F, 1.0F};
        const glm::vec4 dim{0.55F, 0.60F, 0.70F, 1.0F};

        b.addInstance(ui_dao::sdf_shape({8.0F * s, 8.0F * s, W - 16.0F * s, H - 16.0F * s},
                                        view, 16.0F * s, bg,
                                        glm::vec4{0.0F, 0.0F, 0.0F, 0.5F}, 4.0F * s,
                                        12.0F * s));
        b.addInstance(ui_dao::shape_border({8.0F * s, 8.0F * s, W - 16.0F * s,
                                            H - 16.0F * s},
                                           view, 16.0F * s, line, 1.0F * s));

        // 文本/遮罩元数据先建，MASKED tile 的 mask 才在 draw 前就绪
        if (text_w_ != W || text_h_ != H)
        {
            build_text(ctx.fontSelect, W, H, tile_at, content_of, wide);
            text_w_ = W;
            text_h_ = H;
            std::println("[compose] viewport={}x{} scale={} | 7 programs: shape "
                         "path_fill path_stroke texture blur color_matrix noise | tiles=19",
                         W, H, s);
        }

        for (int i = 0; i < 18; ++i)
        {
            const auto t = tile_at(i % cols, i / cols);
            b.addInstance(ui_dao::sdf_shape(t, view, 10.0F * s, card,
                                            glm::vec4{0.0F, 0.0F, 0.0F, 0.28F},
                                            2.0F * s, 6.0F * s));
            b.addInstance(ui_dao::shape_border(t, view, 10.0F * s, line, 1.0F * s));
            effect(i, b, view, content_of(t), accent, cyan, orange, pink, green, yellow,
                   card_alt, s);
        }

        b.addInstance(ui_dao::sdf_shape(wide, view, 12.0F * s, card,
                                        glm::vec4{0.0F, 0.0F, 0.0F, 0.30F}, 2.0F * s,
                                        8.0F * s));
        b.addInstance(ui_dao::shape_border(wide, view, 12.0F * s, line, 1.0F * s));
        ui_compose::composed_ui(b, wide, view, accent, card_alt, green, orange, dim,
                                kTexChess, kSamplerLinear, s);

        if (!text_.empty())
            b.addInstances(std::span<const shader_data::TexQuad>{text_});
    }

  private:
    std::vector<shader_data::TexQuad> text_;
    float text_w_ = -1.0F;
    float text_h_ = -1.0F;

    std::uint32_t mask_tex_ = kTexPhoto;
    std::uint32_t mask_sampler_ = kSamplerLinear;
    UvTransform mask_uv_{};

    void effect(int i, shader_data::CommandBuilder &b, const ui_dao::view &v,
                       const ui_dao::rect_px &c, const glm::vec4 &accent,
                       const glm::vec4 &cyan, const glm::vec4 &orange,
                       const glm::vec4 &pink, const glm::vec4 &green,
                       const glm::vec4 &yellow, const glm::vec4 &card_alt, float s)
    {
        switch (i)
        {
        case 0: {
            // ability2 MODE_LINEAR / MODE_SRGB：上 gamma、下 linear
            const ui_dao::rect_px top{c.x, c.y, c.w, c.h * 0.5F};
            const ui_dao::rect_px bot{c.x, c.y + c.h * 0.5F, c.w, c.h * 0.5F};
            ui_compose::linear(b, top, v, glm::pow(accent, glm::vec4(1.0F / 2.2F)),
                               glm::pow(pink, glm::vec4(1.0F / 2.2F)), true, 8.0F * s);
            ui_compose::linear(b, bot, v, accent, pink, true, 8.0F * s);
        }
        break;
        case 1:
            ui_compose::radial(b, c, v, yellow, glm::vec4{0.12F, 0.10F, 0.18F, 1.0F},
                               8.0F * s);
            break;
        case 2:
            ui_compose::conic(b, c, v, cyan, pink, 3.0F, 8.0F * s);
            break;
        case 3:
            ui_compose::per_corner(
                b, c, v, glm::vec4{24.0F * s, 4.0F * s, 24.0F * s, 4.0F * s}, accent);
            break;
        case 4:
            ui_compose::ring(b, c, v, cyan, 5.0F * s, 0.72F);
            ui_compose::ring(b, c, v, yellow, 3.0F * s, 0.44F);
            break;
        case 5:
            ui_compose::arc(b, c, v, cyan, 6.0F * s);
            break;
        case 6: {
            // ability2 MODE_PATH_AA：左 AA on，右 AA off（硬边）
            const ui_dao::rect_px left{c.x, c.y, c.w * 0.48F, c.h};
            const ui_dao::rect_px right{c.x + c.w * 0.52F, c.y, c.w * 0.48F, c.h};
            ui_compose::triangle(b, left, v, accent, cyan, 2.0F * s);
            ui_compose::triangle(b, right, v, accent, cyan, 0.0F);
        }
        break;
        case 7:
            ui_compose::star(b, c, v, accent, pink, 1.0F * s);
            break;
        case 8:
            ui_compose::dash(b, c, v, yellow, 5.0F * s);
            break;
        case 9:
            ui_compose::rotated(b, c, v, accent, 0.42F, 8.0F * s);
            break;
        case 10:
            ui_compose::skewed(b, c, v, pink, 0.42F, 8.0F * s);
            break;
        case 11: {
            const ui_dao::rect_px left{c.x, c.y, c.w * 0.48F, c.h};
            const ui_dao::rect_px right{c.x + c.w * 0.52F, c.y, c.w * 0.48F, c.h};
            ui_compose::nine_slice_color(b, left, v, 2.0F * s, 8.0F * s);
            ui_compose::nine_slice(b, right, v, kTexGradient, kSamplerLinear, 0.2F,
                                   14.0F * s);
        }
        break;
        case 12: {
            const ui_dao::rect_px left{c.x, c.y, c.w * 0.48F, c.h};
            const ui_dao::rect_px right{c.x + c.w * 0.52F, c.y, c.w * 0.48F, c.h};
            ui_compose::alpha_mask(b, left, v, accent, 8.0F * s, 12.0F * s, true);
            ui_compose::masked(b, right, v, kTexPhoto, kSamplerLinear, mask_tex_,
                               mask_sampler_, mask_uv_, 6.0F * s);
        }
        break;
        case 13:
            ui_compose::inner_shadow(b, c, v, card_alt,
                                     glm::vec4{0.0F, 0.0F, 0.0F, 0.55F},
                                     glm::vec4{0.0F, 0.0F, 0.0F, 0.55F}, 9.0F * s,
                                     16.0F * s, 7.0F * s, s);
            break;
        case 14: {
            // ability2 MODE_BLUR / MODE_MIPMAP_TEX：上模糊，下 4 级 explicit lod
            const ui_dao::rect_px top{c.x, c.y, c.w, c.h * 0.48F};
            const ui_dao::rect_px bot{c.x, c.y + c.h * 0.52F, c.w, c.h * 0.48F};
            b.addInstance(ui_dao::blur(top, v, kTexPhoto, kSamplerLinear, 24.0F * s, 5U,
                                       glm::vec4(1.0F), UvTransform{}, 6.0F * s));
            for (int k = 0; k < 4; ++k)
            {
                const float h = bot.h / 4.0F;
                b.addInstance(ui_dao::texture_lod(
                    {bot.x, bot.y + float(k) * h, bot.w, h}, v, kTexPhoto,
                    kSamplerLinear, float(k) * 1.5F, glm::vec4(1.0F), UvTransform{},
                    0.0F));
            }
        }
        break;
        case 15: {
            glm::mat4 sepia{1.0F};
            sepia[0] = glm::vec4{0.393F, 0.349F, 0.272F, 0.0F};
            sepia[1] = glm::vec4{0.769F, 0.686F, 0.534F, 0.0F};
            sepia[2] = glm::vec4{0.189F, 0.168F, 0.131F, 0.0F};
            sepia[3] = glm::vec4{0.0F, 0.0F, 0.0F, 1.0F};
            b.addInstance(ui_dao::color_matrix(c, v, kTexPhoto, kSamplerLinear, sepia,
                                               glm::vec4{0.02F, 0.01F, 0.0F, 0.0F},
                                               glm::vec4(1.0F), UvTransform{},
                                               6.0F * s));
        }
        break;
        case 16:
            b.addInstance(ui_dao::noise(c, v, glm::vec4{0.02F, 0.05F, 0.12F, 1.0F},
                                        glm::vec4{0.20F, 0.78F, 0.88F, 1.0F}, 5.0F,
                                        7.0F, 5U, 6.0F * s));
            break;
        case 17:
            // glyph 文本由 build_text() 缓存后统一提交
            break;
        default:
            break;
        }
    }

    template <class FontSelectT, class TileAt, class ContentOf>
    void build_text(FontSelectT &fontSelect, float W, float H, const TileAt &tile_at,
                    const ContentOf &content_of, const ui_dao::rect_px &wide)
    {
        const float s = std::max(W / 800.0F, 1.0F);
        const ui_dao::view view{W, H};
        text_.clear();

        emit_run(text_, fontSelect, "ABILITY2 -> DAO COMPOSITION", view, 20.0F * s,
                 16.0F * s, 30.0F * s, glm::vec4{0.90F, 0.93F, 0.98F, 1.0F});
        emit_run(text_, fontSelect,
                 "7 programs + params + composition; pass: blend/clip/layer/mipmap",
                 view, 10.0F * s, 16.0F * s, 48.0F * s,
                 glm::vec4{0.55F, 0.60F, 0.70F, 1.0F});

        static constexpr std::array<const char *, 18> names = {
            "LINEAR/SRGB", "RADIAL",  "CONIC",    "PER-CORNER", "RING",
            "ARC",     "TRIANGLE/AA", "STAR",  "DASH",         "ROTATE",
            "SKEW",    "NINE-SLICE", "MASKED", "INNER SHADOW", "BLUR/MIPMAP",
            "SEPIA",   "NOISE",    "TEXT/EMOJI"};
        for (int i = 0; i < 18; ++i)
        {
            const auto t = tile_at(i % 5, i / 5);
            emit_run(text_, fontSelect, names[static_cast<std::size_t>(i)], view,
                     9.0F * s, t.x + 8.0F * s, t.y + 14.0F * s,
                     glm::vec4{0.55F, 0.60F, 0.70F, 1.0F});
        }
        emit_run(text_, fontSelect, "COMPOSED UI", view, 9.0F * s, wide.x + 8.0F * s,
                 wide.y + 14.0F * s, glm::vec4{0.90F, 0.93F, 0.98F, 1.0F});

        emit_run(text_, fontSelect, "NAME / STATE", view, 9.0F * s, wide.x + 14.0F * s,
                 wide.y + 36.0F * s, glm::vec4{0.08F, 0.10F, 0.14F, 1.0F});
        // WRAP / SELECT / RICH 的草稿：CPU 布局 -> glyph，selection 用 Shape
        emit_run(text_, fontSelect, "WRAP TEXT", view, 8.0F * s, wide.x + 180.0F * s,
                 wide.y + 56.0F * s, glm::vec4{0.33F, 0.68F, 1.0F, 1.0F});
        emit_run(text_, fontSelect, "BY WIDTH", view, 8.0F * s, wide.x + 180.0F * s,
                 wide.y + 68.0F * s, glm::vec4{0.85F, 0.88F, 0.94F, 1.0F});
        emit_run(text_, fontSelect, "RICH", view, 8.0F * s, wide.x + 180.0F * s,
                 wide.y + 80.0F * s, glm::vec4{1.0F, 0.32F, 0.52F, 1.0F});

        static constexpr std::array<const char *, 3> rows = {"row A", "row B", "row C"};
        static constexpr std::array<const char *, 3> states = {"ready", "busy", "idle"};
        for (int i = 0; i < 3; ++i)
        {
            const float y = wide.y + 48.0F * s + float(i) * 22.0F * s;
            emit_run(text_, fontSelect, rows[static_cast<std::size_t>(i)], view,
                     9.0F * s, wide.x + 32.0F * s, y + 13.0F * s,
                     glm::vec4{0.85F, 0.88F, 0.94F, 1.0F});
            emit_run(text_, fontSelect, states[static_cast<std::size_t>(i)], view,
                     9.0F * s, wide.x + 90.0F * s, y + 13.0F * s,
                     glm::vec4{0.55F, 0.60F, 0.70F, 1.0F});
        }

        const auto g = tile_at(2, 3);
        const auto gc = content_of(g);
        emit_run(text_, fontSelect, "😀 世界", view, 16.0F * s, gc.x + gc.w * 0.5F,
                 gc.y + gc.h * 0.36F, glm::vec4{0.90F, 0.93F, 0.98F, 1.0F}, true);
        emit_run(text_, fontSelect, "WRAP", view, 9.0F * s, gc.x + 8.0F * s,
                 gc.y + gc.h * 0.64F, glm::vec4{0.33F, 0.68F, 1.0F, 1.0F});
        emit_run(text_, fontSelect, "TEXT", view, 9.0F * s, gc.x + 42.0F * s,
                 gc.y + gc.h * 0.64F, glm::vec4{1.0F, 0.32F, 0.52F, 1.0F});
        emit_run(text_, fontSelect, "مرحبا", view, 9.0F * s, gc.x + 8.0F * s,
                 gc.y + gc.h * 0.86F, glm::vec4{0.55F, 0.60F, 0.70F, 1.0F});

        auto mask = run_text_pipeline(fontSelect, "世", "zh-CN");
        for (const auto &run : mask.shape_result)
        {
            for (const auto &sg : run)
            {
                const auto q = ui_dao::glyph_from_shaped(sg, view, 64.0F * s, 0.0F, 0.0F,
                                                         glm::vec4(1.0F));
                mask_tex_ = q.textureIndex;
                mask_sampler_ = q.samplerIndex;
                mask_uv_ = q.uvTransform;
                return;
            }
        }
    }

    template <class FontSelectT>
    static void emit_run(std::vector<shader_data::TexQuad> &out, FontSelectT &fontSelect,
                         const char *text, const ui_dao::view &view, float px,
                         float x, float baseline, const glm::vec4 &color,
                         bool center = false)
    {
        auto result = run_text_pipeline(fontSelect, text, "zh-CN");
        const auto &runs = result.shape_result;

        float total = 0.0F;
        if (center)
            for (const auto &run : runs)
                for (const auto &g : run)
                    total += static_cast<float>(g.advance_x) * px;

        float cursor = center ? x - total * 0.5F : x;
        for (const auto &run : runs)
        {
            for (const auto &g : run)
            {
                if (!(g.plane_bounds == decltype(g.plane_bounds){}))
                    out.push_back(ui_dao::glyph_from_shaped(g, view, px, cursor,
                                                            baseline, color));
                cursor += static_cast<float>(g.advance_x) * px;
            }
        }
    }
};

} // namespace ui_new
