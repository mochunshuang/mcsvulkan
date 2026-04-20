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

                       query_vulkan12_features.bufferDeviceAddress && // diff: [new]
                       // 检查Vulkan 1.2中的bufferDeviceAddress
                       query_vulkan12_features.scalarBlockLayout && // diff: [new]
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
    constexpr int TEXTURE_COUNT = 5; // diff: [test_texture2] msdf：测试纹理+着色器
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

    const std::string TEXTURE_PATH_4 = pre + "/missing_char.png";
    const std::string JSON_PATH_4 = pre + "/missing_char.json";
    const std::string FONT_PATH_4 = pre + "/missing_char.ttf";
    // const std::string FONT_PATH_4 = pre + "/missing_char.ttc";

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
                    font::font_registration{
                        .font_path = FONT_PATH_4,
                        .json_path = JSON_PATH_4,
                        .type = font::FontType::eMSDF,
                        .texture_info = {.bind = {.texture_index = 4, .sampler_index = 0},
                                         .image_variant =
                                             font::texture_info::stbi_image_type{
                                                 .image_format = STBI_rgb_alpha,
                                                 .image_path = TEXTURE_PATH_4}}},
                });
    auto font_factory = font::FontFactory{{.allocator = allocator,
                                           .device = &device,
                                           .pool = &commandPool,
                                           .queue = &GRAPHICS_AND_PRESENT},
                                          *loader};
    auto font_selct =
        font::FontSelector{&font_factory, "zh-CN"}.load(std::move(registe_fonts));
    font_selct.initNotdefFont();
    assert(font_selct.notdefFont() != nullptr);

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

    constexpr auto rawText =
        u8"我你好世界你好世界你好世界🤣\nW3C (World)👪 ﷲ é \nמעביר את "
        u8"שירותי- ERCIM."; // NOTE: BUG
    auto norm = font_ns::utf8proc::normalize(rawText);
    const std::vector<uint32_t> &codepoints = norm.codepoints;
    font_ns::utf8proc::print_normalized(norm, rawText);

    // NOTE: 结果 [comments/test_shape2_4.png] [comments/test_shape2_5.png]
    // [comments/test_shape2_6.png]
    auto analyze_result = font_ns::bidi::analyze(codepoints, ltr);
    font_ns::bidi::print_bidi_result(codepoints, analyze_result);

    auto test_text_runs = font_ns::assign_fonts(analyze_result, font_selct);
    font_ns::print_text_runs(test_text_runs);

    auto test_shape_result = font_ns::harfbuzz::shape(
        analyze_result.mirrored_codepoints, test_text_runs, font_selct.notdefFont());
    font_ns::harfbuzz::print_shape_result(analyze_result.mirrored_codepoints,
                                          test_shape_result);

    // diff: [test_line_breaks] start
    auto break_result = font_ns::libunibreak::analyze_line_breaks(
        analyze_result.mirrored_codepoints, "zh-CN");
    font_ns::libunibreak::print_break_result(break_result,
                                             analyze_result.mirrored_codepoints);

    using RenderMeshes = std::vector<mesh_base>;

    constexpr float BASE_WIDTH = 800.0f;
    constexpr float BASE_HEIGHT = 600.0f;
    auto generate_line_meshes =
        [](VmaAllocator allocator, const CommandPool &pool, const Queue &queue,
           const std::vector<std::vector<font_ns::harfbuzz::shape_result>> &shape_results,
           const std::vector<char> &break_types,
           double maxLineWidth, // 基准世界行宽（已是最终 NDC 单位）
           double fontSize,     // 基准世界字体大小（已是最终 NDC 单位）
           double originX,      // 基准世界起始 X（已是最终 NDC 单位）
           double originY,      // 基准世界起始 Y（已是最终 NDC 单位）
           double originZ,      // Z 深度
           double lineHeight)   // 行高（负值向下，已是最终 NDC 单位）
        -> RenderMeshes {
        RenderMeshes objects;

        // 字形原始最大高度（用于基线对齐）
        double maxGlyphRawHeight = 0.0;
        for (const auto &run : shape_results)
            for (const auto &glyph : run)
            {
                double h = glyph.plane_bounds.top - glyph.plane_bounds.bottom;
                if (h > maxGlyphRawHeight)
                    maxGlyphRawHeight = h;
            }

        double currentY = originY - (maxGlyphRawHeight * fontSize) * 0.5;

        // 建立逻辑索引到字形的映射
        std::vector<const font_ns::harfbuzz::shape_result *> visual_glyphs;
        std::unordered_map<size_t, const font_ns::harfbuzz::shape_result *>
            logical_to_glyph;
        for (const auto &run : shape_results)
            for (const auto &glyph : run)
            {
                visual_glyphs.push_back(&glyph);
                logical_to_glyph[glyph.logical_idx] = &glyph;
            }

        // 按逻辑顺序分行
        struct LineInfo
        {
            std::vector<size_t> logical_indices;
        };
        std::vector<LineInfo> lines;
        LineInfo current_line;
        double cursorX = originX;

        for (size_t log_idx = 0; log_idx < break_types.size(); ++log_idx)
        {
            char break_type = break_types[log_idx];
            if (break_type == LINEBREAK_MUSTBREAK)
            {
                if (!current_line.logical_indices.empty())
                {
                    lines.push_back(std::move(current_line));
                    current_line = LineInfo{};
                    cursorX = originX;
                }
                continue;
            }

            auto it = logical_to_glyph.find(log_idx);
            if (it == logical_to_glyph.end())
                continue;

            const auto *glyph = it->second;
            double advance = glyph->advance_x * fontSize;
            double new_width = cursorX + advance - originX;

            if (!current_line.logical_indices.empty() && new_width > maxLineWidth)
            {
                lines.push_back(std::move(current_line));
                current_line = LineInfo{};
                cursorX = originX;
            }

            current_line.logical_indices.push_back(log_idx);
            cursorX += advance;
        }
        if (!current_line.logical_indices.empty())
            lines.push_back(std::move(current_line));

        // 生成网格顶点（坐标已是基准世界单位，无需补偿）
        for (const auto &line : lines)
        {
            std::unordered_set<size_t> log_set(line.logical_indices.begin(),
                                               line.logical_indices.end());
            std::vector<const font_ns::harfbuzz::shape_result *> line_glyphs;
            for (const auto *glyph : visual_glyphs)
                if (log_set.count(glyph->logical_idx))
                    line_glyphs.push_back(glyph);

            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;
            uint32_t baseVertex = 0;
            double cursorX = originX;

            for (const auto *g : line_glyphs)
            {
                double baselineY = currentY + g->offset_y * fontSize;
                float left =
                    static_cast<float>(cursorX + g->plane_bounds.left * fontSize);
                float bottom =
                    static_cast<float>(baselineY + g->plane_bounds.bottom * fontSize);
                float right =
                    static_cast<float>(cursorX + g->plane_bounds.right * fontSize);
                float top =
                    static_cast<float>(baselineY + g->plane_bounds.top * fontSize);
                float z = static_cast<float>(originZ);

                glm::vec2 uv_lb(g->uv_bounds.left, g->uv_bounds.bottom);
                glm::vec2 uv_rb(g->uv_bounds.right, g->uv_bounds.bottom);
                glm::vec2 uv_rt(g->uv_bounds.right, g->uv_bounds.top);
                glm::vec2 uv_lt(g->uv_bounds.left, g->uv_bounds.top);

                uint32_t textureIdx = g->font_ctx->bind.texture_index;
                uint32_t samplerIdx = g->font_ctx->bind.sampler_index;
                uint32_t fontType = static_cast<uint32_t>(g->font_ctx->type);
                float pxRange = static_cast<float>(
                    g->font_ctx->font.atlas.distanceRange.value_or(0.0));

                vertices.push_back({{left, bottom, z},
                                    {1, 0, 0},
                                    uv_lb,
                                    textureIdx,
                                    samplerIdx,
                                    fontType,
                                    pxRange,
                                    0});
                vertices.push_back({{right, bottom, z},
                                    {0, 1, 0},
                                    uv_rb,
                                    textureIdx,
                                    samplerIdx,
                                    fontType,
                                    pxRange,
                                    0});
                vertices.push_back({{right, top, z},
                                    {0, 0, 1},
                                    uv_rt,
                                    textureIdx,
                                    samplerIdx,
                                    fontType,
                                    pxRange,
                                    0});
                vertices.push_back({{left, top, z},
                                    {1, 0, 0},
                                    uv_lt,
                                    textureIdx,
                                    samplerIdx,
                                    fontType,
                                    pxRange,
                                    0});

                indices.push_back(baseVertex + 0);
                indices.push_back(baseVertex + 1);
                indices.push_back(baseVertex + 2);
                indices.push_back(baseVertex + 0);
                indices.push_back(baseVertex + 2);
                indices.push_back(baseVertex + 3);

                baseVertex += 4;
                cursorX += g->advance_x * fontSize;
            }

            if (!vertices.empty())
            {
                objects.emplace_back(
                    allocator, pool, queue,
                    mesh_base::mesh_data{std::move(vertices), std::move(indices)});
            }
            currentY += lineHeight;
        }

        return objects;
    };
    // diff: [test_yoga] start
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

    auto generateVerticesFromLayout = [](Layout &layout, layout_size size,
                                         std::vector<Vertex> &outVerts,
                                         std::vector<uint32_t> &outIndices) {
        // 先让 Yoga 计算布局（传入当前屏幕宽高）
        layout.calculate(size);
        auto [screenWidth, screenHeight] = size;
        //NOTE: 字符串顶点目前没有翻转，我们也要适应
        //NOTE:当前做法（统一使用“世界 Y 向上”生成顶点，再通过投影矩阵统一翻转）更符合图形学惯例.
        // NDC 的 Y 向下是 Vulkan 规范的根本属性，不是代码主动“翻转”的结果。代码中的翻转操作，是为了将世界坐标系的 Y 向上映射到 NDC 的 Y 向下，以满足规范要求。
        // NOTE: 已经统一在 MVP 做-Y翻转了；
        // 返回 glm::vec3，z 分量设为 0.0f（或根据深度需求调整）
        // auto screenToNDC = [=](float x, float y) -> glm::vec3 {
        //     return {(x / screenWidth) * 2.0f - 1.0f, (y / screenHeight) * 2.0f - 1.0f,
        //             0.0f};
        // };
        // auto screenToNDC = [=](float x, float y) -> glm::vec3 {
        //     // 从“世界坐标系（Y 向上）”到“Vulkan NDC（Y 向下）”的转换。
        //     float worldY = screenHeight - y; // 翻转 Y 轴
        //     return {(x / screenWidth) * 2.0f - 1.0f,
        //             (worldY / screenHeight) * 2.0f - 1.0f, 0.0f};
        // };
        auto screenToNDC = [=](float x, float y) -> glm::vec3 {
            // 锚定左上角：屏幕像素(0,0)对应NDC(-1,1)
            // 基于基准尺寸映射，内容大小固定
            float ndcX = -1.0f + (x / BASE_WIDTH) * 2.0f;
            float ndcY = 1.0f - (y / BASE_HEIGHT) * 2.0f;
            return {ndcX, ndcY, 0.0f};
        };

        size_t index{};
        auto getColor = [&]() -> glm::vec3 {
            static const std::vector<glm::vec3> colors = {
                {1.0f, 0.0f, 0.0f}, // 红
                {0.0f, 1.0f, 0.0f}, // 绿
                {0.0f, 0.0f, 1.0f}, // 蓝
                {1.0f, 1.0f, 0.0f}, // 黄
                {0.5f, 0.5f, 0.5f}, // 灰
                {0.0f, 1.0f, 1.0f}, // 青
                {1.0f, 0.0f, 1.0f}, // 品红
                {1.0f, 0.5f, 0.0f}, // 橙
                {0.5f, 0.0f, 1.0f}  // 紫
            };
            if (index > colors.size())
                index = 0;
            return colors[index];
        };

        auto traverse = [&](this auto &self, const Node *cur, float parentLeft,
                            float parentTop) {
            if (cur == nullptr)
                return;
            auto node = **cur;
            float left = YGNodeLayoutGetLeft(node) + parentLeft;
            float top = YGNodeLayoutGetTop(node) + parentTop;
            float width = YGNodeLayoutGetWidth(node);
            float height = YGNodeLayoutGetHeight(node);

            if (width > 0.0f && height > 0.0f)
            {
                float right = left + width;
                float bottom = top + height;
                glm::vec3 p0 = screenToNDC(left, bottom);
                glm::vec3 p1 = screenToNDC(right, bottom);
                glm::vec3 p2 = screenToNDC(left, top);
                glm::vec3 p3 = screenToNDC(right, top);
                glm::vec3 color = getColor();

                uint32_t base = static_cast<uint32_t>(outVerts.size());
                outVerts.push_back(
                    Vertex{.pos = p0,
                           .color = color,
                           .texCoord = {},
                           .textureIndex = {},
                           .samplerIndex = {},
                           .fontType = static_cast<uint32_t>(font::FontType::eNONE),
                           .pxRange = {}});
                outVerts.push_back(
                    Vertex{.pos = p1,
                           .color = color,
                           .texCoord = {},
                           .textureIndex = {},
                           .samplerIndex = {},
                           .fontType = static_cast<uint32_t>(font::FontType::eNONE),
                           .pxRange = {}});
                outVerts.push_back(
                    Vertex{.pos = p2,
                           .color = color,
                           .texCoord = {},
                           .textureIndex = {},
                           .samplerIndex = {},
                           .fontType = static_cast<uint32_t>(font::FontType::eNONE),
                           .pxRange = {}});
                outVerts.push_back(
                    Vertex{.pos = p3,
                           .color = color,
                           .texCoord = {},
                           .textureIndex = {},
                           .samplerIndex = {},
                           .fontType = static_cast<uint32_t>(font::FontType::eNONE),
                           .pxRange = {}});

                outIndices.push_back(base + 0);
                outIndices.push_back(base + 1);
                outIndices.push_back(base + 2);
                outIndices.push_back(base + 1);
                outIndices.push_back(base + 3);
                outIndices.push_back(base + 2);

                // // 三角形1: p0 → p2 → p1
                // outIndices.push_back(base + 0);
                // outIndices.push_back(base + 2);
                // outIndices.push_back(base + 1);

                // // 三角形2: p1 → p2 → p3
                // outIndices.push_back(base + 1);
                // outIndices.push_back(base + 2);
                // outIndices.push_back(base + 3);
            }
            index++;

            // NOTE: 是否必须迁移到上面的 if条件内部。
            for (const auto &child : cur->childrens())
            {
                self(child.get(), left, top);
            }
        };
        traverse(layout.root(), 0.0f, 0.0f);
    };
    std::vector<Vertex> yogaVertices;
    std::vector<uint32_t> yogaIndices;
    generateVerticesFromLayout(layout, {WIDTH, HEIGHT}, yogaVertices, yogaIndices);
    mesh_base layout_mesh{
        allocator, commandPool, GRAPHICS_AND_PRESENT, {yogaVertices, yogaIndices}};

    // diff: [test_yoga2] start 可重用文本网格更新逻辑

    auto updateTextMeshes = [&](float screenWidth, float screenHeight) {
        constexpr float BASE_FONT_SIZE_PX = 16.0f;

        // 获取文本显示节点的布局信息（Yoga 返回的像素坐标）
        auto &text_display = screen.childrens()[2];
        YGNodeRef textNode = **text_display;
        float left = YGNodeLayoutGetLeft(textNode);
        float top = YGNodeLayoutGetTop(textNode);
        float width = YGNodeLayoutGetWidth(textNode);
        float height = YGNodeLayoutGetHeight(textNode);

        float padLeft = YGNodeStyleGetPadding(textNode, YGEdgeLeft).value;
        float padRight = YGNodeStyleGetPadding(textNode, YGEdgeRight).value;
        float padTop = YGNodeStyleGetPadding(textNode, YGEdgeTop).value;
        float padBottom = YGNodeStyleGetPadding(textNode, YGEdgeBottom).value;

        float contentLeft = left + padLeft;
        float contentTop = top + padTop;
        float contentWidth = width - padLeft - padRight;

        // 基于基准尺寸直接计算 NDC 坐标（锚定左上角）
        double originX = -1.0 + (contentLeft / BASE_WIDTH) * 2.0;
        double originY = 1.0 - (contentTop / BASE_HEIGHT) * 2.0;
        double maxLineWidth = (contentWidth / BASE_WIDTH) * 2.0; // 基准世界单位

        // 字形原始最大高度
        double maxGlyphRawHeight = 0.0;
        for (const auto &run : test_shape_result)
            for (const auto &glyph : run)
            {
                double h = glyph.plane_bounds.top - glyph.plane_bounds.bottom;
                if (h > maxGlyphRawHeight)
                    maxGlyphRawHeight = h;
            }

        // 基准世界字体大小（基于 BASE_HEIGHT 转换）
        double baseFontSizeWorld = BASE_FONT_SIZE_PX * (2.0 / BASE_HEIGHT);
        const double lineHeightFactor = 1.2;
        double lineHeightWorld =
            -baseFontSizeWorld * maxGlyphRawHeight * lineHeightFactor;

        // 调用网格生成函数（已去除 screenWidth/screenHeight 参数）
        return generate_line_meshes(allocator, commandPool, GRAPHICS_AND_PRESENT,
                                    test_shape_result, break_result.types, maxLineWidth,
                                    baseFontSizeWorld, originX, originY, 0.0,
                                    lineHeightWorld);
    };
    VkExtent2D fbSize = window.getFramebufferSize();
    float screenW = static_cast<float>(fbSize.width);
    float screenH = static_cast<float>(fbSize.height);

    RenderMeshes objects = updateTextMeshes(screenW, screenH);
    // diff: [test_yoga2] end
    objects.insert(objects.begin(), std::move(layout_mesh));
    // diff: [test_yoga] end

    // diff: [test_line_breaks] end
    // diff: [test_shape] end

    // 更新Uniform Buffer数据的函数
    auto updateUniformBuffer = [&](uint32_t currentFrame) {
#if 0
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
#else
        // NOTE: 无副作用操作
        UniformBufferObject ubo{};
        ubo.model = glm::mat4(1.0f); // 单位矩阵
        ubo.view = glm::mat4(1.0f);
        ubo.proj = glm::mat4(1.0f);
        // 若仍需保持 Vulkan 的 Y 轴翻转，可取消下面注释：
        ubo.proj[1][1] *= -1;

        memcpy(uniformBuffersMapped[currentFrame], &ubo, sizeof(ubo));
#endif
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
                 .rasterizationState =
                     {.depthClampEnable = VK_FALSE,
                      .rasterizerDiscardEnable = VK_FALSE,
                      .polygonMode = VK_POLYGON_MODE_FILL,
                      //   .cullMode =
                      //       VK_CULL_MODE_BACK_BIT, // TODO(mcs): [test_yoga] 文本和布局顶点不兼容
                      //   .frontFace = VK_FRONT_FACE_CLOCKWISE,
                      .cullMode = VK_CULL_MODE_BACK_BIT,            // 恢复背面剔除
                      .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, // 逆时针为正面
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

    //diff: [test_yoga] start
#if 0
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
                        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE, // 预乘 Alpha
                        // diff: [test_texture2.cpp] end
                        .colorWriteMask =
                            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT}}},
