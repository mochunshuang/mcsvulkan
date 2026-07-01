
#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <flat_map>
#include <glm/ext/matrix_transform.hpp>
#include <glm/fwd.hpp>
#include <iostream>
#include <optional>
#include <print>
#include <chrono>
#include <random>
#include <span>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "../head.hpp"

using Instance = mcs::vulkan::Instance;
using create_instance = mcs::vulkan::tool::create_instance;
using create_debugger = mcs::vulkan::tool::create_debugger;
using physical_device_selector = mcs::vulkan::tool::physical_device_selector;
using mcs::vulkan::vkMakeVersion;
using mcs::vulkan::vkApiVersion;

using mcs::vulkan::tool::enable_intance_build;
using mcs::vulkan::tool::structure_chain;
using mcs::vulkan::tool::queue_family_index_selector;
using mcs::vulkan::tool::create_logical_device;
using mcs::vulkan::tool::create_swap_chain;
using mcs::vulkan::tool::create_pipeline_layout;
using mcs::vulkan::tool::create_graphics_pipeline;
using mcs::vulkan::tool::create_command_pool;
using mcs::vulkan::tool::frame_context;
using mcs::vulkan::tool::sType;
using mcs::vulkan::tool::create_descriptor_set_layout;
using mcs::vulkan::tool::create_descriptor_pool;

using mcs::vulkan::camera::camera_interface;
using mcs::vulkan::input::glfw_input;

using mcs::vulkan::raii_vulkan;
using mcs::vulkan::PhysicalDevice;
using mcs::vulkan::surface_impl;
using surface = mcs::vulkan::wsi::glfw::Window;
using mcs::vulkan::Queue;

using mcs::vulkan::LogicalDevice;

using mcs::vulkan::tool::make_pNext;

using mcs::vulkan::CommandPool;
using mcs::vulkan::CommandBuffers;
using mcs::vulkan::CommandBuffer;
using mcs::vulkan::CommandBufferView;
using mcs::vulkan::Fence;

using raii_vma = mcs::vulkan::raii_vma;

using mcs::vulkan::tool::simple_copy_buffer;

using mcs::vulkan::meta::make_aggregate_ref;
using mcs::vulkan::meta::make_aggregate;

using mcs::vulkan::ecs::gen_soa_aggregate;

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;
constexpr auto TITLE = "test_my_triangle";

static constexpr auto MAX_FRAMES_IN_FLIGHT = 2;

struct my_render
{

    struct image_info
    {
        struct trans
        {
            VkImageLayout layout;
            VkAccessFlags2 access_mask;
            VkPipelineStageFlags2 stage_mask;
        };
        VkImage image;
        VkImageAspectFlags aspect_mask;
        trans src;
        trans dst;
    };

    template <std::same_as<image_info>... T>
    static void transition_image_layout(const CommandBufferView &commandBuffer,
                                        T... info) noexcept
    {
        std::array<VkImageMemoryBarrier2, sizeof...(T)> barriers{VkImageMemoryBarrier2{
            .sType = sType<VkImageMemoryBarrier2>(),
            // Specify the pipeline stages and access masks for the barrier
            .srcStageMask = info.src.stage_mask,
            .srcAccessMask = info.src.access_mask,
            .dstStageMask = info.dst.stage_mask,
            .dstAccessMask = info.dst.access_mask,
            // Specify the old and new layouts of the image
            .oldLayout = info.src.layout,
            .newLayout = info.dst.layout,
            // We are not changing the ownership between queues
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            // Specify the image to be affected by this barrier
            .image = info.image,
            // Define the subresource range (which parts of the image are affected)
            .subresourceRange = {.aspectMask = info.aspect_mask,
                                 .baseMipLevel = 0,
                                 .levelCount = VK_REMAINING_MIP_LEVELS,
                                 .baseArrayLayer = 0,
                                 .layerCount = VK_REMAINING_ARRAY_LAYERS}}...};
        commandBuffer.pipelineBarrier2(VkDependencyInfo{
            .sType = sType<VkDependencyInfo>(),
            .pNext = nullptr,
            .dependencyFlags = 0,
            .memoryBarrierCount = 0,
            .pMemoryBarriers = nullptr,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers = nullptr,
            .imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size()),
            .pImageMemoryBarriers = barriers.data()});
    }
};

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

struct Vertex // NOLINT
{
    glm::vec3 pos;
    glm::vec2 texCoord; // diff: [texture] 添加纹理坐标
};
struct VertexAttribute
{
    glm::vec3 color; // 仅颜色
};

namespace mesh
{
    using buffer_base = mcs::vulkan::memory::buffer_base;
    using auto_map_buffer = mcs::vulkan::memory::auto_map_buffer;
    using mcs::vulkan::memory::create_simple_buffer;
    using mcs::vulkan::memory::create_staging_buffer;

    using index_type = uint32_t;

    using position_3d = glm::vec3;

    static auto get_topLeftLocal(const std::span<const Vertex> &vertices) noexcept
    { // 计算包围盒和左上角坐标
        assert(!vertices.empty());
        glm::vec3 minPos = vertices[0].pos;
        glm::vec3 maxPos = vertices[0].pos;
        for (const auto &v : vertices)
        {
            minPos = glm::min(minPos, v.pos);
            maxPos = glm::max(maxPos, v.pos);
        }
        // 左上角 = (minX, maxY, maxZ)
        return glm::vec3(minPos.x, maxPos.y, maxPos.z);
    }

}; // namespace mesh

namespace font
{
    // 2. Library Initialization
    using freetype_version = mcs::vulkan::font::freetype::version;
    using freetype_loader = mcs::vulkan::font::freetype::loader;
    using freetype_face = mcs::vulkan::font::freetype::face;

    using Font = mcs::vulkan::font::Font;
    // using FontTexture = mcs::vulkan::font::FontTexture;
    using FontType = mcs::vulkan::font::FontType;

    using harfbuzz_font = mcs::vulkan::font::harfbuzz::font;

    using texture_bind_sampler = mcs::vulkan::font::texture_bind_sampler;
    // using FontContext = mcs::vulkan::font::FontContext;

    // using glyph_info = mcs::vulkan::font::GlyphInfo;
    using texture_info = mcs::vulkan::font::texture_info;
    using FontInfo = mcs::vulkan::font::FontInfo;
    using font_register = mcs::vulkan::font::font_register;

    using font_registration = mcs::vulkan::font::font_registration;
    // using FontSelector = mcs::vulkan::font::FontSelector;
    // using FontFactory = mcs::vulkan::font::FontFactory;
    using FontMetadata = mcs::vulkan::font::FontMetadata;

    // mcs::vulkan::font
    using FontTexture = mcs::vulkan::memory::resource;

    template <typename FontContext>
    class GlyphInfo
    {
      public:
        using Bounds = Font::Bounds;
        GlyphInfo() = default;
        constexpr GlyphInfo(Bounds uv_bounds, const FontContext *font_ctx,
                            const Font::glyphs_type *glyph) noexcept
            : uvBounds_{uv_bounds}, fontCtx_{font_ctx}, glyph_{glyph}
        {
        }

        [[nodiscard]] constexpr auto &type() const noexcept
        {
            return fontCtx_->type;
        }

        // NOLINTBEGIN
        [[nodiscard]] constexpr auto &texture_index() const noexcept
        {
            return fontCtx_->bind.texture_index;
        }
        [[nodiscard]] constexpr auto &sampler_index() const noexcept
        {
            return fontCtx_->bind.sampler_index;
        }
        [[nodiscard]] constexpr auto &font() const noexcept
        {
            return fontCtx_->font;
        }
        [[nodiscard]] constexpr Bounds plane_bounds() const noexcept
        {
            return glyph_->planeBounds.value_or(Bounds{0, 0, 0, 0});
        }
        [[nodiscard]] constexpr double advance() const noexcept
        {
            return glyph_->advance;
        }

        constexpr static GlyphInfo make(const Font::glyphs_type &glyph,
                                        const FontContext &atlas) noexcept
        {
            auto atlasBounds = glyph.atlasBounds;
            if (!atlasBounds)
                // 空格等
                return GlyphInfo{
                    {.left = 0, .bottom = 0, .right = 0, .top = 0}, &atlas, &glyph};

            const auto &font = atlas.font;
            double w = font.atlas.width;
            double h = font.atlas.height;
            if (atlas.font.atlas.yOrigin == "bottom")
            {
                // Y_UPWARD: 需要翻转
                auto left = atlasBounds->left / w;
                auto bottom = (h - atlasBounds->bottom) / h;
                auto right = atlasBounds->right / w;
                auto top = (h - atlasBounds->top) / h;
                return GlyphInfo{
                    {.left = left, .bottom = bottom, .right = right, .top = top},
                    &atlas,
                    &glyph};
            }

            // "top"
            // Y_DOWNWARD: 无需翻转，直接使用原始 bounds（已交换）
            auto left = atlasBounds->left / w;
            auto bottom = atlasBounds->bottom / h;
            auto right = atlasBounds->right / w;
            auto top = atlasBounds->top / h;
            return GlyphInfo{{.left = left, .bottom = bottom, .right = right, .top = top},
                             &atlas,
                             &glyph};
        }

        [[nodiscard]] constexpr const FontContext *font_ctx() const noexcept
        {
            return fontCtx_;
        }

        [[nodiscard]] constexpr Bounds uv_bounds() const noexcept
        {
            return uvBounds_;
        }

      private:
        Bounds uvBounds_;
        const FontContext *fontCtx_;
        const Font::glyphs_type *glyph_;
    }; // NOLINTEND

    template <typename FontTexture>
    class FontContext
    {
      public: // NOLINTBEGIN
        using texture_type = FontTexture;

        std::string name;
        Font font;
        FontTexture texture;
        mcs::vulkan::font::freetype::face face;

        FontType type;
        texture_bind_sampler bind;

        std::unordered_map<FT_UInt, const Font::glyphs_type *> glyph_index_to_glyphs;
        std::unordered_map<uint32_t, const Font::glyphs_type *> unicode_default_glyphs;
        mcs::vulkan::font::harfbuzz::font hb_font;

        FontMetadata meta_data;
        // NOLINTEND

        constexpr FontContext(const std::string &jsonPath, FontTexture texture,
                              mcs::vulkan::font::freetype::face &&face, FontType type,
                              texture_bind_sampler bind, FontMetadata meta_data)
            : name(jsonPath), font(Font::make(jsonPath)), texture(std::move(texture)),
              face(std::move(face)), type(type), bind(bind),
              meta_data{std::move(meta_data)}
        {
            FT_Face raw_face = *this->face; // 获取原始 FT_Face
            FT_Set_Pixel_Sizes(raw_face, 0,
                               static_cast<FT_UInt>(font.atlas.size)); // 设置像素大小

            hb_font = mcs::vulkan::font::harfbuzz::font{raw_face}; // 创建 HarfBuzz 字体

            using mcs::vulkan::MCS_ASSERT;
            // 1. 填充 glyph_index_to_info
            for (const auto &glyph : font.glyphs)
            {
                using GLYPH_INDEX = Font::glyphs_type::GLYPH_INDEX;
                using UNICODE_CODEPOINT = Font::glyphs_type::UNICODE_CODEPOINT;
                static_assert(sizeof(GLYPH_INDEX) == sizeof(UNICODE_CODEPOINT),
                              "should equal.");
                static_assert(sizeof(GLYPH_INDEX) == sizeof(FT_UInt), "should equal.");

                assert(!std::holds_alternative<std::monostate>(glyph.index_or_unicode));
                if (std::holds_alternative<GLYPH_INDEX>(glyph.index_or_unicode))
                {
                    auto glyph_index = static_cast<FT_UInt>(
                        std::get<GLYPH_INDEX>(glyph.index_or_unicode));
                    MCS_ASSERT(!glyph_index_to_glyphs.contains(glyph_index));
                    glyph_index_to_glyphs[glyph_index] = &glyph;
                }
                else
                {
                    auto codepoint = std::get<UNICODE_CODEPOINT>(glyph.index_or_unicode);
                    FT_UInt glyph_index = FT_Get_Char_Index(raw_face, codepoint);
                    if (glyph_index != 0)
                    {
                        MCS_ASSERT(!glyph_index_to_glyphs.contains(glyph_index));
                        glyph_index_to_glyphs[glyph_index] = &glyph;
                    }
                }
            }

            // 2. 构建 unicode_default_glyphs
            FT_Select_Charmap(raw_face, FT_ENCODING_UNICODE); // 确保使用 Unicode 映射
            FT_UInt gindex;                                   // NOLINT
            FT_ULong charcode = FT_Get_First_Char(raw_face, &gindex);
            while (gindex != 0)
            {
                auto it = glyph_index_to_glyphs.find(gindex);
                if (it != glyph_index_to_glyphs.end())
                {
                    MCS_ASSERT(!unicode_default_glyphs.contains(charcode));
                    unicode_default_glyphs[charcode] = it->second;
                }
                charcode = FT_Get_Next_Char(raw_face, charcode, &gindex);
            }
        }

        [[nodiscard]] constexpr double getEmUnits() const noexcept
        {
            return static_cast<double>((*face)->units_per_EM);
        }
        // NOLINTBEGIN
        [[nodiscard]] constexpr bool has_glyph(uint32_t codepoint) const noexcept
        {
            return unicode_default_glyphs.contains(codepoint);
        }
        static constexpr bool contain_codepoint(const FontMetadata &meta_data,
                                                char32_t codepoint) noexcept
        {
            return hb_set_has(meta_data.unicode_set.get(), codepoint) != 0;
        }
        [[nodiscard]] constexpr static bool contain_lang(const FontMetadata &meta_data,
                                                         hb_tag_t language_tag) noexcept
        {
            return meta_data.lang_tags.contains(language_tag);
        }
        [[nodiscard]] constexpr static bool contain_script(const FontMetadata &meta_data,
                                                           hb_script_t script) noexcept
        {
            return meta_data.scripts.contains(script);
        }

        [[nodiscard]] constexpr bool contain_codepoint(char32_t codepoint) const noexcept
        {
            return contain_codepoint(meta_data, codepoint);
        }
        [[nodiscard]] constexpr bool contain_lang(hb_tag_t language_tag) const noexcept
        {
            return contain_lang(meta_data, language_tag);
        }
        [[nodiscard]] constexpr bool contain_script(hb_script_t script) const noexcept
        {
            return contain_script(meta_data, script);
        }

        using glyph_info_type = GlyphInfo<FontContext>;
        // NOLINTEND
    };

    template <typename FontContext, typename MakeFontTexture>
        requires(requires(MakeFontTexture make, FontInfo info) {
            { make(info) } -> std::same_as<typename FontContext::texture_type>;
        })
    class FontFactory
    {

      public:
        using font_context_type = FontContext;
        constexpr FontFactory(MakeFontTexture make, FT_Library library) noexcept
            : make_{std::move(make)}, library_{library}
        {
        }

        const FontContext *make(FontInfo info)
        {
            using stbi_image_type = texture_info::stbi_image_type;
            const auto &registration = info.registration;
            if (std::holds_alternative<stbi_image_type>(
                    registration.texture_info.image_variant))
            {
                fonts_.emplace_back(std::make_unique<FontContext>(
                    registration.json_path, make_(info),
                    mcs::vulkan::font::freetype::face{library_, registration.font_path,
                                                      info.meta_data.face_index},
                    registration.type, registration.texture_info.bind,
                    std::move(info.meta_data)));
                return fonts_.back().get();
            }
            return nullptr;
        }

      private:
        MakeFontTexture make_;
        FT_Library library_;
        std::vector<std::unique_ptr<FontContext>> fonts_;
    };

    template <typename MakeFontTexture>
    constexpr static auto make_font_factory(MakeFontTexture make, FT_Library library)
        -> FontFactory<FontContext<decltype(make(std::declval<FontInfo>()))>,
                       MakeFontTexture>
    {
        return {std::move(make), library};
    }

    template <typename FontFactory>
    class FontSelector
    {
        using FontContext = FontFactory::font_context_type;
        struct select_result // NOLINTBEGIN
        {
            const FontContext *font{nullptr};
            hb_language_t language{nullptr};

            operator bool() const noexcept
            {
                return font != nullptr;
            }
        }; // NOLINTEND

        // 辅助：在 selectable_ 中查找满足谓词的字体
        const FontContext *findInSelectable(auto &&pred) const noexcept
        {
            for (const auto *font : selectable_)
            {
                if (pred(font))
                    return font;
            }
            return nullptr;
        }

        // 辅助：在 candidate_ 中查找满足 info_pred 的字体，加载并验证 has_glyph
        const FontContext *loadFromCandidate(auto &&info_pred, char32_t codepoint)
        {
            for (auto it = candidate_.begin(); it != candidate_.end(); ++it)
            {
                if (info_pred(*it))
                    if (const auto *newFont = factory_->make(std::move(*it)); newFont)
                    {
                        selectable_.push_back(newFont);
                        candidate_.erase(it);
                        if (newFont->has_glyph(codepoint))
                            return newFont;
                    }
            }
            return nullptr;
        }

        select_result findOrLoad(std::optional<hb_tag_t> tag,
                                 std::optional<hb_script_t> script, char32_t codepoint)
        {
            // 构造 selectable 谓词：检查已加载字体
            auto selectable_pred = [&](const FontContext *font) -> bool {
                if (!font->has_glyph(codepoint))
                    return false;
                if (tag && !font->contain_lang(*tag))
                    return false;
                if (script && !font->contain_script(*script))
                    return false;
                // contain_codepoint 可省略，因为 has_glyph 更强
                return true;
            };

            // 构造 candidate 谓词：仅检查元数据（未加载）
            auto candidate_pred = [&](const FontInfo &info) -> bool {
                if (tag && !FontContext::contain_lang(info.meta_data, *tag))
                    return false;
                if (script && !FontContext::contain_script(info.meta_data, *script))
                    return false;
                if (!FontContext::contain_codepoint(info.meta_data, codepoint))
                    return false;
                return true;
            };

            // 1. 在已加载字体中查找
            if (const auto *font = findInSelectable(selectable_pred))
            {
                return {.font = font,
                        .language =
                            tag ? mcs::vulkan::font::harfbuzz::tag_to_language(*tag)
                                : nullptr};
            }

            // 2. 在候选字体中查找并加载
            if (const auto *font = loadFromCandidate(candidate_pred, codepoint))
            {
                return {.font = font,
                        .language =
                            tag ? mcs::vulkan::font::harfbuzz::tag_to_language(*tag)
                                : nullptr};
            }

            return {.font = nullptr, .language = nullptr};
        }

      public:
        using select_result_type = select_result;
        using font_context_type = FontContext;
        // TODO(mcs): 可补充字体排序权重、缓存匹配结果、处理多语言回退顺序
        /*
        字体选择的核心是确定哪个字体能够提供该字符（码点）的合理字形，而具体字形形状、变体、合字等是通过
        HarfBuzz 的 shaping
        过程在选定字体后处理的。因此，将字形作为选择因素的必要性较低
        */
        [[nodiscard]] constexpr auto selectFont(char32_t codepoint, hb_script_t script)
            -> select_result
        {
            // 完全匹配
            if (auto ret = findOrLoad(preferenceTag(), script, codepoint))
                return ret;
            // 不限定包括语言
            if (auto ret = findOrLoad(std::nullopt, script, codepoint))
                return ret;
            // 不限定语言和脚本
            return findOrLoad(std::nullopt, std::nullopt, codepoint);
        }
        [[nodiscard]] constexpr auto operator()(char32_t codepoint, hb_script_t script)
            -> select_result
        {
            return selectFont(codepoint, script);
        }

        [[nodiscard]] static hb_language_t getFallbackLanguage(
            hb_script_t script, const FontContext *select_font) noexcept
        {
            if (select_font != nullptr && !select_font->meta_data.lang_tags.empty() &&
                select_font->meta_data.scripts.contains(script))
            {
                if (hb_language_t default_lang =
                        mcs::vulkan::font::harfbuzz::script_to_language(script);
                    default_lang != HB_LANGUAGE_INVALID)
                    for (hb_tag_t tag : select_font->meta_data.lang_tags)
                        if (default_lang == hb_ot_tag_to_language(tag))
                            return default_lang;

                MCSLOG_WARN("no default_lang in {} ", select_font->name);
                return hb_ot_tag_to_language(*select_font->meta_data.lang_tags.begin());
            }
            // 无字体支持该脚本时，使用 HarfBuzz 的默认语言
            return hb_language_get_default();
        }

