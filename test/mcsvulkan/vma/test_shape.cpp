#include <algorithm>
#include <array>
#include <cassert>
#include <codecvt>
#include <optional>
#include <print>
#include <set>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <cstdint>
#include <exception>
#include <iostream>
#include <print>
#include <chrono>
#include <stdexcept>
#include <utility>

// diff: [test_texture2.cpp] start
// #include <msdfgen.h>
// #include <msdfgen-ext.h>
// diff: [test_texture2.cpp] end

// diff: [test_texture3.cpp] start
// #include <nlohmann/json.hpp>

// diff: [test_texture3.cpp] end

// NOTE: 布局添加的头文件
// diff: [test_msdf_atlas_gen3] start
// #include <ft2build.h>
// #include FT_FREETYPE_H

#include <hb.h>
#include <hb-ft.h>
// #include <raqm.h>                //NOTE: 不需要,因为和换行不搭
#include <utf8proc.h>            // 新增：utf8proc 头文件
#include <SheenBidi/SheenBidi.h> //diff: [test_bidi] bidi
#include <SheenBidi/SBScript.h>
#include <linebreak.h>   // libunibreak 核心头文件
#include <unibreakdef.h> // 基础定义
// diff: [test_msdf_atlas_gen3] end

#include "../head.hpp"
using json = nlohmann::json;

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

using buffer_base = mcs::vulkan::vma::buffer_base;
using mcs::vulkan::vma::create_buffer;
using mcs::vulkan::vma::staging_buffer;

using mcs::vulkan::vma::create_resources;
using mcs::vulkan::vma::create_image;

using mcs::vulkan::tool::simple_copy_buffer;

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;
constexpr auto TITLE = "test_my_triangle";
using mcs::vulkan::MCS_ASSERT;

static constexpr auto MAX_FRAMES_IN_FLIGHT = 2;

// diff: [test_msdf_atlas_gen] start
using mcs::vulkan::vma::create_texture_image;
using mcs::vulkan::tool::create_sampler;
using texture_image_base = mcs::vulkan::vma::texture_image_base;
using raw_stbi_image = mcs::vulkan::load::raw_stbi_image;
using Sampler = mcs::vulkan::Sampler;
// diff: [test_msdf_atlas_gen]

// 新增：返回 UTF-8 字符的字节数（根据首字节）
static int utf8_char_len(unsigned char c)
{
    if (c < 0x80)
        return 1;
    else if ((c >> 5) == 0x06)
        return 2; // 110xxxxx
    else if ((c >> 4) == 0x0E)
        return 3; // 1110xxxx
    else if ((c >> 3) == 0x1E)
        return 4; // 11110xxx
    else
        return 1; // 无效序列，保守处理为 1 字节
}

// 新增：解码当前 UTF-8 字符，返回码点，并移动指针
static uint32_t utf8_decode(const unsigned char **ptr)
{
    const unsigned char *p = *ptr;
    uint32_t cp;
    int len = utf8_char_len(*p);
    if (len == 1)
    {
        cp = *p;
        *ptr = p + 1;
    }
    else if (len == 2)
    {
        cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
        *ptr = p + 2;
    }
    else if (len == 3)
    {
        cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        *ptr = p + 3;
    }
    else if (len == 4)
    {
        cp = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) |
             (p[3] & 0x3F);
        *ptr = p + 4;
    }
    else
    {
        cp = *p;
        *ptr = p + 1;
    }
    return cp;
}

