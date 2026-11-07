#pragma once
// ═══════════════════════════════════════════════════════════════════════════
// test_intent6_dao_test.hpp —— DAO 直连 shader 的机制程序测试
//
//   7 个机制程序（一个机制一个像素程序），border 是 shape 的参数：
//     M1 Shape(SDF)  M2 PathFill  M3 PathStroke  M4 Texture
//     M5 Blur(卷积)  M6 ColorMatrix(逐点矩阵)  M7 Noise(程序化)
//   glyph 不是独立程序：它是 M4 的 metadata（fontType/pxRange/modulate）。
//
//   一个 Node（"rectBox"）里画 8 个 tile：
//     M1..M4 + M4b(glyph meta) + M5..M7
//
//   约束：
//     · render() 内部只调用 ui_dao::* 编码器 + ctx.drawRecorder 提交；
//     · 不碰 canvas / ABI / NDC；
//     · 数据写死在这里（像素坐标、颜色、路径点、纹理槽位）。
//
//   依赖：在 test_intent6.cpp 中、ScreenWidget / render_context / ui_dao
//   与 run_text_pipeline 之后 include。
// ═══════════════════════════════════════════════════════════════════════════

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <print>
#include <span>
#include <vector>

namespace ui_new
{

struct dao_primitives_render_object final : RenderObject
{
    // ── 纹理槽位（descriptorInit 的上传顺序，写死；见 test_intent6.cpp）──
    //   0=texture.jpg  1=white  2=gradient  3=chessboard
    //   0=linear sampler，1=nearest sampler
    static constexpr std::uint32_t kTexPhoto = 0;
    static constexpr std::uint32_t kSamplerLinear = 0;