        constexpr FontSelector(FontFactory *factory, hb_tag_t preference) noexcept
            : factory_{factory},
              language_{mcs::vulkan::font::harfbuzz::make_language_type(preference)}
        {
        }
        constexpr FontSelector(FontFactory *factory,
                               const std::string_view &bcp47) noexcept
            : FontSelector(factory, mcs::vulkan::font::harfbuzz::bcp47_to_tag(bcp47))
        {
        }

        [[nodiscard]] hb_language_t preferenceLanguage() const noexcept
        {
            return language_.language;
        }

        // NOTE: 值语义更高效
        [[nodiscard]] std::string_view langBcp47() const noexcept
        {
            return language_.lang_bcp47;
        }

        [[nodiscard]] constexpr hb_tag_t preferenceTag() const noexcept
        {
            return language_.tag;
        }
        constexpr auto &&setPreferenceTag(hb_tag_t preferenceTag) noexcept
        {
            language_ = mcs::vulkan::font::harfbuzz::make_language_type(preferenceTag);
            return *this;
        }
        // NOTE: 语言 地区
        constexpr auto &&setPreferenceLanguage(const std::string_view &bcp47) noexcept
        {
            language_ = mcs::vulkan::font::harfbuzz::make_language_type(
                mcs::vulkan::font::harfbuzz::bcp47_to_tag(bcp47));
            return std::move(*this);
        }
        constexpr auto &&load(FontInfo info)
        {
            const FontContext *newFont = factory_->make(std::move(info));
            mcs::vulkan::MCS_ASSERT(newFont != nullptr);
            selectable_.emplace_back(newFont);
            return std::move(*this);
        }

        constexpr auto &&load(std::vector<FontInfo> infos)
        {
            for (auto &info : infos)
                load(std::move(info));
            return std::move(*this);
        }

        [[nodiscard]] constexpr const auto &candidate() const noexcept
        {
            return candidate_;
        }
        constexpr auto &&setCandidate(std::vector<FontInfo> candidate) noexcept
        {
            candidate_ = std::move(candidate);
            return std::move(*this);
        }

        [[nodiscard]] constexpr auto &selectable() const noexcept
        {
            return selectable_;
        }
        constexpr void initNotdefFont() noexcept
        {
            for (const FontContext *c : selectable_)
            {
                if (c->glyph_index_to_glyphs.size() == 1 &&
                    c->glyph_index_to_glyphs.contains(0))
                {
                    notdefFont_ = c;
                    break;
                }
            }
        }

        [[nodiscard]] constexpr const FontContext *notdefFont() const noexcept
        {
            return notdefFont_;
        }

        constexpr void setNotdefFont(const FontContext *newFont) noexcept
        {
            notdefFont_ = newFont;
        }
        constexpr void setNotdefFont(int index) noexcept
        {
            notdefFont_ = selectable_[index];
            assert((notdefFont_->glyph_index_to_glyphs.contains(0)));
        }

      private:
        FontFactory *factory_;
        mcs::vulkan::font::harfbuzz::language_type language_;
        std::vector<const FontContext *> selectable_; // NOTE: 已经排好序
        std::vector<FontInfo> candidate_;
        const FontContext *notdefFont_;
    };

    // using glyph_info = GlyphInfo;
    // class FontLibrary
    // {

    //     std::vector<FontContext> fonts_;
    //     struct font_value
    //     {
    //         size_t font_index;
    //         int score;

    //         [[nodiscard]] glyph_info getDefaultGlyph(
    //             Font::glyphs_type::UNICODE_CODEPOINT unicode,
    //             const FontLibrary &lib) const
    //         {
    //             auto &font = lib.fonts_[font_index];
    //             assert(!font.unicode_default_glyphs.empty());
    //             assert(font.unicode_default_glyphs.contains(unicode));
    //             auto &glyph = font.unicode_default_glyphs.at(
    //                 unicode); // 使用 at() 而不是 operator[]
    //             return glyph_info::make(*glyph, font);
    //         }
    //     };
    //     struct font_value_greater
    //     {
    //         constexpr bool operator()(const font_value &a,
    //                                   const font_value &b) const noexcept
    //         {
    //             if (a.score != b.score)
    //                 return a.score > b.score; // 分数越大越靠前
    //             return a.font_index < b.font_index;
    //         }
    //     };
    //     // 在 unordered_multimap 的值类型中使用该比较器
    //     std::unordered_multimap<char32_t, std::set<font_value, font_value_greater>>
    //         unicodeDefaultFont_;

    //   public:
    //     auto &operator[](size_t i) noexcept
    //     {
    //         return fonts_[i];
    //     }
    //     auto &operator[](size_t i) const noexcept
    //     {
    //         return fonts_[i];
    //     }

    //     FontLibrary() = default;
    //     void load(FontType type, texture_bind_sampler bind, FontTexture texture,
    //               const std::string &jsonPath, freetype_face face, int score = 0)
    //     {
    //         // 先构造一个完整的 FontAtlas 对象（但此时 hb_font 还未初始化）
    //         fonts_.emplace_back(jsonPath, std::move(texture), std::move(face), type, bind,
    //                             FontMetadata{});

    //         auto &newFont = fonts_.back();

    //         // 遍历当前字体的 Unicode→字形信息映射
    //         for (const auto &[ch, glyphPtr] : newFont.unicode_default_glyphs)
    //         {
    //             size_t font_index = fonts_.size() - 1; // 当前字体索引
    //             font_value fv{font_index, score};
    //             auto it = unicodeDefaultFont_.find(ch);
    //             if (it == unicodeDefaultFont_.end())
    //             {
    //                 // 首次出现该 Unicode，创建新的有序集合
    //                 std::set<font_value, font_value_greater> s;
    //                 s.insert(fv);
    //                 unicodeDefaultFont_.emplace(ch, std::move(s));
    //             }
    //             else
    //             {
    //                 // 已存在，直接插入（set 会自动排序并去重）
    //                 it->second.insert(fv);
    //             }
    //         }

    //         std::println("Font {} loaded, glyph_index_to_glyphs size: {},  "
    //                      "unicode_default_glyphs size: {}",
    //                      newFont.name, newFont.glyph_index_to_glyphs.size(),
    //                      newFont.unicode_default_glyphs.size());
    //         if (newFont.unicode_default_glyphs.empty())
    //         {
    //             std::println("Warning: unicode_default_glyphs is empty for font {}",
    //                          newFont.name);
    //         }
    //     }
    //     [[nodiscard]] std::optional<font_value> getFont(char32_t unicode) const noexcept
    //     {
    //         auto it = unicodeDefaultFont_.find(unicode);
    //         if (it != unicodeDefaultFont_.end() && !it->second.empty())
    //             return *it->second.begin(); // 返回集合中第一个元素（最高分字体）

    //         return std::nullopt;
    //     }

    //     // 从 fonts_ 集合 得到 texture_image_base 的 views 集合
    //     [[nodiscard]] auto getTextureImageBases() const noexcept
    //     {
    //         return fonts_ | std::views::transform(
    //                             [](const FontContext &atlas) noexcept -> const auto & {
    //                                 return atlas.texture;
    //                             });
    //     }
    // };
}; // namespace font

// diff: [Uniform] start
class UniformBufferObject
{
  public:
    // glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};
// diff: [Uniform] end

// diff: [texture] start

void generateCheckerboardTexture(uint8_t *pixels, int width, int height,
                                 uint8_t color1[4], uint8_t color2[4],
                                 int checkerSize = 32)
{
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            int tileX = x / checkerSize;
            int tileY = y / checkerSize;
            bool isEven = ((tileX + tileY) % 2) == 0;

            uint8_t *pixel = &pixels[(y * width + x) * 4];
            if (isEven)
            {
                pixel[0] = color1[0];
                pixel[1] = color1[1];
                pixel[2] = color1[2];
                pixel[3] = color1[3];
            }
            else
            {
                pixel[0] = color2[0];
                pixel[1] = color2[1];
                pixel[2] = color2[2];
                pixel[3] = color2[3];
            }
        }
    }
}

void generateGradientTexture(uint8_t *pixels, int width, int height, uint8_t topColor[4],
                             uint8_t bottomColor[4])
{
    for (int y = 0; y < height; ++y)
    {
        float t = static_cast<float>(y) / (height - 1);
        for (int x = 0; x < width; ++x)
        {
            uint8_t *pixel = &pixels[(y * width + x) * 4];

            pixel[0] = static_cast<uint8_t>(topColor[0] * (1 - t) + bottomColor[0] * t);
            pixel[1] = static_cast<uint8_t>(topColor[1] * (1 - t) + bottomColor[1] * t);
            pixel[2] = static_cast<uint8_t>(topColor[2] * (1 - t) + bottomColor[2] * t);
            pixel[3] = 255;
        }
    }
}

enum class perspective_member : std::uint8_t
{
    eFOVY,
    eASPECT,
    eZ_NEAR,
    eZ_FAR
};
// NOTE: WS 依旧有效，超过+-10就完蛋
template <perspective_member member>
auto update_perspective(camera_interface &camera, float step) noexcept
{
    using enum perspective_member;
    if (member == eFOVY)
        camera.refPerspectiveMatrix().fovy += step;
    else if (member == eASPECT)
        camera.refPerspectiveMatrix().aspect += step;
    else if (member == eZ_NEAR)
        camera.refPerspectiveMatrix().zNear += step;
    else
        camera.refPerspectiveMatrix().zFar += step;

    auto [fovy, aspect, zNear, zFar] = camera.refPerspectiveMatrix();
    std::print("Key::eW: [fovy: {:f},aspect: {:f},zNear: {:f},zFar: {:f}]\n", fovy,
               aspect, zNear, zFar);
}
// diff: [camera_perspective] end

// diff: [camera_model] start
struct model_matrix
{
    // NOLINTBEGIN
    glm::vec3 translation = glm::vec3(0.0F, 0.0F, 0.0F);    // 平移（默认无偏移）
    glm::quat rotation = glm::quat(1.0F, 0.0F, 0.0F, 0.0F); // 旋转（默认无旋转）
    glm::vec3 scale = glm::vec3(1.0F, 1.0F, 1.0F);          // 缩放（默认无缩放）
    // NOLINTEND

    // ========== 核心：计算最终的model矩阵（固定顺序：缩放→旋转→平移） ==========
    constexpr auto operator()() const noexcept
    {
        // 1. 缩放 → 2. 旋转 → 3. 平移（3D变换的标准顺序）
        auto model = glm::mat4(1.0F);
        // 平移
        model = glm::translate(model, translation);
        // 旋转（四元数转矩阵）
        model = model * glm::toMat4(rotation);
        // 缩放
        model = glm::scale(model, scale);
        return model;
    }
};

// diff: [camera_model] end

//diff: [test_dod8] start
struct object_data
{
    glm::mat4 matrix;
};
struct UvTransform
{
    glm::vec2 scale{1.0f, 1.0f};
    glm::vec2 offset{0.0f, 0.0f};

    // 默认：完整纹理
    constexpr static UvTransform identity() noexcept
    {
        return {};
    }

    // 从目标矩形的四个纹理坐标构造
    static UvTransform from_target_verts(std::span<const glm::vec2> target)
    {
        // 对轴对齐矩形直接取边界
        float l = std::min({target[0].x, target[1].x, target[2].x, target[3].x});
        float r = std::max({target[0].x, target[1].x, target[2].x, target[3].x});
        float b = std::min({target[0].y, target[1].y, target[2].y, target[3].y});
        float t = std::max({target[0].y, target[1].y, target[2].y, target[3].y});
        return {.scale = {r - l, t - b}, .offset = {l, b}};
    }

    // 也可以接受 origin（默认为标准 [0,1]²）和目标，用于完整仿射
    static UvTransform from_verts(std::span<const glm::vec2> origin,
                                  std::span<const glm::vec2> target)
    {
        // 与 from_target_verts 类似，但考虑 origin 缩放
        float ol = std::min({origin[0].x, origin[1].x, origin[2].x, origin[3].x});
        float or_ = std::max({origin[0].x, origin[1].x, origin[2].x, origin[3].x});
        float ob = std::min({origin[0].y, origin[1].y, origin[2].y, origin[3].y});
        float ot = std::max({origin[0].y, origin[1].y, origin[2].y, origin[3].y});

        float tl = std::min({target[0].x, target[1].x, target[2].x, target[3].x});
        float tr = std::max({target[0].x, target[1].x, target[2].x, target[3].x});
        float tb = std::min({target[0].y, target[1].y, target[2].y, target[3].y});
        float tt = std::max({target[0].y, target[1].y, target[2].y, target[3].y});

        glm::vec2 originSize{or_ - ol, ot - ob};
        glm::vec2 targetSize{tr - tl, tt - tb};
        glm::vec2 originBase{ol, ob};
        glm::vec2 targetBase{tl, tb};

        glm::vec2 scale = targetSize / originSize;
        glm::vec2 offset = targetBase - originBase * scale;

        return {scale, offset};
    }
};

struct InstanceData
{
    uint32_t objectId;
    uint32_t textureIndex; // 纹理数组索引
    uint32_t samplerIndex; // 采样器数组索引
    object_data objectData;
    UvTransform uvTransform;

    //diff: [test_dod12] start
    uint32_t fontType = static_cast<uint32_t>(font::FontType::eNONE);
    float pxRange;         //msdf 需要
    uint32_t modulateFlag; // 混合需要
    //diff: [test_dod12] end
};
struct CommandConstant
{
    uint32_t perInstanceAttributeCount;
};
struct PushData
{
    uint64_t vertexAddress;           // 全局顶点缓冲区地址
    uint64_t attributeAddress;        // 全局属性池地址
    uint64_t instanceAddress;         // 全局实例缓冲区地址
    uint64_t commandConstantsAddress; // 新增：命令常量缓冲区地址
};

struct Mesh
{
    uint32_t vertexCount;  // 网格的顶点数量
    uint32_t vertexOffset; // 在全局顶点池中的偏移
    uint32_t indexOffset;  // 在全局索引池中的偏移
    uint32_t indexCount;   // 索引数量

    [[nodiscard]] VkDrawIndexedIndirectCommand getDrawCommand(
        uint32_t instanceCount, uint32_t firstInstance = 0) const noexcept
    {
        return {.indexCount = indexCount,
                .instanceCount = instanceCount,
                .firstIndex = indexOffset,
                .vertexOffset = static_cast<int32_t>(vertexOffset),
                .firstInstance = firstInstance};
    }
};

struct PerFrameBatch
{
    mcs::vulkan::memory::auto_map_buffer globalVertexBuffer{};
    VkDeviceSize globalVertexBufferCapacity = 0;
    VkDeviceAddress globalVertexBufferAddress = 0;

    mcs::vulkan::memory::auto_map_buffer globalIndexBuffer{};
    VkDeviceSize globalIndexBufferCapacity = 0;

    mcs::vulkan::memory::auto_map_buffer globalInstanceBuffer{};
    VkDeviceSize globalInstanceBufferCapacity = 0;
    VkDeviceAddress globalInstanceBufferAddress = {};

    mcs::vulkan::memory::auto_map_buffer globalAttributeBuffer{};
    VkDeviceSize globalAttributeBufferCapacity = 0;
    VkDeviceAddress globalAttributeBufferAddress = 0;

    mcs::vulkan::memory::auto_map_buffer indirectDrawBuffer{};
    VkDeviceSize indirectDrawBufferCapacity = 0;

    // 新增：命令常量缓冲区
    mcs::vulkan::memory::auto_map_buffer commandConstantsBuffer{};
    VkDeviceAddress commandConstantsBufferAddress = 0;

    uint32_t draw3DCount = 0;
    uint32_t drawCountUI = 0;
};
//diff: [test_dod8] end

//diff: [test_indirectdraw2] start
// NOLINTBEGIN

using mcs::vulkan::load::raw_stbi_image;

using Sampler = mcs::vulkan::Sampler;
using create_sampler = mcs::vulkan::tool::create_sampler;