// 新增：将 UTF-32 码点编码为 UTF-8 并输出到 stdout
static void print_utf32(uint32_t cp)
{
    if (cp < 0x80)
    {
        putchar((char)cp);
    }
    else if (cp < 0x800)
    {
        putchar((char)(0xC0 | (cp >> 6)));
        putchar((char)(0x80 | (cp & 0x3F)));
    }
    else if (cp < 0x10000)
    {
        putchar((char)(0xE0 | (cp >> 12)));
        putchar((char)(0x80 | ((cp >> 6) & 0x3F)));
        putchar((char)(0x80 | (cp & 0x3F)));
    }
    else if (cp <= 0x10FFFF)
    {
        putchar((char)(0xF0 | (cp >> 18)));
        putchar((char)(0x80 | ((cp >> 12) & 0x3F)));
        putchar((char)(0x80 | ((cp >> 6) & 0x3F)));
        putchar((char)(0x80 | (cp & 0x3F)));
    }
}
// C++23：char32_t 转 UTF-8 字符串（修复to_utf8缺失问题）
std::string to_utf8(char32_t cp)
{
    std::string utf8;
    if (cp <= 0x7F)
    {
        utf8 += static_cast<char>(cp);
    }
    else if (cp <= 0x7FF)
    {
        utf8 += static_cast<char>(0xC0 | (cp >> 6));
        utf8 += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else if (cp <= 0xFFFF)
    {
        utf8 += static_cast<char>(0xE0 | (cp >> 12));
        utf8 += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        utf8 += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else if (cp <= 0x10FFFF)
    {
        utf8 += static_cast<char>(0xF0 | (cp >> 18));
        utf8 += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        utf8 += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        utf8 += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return utf8;
}

// C++23：UTF-8 字符串 转 Unicode 码点数组（核心修复：先转码点，再用SBRun截取）
std::vector<char32_t> utf8_to_codepoints(const char *utf8)
{
    std::vector<char32_t> cps;
    const uint8_t *data = reinterpret_cast<const uint8_t *>(utf8);
    while (*data)
    {
        char32_t cp = 0;
        size_t len = 0;
        if (*data < 0x80)
        {
            cp = *data;
            len = 1;
        }
        else if ((*data & 0xE0) == 0xC0)
        {
            cp = (*data & 0x1F) << 6 | data[1] & 0x3F;
            len = 2;
        }
        else if ((*data & 0xF0) == 0xE0)
        {
            cp = (*data & 0x0F) << 12 | (data[1] & 0x3F) << 6 | data[2] & 0x3F;
            len = 3;
        }
        else if ((*data & 0xF8) == 0xF0)
        {
            cp = (*data & 0x07) << 18 | (data[1] & 0x3F) << 12 | (data[2] & 0x3F) << 6 |
                 data[3] & 0x3F;
            len = 4;
        }
        else
        {
            len = 1;
        }
        if (cp)
            cps.push_back(cp);
        data += len;
    }
    return cps;
}

struct my_render
{
    // NOLINTNEXTLINE
    constexpr static void transition_image_layout(
        const CommandBufferView &commandBuffer, VkImage image,
        VkImageAspectFlags aspectMask, VkImageLayout oldLayout, VkImageLayout newLayout,
        VkAccessFlags2 srcAccessMask,
        VkAccessFlags2 dstAccessMask, // NOLINT
        VkPipelineStageFlags2 srcStageMask,
        VkPipelineStageFlags2 dstStageMask // NOLINT
        ) noexcept
    {
        VkImageMemoryBarrier2 barrier = {
            .sType = sType<VkImageMemoryBarrier2>(),
            // Specify the pipeline stages and access masks for the barrier
            .srcStageMask = srcStageMask,
            .srcAccessMask = srcAccessMask,
            .dstStageMask = dstStageMask,
            .dstAccessMask = dstAccessMask,
            // Specify the old and new layouts of the image
            .oldLayout = oldLayout,
            .newLayout = newLayout,
            // We are not changing the ownership between queues
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            // Specify the image to be affected by this barrier
            .image = image,
            // Define the subresource range (which parts of the image are affected)
            .subresourceRange = {.aspectMask = aspectMask,
                                 .baseMipLevel = 0,
                                 .levelCount = 1,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1}};
        VkDependencyInfo dependency_info = {.sType = sType<VkDependencyInfo>(),
                                            .dependencyFlags = {},
                                            .imageMemoryBarrierCount = 1,
                                            .pImageMemoryBarriers = &barrier};
        commandBuffer.pipelineBarrier2(dependency_info);
    }
};

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

struct PushConstants
{
    uint64_t vertexBufferAddress; // 使用uint64_t而不是VkDeviceAddress

    // diff: [test_msdf_atlas_gen1] start
    // diff: [texture] start
    // uint32_t textureIndex; // 添加纹理索引
    // uint32_t samplerIndex; // 添加采样器索引
    // diff: [texture] end
    // diff: [test_msdf_atlas_gen1] end
};
struct Vertex // NOLINT
{
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord; // diff: [texture] 添加纹理坐标

    // diff: [test_msdf_atlas_gen1] start
    uint32_t textureIndex; // 4 纹理索引
    uint32_t samplerIndex; // 4 采样器索引
    uint32_t fontType;     // 4 (枚举对齐到 uint32_t)
    float pxRange{8};      // 4 float
    // diff: [test_msdf_atlas_gen1] end

    uint32_t modulateFlag; // diff: [test_emoji]
};
static_assert(alignof(Vertex) == 4, "check alignof error");
static_assert(sizeof(Vertex) == 32 + 12 + 4 + 4, "check sizeof error");

struct mesh_base
{
    using index_type = uint32_t;
    // 双缓冲顶点数据：每个飞行帧都有自己的缓冲区
    struct FrameBuffers
    {
        buffer_base vertexBuffer;
        buffer_base indexBuffer;
        VkDeviceAddress vertexBufferAddress = 0;
        uint32_t indexCount = 0; // diff: [test_libunibreak3] 新增：该帧的索引数量
    };

    static consteval auto indexType()
    {
        return VK_INDEX_TYPE_UINT32; // 必须匹配 index_type
    }

    struct mesh_data
    {
        std::vector<Vertex> vertices;
        std::vector<index_type> indices; // NOTE: 需要保留
        void clear()
        {
            vertices.clear();
        }
    };

    const CommandPool *commandpool{};
    const Queue *queue{};

    std::array<FrameBuffers, MAX_FRAMES_IN_FLIGHT> frameBuffers;
    mesh_data queue_data;
    uint32_t count = 0;

    mesh_base(VmaAllocator allocator, const CommandPool &pool, const Queue &queue,
              mesh_data init_data)
        : commandpool{&pool}, queue{&queue}, queue_data{std::move(init_data)}
    {
        for (auto &fb : frameBuffers)
        {
            createVertexBufferForFrame(allocator, fb);
            createIndexBufferForFrame(allocator, fb);
            getBufferDeviceAddresses(fb);
        }
    }

    // buffer 内存要求
    static constexpr auto REQUIRE_BUFFER_USAGE =
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    constexpr void createVertexBufferForFrame(VmaAllocator allocator, FrameBuffers &fb)
    {
        auto &vertices = queue_data.vertices;
        const VkDeviceSize BUFFER_SIZE = sizeof(vertices[0]) * vertices.size();
        auto &destBuffer = fb.vertexBuffer;
        constexpr auto USAGE = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

        // 直接使用重构后的 create_buffer 函数
        destBuffer = create_buffer(
            allocator,
            {.sType = sType<VkBufferCreateInfo>(),
             .size = BUFFER_SIZE,
             .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | USAGE | REQUIRE_BUFFER_USAGE,
             .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
            {
                .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
                .usage = VMA_MEMORY_USAGE_AUTO,
            });

        // 创建暂存缓冲区并复制数据
        const auto STAGING_BUFFER = staging_buffer(allocator, BUFFER_SIZE);
        STAGING_BUFFER.copyDataToBuffer(vertices.data(), BUFFER_SIZE);

        simple_copy_buffer(*commandpool, *queue, STAGING_BUFFER.buffer(),
                           destBuffer.buffer(), VkBufferCopy{.size = BUFFER_SIZE});
    }

    void createIndexBufferForFrame(VmaAllocator allocator, FrameBuffers &fb)
    {
        auto &indices = queue_data.indices;
        const VkDeviceSize BUFFER_SIZE = sizeof(indices[0]) * indices.size();
        auto &destBuffer = fb.indexBuffer;
        fb.indexCount = static_cast<uint32_t>(indices.size()); // 保存索引数
        constexpr auto USAGE = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

        // 直接使用重构后的 create_buffer 函数
        destBuffer = create_buffer(
            allocator,
            {.sType = sType<VkBufferCreateInfo>(),
             .size = BUFFER_SIZE,
             .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | USAGE | REQUIRE_BUFFER_USAGE,
             .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
            {.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
             .usage = VMA_MEMORY_USAGE_AUTO});

        // 创建暂存缓冲区并复制数据
        const auto STAGING_BUFFER = staging_buffer(allocator, BUFFER_SIZE);
        STAGING_BUFFER.copyDataToBuffer(indices.data(), BUFFER_SIZE);

        simple_copy_buffer(*commandpool, *queue, STAGING_BUFFER.buffer(),
                           destBuffer.buffer(), {VkBufferCopy{.size = BUFFER_SIZE}});
    }

    void getBufferDeviceAddresses(FrameBuffers &fb) const
    {
        const auto *device = commandpool->device();
        if (fb.vertexBuffer.buffer() != VK_NULL_HANDLE)
        {
            fb.vertexBufferAddress = device->getBufferDeviceAddress(
                {.sType = sType<VkBufferDeviceAddressInfo>(),
                 .buffer = fb.vertexBuffer.buffer()});
        }
    }

    [[nodiscard]] VkDeviceAddress getVertexBufferAddress(
        uint32_t currentFrame) const noexcept
    {
        return frameBuffers[currentFrame].vertexBufferAddress;
    }

    // 只排队，不立即执行
    void queueUpdate(std::vector<Vertex> vertices, std::vector<index_type> indices)
    {
        queue_data.vertices = std::move(vertices);
        queue_data.indices = std::move(indices);
        count = 2;
    }

    void requestUpdate(VmaAllocator allocator, uint32_t currentFrame)
    {
        if (count == 0)
        {
            queue_data.clear();
            return;
        }
        --count;

        // 更新GPU缓冲区（这个帧当前没有被GPU使用）
        auto &fb = frameBuffers[currentFrame];
        createVertexBufferForFrame(allocator, fb);
        createIndexBufferForFrame(allocator, fb);
        getBufferDeviceAddresses(fb);
    }

    constexpr void clear() noexcept
    {
        frameBuffers = {};
    }
};

// vma conifg ---------------------------------------------
constexpr auto VMA_USE_BIND_MEMORY2 = true;
constexpr auto VMA_USE_MEMORY_BUDGET = true;
constexpr auto VMA_USE_BUFFER_DEVICE_ADDRESS = true;

static auto initVmaFlag(std::vector<const char *> &requiredDeviceExtension)
{
    // NOTE: 配置 vma 的 flags
    VmaAllocatorCreateFlags vma_flags = 0;
    // @see
    // https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/quick_start.html#:~:text=of%20this%20function.-,Enabling%20extensions,-VMA%20can%20automatically
#if VK_USE_PLATFORM_WIN32_KHR
    std::cout << "vma_flags config with WIN32 \n";
    requiredDeviceExtension.emplace_back("VK_KHR_external_memory_win32");
    vma_flags |= VMA_ALLOCATOR_CREATE_KHR_EXTERNAL_MEMORY_WIN32_BIT;
#endif
    if constexpr (VMA_USE_BIND_MEMORY2)
    {
        std::cout << "vma_flags config with VK_KHR_bind_memory2 \n";
        requiredDeviceExtension.emplace_back("VK_KHR_bind_memory2");
        vma_flags |= VMA_ALLOCATOR_CREATE_KHR_BIND_MEMORY2_BIT;
    }
    if constexpr (VMA_USE_MEMORY_BUDGET)
    {
        std::cout << "vma_flags config with VK_EXT_memory_budget \n";
        requiredDeviceExtension.emplace_back("VK_EXT_memory_budget");
        vma_flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
    }
    if constexpr (VMA_USE_BUFFER_DEVICE_ADDRESS)
    {
        std::cout << "vma_flags config with VK_KHR_buffer_device_address \n";
        requiredDeviceExtension.emplace_back("VK_KHR_buffer_device_address");
        vma_flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    }
    return vma_flags;
}
static void updateVmaFlag(VmaAllocatorCreateFlags &vma_flags,
                          PhysicalDevice &physicalDevice,
                          std::vector<const char *> &requiredDeviceExtension)
{
    VkPhysicalDeviceProperties properties = physicalDevice.getProperties();
    if (properties.vendorID == 0x1002)
    {
        std::cout << "vma_flags config with amd gpu \n";
        requiredDeviceExtension.emplace_back(
            VK_AMD_DEVICE_COHERENT_MEMORY_EXTENSION_NAME);
        vma_flags |= VMA_ALLOCATOR_CREATE_AMD_DEVICE_COHERENT_MEMORY_BIT;
    }
}

// diff: [Uniform] start
class UniformBufferObject
{
  public:
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};
// diff: [Uniform] end

// diff: [test_msdf_atlas_gen] start: 删掉 [texture] 范围的代码
//  diff: [texture] start
//  全部删掉
// diff: [texture] end
// diff: [test_msdf_atlas_gen] end

namespace font
{
    // 2. Library Initialization
    using freetype_version = mcs::vulkan::font::freetype::version;
    using freetype_loader = mcs::vulkan::font::freetype::loader;
    using freetype_face = mcs::vulkan::font::freetype::face;

    using Font = mcs::vulkan::font::Font;
    using FontTexture = mcs::vulkan::font::FontTexture;
    using FontType = mcs::vulkan::font::FontType;

    using harfbuzz_font = mcs::vulkan::font::harfbuzz::font;

    using texture_bind_sampler = mcs::vulkan::font::texture_bind_sampler;
    using FontContext = mcs::vulkan::font::FontContext;

    using glyph_info = mcs::vulkan::font::GlyphInfo;
    using texture_info = mcs::vulkan::font::texture_info;
    using FontInfo = mcs::vulkan::font::FontInfo;
    using font_register = mcs::vulkan::font::font_register;

    using font_registration = mcs::vulkan::font::font_registration;
    using FontSelector = mcs::vulkan::font::FontSelector;
    using FontFactory = mcs::vulkan::font::FontFactory;
    using FontMetadata = mcs::vulkan::font::FontMetadata;

    class FontLibrary
    {

        std::vector<FontContext> fonts_;
        struct font_value
        {
            size_t font_index;
            int score;

            [[nodiscard]] glyph_info getDefaultGlyph(
                Font::glyphs_type::UNICODE_CODEPOINT unicode,
                const FontLibrary &lib) const
            {
                auto &font = lib.fonts_[font_index];
                assert(!font.unicode_default_glyphs.empty());
                assert(font.unicode_default_glyphs.contains(unicode));
                auto &glyph = font.unicode_default_glyphs.at(
                    unicode); // 使用 at() 而不是 operator[]
                return glyph_info::make(*glyph, font);
            }
        };
        struct font_value_greater
        {
            constexpr bool operator()(const font_value &a,
                                      const font_value &b) const noexcept
            {
                if (a.score != b.score)
                    return a.score > b.score; // 分数越大越靠前
                return a.font_index < b.font_index;
            }
        };
        // 在 unordered_multimap 的值类型中使用该比较器
        std::unordered_multimap<char32_t, std::set<font_value, font_value_greater>>
            unicodeDefaultFont_;

      public:
        auto &operator[](size_t i) noexcept
        {
            return fonts_[i];
        }
        auto &operator[](size_t i) const noexcept
        {
            return fonts_[i];
        }

        FontLibrary() = default;
        void load(FontType type, texture_bind_sampler bind,
                  const FontTexture::msdf_info &info, const std::string &jsonPath,
                  freetype_face face, int score = 0)
        {
            // 先构造一个完整的 FontAtlas 对象（但此时 hb_font 还未初始化）
            fonts_.emplace_back(jsonPath, info, std::move(face), type, bind,
                                FontMetadata{});

            auto &newFont = fonts_.back();

            // 遍历当前字体的 Unicode→字形信息映射
            for (const auto &[ch, glyphPtr] : newFont.unicode_default_glyphs)
            {
                size_t font_index = fonts_.size() - 1; // 当前字体索引
                font_value fv{font_index, score};
                auto it = unicodeDefaultFont_.find(ch);
                if (it == unicodeDefaultFont_.end())
                {
                    // 首次出现该 Unicode，创建新的有序集合
                    std::set<font_value, font_value_greater> s;
                    s.insert(fv);
                    unicodeDefaultFont_.emplace(ch, std::move(s));
                }
                else
                {
                    // 已存在，直接插入（set 会自动排序并去重）
                    it->second.insert(fv);
                }
            }

            std::println("Font {} loaded, glyph_index_to_glyphs size: {},  "
                         "unicode_default_glyphs size: {}",
                         newFont.name, newFont.glyph_index_to_glyphs.size(),
                         newFont.unicode_default_glyphs.size());
            if (newFont.unicode_default_glyphs.empty())
            {
                std::println("Warning: unicode_default_glyphs is empty for font {}",
                             newFont.name);
            }
        }
        [[nodiscard]] std::optional<font_value> getFont(char32_t unicode) const noexcept
        {
            auto it = unicodeDefaultFont_.find(unicode);
            if (it != unicodeDefaultFont_.end() && !it->second.empty())
                return *it->second.begin(); // 返回集合中第一个元素（最高分字体）

            return std::nullopt;
        }

        // 从 fonts_ 集合 得到 texture_image_base 的 views 集合
        [[nodiscard]] auto getTextureImageBases() const noexcept
        {
            return fonts_ | std::views::transform([](const FontContext &atlas) noexcept
                                                      -> const texture_image_base & {
                       return atlas.texture.view();
                   });
        }
    };
}; // namespace font

/*
MSDF
利用纹理中的多通道距离数据，经采样器线性插值后计算透明度，再通过混合与背景合成，实现抗锯齿效果。这种技术尤其适合字体渲染，能在放大时保持字形锐利，特别是拐角处无模糊。
NOTE: MSDF原理， 混合是透明度渲染的基础
MSDF 的抗锯齿效果需要三个条件同时满足：
纹理图像：格式为线性（如 UNORM），存储的距离值准确。
采样器：线性过滤，确保距离值插值正确。
混合：启用透明度混合，将着色器计算出的透明度与背景合成，形成平滑边缘。
*/
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
                                     // apiVersion必须是应用程序设计使用的Vulkan的最高版本
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
    // diff: 添加扩展
    requiredDeviceExtension.emplace_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);

    // diff: [new] 启用bufferDeviceAddress特性
    structure_chain<VkPhysicalDeviceFeatures2, VkPhysicalDeviceVulkan13Features,
                    VkPhysicalDeviceVulkan12Features, // diff: 添加Vulkan 1.2特性
                    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT>
        enablefeatureChain = {
            {.features =
                 {// diff: [texture] 纹理硬件各向异性过滤
                  .samplerAnisotropy = VK_TRUE,
                  .shaderInt64 = VK_TRUE}}, // diff: shader 扩展这里也要打开
            {.synchronization2 = VK_TRUE, .dynamicRendering = VK_TRUE},
            {

                // diff: [texture] start 在Vulkan 1.2特性中设置描述符索引特性
                .descriptorIndexing = VK_TRUE, // 必须启用

                // .descriptorBindingPartiallyBound = VK_TRUE,
                // .descriptorBindingVariableDescriptorCount = VK_TRUE,

                .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
                .runtimeDescriptorArray = VK_TRUE,
                // diff: [texture] end

                .scalarBlockLayout = VK_TRUE, // diff: [new] 启用标量块布局
                .bufferDeviceAddress =
                    VK_TRUE, // diff: [new] 在Vulkan 1.3中也启用bufferDeviceAddress
            },
            {.extendedDynamicState = VK_TRUE}};

    // NOTE: 配置 vma 的 flags
    VmaAllocatorCreateFlags vma_flags = initVmaFlag(requiredDeviceExtension);

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
                auto query =
                    structure_chain<VkPhysicalDeviceFeatures2,
                                    VkPhysicalDeviceVulkan13Features,
                                    VkPhysicalDeviceVulkan12Features,
                                    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT>{
                        {}, {}, {}, {}};
                physicalDevice.getFeatures2(&query.head());
                auto &features2 = query.template get<VkPhysicalDeviceFeatures2>();
                auto &query_vulkan13_features =
                    query.template get<VkPhysicalDeviceVulkan13Features>();
                auto &query_vulkan12_features = // diff: [new] 检查Vulkan 1.2特性
                    query.template get<VkPhysicalDeviceVulkan12Features>();
                auto &query_extended_dynamic_state_features =
                    query.template get<VkPhysicalDeviceExtendedDynamicStateFeaturesEXT>();
                return features2.features
                           .samplerAnisotropy && // diff: [texture] 采样特性检查
                       features2.features.shaderInt64 &&
                       query_vulkan13_features.dynamicRendering &&
                       query_vulkan13_features.synchronization2 &&

                       query_vulkan12_features
                           .bufferDeviceAddress && // diff: [new]
                                                   // 检查Vulkan 1.2中的bufferDeviceAddress
                       query_vulkan12_features
                           .scalarBlockLayout && // diff: [new]
                                                 // 检查scalarBlockLayout
                       // diff: [texture] start 检查Vulkan 1.2中的描述符索引特性
                       query_vulkan12_features.runtimeDescriptorArray &&
                       //    query_vulkan12_features.descriptorBindingPartiallyBound &&
                       //    query_vulkan12_features.descriptorBindingVariableDescriptorCount
                       //    &&
                       query_vulkan12_features.descriptorIndexing &&
                       query_vulkan12_features
                           .shaderSampledImageArrayNonUniformIndexing &&
                       // diff: [texture] end
                       query_extended_dynamic_state_features.extendedDynamicState;
            })
            .select()[0];

    // NOTE: 根据GPU 配置 vma
    updateVmaFlag(vma_flags, physical_device, requiredDeviceExtension);

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
                // NOTE: 下面的方式是错误的。因为 features.next=null
                //  .pEnabledFeatures = &enablefeatureChain.head().features,
            })
            .build(physical_device);
    requiredDeviceExtension.clear();

    // NOTE: 逻辑设备确定后，就能初始化 vma了
    raii_vma vma{{.flags = vma_flags,
                  .physicalDevice = *physical_device,
                  .device = *device,
                  .instance = *instance,
                  .vulkanApiVersion = APIVERSION}};
    [[maybe_unused]] VmaAllocator allocator = vma.allocator();

    const auto GRAPHICS_AND_PRESENT = Queue(
        device, {.queue_family_index = GRAPHICS_QUEUE_FAMILY_IDX, .queue_index = 0});

    auto swapchainBuild =
        create_swap_chain{surface, device}
            .setCreateInfo(
                {.changeMinImageCount =
                     [](uint32_t minImageCount) noexcept { return minImageCount + 1; },
                 .candidateSurfaceFormats =
                     {{// diff: [test_texture2] 不需从 SRGB 改为 UNORM
                       .format = VK_FORMAT_R8G8B8A8_SRGB,
                       .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}},
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

    // ------------------------ 资产管理开始 ---------------------------------
    // diff: [Uniform] start
    // diff: [texture] start
    // diff: [test_libunibreak2] 改成+1.
    // TODO(mcs) 一个ttc 可能要生成两个纹理，因此提前约束纹理槽是有点难的
    constexpr int TEXTURE_COUNT = 4; // diff: [test_texture2] msdf：测试纹理+着色器
    constexpr int SAMPLER_COUNT = 2; // 创建2个不同的采样器
    // diff: [texture] end
    auto descriptorSetLayout =
        create_descriptor_set_layout{}
            .setCreateInfo(
                {.bindings =
                     {
                         // 普通 uniform
                         VkDescriptorSetLayoutBinding{
                             .binding = 0,
                             .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                             .descriptorCount = 1,
                             .stageFlags = VK_SHADER_STAGE_VERTEX_BIT},
                         // diff: [texture] start
                         // 纹理绑定
                         VkDescriptorSetLayoutBinding{
                             .binding = 1,
                             .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                             .descriptorCount = TEXTURE_COUNT,
                             .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                             .pImmutableSamplers = nullptr,
                         },
                         // 采样器绑定
                         VkDescriptorSetLayoutBinding{
                             .binding = 2,
                             .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                             .descriptorCount = SAMPLER_COUNT,
                             .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                             .pImmutableSamplers = nullptr,
                         }
                         // diff: [texture] end
                     }})
            .build(device);
    auto descriptorPool =
        create_descriptor_pool{}
            .setCreateInfo(
                {.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
                 .maxSets = MAX_FRAMES_IN_FLIGHT,
                 .poolSizes =
                     {VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                           .descriptorCount = MAX_FRAMES_IN_FLIGHT},
                      // diff: [texture] start
                      VkDescriptorPoolSize{
                          .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                          .descriptorCount =
                              TEXTURE_COUNT * static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT),
                      },
                      VkDescriptorPoolSize{
                          .type = VK_DESCRIPTOR_TYPE_SAMPLER,
                          // 每个描述符集需要 SAMPLER_COUNT 个采样器，
                          .descriptorCount = (SAMPLER_COUNT) *
                                             static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT),
                          // diff: [texture] end
                      }}})
            .build(device);
    auto descriptorSets = descriptorPool.allocateDescriptorSets(
        {.descriptorSets = std::vector<VkDescriptorSetLayout>{MAX_FRAMES_IN_FLIGHT,
                                                              *descriptorSetLayout}});

    constexpr VkDeviceSize BUFFER_SIZE = sizeof(UniformBufferObject);
    std::array<buffer_base, MAX_FRAMES_IN_FLIGHT> uniformBuffers;
    std::array<void *, MAX_FRAMES_IN_FLIGHT> uniformBuffersMapped{};
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        uniformBuffers[i] = create_buffer(
            allocator,
            {.sType = sType<VkBufferCreateInfo>(),
             .size = BUFFER_SIZE,
             .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
             .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
            {.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
             .usage = VMA_MEMORY_USAGE_AUTO});
        uniformBuffersMapped[i] = uniformBuffers[i].map();
    }

    // 第一个纹理：从文件加载
    // diff: [test_texture3.cpp] start
    // diff: [test_texture2.cpp] start
    auto pre = std::string{MSDF_OUTPUT_DIR};
    const std::string TEXTURE_PATH_0 = pre + "/english_atlas.png";
    const std::string JSON_PATH_0 = pre + "/english_atlas.json";
    const std::string FONT_PATH_0 = pre + "/english_atlas.ttf";

    const std::string TEXTURE_PATH_1 = pre + "/msyh_chinese.png";
    const std::string JSON_PATH_1 = pre + "/msyh_chinese.json";
    const std::string FONT_PATH_1 = pre + "/msyh_chinese.ttc";

    // diff: [test_emoji] start
    const std::string TEXTURE_PATH_2 = pre + "/emoji.png";
    const std::string JSON_PATH_2 = pre + "/emoji.json";
    const std::string FONT_PATH_2 = pre + "/emoji.ttf";
    // diff: [test_emoji] end

    // NOTE: 恶心玩意。字体太关键了。真的日了狗。一切向浏览器看清，记住咯
    // NOTE: 上面的字体是： objects.size()==text.size(): true。 太恶心了，日了狗
    //  diff: [test_harfbuzz3] start。 objects.size()==text.size(): false 妈的连字终于出现
    const std::string TEXTURE_PATH_3 = pre + "/arial_all.png";
    const std::string JSON_PATH_3 = pre + "/arial_all.json";
    const std::string FONT_PATH_3 = pre + "/arial_all.ttf";

    // diff: [test_bidi] end

    // 添加字体
    font::freetype_loader loader{};
    std::vector<font::FontInfo> registe_fonts = font::font_register::makeFontInfos(
        loader, {
                    //
                    font::font_registration{
                        .font_path = FONT_PATH_0,
                        .json_path = JSON_PATH_0,
                        .type = font::FontType::eMSDF,
                        .texture_info = {.bind = {.texture_index = 0, .sampler_index = 0},
                                         .image_variant =
                                             font::texture_info::stbi_image_type{
                                                 .image_format = STBI_rgb_alpha,
                                                 .image_path = TEXTURE_PATH_0}}},
                    font::font_registration{
                        .font_path = FONT_PATH_1,
                        .json_path = JSON_PATH_1,
                        .type = font::FontType::eMSDF,
                        .texture_info = {.bind = {.texture_index = 1, .sampler_index = 0},
                                         .image_variant =
                                             font::texture_info::stbi_image_type{
                                                 .image_format = STBI_rgb_alpha,
                                                 .image_path = TEXTURE_PATH_1}}},
                    font::font_registration{
                        .font_path = FONT_PATH_2,
                        .json_path = JSON_PATH_2,
                        .type = font::FontType::eBITMAP,
                        .texture_info = {.bind = {.texture_index = 2, .sampler_index = 0},
                                         .image_variant =
                                             font::texture_info::stbi_image_type{
                                                 .image_format = STBI_rgb_alpha,
                                                 .image_path = TEXTURE_PATH_2}}},
                    font::font_registration{
                        .font_path = FONT_PATH_3,
                        .json_path = JSON_PATH_3,
                        .type = font::FontType::eMSDF,
                        .texture_info = {.bind = {.texture_index = 3, .sampler_index = 0},
                                         .image_variant =
                                             font::texture_info::stbi_image_type{
                                                 .image_format = STBI_rgb_alpha,
                                                 .image_path = TEXTURE_PATH_3}}},
                });
    auto font_factory = font::FontFactory{{.allocator = allocator,
                                           .device = &device,
                                           .pool = &commandPool,
                                           .queue = &GRAPHICS_AND_PRESENT},
                                          *loader};
    auto font_selct =
        font::FontSelector{&font_factory, "zh-CN"}.load(std::move(registe_fonts));

    namespace font_ns = mcs::vulkan::font;

    auto textures = font_selct.getTextureImageBases();
    // Sampler
    std::vector<Sampler> samplers;
    samplers.reserve(SAMPLER_COUNT);
    {
        // 采样器类型0：线性过滤，重复寻址，各向异性
        auto build_sampler = create_sampler{}.setCreateInfo(
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
             .maxAnisotropy = physical_device.getProperties().limits.maxSamplerAnisotropy,
             .compareEnable = VK_FALSE,
             .compareOp = VK_COMPARE_OP_ALWAYS,
             .minLod = 0,
             .maxLod = VK_LOD_CLAMP_NONE, // NOTE: vulkan 内置最大值，就能适配一切mip
             .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
             .unnormalizedCoordinates = VK_FALSE});

        samplers.emplace_back(build_sampler.build(device));

        auto &samplerInfo = build_sampler.createInfo();
        // 采样器类型1：最近邻过滤，钳位到边缘
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0F;

        samplers.emplace_back(build_sampler.build(device));
    }

    uint32_t textures_count = TEXTURE_COUNT;
    uint32_t samplers_count = SAMPLER_COUNT;
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        VkDescriptorBufferInfo bufferInfo = {
            .buffer = uniformBuffers[i].buffer(), .offset = 0, .range = BUFFER_SIZE};

        // diff: [texture] start
        // 写入纹理图像
        std::vector<VkDescriptorImageInfo> textureInfos =
            textures | std::views::transform([](const auto &t) constexpr noexcept {
                return VkDescriptorImageInfo{
                    .sampler = nullptr,
                    .imageView = *t.imageView(),
                    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            }) |
            std::ranges::to<std::vector>();
        std::vector<VkDescriptorImageInfo> samplerInfos =
            samplers | std::views::transform([](const auto &s) constexpr noexcept {
                return VkDescriptorImageInfo{
                    .sampler = *s,
                    .imageView = nullptr,
                    .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                };
            }) |
            std::ranges::to<std::vector>();

        std::array<VkWriteDescriptorSet, 3> writes = {
            VkWriteDescriptorSet{.sType = sType<VkWriteDescriptorSet>(),
                                 .dstSet = descriptorSets[i],
                                 .dstBinding = 0,
                                 .dstArrayElement = 0,
                                 .descriptorCount = 1,
                                 .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                 .pBufferInfo = &bufferInfo},
            VkWriteDescriptorSet{.sType = sType<VkWriteDescriptorSet>(),
                                 .dstSet = descriptorSets[i],
                                 .dstBinding = 1,
                                 .dstArrayElement = 0,
                                 .descriptorCount = textures_count,
                                 .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                 .pImageInfo = textureInfos.data()},
            VkWriteDescriptorSet{.sType = sType<VkWriteDescriptorSet>(),
                                 .dstSet = descriptorSets[i],
                                 .dstBinding = 2,
                                 .dstArrayElement = 0,
                                 .descriptorCount = samplers_count,
                                 .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                                 .pImageInfo = samplerInfos.data()}};
        device.updateDescriptorSets(static_cast<uint32_t>(writes.size()), writes.data(),
                                    0, nullptr);
        // diff: [texture] end
    }

    // ------------------------ 资产管理结束 ---------------------------------

    // diff: [test_shape] start: 证明了，确实 harfbuzz::shape 单一责任: 确定字形+连字
    // NOTE: BIDI 以及 每段LTR 还是 RTL 等必须提取确定，和翻转，等等。 标准是vscode的画面
    constexpr auto ltr = 0; // NOLINT
    constexpr auto rtl = 1; // NOLINT

    // constexpr auto rawText = u8"是的是的ABC🤣"; // ✅

    // constexpr auto rawText = u8"السلام"; // ✅
    // constexpr auto rawText = u8"3D 世界: مرحبا بالعالم!"; // ✅

    // NOTE: 不确定是否BIDI正确
    constexpr auto rawText = u8"یہ ایک )car( ہے۔"; // ✅

    // constexpr auto rawText = u8"یہ ایک )世界car🤣( ہے۔"; // ✅
    // constexpr auto expected = u8"\u06CC\u06C1 \u0627\u06CC\u06A9 "
    //                           u8")\u4E16\u754Ccar\U0001F923( \u06C1\u06D2\u06D4";
    // static_assert(std::u8string_view(rawText) == std::u8string_view(expected));

    // constexpr auto rawText = u8"W3C (World) מעביר את שירותי- ERCIM."; // ✅
    auto norm = font_ns::utf8proc::normalize(rawText);
    const std::vector<uint32_t> &codepoints = norm.codepoints;
    font_ns::utf8proc::print_normalized(norm, rawText);

    // NOTE: 结果 [comments/test_shape2_4.png] [comments/test_shape2_5.png]
    // [comments/test_shape2_6.png]
    auto analyze_result = font_ns::bidi::analyze(codepoints, rtl);
    font_ns::bidi::print_bidi_result(codepoints, analyze_result);

    // auto bidi_result =
    //     font_ns::bidi::get_bidi_result(codepoints, std::move(analyze_result));
    // font_ns::bidi::print_bidi_result(bidi_result);

    // BUG: 请不要猜，计算机一个小数点位置不对就是天差地别:
    // [comments/test_shape2_0.png] [comments/test_shape2_1.png]
    // 输入太讲究了： 最终应该使用BIDI的结果 得到脚本
    // [comments/test_shape2_2.png]
    // BIDI 之后再 分割脚本 手写是错误的，别乱来。默认是稳定的吧，不详细追究了
    // [comments/test_shape2_3.png]

    auto test_text_runs = font_ns::assign_fonts(analyze_result, font_selct);
    font_ns::print_text_runs(test_text_runs);

    auto test_shape_result =
        font_ns::harfbuzz::shape(analyze_result.mirrored_codepoints, test_text_runs);
    font_ns::harfbuzz::print_shape_result(analyze_result.mirrored_codepoints,
                                          test_shape_result);

    using RenderMeshes = std::vector<mesh_base>;
    auto generate_line_meshes =
        [](VmaAllocator allocator, const CommandPool &pool, const Queue &queue,
           std::vector<std::vector<font_ns::harfbuzz::shape_result>> &shape_result)
        -> RenderMeshes {
        RenderMeshes objects;

        double currentY = 0.0f; // 基线 Y 坐标，向上为正
        double currentX = 0.0f;

        uint32_t baseVertex = 0;
        double fontSize = 0.2; // NOTE: 连字闪烁并没有解决。大了会很明显,开源优雅解决
        double cursorX = -0.8;

        std::vector<Vertex> lineVertices;
        std::vector<uint32_t> lineIndices;

        auto gen_mesh = [&](float originX, const font_ns::harfbuzz::shape_result &g) {
            float originY = currentY + static_cast<float>(g.offset_y * fontSize);

            float left = originX + static_cast<float>(g.plane_bounds.left * fontSize);
            float bottom = originY + static_cast<float>(g.plane_bounds.bottom * fontSize);
            float right = originX + static_cast<float>(g.plane_bounds.right * fontSize);
            float top = originY + static_cast<float>(g.plane_bounds.top * fontSize);

            glm::vec2 uv_lb(g.uv_bounds.left, g.uv_bounds.bottom);
            glm::vec2 uv_rb(g.uv_bounds.right, g.uv_bounds.bottom);
            glm::vec2 uv_rt(g.uv_bounds.right, g.uv_bounds.top);
            glm::vec2 uv_lt(g.uv_bounds.left, g.uv_bounds.top);

            uint32_t textureIdx = g.font_ctx->bind.texture_index;
            uint32_t samplerIdx = g.font_ctx->bind.sampler_index;
            uint32_t fontType = static_cast<uint32_t>(g.font_ctx->type);
            float pxRange =
                static_cast<float>(g.font_ctx->font.atlas.distanceRange.value_or(0.0));

            lineVertices.push_back({{left, bottom, 0.0f},
                                    {1.0f, 0.0f, 0.0f},
                                    uv_lb,
                                    textureIdx,
                                    samplerIdx,
                                    fontType,
                                    pxRange,
                                    0u});
            lineVertices.push_back({{right, bottom, 0.0f},
                                    {0.0f, 1.0f, 0.0f},
                                    uv_rb,
                                    textureIdx,
                                    samplerIdx,
                                    fontType,
                                    pxRange,
                                    0u});
            lineVertices.push_back({{right, top, 0.0f},
                                    {0.0f, 0.0f, 1.0f},
                                    uv_rt,
                                    textureIdx,
                                    samplerIdx,
                                    fontType,
                                    pxRange,
                                    0u});
            lineVertices.push_back({{left, top, 0.0f},
                                    {1.0f, 0.0f, 0.0f},
                                    uv_lt,
                                    textureIdx,
                                    samplerIdx,
                                    fontType,
                                    pxRange,
                                    0U});

            lineIndices.push_back(baseVertex + 0);
            lineIndices.push_back(baseVertex + 1);
            lineIndices.push_back(baseVertex + 2);
            lineIndices.push_back(baseVertex + 0);
            lineIndices.push_back(baseVertex + 2);
            lineIndices.push_back(baseVertex + 3);

            baseVertex += 4;
        };

        for (auto &run : shape_result)
        {
            for (auto &g : run)
            {
                gen_mesh(cursorX, g);
                cursorX += static_cast<float>(g.advance_x * fontSize);
            }
        }
        assert(lineIndices.size() != 0);
        objects.emplace_back(allocator, pool, queue,
                             mesh_base::mesh_data{.vertices = std::move(lineVertices),
                                                  .indices = std::move(lineIndices)});

        return objects;
    };

    // 生成网格
    RenderMeshes objects = generate_line_meshes(allocator, commandPool,
                                                GRAPHICS_AND_PRESENT, test_shape_result);
    // diff: [test_shape] end

    // 更新Uniform Buffer数据的函数
    auto updateUniformBuffer = [&](uint32_t currentFrame) {
        static auto startTime = std::chrono::high_resolution_clock::now();
        auto currentTime = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(
                         currentTime - startTime)
                         .count();

        // 旋转持续时间（秒），可改为任意需要的值
        const float rotationDuration = 10000.0F;

        // 计算受限后的旋转角度：在 duration 秒内线性增加到最大值，之后保持不变
        float angle = std::min(time, rotationDuration) * glm::radians(90.0f);

        UniformBufferObject ubo{};
        auto swapChainExtent = swapchain.imageExtent();

        // 模型矩阵：角度随时间增加，到达 rotationDuration 后固定
        ubo.model = glm::rotate(glm::mat4(1.0F), angle, glm::vec3(0.0F, 0.0F, 1.0F));

        // 视图矩阵：固定视角
        ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0F, 0.0F, 0.0F),
                               glm::vec3(0.0F, 0.0F, 1.0F));

        // 投影矩阵
        ubo.proj = glm::perspective(glm::radians(45.0f),
                                    swapChainExtent.width / (float)swapChainExtent.height,
                                    0.1f, 10.0F);
        ubo.proj[1][1] *= -1; // 翻转Y轴（Vulkan坐标系）

        // 复制到uniform buffer
        memcpy(uniformBuffersMapped[currentFrame], &ubo, sizeof(ubo));
    };

    // diff: [Uniform] end

    // diff: [depth] start
    auto depthResourcesBuild =
        create_resources{device, allocator}
            .setCreateInfo(
                {.imageType = VK_IMAGE_TYPE_2D,
                 .format = create_resources::findSupportedFormat(
                     physical_device,
                     std::array<VkFormat, 3>{VkFormat::VK_FORMAT_D32_SFLOAT,
                                             VkFormat::VK_FORMAT_D32_SFLOAT_S8_UINT,
                                             VkFormat::VK_FORMAT_D24_UNORM_S8_UINT},
                     VkImageTiling::VK_IMAGE_TILING_OPTIMAL,
                     VkFormatFeatureFlagBits::
                         VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT),
                 .extent = {.width = swapchain.refImageExtent().width,
                            .height = swapchain.refImageExtent().height,
                            .depth = 1},
                 .mipLevels = 1,
                 .arrayLayers = 1,
                 .samples =
                     physical_device.getMaxUsableSampleCount(), // diff: update by [depth]
                 .tiling = VK_IMAGE_TILING_OPTIMAL,
                 .usage =
                     VkImageUsageFlagBits::VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                 .sharingMode = VkSharingMode::VK_SHARING_MODE_EXCLUSIVE,
                 .initialLayout = VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED,
                 // VMA 配置
                 .allocationCreateInfo = {.flags =
                                              VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
                                          .usage = VMA_MEMORY_USAGE_AUTO}})
            .setViewCreateInfo(
                {.viewType = VK_IMAGE_VIEW_TYPE_2D,
                 .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                                      .baseMipLevel = 0,
                                      .levelCount = 1,
                                      .baseArrayLayer = 0,
                                      .layerCount = 1}});
    auto depthResource = depthResourcesBuild.build();
    const auto &depthFormat_ref = depthResourcesBuild.refImageFormat();
    std::cout << "depthResource hasStencilComponent: "
              << depthResourcesBuild.hasStencilComponent() << '\n';
    // diff: [depth] end

    // diff: [msaa] start
    auto msaaResourcesBuild =
        create_resources{device, allocator}
            .setCreateInfo(
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
                 .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                 // VMA 配置
                 .allocationCreateInfo = {.flags =
                                              VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
                                          .usage = VMA_MEMORY_USAGE_AUTO}})
            .setViewCreateInfo(
                {.viewType = VK_IMAGE_VIEW_TYPE_2D,
                 .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                      .baseMipLevel = 0,
                                      .levelCount = 1,
                                      .baseArrayLayer = 0,
                                      .layerCount = 1}});
    auto msaaResource = msaaResourcesBuild.build();

    // diff: [msaa] end

    auto pipelineLayout =
        create_pipeline_layout{}
            .setCreateInfo( // diff: [Uniform] 将描述符集合布局设置到管线布局
                {.setLayouts = {*descriptorSetLayout},
                 // diff: [new] 布局定义推送常量范围
                 .pushConstantRanges = {{.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                                         .offset = 0,
                                         .size = sizeof(PushConstants)}}})
            .build(device);

    using stage_info = create_graphics_pipeline::stage_info;
    auto graphicsPipeline =
        create_graphics_pipeline{}
            .setCreateInfo(
                {.pNext = make_pNext(structure_chain<VkPipelineRenderingCreateInfo>{
                     {.colorAttachmentCount = 1,
                      .pColorAttachmentFormats = &swapchainBuild.refImageFormat(),
                      .depthAttachmentFormat =
                          depthFormat_ref}}), // diff: [depth] 深度附件格式
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
                                        .cullMode =
                                            VK_CULL_MODE_NONE, // diff:[Uniform] 关键
                                        .frontFace = VK_FRONT_FACE_CLOCKWISE,
                                        // .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                                        .depthBiasEnable = VK_FALSE,
                                        .lineWidth = 1.0F},
                 .multisampleState = // diff: [msaa] start 多重采样状态使用MSAA采样计数
                 {
                     .rasterizationSamples = physical_device.getMaxUsableSampleCount(),
                     .sampleShadingEnable = VK_FALSE,
                     .minSampleShading = 1.0F,
                     .sampleMask = {},
                     .alphaToCoverageEnable = VK_FALSE,
                     .alphaToOneEnable = VK_FALSE,
                 }, // diff: [msaa] end
                    // diff: [depth] start 添加深度测试和模板测试状态
                 .depthStencilState =
                     {// diff: [test_harfbuzz] start 连字引入了 BUG. 连字有闪烁
                      .depthTestEnable = VK_TRUE,
                      .depthWriteEnable = VK_TRUE,
                      //   .depthTestEnable = VK_FALSE, // NOTE: 临时关闭 解决
                      //   .depthWriteEnable = VK_FALSE,
                      // diff: [test_harfbuzz] end 连字引入了 BUG. 连字有闪烁
                      .depthCompareOp = VK_COMPARE_OP_LESS,
                      .depthBoundsTestEnable = VK_FALSE,
                      .stencilTestEnable = VK_FALSE,
                      .front = {},
                      .back = {},
                      .minDepthBounds = 0.0F,
                      .maxDepthBounds = 1.0F}, // diff: [depth] end
                 .colorBlendState =
                     {.logicOpEnable = VK_FALSE,
                      .logicOp = VkLogicOp::VK_LOGIC_OP_COPY,
                      .attachments =
                          /*
                           //NOTE: [comments/test_texture2_0.png]
                           MSDF 的抗锯齿依赖于透明度混合，原因如下：
                           MSDF 的工作原理：
                           距离场纹理存储的是有符号距离值，在着色器中转换为透明度opacity(取值范围
                           0~1) 最终颜色是前景色和背景色的混合：color =
                           mix(bgColor,fgColor, opacity)。


                           透明度混合的作用：混合阶段将着色器输出的颜色与帧缓冲区中已有的背景颜色进行合成，形成平滑的边缘。如果不启用混合，着色器输出的颜色会直接覆盖背景，透明度信息被丢弃，边缘就会呈现锯齿（要么完全不透明，要么完全透明）。

                           文档示例的隐含条件：示例着色器中的 mix
                           函数只是计算了混合后的颜色，但实际写入帧缓冲区时，仍需依赖图形管线的混合功能来与背景合成。文档省略了管线设置，是因为它假定开发者已具备基础渲染知识。
                           */
                      {{                        // diff: [test_texture2.cpp] start
                        .blendEnable = VK_TRUE, // 启用混合
                        .srcColorBlendFactor =
                            VK_BLEND_FACTOR_ONE, // 预乘 Alpha
                                                 // diff: [test_texture2.cpp] end
                        .colorWriteMask =
                            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT}}},
                 .dynamicState = {.dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                                                    VK_DYNAMIC_STATE_SCISSOR,
                                                    // NOTE: 解决闪烁，解决Z-fighting
                                                    VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE}},
                 .layout = *pipelineLayout})
            .build(device);

    constexpr auto MAX_FRAMES_IN_FLIGHT = 2;
    CommandBuffers commandBuffers =
        commandPool.allocateCommandBuffers({.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                            .commandBufferCount = MAX_FRAMES_IN_FLIGHT});
    frame_context<MAX_FRAMES_IN_FLIGHT> frameContext{device, swapchain.imagesSize()};

    // diff: [test_msdf_atlas_gen1] 删掉 硬编码顶点

    // diff: end // NOLINTEND

    struct record_info
    {
        uint32_t current_frame;
        uint32_t image_index;
        VkDescriptorSet descriptor_set;
    };
    // NOLINTNEXTLINE
    const auto recordCommandBuffer = [&](const CommandBufferView &commandBuffer,
                                         record_info info) {
        auto [currentFrame, imageIndex, descriptorSet] = info;
        VkImage image = swapchain.image(imageIndex);
        VkImageView imageView = swapchain.imageView(imageIndex);
        auto imageExtent = swapchain.imageExtent();
        VkImage depthImage = depthResource.image();
        VkImageView depthImageView = depthResource.imageView();

        VkImage msaaImage = msaaResource.image();
        VkImageView msaaImageView = msaaResource.imageView();

        commandBuffer.begin({.sType = sType<VkCommandBufferBeginInfo>()});

        // Before starting rendering,
        // transition the swapchain image to COLOR_ATTACHMENT_OPTIMAL
        my_render::transition_image_layout(
            commandBuffer, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            {}, // srcAccessMask (no need to wait for previous operations)
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, // diff: update by  [msaa]
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

        // Transition the multisampled color image to COLOR_ATTACHMENT_OPTIMAL
        // diff: [msaa] start
        my_render::transition_image_layout(
            commandBuffer, msaaImage, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
        // diff: [msaa] end

        // Transition depth image to depth attachment optimal layout
        // diff: [depth] start
        my_render::transition_image_layout(
            commandBuffer, depthImage, VK_IMAGE_ASPECT_DEPTH_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT);
        // diff: [depth] end

        // diff: update by [mass] start 修改颜色附件为MSAA颜色图像，添加解析附件
        VkRenderingAttachmentInfo colorAttachment = {
            .sType = sType<VkRenderingAttachmentInfo>(),
            .imageView = msaaImageView,
            .imageLayout = VkImageLayout::VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT, // diff: 添加解析模式
            .resolveImageView = imageView,              // diff: 解析到交换链图像
            .resolveImageLayout =
                VkImageLayout::VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, // diff:
                                                                         // 解析图像布局
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.color = {.float32 = {0.0F, 0.0F, 0.0F, 1.0F}}}};
        // diff: update by [mass] end

        // diff: [depth] start: 添加深度附件
        VkRenderingAttachmentInfo depthAttachment = {
            .sType = sType<VkRenderingAttachmentInfo>(),
            .imageView = depthImageView,
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.depthStencil = {.depth = 1.0F}}};
        // diff: [depth] end

        commandBuffer.beginRendering({
            .sType = sType<VkRenderingInfo>(),
            .renderArea = {.offset = {.x = 0, .y = 0}, .extent = imageExtent},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachment,
            .pDepthAttachment = &depthAttachment // diff: [depth] 附件绑定到渲染中
        });
        commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, *graphicsPipeline);

        commandBuffer.setViewport(0, std::array<VkViewport, 1>{VkViewport{
                                         .x = 0.0F,
                                         .y = 0.0F,
                                         .width = static_cast<float>(imageExtent.width),
                                         .height = static_cast<float>(imageExtent.height),
                                         .minDepth = 0.0F,
                                         .maxDepth = 1.0F}});

        commandBuffer.setScissor(
            0, std::array<VkRect2D, 1>{
                   VkRect2D{.offset = {.x = 0, .y = 0}, .extent = imageExtent}});

        {
            // NOTE: 只需要一次
            //  绑定描述符集（保持不变）
            commandBuffer.bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                             *pipelineLayout, 0, 1, &descriptorSet, 0,
                                             nullptr);
            // diff:[test_libunibreak4] 解决闪烁问题,不会影响到透明。
            // NOTE: 笔画没有丢失，因为关闭深度测试也是一样的效果。
            // 半透明和透明渲染时机不一样
            commandBuffer.setDepthWriteEnable(false);
            // 绘制单个 字符
            for (auto &input_mesh : objects)
            {

                // 推送常量
                uint64_t vertexBufferAddress =
                    input_mesh.getVertexBufferAddress(currentFrame);
                commandBuffer.pushConstants(*pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                                            0, sizeof(PushConstants),
                                            &vertexBufferAddress);

                // diff: [test_libunibreak3] start
                //  绑定索引缓冲区（全局）
                auto &fb = input_mesh.frameBuffers[currentFrame];
                commandBuffer.bindIndexBuffer(fb.indexBuffer.buffer(), 0,
                                              mesh_base::indexType());
                commandBuffer.drawIndexed(fb.indexCount, 1, 0, 0,
                                          0); // 使用帧特定的索引数
                // diff: [test_libunibreak3] end
            }
        }

        commandBuffer.endRendering();

        // After rendering, transition the swapchain image to PRESENT_SRC
        my_render::transition_image_layout(
            commandBuffer, image, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, 0,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
        commandBuffer.end();
    };
    // NOLINTNEXTLINE
    const auto recreateSwapChain = [&]() constexpr {
        surface.waitGoodFramebufferSize();
        device.waitIdle();

        swapchain.destroy();
        swapchain = swapchainBuild.rebuild();

        // diff: [msaa] start
        msaaResourcesBuild.updateImageExtent({.width = swapchain.refImageExtent().width,
                                              .height = swapchain.refImageExtent().height,
                                              .depth = 1});
        msaaResource = msaaResourcesBuild.rebuild();
        // diff: [msaa] end

        // diff: [depth] start
        depthResourcesBuild.updateImageExtent(
            {.width = swapchain.refImageExtent().width,
             .height = swapchain.refImageExtent().height,
             .depth = 1});
        depthResource = depthResourcesBuild.rebuild();
        // diff: [depth] end
    };
    // NOLINTNEXTLINE
    const auto drawFrame = [&]() constexpr {
        auto &inFlightFences = frameContext.inFlightFences;
        auto &currentFrame = frameContext.currentFrame;
        auto &presentCompleteSemaphore = frameContext.presentCompleteSemaphore;
        auto &semaphoreIndex = frameContext.semaphoreIndex;
        auto &renderFinishedSemaphore = frameContext.renderFinishedSemaphore;

        const LogicalDevice *logicalDevice = frameContext.device_;

        while (logicalDevice->waitForFences(1, inFlightFences[currentFrame], VK_TRUE,
                                            UINT64_MAX) == VK_TIMEOUT)
            ;

        auto [result, imageIndex] = swapchain.acquireNextImage(
            UINT64_MAX, presentCompleteSemaphore[semaphoreIndex], nullptr);
        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            recreateSwapChain();
            return;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
            throw std::runtime_error("failed to acquire swap chain image!");

        logicalDevice->resetFences(1, inFlightFences[currentFrame]);

        const auto &commandBuffer = commandBuffers[currentFrame];
        commandBuffer.reset({});

        // diff: [Uniform] start
        updateUniformBuffer(currentFrame);
        // diff: [Uniform] end

        // diff: vkResetFences 之后更新顶点. 仅仅更新飞行的帧
        // input_mesh.requestUpdate(allocator, currentFrame);
        // diff: end
        recordCommandBuffer(commandBuffer,
                            {.current_frame = currentFrame,
                             .image_index = imageIndex,
                             .descriptor_set = descriptorSets[currentFrame]});

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
            recreateSwapChain();
        }
        semaphoreIndex = (semaphoreIndex + 1) % presentCompleteSemaphore.size();
        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    };
    while (window.shouldClose() == 0)
    {
        surface::pollEvents();

        drawFrame();
    }
    device.waitIdle();
    // diff: [Uniform] start
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (uniformBuffersMapped[i] != nullptr)
        {
            uniformBuffers[i].unmap();
            uniformBuffersMapped[i] = nullptr;
        }
    } // diff: [Uniform] end

    std::cout << "main done\n";
    return 0;
}
catch (std::exception &e)
{
    std::println("main catch exception: {}", e.what());
}