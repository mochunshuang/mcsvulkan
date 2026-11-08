#pragma once
// ============================================================================
// test_intent7_ability_test.hpp
//
// Widget-level migration of test_ui_ability2 into intent7's [pass ... pass]
// framework. It demonstrates the same capability families, not identical
// pixels:
//
//   shape: linear / radial / conic / per-corner / ring / arc
//   path:  triangle+AA / star / dash / rotate / skew
//   texture: nine-slice / masked / mipmap-lod
//   effects: inner shadow / blur / color matrix / noise
//   text: MSDF glyphs in a final overlay pass
//   pass features: clip (per-segment scissor), layer (two passes),
//                  pipeline switch, alpha blending
//
// All drawing goes through CommandBuilder -> emit -> FrameBuilder -> OpStream
// -> VkEmitter -> explicit vkCmd*.
// ============================================================================

#include "test_intent7_pass_abi.hpp"
#include "test_intent7_ui_dao.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <print>
#include <span>
#include <vector>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace ui_new
{

struct ability_test_scene final : RenderObject
{
    static constexpr std::uint32_t kTexPhoto = 0;
    static constexpr std::uint32_t kTexGradient = 2;
    static constexpr std::uint32_t kTexChess = 3;
    static constexpr std::uint32_t kSamplerLinear = 0;

    ability_test_scene(VkPipeline opaque, VkPipeline transparent,
                       VkPipeline additive, VkPipeline multiply,
                       VkPipelineLayout layout, const mesh_data &mesh,
                       VkExtent2D extent, VkSampleCountFlagBits samples) noexcept
        : opaque_{opaque}, transparent_{transparent}, additive_{additive},
          multiply_{multiply}, layout_{layout}, mesh_{mesh}, extent_{extent},
          samples_{samples}
    {
    }

    void render(ScreenWidget *screen, Widget *, render_context &ctx) override
    {
        auto *manager = ctx.passManager;
        assert(manager != nullptr);

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

        const ui_dao::view view{W, H};
        const float s = std::max(W / 800.0F, 1.0F);
        const float margin = 14.0F * s;
        const float top = 78.0F * s;
        const float bottom = 12.0F * s;
        const float gap = 6.0F * s;
        constexpr int cols = 5;
        constexpr int rows = 4;
        const float tileW =
            (W - 2.0F * margin - float(cols - 1) * gap) / float(cols);
        const float tileH =
            (H - top - bottom - float(rows - 1) * gap) / float(rows);
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
        const glm::vec4 text{0.90F, 0.93F, 0.98F, 1.0F};

        const PushData push{.cameraIndex = 1,
                            .renderVersion = screen->renderVersion()};
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
            .pipeline = opaque_, .layout = layout_, .set = ctx.mainSet,
            .viewport = vp,      .scissor = sc,       .pushData = push,
            .mesh = mesh_,
        };

        assert(ctx.mainPassDesc != nullptr);
        auto contentDesc = ctx.mainPassDesc->withLoad(VK_ATTACHMENT_LOAD_OP_LOAD)
                               .withStore(VK_ATTACHMENT_STORE_OP_STORE);

        manager->reset();

        // ---- pass 0: base layer -----------------------------------------
        auto &basePass = manager->beginPass(contentDesc, base);
        upload_global_mesh(basePass, ctx);
        auto &baseBuilder = basePass.frame.builder;
        baseBuilder.addInstance(ui_dao::sdf_shape(
            {8.0F * s, 8.0F * s, W - 16.0F * s, H - 16.0F * s}, view, 16.0F * s, bg,
            glm::vec4{0.0F, 0.0F, 0.0F, 0.5F}, 4.0F * s, 12.0F * s));
        baseBuilder.addInstance(ui_dao::shape_border(
            {8.0F * s, 8.0F * s, W - 16.0F * s, H - 16.0F * s}, view, 16.0F * s,
            line, 1.0F * s));
        for (int i = 0; i < 18; ++i)
        {
            const auto t = tile_at(i % cols, i / cols);
            baseBuilder.addInstance(ui_dao::sdf_shape(
                t, view, 10.0F * s, card, glm::vec4{0.0F, 0.0F, 0.0F, 0.28F},
                2.0F * s, 6.0F * s));
            baseBuilder.addInstance(
                ui_dao::shape_border(t, view, 10.0F * s, line, 1.0F * s));
        }
        baseBuilder.addInstance(ui_dao::sdf_shape(
            wide, view, 12.0F * s, card, glm::vec4{0.0F, 0.0F, 0.0F, 0.30F},
            2.0F * s, 8.0F * s));
        baseBuilder.addInstance(
            ui_dao::shape_border(wide, view, 12.0F * s, line, 1.0F * s));
        manager->endPass();

        // ---- pass 1: path/shape layer -----------------------------------
        auto transparentBase = base;
        transparentBase.pipeline = transparent_;
        auto &shapePass = manager->beginPass(contentDesc, transparentBase);
        upload_global_mesh(shapePass, ctx);
        auto &shapeBuilder = shapePass.frame.builder;
        shapeBuilder.setPipeline(transparent_, layout_);
        for (int i = 0; i < 11; ++i)
        {
            const auto t = tile_at(i % cols, i / cols);
            shapeBuilder.setScissor(VkRect2D{
                .offset = {static_cast<std::int32_t>(t.x),
                           static_cast<std::int32_t>(t.y)},
                .extent = {static_cast<std::uint32_t>(t.w),
                           static_cast<std::uint32_t>(t.h)}});
            effect(i, shapeBuilder, view, content_of(t), s, accent, cyan, orange,
                   pink, green, yellow, card_alt, dim);
        }
        shapeBuilder.setScissor(sc);
        manager->endPass();

        // ---- pass 2: texture/effect layer (recorded as secondary) -------
        auto &effectPass = manager->beginPass(contentDesc, transparentBase);
        upload_global_mesh(effectPass, ctx);
        auto &effectBuilder = effectPass.frame.builder;
        effectBuilder.setPipeline(transparent_, layout_);
        for (int i = 11; i < 18; ++i)
        {
            const auto t = tile_at(i % cols, i / cols);
            effectBuilder.setScissor(VkRect2D{
                .offset = {static_cast<std::int32_t>(t.x),
                           static_cast<std::int32_t>(t.y)},
                .extent = {static_cast<std::uint32_t>(t.w),
                           static_cast<std::uint32_t>(t.h)}});
            effect(i, effectBuilder, view, content_of(t), s, accent, cyan, orange,
                   pink, green, yellow, card_alt, dim);
        }
        effectBuilder.setScissor(
            VkRect2D{.offset = {static_cast<std::int32_t>(wide.x),
                                static_cast<std::int32_t>(wide.y)},
                     .extent = {static_cast<std::uint32_t>(wide.w),
                                static_cast<std::uint32_t>(wide.h)}});
        composed_tile(effectBuilder, wide, view, s, accent, green, orange, dim);
        manager->endPass();

        // ---- pass 3: text layer -----------------------------------------
        build_text(ctx, view, s, tile_at, content_of, wide, text, dim, accent,
                   pink, orange, green);
        if (!text_.empty())
        {
            auto overlayDesc =
                contentDesc.withStore(VK_ATTACHMENT_STORE_OP_DONT_CARE);
            auto &overlay = manager->beginPass(overlayDesc, transparentBase);
            upload_global_mesh(overlay, ctx);
            overlay.frame.builder.addInstances(
                std::span<const shader_data::TexQuad>{text_});
            manager->endPass();
        }

        std::println("[ability-test] passes={} glyphs={}", manager->passes().size(),
                     text_.size());
    }

  private:
    VkPipeline opaque_{};
    VkPipeline transparent_{};
    VkPipeline additive_{};
    VkPipeline multiply_{};
    VkPipelineLayout layout_{};
    mesh_data mesh_{};
    VkExtent2D extent_{};
    VkSampleCountFlagBits samples_{VK_SAMPLE_COUNT_4_BIT};
    std::vector<shader_data::TexQuad> text_{};

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

    static void arc_points(float start, float end, int count,
                           std::array<glm::vec2, 32> &out)
    {
        count = std::clamp(count, 2, 32);
        for (int i = 0; i < count; ++i)
        {
            const float t = static_cast<float>(i) / float(count - 1);
            const float a = start + (end - start) * t;
            out[static_cast<std::size_t>(i)] = {0.5F + 0.42F * std::cos(a),
                                                0.5F + 0.42F * std::sin(a)};
        }
    }

    static void star_points(std::array<glm::vec2, 10> &out)
    {
        for (int i = 0; i < 10; ++i)
        {
            const float a = -glm::half_pi<float>() + float(i) * glm::pi<float>() / 5.0F;
            const float r = (i & 1) == 0 ? 0.46F : 0.20F;
            out[static_cast<std::size_t>(i)] = {0.5F + r * std::cos(a),
                                                0.5F + r * std::sin(a)};
        }
    }

    void effect(int i, shader_data::CommandBuilder &b, const ui_dao::view &v,
                const ui_dao::rect_px &c, float s, const glm::vec4 &accent,
                const glm::vec4 &cyan, const glm::vec4 &orange,
                const glm::vec4 &pink, const glm::vec4 &green,
                const glm::vec4 &yellow, const glm::vec4 &card_alt,
                const glm::vec4 &dim)
    {
        switch (i)
        {
        case 0: {
            const auto top = ui_dao::rect_px{c.x, c.y, c.w, c.h * 0.5F};
            const auto bot = ui_dao::rect_px{c.x, c.y + c.h * 0.5F, c.w, c.h * 0.5F};
            b.addInstance(ui_dao::sdf_linear(top, v, 6.0F * s, accent, pink, true));
            b.addInstance(ui_dao::sdf_linear(bot, v, 6.0F * s, accent, pink, false));
            break;
        }
        case 1:
            b.addInstance(ui_dao::sdf_radial(
                c, v, 8.0F * s, yellow, glm::vec4{0.12F, 0.10F, 0.18F, 1.0F}));
            break;
        case 2:
            b.addInstance(ui_dao::sdf_conic(c, v, 8.0F * s, cyan, pink, 3.0F));
            break;
        case 3:
            b.addInstance(ui_dao::shape_per_corner(
                c, v, glm::vec4{24.0F * s, 4.0F * s, 24.0F * s, 4.0F * s},
                accent));
            break;
        case 4:
            b.addInstance(ui_dao::shape_border(c, v, 8.0F * s, cyan, 6.0F * s));
            b.addInstance(ui_dao::shape_border(
                {c.x + 8.0F * s, c.y + 8.0F * s, c.w - 16.0F * s, c.h - 16.0F * s},
                v, 6.0F * s, yellow, 3.0F * s));
            break;
        case 5: {
            std::array<glm::vec2, 32> pts{};
            arc_points(-glm::half_pi<float>(), glm::half_pi<float>(), 20, pts);
            b.addInstance(ui_dao::path_stroke(c, v,
                                              std::span{pts.data(), 20}, 6.0F * s,
                                              cyan, {}, 1));
            break;
        }
        case 6: {
            const std::array<glm::vec2, 3> tri{{{0.5F, 0.10F},
                                                {0.90F, 0.86F},
                                                {0.10F, 0.86F}}};
            const auto left = ui_dao::rect_px{c.x, c.y, c.w * 0.48F, c.h};
            const auto right = ui_dao::rect_px{c.x + c.w * 0.52F, c.y,
                                               c.w * 0.48F, c.h};
            b.addInstance(ui_dao::path_fill(left, v, tri, accent, cyan, 0, 2.5F));
            b.addInstance(ui_dao::path_fill(right, v, tri, accent, cyan, 0, 0.0F));
            break;
        }
        case 7: {
            std::array<glm::vec2, 10> pts{};
            star_points(pts);
            b.addInstance(ui_dao::path_fill(c, v, pts, accent, pink, 0, 1.0F));
            break;
        }
        case 8: {
            for (int k = 0; k < 4; ++k)
            {
                const float x0 = 0.06F + float(k) * 0.24F;
                const std::array<glm::vec2, 2> seg{{{x0, 0.5F},
                                                    {x0 + 0.12F, 0.5F}}};
                b.addInstance(ui_dao::path_stroke(c, v, seg, 7.0F * s, yellow, {},
                                                  1));
            }
            break;
        }
        case 9: {
            auto shape = ui_dao::sdf_shape(c, v, 8.0F * s, accent);
            shape.model = shape.model * glm::rotate(glm::mat4(1.0F),
                                                    glm::radians(18.0F),
                                                    glm::vec3(0.0F, 0.0F, 1.0F));
            b.addInstance(shape);
            break;
        }
        case 10: {
            auto shape = ui_dao::sdf_shape(c, v, 8.0F * s, pink);
            glm::mat4 shear(1.0F);
            shear[1][0] = 0.35F;
            shape.model = shape.model * shear;
            b.addInstance(shape);
            break;
        }
        case 11: {
            // Nine-slice texture: 3x3 sub-rects with matching UV sub-rects.
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 3; ++col)
                {
                    const ui_dao::rect_px q{c.x + float(col) * c.w / 3.0F,
                                            c.y + float(row) * c.h / 3.0F,
                                            c.w / 3.0F, c.h / 3.0F};
                    UvTransform uv{};
                    uv.scale = {1.0F / 3.0F, 1.0F / 3.0F};
                    uv.offset = {float(col) / 3.0F, float(row) / 3.0F};
                    b.addInstance(ui_dao::texture(q, v, kTexGradient,
                                                  kSamplerLinear, glm::vec4(1.0F),
                                                  uv, 2.0F * s));
                }
            break;
        }
        case 12: {
            const auto left = ui_dao::rect_px{c.x, c.y, c.w * 0.48F, c.h};
            const auto right = ui_dao::rect_px{c.x + c.w * 0.52F, c.y,
                                               c.w * 0.48F, c.h};
            // Nested clip: the outer tile scissor intersects this inner rect.
            b.pushScissor(VkRect2D{
                .offset = {static_cast<std::int32_t>(c.x + 6.0F * s),
                           static_cast<std::int32_t>(c.y + 6.0F * s)},
                .extent = {static_cast<std::uint32_t>(c.w * 0.44F),
                           static_cast<std::uint32_t>(c.h - 12.0F * s)}});
            b.addInstance(ui_dao::shape_alpha_mask(left, v, 4.0F * s, accent,
                                                   10.0F * s, true));
            b.popScissor();
            b.addInstance(ui_dao::texture_masked(
                right, v, kTexPhoto, kSamplerLinear, kTexChess, kSamplerLinear,
                UvTransform{}, glm::vec4(1.0F), {}, 4.0F * s));
            break;
        }
        case 13:
            b.addInstance(ui_dao::sdf_shape(c, v, 8.0F * s, card_alt));
            b.addInstance(ui_dao::shape_inner_shadow(
                c, v, 8.0F * s, glm::vec4{0.0F, 0.0F, 0.0F, 0.55F}, 5.0F * s,
                10.0F * s));
            break;
        case 14: {
            const auto top = ui_dao::rect_px{c.x, c.y, c.w, c.h * 0.48F};
            const auto bot = ui_dao::rect_px{c.x, c.y + c.h * 0.52F, c.w,
                                             c.h * 0.48F};
            b.addInstance(ui_dao::blur(top, v, kTexPhoto, kSamplerLinear,
                                       24.0F * s, 5, glm::vec4(1.0F), {}, 4.0F * s));
            for (int k = 0; k < 4; ++k)
            {
                const float h = bot.h / 4.0F;
                b.addInstance(ui_dao::texture_lod(
                    {bot.x, bot.y + float(k) * h, bot.w, h}, v, kTexPhoto,
                    kSamplerLinear, float(k) * 1.5F, glm::vec4(1.0F), {}, 0.0F));
            }
            break;
        }
        case 15: {
            glm::mat4 sepia(1.0F);
            sepia[0] = glm::vec4{0.393F, 0.349F, 0.272F, 0.0F};
            sepia[1] = glm::vec4{0.769F, 0.686F, 0.534F, 0.0F};
            sepia[2] = glm::vec4{0.189F, 0.168F, 0.131F, 0.0F};
            sepia[3] = glm::vec4{0.0F, 0.0F, 0.0F, 1.0F};
            b.addInstance(ui_dao::color_matrix(
                c, v, kTexPhoto, kSamplerLinear, sepia,
                glm::vec4{0.02F, 0.01F, 0.0F, 0.0F}, glm::vec4(1.0F), {},
                4.0F * s));
            break;
        }
        case 16:
            b.addInstance(ui_dao::noise(c, v, glm::vec4{0.02F, 0.05F, 0.12F, 1.0F},
                                        glm::vec4{0.20F, 0.78F, 0.88F, 1.0F}, 5.0F,
                                        7.0F, 5U, 4.0F * s));
            break;
        case 17:
            // Text is emitted in the overlay pass.
            b.addInstance(ui_dao::sdf_shape(c, v, 8.0F * s, card_alt));
            break;
        default:
            break;
        }
    }

    void composed_tile(shader_data::CommandBuilder &b,
                       const ui_dao::rect_px &wide, const ui_dao::view &v, float s,
                       const glm::vec4 &accent, const glm::vec4 &green,
                       const glm::vec4 &orange, const glm::vec4 &dim)
    {
        b.addInstance(ui_dao::sdf_linear(
            {wide.x + 8.0F * s, wide.y + 8.0F * s, 120.0F * s,
             wide.h - 16.0F * s},
            v, 10.0F * s, accent, green, true));
        b.addInstance(ui_dao::texture(
            {wide.x + 140.0F * s, wide.y + 8.0F * s, 96.0F * s,
             wide.h - 16.0F * s},
            v, kTexChess, kSamplerLinear, glm::vec4(1.0F), {}, 8.0F * s));
        b.addInstance(ui_dao::shape_border(
            {wide.x + 248.0F * s, wide.y + 8.0F * s, 96.0F * s,
             wide.h - 16.0F * s},
            v, 8.0F * s, orange, 3.0F * s));
        b.addInstance(ui_dao::noise(
            {wide.x + 360.0F * s, wide.y + 8.0F * s,
             std::max(wide.w - 368.0F * s, 16.0F), wide.h - 16.0F * s},
            v, glm::vec4{0.02F, 0.05F, 0.12F, 1.0F},
            glm::vec4{0.20F, 0.78F, 0.88F, 1.0F}, 5.0F, 3.0F, 4U, 6.0F * s));

        // Blend-mode demonstration: additive glow + multiply tint.
        b.setPipeline(additive_, layout_);
        b.addInstance(ui_dao::sdf_shape(
            {wide.x + 470.0F * s, wide.y + 14.0F * s, 70.0F * s, wide.h - 28.0F * s},
            v, 35.0F * s, glm::vec4{accent.r, accent.g, accent.b, 0.35F}));
        b.setPipeline(multiply_, layout_);
        b.addInstance(ui_dao::sdf_shape(
            {wide.x + 470.0F * s, wide.y + 14.0F * s, 70.0F * s, wide.h - 28.0F * s},
            v, 35.0F * s, glm::vec4{0.55F, 0.75F, 1.0F, 1.0F}));
        b.setPipeline(transparent_, layout_);
        (void)dim;
    }

    template <class TileAt, class ContentOf>
    void build_text(render_context &ctx, const ui_dao::view &v, float s,
                    const TileAt &tile_at, const ContentOf &content_of,
                    const ui_dao::rect_px &wide, const glm::vec4 &text,
                    const glm::vec4 &dim, const glm::vec4 &accent,
                    const glm::vec4 &pink, const glm::vec4 &orange,
                    const glm::vec4 &green)
    {
        text_.clear();
        emit_text(ctx, "ABILITY2 -> INTENT7 FRAMEWORK", v, 20.0F * s, 16.0F * s,
                  30.0F * s, text);
        emit_text(ctx, "7 programs + clip + layer + blend + mipmap + text",
                  v, 10.0F * s, 16.0F * s, 48.0F * s, dim);
        emit_text(ctx, "4 passes | effects = secondary | transfer + compute",
                  v, 8.0F * s, 16.0F * s, 63.0F * s, dim);

        static constexpr std::array<const char *, 18> names = {
            "LINEAR/SRGB", "RADIAL",   "CONIC",    "PER-CORNER", "RING",
            "ARC",        "TRIANGLE", "STAR",     "DASH",       "ROTATE",
            "SKEW",       "NINE-SLICE", "MASKED", "INNER-SHADOW", "BLUR/MIP",
            "SEPIA",      "NOISE",    "TEXT"};
        for (int i = 0; i < 18; ++i)
        {
            const auto t = tile_at(i % 5, i / 5);
            emit_text(ctx, names[static_cast<std::size_t>(i)], v, 9.0F * s,
                      t.x + 8.0F * s, t.y + 14.0F * s, dim);
        }

        emit_text(ctx, "COMPOSED UI", v, 10.0F * s, wide.x + 14.0F * s,
                  wide.y + 14.0F * s, text);
        emit_text(ctx, "texture", v, 8.0F * s, wide.x + 150.0F * s,
                  wide.y + 28.0F * s, accent);
        emit_text(ctx, "border", v, 8.0F * s, wide.x + 258.0F * s,
                  wide.y + 28.0F * s, orange);
        emit_text(ctx, "noise", v, 8.0F * s, wide.x + 370.0F * s,
                  wide.y + 28.0F * s, green);
        emit_text(ctx, "MSDF TEXT", v, 9.0F * s, wide.x + 14.0F * s,
                  wide.y + 44.0F * s, pink);
        emit_text(ctx, "TARGET: 2 MRT / 4x MSAA / resolve", v, 7.5F * s,
                  wide.x + 14.0F * s, wide.y + 60.0F * s, dim);
        emit_text(ctx, "GEOM: indexed + instanced + indirect", v, 7.5F * s,
                  wide.x + 14.0F * s, wide.y + 72.0F * s, dim);
        emit_text(ctx, "TEX: linear/nearest + mip/lod", v, 7.5F * s,
                  wide.x + 14.0F * s, wide.y + 84.0F * s, dim);
        emit_text(ctx, "PASS: 4 / secondary / nested clip", v, 7.5F * s,
                  wide.x + 14.0F * s, wide.y + 96.0F * s, dim);
        emit_text(ctx, "SYNC: barrier2 | COMPUTE: dispatch", v, 7.5F * s,
                  wide.x + 14.0F * s, wide.y + 108.0F * s, dim);
        (void)content_of;
    }

    void emit_text(render_context &ctx, const char *s, const ui_dao::view &v,
                   float px, float x, float baseline, const glm::vec4 &color)
    {
        auto result = run_text_pipeline(ctx.fontSelect, s, "zh-CN");
        float cursor = x;
        for (const auto &run : result.shape_result)
        {
            for (const auto &g : run)
            {
                if (!(g.plane_bounds == decltype(g.plane_bounds){}))
                    text_.push_back(
                        ui_dao::glyph_from_shaped(g, v, px, cursor, baseline, color));
                cursor += static_cast<float>(g.advance_x) * px;
            }
        }
    }
};

} // namespace ui_new