auto generateGradientTexture(const LogicalDevice &device, const CommandPool &pool,
                             const Queue &queue, int textureType = 0, bool mipmap = true)
{
    auto texWidth_ = 256;
    auto texHeight_ = 256;

    mcs::vulkan::memory::create_texture create_texture{
        [](mcs::vulkan::memory::create_texture::image_info imageInfo)
            -> mcs::vulkan::memory::create_texture::create_info {
            return {.imageType = VK_IMAGE_TYPE_2D,
                    .format = VK_FORMAT_R8G8B8A8_SRGB,
                    .extent = {.width = imageInfo.extent.width,
                               .height = imageInfo.extent.height,
                               .depth = 1},
                    .mipLevels = imageInfo.mipLevels,
                    .arrayLayers = 1,
                    .samples = VK_SAMPLE_COUNT_1_BIT,
                    .tiling = VK_IMAGE_TILING_OPTIMAL,
                    .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                             VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
        },
        mcs::vulkan::memory::gen_memory_allocate_info(
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
        [](VkImageCreateInfo imageCreateInfo,
           VkImage image) -> mcs::vulkan::memory::create_texture::view_create_info {
            return {.image = image,
                    .viewType = VK_IMAGE_VIEW_TYPE_2D,
                    .format = imageCreateInfo.format,
                    .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                         .baseMipLevel = 0,
                                         .levelCount = imageCreateInfo.mipLevels,
                                         .baseArrayLayer = 0,
                                         .layerCount = 1}};
        }};

    auto imageSize = texWidth_ * texHeight_ * STBI_rgb_alpha;
    auto pixels = std::make_unique_for_overwrite<uint8_t[]>(imageSize);
    if (textureType == 1)
    {
        // 生成红蓝棋盘纹理
        uint8_t color1[4] = {255, 0, 0, 255}; // 红色
        uint8_t color2[4] = {0, 0, 255, 255}; // 蓝色
        generateCheckerboardTexture(pixels.get(), texWidth_, texHeight_, color1, color2);
    }
    else if (textureType == 2)
    {
        // 生成绿色到黄色渐变纹理
        uint8_t topColor[4] = {0, 255, 0, 255};      // 绿色
        uint8_t bottomColor[4] = {255, 255, 0, 255}; // 黄色
        generateGradientTexture(pixels.get(), texWidth_, texHeight_, topColor,
                                bottomColor);
    }
    return create_texture.build(
        device, pool, queue,
        mcs::vulkan::memory::create_texture::image_info{
            .extent = {.width = static_cast<uint32_t>(texWidth_),
                       .height = static_cast<uint32_t>(texHeight_)},
            .pixels =
                std::span<const uint8_t>{pixels.get(), static_cast<uint64_t>(imageSize)},
            .mipLevels = mipmap ? mcs::vulkan::memory::create_texture::getMipLevels(
                                      texWidth_, texHeight_)
                                : 1});
}
// NOLINTEND
//diff: [test_indirectdraw2] end

//diff: [test_dod4] start
consteval int parse_int(std::string_view sv)
{
    int result = 0;
    for (char c : sv)
    {
        if (c < '0' || c > '9')
            throw std::meta::exception{"sv is not number", std::meta::current_function()};
        result = result * 10 + (c - '0');
    }
    return result;
}
template <mcs::vulkan::ecs::static_string prefix, size_t min, size_t max>
constexpr void invoke_aggregate_ranges(auto &object, auto &&...args)
{
    using T = std::remove_cvref_t<decltype(object)>;
    constexpr auto members = T::members; //NOTE: 来自继承的
    template for (constexpr auto I : std::views::indices(members.size()))
    {
        constexpr auto member_name = T::template get_member_name<I>().view();
        constexpr auto prefix_name = prefix.view();
        if constexpr (member_name.starts_with(prefix_name))
        {
            constexpr auto num_str = member_name.substr(prefix_name.size());
            constexpr int num = parse_int(num_str);
            if constexpr (num >= min && num <= max)
                object.[:members[I]:](std::forward<decltype(args)>(args)...);
        }
    }
};
//diff: [test_dod4] end

int main()
try
{
#ifdef VERT_SHADER_PATH
    std::cout << "VERT_SHADER_PATH: " << VERT_SHADER_PATH << '\n';
#endif
#ifdef FRAG_SHADER_PATH
    std::cout << "FRAG_SHADER_PATH: " << FRAG_SHADER_PATH << '\n';
#endif
    raii_vulkan ctx{};
    surface window{};
    window.setup({.width = WIDTH, .height = HEIGHT}, TITLE); // NOLINT

    constexpr auto APIVERSION = VK_API_VERSION_1_4;
    static_assert(vkApiVersion(1, 4, 0) == VK_API_VERSION_1_4);
    static_assert(vkApiVersion(0, 1, 4, 0) == VK_API_VERSION_1_4);

    auto enables = enable_intance_build{}
                       .enableDebugExtension()
                       .enableValidationLayer()
                       .enableSurfaceExtension<surface>();

    enables.check();
    enables.print();

    Instance instance =
        create_instance{}
            .setCreateInfo(
                {.applicationInfo = {.pApplicationName = "Hello Triangle",
                                     .applicationVersion = vkMakeVersion(1, 0, 0),
                                     .pEngineName = "No Engine",
                                     .engineVersion = vkMakeVersion(1, 0, 0),
                                     .apiVersion = APIVERSION},
                 .enabledLayers = enables.enabledLayers(),
                 .enabledExtensions = enables.enabledExtensions()})
            .build();
    auto debuger = create_debugger{}
                       .setCreateInfo(create_debugger::defaultCreateInfo())
                       .build(instance);

    std::vector<const char *> requiredDeviceExtension = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_SPIRV_1_4_EXTENSION_NAME,
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
        VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME}; // NOLINTEND
    requiredDeviceExtension.emplace_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
    structure_chain<VkPhysicalDeviceFeatures2, VkPhysicalDeviceVulkan13Features,
                    VkPhysicalDeviceVulkan12Features, VkPhysicalDeviceVulkan11Features,
                    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT>
        enablefeatureChain = {
            {.features =
                 {
                     /*
                    当不同附件的混合状态（包括 colorWriteMask）不同时，即使所有附件的 blendEnable 都是 VK_FALSE，也必须启用 independentBlend 特性，否则就是非法。
                    你的两个附件 colorWriteMask 不同（一个包含 A，一个只有 R|G），因此需要该特性
                    */
                     //diff: [test_indirectdraw_no_pick] start
                     .independentBlend = VK_TRUE, // 新增
                     //diff: [test_indirectdraw_no_pick] end
                     //NOTE: gcc 要求。严格的初始化顺序。 multiDrawIndirect 要在前面
                     .multiDrawIndirect = VK_TRUE, //diff: [test_indirectdraw]
                     .samplerAnisotropy = VK_TRUE,
                     .shaderInt64 = VK_TRUE,

                 }},
            {.synchronization2 = VK_TRUE, .dynamicRendering = VK_TRUE},
            {
                .descriptorIndexing = VK_TRUE,
                .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
                .descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
                .descriptorBindingStorageImageUpdateAfterBind = VK_TRUE,
                .descriptorBindingPartiallyBound = VK_TRUE,
                .descriptorBindingVariableDescriptorCount = VK_TRUE,

                .runtimeDescriptorArray = VK_TRUE,

                .scalarBlockLayout = VK_TRUE,
                .bufferDeviceAddress = VK_TRUE,

            },
            {
                .shaderDrawParameters = VK_TRUE //diff: [test_indirectdraw]
            },
            {.extendedDynamicState = VK_TRUE}};

    auto [id [[maybe_unused]], physical_device [[maybe_unused]]] =
        physical_device_selector{instance}
            .requiredDeviceExtension(requiredDeviceExtension)
            .requiredProperties([](const VkPhysicalDeviceProperties
                                       &device_properties) constexpr noexcept {
                return device_properties.apiVersion >= VK_API_VERSION_1_3;
            })
            .requiredQueueFamily(
                [](const VkQueueFamilyProperties &qfp) constexpr noexcept {
                    return !!(qfp.queueFlags & VK_QUEUE_GRAPHICS_BIT);
                })
            .requiredFeatures([](const PhysicalDevice &physicalDevice) constexpr noexcept
                                  -> bool {
                auto query = structure_chain<
                    VkPhysicalDeviceFeatures2, VkPhysicalDeviceVulkan13Features,
                    VkPhysicalDeviceVulkan12Features, VkPhysicalDeviceVulkan11Features,
                    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT>{{}, {}, {}, {}, {}};
                physicalDevice.getFeatures2(&query.head());
                auto &features2 = query.template get<VkPhysicalDeviceFeatures2>();
                auto &query_vulkan13_features =
                    query.template get<VkPhysicalDeviceVulkan13Features>();
                auto &query_vulkan12_features =
                    query.template get<VkPhysicalDeviceVulkan12Features>();
                auto &query_vulkan11_features =
                    query.template get<VkPhysicalDeviceVulkan11Features>();
                auto &query_extended_dynamic_state_features =
                    query.template get<VkPhysicalDeviceExtendedDynamicStateFeaturesEXT>();
                return features2.features.samplerAnisotropy &&
                       features2.features.shaderInt64 &&
                       features2.features.multiDrawIndirect && //diff: [test_indirectdraw]
                       features2.features
                           .independentBlend && //diff: [test_indirectdraw_no_pick]
                       query_vulkan13_features.dynamicRendering &&
                       query_vulkan13_features.synchronization2 &&
                       query_vulkan12_features.bufferDeviceAddress &&
                       query_vulkan12_features.scalarBlockLayout &&
                       query_vulkan12_features.runtimeDescriptorArray &&
                       query_vulkan12_features.descriptorBindingPartiallyBound &&
                       query_vulkan12_features.descriptorBindingVariableDescriptorCount &&
                       query_vulkan12_features.descriptorIndexing &&
                       query_vulkan12_features
                           .shaderSampledImageArrayNonUniformIndexing &&
                       query_vulkan11_features
                           .shaderDrawParameters && //diff: [test_indirectdraw]
                       query_extended_dynamic_state_features.extendedDynamicState;
            })
            .select()[0];

    mcs::vulkan::surface auto surface = surface_impl(physical_device, window);
    const uint32_t GRAPHICS_QUEUE_FAMILY_IDX =
        queue_family_index_selector{physical_device}
            .requiredQueueFamily([&](const VkQueueFamilyProperties &qfp,
                                     uint32_t queueFamilyIndex) -> bool {
                return (qfp.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                       physical_device.getSurfaceSupportKHR(queueFamilyIndex, *surface);
            })
            .select()[0];

    LogicalDevice device =
        create_logical_device{}
            .setCreateInfo({
                .pNext = make_pNext(enablefeatureChain),
                .queueCreateInfos = create_logical_device::makeQueueCreateInfos(
                    create_logical_device::queue_create_info{
                        .queueFamilyIndex = GRAPHICS_QUEUE_FAMILY_IDX,
                        .queueCount = 1,
                        .queuePrioritie = 1.0}),
                .enabledExtensions = requiredDeviceExtension,
            })
            .build(physical_device);
    requiredDeviceExtension.clear();

    const auto GRAPHICS_AND_PRESENT = Queue(
        device, {.queue_family_index = GRAPHICS_QUEUE_FAMILY_IDX, .queue_index = 0});

    auto swapchainBuild =
        create_swap_chain{surface, device}
            .setCreateInfo(
                {.changeMinImageCount =
                     [](uint32_t minImageCount) noexcept { return minImageCount + 1; },
                 .candidateSurfaceFormats = {{.format = VK_FORMAT_B8G8R8A8_SRGB,
                                              .colorSpace =
                                                  VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}},
                 .imageArrayLayers = 1,
                 .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                 .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
                 .queueFamilyIndices = {},
                 .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
                 .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                 .candidatePresentModes = {VK_PRESENT_MODE_MAILBOX_KHR,
                                           VK_PRESENT_MODE_IMMEDIATE_KHR},
                 .clipped = VK_TRUE})
            .setViewCreateInfo(
                {.viewType = VK_IMAGE_VIEW_TYPE_2D,
                 .components = {.r = VK_COMPONENT_SWIZZLE_IDENTITY,
                                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                                .a = VK_COMPONENT_SWIZZLE_IDENTITY},
                 .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                      .baseMipLevel = 0,
                                      .levelCount = 1,
                                      .baseArrayLayer = 0,
                                      .layerCount = 1}});
    auto swapchain = swapchainBuild.build();

    CommandPool commandPool =
        create_command_pool{}
            .setCreateInfo({.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                            .queueFamilyIndex = GRAPHICS_QUEUE_FAMILY_IDX})
            .build(device);

    constexpr int MAX_TEXTURES = 64;       // 预分配最大纹理槽位数（足够大）
    std::set<uint32_t> activeTextureSlots; // 记录当前已占用的槽位
    constexpr int SAMPLER_COUNT = 4;       // 创建2个不同的采样器

    std::array<VkDescriptorBindingFlags, 4> bindingFlags = {
        // 绑定0：Uniform Buffer - 通常不需要绑定后更新
        0,
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT, //允许不连续占用纹理槽（如 1、3、5、33）
        0,
        0, // 对应 binding 3（Storage Buffer）
    };

    auto descriptorSetLayout =
        create_descriptor_set_layout{}
            .setCreateInfo(
                {.pNext = make_pNext(
                     structure_chain<VkDescriptorSetLayoutBindingFlagsCreateInfo>{
                         {.bindingCount = static_cast<uint32_t>(bindingFlags.size()),
                          .pBindingFlags = bindingFlags.data()}}),
                 .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
                 .bindings =
                     {
                         VkDescriptorSetLayoutBinding{
                             .binding = 0,
                             .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                             .descriptorCount = 1,
                             .stageFlags = VK_SHADER_STAGE_VERTEX_BIT},
                         VkDescriptorSetLayoutBinding{
                             .binding = 1,
                             .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                             .descriptorCount = MAX_TEXTURES,
                             .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                             .pImmutableSamplers = nullptr,
                         },
                         VkDescriptorSetLayoutBinding{
                             .binding = 2,
                             .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                             .descriptorCount = SAMPLER_COUNT,
                             .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                             .pImmutableSamplers = nullptr,
                         }, //diff: [test_indirectdraw] end
                         VkDescriptorSetLayoutBinding{
                             .binding = 3,
                             .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                             .descriptorCount = 1,
                             .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                         }, //diff: [test_indirectdraw] end
                     }})
            .build(device);

    auto descriptorPool =
        create_descriptor_pool{}
            .setCreateInfo(
                {.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT |
                          VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
                 .maxSets = MAX_FRAMES_IN_FLIGHT,
                 .poolSizes =
                     {
                         VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                              .descriptorCount = MAX_FRAMES_IN_FLIGHT},
                         VkDescriptorPoolSize{
                             .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                             .descriptorCount = MAX_TEXTURES * static_cast<uint32_t>(
                                                                   MAX_FRAMES_IN_FLIGHT),
                         },
                         VkDescriptorPoolSize{
                             .type = VK_DESCRIPTOR_TYPE_SAMPLER,
                             .descriptorCount =
                                 (SAMPLER_COUNT) *
                                 static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT),
                         }, //diff: [test_indirectdraw] start
                         VkDescriptorPoolSize{
                             .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                             .descriptorCount =
                                 MAX_FRAMES_IN_FLIGHT}, //diff: [test_indirectdraw] end
                     }})
            .build(device);
    auto descriptorSets = descriptorPool.allocateDescriptorSets(
        {.descriptorSets = std::vector<VkDescriptorSetLayout>{MAX_FRAMES_IN_FLIGHT,
                                                              *descriptorSetLayout}});
    constexpr VkDeviceSize BUFFER_SIZE = sizeof(UniformBufferObject);
    // diff: [test_dod7] start: 不再是 vma 的内存
    std::array<mcs::vulkan::memory::auto_map_buffer, MAX_FRAMES_IN_FLIGHT> uniformBuffers;
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        uniformBuffers[i] = mcs::vulkan::memory::auto_map_buffer(
            mcs::vulkan::memory::create_simple_buffer(
                device,
                {.size = BUFFER_SIZE,
                 .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                 .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
            BUFFER_SIZE);
    }
    // diff: [test_dod7] end
    uint32_t create_texture_mipLevels = 1;
    mcs::vulkan::memory::create_texture create_texture{
        [&](mcs::vulkan::memory::create_texture::image_info imageInfo)
            -> mcs::vulkan::memory::create_texture::create_info {
            create_texture_mipLevels = imageInfo.mipLevels;
            return {.imageType = VK_IMAGE_TYPE_2D,
                    .format = VK_FORMAT_R8G8B8A8_SRGB,
                    .extent = {.width = imageInfo.extent.width,
                               .height = imageInfo.extent.height,
                               .depth = 1},
                    .mipLevels = imageInfo.mipLevels,
                    .arrayLayers = 1,
                    .samples = VK_SAMPLE_COUNT_1_BIT,
                    .tiling = VK_IMAGE_TILING_OPTIMAL,
                    .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                             VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
        },
        mcs::vulkan::memory::gen_memory_allocate_info(
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
        [](VkImageCreateInfo imageCreateInfo,
           VkImage image) -> mcs::vulkan::memory::create_texture::view_create_info {
            return {.image = image,
                    .viewType = VK_IMAGE_VIEW_TYPE_2D,
                    .format = imageCreateInfo.format,
                    .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                         .baseMipLevel = 0,
                                         .levelCount = imageCreateInfo.mipLevels,
                                         .baseArrayLayer = 0,
                                         .layerCount = 1}};
        }};

    std::map<uint32_t, mcs::vulkan::memory::resource> textureMap; // 槽位 → 纹理
    {
        raw_stbi_image{"textures/texture.jpg", STBI_rgb_alpha};

        textureMap.emplace(0, create_texture.templateForImage2d(
                                  device, commandPool, GRAPHICS_AND_PRESENT,
                                  raw_stbi_image{"textures/texture.jpg", STBI_rgb_alpha},
                                  true));
    }
    activeTextureSlots.insert(0);

    textureMap.emplace(3,
                       generateGradientTexture(device, commandPool, GRAPHICS_AND_PRESENT,
                                               1)); // 棋盘
    activeTextureSlots.insert(3);

    textureMap.emplace(9,
                       generateGradientTexture(device, commandPool, GRAPHICS_AND_PRESENT,
                                               2)); // 渐变
    activeTextureSlots.insert(9);

    // 创建纯白纹理（槽 1）
    static constexpr auto UITextureIndex = 1;
    {
        uint32_t white = 0xFFFFFFFF;
        auto tex =
            create_texture.build(device, commandPool, GRAPHICS_AND_PRESENT,
                                 mcs::vulkan::memory::create_texture::image_info{
                                     .extent = {.width = 1, .height = 1},
                                     .pixels = std::span<const uint8_t>(
                                         reinterpret_cast<const uint8_t *>(&white), 4),
                                     .mipLevels = 1});
        textureMap.emplace(UITextureIndex, std::move(tex));
        activeTextureSlots.insert(UITextureIndex);
    }

    std::vector<Sampler> samplers;
    samplers.reserve(SAMPLER_COUNT);
    samplers.emplace_back(
        create_sampler{}
            .setCreateInfo(create_sampler::templateLinear())
            .enableAnisotropy(
                device.physicalDevice()->getProperties().limits.maxSamplerAnisotropy)
            .updateMaxLodByMipmap(create_texture_mipLevels)
            .build(device)); // 线性采样器
    samplers.emplace_back(create_sampler{}
                              .setCreateInfo(create_sampler::templateNearest())
                              .build(device)); // 最近邻采样器

    // diff: [test_dod12] start
    // NOTE: 字体的采样器
    {
        // 采样器类型2：线性过滤，重复寻址，各向异性
        samplers.emplace_back(
            create_sampler{}
                .setCreateInfo(
                    {.pNext = {},
                     .flags = {},
                     .magFilter = VK_FILTER_LINEAR,
                     .minFilter = VK_FILTER_LINEAR,
                     .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                     .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                     .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                     .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                     .mipLodBias = 0,
                     .anisotropyEnable = VK_TRUE, // NOTE: 各向异性器件特性启用
                     .maxAnisotropy =
                         physical_device.getProperties().limits.maxSamplerAnisotropy,
                     .compareEnable = VK_FALSE,
                     .compareOp = VK_COMPARE_OP_ALWAYS,
                     .minLod = 0,
                     .maxLod =
                         VK_LOD_CLAMP_NONE, // NOTE: vulkan 内置最大值，就能适配一切mip
                     .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
                     .unnormalizedCoordinates = VK_FALSE})
                .build(device));

        // 采样器类型1：最近邻过滤，钳位到边缘
        samplers.emplace_back(
            create_sampler{}
                .setCreateInfo({.pNext = {},
                                .flags = {},
                                .magFilter = VK_FILTER_NEAREST,
                                .minFilter = VK_FILTER_NEAREST,
                                .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                                .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                .mipLodBias = 0,
                                .anisotropyEnable = VK_FALSE, // NOTE: 无各向异性器
                                .maxAnisotropy = 1,
                                .compareEnable = VK_FALSE,
                                .compareOp = VK_COMPARE_OP_ALWAYS,
                                .minLod = 0,
                                .maxLod = VK_LOD_CLAMP_NONE,
                                .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
                                .unnormalizedCoordinates = VK_FALSE})
                .build(device));
    }
    // diff: [test_dod12] end

    //diff: [test_dod12] start: 加载字符纹理
    auto pre = std::string{MSDF_OUTPUT_DIR};
    const std::string TEXTURE_PATH_0 = pre + "/english_atlas.png";
    const std::string JSON_PATH_0 = pre + "/english_atlas.json";
    const std::string FONT_PATH_0 = pre + "/english_atlas.ttf";

    const std::string TEXTURE_PATH_1 = pre + "/msyh_chinese.png";
    const std::string JSON_PATH_1 = pre + "/msyh_chinese.json";
    const std::string FONT_PATH_1 = pre + "/msyh_chinese.ttc";

    const std::string TEXTURE_PATH_2 = pre + "/emoji.png";
    const std::string JSON_PATH_2 = pre + "/emoji.json";
    const std::string FONT_PATH_2 = pre + "/emoji.ttf";

    const std::string TEXTURE_PATH_3 = pre + "/arial_all.png";
    const std::string JSON_PATH_3 = pre + "/arial_all.json";
    const std::string FONT_PATH_3 = pre + "/arial_all.ttf";

    const std::string TEXTURE_PATH_4 = pre + "/missing_char.png";
    const std::string JSON_PATH_4 = pre + "/missing_char.json";
    const std::string FONT_PATH_4 = pre + "/missing_char.ttf";

    constexpr auto texture_indexs = std::array{10, 11, 12, 13, 14};
    constexpr auto texture_sampler_indexs = std::array{2, 3};

    // 添加字体
    font::freetype_loader loader{};
    std::vector<font::FontInfo> registe_fonts =
        font::font_register::makeFontInfos(
            loader,
            {
                //
                font::font_registration{
                    .font_path = FONT_PATH_0,
                    .json_path = JSON_PATH_0,
                    .type = font::FontType::eMSDF,
                    .texture_info = {.bind = {.texture_index = texture_indexs[0],
                                              .sampler_index = texture_sampler_indexs[0]},
                                     .image_variant =
                                         font::texture_info::stbi_image_type{
                                             .image_format = STBI_rgb_alpha,
                                             .image_path = TEXTURE_PATH_0}}},
                font::font_registration{
                    .font_path = FONT_PATH_1,
                    .json_path = JSON_PATH_1,
                    .type = font::FontType::eMSDF,
                    .texture_info = {.bind = {.texture_index = texture_indexs[1],
                                              .sampler_index = texture_sampler_indexs[0]},
                                     .image_variant =
                                         font::texture_info::stbi_image_type{
                                             .image_format = STBI_rgb_alpha,
                                             .image_path = TEXTURE_PATH_1}}},
                font::font_registration{
                    .font_path = FONT_PATH_2,
                    .json_path = JSON_PATH_2,
                    .type = font::FontType::eBITMAP,
                    .texture_info = {.bind = {.texture_index = texture_indexs[2],
                                              .sampler_index = texture_sampler_indexs[0]},
                                     .image_variant =
                                         font::texture_info::stbi_image_type{
                                             .image_format = STBI_rgb_alpha,
                                             .image_path = TEXTURE_PATH_2}}},
                font::font_registration{
                    .font_path = FONT_PATH_3,
                    .json_path = JSON_PATH_3,
                    .type = font::FontType::eMSDF,
                    .texture_info = {.bind = {.texture_index = texture_indexs[3],
                                              .sampler_index = texture_sampler_indexs[0]},
                                     .image_variant =
                                         font::texture_info::stbi_image_type{
                                             .image_format = STBI_rgb_alpha,
                                             .image_path = TEXTURE_PATH_3}}},
                font::font_registration{
                    .font_path = FONT_PATH_4,
                    .json_path = JSON_PATH_4,
                    .type = font::FontType::eMSDF,
                    .texture_info = {.bind = {.texture_index = texture_indexs[4],
                                              .sampler_index = texture_sampler_indexs[0]},
                                     .image_variant =
                                         font::texture_info::stbi_image_type{
                                             .image_format = STBI_rgb_alpha,
                                             .image_path = TEXTURE_PATH_4}}},
            });

    auto font_factory = font::make_font_factory(
        [&device, &commandPool, &GRAPHICS_AND_PRESENT](const font::FontInfo &info) {
            mcs::vulkan::memory::create_texture create_font_texture{
                [](mcs::vulkan::memory::create_texture::image_info imageInfo)
                    -> mcs::vulkan::memory::create_texture::create_info {
                    return {.imageType = VK_IMAGE_TYPE_2D,
                            .format = VK_FORMAT_R8G8B8A8_SRGB,
                            .extent = {.width = imageInfo.extent.width,
                                       .height = imageInfo.extent.height,
                                       .depth = 1},
                            .mipLevels = imageInfo.mipLevels,
                            .arrayLayers = 1,
                            .samples = VK_SAMPLE_COUNT_1_BIT,
                            .tiling = VK_IMAGE_TILING_OPTIMAL,
                            .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                     VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                     VK_IMAGE_USAGE_SAMPLED_BIT,
                            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
                },
                mcs::vulkan::memory::gen_memory_allocate_info(
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
                [](VkImageCreateInfo imageCreateInfo, VkImage image)
                    -> mcs::vulkan::memory::create_texture::view_create_info {
                    return {.image = image,
                            .viewType = VK_IMAGE_VIEW_TYPE_2D,
                            .format = imageCreateInfo.format,
                            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                 .baseMipLevel = 0,
                                                 .levelCount = imageCreateInfo.mipLevels,
                                                 .baseArrayLayer = 0,
                                                 .layerCount = 1}};
                }};

            using stbi_image_type = mcs::vulkan::font::texture_info::stbi_image_type;
            const auto &registration = info.registration;
            if (std::holds_alternative<stbi_image_type>(
                    registration.texture_info.image_variant))
            {
                using image_type = stbi_image_type::type;
                const auto &imageInfo =
                    std::get<stbi_image_type>(registration.texture_info.image_variant);
                auto image =
                    image_type{imageInfo.image_path.data(), imageInfo.image_format};
                auto texWidth = image.width();
                auto texHeight = image.height();
                auto imageSize = image.size();

                return create_font_texture.build(
                    device, commandPool, GRAPHICS_AND_PRESENT,
                    mcs::vulkan::memory::create_texture::image_info{
                        .extent = {.width = static_cast<uint32_t>(texWidth),
                                   .height = static_cast<uint32_t>(texHeight)},
                        .pixels =
                            std::span<const uint8_t>{image.data(),
                                                     static_cast<uint64_t>(imageSize)},
                        .mipLevels = 1});
            }
            throw std::logic_error{"check image_variant"};
        },
        *loader);
    auto font_selct =
        font::FontSelector{&font_factory, "zh-CN"}.load(std::move(registe_fonts));
    font_selct.initNotdefFont();
    assert(font_selct.notdefFont() != nullptr);

    // NOTE: 标记激活 占用
    // activeTextureSlots.insert_range(texture_indexs); //NOTE: 分开。

    namespace font_ns = mcs::vulkan::font;

    //diff: [test_dod12] end

    //NOTE: 不需要保存 VkDescriptorImageInfo。 有textureMap和索引够了
    {
        // 采样器信息：只一份，所有帧共用
        auto samplerInfos =
            samplers | std::views::transform([](const auto &s) constexpr noexcept {
                return VkDescriptorImageInfo{.sampler = *s,
                                             .imageView = nullptr,
                                             .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED};
            }) |
            std::ranges::to<std::vector>();

        // 纹理信息：只一份，所有帧共用
        std::vector<VkDescriptorImageInfo> imageInfos(MAX_TEXTURES);
        for (uint32_t slot : activeTextureSlots)
        {
            imageInfos[slot] = VkDescriptorImageInfo{
                .sampler = nullptr,
                .imageView = textureMap[slot].imageView(),
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        }
        //diff: [test_dod12] start
        auto &selectables = font_selct.selectable();
        for (auto const [index, slot] : std::views::enumerate(texture_indexs))
        {
            imageInfos[slot] = VkDescriptorImageInfo{
                .sampler = nullptr,
                .imageView = selectables[index]->texture.imageView(),
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        }
        //diff: [test_dod12] end

        // 一个循环搞定所有帧的描述符更新
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            std::vector<VkWriteDescriptorSet> writes;

            // 绑定 0: Uniform Buffer
            VkDescriptorBufferInfo uniformInfo{
                .buffer = uniformBuffers[i].buffer(), .offset = 0, .range = BUFFER_SIZE};
            writes.push_back({
                .sType = sType<VkWriteDescriptorSet>(),
                .dstSet = descriptorSets[i],
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &uniformInfo,
            });

            // 绑定 1: Sampled Images（指向全局 imageInfos 的对应槽位）
            for (uint32_t slot : activeTextureSlots)
            {

                writes.push_back({
                    .sType = sType<VkWriteDescriptorSet>(),
                    .dstSet = descriptorSets[i],
                    .dstBinding = 1,
                    .dstArrayElement = slot,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    .pImageInfo =
                        &imageInfos[slot], // 安全：imageInfos 生命周期远长于此处
                });
            }
            //diff: [test_dod12] start
            for (auto const [index, slot] : std::views::enumerate(texture_indexs))
            {

                writes.push_back({
                    .sType = sType<VkWriteDescriptorSet>(),
                    .dstSet = descriptorSets[i],
                    .dstBinding = 1,
                    .dstArrayElement = static_cast<uint32_t>(slot),
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    .pImageInfo =
                        &imageInfos[slot], // 安全：imageInfos 生命周期远长于此处
                });
            }
            //diff: [test_dod12] end

            // 绑定 2: Samplers
            writes.push_back({
                .sType = sType<VkWriteDescriptorSet>(),
                .dstSet = descriptorSets[i],
                .dstBinding = 2,
                .dstArrayElement = 0,
                // NOTE: 上面是 shader的信息。下面是传输的信息
                .descriptorCount = SAMPLER_COUNT,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                .pImageInfo = samplerInfos.data(),
            });
            device.updateDescriptorSets(static_cast<uint32_t>(writes.size()),
                                        writes.data(), 0, nullptr);
        }
    }

    //diff: [test_dod12] start: 解析字符串。生成一些元信息
    constexpr auto ltr = 0; // NOLINT
    constexpr auto rtl = 1; // NOLINT

    constexpr auto rawText =
        u8"我你好世界你好世界你好世界🤣\nW3C (World)👪 ﷲ é \nמעביר את "
        u8"שירותי- ERCIM."; // NOTE: BUG
    auto norm = font_ns::utf8proc::normalize(rawText);
    const std::vector<uint32_t> &codepoints = norm.codepoints;
    font_ns::utf8proc::print_normalized(norm, rawText);

    auto analyze_result = font_ns::bidi::analyze(codepoints, ltr);
    font_ns::bidi::print_bidi_result(codepoints, analyze_result);

    auto test_text_runs = font_ns::assign_fonts(analyze_result, font_selct);
    font_ns::print_text_runs(test_text_runs);

    auto test_shape_result = font_ns::harfbuzz::shape(
        analyze_result.mirrored_codepoints, test_text_runs, font_selct.notdefFont());
    font_ns::harfbuzz::print_shape_result(analyze_result.mirrored_codepoints,
                                          test_shape_result);

    auto break_result = font_ns::libunibreak::analyze_line_breaks(
        analyze_result.mirrored_codepoints, "zh-CN");
    font_ns::libunibreak::print_break_result(break_result,
                                             analyze_result.mirrored_codepoints);

    //diff: [test_dod12] end

    //diff: [test_dod8] start
    std::vector<Vertex> allVertices;
    std::vector<uint32_t> allIndices;
    std::unordered_map<std::string, Mesh> meshMap;
    auto addMesh = [&](const std::string &name, const std::span<const Vertex> &verts,
                       const std::span<const uint32_t> &indices) {
        uint32_t vOff = static_cast<uint32_t>(allVertices.size());
        uint32_t iOff = static_cast<uint32_t>(allIndices.size());
        allVertices.insert(allVertices.end(), verts.begin(), verts.end());
        allIndices.insert(allIndices.end(), indices.begin(), indices.end());
        meshMap[name] = {static_cast<uint32_t>(verts.size()), vOff, iOff,
                         static_cast<uint32_t>(indices.size())};
    };

    // 构建全局几何池
    //diff: [test_dod10] start: 改成 -1 1 更标准一点
    static constexpr std::array<Vertex, 4> quadVerts = {
        Vertex{{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f}}, // 左下
        Vertex{{1.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},  // 右下
        Vertex{{1.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},   // 右上
        Vertex{{-1.0f, 1.0f, 0.0f}, {0.0f, 1.0f}}   // 左上
    };
    // 从目标矩形的左下角 (p0) 和右上角 (p2) 构建实例矩阵
    // 假设源四边形已经是 NDC [-1, 1] 范围，目标矩形也在 NDC 空间. 这个矩阵的作用是：将 NDC 的 [-1, 1] 四边形，映射到目标 NDC 矩形 [p0, p2]。
    static constexpr auto make_ndc_quad_matrix = [](glm::vec2 p0, glm::vec2 p2,
                                                    float z = 0.0f) {
        float cx = (p0.x + p2.x) * 0.5f;
        float cy = (p0.y + p2.y) * 0.5f;
        float sx = (p2.x - p0.x) * 0.5f; // 半宽
        float sy = (p2.y - p0.y) * 0.5f; // 半高
        return glm::translate(glm::mat4(1.0f), glm::vec3(cx, cy, z)) *
               glm::scale(glm::mat4(1.0f), glm::vec3(sx, sy, 1.0f));
    };
    //diff: [test_dod10] end
    constexpr auto quadIdx = std::array<uint32_t, 6>{0, 1, 2, 0, 2, 3};
    addMesh("quad", std::span{quadVerts}, std::span{quadIdx});

    static constexpr auto make_instance_matrix_from_verts =
        [](std::span<const glm::vec3> origin, // 4 个顶点：左下,右下,右上,左上
           std::span<const glm::vec3> target) // 4 个顶点：左下,右下,右上,左上
        constexpr noexcept {
            // 1. 提取 origin 边向量和基
            const glm::vec3 O0 = origin[0];
            const glm::vec3 Uo = origin[1] - O0;
            const glm::vec3 Vo = origin[3] - O0;
            glm::vec3 Wo = glm::cross(Uo, Vo);
            if (glm::length2(Wo) < 1e-12f)
                Wo = glm::vec3(0.0f, 0.0f, 1.0f);
            else
                Wo = glm::normalize(Wo);

            // 2. 提取 target 边向量和基
            const glm::vec3 T0 = target[0];
            const glm::vec3 Ut = target[1] - T0;
            const glm::vec3 Vt = target[3] - T0;
            glm::vec3 Wt = glm::cross(Ut, Vt);
            if (glm::length2(Wt) < 1e-12f)
                Wt = glm::vec3(0.0f, 0.0f, 1.0f);
            else
                Wt = glm::normalize(Wt);

            // 3. 构建基矩阵并计算 A = B_t * inverse(B_o)
            glm::mat3 Bo(Uo, Vo, Wo); // 列分别为 Uo, Vo, Wo
            glm::mat3 Bt(Ut, Vt, Wt);
            glm::mat3 A = Bt * glm::inverse(Bo);

            // 4. 计算平移 t = T0 - A * O0
            glm::vec3 t = T0 - A * O0;

            // 5. 构造 4x4 矩阵 (glm 列主序)
            glm::mat4 M(1.0f);
            M[0] = glm::vec4(A[0], 0.0f); // 第一列
            M[1] = glm::vec4(A[1], 0.0f); // 第二列
            M[2] = glm::vec4(A[2], 0.0f); // 第三列
            M[3] = glm::vec4(t, 1.0f);    // 第四列 (平移)
            return M;
        };

    constexpr uint32_t MAX_INSTANCE_COUNT = 1000;
    constexpr uint32_t MAX_DRAW_CALLS = 10;

    std::array<PerFrameBatch, MAX_FRAMES_IN_FLIGHT> batches;
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        auto &batche = batches[i];
        {
            auto &buffer = batche.globalVertexBuffer;
            auto &capacity = batche.globalVertexBufferCapacity;
            auto &address = batche.globalVertexBufferAddress;
            capacity = allVertices.size() * sizeof(Vertex);
            buffer = mcs::vulkan::memory::auto_map_buffer(
                mcs::vulkan::memory::create_simple_buffer(
                    device,
                    {.size = capacity,
                     .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                     .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
                capacity);
            address = device.getBufferDeviceAddress(
                {.sType = sType<VkBufferDeviceAddressInfo>(), .buffer = buffer.buffer()});
            memcpy(buffer.mapPtr(), allVertices.data(), capacity);
        }
        {
            auto &buffer = batche.globalIndexBuffer;
            auto &capacity = batche.globalIndexBufferCapacity;
            capacity = allIndices.size() * sizeof(uint32_t);
            buffer = mcs::vulkan::memory::auto_map_buffer(
                mcs::vulkan::memory::create_simple_buffer(
                    device,
                    {.size = capacity,
                     .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                     .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
                capacity);
            memcpy(buffer.mapPtr(), allIndices.data(), capacity);
        }
        {
            constexpr VkDeviceSize MAX_INSTANCE_DATA_SIZE =
                MAX_DRAW_CALLS * MAX_INSTANCE_COUNT * sizeof(InstanceData);

            auto &buffer = batche.globalInstanceBuffer;
            auto &capacity = batche.globalInstanceBufferCapacity;
            auto &address = batche.globalInstanceBufferAddress;
            capacity = MAX_INSTANCE_DATA_SIZE;
            buffer = mcs::vulkan::memory::auto_map_buffer(
                mcs::vulkan::memory::create_simple_buffer(
                    device,
                    {.size = capacity,
                     .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                     .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
                capacity);
            address = device.getBufferDeviceAddress(
                {.sType = sType<VkBufferDeviceAddressInfo>(), .buffer = buffer.buffer()});

            // NOTE: 绘制的时候，每帧填充
        }
        {

            constexpr VkDeviceSize MAX_Attribute_SIZE = 1 * 1024 * 1024;

            auto &buffer = batche.globalAttributeBuffer;
            auto &capacity = batche.globalAttributeBufferCapacity;
            auto &address = batche.globalAttributeBufferAddress;
            capacity = MAX_Attribute_SIZE;
            buffer = mcs::vulkan::memory::auto_map_buffer(
                mcs::vulkan::memory::create_simple_buffer(
                    device,
                    {.size = capacity,
                     .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                     .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
                capacity);
            address = device.getBufferDeviceAddress(
                {.sType = sType<VkBufferDeviceAddressInfo>(), .buffer = buffer.buffer()});

            // NOTE: 绘制的时候，每帧填充
        }
        {

            constexpr VkDeviceSize MAX_IndirectDraw_SIZE = 1 * 1024 * 1024;

            auto &buffer = batche.indirectDrawBuffer;
            auto &capacity = batche.indirectDrawBufferCapacity;
            capacity = MAX_IndirectDraw_SIZE;
            buffer = mcs::vulkan::memory::auto_map_buffer(
                mcs::vulkan::memory::create_simple_buffer(
                    device,
                    {.size = capacity,
                     .usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                     .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
                capacity);
            // NOTE: 绘制的时候，每帧填充
        }
        {
            // 新增 command constants buffer
            constexpr VkDeviceSize MAX_CMD_CONST_SIZE =
                MAX_DRAW_CALLS * sizeof(CommandConstant);
            batche.commandConstantsBuffer = mcs::vulkan::memory::auto_map_buffer(
                mcs::vulkan::memory::create_simple_buffer(
                    device,
                    {.size = MAX_CMD_CONST_SIZE,
                     .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                     .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
                MAX_CMD_CONST_SIZE);
            batche.commandConstantsBufferAddress = device.getBufferDeviceAddress(
                {.sType = sType<VkBufferDeviceAddressInfo>(),
                 .buffer = batche.commandConstantsBuffer.buffer()});
        }
    }

    // diff: [test_dod8] end

    auto depthResourcesBuild = mcs::vulkan::memory::build_simple_resource(
        {.imageType = VK_IMAGE_TYPE_2D,
         .format = mcs::vulkan::memory::create_resources::findSupportedFormat(
             physical_device,
             std::array<VkFormat, 3>{VkFormat::VK_FORMAT_D32_SFLOAT,
                                     VkFormat::VK_FORMAT_D32_SFLOAT_S8_UINT,
                                     VkFormat::VK_FORMAT_D24_UNORM_S8_UINT},
             VkImageTiling::VK_IMAGE_TILING_OPTIMAL,
             VkFormatFeatureFlagBits::VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT),
         .extent = {.width = swapchain.refImageExtent().width,
                    .height = swapchain.refImageExtent().height,
                    .depth = 1},
         .mipLevels = 1,
         .arrayLayers = 1,
         .samples = physical_device.getMaxUsableSampleCount(),
         .tiling = VK_IMAGE_TILING_OPTIMAL,
         .usage = VkImageUsageFlagBits::VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
         .sharingMode = VkSharingMode::VK_SHARING_MODE_EXCLUSIVE,
         .initialLayout = VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED},
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        [](VkImageCreateInfo imageCreateInfo, VkImage image) noexcept
            -> mcs::vulkan::memory::create_image::view_create_info {
            return {.image = image,
                    .viewType = VK_IMAGE_VIEW_TYPE_2D,
                    .format = imageCreateInfo.format,
                    .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                                         .baseMipLevel = 0,
                                         .levelCount = 1,
                                         .baseArrayLayer = 0,
                                         .layerCount = 1}};
        });

    auto depthResource = depthResourcesBuild.build(device);
    const auto &depthFormat_ref = depthResourcesBuild.refCreateInfoFormat();
    std::cout << "depthResource hasStencilComponent: "
              << depthResourcesBuild.hasStencilComponent() << '\n';

    auto msaaResourcesBuild = mcs::vulkan::memory::build_simple_resource(
        {.imageType = VK_IMAGE_TYPE_2D,
         .format = swapchainBuild.refImageFormat(),
         .extent = {.width = swapchain.refImageExtent().width,
                    .height = swapchain.refImageExtent().height,
                    .depth = 1},
         .mipLevels = 1,
         .arrayLayers = 1,
         .samples = physical_device.getMaxUsableSampleCount(),
         .tiling = VK_IMAGE_TILING_OPTIMAL,
         .usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
         .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED},
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        [](VkImageCreateInfo imageCreateInfo, VkImage image) noexcept
            -> mcs::vulkan::memory::create_image::view_create_info {
            return {.image = image,
                    .viewType = VK_IMAGE_VIEW_TYPE_2D,
                    .format = imageCreateInfo.format,
                    .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                         .baseMipLevel = 0,
                                         .levelCount = 1,
                                         .baseArrayLayer = 0,
                                         .layerCount = 1}};
        });
    auto msaaResource = msaaResourcesBuild.build(device);

    auto pipelineLayout =
        create_pipeline_layout{}
            .setCreateInfo(
                {.setLayouts = {*descriptorSetLayout},
                 // diff: [test_dod8] 推送常量传说地址更快，没有绑定的开销？
                 .pushConstantRanges = {{.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                                         .offset = 0,
                                         .size = sizeof(PushData)}}})
            .build(device);

    using stage_info = create_graphics_pipeline::stage_info;

    //diff: [test_indirectdraw_no_pick] start
    // 定义两个颜色附件格式
    std::array<VkFormat, 2> mainColorFormats = {
        swapchainBuild.refImageFormat(), // location 0 (swapchain)
        VK_FORMAT_R32G32_UINT            // location 1 (picking)
    }; //diff: [test_indirectdraw_no_pick] end

    auto graphicsPipeline =
        create_graphics_pipeline{}
            .setCreateInfo(
                {.pNext = make_pNext(structure_chain<VkPipelineRenderingCreateInfo>{
                     {//diff: [test_indirectdraw_no_pick] start
                      .colorAttachmentCount =
                          static_cast<uint32_t>(mainColorFormats.size()),
                      .pColorAttachmentFormats = mainColorFormats.data(),
                      //diff: [test_indirectdraw_no_pick] end
                      .depthAttachmentFormat = depthFormat_ref}}),
                 .stages = create_graphics_pipeline::makeStages(
                     stage_info{.stage = VK_SHADER_STAGE_VERTEX_BIT,
                                .filePath = VERT_SHADER_PATH,
                                .pName = "main"},
                     stage_info{.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                                .filePath = FRAG_SHADER_PATH,
                                .pName = "main"}),
                 .vertexInputState = {},
                 .inputAssemblyState = {.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                        .primitiveRestartEnable = VK_FALSE},
                 .tessellationState = {},
                 .viewportState = {.viewports = {VkViewport{}}, .scissors = {VkRect2D{}}},
                 .rasterizationState = {.depthClampEnable = VK_FALSE,
                                        .rasterizerDiscardEnable = VK_FALSE,
                                        .polygonMode = VK_POLYGON_MODE_FILL,
                                        .cullMode = VK_CULL_MODE_NONE,
                                        .frontFace = VK_FRONT_FACE_CLOCKWISE,
                                        .depthBiasEnable = VK_FALSE,
                                        .lineWidth = 1.0F},
                 .multisampleState =
                     {
                         .rasterizationSamples =
                             physical_device.getMaxUsableSampleCount(),
                         .sampleShadingEnable = VK_FALSE,
                         .minSampleShading = 1.0F,
                         .sampleMask = {},
                         .alphaToCoverageEnable = VK_FALSE,
                         .alphaToOneEnable = VK_FALSE,
                     },
                 .depthStencilState = {.depthTestEnable = VK_TRUE,
                                       .depthWriteEnable = VK_TRUE,
                                       .depthCompareOp = VK_COMPARE_OP_LESS,
                                       .depthBoundsTestEnable = VK_FALSE,
                                       .stencilTestEnable = VK_FALSE,
                                       .front = {},
                                       .back = {},
                                       .minDepthBounds = 0.0F,
                                       .maxDepthBounds = 1.0F},
                 .colorBlendState =
                     {.logicOpEnable = VK_FALSE,
                      .logicOp = VkLogicOp::VK_LOGIC_OP_COPY,
                      .attachments =
                          {
                              {
                                  //diff: [test_dod12] start: 附件 0 – 开启混合，支持 MSDF 与普通纹理混合
                                  .blendEnable = VK_TRUE,
                                  .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
                                  .dstColorBlendFactor =
                                      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                  .colorBlendOp = VK_BLEND_OP_ADD,
                                  .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                                  .dstAlphaBlendFactor =
                                      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                  .alphaBlendOp = VK_BLEND_OP_ADD,
                                  .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                                    VK_COLOR_COMPONENT_G_BIT |
                                                    VK_COLOR_COMPONENT_B_BIT |
                                                    VK_COLOR_COMPONENT_A_BIT,
                                  //diff: [test_dod12] end
                              },
                              //diff: [test_indirectdraw_no_pick] start
                              {
                                  // location 1 (R32G32_UINT，不能混合)
                                  .blendEnable = VK_FALSE,
                                  .colorWriteMask =
                                      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT,
                              },
                              //diff: [test_indirectdraw_no_pick] end
                          }},
                 .dynamicState =
                     {.dynamicStates =
                          {
                              VK_DYNAMIC_STATE_VIEWPORT,
                              VK_DYNAMIC_STATE_SCISSOR,
                              // diff: [test_dod10] start: UI的 z 是一样的哦。 所以前面的会被丢弃。我们不能让UI的z相同就丢弃
                              VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
                              VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
                              // diff: [test_dod10] end
                          }},
                 .layout = *pipelineLayout})
            .build(device);

    static constexpr auto MAX_FRAMES_IN_FLIGHT = 2;
    CommandBuffers commandBuffers =
        commandPool.allocateCommandBuffers({.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                            .commandBufferCount = MAX_FRAMES_IN_FLIGHT});
    frame_context<MAX_FRAMES_IN_FLIGHT> frameContext{device, swapchain.imagesSize()};

    // NOLINTBEGIN

    // diff: [test_dod2] start 定义全部数据

    struct model_state
    {
        mcs::vulkan::event::position2d_event last_pos{};
        bool is_middle_button_pressed = false;
        bool is_left_button_pressed = false;
        bool is_right_button_pressed_last = false; // 新增：记录上一帧右键状态
        mcs::vulkan::event::position2d_event last_left_pos{};
        model_matrix model_matrix{};
    };

    // diff: [test_dod2] end

    //diff: [test_dod8.cpp] start
    using auto_spin_data_type =
        gen_soa_aggregate<{"instanceData", ^^InstanceData},
                          {"vertexAttribute", ^^std::array<VertexAttribute, 4>}>;
    auto_spin_data_type autoSpinStore{1};

    using interactive_type =
        gen_soa_aggregate<{"instanceData", ^^InstanceData},
                          {"vertexAttribute", ^^std::array<VertexAttribute, 4>},
                          {"topLeftLocal", ^^glm::vec3}, {"modelState", ^^model_state}>;
    interactive_type interactiveStore{1};

    // NOTE: spinElapsed 全部mesh共享？
    auto autoSpinStoreShareData =
        make_aggregate<"autoSpinStoreShareData", "spinElapsed">(float{});

    //diff: [test_dod10] start

    // 新增 UI store（结构与其他 store 完全一致）
    using ui_data_type =
        gen_soa_aggregate<{"instanceData", ^^InstanceData},
                          {"vertexAttribute", ^^std::array<VertexAttribute, 4>}>;
    ui_data_type uiStore{100};

    using namespace mcs::vulkan::yoga::literals;
    using namespace mcs::vulkan::yoga;
    Node screen{
        "screen",
        Config{YGConfigNew()}.setUseWebDefaults(false).setPointScaleFactor(
            window.getContentScale().xscale),
        Style{.width = {Pixels{WIDTH}}, .height = {Pixels{HEIGHT}}},
        Node{
            "root",
            Style{
                .padding = {10_px},
                .width = {200_px},
                .height = {200_px},
            },
            Node{"child0", Style{.margin = {5_px}, .width = {100_px}, .height = {100_px}},
                 Node{"child0-0",
                      Style{.margin = {5_px}, .width = {50_px}, .height = {50_px}}}},
            Node{"1", Style{.margin = {5_px}, .width = {100_px}, .height = {20_px}}},
        },
        Node{
            "root2",
            Style{
                .padding = {10_px},
                .width = {200_px},
                .height = {200_px},
            },
            Node{"1", Style{.margin = {5_px}, .width = {100_px}, .height = {20_px}}},
        },
        Node{"text_display", Style{
                                 .padding = {10_px},
                                 .width = {200_px},
                                 .height = {200_px},
                             }}};
    window.enableContentScaleCallback([&](void *self, float xscale, float yscale) {
        MCSLOG_INFO("glfw change ContentScaleCallback");
        screen.refConfig().setPointScaleFactor(xscale);
    });

    Layout layout{screen};
    auto on_resize = [&](Layout &layout, layout_size size) {
        layout.root()->refStyle().width = Pixels{size.available_width};
        layout.root()->refStyle().height = Pixels{size.available_height};
        auto &child0 = screen.childrens()[0];
        auto &child1 = screen.childrens()[1];

        constexpr auto test_insert = true;
        if constexpr (not test_insert)
        {
            if (!child0->childrens().empty())
                child1->emplace_back(child0->removeChild(0));
            else
                child0->emplace_back(child1->removeChild(0));
        }
        else
        {
            if (child0->childrens().size() == 2)
                child1->insert(child0->removeChild(0), 0);
            else
                child0->insert(child1->removeChild(0), 0);
        }
        layout.update();
    };
    layout.calculate({.available_width = WIDTH, .available_height = HEIGHT});
    std::println("layout.print [start]: ContentScale: [{},{}]",
                 window.getContentScale().xscale, window.getContentScale().yscale);
    layout.print();
    std::println("layout.print [end]");

    // #define random_z

    auto generateUIInstances = [&](Layout &layout) {
        // uiStore.clear();

        float w = static_cast<float>(swapchain.refImageExtent().width);
        float h = static_cast<float>(swapchain.refImageExtent().height);
        auto pixelToNDC = [=](float px, float py) -> glm::vec2 {
            return {2.0f * px / w - 1.0f, 2.0f * py / h - 1.0f};
        };
        uint32_t global_id = 0;
        const auto traverse = [&uiStore, &pixelToNDC, &global_id, &w,
                               &h](this auto &self, uint32_t cur_id, const Node *node,
                                   float parentLeft, float parentTop) {
            if (!node)
                return;
            auto *ygNode = **node;
            float x = parentLeft + YGNodeLayoutGetLeft(ygNode);
            float y = parentTop + YGNodeLayoutGetTop(ygNode);
            float nw = YGNodeLayoutGetWidth(ygNode);
            float nh = YGNodeLayoutGetHeight(ygNode);

            if (nw > 0 && nh > 0)
            {
                // NDC 坐标
                glm::vec2 p0 = pixelToNDC(x, y);
                glm::vec2 p1 = pixelToNDC(x + nw, y);
                glm::vec2 p2 = pixelToNDC(x + nw, y + nh);
                glm::vec2 p3 = pixelToNDC(x, y + nh);

                // 生成实例矩阵（只负责位置和缩放）
#ifdef random_z
                const auto z = cur_id * 0.0001F;
#else
                const auto z = 0.0F;
#endif //random_z

                glm::mat4 M = make_ndc_quad_matrix(p0, p2, z);

                InstanceData inst{.objectId = cur_id,
                                  .textureIndex = UITextureIndex,
                                  .samplerIndex = 0,
                                  .objectData = object_data{M},
                                  .uvTransform = UvTransform::identity()};

                // 8 组预定义的四角颜色，用于区分 8 个实例
                const std::array<std::array<glm::vec3, 4>, 8> instanceColors = {
                    {// 实例0（id=1000）：红调，四角：红、橙、粉、浅橙
                     {{{1.0f, 0.0f, 0.0f},
                       {1.0f, 0.5f, 0.0f},
                       {1.0f, 0.0f, 0.5f},
                       {1.0f, 0.5f, 0.2f}}},
                     // 实例1（id=1001）：绿调，四角：绿、黄绿、青绿、浅绿
                     {{{0.0f, 1.0f, 0.0f},
                       {0.5f, 1.0f, 0.0f},
                       {0.0f, 1.0f, 0.5f},
                       {0.2f, 1.0f, 0.2f}}},
                     // 实例2（id=1002）：蓝调，四角：蓝、浅蓝、紫蓝、淡紫
                     {{{0.0f, 0.0f, 1.0f},
                       {0.0f, 0.5f, 1.0f},
                       {0.5f, 0.0f, 1.0f},
                       {0.2f, 0.5f, 1.0f}}},
                     // 实例3（id=1003）：黄调，四角：黄、金黄、黄绿、浅黄
                     {{{1.0f, 1.0f, 0.0f},
                       {1.0f, 0.8f, 0.0f},
                       {0.8f, 1.0f, 0.0f},
                       {1.0f, 1.0f, 0.3f}}},
                     // 实例4（id=1004）：品红调，四角：品红、粉红、紫、浅紫
                     {{{1.0f, 0.0f, 1.0f},
                       {1.0f, 0.5f, 0.5f},
                       {0.5f, 0.0f, 1.0f},
                       {0.8f, 0.2f, 0.8f}}},
                     // 实例5（id=1005）：青调，四角：青、浅青、蓝绿、淡青
                     {{{0.0f, 1.0f, 1.0f},
                       {0.0f, 0.8f, 0.8f},
                       {0.5f, 1.0f, 1.0f},
                       {0.2f, 0.8f, 0.8f}}},
                     // 实例6（id=1006）：橙调，四角：橙、红橙、黄橙、浅橙
                     {{{1.0f, 0.5f, 0.0f},
                       {1.0f, 0.3f, 0.0f},
                       {0.8f, 0.4f, 0.0f},
                       {1.0f, 0.6f, 0.2f}}},
                     // 实例7（id=1007）：紫调，四角：紫、浅紫、蓝紫、淡紫
                     {{{0.5f, 0.0f, 0.5f},
                       {0.6f, 0.2f, 0.6f},
                       {0.3f, 0.0f, 0.5f},
                       {0.7f, 0.3f, 0.7f}}}}};

                // 在 traverse 循环中，为每个矩形分配颜色
                size_t colorIdx = cur_id % 8;
                std::array<VertexAttribute, 4> attrs{{
                    VertexAttribute{instanceColors[colorIdx][0]}, // 左上
                    VertexAttribute{instanceColors[colorIdx][1]}, // 右上
                    VertexAttribute{instanceColors[colorIdx][2]}, // 右下
                    VertexAttribute{instanceColors[colorIdx][3]}  // 左下
                }};

                std::println("uiStore add id: {}", cur_id);
                if (cur_id == 0)
                {
                    auto printMat4 = [](const glm::mat4 &m) {
                        std::println("[{}, {}, {}, {}]", m[0][0], m[0][1], m[0][2],
                                     m[0][3]);
                        std::println("[{}, {}, {}, {}]", m[1][0], m[1][1], m[1][2],
                                     m[1][3]);
                        std::println("[{}, {}, {}, {}]", m[2][0], m[2][1], m[2][2],
                                     m[2][3]);
                        std::println("[{}, {}, {}, {}]", m[3][0], m[3][1], m[3][2],
                                     m[3][3]);
                    };
                    std::println("screen: w={}, h={}", w, h);
                    std::println("p0=({},{}), p1=({},{}), p2=({},{}), p3=({},{})", p0.x,
                                 p0.y, p1.x, p1.y, p2.x, p2.y, p3.x, p3.y);
                    std::println("matrix:");
                    printMat4(M);
                }
                uiStore.new_entity(inst, attrs);
            }

            for (const auto &child : node->childrens())
                self(++global_id, child.get(), x, y);
        };

        traverse(global_id, layout.root(), 0.0f, 0.0f);
    };

    // 初始调用
    generateUIInstances(layout);
    // uiStore
    std::println("uiStore.size(): {}", uiStore.size());
    //diff: [test_dod10] end

    auto soaCtx = make_aggregate_ref<"soaCtx", "autoSpinStore", "interactiveStore",
                                     "autoSpinStoreShareData", "uiStore">(
        autoSpinStore, interactiveStore, autoSpinStoreShareData, uiStore);

    //diff: [test_dod8.cpp] end

    struct record_info
    {
        uint32_t current_frame;
        uint32_t image_index;
    };

    camera_interface camera =
        camera_interface{}
            .setViewsMatrix({.eye = glm::vec3(0.0F, 0.0F, 2.0F),
                             .center = glm::vec3(0.0F, 0.0F, 0.0F),
                             .up = glm::vec3(0.0F, 1.0F, 0.0F)})
            .setPerspectiveMatrix(
                {.fovy = glm::radians(45.0F),
                 .aspect = swapchain.refImageExtent().width /
                           static_cast<float>(swapchain.refImageExtent().height),
                 .zNear = 0.1f,
                 .zFar = 10.0F});

    glfw_input input{};

    // ========== 拾取资源 ==========
    // 创建拾取图像（R32G32_UINT）

    auto pickResourcesBuild = mcs::vulkan::memory::build_simple_resource(
        {.imageType = VK_IMAGE_TYPE_2D,
         .format = VK_FORMAT_R32G32_UINT,
         .extent = {.width = WIDTH, .height = HEIGHT, .depth = 1},
         .mipLevels = 1,
         .arrayLayers = 1,
         .samples = physical_device.getMaxUsableSampleCount(), //NOTE: 和主管线一样
         .tiling = VK_IMAGE_TILING_OPTIMAL,
         .usage =
             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT //diff: [test_indirectdraw_no_pick] 没有任何代码从 pickingImage 进行拷贝或读取，因此 TRANSFER_SRC_BIT 完全没有必要
         // | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
         ,
         .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED},
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        [](VkImageCreateInfo imageCreateInfo, VkImage image) noexcept
            -> mcs::vulkan::memory::create_image::view_create_info {
            return {.image = image,
                    .viewType = VK_IMAGE_VIEW_TYPE_2D,
                    .format = imageCreateInfo.format,
                    .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                         .baseMipLevel = 0,
                                         .levelCount = 1,
                                         .baseArrayLayer = 0,
                                         .layerCount = 1}};
        });
    auto pickResource = pickResourcesBuild.build(device);

    //  创建解析图像（单样本）

    auto resolveResourcesBuild = mcs::vulkan::memory::build_simple_resource(
        {.imageType = pickResourcesBuild.refCreateInfo().imageType,
         .format = pickResourcesBuild.refCreateInfo().format,
         .extent = pickResourcesBuild.refCreateInfo().extent,
         .mipLevels = pickResourcesBuild.refCreateInfo().mipLevels,
         .arrayLayers = pickResourcesBuild.refCreateInfo().arrayLayers,
         .samples = VK_SAMPLE_COUNT_1_BIT,
         .tiling = pickResourcesBuild.refCreateInfo().tiling,
         .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |   // 作为 resolve 目标
                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT |   // 用于复制像素到缓冲区
                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT // ← 必须！
         ,
         .sharingMode = pickResourcesBuild.refCreateInfo().sharingMode,
         .initialLayout = pickResourcesBuild.refCreateInfo().initialLayout},
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        [](VkImageCreateInfo imageCreateInfo, VkImage image) noexcept
            -> mcs::vulkan::memory::create_image::view_create_info {
            return {.image = image,
                    .viewType = VK_IMAGE_VIEW_TYPE_2D,
                    .format = imageCreateInfo.format,
                    .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                         .baseMipLevel = 0,
                                         .levelCount = 1,
                                         .baseArrayLayer = 0,
                                         .layerCount = 1}};
        });
    auto resolveResource = resolveResourcesBuild.build(device);

    // 每个飞行帧的暂存缓冲区（8 字节 = R32G32_UINT 像素）
    std::array<mcs::vulkan::memory::auto_map_buffer, MAX_FRAMES_IN_FLIGHT> pickingFrames;
    // 当前鼠标位置（已翻转 Y 轴，与 Vulkan 视口一致）
    struct pick_mouse
    {
        glm::ivec2 pos{0, 0};
        bool valid = false;
    };
    pick_mouse pickMouse;
    for (auto &pf : pickingFrames)
    {
        constexpr auto BUFFER_SIZE = 8;
        pf = mcs::vulkan::memory::auto_map_buffer(
            mcs::vulkan::memory::create_simple_buffer(
                device,
                {.size = BUFFER_SIZE,
                 .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
            BUFFER_SIZE);
    }

    auto pickCtx =
        make_aggregate_ref<"pickCtx", "pickResourcesBuild", "pickResource",
                           "resolveResourcesBuild", "resolveResource", "frames", "mouse">(
            pickResourcesBuild, pickResource, resolveResourcesBuild, resolveResource,
            pickingFrames, pickMouse);
    auto globalCtx =
        make_aggregate_ref<"globalCtx", "device", "window", "surface", "swapchainBuild",
                           "swapchain", "camera", "frameContext", "commandPool",
                           "commandBuffers", "queue", "meshMap">(
            device, window, surface, swapchainBuild, swapchain, camera, frameContext,
            commandPool, commandBuffers, GRAPHICS_AND_PRESENT, meshMap);

    auto mainCtx =
        make_aggregate_ref<"mainCtx", "pipeline", "pipelineLayout", "depthResourcesBuild",
                           "depthResource", "msaaResourcesBuild", "msaaResource">(
            graphicsPipeline, pipelineLayout, depthResourcesBuild, depthResource,
            msaaResourcesBuild, msaaResource);

    auto mainShaderCtx =
        make_aggregate_ref<"mainShaderCtx", "indirectDrawBatches", "uniformBuffers",
                           "descriptorSets">(batches, uniformBuffers, descriptorSets);

    float deltaTime = 0.0F; // 每帧由主循环更新
    auto inputCtx =
        make_aggregate_ref<"inputCtx", "input", "deltaTime">(input, deltaTime);

    // record_info
    auto recordCtx = make_aggregate<"recordCtx", "info">(record_info{});
    auto world = make_aggregate_ref<"world", "globalCtx", "mainCtx", "mainShaderCtx",
                                    "pickCtx", "inputCtx", "soaCtx", "recordCtx">(
        globalCtx, mainCtx, mainShaderCtx, pickCtx, inputCtx, soaCtx, recordCtx);
    using world_type = decltype(world);

    // diff: [test_dod2] start:  world之后 才能调用 数据API

    // init data

    //diff: [test_dod8] start
    auto global_id = 0;
    // 从模型矩阵生成 model_state（其他交互字段保持默认）
    constexpr auto make_model_state_from_matrix = [](const glm::mat4 &M) {
        glm::vec3 trans, scale;
        glm::quat rot;
        glm::vec3 skew;
        glm::vec4 persp;
        glm::decompose(M, scale, rot, trans, skew, persp);

        model_state state;
        state.model_matrix.translation = trans;
        state.model_matrix.rotation = rot;
        state.model_matrix.scale = scale;
        // last_pos, is_middle_button_pressed 等保持默认即可
        return state;
    };

    auto autoSpinId = autoSpinStore.new_entity(
        InstanceData{global_id++, 0, 0,
                     object_data{make_ndc_quad_matrix(glm::vec2(-0.5f, -0.5f),
                                                      glm::vec2(0.5f, 0.5f))}},
        std::array<VertexAttribute, 4>{
            VertexAttribute{glm::vec3{1.0f, 0.0f, 0.0f}}, // 红
            VertexAttribute{glm::vec3{1.0f, 0.0f, 0.0f}}, // 红
            VertexAttribute{glm::vec3{0.0f, 1.0f, 0.0f}}, // 绿
            VertexAttribute{glm::vec3{0.0f, 1.0f, 0.0f}}, // 绿
        });
    auto interactiveId = interactiveStore.new_entity(
        InstanceData{global_id++, 3, 1, object_data{}},
        std::array<VertexAttribute, 4>{
            VertexAttribute{{1.0f, 0.0f, 1.0f}}, // 品红
            VertexAttribute{{0.0f, 1.0f, 1.0f}}, // 青
            VertexAttribute{{1.0f, 0.5f, 0.0f}}, // 橙
            VertexAttribute{{0.5f, 0.0f, 0.5f}}, // 紫
        },
        mesh::get_topLeftLocal(std::span{quadVerts}),
        model_state{make_model_state_from_matrix(
            make_ndc_quad_matrix(glm::vec2(-0.5f, -0.5f), glm::vec2(0.5f, 0.5f), 0.2f))});

    // 随机生成两个纹理索引和采样器索引

    //diff: [test_dod3] end

    // diff: [test_dod2] end

    static constexpr auto views_matrix_update = [](world_type &world) {
        const auto &input = world.inputCtx.input;
        auto &camera = world.globalCtx.camera;
        using mcs::vulkan::event::Key;
        const float step = 0.1f; // 移动步长，可根据需要调整

        // 获取当前相机参数
        glm::vec3 eye = camera.refViewsMatrix().eye;
        glm::vec3 center = camera.refViewsMatrix().center;
        glm::vec3 up = camera.refViewsMatrix().up; // 通常为 (0,1,0)

        // 计算视线方向和右向
        glm::vec3 forward = glm::normalize(center - eye);
        glm::vec3 right = glm::normalize(glm::cross(forward, up));

        glm::vec3 delta(0.0f);

        if (input.isKeyPressedOrRepeat(Key::eW))
            delta += forward * step;
        if (input.isKeyPressedOrRepeat(Key::eS))
            delta -= forward * step;
        if (input.isKeyPressedOrRepeat(Key::eA))
            delta -= right * step;
        if (input.isKeyPressedOrRepeat(Key::eD))
            delta += right * step;
        if (input.isKeyPressedOrRepeat(Key::eQ))
            delta += glm::vec3(0.0f, step, 0.0f); // 世界Y轴向上
        if (input.isKeyPressedOrRepeat(Key::eE))
            delta += glm::vec3(0.0f, -step, 0.0f); // 世界Y轴向下

        if (glm::length2(delta) > 0.0f)
        {
            // 同时移动 eye 和 center，保持视线方向不变
            camera.refViewsMatrix().eye += delta;
            camera.refViewsMatrix().center += delta;
        }

        // ========== Ctrl+右键旋转视角 ==========
        {
            static mcs::vulkan::event::position2d_event lastRightPos{};
            static bool isRightDragging = false;
            using mcs::vulkan::event::MouseButtons;

            bool ctrlPressed = input.isKeyPressedOrRepeat(Key::eLEFT_CONTROL) ||
                               input.isKeyPressedOrRepeat(Key::eRIGHT_CONTROL);
            bool curRightPressed =
                input.isMouseButtonPressed(MouseButtons::eMOUSE_BUTTON_RIGHT);

            if (curRightPressed && ctrlPressed)
            {
                auto cur = input.cursorPos();
                if (!isRightDragging)
                {
                    lastRightPos = cur;
                    isRightDragging = true;
                }
                else
                {
                    float dx = static_cast<float>(cur.xpos - lastRightPos.xpos);
                    float dy = static_cast<float>(cur.ypos - lastRightPos.ypos);

                    if (dx != 0.0f || dy != 0.0f)
                    {
                        const float rotSensitivity = 0.005f; // 灵敏度可调

                        glm::vec3 eye = camera.refViewsMatrix().eye;
                        glm::vec3 center = camera.refViewsMatrix().center;
                        glm::vec3 up = camera.refViewsMatrix().up;

                        glm::vec3 forward = glm::normalize(center - eye);
                        glm::vec3 right = glm::normalize(glm::cross(forward, up));

                        // 偏航（绕世界Y轴）和俯仰（绕局部右轴）
                        float yaw = -dx * rotSensitivity;   // 鼠标左移时向左转
                        float pitch = -dy * rotSensitivity; // 鼠标上推时向上看

                        // 限制俯仰角，避免翻转
                        const float maxPitch = glm::radians(89.0f);
                        float currentPitch = asin(glm::clamp(forward.y, -1.0f, 1.0f));
                        float newPitch =
                            glm::clamp(currentPitch + pitch, -maxPitch, maxPitch);
                        pitch = newPitch - currentPitch;

                        // 构建旋转矩阵：先绕世界Y轴旋转 yaw，再绕局部右轴旋转 pitch
                        glm::mat4 rotYaw =
                            glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0, 1, 0));
                        glm::mat4 rotPitch = glm::rotate(glm::mat4(1.0f), pitch, right);

                        // 应用旋转到 forward
                        glm::vec4 newForward =
                            rotYaw * rotPitch * glm::vec4(forward, 0.0f);
                        forward = glm::normalize(glm::vec3(newForward));

                        // 更新 center，保持 eye 不变
                        float distance = glm::length(center - eye);
                        camera.refViewsMatrix().center = eye + forward * distance;
                    }
                    lastRightPos = cur;
                }
            }
            else
            {
                isRightDragging = false;
            }
        };
    };

    static constexpr auto views_perspective_update = [](world_type &world) {
        const auto &input = world.inputCtx.input;
        auto &camera = world.globalCtx.camera;
        using mcs::vulkan::event::Key;

        if (input.isKeyPressedOrRepeat(Key::eR))
        {
            constexpr auto step = 00.1;
            update_perspective<perspective_member::eFOVY>(camera, step);
        }
        if (input.isKeyPressedOrRepeat(Key::eF))
        {
            constexpr auto step = -00.1;
            update_perspective<perspective_member::eFOVY>(camera, step);
        }
        if (input.isKeyPressedOrRepeat(Key::eT))
        {
            constexpr auto step = 00.1;
            update_perspective<perspective_member::eASPECT>(camera, step);
        }
        if (input.isKeyPressedOrRepeat(Key::eG))
        {
            constexpr auto step = -00.1;
            update_perspective<perspective_member::eASPECT>(camera, step);
        }

        if (input.isKeyPressedOrRepeat(Key::eY))
        {
            constexpr auto step = 00.1;
            update_perspective<perspective_member::eZ_NEAR>(camera, step);
        }
        if (input.isKeyPressedOrRepeat(Key::eH))
        {
            constexpr auto step = -00.1;
            update_perspective<perspective_member::eZ_NEAR>(camera, step);
        }

        if (input.isKeyPressedOrRepeat(Key::eU))
        {
            constexpr auto step = 00.1;
            update_perspective<perspective_member::eZ_FAR>(camera, step);
        }
        if (input.isKeyPressedOrRepeat(Key::eJ))
        {
            constexpr auto step = -00.1;
            update_perspective<perspective_member::eZ_FAR>(camera, step);
        }
    };

    // diff: [test_model_matrix3] 小小调整
    static constexpr auto updateObjectData = [](world_type &world,
                                                uint32_t currentFrame) noexcept {
        auto &soaCtx = world.soaCtx;

        auto &autoSpinStore = soaCtx.autoSpinStore;
        auto &interactiveStore = soaCtx.interactiveStore;

        auto &spinElapsed = soaCtx.autoSpinStoreShareData.spinElapsed;

        // 获取 deltaTime（从 inputCtx 读取）
        const float dt = world.inputCtx.deltaTime;
        // 更新自旋动画的累积时间
        spinElapsed += dt;

        //diff: [test_dod8] start
        auto start = std::chrono::high_resolution_clock::now();
        uint32_t idx = 0;
        {
            // Buffer is already mapped. You can access its memory.
            for (auto [instanceData] : autoSpinStore.view<"instanceData">(currentFrame))
            {
                static_assert(std::is_reference_v<decltype(instanceData)>);
                auto &mat = instanceData.objectData.matrix;
                if (idx == 0) [[unlikely]]
                {

                    // 从现有矩阵提取原始平移和缩放
                    glm::vec3 translation = glm::vec3(mat[3]);
                    glm::vec3 scale(glm::length(glm::vec3(mat[0])),
                                    glm::length(glm::vec3(mat[1])),
                                    glm::length(glm::vec3(mat[2])));

                    float phaseOffset = idx * glm::radians(137.5f);
                    auto angle = spinElapsed * glm::radians(90.0f) + phaseOffset;
                    glm::mat4 rot =
                        glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 0.0f, 1.0f));

                    mat = glm::translate(glm::mat4(1.0f), translation) * rot *
                          glm::scale(glm::mat4(1.0f), scale);

                    //diff: [test_dod9] start
                    // ---- 动态 UV 变换 ----
                    auto &uv = instanceData.uvTransform;
                    // 示例1：纹理随时间向右滚动（重复模式）
                    uv.offset.x = fmodf(spinElapsed * 0.2f, 1.0f);
                    // 示例2：纹理在 0.5 倍到 1.5 倍之间周期性缩放
                    float s = 1.0f + 0.5f * sinf(spinElapsed * 2.0f);
                    uv.scale = glm::vec2(s, s);
                    //diff: [test_dod9] end
                }
                else [[likely]]
                {
                    // 提取现有的平移和缩放
                    glm::vec3 translation = glm::vec3(mat[3]);       // 第4列是平移
                    glm::vec3 scale(glm::length(glm::vec3(mat[0])),  // 第1列长度 = X缩放
                                    glm::length(glm::vec3(mat[1])),  // 第2列长度 = Y缩放
                                    glm::length(glm::vec3(mat[2]))); // 第3列长度 = Z缩放

                    float phaseOffset = idx * glm::radians(137.5f);
                    float angle = spinElapsed * glm::radians(90.0f) + phaseOffset;
                    glm::mat4 rot =
                        glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 0.0f, 1.0f));

                    // 重构：缩放 → 旋转 → 平移（与创建时的顺序保持一致）
                    mat = glm::translate(glm::mat4(1.0f), translation) * rot *
                          glm::scale(glm::mat4(1.0f), scale);
                }

                ++idx;
            }
            for (const auto &[instanceData, modelState] :
                 interactiveStore.view<"instanceData", "modelState">(currentFrame))
            {
                //diff: [test_dod8] 构造时的偏移迁移到这里
                auto &objectData = instanceData.objectData;
                // modelState.model_matrix.translation.z = 0.5f; //diff: [test_dod9] 不需要补丁了。初始化的时候就填写好了
                objectData = object_data{modelState.model_matrix()};
            }
        }
        // diff: [test_dod11] start
        // UI 的矩阵操作. 各自UI组件的中心旋转
        if (0)
        {
            auto &uiStore = world.soaCtx.uiStore;
            float time = world.soaCtx.autoSpinStoreShareData.spinElapsed; // 复用全局时间

            // 在 uiStore 完成填充后，立即保存原始基础矩阵（只保存一次）
            static std::vector<glm::mat4> uiBaseMatrices = [&]() {
                std::vector<glm::mat4> uiBaseMatrices;
                for (auto [instanceData] : uiStore.view<"instanceData">(0))
                { // frame=0 的数据
                    uiBaseMatrices.push_back(instanceData.objectData.matrix);
                }
                return uiBaseMatrices;
            }();

            // 确保 baseMatrices 数量与当前 uiStore 一致
            if (uiBaseMatrices.size() == uiStore.size())
            {
                int idx = 0;
                for (auto [instanceData] : uiStore.view<"instanceData">(currentFrame))
                {
                    const glm::mat4 &baseMat = uiBaseMatrices[idx];
                    // 从基础矩阵提取平移和原始缩放（固定不变）
                    glm::vec3 translation = glm::vec3(baseMat[3]);
                    glm::vec3 baseScale(glm::length(glm::vec3(baseMat[0])),
                                        glm::length(glm::vec3(baseMat[1])),
                                        glm::length(glm::vec3(baseMat[2])));

                    // 动画参数（绝对时间，不累积）
                    float angle = time * 0.5f;
                    float s = 1.0f + 0.3f * sinf(time * 2.0f);
                    glm::vec3 animatedScale = baseScale * s;

                    // 构建变换矩阵：平移 * 旋转 * 缩放
                    glm::mat4 R = glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0, 0, 1));
                    auto &M = instanceData.objectData.matrix;
                    M = glm::translate(glm::mat4(1.0f), translation) * R *
                        glm::scale(glm::mat4(1.0f), animatedScale);

                    ++idx;
                }
            }
        }
        else // UI 整体动画（所有面板绕 UI 组中心旋转/缩放）
        {
            auto &uiStore = world.soaCtx.uiStore;
            float time = world.soaCtx.autoSpinStoreShareData.spinElapsed;

            static std::vector<glm::mat4> uiBaseMatrices = [&]() {
                std::vector<glm::mat4> bases;
                for (auto [instanceData] : uiStore.view<"instanceData">(0))
                    bases.push_back(instanceData.objectData.matrix);
                return bases;
            }();

            if (uiBaseMatrices.size() == uiStore.size())
            {
                // 1. 计算整个 UI 组的包围盒中心（或者直接使用 NDC 原点 (0,0)）
                glm::vec2 center(0.0f); // 如果希望绕屏幕中心旋转，就用 (0,0)
                // 如果想绕 UI 组的真实中心旋转，可以遍历 baseMatrices 的平移分量求平均：
                // for (const auto& mat : uiBaseMatrices) center += glm::vec2(mat[3]);
                // center /= static_cast<float>(uiBaseMatrices.size());

                // 2. 动画参数（绝对时间）
                float angle = time * 0.5f;
                float s = 1.0f + 0.3f * sinf(time * 2.0f);

                // 3. 构建全局变换矩阵：平移到中心 → 旋转 → 缩放 → 平移回原点
                glm::mat4 globalTransform =
                    glm::translate(glm::mat4(1.0f), glm::vec3(center, 0.0f)) *
                    glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0, 0, 1)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(s, s, 1.0f)) *
                    glm::translate(glm::mat4(1.0f), glm::vec3(-center, 0.0f));

                // 4. 对每个 UI 面板应用全局变换
                int idx = 0;
                for (auto [instanceData] : uiStore.view<"instanceData">(currentFrame))
                {
                    instanceData.objectData.matrix =
                        globalTransform * uiBaseMatrices[idx];
                    ++idx;
                }
            }
        }
        // diff: [test_dod11] end
        // 3. 记录结束时间点
        auto end = std::chrono::high_resolution_clock::now();

        // 4. 计算时间差，并转换为毫秒
        auto duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        // 5. 打印耗时
        // if (auto count = duration.count(); count > 0)
        //     std::cout << "updateObjectData耗时: " << duration.count()
        //               << " 毫秒,数据量: " << idx << std::endl;
        //diff: [test_dod8] end
    };
    // diff: [test_model_matrix2] end

    static constexpr auto updateVertexData = [](world_type &world,
                                                uint32_t currentFrame) noexcept {
        auto &soaCtx = world.soaCtx;

        auto &autoSpinStore = soaCtx.autoSpinStore;
        auto &interactiveStore = soaCtx.interactiveStore;
    };

    static constexpr auto model_update = [](world_type &world) {
        auto &globalCtx = world.globalCtx;
        auto &inputCtx = world.inputCtx;

        auto &soaCtx = world.soaCtx;
        auto &interactiveStore = soaCtx.interactiveStore;
        auto [modelState, topLeftLocal] =
            *(interactiveStore.view<"modelState", "topLeftLocal">().begin());

        const auto &input = inputCtx.input;
        auto &camera = globalCtx.camera;
        auto &swapchain = globalCtx.swapchain;
        auto &frameContext = globalCtx.frameContext;

        auto &[lastPos, isMiddleButtonPressed, isLeftButtonPressed,
               rightButtonPressedLast, lastLeftPos, modelMatrix] = modelState;
        using mcs::vulkan::event::MouseButtons;
        using mcs::vulkan::event::Key;
        using mcs::vulkan::event::scroll_event;
        const auto cur = input.cursorPos();
        const auto windowSize = swapchain.refImageExtent();

        const bool curMiddlePressed =
            input.isMouseButtonPressed(MouseButtons::eMOUSE_BUTTON_MIDDLE);
        const bool curLeftPressed =
            input.isMouseButtonPressed(MouseButtons::eMOUSE_BUTTON_LEFT);
        const bool curRightPressed =
            input.isMouseButtonPressed(MouseButtons::eMOUSE_BUTTON_RIGHT);

        if (scroll_event{} != input.scroll())
        {
            std::println("input.scroll(): {}", input.scroll());
            // 获取修饰键状态
            bool ctrlPressed = input.isKeyPressedOrRepeat(Key::eLEFT_CONTROL) ||
                               input.isKeyPressedOrRepeat(Key::eRIGHT_CONTROL);
            bool altPressed = input.isKeyPressedOrRepeat(Key::eLEFT_ALT) ||
                              input.isKeyPressedOrRepeat(Key::eRIGHT_ALT);
            bool shiftPressed = input.isKeyPressedOrRepeat(Key::eLEFT_SHIFT) ||
                                input.isKeyPressedOrRepeat(Key::eRIGHT_SHIFT);
            auto scroll = input.scroll();
            float delta = scroll.yoffset; // +1 向上滚（放大），-1 向下滚（缩小）

            if (delta != 0.0f)
            {
                // 缩放因子：每格 ±10%
                const float factor = (delta > 0) ? 1.1f : (1.0f / 1.1f);

                // 统计按下修饰键的数量
                int modCount =
                    (ctrlPressed ? 1 : 0) + (altPressed ? 1 : 0) + (shiftPressed ? 1 : 0);

                if (modCount == 1)
                {
                    // 仅一个修饰键按下：缩放对应的轴
                    if (ctrlPressed)
                        modelMatrix.scale.x *= factor;
                    else if (altPressed)
                        modelMatrix.scale.y *= factor;
                    else if (shiftPressed)
                        modelMatrix.scale.z *= factor;
                }
                else
                {
                    // 无修饰键或多个修饰键：均匀缩放所有轴
                    modelMatrix.scale *= factor;
                }

                // 可选：限制缩放范围，避免过小或过大
                // modelMatrix_.scale = glm::clamp(modelMatrix_.scale, 0.01f, 100.0f);
            }
        }

        // ========== 右键单击复位物体 ==========
        if (curRightPressed && !rightButtonPressedLast)
        {
            // 按下瞬间：平移归零，旋转归单位四元数
            modelMatrix.translation = glm::vec3(0.0f);
            modelMatrix.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            // scale 保持 (1,1,1) 不变
            modelMatrix.scale = {1, 1, 1};
        }
        rightButtonPressedLast = curRightPressed; // 更新状态供下一帧使用

        // ========== 左键旋转处理（支持修饰键分离轴） ==========
        if (curLeftPressed)
        {
            if (!isLeftButtonPressed)
            {
                lastLeftPos = cur;
                isLeftButtonPressed = true;
            }
            else
            {
                float dx = static_cast<float>(cur.xpos - lastLeftPos.xpos);
                float dy = static_cast<float>(cur.ypos - lastLeftPos.ypos);

                if (dx != 0.0f || dy != 0.0f)
                {
                    glm::mat4 view = camera.viewsMatrix();
                    glm::mat4 viewInv = glm::inverse(view);
                    glm::vec3 worldRight = viewInv[0]; // 屏幕右方向（水平轴）
                    glm::vec3 worldUp = viewInv[1];    // 屏幕上方向（垂直轴）
                    glm::vec3 worldForward = -glm::vec3(viewInv[2]); // 视线方向

                    const float rotSensitivity = 0.01f;

                    bool ctrlPressed = input.isKeyPressedOrRepeat(Key::eLEFT_CONTROL) ||
                                       input.isKeyPressedOrRepeat(Key::eRIGHT_CONTROL);
                    bool altPressed = input.isKeyPressedOrRepeat(Key::eLEFT_ALT) ||
                                      input.isKeyPressedOrRepeat(Key::eRIGHT_ALT);
                    bool shiftPressed = input.isKeyPressedOrRepeat(Key::eLEFT_SHIFT) ||
                                        input.isKeyPressedOrRepeat(Key::eRIGHT_SHIFT);

                    glm::quat deltaRot(1.0f, 0.0f, 0.0f, 0.0f);

                    if (ctrlPressed)
                    {
                        // Ctrl + 水平移动：绕屏幕垂直轴（左右旋转）
                        deltaRot = glm::angleAxis(dx * rotSensitivity, worldUp);
                    }
                    else if (altPressed)
                    {
                        // Alt + 垂直移动：绕屏幕水平轴（上下翻转）
                        deltaRot = glm::angleAxis(dy * rotSensitivity, worldRight);
                    }
                    else if (shiftPressed)
                    {
                        // Shift + 水平移动：绕视线方向（滚动）
                        deltaRot = glm::angleAxis(dx * rotSensitivity, worldForward);
                    }
                    else
                    {
                        // 无修饰键：水平绕 worldUp，垂直绕 worldRight
                        glm::quat rotY = glm::angleAxis(dx * rotSensitivity, worldUp);
                        glm::quat rotX = glm::angleAxis(dy * rotSensitivity, worldRight);
                        deltaRot = rotY * rotX;
                    }

                    if (deltaRot != glm::quat(1.0f, 0.0f, 0.0f, 0.0f))
                    {
                        modelMatrix.rotation = deltaRot * modelMatrix.rotation;
                    }
                }
                lastLeftPos = cur;
            }
        }
        else
        {
            isLeftButtonPressed = false;
        }
        if (curMiddlePressed)
        {
            // 检测 Shift 键（独立于左键修饰键）
            bool shiftPressed = input.isKeyPressedOrRepeat(Key::eLEFT_SHIFT) ||
                                input.isKeyPressedOrRepeat(Key::eRIGHT_SHIFT);

            glm::mat4 view = camera.viewsMatrix();
            glm::mat4 proj = camera.perspectiveMatrix();
            glm::mat4 viewInv = glm::inverse(view);
            glm::vec4 viewport(0.0f, 0.0f, static_cast<float>(windowSize.width),
                               static_cast<float>(windowSize.height));

            glm::vec3 currentScale = modelMatrix.scale;
            glm::quat currentRot = modelMatrix.rotation;
            glm::vec3 localTopLeft = topLeftLocal;
            glm::vec3 scaledTopLeft = currentScale * localTopLeft;
            glm::vec3 rotatedOffset =
                currentRot * scaledTopLeft; // 从平移位置指向左上角的偏移量
            glm::vec3 currentTopLeftWorld = modelMatrix.translation + rotatedOffset;

            glm::vec3 winNear(static_cast<float>(cur.xpos),
                              static_cast<float>(windowSize.height - cur.ypos), 0.0f);
            glm::vec3 winFar(static_cast<float>(cur.xpos),
                             static_cast<float>(windowSize.height - cur.ypos), 1.0f);
            glm::vec3 nearWorld = glm::unProject(winNear, view, proj, viewport);
            glm::vec3 farWorld = glm::unProject(winFar, view, proj, viewport);
            glm::vec3 dirWorld = glm::normalize(farWorld - nearWorld);

            const float eps = 1e-6f;
            const float maxT = 1000.0f;   // 最大射线距离，根据场景调整
            const float maxDelta = 10.0f; // 单次最大平移变化量（可选）

            glm::vec3 newTranslation = modelMatrix.translation; // 默认不变

            if (shiftPressed)
            {
                // ========== 旧模式：世界 XY 平面平移（固定世界 Z） ==========
                float targetZ = currentTopLeftWorld.z; // 保持当前左上角的世界 Z
                if (std::fabs(dirWorld.z) > eps)
                {
                    float t = (targetZ - nearWorld.z) / dirWorld.z;
                    if (t >= 0.0f && t < maxT)
                    {
                        glm::vec3 worldPoint = nearWorld + t * dirWorld;
                        newTranslation = worldPoint - rotatedOffset;
                    }
                }
            }
            else
            {
                // ========== 新模式：屏幕空间平移（固定相机空间 Z） ==========
                // 将当前左上角转换到相机空间，获取其深度
                glm::vec4 currentTopLeftView =
                    view * glm::vec4(currentTopLeftWorld, 1.0f);
                float depthView = currentTopLeftView.z; // 相机空间 Z

                // 将射线端点转换到相机空间
                glm::vec4 nearView = view * glm::vec4(nearWorld, 1.0f);
                glm::vec4 farView = view * glm::vec4(farWorld, 1.0f);
                glm::vec3 dirView = glm::vec3(farView - nearView); // 相机空间射线方向

                if (std::fabs(dirView.z) > eps)
                {
                    float t = (depthView - nearView.z) / dirView.z;
                    if (t >= 0.0f && t <= 1.0f) // 交点在射线范围内
                    {
                        glm::vec3 pointView = glm::vec3(nearView) + t * dirView;
                        glm::vec4 pointWorld = viewInv * glm::vec4(pointView, 1.0f);
                        glm::vec3 worldPoint = glm::vec3(pointWorld) / pointWorld.w;
                        newTranslation = worldPoint - rotatedOffset;
                    }
                }
            }

            // 可选：限制平移变化量，防止跳变
            glm::vec3 delta = newTranslation - modelMatrix.translation;
            if (glm::length(delta) < maxDelta)
            {
                modelMatrix.translation = newTranslation;
            }
            // 若超过限制，可以忽略本次更新，或按比例缩放

            if (!isMiddleButtonPressed)
                isMiddleButtonPressed = true;
            lastPos = cur;
        }
        else
        {
            isMiddleButtonPressed = false;
        }
    };

    // diff: [test_indirectdraw] start prepareBatch：动态分配合批资源并填充数据 // NOLINTNEXTLINE
    static constexpr auto prepareBatch = [](world_type &world, uint32_t currentFrame) {
        auto &globalCtx = world.globalCtx;
        auto &mainShaderCtx = world.mainShaderCtx;

        auto &device = globalCtx.device;
        auto &meshMap = globalCtx.meshMap;

        auto &descriptorSets = mainShaderCtx.descriptorSets;
        auto &batches = mainShaderCtx.indirectDrawBatches;

        auto &descriptorSet = descriptorSets[currentFrame];
        auto &batch = batches[currentFrame];

        auto &soaCtx = world.soaCtx;
        auto &autoSpinStore = soaCtx.autoSpinStore;
        auto &interactiveStore = soaCtx.interactiveStore;
        auto &uiStore = soaCtx.uiStore;

        auto start = std::chrono::high_resolution_clock::now();
        //diff: [test_dod11] start
        {
            static std::vector<VkDrawIndexedIndirectCommand> g_cmds;
            static std::vector<InstanceData> g_allInstances;
            static std::vector<VertexAttribute> g_allAttributes;

            // 临时命令和常量数组
            std::vector<VkDrawIndexedIndirectCommand> cmds;
            std::vector<CommandConstant> cmdConsts;

            // 用于写入 buffer 的偏移
            VkDeviceSize instanceOffset = 0;
            VkDeviceSize attributeOffset = 0;
            batch.draw3DCount = 0;
            batch.drawCountUI = 0;

            // 处理一个 store（它绑定到一个 mesh，mesh 提供顶点数）
            auto processStore = [&](auto &store, const Mesh &mesh, uint32_t frame) {
                uint32_t count = store.size(); // 存活实体数
                if (count == 0)
                    return;
                // ★ 计算正确的 firstInstance
                uint32_t firstInstance =
                    static_cast<uint32_t>(instanceOffset / sizeof(InstanceData));

                // 1. 整体拷贝实例数据（每个实体一个 InstanceData）
                // 假设 store 中有 "instanceData" 字段，且类型为 InstanceData
                InstanceData *instSrc =
                    store.template raw_field<"instanceData">(frame).data();
                VkDeviceSize instSize = count * sizeof(InstanceData);
                memcpy((char *)batch.globalInstanceBuffer.mapPtr() + instanceOffset,
                       instSrc, instSize);

                // 2. 整体拷贝属性数据（每个实体一个 std::array<VertexAttribute, 4>）
                std::array<VertexAttribute, 4> *attrSrc =
                    store.template raw_field<"vertexAttribute">(frame).data();
                VkDeviceSize attrSize = count * sizeof(std::array<VertexAttribute, 4>);
                memcpy((char *)batch.globalAttributeBuffer.mapPtr() + attributeOffset,
                       attrSrc, attrSize);

                // 3. 生成间接命令
                cmds.push_back(mesh.getDrawCommand(count, firstInstance));

                // 4. 生成命令常量
                cmdConsts.push_back(CommandConstant{mesh.vertexCount});

                instanceOffset += instSize;
                attributeOffset += attrSize;

                // std::cout << "写入了实体: " << count << std::endl;
            };

            const Mesh &quadMesh = meshMap["quad"];
            // 3D
            {
                processStore(autoSpinStore, quadMesh, currentFrame);
                processStore(interactiveStore, quadMesh, currentFrame);
            }
            uint32_t draw3DCount = static_cast<uint32_t>(cmds.size());
            batch.draw3DCount = draw3DCount;

            // UI
            {
                // ========== UI 周期显示控制 ==========
                constexpr float UI_ON_DURATION = 1.0f;
                constexpr float UI_OFF_DURATION = 3.0f;
                constexpr float UI_PERIOD = UI_ON_DURATION + UI_OFF_DURATION;

                static auto s_uiCycleStart = std::chrono::steady_clock::now();
                auto now = std::chrono::steady_clock::now();
                float elapsed =
                    std::chrono::duration<float>(now - s_uiCycleStart).count();
                if (elapsed >= UI_PERIOD)
                {
                    s_uiCycleStart = now;
                    elapsed = 0.0f;
                }
                bool showUI = (elapsed < UI_ON_DURATION);

                if (showUI)
                {
                    processStore(uiStore, quadMesh, currentFrame);
                }
            }
            batch.drawCountUI = cmds.size() - draw3DCount;

            // 上传命令和常量
            // 所有命令（3D+UI）一次性拷贝
            uint32_t totalDrawCount = static_cast<uint32_t>(cmds.size());
            memcpy(batch.indirectDrawBuffer.mapPtr(), cmds.data(),
                   totalDrawCount * sizeof(VkDrawIndexedIndirectCommand));
            memcpy(batch.commandConstantsBuffer.mapPtr(), cmdConsts.data(),
                   totalDrawCount * sizeof(CommandConstant));
        }
        //diff: [test_dod11] end
        // 3. 记录结束时间点
        auto end = std::chrono::high_resolution_clock::now();

        // 4. 计算时间差，并转换为毫秒
        auto duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        // NOTE: 函数执行耗时: 2 毫秒. [FPS]  77.1 (avg frame: 12.963 ms) 占用了1/6
        // 5. 打印耗时
        if (auto count = duration.count(); count > 0)
            std::cout << "函数执行耗时: " << duration.count() << " 毫秒" << std::endl;
    };

    // diff: [test_indirectdraw] end
    // diff: [test_dod5] start
    static constexpr auto mainPipeline = make_aggregate<
        "mainPipeline", "transitionImageLayout", "beginRendering", "setPipelineState",
        "draw", "endRendering">(
        std::constant_wrapper<[](world_type &world) {
            auto &globalCtx = world.globalCtx;
            auto &mainCtx = world.mainCtx;
            auto &recordCtx = world.recordCtx;
            auto &pickCtx = world.pickCtx;

            auto &swapchain = globalCtx.swapchain;
            auto &commandBuffers = globalCtx.commandBuffers;

            const auto &[currentFrame, imageIndex] = recordCtx.info;
            const auto &commandBuffer = commandBuffers[currentFrame];

            // images
            auto pickingImage = pickCtx.pickResource.image();
            auto resolveImage = pickCtx.resolveResource.image();
            auto &depthResource = mainCtx.depthResource;
            auto &msaaResource = mainCtx.msaaResource;

            VkImage image = swapchain.image(imageIndex);
            VkImage depthImage = depthResource.image();

            VkImage msaaImage = msaaResource.image();

            // Before starting rendering,
            // transition the swapchain image to COLOR_ATTACHMENT_OPTIMAL
            my_render::transition_image_layout(
                commandBuffer,
                my_render::image_info{
                    .image = image,
                    .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .src = {.layout = VK_IMAGE_LAYOUT_UNDEFINED,
                            .access_mask = VK_ACCESS_2_NONE,
                            .stage_mask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT},
                    .dst = {.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            .access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            .stage_mask =
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT}},
                my_render::image_info{
                    .image = msaaImage,
                    .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .src = {.layout = VK_IMAGE_LAYOUT_UNDEFINED,
                            .access_mask = VK_ACCESS_2_NONE,
                            .stage_mask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT},
                    .dst = {.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            .access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            .stage_mask =
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT}},
                my_render::image_info{
                    .image = depthImage,
                    .aspect_mask = VK_IMAGE_ASPECT_DEPTH_BIT,
                    .src = {.layout = VK_IMAGE_LAYOUT_UNDEFINED,
                            .access_mask = VK_ACCESS_2_NONE,
                            .stage_mask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT},
                    .dst = {.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                            .access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                            .stage_mask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                          VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT}},
                //diff: [test_indirectdraw_no_pick] start  转换拾取 MSAA 图像布局
                my_render::image_info{
                    .image = pickingImage,
                    .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .src = {VK_IMAGE_LAYOUT_UNDEFINED, VK_ACCESS_2_NONE,
                            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT},
                    .dst = {VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT}},
                my_render::image_info{
                    .image = resolveImage,
                    .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .src = {VK_IMAGE_LAYOUT_UNDEFINED, VK_ACCESS_2_NONE,
                            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT},
                    .dst = {VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT}}
                //diff: [test_indirectdraw_no_pick] end
            );
        }>{},
        std::constant_wrapper<[](world_type &world) {
            auto &globalCtx = world.globalCtx;
            auto &mainCtx = world.mainCtx;
            auto &recordCtx = world.recordCtx;
            auto &pickCtx = world.pickCtx;

            auto &swapchain = globalCtx.swapchain;
            auto &commandBuffers = globalCtx.commandBuffers;

            const auto &[currentFrame, imageIndex] = recordCtx.info;
            const auto &commandBuffer = commandBuffers[currentFrame];

            // imageViews
            auto &depthResource = mainCtx.depthResource;
            auto &msaaResource = mainCtx.msaaResource;

            VkImageView msaaImageView = msaaResource.imageView();
            VkImageView imageView = swapchain.imageView(imageIndex);
            auto pickingImageView = pickCtx.pickResource.imageView();
            auto pickingResolveImageView = pickCtx.resolveResource.imageView();
            VkImageView depthImageView = depthResource.imageView();
            auto imageExtent = swapchain.imageExtent();

            VkRenderingAttachmentInfo colorAttachment = {
                .sType = sType<VkRenderingAttachmentInfo>(),
                .imageView = msaaImageView,
                .imageLayout = VkImageLayout::VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT,
                .resolveImageView =
                    imageView, //NOTE: 将msaaImageView的输出替换到imageView，这就是关键
                .resolveImageLayout =
                    VkImageLayout::VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = {.color = {.float32 = {0.0F, 0.0F, 0.0F, 1.0F}}}};
            //diff: [test_indirectdraw_no_pick] start
            // 拾取附件（location=1）
            VkRenderingAttachmentInfo pickAttachment{
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = pickingImageView,
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .resolveMode =
                    VK_RESOLVE_MODE_SAMPLE_ZERO_BIT, // 整数格式必须用 SAMPLE_ZERO
                .resolveImageView = pickingResolveImageView,
                .resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = {
                    .color = {.uint32 = {0xFFFFFFFF, 0xFFFFFFFF, 0, 0}}}}; // NOLINT
            std::array attachments = {colorAttachment, pickAttachment};
            //                        ^ 索引 0          ^ 索引 1
            //diff: [test_indirectdraw_no_pick] end

            VkRenderingAttachmentInfo depthAttachment = {
                .sType = sType<VkRenderingAttachmentInfo>(),
                .imageView = depthImageView,
                .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = {.depthStencil = {.depth = 1.0F}}};

            commandBuffer.beginRendering(
                {.sType = sType<VkRenderingInfo>(),
                 .renderArea = {.offset = {.x = 0, .y = 0}, .extent = imageExtent},
                 .layerCount = 1,
                 //diff: [test_indirectdraw_no_pick] start
                 .colorAttachmentCount = static_cast<uint32_t>(attachments.size()),
                 .pColorAttachments = attachments.data(),
                 //diff: [test_indirectdraw_no_pick] end
                 .pDepthAttachment = &depthAttachment});
        }>{},
        std::constant_wrapper<[](world_type &world) {
            auto &globalCtx = world.globalCtx;
            auto &mainCtx = world.mainCtx;
            auto &recordCtx = world.recordCtx;
            auto &mainShaderCtx = world.mainShaderCtx;

            auto &swapchain = globalCtx.swapchain;
            auto &commandBuffers = globalCtx.commandBuffers;

            const auto &[currentFrame, imageIndex] = recordCtx.info;
            const auto &commandBuffer = commandBuffers[currentFrame];

            auto &graphicsPipeline = mainCtx.pipeline;
            auto imageExtent = swapchain.imageExtent();
            auto &pipelineLayout = mainCtx.pipelineLayout;

            auto &descriptorSets = mainShaderCtx.descriptorSets;
            auto descriptorSet = descriptorSets[currentFrame];

            commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       *graphicsPipeline);
            commandBuffer.setViewport(
                0, std::array<VkViewport, 1>{
                       VkViewport{.x = 0.0F,
                                  .y = 0.0F,
                                  .width = static_cast<float>(imageExtent.width),
                                  .height = static_cast<float>(imageExtent.height),
                                  .minDepth = 0.0F,
                                  .maxDepth = 1.0F}});

            commandBuffer.setScissor(
                0, std::array<VkRect2D, 1>{
                       VkRect2D{.offset = {.x = 0, .y = 0}, .extent = imageExtent}});
            commandBuffer.bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                             *pipelineLayout, 0, 1, &(descriptorSet), 0,
                                             nullptr);
        }>{},
        std::constant_wrapper<[](world_type &world) {
            auto &mainShaderCtx = world.mainShaderCtx;
            auto &recordCtx = world.recordCtx;
            auto &globalCtx = world.globalCtx;
            auto &mainCtx = world.mainCtx;

            auto &commandBuffers = globalCtx.commandBuffers;
            auto &camera = globalCtx.camera;

            auto &pipelineLayout = mainCtx.pipelineLayout;

            auto &uniformBuffers = mainShaderCtx.uniformBuffers;

            const auto &[currentFrame, imageIndex] = recordCtx.info;
            const auto &commandBuffer = commandBuffers[currentFrame];

            auto &batches = mainShaderCtx.indirectDrawBatches;

            // diff: [test_dod8] start 使用合并索引缓冲和间接绘制
            auto &batch = batches[currentFrame];
            commandBuffer.bindIndexBuffer(batch.globalIndexBuffer.buffer(), 0,
                                          VK_INDEX_TYPE_UINT32);
            PushData pushConstants = {
                .vertexAddress = batch.globalVertexBufferAddress,
                .attributeAddress = batch.globalAttributeBufferAddress,
                .instanceAddress = batch.globalInstanceBufferAddress,
                .commandConstantsAddress = batch.commandConstantsBufferAddress};
            commandBuffer.pushConstants(*pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                                        sizeof(PushData), &pushConstants);
            // diff: [test_dod11] start

            auto uploadUniformBuffers = [&](UniformBufferObject ubo) {
                auto *uniformBuffersPtr = uniformBuffers[currentFrame].mapPtr();

                //diff: [test_dod10] start: NDC 必须统一啊。这是BUG的来源，难怪怎么都铺不满
                // ---- UI 渲染强制使用单位矩阵 ----
                // ubo.view = glm::mat4(1.0f);
                // ubo.proj = glm::mat4(1.0f);
                memcpy(uniformBuffersPtr, &ubo, sizeof(ubo));
            };

            if (batch.draw3DCount > 0)
            {
                uploadUniformBuffers(
                    {.view = camera.viewsMatrix(), .proj = camera.perspectiveMatrix()});
                commandBuffer.setDepthWriteEnable(true);
                commandBuffer.setDepthTestEnable(true);
                commandBuffer.drawIndexedIndirect(batch.indirectDrawBuffer.buffer(), 0,
                                                  batch.draw3DCount,
                                                  sizeof(VkDrawIndexedIndirectCommand));
            }
            if (batch.drawCountUI)
            {
                uploadUniformBuffers(
                    {.view = glm::mat4(1.0f), .proj = glm::mat4(1.0f)}); // 单位矩阵

#ifdef random_z
                // NOTE: 下面的代码，才能当作普通的卡片。 开启深度测试和模板测试就和相机有关系了。会被当作普通的UI。总之效果差别很大
                {
                    // uploadUniformBuffers({.view = camera.viewsMatrix(),
                    //                       .proj = camera.perspectiveMatrix()});
                }
                commandBuffer.setDepthWriteEnable(true); //NOTE: 先开启看看效果
                commandBuffer.setDepthTestEnable(true);
#else
                commandBuffer.setDepthWriteEnable(false);
                commandBuffer.setDepthTestEnable(false);
#endif //random_z
                VkDeviceSize offset =
                    batch.draw3DCount * sizeof(VkDrawIndexedIndirectCommand);
                commandBuffer.drawIndexedIndirect(batch.indirectDrawBuffer.buffer(),
                                                  offset, batch.drawCountUI,
                                                  sizeof(VkDrawIndexedIndirectCommand));
            }
            // diff: [test_dod11] end
            // diff: [test_dod8] start
        }>{},
        std::constant_wrapper<[](world_type &world) {
            auto &mainShaderCtx = world.mainShaderCtx;
            auto &recordCtx = world.recordCtx;
            auto &globalCtx = world.globalCtx;
            auto &pickCtx = world.pickCtx;

            auto &commandBuffers = globalCtx.commandBuffers;

            const auto &[currentFrame, imageIndex] = recordCtx.info;
            const auto &commandBuffer = commandBuffers[currentFrame];

            auto resolveImage = pickCtx.resolveResource.image();

            auto &pickingFrames = pickCtx.frames;
            auto &pickMouse = pickCtx.mouse;

            auto &mouseValid = pickMouse.valid;
            auto &mousePos = pickMouse.pos;

            commandBuffer.endRendering();
            if (mouseValid)
            {
                //diff: [test_indirectdraw_no_pick] start
                // 转换 resolve 目标到 TRANSFER_SRC
                my_render::transition_image_layout(
                    commandBuffer,
                    my_render::image_info{
                        .image = resolveImage,
                        .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .src = {VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT},
                        .dst = {VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                VK_ACCESS_2_TRANSFER_READ_BIT,
                                VK_PIPELINE_STAGE_2_TRANSFER_BIT}});
                //NOTE: 复制到 pickingFrames，就可以读取了，拿到着色器的输出
                commandBuffer.copyImageToBuffer(
                    resolveImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    pickingFrames[currentFrame].buffer(),
                    std::array<VkBufferImageCopy, 1>{VkBufferImageCopy{
                        .bufferOffset = 0,
                        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                        .imageOffset = {mousePos.x, mousePos.y, 0},
                        .imageExtent = {1, 1, 1}}});

                //diff: [test_indirectdraw_no_pick] end
            }
        }>{});
    // NOLINTNEXTLINE
    static constexpr auto recordCommandBuffer = [](world_type &world) {
        auto &globalCtx = world.globalCtx;
        auto &recordCtx = world.recordCtx;

        auto &swapchain = globalCtx.swapchain;
        auto &commandBuffers = globalCtx.commandBuffers;

        auto [currentFrame, imageIndex] = recordCtx.info;
        const auto &commandBuffer = commandBuffers[currentFrame];
        VkImage image = swapchain.image(imageIndex);

        commandBuffer.begin({.sType = sType<VkCommandBufferBeginInfo>()});
        mainPipeline.transitionImageLayout(world);
        mainPipeline.beginRendering(world);
        mainPipeline.setPipelineState(world);
        mainPipeline.draw(world);
        mainPipeline.endRendering(world);

        // After rendering, transition the swapchain image to PRESENT_SRC
        my_render::transition_image_layout(
            commandBuffer,
            my_render::image_info{
                .image = image,
                .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                .src = {.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        .access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                        .stage_mask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT},
                .dst = {.layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                        .access_mask = VK_ACCESS_2_NONE,
                        .stage_mask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT}});
        commandBuffer.end();
    };
    // diff: [test_dod5] end

    // NOLINTNEXTLINE
    static constexpr auto recreateSwapChain = [](world_type &world) constexpr {
        auto &globalCtx = world.globalCtx;
        auto &pickCtx = world.pickCtx;
        auto &mainCtx = world.mainCtx;

        auto &device = globalCtx.device;
        auto &surface = globalCtx.surface;
        auto &swapchainBuild = globalCtx.swapchainBuild;
        auto &swapchain = globalCtx.swapchain;
        auto &camera = globalCtx.camera;
        auto &frameContext = globalCtx.frameContext;

        auto &msaaResourcesBuild = mainCtx.msaaResourcesBuild;
        auto &depthResource = mainCtx.depthResource;
        auto &depthResourcesBuild = mainCtx.depthResourcesBuild;
        auto &msaaResource = mainCtx.msaaResource;

        auto &pickResourcesBuild = pickCtx.pickResourcesBuild;
        auto &pickResource = pickCtx.pickResource;
        auto &resolveResourcesBuild = pickCtx.resolveResourcesBuild;
        auto &resolveResource = pickCtx.resolveResource;
        auto &pickMouse = pickCtx.mouse;

        surface.waitGoodFramebufferSize();
        device.waitIdle();

        swapchain = swapchainBuild.rebuild();
        auto newExtent = swapchain.refImageExtent();
        VkExtent3D imageExtent = {
            .width = newExtent.width, .height = newExtent.height, .depth = 1};

        msaaResource = msaaResourcesBuild.setCreateInfoExtent(imageExtent).build(device);
        depthResource =
            depthResourcesBuild.setCreateInfoExtent(imageExtent).build(device);
        camera.refPerspectiveMatrix().aspect =
            newExtent.width / static_cast<float>(newExtent.height);

        pickResource = pickResourcesBuild.setCreateInfoExtent(imageExtent).build(device);
        resolveResource =
            resolveResourcesBuild.setCreateInfoExtent(imageExtent).build(device);

        pickMouse.valid = false;

        frameContext.rebuild(swapchain.imagesSize());
    };
    // diff: [test_dod5] start
    static auto s_lastFrameTime = std::chrono::high_resolution_clock::now();
    static constexpr auto limit_frame_rate = [](auto & /*world*/,
                                                float targetFPS = 60.0f) {
        const float targetFrameTime = 1.0f / targetFPS;
        auto now = std::chrono::high_resolution_clock::now();
        float elapsed = std::chrono::duration<float>(now - s_lastFrameTime).count();

        if (elapsed < targetFrameTime)
        {
            auto sleepTime = std::chrono::duration<float>(targetFrameTime - elapsed);
            std::this_thread::sleep_for(
                std::chrono::duration_cast<std::chrono::microseconds>(sleepTime));
            s_lastFrameTime = std::chrono::high_resolution_clock::now();
        }
        else
        {
            s_lastFrameTime = now;
        }
    };
    static constexpr auto update_delta_time = [](auto &world) {
        auto now = std::chrono::high_resolution_clock::now();
        float rawDelta = std::chrono::duration<float>(now - s_lastFrameTime).count();
        world.inputCtx.deltaTime = glm::clamp(rawDelta, 0.001f, 0.1f);
        s_lastFrameTime = now;
    };
    static constexpr auto FrameRateMonitor = [](auto & /*world*/) {
        static auto lastPrintTime = std::chrono::high_resolution_clock::now();
        static int frameCounter = 0;

        ++frameCounter;
        auto now = std::chrono::high_resolution_clock::now();
        float elapsed = std::chrono::duration<float>(now - lastPrintTime).count();

        if (elapsed >= 1.0f)
        {
            float fps = frameCounter / elapsed;
            float avgFrameTimeMs = elapsed * 1000.0f / frameCounter;
            std::println("[FPS] {:5.1f} (avg frame: {:.3f} ms)", fps, avgFrameTimeMs);
            frameCounter = 0;
            lastPrintTime = now;
        }
    };
    // diff: [test_dod5] end
    static constexpr auto InputController = [](auto &world) {
        auto &inputCtx = world.inputCtx;
        auto &pickCtx = world.pickCtx;
        auto &globalCtx = world.globalCtx;
        auto &soaCtx = world.soaCtx;

        auto &input = inputCtx.input;
        auto &pickMouse = pickCtx.mouse;
        auto &swapchain = globalCtx.swapchain;

        surface::pollEvents();

        auto cur = input.cursorPos();
        auto ext = swapchain.refImageExtent();
        if (cur.xpos >= 0 && cur.xpos < static_cast<int>(ext.width) && cur.ypos >= 0 &&
            cur.ypos < static_cast<int>(ext.height))
        {
            pickMouse.pos = {cur.xpos, cur.ypos};
            pickMouse.valid = true;
        }
        else
        {
            pickMouse.valid = false;
        }

        // 每1秒添加一批实体，直到总数达到1000
        {
            auto &autoSpinStore = world.soaCtx.autoSpinStore;
            constexpr int TARGET_ENTITIES = 1000;
            constexpr int BATCH_SIZE = 100;

            if (autoSpinStore.size() < TARGET_ENTITIES)
            {
                static auto lastAddTime = std::chrono::steady_clock::now();
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                                          lastAddTime)
                        .count() >= 1000)
                {
                    lastAddTime = now;
                    int toAdd =
                        std::min(BATCH_SIZE, TARGET_ENTITIES -
                                                 static_cast<int>(autoSpinStore.size()));

                    if (toAdd > autoSpinStore.free_size())
                        autoSpinStore.expansion_size(toAdd - autoSpinStore.free_size());

                    std::mt19937 rng(std::random_device{}());
                    std::uniform_real_distribution<float> colorDist(0.0f, 1.0f);
                    std::uniform_real_distribution<float> posDist(-1.5f,
                                                                  1.5f); // 散开范围
                    std::uniform_real_distribution<float> scaleDist(0.01f,
                                                                    0.05f); // 粒子极小
                    // 可选：随机使用不同纹理和采样器
                    std::uniform_int_distribution<int> texDist(0, 1);
                    std::uniform_int_distribution<int> sampDist(0, 1);

                    for (int i = 0; i < toAdd; ++i)
                    {
                        float scale = scaleDist(rng);
                        glm::vec3 pos(posDist(rng), posDist(rng), 0.0f);

                        // 构建模型矩阵：先缩放，后平移（重要！否则位置会被缩放影响）
                        glm::mat4 model = glm::translate(glm::mat4(1.0f), pos) *
                                          glm::scale(glm::mat4(1.0f), glm::vec3(scale));
                        // 若需要粒子旋转（不关心朝向，可加随机旋转）
                        // model = model * glm::toMat4(glm::angleAxis(posDist(rng), glm::vec3(0,0,1)));

                        uint32_t texIdx = texDist(rng) ? 3 : 0;   // 纹理0或3（棋盘/渐变）
                        uint32_t sampIdx = sampDist(rng) ? 1 : 0; // 采样器0或1

                        InstanceData instData{
                            .objectId = static_cast<uint32_t>(autoSpinStore.size()),
                            .textureIndex = texIdx,
                            .samplerIndex = sampIdx,
                            .objectData = object_data{model}};

                        std::array<VertexAttribute, 4> attrs;
                        for (auto &attr : attrs)
                            attr.color =
                                glm::vec3(colorDist(rng), colorDist(rng), colorDist(rng));

                        autoSpinStore.new_entity(instData, attrs);
                    }
                }
            }
        }
    };

    //NOTE: 下面的内联做的更好，编译期的数据更利于优化
    static constexpr auto vulaknDataTransformer =
        make_aggregate<"vulaknDataTransformer", "ProcessingWorldInput_0",
                       "AfterWaitForFences_0", "BeforeRecordCommandBuffer_0">(
            std::constant_wrapper<[](world_type &world) {
                // limit_frame_rate(world);  // ① 帧率限制（可选）
                update_delta_time(world); // ② 必须在帧率限制之后，保证 delta 正确
                InputController(world);
                views_matrix_update(world);
                views_perspective_update(world);
                model_update(world);
                FrameRateMonitor(world);            // ③ 纯监控
                world.inputCtx.input.scroll() = {}; // 滚轮清零放在最后
            }>{},
            std::constant_wrapper<[](world_type &world) {
                auto &globalCtx = world.globalCtx;
                auto &pickCtx = world.pickCtx;

                auto &frameContext = globalCtx.frameContext;

                auto &pickMouse = pickCtx.mouse;
                auto &pickingFrames = pickCtx.frames;

                auto &currentFrame = frameContext.currentFrame;

                // 等待栅栏后，正式获取图像前
                if (currentFrame > 0 && pickMouse.valid)
                {
                    uint32_t readIdx =
                        (currentFrame - 1 + MAX_FRAMES_IN_FLIGHT) % MAX_FRAMES_IN_FLIGHT;
                    auto *data = static_cast<uint32_t *>(pickingFrames[readIdx].mapPtr());
                    uint32_t objId = data[0];
                    uint32_t primId = data[1];
                    if (objId != 0xFFFFFFFF)
                    {
                        std::cout << "Hover: Object=" << objId << ", Triangle=" << primId
                                  << "\n";
                    }
                }
            }>{},
            std::constant_wrapper<[](world_type &world) {
                auto &globalCtx = world.globalCtx;
                auto &mainShaderCtx = world.mainShaderCtx;

                auto &frameContext = globalCtx.frameContext;
                auto &camera = globalCtx.camera;

                auto &currentFrame = frameContext.currentFrame;

                auto &uniformBuffers = mainShaderCtx.uniformBuffers;

                //diff: [test_dod3] start
                updateObjectData(world, currentFrame);
                updateVertexData(world, currentFrame);
                prepareBatch(world, currentFrame);
                //diff: [test_dod3] end
            }>{});

    // NOLINTNEXTLINE
    static constexpr auto drawFrame = [](world_type &world) constexpr {
        auto &globalCtx = world.globalCtx;
        auto &pickCtx = world.pickCtx;
        auto &recordCtx = world.recordCtx;

        auto &device = globalCtx.device;

        auto &swapchain = globalCtx.swapchain;
        auto &frameContext = globalCtx.frameContext;
        auto &window = globalCtx.window;
        auto &commandBuffers = globalCtx.commandBuffers;
        const auto &GRAPHICS_AND_PRESENT = globalCtx.queue;

        auto &pickingFrames = pickCtx.frames;
        auto &pickMouse = pickCtx.mouse;

        auto &inFlightFences = frameContext.inFlightFences;
        auto &currentFrame = frameContext.currentFrame;
        auto &presentCompleteSemaphore = frameContext.presentCompleteSemaphore;
        auto &semaphoreIndex = frameContext.semaphoreIndex;
        auto &renderFinishedSemaphore = frameContext.renderFinishedSemaphore;

        while (device.waitForFences(1, inFlightFences[currentFrame], VK_TRUE,
                                    UINT64_MAX) == VK_TIMEOUT)
            ;

        invoke_aggregate_ranges<"AfterWaitForFences_", 0, 0>(vulaknDataTransformer,
                                                             world);

        auto [result, imageIndex] = swapchain.acquireNextImage(
            UINT64_MAX, presentCompleteSemaphore[semaphoreIndex], nullptr);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        {
            recreateSwapChain(world);
            return;
        }
        if (result != VK_SUCCESS)
            throw std::runtime_error("failed to acquire swap chain image!");
        device.resetFences(1, inFlightFences[currentFrame]);

        invoke_aggregate_ranges<"BeforeRecordCommandBuffer_", 0, 0>(vulaknDataTransformer,
                                                                    world);

        const auto &commandBuffer = commandBuffers[currentFrame];
        commandBuffer.reset({});
        recordCtx.info = {.current_frame = currentFrame, .image_index = imageIndex};

        recordCommandBuffer(world);

        // NOLINTNEXTLINE
        VkPipelineStageFlags waitDestinationStageMask[] = {
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        GRAPHICS_AND_PRESENT.submit(
            1,
            {.sType = sType<VkSubmitInfo>(),
             .waitSemaphoreCount = 1,
             .pWaitSemaphores = &presentCompleteSemaphore[semaphoreIndex],
             .pWaitDstStageMask = waitDestinationStageMask,
             .commandBufferCount = 1,
             .pCommandBuffers = &*commandBuffer,
             .signalSemaphoreCount = 1,
             .pSignalSemaphores = &renderFinishedSemaphore[imageIndex]},
            inFlightFences[currentFrame]);

        result = GRAPHICS_AND_PRESENT.presentKHR(
            {.sType = sType<VkPresentInfoKHR>(),
             .waitSemaphoreCount = 1,
             .pWaitSemaphores = &renderFinishedSemaphore[imageIndex],
             .swapchainCount = 1,
             .pSwapchains = &(*swapchain),
             .pImageIndices = &imageIndex});
        if (auto &framebufferResized = window.refFramebufferResized();
            result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
            framebufferResized)
        {
            framebufferResized = false;
            recreateSwapChain(world);
        }
        semaphoreIndex = (semaphoreIndex + 1) % presentCompleteSemaphore.size();
        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    };
    while (globalCtx.window.shouldClose() == 0)
    {
        invoke_aggregate_ranges<"ProcessingWorldInput_", 0, 0>(vulaknDataTransformer,
                                                               world);

        // ========== 新增：每 2 秒切换 autoSpinStore 第一个实体的纹理索引 ==========
        {
            auto &autoSpinStore = world.soaCtx.autoSpinStore;
            if (autoSpinStore.size() > 0) // 确保至少有一个实体
            {
                static auto lastTexSwitch = std::chrono::steady_clock::now();
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - lastTexSwitch)
                        .count() >= 2)
                {
                    lastTexSwitch = now;

                    // 直接通过 view_entity 获取第一个实体的 textureIndex 引用并修改
                    // 注意：实体 ID 是 0（第一个创建的实体）
                    auto [texRef] = autoSpinStore.view_entity<"instanceData">(0, 0);
                    // 在 0 和 3 之间切换（这两个槽位已经绑定了纹理，确保有效）
                    texRef.textureIndex = (texRef.textureIndex == 0) ? 3 : 0;
                }
            }
        }
        // ======================================================================
        drawFrame(world);

        // FPS 统计
        {
            static auto lastTime = std::chrono::high_resolution_clock::now();
            static int frameCount = 0;
            static int totalFrames = 0;
            static float accumulatedFPS = 0.0f;
            static int avgSamples = 0;

            frameCount++;
            totalFrames++;

            auto now = std::chrono::high_resolution_clock::now();
            float elapsed = std::chrono::duration<float>(now - lastTime).count();
            if (elapsed >= 1.0F)
            {
                float fps = frameCount / elapsed;
                accumulatedFPS += fps;
                avgSamples++;
                float avgFPS = accumulatedFPS / avgSamples;
                float frameTimeMs = (elapsed / frameCount) * 1000.0f;
                std::println(
                    "FPS: {:.1f} (avg: {:.1f}) | FrameTime: {:.2f}ms | Total: {}", fps,
                    avgFPS, frameTimeMs, totalFrames);
                frameCount = 0;
                lastTime = now;
            }
        }
    }
    device.waitIdle();

    // diff: [test_dod2] 不再有 手动 map unmap

    std::cout << "main done\n";
    return 0;
}
catch (std::exception &e)
{
    std::println("main catch exception: {}", e.what());
}