#else
                 // NOTE: finalColor = srcColor * 1 + dstColor * (1 - srcAlpha) 。 这是混合背景颜色的关键
                 .colorBlendState =
                     {.logicOpEnable = VK_FALSE,
                      .logicOp = VkLogicOp::VK_LOGIC_OP_COPY,
                      .attachments =
                          {{.blendEnable = VK_TRUE,
                            .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
                            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                            .colorBlendOp = VK_BLEND_OP_ADD,
                            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                            .alphaBlendOp = VK_BLEND_OP_ADD,
                            .colorWriteMask =
                                VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT}}},
#endif
                 //diff: [test_yoga] end
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
        //diff: [test_yoga] start. 还要手动更新
        {
            VkExtent2D newSize = window.getFramebufferSize();
            uint32_t newWidth = newSize.width;
            uint32_t newHeight = newSize.height;

            on_resize(layout, {.available_width = static_cast<float>(newWidth),
                               .available_height = static_cast<float>(newHeight)});
            std::vector<Vertex> newVerts;
            std::vector<uint32_t> newIndices;
            generateVerticesFromLayout(
                layout,
                {.available_width = static_cast<float>(newWidth),
                 .available_height = static_cast<float>(newHeight)},
                newVerts, newIndices);
            objects[0].queueUpdate(std::move(newVerts), std::move(newIndices));

            //diff: [test_yoga2] start
            // 更新文本网格（替换索引1及之后的所有元素）
            RenderMeshes newTextMeshes = updateTextMeshes(static_cast<float>(newWidth),
                                                          static_cast<float>(newHeight));
            // 保留 objects[0]，删除其余，再插入新文本网格
            if (objects.size() > 1)
                objects.erase(objects.begin() + 1, objects.end());
            objects.insert(objects.end(), std::make_move_iterator(newTextMeshes.begin()),
                           std::make_move_iterator(newTextMeshes.end()));
            //diff: [test_yoga2] end
        }
        //diff: [test_yoga] end

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
        objects[0].requestUpdate(allocator, currentFrame); //diff: [test_yoga]
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