    void render(ScreenWidget *screen, Widget *, render_context &ctx) override
    {
        // 布局必须以真实 framebuffer（builder 的 viewport）为准：
        // ScreenWidget 的 size 只在 recreateSwapChain 时才更新，首帧可能是旧值。
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
        if (W < 32.0F || H < 32.0F)
            return;

        const ui_dao::view view{W, H};
        auto &b = ctx.drawRecorder;

        // ── 布局：4 列 × 2 行，全部在一个 Node 内 ─────────────────────
        const float margin = 20.0F;
        const float top = 92.0F;
        const float bottom = 20.0F;
        const float gap = 14.0F;
        constexpr int cols = 4;
        constexpr int rows = 2;
        const float tileW = (W - 2.0F * margin - float(cols - 1) * gap) / float(cols);
        const float tileH = (H - top - bottom - float(rows - 1) * gap) / float(rows);
        if (tileW < 64.0F || tileH < 64.0F)
            return;

        const auto tile_rect = [&](int c, int r) {
            return ui_dao::rect_px{margin + float(c) * (tileW + gap),
                                   top + float(r) * (tileH + gap), tileW, tileH};
        };
        const auto inset = [](const ui_dao::rect_px &r, float px) {
            return ui_dao::rect_px{r.x + px, r.y + px, std::max(r.w - 2.0F * px, 1.0F),
                                   std::max(r.h - 2.0F * px, 1.0F)};
        };

        // ── 调色板 ────────────────────────────────────────────────────
        const glm::vec4 bg{0.050F, 0.058F, 0.078F, 1.0F};
        const glm::vec4 card{0.090F, 0.100F, 0.130F, 1.0F};
        const glm::vec4 line{1.0F, 1.0F, 1.0F, 0.10F};
        const glm::vec4 text{0.90F, 0.93F, 0.98F, 1.0F};
        const glm::vec4 dim{0.52F, 0.58F, 0.68F, 1.0F};
        const glm::vec4 accent{0.33F, 0.68F, 1.0F, 1.0F};
        const glm::vec4 cyan{0.25F, 0.85F, 0.78F, 1.0F};
        const glm::vec4 orange{1.0F, 0.60F, 0.26F, 1.0F};
        const glm::vec4 pink{1.0F, 0.32F, 0.52F, 1.0F};
        const glm::vec4 green{0.45F, 0.88F, 0.52F, 1.0F};

        // ═════════════════════════════════════════════════════════════
        // 背景：sdf_shape 大卡片 + shape_border（同一个 Shape 程序）
        // ═════════════════════════════════════════════════════════════
        b.addInstance(ui_dao::sdf_shape({8.0F, 8.0F, W - 16.0F, H - 16.0F}, view, 18.0F,
                                        bg, glm::vec4{0.0F, 0.0F, 0.0F, 0.55F}, 6.0F,
                                        18.0F));
        b.addInstance(ui_dao::shape_border({8.0F, 8.0F, W - 16.0F, H - 16.0F}, view,
                                           18.0F, glm::vec4{1.0F, 1.0F, 1.0F, 0.08F},
                                           1.0F));

        // ═════════════════════════════════════════════════════════════
        // tile 卡片 + 圆角边框（border 是 shape 的参数）
        // ═════════════════════════════════════════════════════════════
        for (int r = 0; r < rows; ++r)
        {
            for (int c = 0; c < cols; ++c)
            {
                const auto t = tile_rect(c, r);
                b.addInstance(ui_dao::sdf_shape(t, view, 14.0F, card,
                                                glm::vec4{0.0F, 0.0F, 0.0F, 0.30F},
                                                3.0F, 8.0F));
                b.addInstance(ui_dao::shape_border(t, view, 14.0F, line, 1.0F));
            }
        }

        // ═════════════════════════════════════════════════════════════
        // [M1] shape：胶囊 + 圆角方 + 竖胶囊
        // ═════════════════════════════════════════════════════════════
        {
            const auto in = inset(tile_rect(0, 0), 14.0F);
            const float pillH = std::max(in.h * 0.18F, 22.0F);
            const float sq = std::min(in.w, in.h) * 0.34F;
            const float capW = std::max(in.w * 0.20F, 18.0F);
            const float capH = in.h * 0.38F;
            b.addInstance(ui_dao::sdf_shape(
                {in.x + in.w * 0.06F, in.y + in.h * 0.16F, in.w * 0.88F, pillH}, view,
                pillH * 0.5F, accent));
            b.addInstance(ui_dao::sdf_shape(
                {in.x + in.w * 0.10F, in.y + in.h * 0.50F, sq, sq}, view, sq * 0.22F,
                orange));
            b.addInstance(ui_dao::sdf_shape(
                {in.x + in.w * 0.68F, in.y + in.h * 0.46F, capW, capH}, view,
                capW * 0.5F, green));
        }

        // ═════════════════════════════════════════════════════════════
        // [M2] path_fill：五角星（10 点多边形，偶数环绕）
        // ═════════════════════════════════════════════════════════════
        {
            const auto in = inset(tile_rect(1, 0), 14.0F);
            const ui_dao::rect_px box{in.x + 16.0F, in.y + in.h * 0.22F,
                                      std::max(in.w - 32.0F, 8.0F),
                                      in.h * 0.62F};
            const float cx = box.x + box.w * 0.5F;
            const float cy = box.y + box.h * 0.5F;
            const float R = std::min(box.w, box.h) * 0.46F;
            std::array<glm::vec2, 10> star{};
            for (int i = 0; i < 10; ++i)
            {
                const float a = -1.5707963F + float(i) * 0.62831853F; // -π/2 + i·2π/10
                const float rr = (i % 2 == 0) ? R : R * 0.42F;
                const float px = cx + std::cos(a) * rr;
                const float py = cy + std::sin(a) * rr;
                star[static_cast<std::size_t>(i)] = {(px - in.x) / in.w,
                                                     (py - in.y) / in.h};
            }
            b.addInstance(ui_dao::path_fill(in, view, star, accent, pink));
        }

        // ═════════════════════════════════════════════════════════════
        // [M3] path_stroke：正弦折线（16 点，圆头圆角，5px）
        // ═════════════════════════════════════════════════════════════
        {
            const auto in = inset(tile_rect(2, 0), 14.0F);
            std::array<glm::vec2, 16> wave{};
            for (int i = 0; i < 16; ++i)
            {
                const float t = float(i) / 15.0F;
                const float px = in.x + 14.0F + t * (in.w - 28.0F);
                const float py = in.y + in.h * 0.52F +
                                 std::sin(t * 6.2831853F * 1.6F) * (in.h * 0.17F);
                wave[static_cast<std::size_t>(i)] = {(px - in.x) / in.w,
                                                     (py - in.y) / in.h};
            }
            b.addInstance(ui_dao::path_stroke(in, view, wave, 5.0F, cyan, green));
        }

        // ═════════════════════════════════════════════════════════════
        // [M4] texture：雕像照片（bindless 槽位 0）
        // ═════════════════════════════════════════════════════════════
        {
            const auto in = inset(tile_rect(3, 0), 14.0F);
            b.addInstance(ui_dao::texture(
                {in.x, in.y + in.h * 0.24F, in.w, in.h * 0.70F}, view, kTexPhoto,
                kSamplerLinear, glm::vec4{1.0F}, UvTransform{}, 10.0F));
        }

        // ═════════════════════════════════════════════════════════════
        // [M4b] glyph：texture + metadata（不是独立程序）
        //       整形结果由 ui_dao::glyph_from_shaped 直接编码成 TexQuad
        // ═════════════════════════════════════════════════════════════
        // （字形实例统一在 build_text() 里生成，最后一次性提交）

        // ═════════════════════════════════════════════════════════════
        // [M5] blur：照片高斯模糊（半径 36 纹素，5 档核）
        // ═════════════════════════════════════════════════════════════
        {
            const auto in = inset(tile_rect(1, 1), 14.0F);
            const float stripW = std::max(in.w * 0.20F, 18.0F);
            const ui_dao::rect_px box{in.x, in.y + in.h * 0.24F, in.w, in.h * 0.70F};
            // 左：清晰照片窄条；右：模糊照片 —— 同一张图直接对比
            b.addInstance(ui_dao::texture({box.x, box.y, stripW, box.h}, view, kTexPhoto,
                                          kSamplerLinear, glm::vec4{1.0F},
                                          UvTransform{}, 8.0F));
            b.addInstance(ui_dao::blur(
                {box.x + stripW + 6.0F, box.y, std::max(box.w - stripW - 6.0F, 8.0F),
                 box.h},
                view, kTexPhoto, kSamplerLinear, 36.0F, 5U, glm::vec4{1.0F},
                UvTransform{}, 10.0F));
        }

        // ═════════════════════════════════════════════════════════════
        // [M6] color_matrix：照片 + sepia 4x4 矩阵
        // ═════════════════════════════════════════════════════════════
        {
            const auto in = inset(tile_rect(2, 1), 14.0F);
            glm::mat4 sepia{1.0F};
            sepia[0] = glm::vec4{0.393F, 0.349F, 0.272F, 0.0F};
            sepia[1] = glm::vec4{0.769F, 0.686F, 0.534F, 0.0F};
            sepia[2] = glm::vec4{0.189F, 0.168F, 0.131F, 0.0F};
            sepia[3] = glm::vec4{0.0F, 0.0F, 0.0F, 1.0F};
            b.addInstance(ui_dao::color_matrix(
                {in.x, in.y + in.h * 0.24F, in.w, in.h * 0.70F}, view, kTexPhoto,
                kSamplerLinear, sepia, glm::vec4{0.02F, 0.01F, 0.0F, 0.0F},
                glm::vec4{1.0F}, UvTransform{}, 10.0F));
        }

        // ═════════════════════════════════════════════════════════════
        // [M7] noise：确定性 fbm（seed 固定 → 同输入同像素）
        // ═════════════════════════════════════════════════════════════
        {
            const auto in = inset(tile_rect(3, 1), 14.0F);
            b.addInstance(ui_dao::noise(
                {in.x, in.y + in.h * 0.24F, in.w, in.h * 0.70F}, view,
                glm::vec4{0.02F, 0.05F, 0.12F, 1.0F},
                glm::vec4{0.20F, 0.78F, 0.88F, 1.0F}, 5.0F, 7.0F, 5U, 10.0F));
        }

        // ═════════════════════════════════════════════════════════════
        // 文字：标题 + 8 个 tile 标签 + glyph tile 正文
        //   缓存按 (W,H)；同输入 → 同一批 TexQuad 实例
        // ═════════════════════════════════════════════════════════════
        if (text_w_ != W || text_h_ != H)
        {
            build_text(ctx.fontSelect, W, H, tile_rect, text, dim);
            text_w_ = W;
            text_h_ = H;
            std::println("[dao] viewport={}x{} screen={}x{} | 7 mechanisms: shape path_fill "
                         "path_stroke texture blur color_matrix noise | + glyph metadata "
                         "(texture) | border=shape param | glyphs={}",
                         W, H, screen->size().width, screen->size().height, text_.size());
        }
        if (!text_.empty())
            b.addInstances(std::span<const shader_data::TexQuad>{text_});
    }

