#pragma once
// ============================================================================
// test_intent7_ability_test.hpp
//
// Widget-level UI ability matrix for intent7's [pass ... pass] framework.
//
// Design rule for this scene:
//   * Every tile is a FIXED number of framebuffer pixels (170 x 126).
//   * The layout is not a fraction of the window: growing the window reveals
//     more empty space, it never scales the widgets.  Pixel -> NDC conversion
//     is done by ui_dao::view{W,H} with the live framebuffer extent.
//   * 36 individual feature cases + one composed panel, each with a visible
//     result.  Every capability family has its own tile:
//
//       shape:  solid/shadow, linear, radial, conic, per-corner, border/ring,
//               ellipse, outer shadow, inner shadow, alpha mask, sRGB/linear
//       path:   triangle+AA, star/even-odd, arc, dash, rotate, skew
//       texture: nine-slice, masked, explicit mip LOD, blur
//       effects: sepia, gray/invert, noise, MSAA resolve, blend modes
//       text:   MSDF, RTL (Arabic), CJK, color emoji, wrapped, mixed bidi
//       pass:   nested clip, group opacity (offscreen), backdrop blur,
//               stencil clip, offscreen composite (secondary command buffer)
//       target: 2 MRT (color + pick), 4x MSAA + resolve, depth/stencil
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
#include <format>
#include <print>
#include <span>
#include <string>
#include <string_view>
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

    // Fixed-pixel design grid.  These are framebuffer pixels; they never
    // depend on the window size.
    static constexpr int kCols = 7;
    static constexpr int kRows = 6;
    static constexpr float kPad = 16.0F;
    static constexpr float kTop = 86.0F;
    static constexpr float kGap = 8.0F;
    static constexpr float kTileW = 170.0F;
    static constexpr float kTileH = 126.0F;
    static constexpr float kGridW = kCols * kTileW + (kCols - 1) * kGap;
    static constexpr float kGridH = kRows * kTileH + (kRows - 1) * kGap;
    static constexpr int kTileCount = 36;

    ability_test_scene(VkPipeline opaque, VkPipeline transparent,
                       VkPipeline additive, VkPipeline multiply,
                       VkPipelineLayout layout, const mesh_data &mesh,
                       VkExtent2D extent, VkSampleCountFlagBits samples,
                       VkImageView offscreenMsaa, VkImageView offscreenResolve,
                       VkImage offscreenMsaaImage, VkImage offscreenResolveImage,
                       std::uint32_t offscreenTexture, VkPipeline stencilMask,
                       VkPipeline stencilContent, VkImageView stencilView,
                       VkImage stencilImage) noexcept
        : opaque_{opaque}, transparent_{transparent}, additive_{additive},
          multiply_{multiply}, layout_{layout}, mesh_{mesh}, extent_{extent},
          samples_{samples}, offscreenMsaa_{offscreenMsaa},
          offscreenResolve_{offscreenResolve},
          offscreenMsaaImage_{offscreenMsaaImage},
          offscreenResolveImage_{offscreenResolveImage},
          offscreenTexture_{offscreenTexture}, stencilMask_{stencilMask},
          stencilContent_{stencilContent}, stencilView_{stencilView},
          stencilImage_{stencilImage}
    {
    }

    // Swapchain resize/fullscreen: the scene captured image-view handles at
    // construction time, so the owner must hand the rebuilt handles back.
    void updateResources(VkImageView offscreenMsaa, VkImageView offscreenResolve,
                         VkImage offscreenMsaaImage, VkImage offscreenResolveImage,
                         std::uint32_t offscreenTexture, VkImageView stencilView,
                         VkImage stencilImage) noexcept
    {
        offscreenMsaa_ = offscreenMsaa;
        offscreenResolve_ = offscreenResolve;
        offscreenMsaaImage_ = offscreenMsaaImage;
        offscreenResolveImage_ = offscreenResolveImage;
        offscreenTexture_ = offscreenTexture;
        stencilView_ = stencilView;
        stencilImage_ = stencilImage;
        // Cached glyph quads bake the pixel -> NDC transform, so they are
        // valid only for one framebuffer extent.
        textW_ = -1.0F;
        textH_ = -1.0F;
        text_.clear();
    }

    void render(ScreenWidget *screen, Widget *, render_context &ctx) override
    {
        auto *manager = ctx.passManager;
        assert(manager != nullptr);
        manager->reset();

        float W = static_cast<float>(screen->size().width);
        float H = static_cast<float>(screen->size().height);
        if (ctx.framebufferExtent.width > 0 && ctx.framebufferExtent.height > 0)
        {
            W = static_cast<float>(ctx.framebufferExtent.width);
            H = static_cast<float>(ctx.framebufferExtent.height);
        }
        else if (ctx.mainPassDesc != nullptr &&
                 ctx.mainPassDesc->renderArea.extent.width > 0 &&
                 ctx.mainPassDesc->renderArea.extent.height > 0)
        {
            W = static_cast<float>(ctx.mainPassDesc->renderArea.extent.width);
            H = static_cast<float>(ctx.mainPassDesc->renderArea.extent.height);
        }
        else if (ctx.drawRecorder.hasViewport())
        {
            const auto vp = ctx.drawRecorder.currentViewport();
            if (vp.width > 0.0F && vp.height > 0.0F)
            {
                W = vp.width;
                H = vp.height;
            }
        }
        if (W < 320.0F || H < 240.0F)
            return;

        const ui_dao::view view{W, H};
        // Fixed-pixel grid, centered when the window is wider than the grid.
        const float x0 = std::max(kPad, (W - kGridW) * 0.5F);
        const auto tile_at = [x0](int c, int r) {
            return ui_dao::rect_px{x0 + float(c) * (kTileW + kGap),
                                   kTop + float(r) * (kTileH + kGap), kTileW,
                                   kTileH};
        };
        const auto content_of = [](const ui_dao::rect_px &t) {
            return ui_dao::rect_px{t.x + 8.0F, t.y + 24.0F,
                                   std::max(t.w - 16.0F, 8.0F),
                                   std::max(t.h - 32.0F, 8.0F)};
        };
        // Composed panel: the bottom row, columns 1..6 (six fixed cells).
        const auto wide =
            ui_dao::rect_px{x0 + (kTileW + kGap), tile_at(0, kRows - 1).y,
                            6.0F * kTileW + 5.0F * kGap, kTileH};

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

        // Text and the emoji alpha-mask handle are built once per framebuffer
        // extent, before the passes are recorded.
        if (textW_ != W || textH_ != H)
        {
            build_text(ctx, view, x0, text, dim, accent, pink, orange, green,
                       cyan, yellow);
            textW_ = W;
            textH_ = H;
        }

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

        // ---- pass 0: base layer -----------------------------------------
        auto &basePass = manager->beginPass(contentDesc, base);
        upload_global_mesh(basePass, ctx);
        auto &baseBuilder = basePass.frame.builder;
        baseBuilder.addInstance(ui_dao::sdf_shape(
            {8.0F, 8.0F, W - 16.0F, H - 16.0F}, view, 16.0F, bg,
            glm::vec4{0.0F, 0.0F, 0.0F, 0.5F}, 4.0F, 12.0F));
        baseBuilder.addInstance(ui_dao::shape_border(
            {8.0F, 8.0F, W - 16.0F, H - 16.0F}, view, 16.0F, line, 1.0F));
        // NDC corner markers: they keep a constant 8 px inset for every
        // framebuffer size, which is exactly the fixed-pixel contract.
        const std::array<glm::vec4, 4> cornerColors{accent, cyan, pink, orange};
        for (int k = 0; k < 4; ++k)
        {
            const float cx = (k % 2 == 0) ? 8.0F : W - 16.0F;
            const float cy = (k < 2) ? 8.0F : H - 16.0F;
            baseBuilder.addInstance(
                ui_dao::sdf_shape({cx, cy, 8.0F, 8.0F}, view, 2.0F,
                                  cornerColors[static_cast<std::size_t>(k)]));
        }
        for (int i = 0; i < kTileCount; ++i)
        {
            const auto t = tile_at(i % kCols, i / kCols);
            const auto entity = static_cast<std::uint32_t>(i + 1);
            // MRT pick stores the entity in object_key.entity_index; the
            // primitive id only identifies the quad's triangle.
            const bool hovered = ctx.last_pick.key.entity_index == entity;
            const auto fill = hovered
                                  ? glm::vec4{accent.r, accent.g, accent.b, 0.38F}
                                  : card;
            baseBuilder.addInstance(ui_dao::sdf_shape(
                t, view, 10.0F, fill, glm::vec4{0.0F, 0.0F, 0.0F, 0.28F}, 2.0F,
                6.0F, 1.0F, entity));
            baseBuilder.addInstance(
                ui_dao::shape_border(t, view, 10.0F, line, 1.0F));
        }
        baseBuilder.addInstance(ui_dao::sdf_shape(
            wide, view, 12.0F, card, glm::vec4{0.0F, 0.0F, 0.0F, 0.30F}, 2.0F,
            8.0F));
        baseBuilder.addInstance(
            ui_dao::shape_border(wide, view, 12.0F, line, 1.0F));
        manager->endPass();

        // ---- pass 1: shape/path layer -----------------------------------
        auto transparentBase = base;
        transparentBase.pipeline = transparent_;
        auto &shapePass = manager->beginPass(contentDesc, transparentBase);
        upload_global_mesh(shapePass, ctx);
        auto &shapeBuilder = shapePass.frame.builder;
        shapeBuilder.setPipeline(transparent_, layout_);
        for (int i = 0; i <= 16; ++i)
        {
            const auto t = tile_at(i % kCols, i / kCols);
            shapeBuilder.setScissor(VkRect2D{
                .offset = {static_cast<std::int32_t>(t.x),
                           static_cast<std::int32_t>(t.y)},
                .extent = {static_cast<std::uint32_t>(t.w),
                           static_cast<std::uint32_t>(t.h)}});
            effect(i, shapeBuilder, view, content_of(t), accent, cyan, orange,
                   pink, green, yellow, card_alt, dim);
        }
        shapeBuilder.setScissor(sc);
        manager->endPass();

        // ---- pass 2: texture/effect layer (recorded as secondary) -------
        auto &effectPass = manager->beginPass(contentDesc, transparentBase);
        upload_global_mesh(effectPass, ctx);
        auto &effectBuilder = effectPass.frame.builder;
        effectBuilder.setPipeline(transparent_, layout_);
        for (int i = 17; i <= 25; ++i)
        {
            const auto t = tile_at(i % kCols, i / kCols);
            effectBuilder.setScissor(VkRect2D{
                .offset = {static_cast<std::int32_t>(t.x),
                           static_cast<std::int32_t>(t.y)},
                .extent = {static_cast<std::uint32_t>(t.w),
                           static_cast<std::uint32_t>(t.h)}});
            effect(i, effectBuilder, view, content_of(t), accent, cyan, orange,
                   pink, green, yellow, card_alt, dim);
        }
        {
            const auto t = tile_at(32 % kCols, 32 / kCols);
            effectBuilder.setScissor(VkRect2D{
                .offset = {static_cast<std::int32_t>(t.x),
                           static_cast<std::int32_t>(t.y)},
                .extent = {static_cast<std::uint32_t>(t.w),
                           static_cast<std::uint32_t>(t.h)}});
            effect(32, effectBuilder, view, content_of(t), accent, cyan, orange,
                   pink, green, yellow, card_alt, dim);
        }
        effectBuilder.setScissor(
            VkRect2D{.offset = {static_cast<std::int32_t>(wide.x),
                                static_cast<std::int32_t>(wide.y)},
                     .extent = {static_cast<std::uint32_t>(wide.w),
                                static_cast<std::uint32_t>(wide.h)}});
        composed_tile(effectBuilder, wide, view, accent, green, orange, pink);
        effectBuilder.setPipeline(transparent_, layout_);
        effectBuilder.setScissor(sc);
        manager->endPass();

        // ---- pass 3: offscreen layer ------------------------------------
        auto offscreenDesc = *ctx.mainPassDesc;
        offscreenDesc.colorCount = 2;
        offscreenDesc.colors[0] = pass_data::color_attachment(
            offscreenMsaa_, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
            {.color = {.float32 = {0.0F, 0.0F, 0.0F, 0.0F}}},
            VK_RESOLVE_MODE_AVERAGE_BIT, offscreenResolve_,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        offscreenDesc.colors[1] = ctx.mainPassDesc->colors[1];
        offscreenDesc.colorFormats = ctx.mainPassDesc->colorFormats;
        offscreenDesc.depthFormat = ctx.mainPassDesc->depthFormat;
        offscreenDesc.stencilFormat = ctx.mainPassDesc->stencilFormat;
        offscreenDesc.samples = ctx.mainPassDesc->samples;
        offscreenDesc.viewMask = ctx.mainPassDesc->viewMask;

        auto &offscreenPass = manager->beginPass(offscreenDesc, transparentBase);
        upload_global_mesh(offscreenPass, ctx);
        auto &offscreenBuilder = offscreenPass.frame.builder;
        offscreenBuilder.setPipeline(transparent_, layout_);
        const auto groupTile = tile_at(33 % kCols, 33 / kCols);
        const auto backdropTile = tile_at(34 % kCols, 34 / kCols);
        offscreenBuilder.setScissor(VkRect2D{
            .offset = {static_cast<std::int32_t>(groupTile.x),
                       static_cast<std::int32_t>(groupTile.y)},
            .extent = {static_cast<std::uint32_t>(groupTile.w),
                       static_cast<std::uint32_t>(groupTile.h)}});
        offscreenBuilder.addInstance(ui_dao::sdf_shape(
            {groupTile.x + 16.0F, groupTile.y + 28.0F, groupTile.w * 0.52F,
             groupTile.h * 0.60F},
            view, 12.0F, accent));
        offscreenBuilder.addInstance(ui_dao::sdf_shape(
            {groupTile.x + groupTile.w * 0.38F, groupTile.y + 28.0F,
             groupTile.w * 0.52F, groupTile.h * 0.60F},
            view, 12.0F, pink));
        offscreenBuilder.setScissor(VkRect2D{
            .offset = {static_cast<std::int32_t>(backdropTile.x),
                       static_cast<std::int32_t>(backdropTile.y)},
            .extent = {static_cast<std::uint32_t>(backdropTile.w),
                       static_cast<std::uint32_t>(backdropTile.h)}});
        offscreenBuilder.addInstance(
            ui_dao::texture(backdropTile, view, kTexPhoto, kSamplerLinear,
                            glm::vec4(1.0F), {}, 8.0F));
        manager->endPass();

        offscreenPass.preBarriers.add(VkImageMemoryBarrier2{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            .srcAccessMask = 0,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .image = offscreenMsaaImage_,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .levelCount = 1,
                                 .layerCount = 1}});
        offscreenPass.preBarriers.add(VkImageMemoryBarrier2{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            .srcAccessMask = 0,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .image = offscreenResolveImage_,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .levelCount = 1,
                                 .layerCount = 1}});

        // ---- pass 4: stencil clipped layer ------------------------------
        const auto clipTile = tile_at(35 % kCols, 35 / kCols);
        auto stencilDesc = offscreenDesc;
        stencilDesc.colors[0] = pass_data::color_attachment(
            offscreenMsaa_, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {},
            VK_RESOLVE_MODE_AVERAGE_BIT, offscreenResolve_,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        stencilDesc.hasDepth = true;
        stencilDesc.depth = pass_data::depth_attachment(
            stencilView_, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
            {.depthStencil = {.depth = 1.0F, .stencil = 0}});
        stencilDesc.depthFormat = ctx.mainPassDesc->depthFormat;
        stencilDesc.setStencil(pass_data::stencil_attachment(
            stencilView_, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
            {.depthStencil = {.depth = 0.0F, .stencil = 0}}));
        stencilDesc.stencilFormat = VK_FORMAT_S8_UINT;
        stencilDesc.colorFormats[0] = ctx.mainPassDesc->colorFormats[0];

        auto stencilBase = transparentBase;
        stencilBase.pipeline = stencilMask_;
        auto &stencilPass = manager->beginPass(stencilDesc, stencilBase);
        upload_global_mesh(stencilPass, ctx);
        auto &stencilBuilder = stencilPass.frame.builder;
        stencilBuilder.setScissor(VkRect2D{
            .offset = {static_cast<std::int32_t>(clipTile.x),
                       static_cast<std::int32_t>(clipTile.y)},
            .extent = {static_cast<std::uint32_t>(clipTile.w),
                       static_cast<std::uint32_t>(clipTile.h)}});
        stencilBuilder.setPipeline(stencilMask_, layout_);
        stencilBuilder.addInstance(ui_dao::sdf_shape(
            {clipTile.x + clipTile.w * 0.16F, clipTile.y + clipTile.h * 0.22F,
             clipTile.w * 0.66F, clipTile.h * 0.58F},
            view, 16.0F, glm::vec4(1.0F)));
        stencilBuilder.setPipeline(stencilContent_, layout_);
        stencilBuilder.addInstance(ui_dao::sdf_linear(
            {clipTile.x, clipTile.y, clipTile.w, clipTile.h}, view, 8.0F,
            accent, pink, true));
        manager->endPass();

        stencilPass.preBarriers.add(VkImageMemoryBarrier2{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            .srcAccessMask = 0,
            .dstStageMask =
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                             VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .image = stencilImage_,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT |
                              VK_IMAGE_ASPECT_STENCIL_BIT,
                .levelCount = 1,
                .layerCount = 1}});
        stencilPass.postBarriers.add(VkImageMemoryBarrier2{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image = offscreenResolveImage_,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .levelCount = 1,
                                 .layerCount = 1}});

        // ---- pass 5: offscreen composite (secondary) ---------------------
        auto &composite = manager->beginPass(contentDesc, transparentBase);
        upload_global_mesh(composite, ctx);
        auto &compositeBuilder = composite.frame.builder;
        compositeBuilder.setPipeline(transparent_, layout_);
        const auto compositeUv = [&](const ui_dao::rect_px &r) {
            UvTransform uv{};
            uv.scale = {r.w / W, r.h / H};
            uv.offset = {r.x / W, r.y / H};
            return uv;
        };
        compositeBuilder.setScissor(VkRect2D{
            .offset = {static_cast<std::int32_t>(groupTile.x),
                       static_cast<std::int32_t>(groupTile.y)},
            .extent = {static_cast<std::uint32_t>(groupTile.w),
                       static_cast<std::uint32_t>(groupTile.h)}});
        compositeBuilder.addInstance(ui_dao::texture(
            groupTile, view, offscreenTexture_, kSamplerLinear,
            glm::vec4(1.0F, 1.0F, 1.0F, 0.55F), compositeUv(groupTile), 8.0F));
        compositeBuilder.setScissor(VkRect2D{
            .offset = {static_cast<std::int32_t>(backdropTile.x),
                       static_cast<std::int32_t>(backdropTile.y)},
            .extent = {static_cast<std::uint32_t>(backdropTile.w),
                       static_cast<std::uint32_t>(backdropTile.h)}});
        compositeBuilder.addInstance(ui_dao::blur(
            backdropTile, view, offscreenTexture_, kSamplerLinear, 24.0F, 6,
            glm::vec4(1.0F), compositeUv(backdropTile), 8.0F));
        compositeBuilder.addInstance(ui_dao::sdf_shape(
            {backdropTile.x + 8.0F, backdropTile.y + 30.0F,
             backdropTile.w - 16.0F, backdropTile.h - 42.0F},
            view, 8.0F, glm::vec4(1.0F, 1.0F, 1.0F, 0.10F)));
        compositeBuilder.addInstance(ui_dao::shape_border(
            {backdropTile.x + 8.0F, backdropTile.y + 30.0F,
             backdropTile.w - 16.0F, backdropTile.h - 42.0F},
            view, 8.0F, glm::vec4(1.0F, 1.0F, 1.0F, 0.28F), 1.0F));
        compositeBuilder.setScissor(VkRect2D{
            .offset = {static_cast<std::int32_t>(clipTile.x),
                       static_cast<std::int32_t>(clipTile.y)},
            .extent = {static_cast<std::uint32_t>(clipTile.w),
                       static_cast<std::uint32_t>(clipTile.h)}});
        compositeBuilder.addInstance(ui_dao::texture(
            clipTile, view, offscreenTexture_, kSamplerLinear, glm::vec4(1.0F),
            compositeUv(clipTile), 8.0F));
        compositeBuilder.setScissor(sc);
        manager->endPass();

        // ---- pass 6: text overlay ---------------------------------------
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

        std::println("[ability-test] passes={} glyphs={}",
                     manager->passes().size(), text_.size());
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
    VkImageView offscreenMsaa_{};
    VkImageView offscreenResolve_{};
    VkImage offscreenMsaaImage_{};
    VkImage offscreenResolveImage_{};
    std::uint32_t offscreenTexture_{0};
    VkPipeline stencilMask_{};
    VkPipeline stencilContent_{};
    VkImageView stencilView_{};
    VkImage stencilImage_{};
    std::vector<shader_data::TexQuad> text_{};
    float textW_{-1.0F};
    float textH_{-1.0F};
    std::uint32_t maskTex_{kTexPhoto};
    std::uint32_t maskSampler_{kSamplerLinear};
    UvTransform maskUv_{};

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
            const float a =
                -glm::half_pi<float>() + float(i) * glm::pi<float>() / 5.0F;
            const float r = (i & 1) == 0 ? 0.46F : 0.20F;
            out[static_cast<std::size_t>(i)] = {0.5F + r * std::cos(a),
                                                0.5F + r * std::sin(a)};
        }
    }

    // Pentagram: 5 vertices in star order.  The even-odd path fill shows the
    // self-intersection hole in the middle.
    static void pentagram_points(std::array<glm::vec2, 5> &out)
    {
        for (int i = 0; i < 5; ++i)
        {
            const float a = -glm::half_pi<float>() +
                            float(i) * (2.0F * glm::pi<float>() * 2.0F / 5.0F);
            out[static_cast<std::size_t>(i)] = {0.5F + 0.46F * std::cos(a),
                                                0.5F + 0.46F * std::sin(a)};
        }
    }

    void effect(int i, shader_data::CommandBuilder &b, const ui_dao::view &v,
                const ui_dao::rect_px &c, const glm::vec4 &accent,
                const glm::vec4 &cyan, const glm::vec4 &orange,
                const glm::vec4 &pink, const glm::vec4 &green,
                const glm::vec4 &yellow, const glm::vec4 &card_alt,
                const glm::vec4 &dim)
    {
        switch (i)
        {
        case 0: {
            // Solid fill + outer drop shadow + border on a lighter surface so
            // the shadow itself has something to fall on.
            b.addInstance(ui_dao::sdf_shape(
                {c.x + 4.0F, c.y + 4.0F, c.w - 8.0F, c.h - 8.0F}, v, 8.0F,
                glm::vec4{0.16F, 0.18F, 0.23F, 1.0F}));
            const auto card = ui_dao::rect_px{c.x + 16.0F, c.y + 10.0F,
                                              c.w - 62.0F, c.h - 50.0F};
            b.addInstance(ui_dao::shape_blurred(card, v, 12.0F,
                                                glm::vec4{0.0F, 0.0F, 0.0F, 0.9F},
                                                20.0F));
            b.addInstance(ui_dao::sdf_shape(card, v, 12.0F, accent));
            b.addInstance(ui_dao::shape_border(
                card, v, 12.0F, glm::vec4{1.0F, 1.0F, 1.0F, 0.55F}, 1.0F));
            const auto inner = ui_dao::rect_px{
                card.x + 16.0F, card.y + 14.0F, card.w - 32.0F, card.h - 28.0F};
            b.addInstance(ui_dao::sdf_shape(inner, v, 8.0F,
                                            glm::vec4{1.0F, 1.0F, 1.0F, 0.92F}));
            break;
        }
        case 1: {
            // Linear gradient: horizontal top half, vertical bottom half.
            const auto top = ui_dao::rect_px{c.x, c.y, c.w, c.h * 0.5F - 2.0F};
            const auto bot = ui_dao::rect_px{c.x, c.y + c.h * 0.5F + 2.0F,
                                             c.w, c.h * 0.5F - 2.0F};
            b.addInstance(ui_dao::sdf_linear(top, v, 6.0F, accent, pink, true));
            b.addInstance(ui_dao::sdf_linear(bot, v, 6.0F, accent, pink, false));
            break;
        }
        case 2:
            b.addInstance(ui_dao::sdf_radial(
                c, v, 8.0F, yellow, glm::vec4{0.12F, 0.10F, 0.18F, 1.0F}));
            break;
        case 3:
            b.addInstance(ui_dao::sdf_conic(c, v, 8.0F, cyan, pink, 3.0F));
            break;
        case 4:
            b.addInstance(ui_dao::shape_per_corner(
                {c.x + 10.0F, c.y + 8.0F, c.w - 20.0F, c.h - 16.0F}, v,
                glm::vec4{28.0F, 4.0F, 28.0F, 4.0F}, accent));
            break;
        case 5:
            b.addInstance(ui_dao::shape_border(c, v, 12.0F, cyan, 7.0F));
            b.addInstance(ui_dao::shape_border(
                {c.x + 14.0F, c.y + 10.0F, c.w - 28.0F, c.h - 20.0F}, v, 8.0F,
                yellow, 3.0F));
            b.addInstance(ui_dao::shape_border(
                {c.x + 28.0F, c.y + 22.0F, c.w - 56.0F, c.h - 44.0F}, v, 5.0F,
                pink, 1.5F));
            break;
        case 6:
            b.addInstance(ui_dao::shape_ellipse(
                {c.x + 6.0F, c.y + 8.0F, c.w * 0.56F, c.h - 16.0F}, v,
                glm::vec4{accent.r, accent.g, accent.b, 0.82F}));
            b.addInstance(ui_dao::shape_ellipse(
                {c.x + c.w * 0.36F, c.y + 20.0F, c.w * 0.56F, c.h - 40.0F}, v,
                glm::vec4{pink.r, pink.g, pink.b, 0.68F}));
            break;
        case 7: {
            // Colored blurred silhouette offset behind a solid card = a
            // visible outer glow/shadow.
            b.addInstance(ui_dao::shape_blurred(
                {c.x + 26.0F, c.y + 24.0F, c.w - 58.0F, c.h - 46.0F}, v, 14.0F,
                glm::vec4{0.15F, 0.80F, 0.90F, 0.90F}, 20.0F));
            b.addInstance(ui_dao::sdf_shape(
                {c.x + 14.0F, c.y + 10.0F, c.w - 58.0F, c.h - 46.0F}, v, 12.0F,
                green));
            b.addInstance(ui_dao::shape_border(
                {c.x + 14.0F, c.y + 10.0F, c.w - 58.0F, c.h - 46.0F}, v, 12.0F,
                glm::vec4{1.0F, 1.0F, 1.0F, 0.45F}, 1.0F));
            break;
        }
        case 8:
            b.addInstance(ui_dao::sdf_shape(c, v, 12.0F,
                                            glm::vec4{0.24F, 0.28F, 0.36F, 1.0F}));
            b.addInstance(ui_dao::shape_inner_shadow(
                c, v, 12.0F, glm::vec4{0.0F, 0.0F, 0.0F, 0.95F}, 12.0F, 22.0F));
            break;
        case 9: {
            const auto left = ui_dao::rect_px{c.x, c.y, c.w * 0.48F, c.h};
            const auto right = ui_dao::rect_px{c.x + c.w * 0.52F, c.y,
                                               c.w * 0.48F, c.h};
            b.addInstance(ui_dao::shape_alpha_mask(left, v, 6.0F, accent, 12.0F,
                                                   true));
            b.addInstance(ui_dao::shape_alpha_mask(
                right, v, 6.0F, cyan, 12.0F, false));
            break;
        }
        case 10: {
            // Color management: sRGB-encoded value fed as if it were linear
            // (top) vs the correct linear value (bottom).  The swapchain is
            // B8G8R8A8_SRGB, so the difference is a real gamma difference.
            const glm::vec4 linear{0.18F, 0.36F, 0.82F, 1.0F};
            const glm::vec4 encoded = glm::pow(linear, glm::vec4(1.0F / 2.2F));
            b.addInstance(ui_dao::sdf_shape(
                {c.x, c.y, c.w, c.h * 0.5F - 2.0F}, v, 4.0F, encoded));
            b.addInstance(ui_dao::sdf_shape(
                {c.x, c.y + c.h * 0.5F + 2.0F, c.w, c.h * 0.5F - 2.0F}, v, 4.0F,
                linear));
            break;
        }
        case 11: {
            const std::array<glm::vec2, 3> tri{{{0.5F, 0.10F},
                                                {0.90F, 0.86F},
                                                {0.10F, 0.86F}}};
            const auto left =
                ui_dao::rect_px{c.x, c.y, c.w * 0.48F, c.h};
            const auto right = ui_dao::rect_px{c.x + c.w * 0.52F, c.y,
                                               c.w * 0.48F, c.h};
            b.addInstance(
                ui_dao::path_fill(left, v, tri, accent, cyan, 0, 2.5F));
            b.addInstance(
                ui_dao::path_fill(right, v, tri, accent, cyan, 0, 0.0F));
            break;
        }
        case 12: {
            std::array<glm::vec2, 5> pts{};
            pentagram_points(pts);
            b.addInstance(ui_dao::path_fill(c, v, std::span{pts.data(), 5},
                                            accent, pink, 0, 1.5F));
            break;
        }
        case 13: {
            std::array<glm::vec2, 32> pts{};
            arc_points(-glm::half_pi<float>(), glm::half_pi<float>(), 20, pts);
            b.addInstance(ui_dao::path_stroke(c, v, std::span{pts.data(), 20},
                                              8.0F, cyan, {}, 1));
            break;
        }
        case 14: {
            for (int k = 0; k < 4; ++k)
            {
                const float x0 = 0.06F + float(k) * 0.24F;
                const std::array<glm::vec2, 2> seg{{{x0, 0.5F},
                                                    {x0 + 0.12F, 0.5F}}};
                b.addInstance(ui_dao::path_stroke(c, v, seg, 7.0F, yellow, {},
                                                  1));
            }
            break;
        }
        case 15: {
            auto shape = ui_dao::sdf_shape(
                {c.x + 16.0F, c.y + 8.0F, c.w - 32.0F, c.h - 16.0F}, v, 10.0F,
                accent);
            shape.model = shape.model * glm::rotate(glm::mat4(1.0F),
                                                    glm::radians(18.0F),
                                                    glm::vec3(0.0F, 0.0F, 1.0F));
            b.addInstance(shape);
            auto border = ui_dao::shape_border(
                {c.x + 16.0F, c.y + 8.0F, c.w - 32.0F, c.h - 16.0F}, v, 10.0F,
                glm::vec4{1.0F, 1.0F, 1.0F, 0.55F}, 1.5F);
            border.model = border.model * glm::rotate(glm::mat4(1.0F),
                                                       glm::radians(18.0F),
                                                       glm::vec3(0.0F, 0.0F, 1.0F));
            b.addInstance(border);
            break;
        }
        case 16: {
            auto shape = ui_dao::sdf_shape(
                {c.x + 14.0F, c.y + 10.0F, c.w - 28.0F, c.h - 20.0F}, v, 8.0F,
                pink);
            glm::mat4 shear(1.0F);
            shear[1][0] = 0.35F;
            shape.model = shape.model * shear;
            b.addInstance(shape);
            break;
        }
        case 17: {
            // Nine-slice: 3x3 sub-rects with matching UV sub-rects.
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 3; ++col)
                {
                    const ui_dao::rect_px q{c.x + float(col) * c.w / 3.0F,
                                            c.y + float(row) * c.h / 3.0F,
                                            c.w / 3.0F, c.h / 3.0F};
                    UvTransform uv{};
                    uv.scale = {1.0F / 3.0F, 1.0F / 3.0F};
                    uv.offset = {float(col) / 3.0F, float(row) / 3.0F};
                    b.addInstance(ui_dao::texture(
                        q, v, kTexGradient, kSamplerLinear, glm::vec4(1.0F), uv,
                        2.0F));
                }
            break;
        }
        case 18: {
            const auto left = ui_dao::rect_px{c.x, c.y, c.w * 0.48F, c.h};
            const auto right = ui_dao::rect_px{c.x + c.w * 0.52F, c.y,
                                               c.w * 0.48F, c.h};
            // Procedural alpha mask (dots) on the left, real texture alpha
            // mask (the emoji atlas, which has a genuine alpha channel) on the
            // right.
            b.addInstance(
                ui_dao::shape_alpha_mask(left, v, 4.0F, accent, 10.0F, true));
            b.addInstance(ui_dao::texture_masked(
                right, v, kTexPhoto, kSamplerLinear, maskTex_, maskSampler_,
                maskUv_, glm::vec4(1.0F), {}, 4.0F));
            break;
        }
        case 19: {
            for (int k = 0; k < 4; ++k)
            {
                const float h = c.h / 4.0F;
                b.addInstance(ui_dao::texture_lod(
                    {c.x, c.y + float(k) * h, c.w, h}, v, kTexPhoto,
                    kSamplerLinear, float(k) * 1.5F, glm::vec4(1.0F), {}, 0.0F));
            }
            break;
        }
        case 20: {
            const auto top =
                ui_dao::rect_px{c.x, c.y, c.w, c.h * 0.62F};
            const auto bot = ui_dao::rect_px{c.x, c.y + c.h * 0.66F, c.w,
                                             c.h * 0.34F};
            b.addInstance(ui_dao::blur(top, v, kTexPhoto, kSamplerLinear, 24.0F,
                                       5, glm::vec4(1.0F), {}, 4.0F));
            b.addInstance(ui_dao::texture(bot, v, kTexPhoto, kSamplerLinear,
                                          glm::vec4(1.0F), {}, 0.0F));
            break;
        }
        case 21: {
            glm::mat4 sepia(1.0F);
            sepia[0] = glm::vec4{0.393F, 0.349F, 0.272F, 0.0F};
            sepia[1] = glm::vec4{0.769F, 0.686F, 0.534F, 0.0F};
            sepia[2] = glm::vec4{0.189F, 0.168F, 0.131F, 0.0F};
            sepia[3] = glm::vec4{0.0F, 0.0F, 0.0F, 1.0F};
            b.addInstance(ui_dao::color_matrix(
                c, v, kTexPhoto, kSamplerLinear, sepia,
                glm::vec4{0.02F, 0.01F, 0.0F, 0.0F}, glm::vec4(1.0F), {},
                4.0F));
            break;
        }
        case 22: {
            const auto left = ui_dao::rect_px{c.x, c.y, c.w * 0.48F, c.h};
            const auto right = ui_dao::rect_px{c.x + c.w * 0.52F, c.y,
                                               c.w * 0.48F, c.h};
            glm::mat4 gray(1.0F);
            gray[0] = glm::vec4{0.299F, 0.587F, 0.114F, 0.0F};
            gray[1] = glm::vec4{0.299F, 0.587F, 0.114F, 0.0F};
            gray[2] = glm::vec4{0.299F, 0.587F, 0.114F, 0.0F};
            b.addInstance(ui_dao::color_matrix(
                left, v, kTexPhoto, kSamplerLinear, gray, glm::vec4(0.0F),
                glm::vec4(1.0F), {}, 4.0F));
            glm::mat4 invert(1.0F);
            invert[0][0] = -1.0F;
            invert[1][1] = -1.0F;
            invert[2][2] = -1.0F;
            b.addInstance(ui_dao::color_matrix(
                right, v, kTexPhoto, kSamplerLinear, invert,
                glm::vec4{1.0F, 1.0F, 1.0F, 0.0F}, glm::vec4(1.0F), {}, 4.0F));
            break;
        }
        case 23:
            b.addInstance(ui_dao::noise(
                c, v, glm::vec4{0.02F, 0.05F, 0.12F, 1.0F},
                glm::vec4{0.20F, 0.78F, 0.88F, 1.0F}, 5.0F, 7.0F, 5U, 4.0F));
            break;
        case 24: {
            // MSAA / resolve: thin rotated geometry and 1.5 px borders are
            // the visual proof of 4x MSAA + average resolve.
            b.addInstance(ui_dao::shape_border(c, v, 10.0F, accent, 2.0F));
            for (int k = 0; k < 6; ++k)
            {
                auto bar = ui_dao::sdf_shape(
                    {c.x + 8.0F, c.y + 10.0F + float(k) * 13.0F, c.w - 16.0F,
                     3.0F},
                    v, 1.5F, glm::vec4{cyan.r, cyan.g, cyan.b, 0.75F});
                bar.model = bar.model * glm::rotate(glm::mat4(1.0F),
                                                    glm::radians(22.0F),
                                                    glm::vec3(0.0F, 0.0F, 1.0F));
                b.addInstance(bar);
            }
            auto ring = ui_dao::shape_border(
                {c.x + 30.0F, c.y + 10.0F, c.w - 60.0F, c.h - 20.0F}, v, 8.0F,
                glm::vec4{1.0F, 1.0F, 1.0F, 0.75F}, 1.5F);
            ring.model = ring.model * glm::rotate(glm::mat4(1.0F),
                                                   glm::radians(12.0F),
                                                   glm::vec3(0.0F, 0.0F, 1.0F));
            b.addInstance(ring);
            break;
        }
        case 25: {
            // Real Vulkan blend state: over / additive / multiply.
            b.addInstance(ui_dao::sdf_shape(
                {c.x + 4.0F, c.y + 4.0F, c.w - 8.0F, c.h - 8.0F}, v, 10.0F,
                glm::vec4{0.06F, 0.07F, 0.10F, 1.0F}));
            const auto disc = [&](float dx, const glm::vec4 &color) {
                b.addInstance(ui_dao::sdf_shape(
                    {c.x + c.w * 0.5F + dx - 22.0F, c.y + c.h * 0.5F - 22.0F,
                     44.0F, 44.0F},
                    v, 22.0F, color));
            };
            b.setPipeline(transparent_, layout_);
            disc(-26.0F, glm::vec4{0.95F, 0.35F, 0.30F, 0.85F});
            b.setPipeline(additive_, layout_);
            disc(0.0F, glm::vec4{0.30F, 0.85F, 0.50F, 0.85F});
            b.setPipeline(multiply_, layout_);
            disc(26.0F, glm::vec4{0.45F, 0.50F, 0.95F, 0.85F});
            b.setPipeline(transparent_, layout_);
            break;
        }
        case 32: {
            const auto left = ui_dao::rect_px{c.x, c.y, c.w * 0.48F, c.h};
            const auto right = ui_dao::rect_px{c.x + c.w * 0.52F, c.y,
                                               c.w * 0.48F, c.h};
            // Nested clip: the inner scissor intersects the outer tile scissor.
            b.pushScissor(VkRect2D{
                .offset = {static_cast<std::int32_t>(left.x + 8.0F),
                           static_cast<std::int32_t>(left.y + 14.0F)},
                .extent = {static_cast<std::uint32_t>(left.w - 16.0F),
                           static_cast<std::uint32_t>(left.h - 28.0F)}});
            b.addInstance(ui_dao::texture(left, v, kTexGradient,
                                          kSamplerLinear, glm::vec4(1.0F), {},
                                          4.0F));
            b.popScissor();
            b.addInstance(ui_dao::shape_alpha_mask(
                right, v, 4.0F, glm::vec4{accent.r, accent.g, accent.b, 0.85F},
                9.0F, true));
            break;
        }
        default:
            break;
        }
    }

    void composed_tile(shader_data::CommandBuilder &b,
                       const ui_dao::rect_px &wide, const ui_dao::view &v,
                       const glm::vec4 &accent, const glm::vec4 &green,
                       const glm::vec4 &orange, const glm::vec4 &pink)
    {
        const float y = wide.y + 28.0F;
        // Left: a small "component strip".
        b.addInstance(ui_dao::sdf_linear(
            {wide.x + 10.0F, y, 120.0F, 64.0F}, v, 10.0F, accent, green,
            true));
        b.addInstance(ui_dao::texture(
            {wide.x + 138.0F, y, 66.0F, 64.0F}, v, kTexChess, kSamplerLinear,
            glm::vec4(1.0F), {}, 8.0F));
        b.addInstance(ui_dao::shape_border(
            {wide.x + 212.0F, y, 66.0F, 64.0F}, v, 8.0F, orange, 3.0F));
        b.addInstance(ui_dao::noise(
            {wide.x + 286.0F, y, 66.0F, 64.0F}, v,
            glm::vec4{0.02F, 0.05F, 0.12F, 1.0F},
            glm::vec4{0.20F, 0.78F, 0.88F, 1.0F}, 5.0F, 3.0F, 4U, 6.0F));

        // Middle: a mock table with alternating rows and status pills.
        for (int row = 0; row < 4; ++row)
        {
            const float ry = y + float(row) * 16.0F;
            b.addInstance(ui_dao::sdf_shape(
                {wide.x + 370.0F, ry, 300.0F, 13.0F}, v, 4.0F,
                (row & 1) ? glm::vec4{0.16F, 0.18F, 0.23F, 1.0F}
                          : glm::vec4{0.12F, 0.13F, 0.17F, 1.0F}));
            b.addInstance(ui_dao::sdf_shape(
                {wide.x + 620.0F, ry + 2.0F, 40.0F, 9.0F}, v, 4.0F,
                (row & 1) ? green : orange));
        }

        // Right: blend-state demonstration (additive glow + multiply tint).
        b.setPipeline(additive_, layout_);
        b.addInstance(ui_dao::sdf_shape(
            {wide.x + 700.0F, y - 2.0F, 70.0F, 70.0F}, v, 35.0F,
            glm::vec4{accent.r, accent.g, accent.b, 0.40F}));
        b.setPipeline(multiply_, layout_);
        b.addInstance(ui_dao::sdf_shape(
            {wide.x + 700.0F, y - 2.0F, 70.0F, 70.0F}, v, 35.0F,
            glm::vec4{0.55F, 0.75F, 1.0F, 1.0F}));
        b.setPipeline(transparent_, layout_);
        b.addInstance(ui_dao::sdf_shape(
            {wide.x + 790.0F, y - 2.0F, 70.0F, 70.0F}, v, 35.0F,
            glm::vec4{pink.r, pink.g, pink.b, 0.35F}));
        b.addInstance(ui_dao::shape_border(
            {wide.x + 790.0F, y - 2.0F, 70.0F, 70.0F}, v, 35.0F,
            glm::vec4{1.0F, 1.0F, 1.0F, 0.55F}, 1.5F));

        // Far right: a "glass" card with blur + noise + border.
        b.addInstance(ui_dao::blur(
            {wide.x + 880.0F, y, 170.0F, 64.0F}, v, kTexPhoto, kSamplerLinear,
            26.0F, 6, glm::vec4(1.0F), {}, 8.0F));
        b.addInstance(ui_dao::sdf_shape(
            {wide.x + 888.0F, y + 6.0F, 154.0F, 52.0F}, v, 8.0F,
            glm::vec4{1.0F, 1.0F, 1.0F, 0.12F}));
        b.addInstance(ui_dao::shape_border(
            {wide.x + 888.0F, y + 6.0F, 154.0F, 52.0F}, v, 8.0F,
            glm::vec4{1.0F, 1.0F, 1.0F, 0.30F}, 1.0F));
    }

    float emit_text(render_context &ctx, std::string_view s,
                    const ui_dao::view &v, float px, float x, float baseline,
                    const glm::vec4 &color, bool rightAlign = false,
                    std::uint32_t entity = 0)
    {
        if (s.empty())
            return 0.0F;
        std::string owned{s};
        auto result = run_text_pipeline(ctx.fontSelect, owned.c_str(), "zh-CN");

        float total = 0.0F;
        for (const auto &run : result.shape_result)
            for (const auto &g : run)
                total += static_cast<float>(g.advance_x) * px;

        float cursor = rightAlign ? x - total : x;
        for (const auto &run : result.shape_result)
        {
            for (const auto &g : run)
            {
                if (!(g.plane_bounds == decltype(g.plane_bounds){}))
                    text_.push_back(ui_dao::glyph_from_shaped(g, v, px, cursor,
                                                              baseline, color,
                                                              entity));
                cursor += static_cast<float>(g.advance_x) * px;
            }
        }
        return total;
    }

    // Greedy UAX #14 driven wrap.  The tile is fixed pixel width, so the line
    // break decision is a CPU layout decision, not a shader trick.
    void emit_text_wrapped(render_context &ctx, std::string_view s,
                           const ui_dao::view &v, float px, float x, float y,
                           float maxWidth, float lineHeight,
                           const glm::vec4 &color,
                           std::uint32_t entity = 0)
    {
        if (s.empty())
            return;
        std::string owned{s};
        auto result = run_text_pipeline(ctx.fontSelect, owned.c_str(), "zh-CN");
        const auto &cps = result.codepoints;
        if (cps.empty())
            return;

        std::vector<float> advance(cps.size(), 0.0F);
        for (const auto &run : result.shape_result)
            for (const auto &g : run)
                advance[static_cast<std::size_t>(g.logical_idx)] +=
                    static_cast<float>(g.advance_x) * px;

        std::vector<int> line(cps.size(), 0);
        int cur = 0;
        float lineWidth = 0.0F;
        int lastBreak = -1;
        for (std::size_t i = 0; i < cps.size(); ++i)
        {
            if (result.break_result.must_break(i))
            {
                line[i] = cur;
                ++cur;
                lineWidth = 0.0F;
                lastBreak = -1;
                continue;
            }
            if (lineWidth + advance[i] > maxWidth && lastBreak >= 0 &&
                lastBreak < static_cast<int>(i))
            {
                ++cur;
                lineWidth = 0.0F;
                for (int j = lastBreak + 1; j <= static_cast<int>(i); ++j)
                {
                    line[static_cast<std::size_t>(j)] = cur;
                    lineWidth += advance[static_cast<std::size_t>(j)];
                }
                lastBreak = -1;
            }
            else
            {
                line[i] = cur;
                lineWidth += advance[i];
            }
            if (result.break_result.allow_break(i))
                lastBreak = static_cast<int>(i);
        }

        std::vector<float> cursor(static_cast<std::size_t>(cur) + 1, x);
        for (const auto &run : result.shape_result)
        {
            for (const auto &g : run)
            {
                const auto li =
                    static_cast<std::size_t>(line[static_cast<std::size_t>(
                        g.logical_idx)]);
                const float gx = cursor[li];
                if (!(g.plane_bounds == decltype(g.plane_bounds){}))
                    text_.push_back(ui_dao::glyph_from_shaped(
                        g, v, px, gx, y + float(li) * lineHeight, color,
                        entity));
                cursor[li] += static_cast<float>(g.advance_x) * px;
            }
        }
    }

    void build_text(render_context &ctx, const ui_dao::view &v, float x0,
                    const glm::vec4 &text, const glm::vec4 &dim,
                    const glm::vec4 &accent, const glm::vec4 &pink,
                    const glm::vec4 &orange, const glm::vec4 &green,
                    const glm::vec4 &cyan, const glm::vec4 &yellow)
    {
        text_.clear();
        const auto tile_at = [x0](int c, int r) {
            return ui_dao::rect_px{x0 + float(c) * (kTileW + kGap),
                                   kTop + float(r) * (kTileH + kGap), kTileW,
                                   kTileH};
        };
        const auto content_of = [](const ui_dao::rect_px &t) {
            return ui_dao::rect_px{t.x + 8.0F, t.y + 24.0F,
                                   std::max(t.w - 16.0F, 8.0F),
                                   std::max(t.h - 32.0F, 8.0F)};
        };
        const auto wide = ui_dao::rect_px{
            x0 + (kTileW + kGap), tile_at(0, kRows - 1).y,
            6.0F * kTileW + 5.0F * kGap, kTileH};
        const auto entity_of = [](int c, int r) {
            return static_cast<std::uint32_t>(r * kCols + c + 1);
        };

        // Header.
        emit_text(ctx, "INTENT7 UI ABILITY MATRIX", v, 26.0F, x0, 34.0F, text);
        emit_text(ctx,
                  "7 programs | 7 passes | fixed-pixel NDC | 4x MSAA | 2 MRT",
                  v, 10.0F, x0, 56.0F, dim);
        emit_text(ctx,
                  "offscreen layer + backdrop + stencil clip + secondary | "
                  "sRGB output + color emoji",
                  v, 9.0F, x0, 73.0F, dim);

        const auto viewText = std::format("VIEW {}x{}  |  PX SCALE 1.0 (FIXED)",
                                          static_cast<int>(v.w),
                                          static_cast<int>(v.h));
        emit_text(ctx, viewText, v, 9.0F, v.w - 16.0F, 34.0F, cyan, true);
        emit_text(ctx, "HOVER ANY TILE: MRT PICK", v, 8.0F, v.w - 16.0F, 51.0F,
                  dim, true);
        emit_text(ctx, "DRAG RESIZE / F11 FULLSCREEN: RESOURCES REBUILT", v, 8.0F,
                  v.w - 16.0F, 66.0F, dim, true);

        // One label per feature tile.
        static constexpr std::array<const char *, kTileCount> names = {
            "SOLID/SHADOW", "LINEAR",       "RADIAL",     "CONIC",
            "PER-CORNER",   "BORDER/RING",  "ELLIPSE",    "OUTER-SHADOW",
            "INNER-SHADOW", "ALPHA-MASK",   "SRGB/LINEAR", "TRIANGLE/AA",
            "STAR/EVEN-ODD", "ARC",         "DASH",       "ROTATE",
            "SKEW",         "NINE-SLICE",   "MASKED-TEX", "MIPMAP/LOD",
            "BLUR",         "SEPIA",        "GRAY/INVERT", "NOISE",
            "MSAA/RESOLVE", "BLEND-MODES",  "TEXT/MSDF",  "TEXT/RTL",
            "TEXT/CJK",     "TEXT/EMOJI",   "TEXT/WRAP",  "TEXT/MIXED-BIDI",
            "CLIP/NESTED",  "GROUP-OPACITY", "BACKDROP",   "STENCIL-CLIP"};
        for (int i = 0; i < kTileCount; ++i)
        {
            const auto t = tile_at(i % kCols, i / kCols);
            emit_text(ctx, names[static_cast<std::size_t>(i)], v, 9.0F,
                      t.x + 8.0F, t.y + 15.0F, dim, false,
                      static_cast<std::uint32_t>(i + 1));
        }

        // Per-tile explanatory samples.
        emit_text(ctx, "sRGB curve", v, 7.5F, tile_at(3, 1).x + 8.0F,
                  tile_at(3, 1).y + 44.0F, dim, false, entity_of(3, 1));
        emit_text(ctx, "linear", v, 7.5F, tile_at(3, 1).x + 8.0F,
                  tile_at(3, 1).y + 100.0F, text, false, entity_of(3, 1));
        emit_text(ctx, "AA 2.5px", v, 7.0F, tile_at(4, 1).x + 10.0F,
                  tile_at(4, 1).y + 112.0F, dim, false, entity_of(4, 1));
        emit_text(ctx, "AA 0px", v, 7.0F, tile_at(4, 1).x + 88.0F,
                  tile_at(4, 1).y + 112.0F, dim, false, entity_of(4, 1));
        emit_text(ctx, "4x resolve", v, 8.0F, tile_at(3, 3).x + 8.0F,
                  tile_at(3, 3).y + 112.0F, dim, false, entity_of(3, 3));
        emit_text(ctx, "OVER / ADD / MUL", v, 7.5F, tile_at(4, 3).x + 10.0F,
                  tile_at(4, 3).y + 112.0F, dim, false, entity_of(4, 3));

        // Text laboratory.  Positions are fixed pixels inside fixed tiles.
        {
            const auto t = tile_at(5, 3);
            const auto c = content_of(t);
            emit_text(ctx, "Aa", v, 34.0F, c.x + 12.0F, c.y + 44.0F, text,
                      false, entity_of(5, 3));
            emit_text(ctx, "MSDF 0123", v, 10.0F, c.x + 8.0F, c.y + 72.0F,
                      dim, false, entity_of(5, 3));
            emit_text(ctx, "MSDF", v, 8.0F, c.x + 128.0F, c.y + 30.0F, accent,
                      false, entity_of(5, 3));
        }
        {
            const auto t = tile_at(6, 3);
            const auto c = content_of(t);
            // RTL paragraph: the bidi shaper already returns visual order.
            emit_text(ctx, "مرحبا", v, 24.0F, c.x + c.w - 8.0F, c.y + 46.0F,
                      text, true, entity_of(6, 3));
            emit_text(ctx, "العالم", v, 14.0F, c.x + c.w - 8.0F, c.y + 70.0F,
                      accent, true, entity_of(6, 3));
            emit_text(ctx, "RTL", v, 8.0F, c.x + 8.0F, c.y + 88.0F, dim, false,
                      entity_of(6, 3));
        }
        {
            const auto t = tile_at(0, 4);
            const auto c = content_of(t);
            emit_text(ctx, "世界", v, 34.0F, c.x + 8.0F, c.y + 52.0F, text,
                      false, entity_of(0, 4));
            emit_text(ctx, "是的 一不", v, 12.0F, c.x + 8.0F, c.y + 80.0F,
                      cyan, false, entity_of(0, 4));
        }
        {
            const auto t = tile_at(1, 4);
            const auto c = content_of(t);
            emit_text(ctx, "😀😃😄", v, 30.0F, c.x + 8.0F, c.y + 50.0F, text,
                      false, entity_of(1, 4));
            emit_text(ctx, "😁😂🤣", v, 20.0F, c.x + 8.0F, c.y + 80.0F, text,
                      false, entity_of(1, 4));
            emit_text(ctx, "COLOR EMOJI", v, 7.5F, c.x + 96.0F, c.y + 88.0F,
                      dim, false, entity_of(1, 4));
        }
        {
            const auto t = tile_at(2, 4);
            const auto c = content_of(t);
            emit_text_wrapped(
                ctx, "MSDF text wraps inside a fixed pixel tile by UAX #14 breaks.",
                v, 7.5F, c.x + 4.0F, c.y + 12.0F, c.w - 8.0F, 12.0F, dim,
                entity_of(2, 4));
        }
        {
            const auto t = tile_at(3, 4);
            const auto c = content_of(t);
            emit_text(ctx, "ABC مرحبا 123", v, 13.0F, c.x + 6.0F,
                      c.y + 30.0F, text, false, entity_of(3, 4));
            emit_text(ctx, "LTR + RTL", v, 8.0F, c.x + 6.0F, c.y + 48.0F,
                      accent, false, entity_of(3, 4));
            emit_text(ctx, "مرحبا ABC", v, 12.0F, c.x + 6.0F, c.y + 72.0F,
                      pink, false, entity_of(3, 4));
        }
        {
            const auto t = tile_at(5, 4);
            emit_text(ctx, "α 0.55", v, 9.0F, t.x + 12.0F, t.y + 112.0F, dim,
                      false, entity_of(5, 4));
        }
        {
            const auto t = tile_at(6, 4);
            emit_text(ctx, "BLUR 24px", v, 9.0F, t.x + 12.0F, t.y + 112.0F,
                      dim, false, entity_of(6, 4));
        }
        {
            const auto t = tile_at(0, 5);
            emit_text(ctx, "EQUAL 1", v, 9.0F, t.x + 12.0F, t.y + 112.0F, dim,
                      false, entity_of(0, 5));
        }

        // Composed panel labels.
        emit_text(ctx, "COMPOSED UI  /  PASS 5 COMPOSITE (SECONDARY)", v,
                  10.0F, wide.x + 10.0F, wide.y + 16.0F, text);
        emit_text(ctx, "GRAD", v, 7.5F, wide.x + 12.0F, wide.y + 108.0F, green);
        emit_text(ctx, "TEX", v, 7.5F, wide.x + 138.0F, wide.y + 108.0F, cyan);
        emit_text(ctx, "BORDER", v, 7.5F, wide.x + 212.0F, wide.y + 108.0F,
                  orange);
        emit_text(ctx, "NOISE", v, 7.5F, wide.x + 286.0F, wide.y + 108.0F,
                  yellow);
        emit_text(ctx, "ROW A", v, 7.5F, wide.x + 378.0F, wide.y + 40.0F, dim);
        emit_text(ctx, "ROW B", v, 7.5F, wide.x + 378.0F, wide.y + 56.0F, dim);
        emit_text(ctx, "ROW C", v, 7.5F, wide.x + 378.0F, wide.y + 72.0F, dim);
        emit_text(ctx, "ROW D", v, 7.5F, wide.x + 378.0F, wide.y + 88.0F, dim);
        emit_text(ctx, "ADD", v, 7.5F, wide.x + 700.0F, wide.y + 108.0F,
                  accent);
        emit_text(ctx, "MUL", v, 7.5F, wide.x + 790.0F, wide.y + 108.0F, pink);
        emit_text(ctx, "MSDF / RTL / 世界 / 😀", v, 9.0F, wide.x + 880.0F,
                  wide.y + 44.0F, text);
        emit_text(ctx, "2 MRT | 4x MSAA | D32S8", v, 7.5F, wide.x + 880.0F,
                  wide.y + 60.0F, dim);
        emit_text(ctx, "indexed+instanced+indirect+BDA", v, 7.5F,
                  wide.x + 880.0F, wide.y + 74.0F, dim);
        emit_text(ctx, "barrier2 | transfer pass", v, 7.5F,
                  wide.x + 880.0F, wide.y + 88.0F, dim);

        // Build a real texture alpha mask from the emoji atlas (it has an
        // alpha channel), then use it in the MASKED-TEX tile.
        auto mask = run_text_pipeline(ctx.fontSelect, "😀", "zh-CN");
        for (const auto &run : mask.shape_result)
        {
            for (const auto &g : run)
            {
                if (g.plane_bounds == decltype(g.plane_bounds){})
                    continue;
                const auto q = ui_dao::glyph_from_shaped(
                    g, v, 64.0F, 0.0F, 0.0F, glm::vec4(1.0F));
                maskTex_ = q.textureIndex;
                maskSampler_ = q.samplerIndex;
                maskUv_ = q.uvTransform;
                return;
            }
        }
    }
};

} // namespace ui_new