  private:
    std::vector<shader_data::TexQuad> text_;
    float text_w_ = -1.0F;
    float text_h_ = -1.0F;

    // 整形一段文字 → ui_dao::glyph_from_shaped → TexQuad 实例行。
    template <class FontSelectT, class TileRectFn>
    void build_text(FontSelectT &fontSelect, float W, float H,
                    const TileRectFn &tile_rect, const glm::vec4 &title_color,
                    const glm::vec4 &label_color)
    {
        const ui_dao::view view{W, H};
        text_.clear();

        emit_run(text_, fontSelect, "7 MECHANISMS + GLYPH META", view, 22.0F, 26.0F,
                 48.0F, title_color);
        emit_run(text_, fontSelect, "glyph = texture + metadata; border = shape param",
                 view, 12.0F, 27.0F, 70.0F, label_color);

        static constexpr std::array<const char *, 8> names = {
            "M1 SDF SHAPE", "M2 PATH FILL", "M3 PATH STROKE", "M4 TEXTURE",
            "M4b GLYPH META", "M5 BLUR", "M6 COLOR MATRIX", "M7 NOISE"};

        for (int i = 0; i < 8; ++i)
        {
            const auto t = tile_rect(i % 4, i / 4);
            emit_run(text_, fontSelect, names[static_cast<std::size_t>(i)], view, 12.0F,
                     t.x + 14.0F, t.y + 25.0F, label_color);
        }

        // glyph 图元的正文：在 tile(0,1) 里居中
        const auto g = tile_rect(0, 1);
        emit_run(text_, fontSelect, "世界 MSDF", view, 24.0F, g.x + g.w * 0.5F,
                 g.y + g.h * 0.5F + 10.0F, title_color, true);
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
                {
                    out.push_back(ui_dao::glyph_from_shaped(g, view, px, cursor,
                                                            baseline, color));
                }
                cursor += static_cast<float>(g.advance_x) * px;
            }
        }
    }
};

} // namespace ui_new
