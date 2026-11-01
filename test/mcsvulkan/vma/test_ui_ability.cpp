#include <any>
#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <exception>
#include <flat_map>
#include <iostream>
#include <memory>
#include <optional>
#include <print>
#include <chrono>
#include <random>
#include <span>
#include <stdexcept>
#include <string_view>
#include <stdint.h>
#include <thread>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include <memory_resource> // pmr 内存资源

#include "../head.hpp"

using mcs::vulkan::Instance;
using mcs::vulkan::tool::create_instance;
using mcs::vulkan::tool::create_debugger;
using mcs::vulkan::tool::create_physical_device_selector;
using mcs::vulkan::vkMakeVersion;
using mcs::vulkan::vkApiVersion;

using mcs::vulkan::tool::enable_intance_build;
using mcs::vulkan::tool::structure_chain;
using mcs::vulkan::tool::create_queue_family_index_selector;
using mcs::vulkan::tool::create_logical_device;
using mcs::vulkan::tool::create_swapchain;
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
using mcs::vulkan::Debugger;
using mcs::vulkan::surface_impl;
using mcs::vulkan::wsi::glfw::Window;
using mcs::vulkan::Queue;

using mcs::vulkan::LogicalDevice;

using mcs::vulkan::tool::make_pNext;

using mcs::vulkan::CommandPool;
using mcs::vulkan::CommandBuffers;
using mcs::vulkan::CommandBuffer;
using mcs::vulkan::CommandBufferView;
using mcs::vulkan::Fence;

using mcs::vulkan::DescriptorSetLayout;
using mcs::vulkan::DescriptorPool;
using mcs::vulkan::DescriptorSets;
using mcs::vulkan::DescriptorSetLayout;

using raii_vma = mcs::vulkan::raii_vma;

using mcs::vulkan::tool::simple_copy_buffer;

using mcs::vulkan::meta::make_aggregate_ref;
using mcs::vulkan::meta::make_aggregate;
using mcs::vulkan::meta::field;
using mcs::vulkan::meta::method;

using mcs::vulkan::ecs::gen_soa_aggregate;
using mcs::vulkan::ecs::gen_soa_struct;
using mcs::vulkan::ecs::soa_vector;
using mcs::vulkan::ecs::proxy_value;

using mcs::vulkan::task::make_task;
using mcs::vulkan::task::init_task;
using mcs::vulkan::task::schedulable_task;

using mcs::vulkan::match;

using mcs::vulkan::fatal_error;

constexpr uint32_t WIDTH = 1280;
constexpr uint32_t HEIGHT = 800;
constexpr auto TITLE = "test_my_triangle";

static constexpr auto MAX_FRAMES_IN_FLIGHT = 2;

namespace camera
{
    using mcs::vulkan::camera::composeTRS;
    using mcs::vulkan::camera::extractTranslationScale;
    using mcs::vulkan::camera::VulkanNDCConfig;
    using mcs::vulkan::camera::transform;
    using mcs::vulkan::camera::computeAnchorOffset;
    using mcs::vulkan::camera::transformPointToWorld;
    using mcs::vulkan::camera::RightHandedView;
    using mcs::vulkan::camera::LeftHandedView;
    using mcs::vulkan::camera::VulkanPerspectiveProjection;
    using mcs::vulkan::camera::VulkanOrthographicProjection;
    using mcs::vulkan::camera::VulkanUIOrthographicProjection;
    using mcs::vulkan::camera::GenCamera;
}; // namespace camera

namespace mesh
{
    using mcs::vulkan::memory::buffer_base;
    using mcs::vulkan::memory::auto_map_buffer;
    using mcs::vulkan::memory::create_simple_buffer;
    using mcs::vulkan::memory::create_staging_buffer;

    using index_type = uint32_t;

    using position_3d = glm::vec3;

}; // namespace mesh

namespace font
{
    // 2. Library Initialization
    using freetype_loader = mcs::vulkan::font::freetype::loader;

    using mcs::vulkan::font::FontType;

    using mcs::vulkan::font::texture_info;
    using mcs::vulkan::font::FontInfo;
    using mcs::vulkan::font::font_register;

    using mcs::vulkan::font::font_registration;

    using mcs::vulkan::font::GenFontContext;
    using mcs::vulkan::font::GenFontFactory;
    using mcs::vulkan::font::GenFontSelector;
    using mcs::vulkan::font::make_font_factory;

}; // namespace font

struct FrameClock
{
    using Clock = std::chrono::steady_clock;    // 单调时钟，适合测时间间隔
    Clock::time_point startTime = Clock::now(); // 程序启动时自动记录
    Clock::time_point lastTime = startTime;
    float deltaTime = 0.016f;

    // 返回从 startTime 到现在的秒数（float）
    float getElapsed() const noexcept
    {
        return std::chrono::duration<float>(Clock::now() - startTime).count();
    }
};

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

struct Vertex // NOLINT
{
    glm::vec3 pos;
    glm::vec2 texCoord; // diff: [texture] 添加纹理坐标
};
struct VertexAttribute
{
    glm::vec3 color; // 仅颜色
};

// diff: [test_dod14] start: 让3D和UI各自一份。推送常量切换
struct CameraInfo
{
    glm::mat4 view;
    glm::mat4 proj;
};
struct UniformBufferObject
{
    CameraInfo cameraInfo[2]; // 0: 3D, 1: UI
};
// diff: [test_dod14] end

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

// diff: [camera_perspective] end
//diff: [test_dod8] start
struct object_data
{
    glm::mat4 matrix;
};
struct VertexTransform
{
    glm::mat4 matrix = glm::mat4(1.0f);
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

//diff: [test_dod16] start
struct object_key
{
    uint32_t object_type{};
    uint32_t entity_index{};
    auto operator<=>(const object_key &) const = default;
};
namespace std
{
    template <>
    struct hash<object_key>
    {
        constexpr size_t operator()(const object_key &k) const noexcept
        {
            size_t h1 = hash<uint32_t>{}(k.object_type);
            size_t h2 = hash<uint32_t>{}(k.entity_index);
            return h1 ^ (h2 << 1);
        }
    };
}; // namespace std
static_assert(sizeof(object_key) == 2 * sizeof(uint32_t));

template <std::size_t Bits>
class render_version
{
    static_assert(Bits >= 1 && Bits <= 32,
                  "render_version: bit width must be in [1, 32]");

  public:
    // ── 最佳底层 uint 类型 ────────────────────────────────
    //   Bits <=  8 → uint8_t
    //   Bits <= 16 → uint16_t
    //   Bits <= 32 → uint32_t
    template <std::size_t B>
    using best_uint_t =
        std::conditional_t<(B <= 8), std::uint8_t,
                           std::conditional_t<(B <= 16), std::uint16_t, std::uint32_t>>;

    using underlying_t = best_uint_t<Bits>;

    static constexpr std::size_t bit_width = Bits;

    // ── 掩码：全 1 ────────────────────────────────────────
    //   用 lambda + if constexpr 避开 `1u << 32` 的 UB
    static constexpr underlying_t mask = [] {
        if constexpr (Bits >= 32)
            return static_cast<underlying_t>(~std::uint32_t{0});
        else
            return static_cast<underlying_t>((std::uint32_t{1} << Bits) - 1u);
    }();

    // ── 构造 ─────────────────────────────────────────────
    constexpr render_version() noexcept = default;

    // 从外部整数导入：自动按 mask 截断
    constexpr explicit render_version(std::uint32_t v) noexcept
        : value_{static_cast<underlying_t>(v & mask)}
    {
    }

    // ── 自增：到 mask 后回绕到 0，永不越界 ────────────────
    constexpr render_version &operator++() noexcept
    {
        value_ = static_cast<underlying_t>((value_ + 1u) & mask);
        return *this;
    }
    constexpr render_version operator++(int) noexcept
    {
        auto old = *this;
        ++*this;
        return old;
    }

    // ── 隐式转换到最佳 uint 类型 ──────────────────────────
    [[nodiscard]] constexpr operator underlying_t() const noexcept
    {
        return value_;
    }

    // 显式取 uint32（打包用）
    [[nodiscard]] constexpr std::uint32_t to_u32() const noexcept
    {
        return static_cast<std::uint32_t>(value_);
    }

    [[nodiscard]] constexpr bool operator==(const render_version &) const noexcept =
        default;

  private:
    underlying_t value_ = 0;
};
using render_version_t = render_version<16>; // 16 位版本号
// picking_result：与 R32G32B32A32_UINT 附件 + frag 的 uvec4 输出严格对齐（16B）
// xy = object_key(type_id, entity_index) 外键；z = primitive_id；w = hover_fn（池实体下标，0xFFFFFFFF = 未绑定）
struct picking_result
{
    // ── 位分配：由 version_t 的 bit_width 推导出 primitive 位宽 ──
    using version_t = render_version_t; // 例如 render_version<16>
    static constexpr std::size_t version_bits = version_t::bit_width;
    static constexpr std::size_t total_bits = 32;
    static constexpr std::size_t primitive_bits = total_bits - version_bits;

    static_assert(primitive_bits >= 1 && primitive_bits < total_bits,
                  "picking_result: version_t leaves no room for primitive_id");

    // ── primitive_id 的返回类型同样按位宽选 ─────────────────
    using primitive_t = version_t::template best_uint_t<primitive_bits>;

    // ── 掩码 ────────────────────────────────────────────────
    static constexpr std::uint32_t primitive_mask =
        (std::uint32_t{1} << primitive_bits) - 1u;

    // ── 数据 ────────────────────────────────────────────────
    object_key key; // outPicking.xy
    std::uint32_t
        packed; // outPicking.z：高 version_bits=版本，低 primitive_bits=primitive_id
    std::uint32_t hover_fn; // outPicking.w

    // ── 读取 ────────────────────────────────────────────────
    [[nodiscard]] constexpr primitive_t primitive_id() const noexcept
    {
        return static_cast<primitive_t>(packed & primitive_mask);
    }
    [[nodiscard]] constexpr version_t::underlying_t render_version() const noexcept
    {
        return version_t{packed >> version_bits}; // 构造时内部会再 mask 一次
    }

    // ── 写入（shader 侧对应同一个 pack 逻辑）────────────────
    static constexpr std::uint32_t pack(version_t v, primitive_t p) noexcept
    {
        return (v.to_u32() << version_bits) |
               (static_cast<std::uint32_t>(p) & primitive_mask);
    }
};
static_assert(sizeof(picking_result) == 16);
//diff: [test_dod16] end
struct mesh_data
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
    bool operator==(const mesh_data &o) const noexcept = default;
    constexpr bool valid() const noexcept
    {
        return vertexCount != 0 && indexCount != 0;
    }
};
struct PushData
{
    // 字段顺序必须与 test_dod19.vert 的 PushConsts 完全一致
    // （GLSL: vertex/data/commandConstants/geomEntry，Glyph 不需要 attributeAddress）
    uint64_t vertexAddress;           // 全局顶点缓冲区地址
    uint64_t instanceAddress;         // 全局实例堆地址（shader: dataAddress）
    uint64_t commandConstantsAddress; // 命令常量缓冲区地址
    uint32_t
        geomEntry; // 几何入口 id（专用字段，不再借用相机索引）：0=普通 1=描边批处理 2=填充扇形批处理
    uint32_t renderVersion = 0; // 渲染的版本。picking跨帧需要版本来过滤就版本
    // 一条 UI 命令的全部参数（与 test_ui_ability.frag 的 push_constant 严格一致）
    glm::vec4 uiRect{};   // xy = 命令矩形左上角（像素），zw = 宽高（像素）
    glm::vec4 uiColor{};  // 主色
    glm::vec4 uiColor2{}; // 次色（渐变终点 / 阴影色 / 底色）
    glm::vec4 uiParams{}; // x = mode，yzw = 模式参数
    glm::vec4 uiAux{};    // 追加参数（线端点 / 偏移 / 角度 / 图集 UV）
    // 拾取：x=object_type(命令 id) y=entity_index z=(version<<16)|primitive w=hover_fn
    // NOTE: 加上这一组正好 128 字节 = VkPhysicalDeviceLimits::maxPushConstantsSize 的保证下限
    glm::uvec4 uiPick{};

    bool operator==(const PushData &o) const noexcept = default;
    constexpr bool valid() const noexcept
    {
        return vertexAddress != 0 && instanceAddress != 0 && commandConstantsAddress != 0;
    }
};

// ═══════════════════════════════════════════════════════════════════════
// 离屏层（offscreen render target）
//
//   blur / backdrop blur / group opacity 的真实做法：先把内容画到离屏图像上，
//   再模糊 / 按组不透明度合成回主通道。这里的离屏图像同时具备两种身份：
//     · 渲染目标（COLOR_ATTACHMENT）
//     · 可被 bindless 数组采样的纹理（SAMPLED）
//   每 frame-in-flight 一套，尺寸固定成图块大小（比全屏省得多）。
// ═══════════════════════════════════════════════════════════════════════
struct ui_layer_ctx
{
    static constexpr uint32_t kPerFrame = 4; // A=模糊结果 B=糊的临时 C=组 D=背景
    static constexpr VkExtent2D kExtent{320, 192};
    struct layer
    {
        VkImage image{};
        VkImageView view{};
        uint32_t texture{};
        uint32_t sampler{};
    };
    std::array<std::array<layer, kPerFrame>, MAX_FRAMES_IN_FLIGHT> layers{};
};
ui_layer_ctx &uiLayers()
{
    static ui_layer_ctx ctx;
    return ctx;
}

// 建离屏层：用 create_texture 建（能生成视图、能进 bindless 数组），
// 初始内容是一张全透明图，之后每帧都会被 clear，所以内容无所谓。
constexpr auto initUiLayers(const LogicalDevice &device, const CommandPool &pool,
                            const Queue &queue, VkFormat format, uint32_t sampler_index,
                            auto &textureManager, auto &descriptorSetManager)
{
    auto &ctx = uiLayers();

    std::vector<uint8_t> zeros(static_cast<std::size_t>(ui_layer_ctx::kExtent.width) *
                                   ui_layer_ctx::kExtent.height * 4u,
                               0u);

    for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame)
        for (uint32_t slot = 0; slot < ui_layer_ctx::kPerFrame; ++slot)
        {
            uint32_t texture_index = 0;
            for (auto i : textureManager.view_free_indexes())
            {
                texture_index = static_cast<uint32_t>(i);
                break;
            }

            mcs::vulkan::memory::create_texture builder{
                [format](mcs::vulkan::memory::create_texture::image_info imageInfo)
                    -> mcs::vulkan::memory::create_texture::create_info {
                    return {.imageType = VK_IMAGE_TYPE_2D,
                            .format = format,
                            .extent = {.width = imageInfo.extent.width,
                                       .height = imageInfo.extent.height,
                                       .depth = 1},
                            .mipLevels = 1,
                            .arrayLayers = 1,
                            .samples = VK_SAMPLE_COUNT_1_BIT,
                            .tiling = VK_IMAGE_TILING_OPTIMAL,
                            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                     VK_IMAGE_USAGE_SAMPLED_BIT |
                                     VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
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
                                                 .levelCount = 1,
                                                 .baseArrayLayer = 0,
                                                 .layerCount = 1}};
                }};

            auto resource = builder.build(
                device, pool, queue,
                mcs::vulkan::memory::create_texture::image_info{
                    .extent = ui_layer_ctx::kExtent,
                    .pixels = std::span<const uint8_t>{zeros.data(), zeros.size()},
                    .mipLevels = 1});

            ctx.layers[frame][slot] = ui_layer_ctx::layer{.image = resource.image(),
                                                          .view = resource.imageView(),
                                                          .texture = texture_index,
                                                          .sampler = sampler_index};

            textureManager.use_slot(texture_index, std::move(resource), "ui.layer");
        }

    descriptorSetManager.update_texture(textureManager.view_used_indexes());
}

constexpr auto inputInit(float aspect)
{
    auto camera = [&]() {
        using namespace camera; // 你的 camera 命名空间
        // 视图：从 eye/center/up 构建 ViewMatrixObject
        glm::vec3 eye(0.0f, 0.0f, 2.0f);
        glm::vec3 center(0.0f, 0.0f, 0.0f);
        glm::vec3 up(0.0f, 1.0f, 0.0f);
        glm::vec3 forward = glm::normalize(center - eye);
        RightHandedView view;
        view.setPosition(eye).setOrientation(glm::quatLookAt(forward, up)); // 右手系

        // 投影：注意原 fovy 是弧度，这里要转成度数，因为构造函数接收度数
        float fovDeg = glm::degrees(glm::radians(45.0f)); // 就是 45.0f
        VulkanPerspectiveProjection proj(fovDeg, aspect, 0.1f, 10.0f);
        return GenCamera(RightHandedView::lookAt(glm::vec3(0, 0, 2), glm::vec3(0, 0, 0),
                                                 glm::vec3(0, 1, 0)),
                         std::move(proj));
    }();
    // 在创建 camera 之后，创建 uiCamera.与i开始是单位矩阵
    auto uiCamera = []() {
        using namespace camera;
        // 视图：相机位于原点，无旋转 → 视图矩阵 = I
        auto uiView = camera::RightHandedView{
            glm::vec3(0.0f, 0.0f, 0.0f), // 位置为原点
            glm::identity<glm::quat>()   // 无旋转
        };
        // 投影：正交范围 [-1,1] 且 near=0, far=1 → 投影矩阵 = I //NOTE: -10.0f, 10.0f 避免绕 Y X 被裁剪
        auto uiProj = camera::VulkanUIOrthographicProjection{-1.0f, 1.0f,   -1.0f,
                                                             1.0f,  -10.0f, 10.0f};
        return camera::GenCamera(uiView, uiProj);
    }();
    auto input = std::make_unique<glfw_input>();
    return make_aggregate<"inputDataCtx", "input", "camera", "uiCamera", "clock">(
        std::move(input), std::move(camera), std::move(uiCamera), FrameClock{});
}
template <class InitInput>
struct genInputCtx
{
    using InputRef = decltype(*(std::declval<InitInput &>().input.get()));
    using CameraRef =
        std::add_lvalue_reference_t<decltype(std::declval<InitInput &>().camera)>;
    using UICameraRef =
        std::add_lvalue_reference_t<decltype(std::declval<InitInput &>().uiCamera)>;
    using ClockRef =
        std::add_lvalue_reference_t<decltype(std::declval<InitInput &>().clock)>;
    InputRef input;
    CameraRef camera;
    UICameraRef uiCamera;
    ClockRef clock;

    explicit constexpr genInputCtx(InitInput &initInput) noexcept
        : input{*initInput.input.get()}, camera{initInput.camera},
          uiCamera{initInput.uiCamera}, clock{initInput.clock}
    {
    }
};
using InputCtx = genInputCtx<decltype(inputInit(0))>;
namespace ui_new
{
    struct ScreenWidget; //NOTE: 指针用途前向声明
}
namespace shader_data
{
    //  C++是静态类型语言。传递指针，必须能用指定的结构体，解析指针
    struct Glyph
    {
        static constexpr auto type_id = 2;
        uint64_t data;           //关联的CPU指针
        uint32_t entity_index;   // 拾取实体索引（outPicking.y）
        uint32_t textureIndex;   // 纹理数组下标（bindless）
        uint32_t samplerIndex;   // 采样器数组下标（bindless）
        uint32_t fontType;       // 字体类型（与 FontType 枚举一致）
        float pxRange;           // MSDF 距离场范围
        uint32_t modulateFlag;   // 1 = 用顶点色调制
        glm::vec4 color;         // 顶点色（默认白）
        glm::mat4 model;         // 平移 + 缩放：[-0.5,0.5] quad -> NDC 字形矩形
        UvTransform uvTransform; // 图集 UV 变换
        uint32_t hover_fn = ~0U;
    };
    static_assert(sizeof(Glyph) == 136);

    struct BufferResource
    {
        mcs::vulkan::memory::auto_map_buffer buffer{};
        VkDeviceSize capacity{};
        constexpr auto write(size_t offset, const void *src, size_t size) noexcept
        {
            // 断言：偏移 + 大小 必须 ≤ 总容量
            assert(offset + size <= capacity);
            return ::memcpy(static_cast<char *>(buffer.mapPtr()) + offset, src, size);
        }
        BufferResource() = default;
        constexpr BufferResource(const LogicalDevice &device, VkDeviceSize capacity,
                                 VkBufferUsageFlags usage, VkSharingMode sharingMode,
                                 VkMemoryPropertyFlags properties)
            : buffer{mcs::vulkan::memory::auto_map_buffer(
                  mcs::vulkan::memory::create_simple_buffer(
                      device,
                      {.size = capacity, .usage = usage, .sharingMode = sharingMode},
                      properties),
                  capacity)},
              capacity{capacity}
        {
        }
    };
    struct BufferResourceWithAddress
    {
        mcs::vulkan::memory::auto_map_buffer buffer{};
        VkDeviceSize capacity{};
        VkDeviceAddress address{};

        constexpr auto write(size_t offset, const void *src, size_t size) noexcept
        {
            // 断言：偏移 + 大小 必须 ≤ 总容量
            assert(offset + size <= capacity);
            return ::memcpy(static_cast<char *>(buffer.mapPtr()) + offset, src, size);
        }

        BufferResourceWithAddress() = default;
        constexpr BufferResourceWithAddress(const LogicalDevice &device,
                                            VkDeviceSize capacity,
                                            VkBufferUsageFlags usage,
                                            VkSharingMode sharingMode,
                                            VkMemoryPropertyFlags properties)
            : buffer{mcs::vulkan::memory::auto_map_buffer(
                  mcs::vulkan::memory::create_simple_buffer(
                      device,
                      {.size = capacity, .usage = usage, .sharingMode = sharingMode},
                      properties),
                  capacity)},
              capacity{capacity}, address{device.getBufferDeviceAddress(
                                      {.sType = sType<VkBufferDeviceAddressInfo>(),
                                       .buffer = buffer.buffer()})}
        {
        }
    };
    struct CommandConstant
    {
        uint32_t type_id;
        uint32_t adddress_offset;
        // NOTE: 可能是错误的方向
        uint32_t slot_count; // 每实例顶点槽位数（通用网格类型用，任意 N；固定类型忽略）
    };
    static_assert(sizeof(CommandConstant) == 12); // 与 GLSL 端一致

    // NOTE: 内存管理还是 自己自带的为好
    // NOTE: 必须枚举出 shader可以输出的全部类型

    // ===================== hover 函数池 + 外键状态机 =====================
    // 全局唯一一份 实体→函数 关联：一个池实体 = 一个可共享的 hover 处理函数，
    // 多个字形可指向同一个 hover_fn（函数被共享）。hover_fn = 0xFFFFFFFF 表示未绑定。
    struct hover_pool
    {
        using hover_callback_t = void (*)(picking_result, bool, InputCtx &,
                                          ui_new::ScreenWidget *) noexcept;
        std::vector<hover_callback_t> hover_fns;

        // 绑定一个可共享的 hover 函数，返回池实体下标（0 也是合法下标）
        uint32_t bind(hover_callback_t fn)
        {
            hover_fns.push_back(std::move(fn));
            return hover_fns.size() - 1;
        }
        // 按实体下标调用（0xFFFFFFFF = 未绑定；越界/已释放 = 不调用）
        void call(uint32_t entity, const picking_result &r, bool enter,
                  InputCtx &inputCtx, ui_new::ScreenWidget *screen) noexcept
        {
            // 拾取未命中时会返回 type=0/entity=0 的结果，这里必须是保护而不是断言
            if (entity == ~0U || entity >= hover_fns.size())
                return;
            hover_fns[entity](r, enter, inputCtx, screen);
        }
    };

    // 外键状态机：只存当前命中 cur（换新时旧 cur 就是 pre，用来发 leave）
    struct hover_manager
    {
        static constexpr auto hover_leave = false;
        static constexpr auto hover_enter = true;
        ui_new::ScreenWidget *screen = nullptr;
        picking_result cur{object_key{0xFFFFFFFF, 0}, 0,
                           0}; // 当前命中（key 无效 = 未悬停）

        constexpr void resetPicking() noexcept
        {
            cur = {object_key{0xFFFFFFFF, 0}, 0, 0};
        }

        constexpr void hover(const picking_result &r, hover_pool &pool,
                             InputCtx &inputCtx) noexcept
        {
            static_assert(0xFFFFFFFF == uint32_t{~0U});
            if (cur.key == r.key)
                return; // 同一外键：无动作
            // 0 和 0xFFFFFFFF 都表示"没命中"（拾取 miss 时 object_type = 0）
            const auto hasObject = [](uint32_t t) noexcept {
                return t != 0U && t != ~0U;
            };
            if (hasObject(cur.key.object_type) && cur.hover_fn != ~0U)
                pool.call(cur.hover_fn, cur, hover_leave, inputCtx, screen);
            cur = r;
            if (hasObject(r.key.object_type) && r.hover_fn != ~0U)
                pool.call(r.hover_fn, r, hover_enter, inputCtx, screen);
        }
    };

    // ===================== 矩形线框（布局 debug 边框；独立于圆角阴影矩形）=====================
    struct UiRect
    {
        static constexpr auto type_id = 3;
        uint64_t data;         //关联的CPU指针
        uint32_t entity_index; // 布局节点索引（拾取外键）
        uint32_t hover_fn;     // hover 池下标（0xFFFFFFFF = 未绑定）
        glm::vec4 center_size; // center.xy + size.xy（NDC）
        glm::vec4 color;       // 边框色
        float border;          // 边框半宽（NDC）
    };
    static_assert(sizeof(UiRect) == 56);

    // ===================== 圆角阴影矩形（照抄 test_sdf Rectangle；TYPE_ROUND_RECT=5）=====================
    // 与 GLSL Rectangle（RECTANGLE_SIZE=264）严格一致；HTML box-shadow 风格
    struct VertexTransform
    {
        glm::mat4 matrix;
    };
    struct Rectangle
    {
        static constexpr auto type_id = 5;
        uint64_t data;                   //关联的CPU指针
        uint32_t entity_index;           // 拾取实体索引（outPicking.y）
        uint32_t effects;                // 特效标志（FX_ROUNDED/SHADOW/FILL）
        glm::vec4 colors[4];             // 四顶点颜色（单色 = 四顶点同色）
        glm::mat4 model;                 // 平移 + 旋转
        VertexTransform vertexTransform; // 顶点缩放（quad × size）
        UvTransform uvTransform;         // UV 变换
        glm::vec2 size;                  // 卡片完整宽/高（NDC，SDF 用）
        glm::vec2 shadowOffset;          // 阴影偏移（相对卡片尺寸）
        glm::vec4 radiusSoftness;        // x=圆角比例 y=边缘柔化 z=阴影模糊 w=阴影扩散
        glm::vec4 shadowColor;           // 阴影色 RGBA（a=0 → 无阴影）
    };
    static_assert(sizeof(Rectangle) == 272);
    static constexpr uint32_t FX_ROUNDED = 1u;
    static constexpr uint32_t FX_SHADOW = 2u;
    static constexpr uint32_t FX_FILL = 4u;

    // ===================== 顶点属性（普通绘制用）：每顶点一份 =====================
    // 顶点本身 + 每顶点属性（无 SDF）；多实例共享顶点/索引，实例数据 = 属性池
    // attrIdx = gl_InstanceIndex × N + 槽位（N = 命令常量 slot_count，任意）
    struct VertexAttr
    {
        glm::vec4 color; // 每顶点颜色（插值 → 渐变）
    };
    static_assert(sizeof(VertexAttr) == 16); //NOTE:

}; // namespace shader_data

//diff: [test_dod24.cpp] start [main中的修改，没有标记]
namespace shader_data
{
    // ===================== 静态绘制状态（不随区域变化） =====================
    struct StaticDrawKey
    {
        VkPipeline pipeline{};
        VkPipelineLayout layout{};
        mesh_data mesh;
        PushData pushData; // 注意：commandConstantsAddress 将在提交时动态填充
        bool operator==(const StaticDrawKey &) const = default;
        constexpr bool valid() const noexcept
        {
            return pipeline != nullptr && layout != nullptr && mesh.valid() &&
                   pushData.valid();
        }
    };

    // ===================== 动态绘制状态（随区域变化） =====================
    constexpr static bool equal_scissors(std::span<const VkRect2D> a,
                                         std::span<const VkRect2D> b) noexcept
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
        {
            const auto &ra = a[i], &rb = b[i];
            if (ra.offset.x != rb.offset.x || ra.offset.y != rb.offset.y ||
                ra.extent.width != rb.extent.width ||
                ra.extent.height != rb.extent.height)
                return false;
        }
        return true;
    }
    constexpr static bool equal_viewports(std::span<const VkViewport> a,
                                          std::span<const VkViewport> b) noexcept
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
        {
            const auto &va = a[i], &vb = b[i];
            if (va.x != vb.x || va.y != vb.y || va.width != vb.width ||
                va.height != vb.height || va.minDepth != vb.minDepth ||
                va.maxDepth != vb.maxDepth)
                return false;
        }
        return true;
    }

    // 一帧内可能出现的“Vulkan 动态状态组合”。
    // 目前包含 scissor / viewport 两组，均可视为“每段可重复使用的区域状态”。
    // 未来若要扩展（如 blend constants、depth bounds），在此加字段即可。
    struct DynamicDrawState
    {
        std::vector<VkRect2D> scissors;
        std::vector<VkViewport> viewports;

        constexpr bool operator==(const DynamicDrawState &o) const noexcept
        {
            return equal_scissors(scissors, o.scissors) &&
                   equal_viewports(viewports, o.viewports);
        }

        constexpr void clear() noexcept
        {
            scissors.clear();
            viewports.clear();
        }
    };

    // ═══════════════════════════════════════════════════════════════
    // Slice<T>：一个半开区间 [start, start+count)，指向某个 Arena<T>
    //   - 语义上等价于 std::span，但只保存 (start, count)，不持有指针；
    //     需要读取时用 .view(pool) 把区间映射回实际的 std::span。
    //   - 用类型参数 T 区分“这段是 VkRect2D 还是 VkViewport”，
    //     避免把两者的 start/count 混在一起。
    // ═══════════════════════════════════════════════════════════════
    template <typename T>
    struct Slice
    {
        uint32_t start = 0;
        uint32_t count = 0;

        [[nodiscard]] std::span<const T> view(const std::vector<T> &pool) const noexcept
        {
            return {pool.data() + start, count};
        }

        [[nodiscard]] bool empty() const noexcept
        {
            return count == 0;
        }
    };

    // ═══════════════════════════════════════════════════════════════
    // Arena<T>：预分配 + 线性追加的池子
    //   - 每帧只把 count 归零，vector 的 capacity 跨帧保留；
    //   - push_span / push_one 只会因容量不足而增长，之后不再分配；
    //   - 追加返回 Slice<T>（或索引），外部只需保存“区间”而非“指针”。
    //
    //   与 GPU 侧的 BufferResource + offset 完全同构：
    //     GPU: 一次性 buffer + indirectOffset/constantOffset
    //     CPU: 一次性 vector + count
    // ═══════════════════════════════════════════════════════════════
    template <typename T>
    struct Arena
    {
        std::vector<T> pool;
        uint32_t count = 0;

        void reset() noexcept
        {
            count = 0;
        }
        void reserve(size_t n)
        {
            pool.reserve(n);
        }

        // 追加一段连续数据，返回它在新 arena 中的位置
        Slice<T> push_span(std::span<const T> src)
        {
            const uint32_t start = count;
            if (count + src.size() > pool.size())
                pool.resize(std::max<size_t>(pool.size() * 2, count + src.size()));
            std::memcpy(pool.data() + start, src.data(), src.size() * sizeof(T));
            count += static_cast<uint32_t>(src.size());
            return {start, static_cast<uint32_t>(src.size())};
        }

        // 追加单个元素，返回它的引用（便于就地进行进一步赋值）
        T &push_one(const T &v)
        {
            if (count >= pool.size())
                pool.resize(std::max<size_t>(pool.size() * 2, 16));
            T &slot = pool[count];
            slot = v;
            ++count;
            return slot;
        }

        [[nodiscard]] std::span<const T> view(Slice<T> s) const noexcept
        {
            return {pool.data() + s.start, s.count};
        }
    };

    // ═══════════════════════════════════════════════════════════════
    // DrawSegment：一个绘制段 = 一次实际的 drawIndexedIndirect 调用
    //
    // 段由四个概念构成，每个概念各占一行，避免“八个 u32 平铺”：
    //   1) 动态状态（Vulkan 每段的 scissor/viewport）→ 用 Slice<T> 引用 arena
    //   2) 命令条数 commandCount（GPU 侧一批 command 的数量）
    //   3) GPU 字节偏移（finishDraw 时回填，指向 indirect/const buffer）
    //
    // 段在 pending.cmds / consts 里严格顺序排列、无间隙、无重叠，
    // 因此不需要 commandStart —— 遍历时按 commandCount 顺序累加即可。
    // ═══════════════════════════════════════════════════════════════
    struct DrawSegment
    {
        // ── 动态状态：本段对应的 scissor / viewport 切片 ──
        Slice<VkRect2D> scissors;
        Slice<VkViewport> viewports;

        // ── 本段包含的 drawIndexedIndirect 命令数 ──
        uint32_t commandCount = 0;

        // ── GPU buffer 字节偏移（finishDraw 回填）──
        uint32_t indirectByteOffset = 0;
        uint32_t constantByteOffset = 0;
    };
    static_assert(std::is_trivially_copyable_v<DrawSegment>);

    // ═══════════════════════════════════════════════════════════════
    // UnitRef：一次“静态状态相同的连续绘制”的引用
    //   - 段本身放在 segmentPool 里，UnitRef 只持有 [start, count)
    //   - 提交时按 UnitRef.staticKey.pipeline 排序绑定，减少管线切换
    // ═══════════════════════════════════════════════════════════════
    struct UnitRef
    {
        StaticDrawKey staticKey;
        Slice<DrawSegment> segments; // 在 segmentPool 中的切片
    };

    // ═══════════════════════════════════════════════════════════════
    // PendingDraw：正在构建中的 DrawUnit（同一静态状态）
    //   - 只保留命令缓冲，段一旦固定就写入 segmentPool，不再 move
    // ═══════════════════════════════════════════════════════════════
    struct PendingDraw
    {
        StaticDrawKey staticKey;
        std::vector<VkDrawIndexedIndirectCommand> cmds; // 跨段连续存储
        std::vector<CommandConstant> consts;            // 与 cmds 一一对应
        size_t currentSegmentCmdStart{0};               // 当前活动段的命令起点
    };

    // ═══════════════════════════════════════════════════════════════
    // DrawRecorder：多实例绘制的命令录制器
    //
    // 设计要点：
    //   1. GPU 侧数据（顶点/实例/命令/常量）都放在固定 buffer 里，
    //      每帧只归零 offset；动态的只是“写入量”，不是“分配”。
    //   2. CPU 侧元数据（segment/scissor/viewport/unit）同样走 arena：
    //      一次性预分配 + 每帧 count 归零，稳态下零分配。
    //   3. 段与段的动态状态通过 Slice<T> 引用 arena 中的连续切片，
    //      DrawSegment 本身保持 POD，可跨帧反复覆写。
    // ═══════════════════════════════════════════════════════════════
    class DrawRecorder
    {
      public:
        // ── GPU 缓冲 ─────────────────────────────────
        BufferResourceWithAddress globalVertexBuffer{};
        BufferResource globalIndexBuffer{};
        BufferResourceWithAddress globalHeapBuffer;
        BufferResource indirectDrawBuffer;
        BufferResourceWithAddress commandConstantsBuffer;

        // ── GPU 端每帧线性偏移（写入量）───────────────
        size_t heapOffset = 0;
        size_t indirectOffset = 0;
        size_t constantOffset = 0;

        // ── CPU 端 arena（元数据）═════════════════════
        // 所有段平铺存储；UnitRef 通过 Slice<DrawSegment> 引用其中的一段
        Arena<DrawSegment> segments;
        // 所有 scissor / viewport 平铺存储；DrawSegment 通过 Slice<T> 引用
        Arena<VkRect2D> scissors;
        Arena<VkViewport> viewports;
        // 所有 unit 平铺存储；一次提交对应 count 个 UnitRef
        Arena<UnitRef> units;

        // ── 当前构建状态 ─────────────────────────────
        StaticDrawKey currentStatic{};
        DynamicDrawState currentDynamic;

        // 当前 unit 在 segmentPool 中的起始下标；
        // addInstances 懒启动时记录，finishDraw 用它确定本 unit 的段区间
        uint32_t currentUnitSegmentStart = 0;

        // ── Pending（仅暂存，不再 move 出去）────────
        PendingDraw pending{};
        bool pendingActive = false;

        // ---------- 构造函数 ----------
        static auto newVertexBuffer(const LogicalDevice &device, VkDeviceSize capacity)
        {
            return BufferResourceWithAddress{
                device, capacity,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                VK_SHARING_MODE_EXCLUSIVE,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
        }

        static auto newIndexBuffer(const LogicalDevice &device, VkDeviceSize capacity)
        {
            return BufferResource{device, capacity, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                  VK_SHARING_MODE_EXCLUSIVE,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
        }

        static auto newHeapBuffer(const LogicalDevice &device, VkDeviceSize capacity)
        {
            return BufferResourceWithAddress{
                device, capacity,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                VK_SHARING_MODE_EXCLUSIVE,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
        }

        static auto newIndirectDrawBuffer(const LogicalDevice &device,
                                          VkDeviceSize capacity)
        {
            return BufferResource{device, capacity, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                                  VK_SHARING_MODE_EXCLUSIVE,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
        }

        static auto newCommandConstantsBuffer(const LogicalDevice &device,
                                              VkDeviceSize capacity)
        {
            return BufferResourceWithAddress{
                device, capacity,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                VK_SHARING_MODE_EXCLUSIVE,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
        }

        constexpr DrawRecorder(const LogicalDevice &device, VkDeviceSize vertexCapacity,
                               VkDeviceSize indexCapacity, VkDeviceSize heapCapacity,
                               VkDeviceSize indirectDrawCapacity,
                               VkDeviceSize commandConstantsCapacity)
            : globalVertexBuffer{newVertexBuffer(device, vertexCapacity)},
              globalIndexBuffer{newIndexBuffer(device, indexCapacity)},
              globalHeapBuffer{newHeapBuffer(device, heapCapacity)},
              indirectDrawBuffer{newIndirectDrawBuffer(device, indirectDrawCapacity)},
              commandConstantsBuffer{
                  newCommandConstantsBuffer(device, commandConstantsCapacity)}
        {
            // 稳态零分配的预留（数字按“不超过 100 条命令”估算，第一次运行后稳定）
            segments.reserve(64);
            scissors.reserve(64);
            viewports.reserve(64);
            units.reserve(32);
            pending.cmds.reserve(128);
            pending.consts.reserve(128);
        }

        // ---------- 每帧重置 ----------
        // 只归零“写指针”，arena 的 capacity 全部保留
        void reset() noexcept
        {
            heapOffset = 0;
            indirectOffset = 0;
            constantOffset = 0;

            segments.reset();
            scissors.reset();
            viewports.reset();
            units.reset();
            currentUnitSegmentStart = 0;

            currentStatic = {};
            currentDynamic.clear();

            pending.staticKey = {};
            pending.cmds.clear();
            pending.consts.clear();
            pending.currentSegmentCmdStart = 0;

            pendingActive = false;
        }

        // ---------- 只结束当前活动段，绝不修改已经记录的段 ----------
        void pushCurrentSegment() noexcept
        {
            if (!pendingActive)
                return;

            const size_t cmdCount = pending.cmds.size() - pending.currentSegmentCmdStart;
            if (cmdCount == 0)
                return;

            // 追加动态状态切片 + 记录本段的命令条数
            // 注意：currentDynamic 此刻仍是“旧状态”，push 完才轮到 setter 去更新
            segments.push_one(DrawSegment{
                .scissors = scissors.push_span(currentDynamic.scissors),
                .viewports = viewports.push_span(currentDynamic.viewports),
                .commandCount = static_cast<uint32_t>(cmdCount),
                .indirectByteOffset = 0, // finishDraw 回填
                .constantByteOffset = 0, // finishDraw 回填
            });

            pending.currentSegmentCmdStart = pending.cmds.size();
        }

        // ---------- 结束当前 DrawUnit ----------
        void finishDraw() noexcept
        {
            if (!pendingActive)
                return;

            pushCurrentSegment();

            if (pending.cmds.empty())
            {
                pendingActive = false;
                return;
            }

            const uint32_t segBegin = currentUnitSegmentStart;
            const uint32_t segEnd = segments.count;
            assert(segEnd > segBegin);

            const size_t cmdBytes =
                pending.cmds.size() * sizeof(VkDrawIndexedIndirectCommand);
            const size_t constBytes = pending.consts.size() * sizeof(CommandConstant);
            const size_t baseIndirect = indirectOffset;
            const size_t baseConstant = constantOffset;

            // 上传 GPU（写入量动态，分配在构造函数时已固定）
            indirectDrawBuffer.write(baseIndirect, pending.cmds.data(), cmdBytes);
            commandConstantsBuffer.write(baseConstant, pending.consts.data(), constBytes);

            // 回填本 unit 内所有段的字节偏移。
            // 段在 pending.cmds 里顺序排列，commandStart 可推导，无需存储。
            uint32_t cmdCursor = 0;
            for (uint32_t i = segBegin; i < segEnd; ++i)
            {
                auto &seg = segments.pool[i];
                seg.indirectByteOffset = static_cast<uint32_t>(
                    baseIndirect + cmdCursor * sizeof(VkDrawIndexedIndirectCommand));
                seg.constantByteOffset = static_cast<uint32_t>(
                    baseConstant + cmdCursor * sizeof(CommandConstant));
                cmdCursor += seg.commandCount;
            }

            // 追加 UnitRef（只记录静态状态 + 段区间，不 move 任何 vector）
            units.push_one(UnitRef{
                .staticKey = pending.staticKey,
                .segments = {segBegin, segEnd - segBegin},
            });

            indirectOffset += cmdBytes;
            constantOffset += constBytes;

            pendingActive = false;
        }

        // ---------- 静态状态设置 ----------
        // 语义：任何一个静态状态变化都意味着“新的一次 draw call 边界”，
        //       先把当前 pending（如果有）收尾，再切换状态。
        void setPipeline(VkPipeline pipeline)
        {
            if (currentStatic.pipeline != pipeline)
            {
                if (currentStatic.valid())
                    finishDraw();
                currentStatic.pipeline = pipeline;
            }
        }

        void setLayout(VkPipelineLayout layout)
        {
            if (currentStatic.layout != layout)
            {
                if (currentStatic.valid())
                    finishDraw();
                currentStatic.layout = layout;
            }
        }

        void setPushData(const PushData &pushData)
        {
            if (currentStatic.pushData != pushData)
            {
                if (currentStatic.valid())
                    finishDraw();
                currentStatic.pushData = pushData;
            }
        }

        void setMesh(const mesh_data &mesh)
        {
            if (currentStatic.mesh != mesh)
            {
                if (currentStatic.valid())
                    finishDraw();
                currentStatic.mesh = mesh;
            }
        }

        // ---------- 动态状态设置 ----------
        // 语义：动态状态只在“段边界”才能切换；切换前必须先把旧段收尾。
        void setScissors(std::span<const VkRect2D> scissors_)
        {
            if (equal_scissors(currentDynamic.scissors, scissors_))
                return;

            if (pendingActive)
                pushCurrentSegment(); // 用旧状态收段

            currentDynamic.scissors.assign(scissors_.begin(), scissors_.end());
        }

        void setViewports(std::span<const VkViewport> viewports_)
        {
            if (equal_viewports(currentDynamic.viewports, viewports_))
                return;

            if (pendingActive)
                pushCurrentSegment();

            currentDynamic.viewports.assign(viewports_.begin(), viewports_.end());
        }

        // ---------- 添加实例数据 ----------
        // 一次 addInstances = 一条 drawIndexedIndirect 命令
        // 相邻且可合并的命令（同 index/vertex/type）会自动合并实例数
        template <typename T>
        void addInstances(std::span<const T> instances)
        {
            assert(currentStatic.valid());
            const auto &mesh = currentStatic.mesh;

            // 懒启动：第一次 addInstances 时记录本 unit 的段起点
            if (!pendingActive)
            {
                pending.staticKey = currentStatic;
                pending.cmds.clear();
                pending.consts.clear();
                pending.currentSegmentCmdStart = 0;
                currentUnitSegmentStart = segments.count; // ★ 段区间的起点
                pendingActive = true;
            }

            const size_t dataOffset = heapOffset;
            globalHeapBuffer.write(heapOffset, instances.data(),
                                   instances.size() * sizeof(T));
            heapOffset += instances.size() * sizeof(T);

            VkDrawIndexedIndirectCommand cmd =
                mesh.getDrawCommand(static_cast<uint32_t>(instances.size()));
            CommandConstant constant{.type_id = T::type_id,
                                     .adddress_offset = static_cast<uint32_t>(dataOffset),
                                     .slot_count = 0};

            // 合并：只有仍在“当前活动段”内的最后一条命令才能合并
            if (!pending.cmds.empty() &&
                pending.currentSegmentCmdStart < pending.cmds.size())
            {
                auto &lastCmd = pending.cmds.back();
                auto &lastConst = pending.consts.back();
                const bool canMerge = lastCmd.indexCount == cmd.indexCount &&
                                      lastCmd.firstIndex == cmd.firstIndex &&
                                      lastCmd.vertexOffset == cmd.vertexOffset &&
                                      lastCmd.firstInstance == cmd.firstInstance &&
                                      lastConst.type_id == constant.type_id;
                if (canMerge)
                {
                    lastCmd.instanceCount += cmd.instanceCount;
                    return;
                }
            }

            pending.cmds.push_back(cmd);
            pending.consts.push_back(constant);
        }

        // ---------- 结束录制 ----------
        void end()
        {
            finishDraw();
        }

        // ---------- 提交 Vulkan 命令 ----------
        void doDraw(CommandBufferView cmd)
        {
            assert(!pendingActive);

            // 顶点通过推送常量的设备地址传上去了
            cmd.bindIndexBuffer(globalIndexBuffer.buffer.buffer(), 0,
                                VK_INDEX_TYPE_UINT32);

            VkPipeline lastPipeline = VK_NULL_HANDLE;

            for (uint32_t u = 0; u < units.count; ++u)
            {
                const auto &unit = units.pool[u];
                assert(unit.staticKey.valid());

                // 只有管线真正改变时才调用绑定
                if (unit.staticKey.pipeline != lastPipeline)
                {
                    cmd.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                     unit.staticKey.pipeline);
                    lastPipeline = unit.staticKey.pipeline;
                }

                const auto segs = unit.segments.view(segments.pool);
                for (const auto &seg : segs)
                {
                    // 用 Slice<T>::view 把区间映射回 std::span，语义自明
                    cmd.setViewport(0, seg.viewports.view(viewports.pool));
                    cmd.setScissor(0, seg.scissors.view(scissors.pool));

                    PushData pc = unit.staticKey.pushData;
                    pc.commandConstantsAddress =
                        commandConstantsBuffer.address + seg.constantByteOffset;

                    cmd.pushConstants(unit.staticKey.layout,
                                      VK_SHADER_STAGE_VERTEX_BIT |
                                          VK_SHADER_STAGE_FRAGMENT_BIT,
                                      0, sizeof(PushData), &pc);

                    cmd.drawIndexedIndirect(indirectDrawBuffer.buffer.buffer(),
                                            seg.indirectByteOffset, seg.commandCount,
                                            sizeof(VkDrawIndexedIndirectCommand));
                }
            }
        }
    };

}; // namespace shader_data
//diff: [test_dod24.cpp] end

constexpr auto initDrawRecorder(const LogicalDevice &device)
{
    constexpr auto vertexCapacity = sizeof(Vertex) * 1000;
    constexpr auto indexCapacity = sizeof(uint32_t) * 1000;
    constexpr auto heapCapacity = sizeof(shader_data::Glyph) * 2000;
    // NOTE: 目前是不超过100条命令的
    constexpr auto indirectDrawCapacity = sizeof(VkDrawIndexedIndirectCommand) * 100;
    constexpr auto commandConstantsCapacity = sizeof(shader_data::CommandConstant) * 100;

    std::array<shader_data::DrawRecorder, MAX_FRAMES_IN_FLIGHT> drawRecorder{
        shader_data::DrawRecorder(device, vertexCapacity, indexCapacity, heapCapacity,
                                  indirectDrawCapacity, commandConstantsCapacity),
        shader_data::DrawRecorder(device, vertexCapacity, indexCapacity, heapCapacity,
                                  indirectDrawCapacity, commandConstantsCapacity)};
    return drawRecorder;
}
//

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

using mcs::vulkan::meta::static_string;

// 删除了 UI 命名空间及其所有依赖（Container/Row/Column/Expanded/Text Trait, UIBuilder, FlatLayoutTree 等）

constexpr auto init()
{
    //NOTE: make_unique 保证地址稳定
    auto ctx = std::make_unique<raii_vulkan>();
    auto window = std::make_unique<Window>();
    window->setup({.width = WIDTH, .height = HEIGHT}, TITLE); // NOLINT

    auto enables = enable_intance_build{}
                       .enableDebugExtension()
                       .enableValidationLayer()
                       .enableSurfaceExtension<Window>();

    constexpr auto APIVERSION = VK_API_VERSION_1_4;
    enables.check();
    enables.print();
    auto instance = std::make_unique<Instance>(
        create_instance{}
            .setCreateInfo(
                {.applicationInfo = {.pApplicationName = "Hello Triangle",
                                     .applicationVersion = vkMakeVersion(1, 0, 0),
                                     .pEngineName = "No Engine",
                                     .engineVersion = vkMakeVersion(1, 0, 0),
                                     .apiVersion = APIVERSION},
                 .enabledLayers = enables.enabledLayers(),
                 .enabledExtensions = enables.enabledExtensions()})
            .build());
    auto debuger = std::make_unique<Debugger>(
        create_debugger{}
            .setCreateInfo(create_debugger::defaultCreateInfo())
            .build(*instance.get()));
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
            {.shaderDemoteToHelperInvocation = VK_TRUE,
             .synchronization2 = VK_TRUE,
             .dynamicRendering = VK_TRUE},
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
        create_physical_device_selector{}
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
            .select(*instance.get())[0];
    auto physical = std::make_unique<PhysicalDevice>(std::move(physical_device));
    auto surface = std::make_unique<surface_impl<Window>>(*physical.get(), *window.get());
    const uint32_t GRAPHICS_QUEUE_FAMILY_IDX =
        create_queue_family_index_selector{}
            .requiredQueueFamily([&](const VkQueueFamilyProperties &qfp,
                                     uint32_t queueFamilyIndex) -> bool {
                return (qfp.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                       physical->getSurfaceSupportKHR(queueFamilyIndex, **surface);
            })
            .select(*physical.get())[0];
    auto device = std::make_unique<LogicalDevice>(
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
            .build(*physical.get()));
    auto GRAPHICS_AND_PRESENT = std::make_unique<Queue>(
        Queue{*device.get(),
              {.queue_family_index = GRAPHICS_QUEUE_FAMILY_IDX, .queue_index = 0}});
    auto commandPool = std::make_unique<CommandPool>(
        create_command_pool{}
            .setCreateInfo({.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                            .queueFamilyIndex = GRAPHICS_QUEUE_FAMILY_IDX})
            .build(*device.get()));
    auto commandBuffers =
        std::make_unique<CommandBuffers>(commandPool->allocateCommandBuffers(
            {.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
             .commandBufferCount = MAX_FRAMES_IN_FLIGHT}));

    auto swapchainBuild =
        create_swapchain{}
            .setCreateInfo(
                {.changeMinImageCount =
                     [](uint32_t minImageCount) noexcept { return minImageCount + 1; },
                 .candidateSurfaceFormats = {{.format = VK_FORMAT_B8G8R8A8_SRGB,
                                              .colorSpace =
                                                  VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}},
                 .imageArrayLayers = 1,
                 .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                               VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
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
    auto swapchain = swapchainBuild.build(*device.get(), *surface.get());

    frame_context<MAX_FRAMES_IN_FLIGHT> frameContext{*device.get(),
                                                     swapchain.imagesSize()};
    // hardware
    return make_aggregate<"hardwareCtx", "vulkan", "window", "instance", "debuger",
                          "physicalDevice", "surface", "device", "queue", "commandPool",
                          "commandBuffers", "swapchainBuild", "swapchain",
                          "frameContext">(
        std::move(ctx), std::move(window), std::move(instance), std::move(debuger),
        std::move(physical), std::move(surface), std::move(device),
        std::move(GRAPHICS_AND_PRESENT), std::move(commandPool),
        std::move(commandBuffers), std::move(swapchainBuild), std::move(swapchain),
        std::move(frameContext));
}

using mcs::vulkan::tool::resource_manager;

constexpr static auto font_linear_sampler_key = "font_linear_sampler";
// UI 纹理用：线性过滤 + clamp + 完整 mip 链（九宫格不能 repeat，否则边缘会串色）
constexpr static auto ui_clamp_sampler_key = "ui_clamp_linear_sampler";
constexpr static auto font_nearest_neighbor_sampler_key = "font_nearest_neighbor_sampler";
constexpr static auto texture_ui_key = "white";
constexpr auto descriptorInit(auto &hardwareCtx)
{
    auto &physical_device = *hardwareCtx.physicalDevice.get();
    auto &surface = *hardwareCtx.surface.get();
    auto &device = *hardwareCtx.device.get();
    auto &GRAPHICS_AND_PRESENT = *hardwareCtx.queue.get();
    auto &commandPool = *hardwareCtx.commandPool.get();

    constexpr int MAX_TEXTURES = 64; // 预分配最大纹理槽位数（足够大）
    constexpr int SAMPLER_COUNT = 5; // 4 个原有 + 1 个 UI 用的 clamp 线性采样器

    std::array<VkDescriptorBindingFlags, 4> bindingFlags = {
        // 绑定0：Uniform Buffer - 通常不需要绑定后更新
        0,
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT, //允许不连续占用纹理槽（如 1、3、5、33）
        0,
        0, // 对应 binding 3（Storage Buffer）
    };

    auto descriptorSetLayoutPtr = std::make_unique<DescriptorSetLayout>(
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
                             // 顶点阶段读段数据、片元阶段读端点
                             .stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                                           VK_SHADER_STAGE_FRAGMENT_BIT,
                         }, //diff: [test_indirectdraw] end
                     }})
            .build(device));
    auto &descriptorSetLayout = *descriptorSetLayoutPtr.get();

    auto descriptorPoolPtr = std::make_unique<DescriptorPool>(
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
            .build(device));
    using descriptorSetsType = decltype(descriptorPoolPtr->allocateDescriptorSets(
        {.descriptorSets = std::vector<VkDescriptorSetLayout>{MAX_FRAMES_IN_FLIGHT,
                                                              *descriptorSetLayout}}));
    auto descriptorSetsPtr =
        std::make_unique<descriptorSetsType>(descriptorPoolPtr->allocateDescriptorSets(
            {.descriptorSets = std::vector<VkDescriptorSetLayout>{
                 MAX_FRAMES_IN_FLIGHT, *descriptorSetLayout}}));
    auto &descriptorSets = *descriptorSetsPtr.get();

    // diff: [test_dod7] start: 不再是 vma 的内存
    auto uniformBuffersPtr = std::make_unique<
        std::array<mcs::vulkan::memory::auto_map_buffer, MAX_FRAMES_IN_FLIGHT>>();
    auto &uniformBuffers = *uniformBuffersPtr.get();
    {
        constexpr VkDeviceSize BUFFER_SIZE = sizeof(UniformBufferObject);
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
    auto textureManagerPtr =
        std::make_unique<resource_manager<mcs::vulkan::memory::resource, MAX_TEXTURES>>();
    auto &textureManager = *textureManagerPtr.get();
    {

        std::vector<mcs::vulkan::memory::resource> upload;
        std::vector<std::string> upload_keys;
        upload.reserve(4);
        upload.push_back(create_texture.templateForImage2d(
            device, commandPool, GRAPHICS_AND_PRESENT,
            raw_stbi_image{"textures/texture.jpg", STBI_rgb_alpha}, true));
        upload_keys.emplace_back("texture");
        uint32_t white = 0xFFFFFFFF;
        upload.push_back(
            create_texture.build(device, commandPool, GRAPHICS_AND_PRESENT,
                                 mcs::vulkan::memory::create_texture::image_info{
                                     .extent = {.width = 1, .height = 1},
                                     .pixels = std::span<const uint8_t>(
                                         reinterpret_cast<const uint8_t *>(&white), 4),
                                     .mipLevels = 1})); //创建纯白纹理
        upload_keys.emplace_back("white");

        upload.push_back(generateGradientTexture(device, commandPool,
                                                 GRAPHICS_AND_PRESENT, 2)); // 渐变
        upload_keys.emplace_back("gradient");
        upload.push_back(generateGradientTexture(device, commandPool,
                                                 GRAPHICS_AND_PRESENT, 1)); // 棋盘
        upload_keys.emplace_back("chessboard");

        for (auto [resource, index, key] :
             std::views::zip(upload, textureManager.view_free_indexes(), upload_keys))
        {
            textureManager.use_slot(index, std::move(resource), std::move(key));
        }
    }
    auto samplerManagerPtr = std::make_unique<resource_manager<Sampler, SAMPLER_COUNT>>();
    auto &samplerManager = *samplerManagerPtr.get();
    {
        std::vector<Sampler> samplers;
        std::vector<std::string> upload_keys;

        samplers.reserve(SAMPLER_COUNT);
        samplers.emplace_back(
            create_sampler{}
                .setCreateInfo(create_sampler::templateLinear())
                .enableAnisotropy(
                    device.physicalDevice()->getProperties().limits.maxSamplerAnisotropy)
                .updateMaxLodByMipmap(create_texture_mipLevels)
                .build(device)); // 线性采样器
        upload_keys.emplace_back("linear_sampler");
        samplers.emplace_back(create_sampler{}
                                  .setCreateInfo(create_sampler::templateNearest())
                                  .build(device)); // 最近邻采样器
        upload_keys.emplace_back("nearest_neighbor_sampler");

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
        upload_keys.emplace_back(font_linear_sampler_key);

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
        upload_keys.emplace_back(font_nearest_neighbor_sampler_key);

        // UI 专用：线性 + CLAMP_TO_EDGE + mipmap 线性（九宫格 / alpha mask / mipmap）
        samplers.emplace_back(
            create_sampler{}
                .setCreateInfo({.pNext = {},
                                .flags = {},
                                .magFilter = VK_FILTER_LINEAR,
                                .minFilter = VK_FILTER_LINEAR,
                                .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                                .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                .mipLodBias = 0,
                                .anisotropyEnable = VK_FALSE,
                                .maxAnisotropy = 1.0F,
                                .compareEnable = VK_FALSE,
                                .compareOp = VK_COMPARE_OP_ALWAYS,
                                .minLod = 0,
                                .maxLod = VK_LOD_CLAMP_NONE,
                                .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
                                .unnormalizedCoordinates = VK_FALSE})
                .build(device));
        upload_keys.emplace_back(ui_clamp_sampler_key);

        for (auto [resource, index, key] :
             std::views::zip(samplers, samplerManager.view_free_indexes(), upload_keys))
        {
            samplerManager.use_slot(index, std::move(resource), std::move(key));
        }
    }
    auto descriptorSetManager = make_aggregate<"descriptorSetManager",
                                               "update_uniform_buffer", "update_texture",
                                               "update_sampler">(
        [&device, &uniformBuffers, &descriptorSets]() {
            constexpr auto dstBinding = 0;
            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
                VkDescriptorBufferInfo uniformInfo{.buffer = uniformBuffers[i].buffer(),
                                                   .offset = 0,
                                                   .range = sizeof(UniformBufferObject)};
                VkWriteDescriptorSet writes{
                    .sType = sType<VkWriteDescriptorSet>(),
                    .dstSet = descriptorSets[i],
                    .dstBinding = dstBinding,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .pBufferInfo = &uniformInfo,
                };
                device.updateDescriptorSets(1, &writes, 0, nullptr);
            }
        },
        [&device, &textureManager, &descriptorSets](auto used_indexes) {
            constexpr auto dstBinding = 1;
            std::vector<VkDescriptorImageInfo> imageInfos(MAX_TEXTURES);
            for (auto index : used_indexes)
            {
                auto &image = textureManager.resources[index];
                imageInfos[index] = VkDescriptorImageInfo{
                    .sampler = nullptr,
                    .imageView = image.imageView(),
                    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            }
            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
                std::vector<VkWriteDescriptorSet> writes;
                for (uint32_t slot : used_indexes)
                {
                    writes.push_back({
                        .sType = sType<VkWriteDescriptorSet>(),
                        .dstSet = descriptorSets[i],
                        .dstBinding = dstBinding,
                        .dstArrayElement = slot,
                        .descriptorCount = 1,
                        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                        .pImageInfo =
                            &imageInfos[slot], // 安全：imageInfos 生命周期远长于此处
                    });
                }
                device.updateDescriptorSets(static_cast<uint32_t>(writes.size()),
                                            writes.data(), 0, nullptr);
            }
        },
        [&device, &samplerManager, &descriptorSets](auto used_indexes) {
            constexpr auto dstBinding = 2;
            std::vector<VkDescriptorImageInfo> samplerInfos(SAMPLER_COUNT);
            for (auto index : used_indexes)
            {
                auto &sampler = samplerManager.resources[index];
                samplerInfos[index] =
                    VkDescriptorImageInfo{.sampler = sampler.data(),
                                          .imageView = nullptr,
                                          .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED};
            }
            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
            {
                std::vector<VkWriteDescriptorSet> writes;
                for (uint32_t slot : used_indexes)
                {
                    writes.push_back({
                        .sType = sType<VkWriteDescriptorSet>(),
                        .dstSet = descriptorSets[i],
                        .dstBinding = dstBinding,
                        .dstArrayElement = static_cast<uint32_t>(slot),
                        // NOTE: 上面是 shader的信息。下面是传输的信息
                        .descriptorCount = 1,
                        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                        .pImageInfo =
                            &samplerInfos[slot], // 安全：imageInfos 生命周期远长于此处
                    });
                }
                device.updateDescriptorSets(static_cast<uint32_t>(writes.size()),
                                            writes.data(), 0, nullptr);
            }
        });

    return make_aggregate<"descriptorCtx", "descriptorSetLayout", "descriptorPool",
                          "descriptorSets", "uniformBuffers", "textureManager",
                          "samplerManager", "descriptorSetManager">(
        std::move(descriptorSetLayoutPtr), std::move(descriptorPoolPtr),
        std::move(descriptorSetsPtr), std::move(uniformBuffersPtr),
        std::move(textureManagerPtr), std::move(samplerManagerPtr),
        std::move(descriptorSetManager));
}
constexpr auto initFont(auto &hardwareCtx, auto &descriptorCtx)
{
    auto &device = *hardwareCtx.device.get();
    auto &GRAPHICS_AND_PRESENT = *hardwareCtx.queue.get();
    auto &commandPool = *hardwareCtx.commandPool.get();

    auto &textureManager = *descriptorCtx.textureManager.get();
    auto &samplerManager = *descriptorCtx.samplerManager.get();

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

    // 添加字体
    auto loaderPtr = std::make_unique<font::freetype_loader>();
    auto &loader = *loaderPtr.get();
    auto factory = font::make_font_factory(
        [&device, &commandPool, &GRAPHICS_AND_PRESENT, &textureManager,
         &samplerManager](font::FontInfo &info) {
            mcs::vulkan::memory::create_texture create_font_texture{
                [](mcs::vulkan::memory::create_texture::image_info imageInfo)
                    -> mcs::vulkan::memory::create_texture::create_info {
                    return {
                        .imageType = VK_IMAGE_TYPE_2D,
                        .format =
                            VK_FORMAT_R8G8B8A8_UNORM, //diff: [test_dod14] msdf是距离场。这个格式更好
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
            using texture_bind_sampler = mcs::vulkan::font::texture_bind_sampler;
            const auto &registration = info.registration;
            if (std::holds_alternative<stbi_image_type>(
                    registration.texture_info.image_variant))
            {
                auto sampler_index = samplerManager.find_slot_by_name(
                    info.registration.type == font::FontType::eMSDF
                        ? font_linear_sampler_key
                        : font_nearest_neighbor_sampler_key);
                if (not sampler_index)
                    throw std::logic_error{"couldn't find a suitable sampler_index"};

                using image_type = stbi_image_type::type;
                const auto &imageInfo =
                    std::get<stbi_image_type>(registration.texture_info.image_variant);
                auto image =
                    image_type{imageInfo.image_path.data(), imageInfo.image_format};
                auto texWidth = image.width();
                auto texHeight = image.height();
                auto imageSize = image.size();

                for (auto texture_index :
                     textureManager.view_free_indexes() | std::views::take(1))
                {
                    // NOTE: update: bind
                    info.registration.texture_info.bind = texture_bind_sampler{
                        .texture_index = static_cast<uint32_t>(texture_index),
                        .sampler_index = static_cast<uint32_t>(*sampler_index)};

                    textureManager.use_slot(
                        texture_index,
                        create_font_texture.build(
                            device, commandPool, GRAPHICS_AND_PRESENT,
                            mcs::vulkan::memory::create_texture::image_info{
                                .extent = {.width = static_cast<uint32_t>(texWidth),
                                           .height = static_cast<uint32_t>(texHeight)},
                                .pixels =
                                    std::span<const uint8_t>{
                                        image.data(), static_cast<uint64_t>(imageSize)},
                                .mipLevels = 1}),
                        imageInfo.image_path);
                    using ManagerType = std::remove_cvref_t<decltype(textureManager)>;
                    using auto_free_slot_type = typename ManagerType::auto_free_slot_type;
                    return auto_free_slot_type{textureManager, texture_index};
                }
                throw std::logic_error{"couldn't find a suitable texture_index"};
            }
            throw std::logic_error{"check image_variant"};
        },
        *loader);

    using factoryType = decltype(factory);
    auto font_factoryPtr = std::make_unique<factoryType>(std::move(factory));
    auto &fontFactory = *font_factoryPtr.get();
    auto fontSelect = font::GenFontSelector{&fontFactory, "zh-CN"}.load(
        font::font_register::makeFontInfos(
            loader, //
            {
                font::font_registration{
                    .font_path = FONT_PATH_0,
                    .json_path = JSON_PATH_0,
                    .type = font::FontType::eMSDF,
                    .texture_info = {.bind = {}, //NOTE: lazy_bind
                                     .image_variant =
                                         font::texture_info::stbi_image_type{
                                             .image_format = STBI_rgb_alpha,
                                             .image_path = TEXTURE_PATH_0}}},
                font::font_registration{
                    .font_path = FONT_PATH_1,
                    .json_path = JSON_PATH_1,
                    .type = font::FontType::eMSDF,
                    .texture_info = {.bind = {},
                                     .image_variant =
                                         font::texture_info::stbi_image_type{
                                             .image_format = STBI_rgb_alpha,
                                             .image_path = TEXTURE_PATH_1}}},
                font::font_registration{
                    .font_path = FONT_PATH_2,
                    .json_path = JSON_PATH_2,
                    .type = font::FontType::eBITMAP,
                    .texture_info = {.bind = {},
                                     .image_variant =
                                         font::texture_info::stbi_image_type{
                                             .image_format = STBI_rgb_alpha,
                                             .image_path = TEXTURE_PATH_2}}},
                font::font_registration{
                    .font_path = FONT_PATH_3,
                    .json_path = JSON_PATH_3,
                    .type = font::FontType::eMSDF,
                    .texture_info = {.bind = {},
                                     .image_variant =
                                         font::texture_info::stbi_image_type{
                                             .image_format = STBI_rgb_alpha,
                                             .image_path = TEXTURE_PATH_3}}},
                font::font_registration{
                    .font_path = FONT_PATH_4,
                    .json_path = JSON_PATH_4,
                    .type = font::FontType::eMSDF,
                    .texture_info = {.bind = {},
                                     .image_variant =
                                         font::texture_info::stbi_image_type{
                                             .image_format = STBI_rgb_alpha,
                                             .image_path = TEXTURE_PATH_4}}},
            }));
    fontSelect.initNotdefFont();
    assert(fontSelect.notdefFont() != nullptr);

    return make_aggregate<"fontCtx", "loader", "fontFactory", "fontSelect">(
        std::move(loaderPtr), std::move(font_factoryPtr), std::move(fontSelect));
}
using HardwareCtx = decltype(init());
using DescriptorCtx = decltype(descriptorInit(std::declval<HardwareCtx &>()));
using FontCtx =
    decltype(initFont(std::declval<HardwareCtx &>(), std::declval<DescriptorCtx &>()));
using FontSelect = std::remove_cvref_t<decltype(std::declval<FontCtx>().fontSelect)>;

// ═══════════════════════════════════════════════════════════════════════
// 真实纹理：九宫格 / alpha mask / mipmap
//
//   和前两轮的“程序化近似”不同，这里真的建了纹理：
//     · 真的像素数据（3x3 九宫格图、径向 alpha 遮罩、高频棋盘）
//     · 真的 mip 链（create_texture 会用 vkCmdBlitImage 逐级生成）
//     · 真的注册进 bindless 数组 + 更新描述符
//   着色器只负责 UV 分区 / 采样 / textureLod。
// ═══════════════════════════════════════════════════════════════════════
// 特殊管线池：混合模式与模板裁剪都要真的换 VkPipeline
//   0=UI(over) 1=add 2=multiply 3=erase 4=maskA 5=maskB 6=clipped
struct ui_pipeline_ctx
{
    // 存 Pipeline（RAII 对象）而不是裸 VkPipeline：临时对象析构会销毁管线
    std::array<std::optional<mcs::vulkan::Pipeline>, 9> pipes{};
    // 必须在 device 销毁前显式清理：函数内 static 的析构晚于 device，
    // 否则退出时会报 N 条 "VkPipeline ... has not been destroyed"
    void reset() noexcept
    {
        for (auto &p : pipes)
            p.reset();
    }
};
// ═══════════════════════════════════════════════════════════════════════
// 文本 Unicode：一行 = HarfBuzz shape 出来的字形序列（视觉顺序）
//   drawFrame 是无捕获 lambda，数据只能走文件作用域静态
// ═══════════════════════════════════════════════════════════════════════
struct text_glyph
{
    glm::vec4 uv;    // uv_bounds
    glm::vec4 plane; // plane_bounds（1em 归一化）
    float advance{0.0F};
    uint32_t tex{0};
    uint32_t samp{0};
    uint32_t fontType{0}; // 0=MSDF/SDF（median3），1=bitmap（直接采 RGBA）
};
struct text_line
{
    std::vector<text_glyph> glyphs;
    float width{0.0F}; // 1em 单位
};
inline std::unordered_map<char32_t, text_glyph> &uiAsciiGlyphs() noexcept
{
    static std::unordered_map<char32_t, text_glyph> glyphs;
    return glyphs;
}

inline std::array<text_line, 4> &uiUnicodeLines() noexcept
{
    static std::array<text_line, 4> lines;
    return lines;
}

// ═══════════════════════════════════════════════════════════════════════
// path 输入接口：PathId + 注册表
//   消费者（even-odd 填充 / path 描边）只拿 PathId，不再内联构造轮廓；
//   注册表每帧 reset()，PathId 在同一帧内稳定。
// ═══════════════════════════════════════════════════════════════════════
namespace ui_path
{
    struct contour
    {
        std::vector<glm::vec2> pts; // 已展平（像素坐标）
        bool closed{true};
    };
    using path = std::vector<contour>;

    struct registry
    {
        std::vector<path> paths;
        uint32_t generation{0}; // 每次 reset 自增：PathId 只在同一代内有效
        void reset() noexcept
        {
            paths.clear();
            ++generation;
        }
        uint32_t add(path p)
        {
            paths.push_back(std::move(p));
            return static_cast<uint32_t>(paths.size() - 1);
        }
        [[nodiscard]] bool valid(uint32_t id) const noexcept
        {
            return id < paths.size(); // bounds check：坏命令不能越界
        }
        [[nodiscard]] const path &get(uint32_t id) const
        {
            static const path empty; // 越界兜底，返回空 path 而不是 UB
            return valid(id) ? paths[id] : empty;
        }
        [[nodiscard]] static constexpr uint32_t invalid() noexcept
        {
            return 0xFFFFFFFFu;
        }
    };
    inline registry &get() noexcept
    {
        static registry r;
        return r;
    }
} // namespace ui_path

// 几何路径用的顶点缓冲（CPU 侧 tessellation 结果）
ui_pipeline_ctx &uiPipelines()
{
    static ui_pipeline_ctx ctx;
    return ctx;
}

struct ui_texture_ctx
{
    uint32_t nineSlice{};
    uint32_t alphaMask{};
    uint32_t checker{};
    uint32_t sampler{};
    VkExtent2D nineSliceExtent{};
    VkExtent2D alphaMaskExtent{};
};
ui_texture_ctx &uiTextures()
{
    static ui_texture_ctx ctx;
    return ctx;
}

constexpr auto initUiTextures(const LogicalDevice &device, const CommandPool &pool,
                              const Queue &queue, uint32_t sampler_index,
                              auto &textureManager, auto &descriptorSetManager)
{
    auto &ctx = uiTextures();
    ctx.sampler = sampler_index;

    // 通用的“像素 → 纹理”构造：RGBA8、可采样、可生成 mip
    auto create_ui_texture = [&](VkExtent2D extent, uint32_t mipLevels,
                                 const std::vector<uint8_t> &pixels) -> uint32_t {
        mcs::vulkan::memory::create_texture builder{
            [](mcs::vulkan::memory::create_texture::image_info imageInfo)
                -> mcs::vulkan::memory::create_texture::create_info {
                return {.imageType = VK_IMAGE_TYPE_2D,
                        .format = VK_FORMAT_R8G8B8A8_UNORM,
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

        auto resource = builder.build(
            device, pool, queue,
            mcs::vulkan::memory::create_texture::image_info{
                .extent = extent,
                .pixels = std::span<const uint8_t>{pixels.data(), pixels.size()},
                .mipLevels = mipLevels});

        uint32_t texture_index = 0;
        for (auto i : textureManager.view_free_indexes())
        {
            texture_index = static_cast<uint32_t>(i);
            break;
        }
        textureManager.use_slot(texture_index, std::move(resource), "ui.texture");
        return texture_index;
    };

    // ── 1. 九宫格源图：96x96，3x3 分区 + 6px 边框 + 1px 分区线 ──────────
    {
        constexpr uint32_t S = 96;
        constexpr uint32_t CELL = S / 3;
        constexpr uint32_t BORDER = 6;
        static const uint8_t palette[9][3] = {
            {230, 60, 60},  {60, 200, 80},   {60, 110, 230},
            {230, 200, 60}, {240, 240, 240}, {60, 210, 210},
            {200, 80, 210}, {240, 140, 60},  {140, 140, 140}};
        std::vector<uint8_t> px(static_cast<std::size_t>(S) * S * 4u);
        for (uint32_t y = 0; y < S; ++y)
            for (uint32_t x = 0; x < S; ++x)
            {
                const uint32_t cell = (y / CELL) * 3u + (x / CELL);
                uint8_t r = palette[cell][0];
                uint8_t g = palette[cell][1];
                uint8_t b = palette[cell][2];
                // 外框 + 分区线：这两类像素在拉伸时必须保持原宽度
                const bool frame =
                    x < BORDER || y < BORDER || x + BORDER >= S || y + BORDER >= S;
                const bool line = (x % CELL) < 1u || (y % CELL) < 1u;
                if (frame)
                {
                    r = static_cast<uint8_t>(r / 3);
                    g = static_cast<uint8_t>(g / 3);
                    b = static_cast<uint8_t>(b / 3);
                }
                else if (line)
                {
                    r = static_cast<uint8_t>(r / 2 + 60);
                    g = static_cast<uint8_t>(g / 2 + 60);
                    b = static_cast<uint8_t>(b / 2 + 60);
                }
                const std::size_t o = (static_cast<std::size_t>(y) * S + x) * 4u;
                px[o + 0] = r;
                px[o + 1] = g;
                px[o + 2] = b;
                px[o + 3] = 255;
            }
        ctx.nineSliceExtent = {S, S};
        ctx.nineSlice = create_ui_texture({S, S}, 1, px);
    }

    // ── 2. alpha 遮罩：128x128，径向渐变 + 一圈环 ──────────────────────
    {
        constexpr uint32_t S = 128;
        std::vector<uint8_t> px(static_cast<std::size_t>(S) * S * 4u);
        for (uint32_t y = 0; y < S; ++y)
            for (uint32_t x = 0; x < S; ++x)
            {
                const float fx = (static_cast<float>(x) + 0.5F) / S * 2.0F - 1.0F;
                const float fy = (static_cast<float>(y) + 0.5F) / S * 2.0F - 1.0F;
                const float r = std::sqrt(fx * fx + fy * fy);
                const float disc = glm::clamp((0.85F - r) / 0.12F, 0.0F, 1.0F);
                const float ring =
                    glm::clamp(1.0F - std::abs(r - 0.62F) / 0.05F, 0.0F, 1.0F);
                const float a = std::max(disc, ring * 0.9F);
                const std::size_t o = (static_cast<std::size_t>(y) * S + x) * 4u;
                px[o + 0] = 255;
                px[o + 1] = 255;
                px[o + 2] = 255;
                px[o + 3] = static_cast<uint8_t>(a * 255.0F);
            }
        ctx.alphaMaskExtent = {S, S};
        ctx.alphaMask = create_ui_texture({S, S}, 1, px);
    }

    // ── 3. 棋盘：256x256、8px 格，带完整 mip 链（8 级）─────────────────
    {
        constexpr uint32_t S = 256;
        constexpr uint32_t CELL = 8;
        constexpr uint32_t MIPS = 8; // 256 -> 1
        std::vector<uint8_t> px(static_cast<std::size_t>(S) * S * 4u);
        for (uint32_t y = 0; y < S; ++y)
            for (uint32_t x = 0; x < S; ++x)
            {
                const bool on = ((x / CELL) + (y / CELL)) % 2u == 0u;
                const uint8_t v = on ? 235 : 40;
                const std::size_t o = (static_cast<std::size_t>(y) * S + x) * 4u;
                px[o + 0] = v;
                px[o + 1] = v;
                px[o + 2] = v;
                px[o + 3] = 255;
            }
        ctx.checker = create_ui_texture({S, S}, MIPS, px);
    }

    descriptorSetManager.update_texture(textureManager.view_used_indexes());
}

struct mesh_manager
{
    std::vector<Vertex> allVertices;
    std::vector<uint32_t> allIndices;
    std::unordered_map<std::string, mesh_data> meshMap;

    constexpr void addMesh(const std::string &name, const std::span<const Vertex> &verts,
                           const std::span<const uint32_t> &indices)
    {
        assert(not name.empty());
        assert(not verts.empty());
        assert(not indices.empty());
        assert(not meshMap.contains(name));
        uint32_t vOff = static_cast<uint32_t>(allVertices.size());
        uint32_t iOff = static_cast<uint32_t>(allIndices.size());
        allVertices.insert(allVertices.end(), verts.begin(), verts.end());
        allIndices.insert(allIndices.end(), indices.begin(), indices.end());
        meshMap[name] = {static_cast<uint32_t>(verts.size()), vOff, iOff,
                         static_cast<uint32_t>(indices.size())};
    }
};

// NOTE: 考虑放到一个命名空间或等区域统一处理
constexpr std::array<Vertex, 4> quadVerts = {
    Vertex{{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f}}, // 左上
    Vertex{{0.5f, -0.5f, 0.0f}, {1.0f, 0.0f}},  // 右上
    Vertex{{0.5f, 0.5f, 0.0f}, {1.0f, 1.0f}},   // 右下
    Vertex{{-0.5f, 0.5f, 0.0f}, {0.0f, 1.0f}}   // 左下
};
constexpr auto quadIdx = std::array<uint32_t, 6>{0, 1, 2, 0, 2, 3};
constexpr auto initMeshManager()
{
    mesh_manager m;
    m.addMesh("quad", std::span{quadVerts}, std::span{quadIdx});
    return m;
}

constexpr auto run_text_pipeline(auto &fontSelect, const auto *rawText,
                                 std::string_view langBcp47, bool ltr = true)
{
    constexpr auto ltr_value = 0; // NOLINT
    constexpr auto rtl_value = 1; // NOLINT
    namespace font_ns = mcs::vulkan::font;

    auto norm = font_ns::utf8proc::normalize(rawText);
    const std::vector<uint32_t> &codepoints = norm.codepoints;
    font_ns::utf8proc::print_normalized(norm, rawText);

    auto analyze_result = font_ns::bidi::analyze(codepoints, ltr ? ltr_value : rtl_value);
    font_ns::bidi::print_bidi_result(codepoints, analyze_result);

    auto test_text_runs = font_ns::assign_fonts(analyze_result, fontSelect);
    font_ns::print_text_runs(test_text_runs);

    auto test_shape_result = font_ns::harfbuzz::shape(
        analyze_result.mirrored_codepoints, test_text_runs, fontSelect.notdefFont());
    font_ns::harfbuzz::print_shape_result(analyze_result.mirrored_codepoints,
                                          test_shape_result);

    auto break_result = font_ns::libunibreak::analyze_line_breaks(
        analyze_result.mirrored_codepoints, langBcp47);
    font_ns::libunibreak::print_break_result(break_result,
                                             analyze_result.mirrored_codepoints);
    return make_aggregate<"text_pipeline_result", "codepoints", "shape_result",
                          "break_result">(std::move(analyze_result.mirrored_codepoints),
                                          std::move(test_shape_result),
                                          std::move(break_result));
}
constexpr auto initPipeline(auto &hardwareCtx, auto &descriptorCtx)
{
    auto &physical_device = *hardwareCtx.physicalDevice.get();
    auto &device = *hardwareCtx.device.get();

    auto &swapchainBuild = hardwareCtx.swapchainBuild;
    auto &swapchain = hardwareCtx.swapchain;

    auto &descriptorSetLayout = *descriptorCtx.descriptorSetLayout.get();

    auto depthResourcesBuild = mcs::vulkan::memory::build_simple_resource(
        {.imageType = VK_IMAGE_TYPE_2D,
         .format = mcs::vulkan::memory::create_resources::findSupportedFormat(
             physical_device,
             std::array<VkFormat, 3>{VkFormat::VK_FORMAT_D32_SFLOAT_S8_UINT,
                                     VkFormat::VK_FORMAT_D24_UNORM_S8_UINT,
                                     VkFormat::VK_FORMAT_D32_SFLOAT},
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
            // 纯深度格式没有模板分量，aspectMask 只能是 DEPTH
            const bool stencil = imageCreateInfo.format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
                                 imageCreateInfo.format == VK_FORMAT_D24_UNORM_S8_UINT ||
                                 imageCreateInfo.format == VK_FORMAT_D16_UNORM_S8_UINT ||
                                 imageCreateInfo.format == VK_FORMAT_S8_UINT;
            return {.image = image,
                    .viewType = VK_IMAGE_VIEW_TYPE_2D,
                    .format = imageCreateInfo.format,
                    .subresourceRange = {.aspectMask = static_cast<VkImageAspectFlags>(
                                             stencil ? (VK_IMAGE_ASPECT_DEPTH_BIT |
                                                        VK_IMAGE_ASPECT_STENCIL_BIT)
                                                     : VK_IMAGE_ASPECT_DEPTH_BIT),
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
    auto swapchainAttachments =
        make_aggregate<"swapchainAttachments", "depthResourcesBuild", "depthResource",
                       "msaaResourcesBuild", "msaaResource">(
            std::move(depthResourcesBuild), std::move(depthResource),
            std::move(msaaResourcesBuild), std::move(msaaResource));

    // ========== 拾取资源 =========
    auto pickResourcesBuild = mcs::vulkan::memory::build_simple_resource(
        {.imageType = VK_IMAGE_TYPE_2D,
         .format = VK_FORMAT_R32G32B32A32_UINT, // 128位：外键 + primitive_id + hover_fn
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
    std::array<mcs::vulkan::memory::auto_map_buffer, MAX_FRAMES_IN_FLIGHT> pickingFrames;
    for (auto &pf : pickingFrames)
    {
        constexpr auto BUFFER_SIZE = sizeof(picking_result); // 16B = 一个 4×u32 texel
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
    struct pick_mouse
    {
        glm::ivec2 pos{0, 0};
        bool valid = false;
    };
    pick_mouse pickMouse;
    auto pickingAttachments =
        make_aggregate<"pickingAttachments", "pickResourcesBuild", "pickResource",
                       "resolveResourcesBuild", "resolveResource", "pickingFrames",
                       "pickMouse">(
            std::move(pickResourcesBuild), std::move(pickResource),
            std::move(resolveResourcesBuild), std::move(resolveResource),
            std::move(pickingFrames), std::move(pickMouse));

    auto pipelineLayout =
        create_pipeline_layout{}
            .setCreateInfo(
                {.setLayouts = {*descriptorSetLayout},
                 // diff: [test_dod8] 推送常量传说地址更快，没有绑定的开销？
                 .pushConstantRanges = {{.stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                                                       VK_SHADER_STAGE_FRAGMENT_BIT,
                                         .offset = 0,
                                         .size = sizeof(PushData)}}})
            .build(device);

    using stage_info = create_graphics_pipeline::stage_info;
    // 定义两个颜色附件格式
    std::array<VkFormat, 2> mainColorFormats = {
        swapchainBuild.refImageFormat(), // location 0 (swapchain)
        VK_FORMAT_R32G32B32A32_UINT      // location 1 (picking)
    };

    /*
    管线名	                深度测试	    深度写入	混合(att0)	        典型用途
    pipelineOpaque3D	    ON (LESS)	    ON	        OFF	            不透明3D物体
    pipelineTransparent3D	ON (LESS)	    OFF	        ON	            半透明3D物体
    pipelineOpaqueUI	    OFF	            OFF	        OFF	            不透明UI（矩形、文字）
    pipelineTransparentUI	OFF	            OFF	        ON	            半透明UI（毛玻璃、淡入淡出）
    */
    auto makePipeline = [&](const VkBool32 depthTest, const VkBool32 depthWrite,
                            VkBool32 blendEnable) {
        return create_graphics_pipeline{}
            .setCreateInfo(
                {.pNext = make_pNext(structure_chain<VkPipelineRenderingCreateInfo>{
                     {//diff: [test_indirectdraw_no_pick] start
                      .colorAttachmentCount =
                          static_cast<uint32_t>(mainColorFormats.size()),
                      .pColorAttachmentFormats = mainColorFormats.data(),
                      //diff: [test_indirectdraw_no_pick] end
                      .depthAttachmentFormat = depthFormat_ref,
                      // 深度格式带模板分量时，管线必须同时声明 stencil 格式，
                      // 否则 VkRenderingInfo::pStencilAttachment 会触发 VUID-08917
                      .stencilAttachmentFormat = depthResourcesBuild.hasStencilComponent()
                                                     ? depthFormat_ref
                                                     : VK_FORMAT_UNDEFINED}}),
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
                 // 深度/模板状态: 片段着色器之后的固定操作. 控制丢弃片元
                 .depthStencilState = {.depthTestEnable = depthTest,
                                       .depthWriteEnable = depthWrite,
                                       .depthCompareOp = VK_COMPARE_OP_LESS,
                                       .depthBoundsTestEnable = VK_FALSE,
                                       .stencilTestEnable = VK_FALSE,
                                       .front = {},
                                       .back = {},
                                       .minDepthBounds = 0.0F,
                                       .maxDepthBounds = 1.0F},
                 // 颜色混合: 片段着色器之后的固定操作,控制画面颜色
                 .colorBlendState =
                     {.logicOpEnable = VK_FALSE,
                      .logicOp = VkLogicOp::VK_LOGIC_OP_COPY,
                      .attachments =
                          {
                              //附件0
                              {
                                  //diff: [test_dod12] start: 附件 0 – 开启混合，支持 MSDF 与普通纹理混合
                                  .blendEnable = blendEnable,
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
                                  // location 1 (拾取：4 通道全写，zw = primitive_id + hover_fn)
                                  .blendEnable = VK_FALSE,
                                  .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                                    VK_COLOR_COMPONENT_G_BIT |
                                                    VK_COLOR_COMPONENT_B_BIT |
                                                    VK_COLOR_COMPONENT_A_BIT,
                              },
                              //diff: [test_indirectdraw_no_pick] end
                          }},
                 .dynamicState = {.dynamicStates =
                                      {
                                          VK_DYNAMIC_STATE_VIEWPORT,
                                          VK_DYNAMIC_STATE_SCISSOR,
                                          //NOTE: 多管线替代下面的动态状态了
                                          //   VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
                                          //   VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
                                      }},
                 .layout = *pipelineLayout})
            .build(device);
    };
    // 离屏层管线：只有一张颜色附件（离屏图像），无深度/无拾取
    auto makeLayerPipeline = [&]() {
        std::array<VkFormat, 1> layerColorFormats = {swapchainBuild.refImageFormat()};
        return create_graphics_pipeline{}
            .setCreateInfo(
                {.pNext = make_pNext(structure_chain<VkPipelineRenderingCreateInfo>{
                     {.colorAttachmentCount = 1,
                      .pColorAttachmentFormats = layerColorFormats.data(),
                      .depthAttachmentFormat = VK_FORMAT_UNDEFINED,
                      .stencilAttachmentFormat = VK_FORMAT_UNDEFINED}}),
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
                 .multisampleState = {.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
                                      .sampleShadingEnable = VK_FALSE,
                                      .minSampleShading = 1.0F,
                                      .sampleMask = {},
                                      .alphaToCoverageEnable = VK_FALSE,
                                      .alphaToOneEnable = VK_FALSE},
                 .depthStencilState = {.depthTestEnable = VK_FALSE,
                                       .depthWriteEnable = VK_FALSE,
                                       .depthCompareOp = VK_COMPARE_OP_ALWAYS,
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
                 .dynamicState = {.dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                                                    VK_DYNAMIC_STATE_SCISSOR}},
                 .layout = *pipelineLayout})
            .build(device);
    };
    auto pipelineUILayer = makeLayerPipeline();

    // 通用管线工厂：混合 / 模板 / 颜色写掩码 都可配
    auto makePipelineEx = [&](VkPipelineColorBlendAttachmentState blendAttachment,
                              VkBool32 stencilEnable, VkStencilOpState stencilOp,
                              VkColorComponentFlags colorWrite,
                              const char *vsEntry = "main") {
        const VkPipelineColorBlendAttachmentState pickAttachmentState{
            .blendEnable = VK_FALSE,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
        return create_graphics_pipeline{}
            .setCreateInfo(
                {.pNext = make_pNext(structure_chain<VkPipelineRenderingCreateInfo>{
                     {.colorAttachmentCount =
                          static_cast<uint32_t>(mainColorFormats.size()),
                      .pColorAttachmentFormats = mainColorFormats.data(),
                      .depthAttachmentFormat = depthFormat_ref,
                      .stencilAttachmentFormat = depthResourcesBuild.hasStencilComponent()
                                                     ? depthFormat_ref
                                                     : VK_FORMAT_UNDEFINED}}),
                 .stages = create_graphics_pipeline::makeStages(
                     stage_info{.stage = VK_SHADER_STAGE_VERTEX_BIT,
                                .filePath = VERT_SHADER_PATH,
                                .pName = vsEntry},
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
                 .multisampleState = {.rasterizationSamples =
                                          physical_device.getMaxUsableSampleCount(),
                                      .sampleShadingEnable = VK_FALSE,
                                      .minSampleShading = 1.0F,
                                      .sampleMask = {},
                                      .alphaToCoverageEnable = VK_FALSE,
                                      .alphaToOneEnable = VK_FALSE},
                 .depthStencilState = {.depthTestEnable = VK_FALSE,
                                       .depthWriteEnable = VK_FALSE,
                                       .depthCompareOp = VK_COMPARE_OP_ALWAYS,
                                       .depthBoundsTestEnable = VK_FALSE,
                                       .stencilTestEnable = stencilEnable,
                                       .front = stencilOp,
                                       .back = stencilOp,
                                       .minDepthBounds = 0.0F,
                                       .maxDepthBounds = 1.0F},
                 .colorBlendState = {.logicOpEnable = VK_FALSE,
                                     .logicOp = VkLogicOp::VK_LOGIC_OP_COPY,
                                     .attachments = {blendAttachment,
                                                     pickAttachmentState}},
                 .dynamicState = {.dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                                                    VK_DYNAMIC_STATE_SCISSOR,
                                                    VK_DYNAMIC_STATE_STENCIL_REFERENCE}},
                 .layout = *pipelineLayout})
            .build(device);
    };

    const auto blendOver = VkPipelineColorBlendAttachmentState{
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    auto blendWith = [&](VkBlendFactor src, VkBlendFactor dst) {
        auto a = blendOver;
        a.srcColorBlendFactor = src;
        a.dstColorBlendFactor = dst;
        return a;
    };
    const auto noColorWrite =
        VkPipelineColorBlendAttachmentState{.blendEnable = VK_FALSE, .colorWriteMask = 0};
    const auto stencilAlwaysIncr =
        VkStencilOpState{.failOp = VK_STENCIL_OP_KEEP,
                         .passOp = VK_STENCIL_OP_INCREMENT_AND_CLAMP,
                         .depthFailOp = VK_STENCIL_OP_KEEP,
                         .compareOp = VK_COMPARE_OP_ALWAYS,
                         .compareMask = 0xFF,
                         .writeMask = 0xFF,
                         .reference = 0};
    auto stencilEqualIncr = stencilAlwaysIncr;
    stencilEqualIncr.compareOp = VK_COMPARE_OP_EQUAL;
    auto stencilEqualKeep = stencilAlwaysIncr;
    stencilEqualKeep.compareOp = VK_COMPARE_OP_EQUAL;
    stencilEqualKeep.passOp = VK_STENCIL_OP_KEEP;
    stencilEqualKeep.writeMask = 0x00;

    uiPipelines().pipes[1] =
        makePipelineEx(blendWith(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE), VK_FALSE,
                       stencilAlwaysIncr, 0);
    uiPipelines().pipes[2] =
        makePipelineEx(blendWith(VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_ZERO),
                       VK_FALSE, stencilAlwaysIncr, 0);
    uiPipelines().pipes[3] = makePipelineEx(
        blendWith(VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA), VK_FALSE,
        stencilAlwaysIncr, 0);
    uiPipelines().pipes[4] = makePipelineEx(noColorWrite, VK_TRUE, stencilAlwaysIncr, 0);
    uiPipelines().pipes[5] = makePipelineEx(noColorWrite, VK_TRUE, stencilEqualIncr, 0);
    // 几何管线：模板 INVERT（even-odd 奇偶计数）；多边形顶点由推送常量传入
    {
        auto stencilInvert = stencilAlwaysIncr;
        stencilInvert.passOp = VK_STENCIL_OP_INVERT;
        uiPipelines().pipes[7] = makePipelineEx(noColorWrite, VK_TRUE, stencilInvert, 0);
        // 8 = 路径描边：写颜色、不测模板（覆盖率由片元解析算）
        uiPipelines().pipes[8] =
            makePipelineEx(blendOver, VK_FALSE, stencilAlwaysIncr, 0);
    }

    uiPipelines().pipes[6] =
        makePipelineEx(blendOver, VK_TRUE, stencilEqualKeep,
                       VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT);

    // 创建4个管线
    auto pipelineOpaque3D = makePipeline(VK_TRUE, VK_TRUE, VK_FALSE);
    auto pipelineTransparent3D = makePipeline(VK_TRUE, VK_FALSE, VK_TRUE);
    auto pipelineOpaqueUI = makePipeline(VK_FALSE, VK_FALSE, VK_FALSE);
    auto pipelineTransparentUI = makePipeline(VK_FALSE, VK_FALSE, VK_TRUE);
    return make_aggregate<"mainPipelineCtx", "pipelineLayout", "pipelineOpaque3D",
                          "pipelineTransparent3D", "pipelineOpaqueUI",
                          "pipelineTransparentUI", "pipelineUILayer",
                          "swapchainAttachments", "pickingAttachments">(
        std::move(pipelineLayout), std::move(pipelineOpaque3D),
        std::move(pipelineTransparent3D), std::move(pipelineOpaqueUI),
        std::move(pipelineTransparentUI), std::move(pipelineUILayer),
        std::move(swapchainAttachments), std::move(pickingAttachments));
}

//diff: [test_dod22] start
constexpr auto &hoverPool() noexcept
{
    static shader_data::hover_pool hoverPool{};
    return hoverPool;
}
struct render_context
{
    shader_data::DrawRecorder &drawRecorder;
    glfw_input &input;
    FrameClock &clock;
    FontSelect &fontSelect;
};

//diff: [test_dod24.cpp] start
namespace ui_new
{
    template <typename T>
    concept constraints = requires(const T &t) {
        { t.isTight() } noexcept -> std::same_as<bool>;
        { t.isNormalized() } noexcept -> std::same_as<bool>;
    };

    // NOLINTBEGIN
    // clang-format off
    struct Size
    {
        double width, height;
    };
    struct Offset
    {
        double x, y;
    };
    enum class Axis               : std::uint8_t { horizontal, vertical };
    enum class FlexFit            : std::uint8_t { tight, loose };
    enum class MainAxisSize       : std::uint8_t { min, max };
    enum class MainAxisAlignment  : std::uint8_t { start, end, center, spaceBetween, spaceAround, spaceEvenly };
    enum class CrossAxisAlignment : std::uint8_t { start, end, center, stretch, baseline };
    enum class TextDirection      : std::uint8_t { rtl, ltr };
    enum class VerticalDirection  : std::uint8_t { up, down };
    enum class TextBaseline       : std::uint8_t { alphabetic, ideographic };

    struct EdgeInsetsGeometry
    {
        static constexpr auto inf = std::numeric_limits<double>::infinity();
        double left,right,top,bottom,start,end;
        
        static constexpr auto fromLRSETB(double left,double right,double start,double end,double top,double bottom) noexcept
        { return EdgeInsetsGeometry{.left=left,.right=right,.top=top,.bottom=bottom,.start=start,.end=end}; }
        static constexpr auto fromLTRB(double left,double top,double right,double bottom) noexcept 
        { return EdgeInsetsGeometry{.left=left,.right=right,.top=top,.bottom=bottom,.start=0,.end=0}; }
        static constexpr auto fromSTEB(double start,double top,double end,double bottom)noexcept
        { return EdgeInsetsGeometry{.left=0,.right=0,.top=top,.bottom=bottom,.start=start,.end=end}; }

        // api
        [[nodiscard]] constexpr auto horizontal()const noexcept { return left + right + start + end; }
        [[nodiscard]] constexpr auto vertical()const noexcept { return top + bottom; }
        // The total offset in the given direction.
        [[nodiscard]] constexpr double along(Axis axis) const noexcept
        {
            switch (axis) 
            {
                case  Axis::horizontal : return horizontal();
                case  Axis::vertical   : return vertical();
            };
        }
        [[nodiscard]] constexpr Size collapsedSize() const noexcept { return {horizontal(), vertical()}; }
        [[nodiscard]] constexpr bool isNonNegative() const noexcept { return left >= 0 && right >= 0 && top >= 0 && bottom >= 0 && start>=0 && end >= 0; }
    };
    struct EdgeInsets : EdgeInsetsGeometry
    {
        static constexpr auto all(double value) noexcept
        {
            return EdgeInsetsGeometry::fromLTRB(value,value,value,value);
        }
    };


    struct Alignment
    {
        double x, y;
        struct _Initializer{float x, y; constexpr operator Alignment() const noexcept { return {x, y};}};
        constexpr static _Initializer topLeft{-1, -1}, topCenter{0, -1}, topRight{1, -1};
        constexpr static _Initializer centerLeft{-1, 0}, center{0, 0}, centerRight{1, 0};
        constexpr static _Initializer bottomLeft{-1, 1}, bottomCenter{0, 1}, bottomRight{1, 1};
    };


    class BoxConstraints
    {
    public:
        static constexpr auto inf = std::numeric_limits<double>::infinity();
        constexpr explicit BoxConstraints(double minWidth = 0, double maxWidth = inf,
                                        double minHeight = 0, double maxHeight = inf)
            : minWidth_{minWidth}, maxWidth_{maxWidth}, minHeight_{minHeight},
            maxHeight_{maxHeight}
        {
        }

        [[nodiscard]] constexpr double minWidth() const noexcept { return minWidth_; }
        [[nodiscard]] constexpr double maxWidth() const noexcept { return maxWidth_; }
        [[nodiscard]] constexpr double minHeight() const noexcept { return minHeight_; }
        [[nodiscard]] constexpr double maxHeight() const noexcept { return maxHeight_; }

        // constraints  concept
        [[nodiscard]] constexpr bool hasTightWidth() const noexcept { return minWidth_ >= maxWidth_; }
        [[nodiscard]] constexpr bool hasTightHeight() const noexcept { return minHeight_ >= maxHeight_; }
        [[nodiscard]] constexpr bool isTight() const noexcept { return hasTightWidth() && hasTightHeight(); }
        [[nodiscard]] constexpr bool isNormalized() const noexcept
        { return minWidth_ >= 0.0 && minWidth_ <= maxWidth_ && minHeight_ >= 0.0 && minHeight_ <= maxHeight_; }
        [[nodiscard]] constexpr bool hasBoundedWidth() const noexcept { return maxWidth_ < inf; }
        [[nodiscard]] constexpr bool hasBoundedHeight() const noexcept { return maxHeight_ < inf; }
        [[nodiscard]] constexpr bool hasInfiniteWidth() const noexcept { return minWidth_ >= inf; }
        [[nodiscard]] constexpr bool hasInfiniteHeight() const noexcept { return minHeight_ >= inf; }

        [[nodiscard]] constexpr bool isSatisfiedBy(Size size) const noexcept 
        {     
            return  (minWidth_ <= size.width) && (size.width <= maxWidth_) &&
                    (minHeight_ <= size.height) && (size.height <= maxHeight_);
        }
        [[nodiscard]] constexpr auto normalize()const noexcept  
        {
            if (isNormalized())
                return *this;
            auto minWidth = minWidth_ >= 0.0 ? minWidth_ : 0.0;
            auto minHeight = minHeight_ >= 0.0 ? minHeight_ : 0.0;
            return BoxConstraints(
                    minWidth,
                    minWidth > maxWidth_ ? minWidth : maxWidth_,
                    minHeight,
                    minHeight > maxHeight_ ? minHeight : maxHeight_);
        }

        // 语义相关 API
        static constexpr auto tight(Size size) noexcept
        { return BoxConstraints{size.width,size.width,size.height,size.height}; }
        struct Optional {std::optional<double> width,height;};
        // null → 宽松;非 null 值 -> 紧
        static constexpr auto tightFor(Optional op) noexcept
        {
            auto [width,height] = op;
            return BoxConstraints{width.value_or(0.0),width.value_or(inf),height.value_or(0.0),height.value_or(inf)};
        }
        struct Finite {double width = inf, height = inf;};
        // infinity → 宽松; 有限数值 → 紧
        static constexpr auto tightForFinite(Finite op) noexcept
        {
            auto [width,height] = op;
            return BoxConstraints{width != inf ? width : 0.0,width != inf ? width : inf,height != inf ? height : 0.0,height != inf ? height : inf};
        }

        static constexpr auto loose(Size size) noexcept
        { return BoxConstraints{0,size.width,0,size.height}; }
        // expand 默认是无限大紧约束; 不设置维度，则该维度无限大
        static constexpr auto expand(Optional op = {}) noexcept
        {
            auto [width,height] = op;
            return BoxConstraints{width.value_or(inf),width.value_or(inf),height.value_or(inf),height.value_or(inf)};
        }

        // 成员API
        struct OptionalConstraints {std::optional<double> minWidth,maxWidth,minHeight,maxHeight;};
        // 无维度定义，则复制当前维度
        constexpr auto copyWith(OptionalConstraints c) const noexcept
        {
            auto [minWidth,maxWidth,minHeight,maxHeight] =c;
            return BoxConstraints{minWidth.value_or(minWidth_),maxWidth.value_or(maxWidth_),minHeight.value_or(minHeight_),maxHeight.value_or(maxHeight_)};
        }
        // 根据给定的边距（内边距）缩小当前的盒子约束，返回一个更紧的新约束，表示在扣除这些边距后，子组件可用的空间范围
        constexpr auto deflate(EdgeInsetsGeometry edges) const noexcept
        {
            double horizontal = edges.horizontal();
            double vertical = edges.vertical();
            double deflatedMinWidth = std::max(0.0, minWidth_ - horizontal);
            double deflatedMinHeight =  std::max(0.0, minHeight_ - vertical);
            return BoxConstraints{
            deflatedMinWidth,
            std::max(deflatedMinWidth, maxWidth_ - horizontal),
            deflatedMinHeight,
            std::max(deflatedMinHeight, maxHeight_ - vertical)};
        }
        constexpr auto enforce(BoxConstraints constraints) const noexcept
        {
            return BoxConstraints{
                std::clamp(minWidth_, constraints.minWidth_, constraints.maxWidth_),
                std::clamp(maxWidth_, constraints.minWidth_, constraints.maxWidth_),
                std::clamp(minHeight_, constraints.minHeight_, constraints.maxHeight_),
                std::clamp(maxHeight_, constraints.minHeight_, constraints.maxHeight_)};
        }
        constexpr auto tighten(Optional op) const noexcept
        {
            auto [ width,height] = op;
            return BoxConstraints{
                !width.has_value() ? minWidth_ : std::clamp(*width, minWidth_, maxWidth_),
                !width.has_value() ? maxWidth_ : std::clamp(*width, minWidth_, maxWidth_),
                !height.has_value() ? minHeight_ : std::clamp(*height, minHeight_, maxHeight_),
                !height.has_value() ? maxHeight_ : std::clamp(*height, minHeight_, maxHeight_)};
        }

        [[nodiscard]] constexpr auto loosen() const noexcept
        { return BoxConstraints{0.0, maxWidth_, 0.0, maxHeight_}; }

        [[nodiscard]] constexpr auto flipped() const noexcept
        { return BoxConstraints{minHeight_, maxHeight_, minWidth_, maxWidth_}; }
        
        constexpr auto constrainWidth(double width = inf ) const noexcept { return std::clamp(width, minWidth_, maxWidth_); } 
        constexpr auto constrainHeight(double height = inf ) const noexcept { return std::clamp(height, minHeight_, maxHeight_); } 
        constexpr Size constrain(Size size) const noexcept { return {constrainWidth(size.width), constrainHeight(size.height)}; }
        constexpr Size constrainDimensions(double width, double height) const noexcept { return constrain(Size{width,height}); }

        // The biggest size that satisfies the constraints.
        constexpr Size biggest() const noexcept { return Size(constrainWidth(), constrainHeight());}
        // The smallest size that satisfies the constraints.
        constexpr Size smallest() const noexcept { return Size(constrainWidth(0), constrainHeight(0));}

        [[nodiscard]] constexpr bool operator==(const BoxConstraints &) const noexcept = default;

        [[nodiscard]] std::string toString() const
        {
            auto fmt1 = [](double v) {
                if (std::isinf(v)) return std::string("inf");
                return std::to_string(v);
            };
            return "BoxConstraints(w=[" + fmt1(minWidth_) + "," + fmt1(maxWidth_) + "]"
                ", h=[" + fmt1(minHeight_) + "," + fmt1(maxHeight_) + "])";
        }
    private:
        double minWidth_;
        double maxWidth_;
        double minHeight_;
        double maxHeight_;
    };

    static_assert(constraints<BoxConstraints>, "should satisfy");


    // 在 Flutter 中，尺寸（w, h）先计算，位置（x, y）后设置
    struct ScreenWidget;
    struct Widget;
    struct RenderObject
    {   
    virtual constexpr void render(ScreenWidget*screen,Widget* owner,render_context &context) = 0;
        virtual ~RenderObject() = default;
    };

    // 自上而下传递约束（Constraints）
    // 自下而上汇报尺寸（Size）
    struct Widget
    { 
        std::string key{};
        Size size{};
        Offset offset{};
        std::unique_ptr<RenderObject> renderObject;

        virtual constexpr void layout(BoxConstraints c) = 0;
        virtual constexpr void updateOffset(Offset offset) noexcept = 0;
        [[nodiscard]] virtual std::span<const std::unique_ptr<Widget>> children() const noexcept = 0;
        virtual ~Widget() = default;
        constexpr auto& Self() noexcept { return *this; }


        // 前序遍历：先自己，再按顺序遍历每个子节点
        template <typename Fn>
        constexpr void visit(Fn &&fn)
        {
            fn(*this);
            for (auto &child : children())
                if (child) child->visit(std::forward<Fn>(fn));
        }

        // 前序查找第一个满足谓词的节点，找不到返回 nullptr
        template <typename Pred>
        constexpr Widget *findIf(Pred &&pred)
        {
            if (pred(*this)) return this;
            for (auto &child : children())
                if (child)
                    if (auto *found = child->findIf(std::forward<Pred>(pred)))
                        return found;
            return nullptr;
        }

        // 按 key 查找
        [[nodiscard]] constexpr Widget *findByKey(std::string_view k)
        {
            return findIf([k](const Widget &w) { return w.key == k; });
        }

      protected:
        Widget() noexcept = default;
        constexpr explicit Widget(std::string k,
                                  std::unique_ptr<RenderObject> ro = nullptr) noexcept
            : key(std::move(k)), renderObject(std::move(ro)) {}
    };

    template <typename B>
    concept WidgetBuild = requires(B &&b) {
        static_cast<std::unique_ptr<Widget>>(std::forward<B>(b));
    };
    struct ScreenWidget
    {
        constexpr render_version_t::underlying_t renderVersion() const noexcept { return renderVersion_ + static_cast<uint8_t>(versionDirty_);}

        constexpr void makeLayoutDirty(uint32_t frame) noexcept
        { layoutDirty_[frame] = true; makeVersionDirty(); }
        constexpr void clearLayoutDirty(uint32_t frame) noexcept
        { layoutDirty_[frame] = false; }
        constexpr bool isLayoutDirty(uint32_t frame) const noexcept
        { return layoutDirty_[frame]; }

        constexpr void makeRenderDirty(uint32_t frame) noexcept
        { renderDirty_[frame] = true; makeVersionDirty();}
        constexpr void clearRenderDirty(uint32_t frame) noexcept
        { renderDirty_[frame] = false; }
        constexpr bool isRenderDirty(uint32_t frame) const noexcept
        { return renderDirty_[frame]; }
        constexpr bool hasDirty(uint32_t frame) const noexcept {return isLayoutDirty(frame) || isRenderDirty(frame); }

        constexpr void layout()
        {
            if(auto *ptr = root_.get())
            {
                ptr->layout(BoxConstraints{0,size_.width,0,size_.height});
                ptr->updateOffset({0,0});
            }
        }
        constexpr void updateSize(double width, double height) noexcept
        {
            if (size_.width == width && size_.height == height)
                return;
            size_ = {width, height};
            for (std::size_t f = 0; f < MAX_FRAMES_IN_FLIGHT; ++f)
                makeLayoutDirty(f);
        }

        void render(render_context &context) 
        {
            Widget* root = root_.get();
            if (root == nullptr) return;

            const auto dfs = [&, screen = this](this auto &self, Widget *node) {
                if (!node) return;

                // 命中就提交渲染；没命中就"路过"这个节点
                if (node->renderObject)
                    node->renderObject->render(screen, node, context);

                // 无论有没有 renderObject，都继续下探子节点
                for (auto& child : node->children())
                    self(child.get());
            };
            dfs(root);
        }
        void recorderCmd(uint32_t frame, render_context &context)
        {
            if(isLayoutDirty(frame))
            {
                layout();
                render(context);


                clearLayoutDirty(frame);
                clearRenderDirty(frame);
                toNextRenderVersion();
                return;
            }
            if(isRenderDirty(frame))
            {
                render(context); //NOTE: 布局不变，仅仅是数据渲染更新
                clearRenderDirty(frame);
                toNextRenderVersion();
                return;
            }
        }


        ScreenWidget()=default;
        constexpr ScreenWidget(Size size,std::unique_ptr<Widget> root) noexcept : size_(size) , root_(std::move(root)) { layoutDirty_.fill(true);renderDirty_.fill(true);}
        template <WidgetBuild Build>
        constexpr ScreenWidget(Size size,Build &&b) noexcept : ScreenWidget(size,static_cast<std::unique_ptr<Widget>>(std::forward<Build>(b))) {}
        
        constexpr auto& size(Size size) noexcept 
        {
            size_ = size;
            return *this;
        }
        constexpr auto& size(double width, double height) noexcept { return size({width,height});}
        
        template <WidgetBuild Build>
        constexpr auto &root(Build &&b) noexcept 
        { 
            root_ = static_cast<std::unique_ptr<Widget>>(std::forward<Build>(b));
            return *this;
        }

        // root 访问器：给外部拿根节点（需要时可加 const 重载）
        [[nodiscard]] constexpr Widget *root() noexcept { return root_.get(); }
        // 从根节点开始转发
        template <typename Fn>
        constexpr void visit(Fn &&fn) { if (root_) root_->visit(std::forward<Fn>(fn)); }

        template <typename Pred>
        constexpr Widget *findIf(Pred &&pred)
        { return root_ ? root_->findIf(std::forward<Pred>(pred)) : nullptr; }

        [[nodiscard]] constexpr Widget *findByKey(std::string_view k)
        { return root_ ? root_->findByKey(k) : nullptr; }

    private:
        std::array<bool, MAX_FRAMES_IN_FLIGHT> layoutDirty_;   // 默认全 false，构造时填 true
        std::array<bool, MAX_FRAMES_IN_FLIGHT> renderDirty_;
        Size size_;
        std::unique_ptr<Widget> root_;
        render_version_t renderVersion_{};   // 默认 0
        bool versionDirty_ = false;
        constexpr void makeVersionDirty() noexcept { versionDirty_ = true; }
        constexpr void clearVersionDirty() noexcept { versionDirty_ = false; }
        constexpr void toNextRenderVersion() noexcept { renderVersion_ = render_version_t{renderVersion()};clearVersionDirty(); }
    };

    // clang-format on
    // NOLINTEND

    /*
    在 Flutter 中，只要父级和子级没有同时强制“唯一宽高”（即紧约束），它们确实会互相“协商”并影响对方的大小。

    但是，这个“互相”必须严格遵守 Flutter 布局的第一大铁律：

    约束（Constraints）向下传递，大小（Size）向上传递，位置由父级决定。

    这不是“双向同时谈判”，而是严格的两步流水线。为了让你看透本质，我们分三种情况拆解：

    1. 子级能改变父级大小吗？（向上影响）—— 能！
    条件：父级没有固定宽高（即父级对子级传递的是松约束，比如 0 ≤ w ≤ ∞）。

    过程：

    父级说：“孩子，你想多大就多大（松约束）。”
    子级说：“那我算出我要 200 宽。”
    子级向上返回 size = 200。
    父级一看：“哦，孩子是 200，我没有固定宽高，那我就把自己也设为 200 宽吧！”
    典型例子：Container 没有设宽高，但它的孩子是一个 Container(width: 200)，那么父级会被撑成 200 宽。

    2. 父级能改变子级大小吗？（向下影响）—— 能！
    条件：父级有固定宽高或紧约束（比如父级宽 100）。

    过程：

    父级说：“你必须正好是 100 宽（紧约束）。”
    子级就算想当 200，也得乖乖算出 100，向上返回 size = 100。
    典型例子：父级是 SizedBox(width: 100)，孩子是 Container(width: 200)，最终孩子被强制变成 100（除非溢出报错）。

    3. 为什么不会变成“死循环”？—— 因为你说的“不强制唯一的宽高”是关键
    你敏锐地注意到了“如果不强制唯一的宽高”这个前提。如果父子都没有强制唯一宽高（即都是松约束），流程是这样的：

    父级先出牌：父级把当前的约束（比如 0-∞）传给子级（方向：向下）。

    子级算结果：子级根据自己的 child 算出自己的 size。

    父级拿结果：父级读取子级的 size，据此算出自己的 size（方向：向上）。

    结束！这里绝对不存在“循环回推”。

    为什么不会无限循环？
    因为约束（Constraints）和大小（Size）是两种完全不同的对象。父级在把约束给子级之前，就已经确定了自己的约束。父级不会因为子级返回了尺寸，就回过头去“修改”刚才传给子级的约束。它是先定规矩，再量尺寸。

    ===============================================================================================
    多子带来唯一的新概念：“分配剩余空间”
    虽然汇总公式变了，但多子布局确实引入了一个单子布局里不存在的问题，那就是 flex（弹性）：

    在单子（Container）里：父级给子级传什么约束，子级就按什么算，简单直接。

    在多子（Row）里：父级先把总宽度拿出来，减去固定宽度孩子的尺寸，剩下的“剩余空间”怎么分？

    这就引出了 RenderFlex 的“两步走”算法：

    第一步（无约束测量）：先问所有非 Expanded 的孩子要多大尺寸（layout 一次）。
    第二步（分配约束）：算出剩余空间，除以 Expanded 的 flex 权重，算出每个弹性孩子的确切新约束，再让他们重新 layout 一次（第二次测量）。


    ===============================================================================================
    // 只有一个孩子
    child.layout(constraints);
    size = child.size;  // ★ 公式：父尺寸 = 唯一孩子的尺寸（完全复制）

    ----------------------------------------------------------------------
    // 多个孩子
    double totalWidth = 0;
    double maxHeight = 0;

    // 遍历所有孩子
    for (child in children) {
    child.layout(constraints, parentUsesSize: true);
    totalWidth += child.size.width;    // 累加宽度（主轴）
    maxHeight = max(maxHeight, child.size.height); // 取最大高度（纵轴）
    }

    // ★ 公式：父尺寸 = 子尺寸的累加和 与 最大值 的结合
    size = Size(totalWidth, maxHeight);

    ----------------------------------------------------------------------
    // 源码位置：RenderPadding.performLayout (极度简化)
    @override
    void performLayout() {
    // 1. 先扣除 margin，把缩水后的约束传给 child
    final BoxConstraints innerConstraints = constraints.deflate(_padding); 
    child.layout(innerConstraints, parentUsesSize: true);
    
    // 2. ★ 核心配合公式：最终尺寸 = 孩子的尺寸 + margin 的宽高
    size = constraints.constrain(Size(
        child.size.width + _padding.horizontal,   // 宽 = 孩子宽 + 左margin + 右margin
        child.size.height + _padding.vertical,    // 高 = 孩子高 + 上margin + 下margin
    ));
    }

    -------------------------------------------------------------------------------------
    // 伪代码：单子父级的 performLayout
    void performLayout() {
    // 1. 把约束缩水后传给唯一的孩子
    child.layout(innerConstraints);
    
    // 2. 算出自己的总尺寸（孩子尺寸 + 边距）
    size = ...;
    
    // 3. ★ 直接计算这一个孩子的 x,y（极其简单）
    child.parentData.offset = Offset(padding.left, padding.top);
    }
    -------------------------------------------------------------------------------------
    // 伪代码：Row 父级的 performLayout（极度简化）
    double currentX = 0;
    for (child in children) {
    // ★ 这才是多子布局真正的核心难点：
    // 根据 MainAxisAlignment（居中、靠左、SpaceBetween）计算出当前孩子的起始 x
    // 如果是 spaceBetween，还要先算出总空白间隔，再逐个累加
    
    child.parentData.offset = Offset(currentX, 0); // 决定 Y 轴对齐（顶部、居中、拉伸）
    currentX += child.size.width; // 累加，指向下一个兄弟的起始位置
    }
    */
    struct ContainerWidget : Widget
    {

        ContainerWidget() = default; // NOLINTBEGIN
        constexpr ContainerWidget(std::string key, std::unique_ptr<RenderObject> ro,
                                  std::optional<BoxConstraints> constraints,
                                  std::optional<Alignment> alignment,
                                  std::optional<EdgeInsetsGeometry> margin,
                                  std::optional<EdgeInsetsGeometry> border,
                                  std::optional<EdgeInsetsGeometry> padding,
                                  std::unique_ptr<Widget> child) noexcept
            : Widget(std::move(key), std::move(ro)), constraints_{constraints},
              alignment_{alignment}, margin_{margin}, border_{border}, padding_{padding},
              child_{std::move(child)} // NOLINTEND
        {
            assert(not constraints_.has_value() || (*constraints_).isNormalized());
            assert(not margin_.has_value() || (*margin_).isNonNegative());
            assert(not border_.has_value() || (*border_).isNonNegative());
            assert(not padding_.has_value() || (*padding_).isNonNegative());
        }

        [[nodiscard]] constexpr auto additionalWidth() const noexcept
        {
            return margin_.value_or({}).horizontal() + border_.value_or({}).horizontal() +
                   padding_.value_or({}).horizontal();
        }
        [[nodiscard]] constexpr auto additionalHeight() const noexcept
        {
            return margin_.value_or({}).vertical() + border_.value_or({}).vertical() +
                   padding_.value_or({}).vertical();
        }
        constexpr void layout(BoxConstraints c) override
        {
            const auto margin = margin_.value_or({});
            const auto border = border_.value_or({});
            const auto padding = padding_.value_or({});

            // ── 1. 先扣 margin，得到 ConstrainedBox 的可用区
            BoxConstraints afterMargin = c.deflate(margin);

            // ── 2. 应用用户 constraints_（若没有就是继承父约束）
            BoxConstraints inner = constraints_.has_value()
                                       ? (*constraints_).enforce(afterMargin)
                                       : afterMargin;

            // ── 3. 再扣 padding + border，得到 child 真正的可用区
            const EdgeInsetsGeometry inset = EdgeInsetsGeometry::fromLTRB(
                border.left + padding.left, border.top + padding.top,
                border.right + padding.right, border.bottom + padding.bottom);
            BoxConstraints childConstraints = inner.deflate(inset);

            if (child_)
            {
                Widget &childRef = *child_;

                // ── 4. 有 alignment → 给孩子松约束，让它算自己的尺寸
                //      无 alignment → 直接把 childConstraints 丢给它
                if (alignment_.has_value())
                {
                    childRef.layout(BoxConstraints{0.0, childConstraints.maxWidth(), 0.0,
                                                   childConstraints.maxHeight()});
                }
                else
                {
                    childRef.layout(childConstraints);
                }

                // ── 5. 内容区尺寸：
                //      有 alignment：Align 展开到内容区的 max（tight 时即 tight 值）
                //      无 alignment：就是孩子尺寸
                Size contentSize =
                    alignment_.has_value() ? childConstraints.biggest() : childRef.size;

                // Padding(padding).size = inner.constrain(child.size + padding + border)
                Size paddingBoxSize = inner.constrain(Size{
                    .width = contentSize.width + inset.horizontal(),
                    .height = contentSize.height + inset.vertical(),
                });

                // ── 6. 父自身 = 内容 + padding + border + margin
                // Padding(margin).size = c.constrain(paddingBox.size + margin)
                Self().size = c.constrain(Size{
                    .width = paddingBoxSize.width + margin.horizontal(),
                    .height = paddingBoxSize.height + margin.vertical(),
                });
            }
            else
            {
                // 无 child：只有 margin 计入自身尺寸，padding/border 不计
                BoxConstraints effective = constraints_.has_value()
                                               ? (*constraints_).enforce(afterMargin)
                                               : afterMargin;

                double w = 0.0, h = 0.0;
                if (constraints_.has_value() && constraints_->isTight())
                {
                    w = effective.maxWidth();
                    h = effective.maxHeight();
                }
                else
                {
                    w = effective.hasBoundedWidth() ? effective.maxWidth() : 0.0;
                    h = effective.hasBoundedHeight() ? effective.maxHeight() : 0.0;
                }
                Self().size = {
                    .width = w + margin.horizontal(),
                    .height = h + margin.vertical(),
                };
            }
        }

        constexpr void updateOffset(Offset offset) noexcept override
        {
            Self().offset = offset;
            if (!child_)
                return;

            Widget &childRef = *child_;

            const auto margin = margin_.value_or({});
            const auto border = border_.value_or({});
            const auto padding = padding_.value_or({});

            // 孩子左边缘 = 父偏移 + margin + border + padding
            double dx = offset.x + margin.left + border.left + padding.left;
            double dy = offset.y + margin.top + border.top + padding.top;

            // 有 alignment：再叠加内容区内的对齐偏移
            if (alignment_.has_value())
            {
                // 内容区尺寸 = 父自身 - padding - border - margin
                // 但更简单：直接用 layout 里算过的内容区尺寸反推
                // 这里从对齐公式出发，需要 free = contentSize - child.size
                // 而 contentSize = Self().size - inset - margin
                double contentW = Self().size.width - margin.horizontal() -
                                  border.horizontal() - padding.horizontal();
                double contentH = Self().size.height - margin.vertical() -
                                  border.vertical() - padding.vertical();

                double freeW = contentW - childRef.size.width;
                double freeH = contentH - childRef.size.height;

                dx += freeW * (alignment_->x + 1.0) * 0.5;
                dy += freeH * (alignment_->y + 1.0) * 0.5;
            }

            childRef.updateOffset({dx, dy});
        }
        [[nodiscard]] constexpr std::span<const std::unique_ptr<Widget>> children()
            const noexcept override
        {
            if (!child_)
                return {};
            return std::span<const std::unique_ptr<Widget>>{&child_, 1};
        }

      private:
        std::optional<BoxConstraints> constraints_;
        std::optional<Alignment> alignment_;
        std::optional<EdgeInsetsGeometry> margin_;
        std::optional<EdgeInsetsGeometry> border_;
        std::optional<EdgeInsetsGeometry> padding_;
        std::unique_ptr<Widget> child_;
    };
    struct ContainerBuild
    {
        constexpr explicit ContainerBuild(std::string key) noexcept : key_(std::move(key))
        {
        }
        constexpr explicit operator std::unique_ptr<Widget>()
        {
            return std::make_unique<ContainerWidget>(
                std::move(key_), std::move(renderObject_),
                width_.has_value() || height_.has_value()
                    ? std::optional<BoxConstraints>{BoxConstraints::tightFor(
                          {.width = width_, .height = height_})}
                    : std::nullopt,
                alignment_, margin_, border_, padding_, std::move(child_));
        }
        constexpr auto &key(std::string key) noexcept
        {
            key_ = std::move(key);
            return *this;
        }
        constexpr auto &renderObject(std::unique_ptr<RenderObject> ro) noexcept
        {
            renderObject_ = std::move(ro);
            return *this;
        }
        constexpr auto &width(double width) noexcept
        {
            width_ = width;
            return *this;
        }
        constexpr auto &height(double height) noexcept
        {
            height_ = height;
            return *this;
        }
        constexpr auto &margin(EdgeInsetsGeometry margin) noexcept
        {
            margin_ = margin;
            return *this;
        }
        constexpr auto &alignment(Alignment alignment) noexcept
        {
            alignment_ = alignment;
            return *this;
        }
        constexpr auto &border(EdgeInsetsGeometry border) noexcept
        {
            border_ = border;
            return *this;
        }
        constexpr auto &padding(EdgeInsetsGeometry padding) noexcept
        {
            padding_ = padding;
            return *this;
        }
        constexpr auto &child(std::unique_ptr<Widget> child) noexcept
        {
            child_ = std::move(child);
            return *this;
        }
        constexpr auto &child(WidgetBuild auto &&b) noexcept
        {
            return child(
                static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
        }

      private:
        std::string key_;
        std::unique_ptr<RenderObject> renderObject_;
        std::optional<double> width_;
        std::optional<double> height_;
        std::optional<Alignment> alignment_;
        std::optional<EdgeInsetsGeometry> margin_;
        std::optional<EdgeInsetsGeometry> border_;
        std::optional<EdgeInsetsGeometry> padding_;
        std::unique_ptr<Widget> child_;
    };
    static constexpr auto Container(std::string key = {}) noexcept // NOLINT
    {
        return ContainerBuild{std::move(key)};
    }

    struct AlignWidget : Widget
    {
        // NOLINTBEGIN
        AlignWidget(std::string key, std::unique_ptr<RenderObject> ro,
                    Alignment alignment, std::optional<double> widthFactor,
                    std::optional<double> heightFactor,
                    std::unique_ptr<Widget> child) noexcept // NOLINTEND
            : Widget(std::move(key), std::move(ro)), alignment_{alignment},
              widthFactor_{widthFactor}, heightFactor_{heightFactor},
              child_{std::move(child)}
        {
            assert(not widthFactor_.has_value() || *widthFactor_ >= 0);
            assert(not heightFactor_.has_value() || *heightFactor_ >= 0);
        }
        void layout(BoxConstraints c) override
        {
            const BoxConstraints constraints = c.normalize();

            if (child_)
            {
                Widget &childRef = *child_;

                // Align 给孩子松约束：最小值为 0，最大值保持不变。
                const BoxConstraints childConstraints = constraints.loosen();

                childRef.layout(childConstraints);

                const Size childSize = childRef.size;

                const double width = widthFactor_.has_value()
                                         ? childSize.width * (*widthFactor_)
                                         : constraints.maxWidth();

                const double height = heightFactor_.has_value()
                                          ? childSize.height * (*heightFactor_)
                                          : constraints.maxHeight();

                Self().size = constraints.constrain(Size{width, height});
            }
            else
            {
                const double width = widthFactor_.has_value()
                                         ? constraints.minWidth() * (*widthFactor_)
                                         : constraints.maxWidth();

                const double height = heightFactor_.has_value()
                                          ? constraints.minHeight() * (*heightFactor_)
                                          : constraints.maxHeight();

                Self().size = constraints.constrain(Size{width, height});
            }
        }

        void updateOffset(Offset offset) noexcept override
        {
            Self().offset = offset;

            if (!child_)
                return;

            Widget &childRef = *child_;

            double freeWidth = Self().size.width - childRef.size.width;
            double freeHeight = Self().size.height - childRef.size.height;

            // Alignment:
            // (-1, -1) => 左上
            // ( 0,  0) => 居中
            // ( 1,  1) => 右下
            double dx = freeWidth * (alignment_.x + 1.0) * 0.5;
            double dy = freeHeight * (alignment_.y + 1.0) * 0.5;

            childRef.updateOffset(Offset{.x = offset.x + dx, .y = offset.y + dy});
        }

        [[nodiscard]] constexpr std::span<const std::unique_ptr<Widget>> children()
            const noexcept override
        {
            if (!child_)
                return {};
            return std::span<const std::unique_ptr<Widget>>{&child_, 1};
        }

      private:
        Alignment alignment_;
        std::optional<double> widthFactor_;
        std::optional<double> heightFactor_;
        std::unique_ptr<Widget> child_;
    };
    struct AlignBuild
    {
        explicit AlignBuild(std::string key) noexcept : key_(std::move(key)) {}
        explicit operator std::unique_ptr<Widget>()
        {
            return std::make_unique<AlignWidget>(
                std::move(key_), std::move(renderObject_), alignment_, widthFactor_,
                heightFactor_, std::move(child_));
        }
        constexpr auto &key(std::string key) noexcept
        {
            key_ = std::move(key);
            return *this;
        }
        constexpr auto &renderObject(std::unique_ptr<RenderObject> ro) noexcept
        {
            renderObject_ = std::move(ro);
            return *this;
        }
        constexpr AlignBuild &alignment(Alignment alignment) noexcept
        {
            alignment_ = alignment;
            return *this;
        }
        constexpr auto &widthFactor(double widthFactor) noexcept
        {
            widthFactor_ = widthFactor;
            return *this;
        }
        constexpr auto &heightFactor(double heightFactor) noexcept
        {
            heightFactor_ = heightFactor;
            return *this;
        }
        constexpr auto &child(std::unique_ptr<Widget> child) noexcept
        {
            child_ = std::move(child);
            return *this;
        }
        constexpr auto &child(WidgetBuild auto &&b) noexcept
        {
            return child(
                static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
        }

      private:
        std::string key_;
        std::unique_ptr<RenderObject> renderObject_;
        Alignment alignment_ = Alignment::center;
        std::optional<double> widthFactor_;
        std::optional<double> heightFactor_;
        std::unique_ptr<Widget> child_;
    };
    static constexpr auto Align(std::string key = {}) noexcept // NOLINT
    {
        return AlignBuild{std::move(key)};
    }

    struct CenterWidget : AlignWidget
    {
    };
    struct CenterBuild : AlignBuild
    {
        using AlignBuild::AlignBuild;
        constexpr AlignBuild &alignment(Alignment alignment) noexcept = delete;
    };
    static constexpr auto Center(std::string key = {}) noexcept // NOLINT
    {
        return CenterBuild{std::move(key)};
    }

    struct SizedBoxWidget : Widget
    {
        // NOLINTBEGIN
        constexpr SizedBoxWidget(std::string key, std::unique_ptr<RenderObject> ro,
                                 std::optional<double> width,
                                 std::optional<double> height,
                                 std::unique_ptr<Widget> child) noexcept
            : Widget(std::move(key), std::move(ro)), width_{width}, height_{height},
              child_{std::move(child)} // NOLINTEND
        {
            assert(not width_.has_value() || *width_ >= 0);
            assert(not height_.has_value() || *height_ >= 0);
        }
        constexpr void layout(BoxConstraints c) override
        {
            // Flutter: RenderConstrainedBox(additionalConstraints: tightFor(width, height))
            BoxConstraints effective =
                BoxConstraints::tightFor({.width = width_, .height = height_}).enforce(c);

            if (child_)
            {
                Widget &childRef = *child_;
                childRef.layout(effective);
                // 自身尺寸 = 孩子的尺寸
                Self().size = childRef.size;
            }
            else
            {
                // Flutter: size = effective.constrain(Size.zero)
                Self().size = effective.smallest();
            }
        }
        constexpr void updateOffset(Offset offset) noexcept override
        {
            Self().offset = offset;
            if (child_)
            {
                // SizedBox 无 margin/padding，孩子与自身同原点
                Widget &childRef = *child_;
                childRef.updateOffset(offset);
            }
        }
        [[nodiscard]] constexpr std::span<const std::unique_ptr<Widget>> children()
            const noexcept override
        {
            if (!child_)
                return {};
            return std::span<const std::unique_ptr<Widget>>{&child_, 1};
        }

      private:
        std::optional<double> width_;
        std::optional<double> height_;
        std::unique_ptr<Widget> child_;
    };
    struct SizedBoxBuild
    {
        explicit SizedBoxBuild(std::string key) noexcept : key_(std::move(key)) {}

        explicit operator std::unique_ptr<Widget>()
        {
            return std::make_unique<SizedBoxWidget>(std::move(key_),
                                                    std::move(renderObject_), width_,
                                                    height_, std::move(child_));
        }

        constexpr auto &key(std::string key) noexcept
        {
            key_ = std::move(key);
            return *this;
        }
        constexpr auto &renderObject(std::unique_ptr<RenderObject> ro) noexcept
        {
            renderObject_ = std::move(ro);
            return *this;
        }
        constexpr auto &width(double width) noexcept
        {
            width_ = width;
            return *this;
        }
        constexpr auto &height(double height) noexcept
        {
            height_ = height;
            return *this;
        }
        constexpr auto &child(std::unique_ptr<Widget> child) noexcept
        {
            child_ = std::move(child);
            return *this;
        }
        constexpr auto &child(WidgetBuild auto &&b) noexcept
        {
            return child(
                static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
        }

      private:
        std::string key_;
        std::unique_ptr<RenderObject> renderObject_;
        std::optional<double> width_;
        std::optional<double> height_;
        std::unique_ptr<Widget> child_;
    };
    static constexpr auto SizedBox(std::string key = {}) noexcept // NOLINT
    {
        return SizedBoxBuild{std::move(key)};
    }

    struct PaddingWidget : Widget
    {
        constexpr PaddingWidget(std::string key, std::unique_ptr<RenderObject> ro,
                                EdgeInsetsGeometry padding,
                                std::unique_ptr<Widget> child) noexcept
            : Widget(std::move(key), std::move(ro)), padding_{padding},
              child_{std::move(child)}
        {
            assert(padding_.isNonNegative());
        }

        constexpr void layout(BoxConstraints c) override
        {
            if (child_)
            {
                Widget &childRef = *child_;

                // 1. 缩水约束：c.deflate(padding) 给孩子
                childRef.layout(c.deflate(padding_));

                // 2. 自身尺寸 = 孩子尺寸 + padding，再被父约束夹住
                Self().size = c.constrain(Size{
                    .width = childRef.size.width + padding_.horizontal(),
                    .height = childRef.size.height + padding_.vertical(),
                });
            }
            else
            {
                Self().size = c.constrain(Size{
                    .width = padding_.horizontal(),
                    .height = padding_.vertical(),
                });
            }
        }

        constexpr void updateOffset(Offset offset) noexcept override
        {
            Self().offset = offset;
            if (child_)
            {
                Widget &childRef = *child_;
                // padding 就是孩子的内缩量（LTR 情况下 start=left, end=right）
                childRef.updateOffset(Offset{
                    .x = offset.x + padding_.left,
                    .y = offset.y + padding_.top,
                });
            }
        }

        [[nodiscard]] constexpr std::span<const std::unique_ptr<Widget>> children()
            const noexcept override
        {
            if (!child_)
                return {};
            return std::span<const std::unique_ptr<Widget>>{&child_, 1};
        }

      private:
        EdgeInsetsGeometry padding_;
        std::unique_ptr<Widget> child_;
    };
    struct PaddingBuild
    {
        constexpr explicit PaddingBuild(std::string key) noexcept : key_(std::move(key))
        {
        }

        explicit operator std::unique_ptr<Widget>()
        {
            if (not padding_.has_value())
                throw std::logic_error{"PaddingBuild required padding value"};
            return std::make_unique<PaddingWidget>(
                std::move(key_), std::move(renderObject_), *padding_, std::move(child_));
        }

        constexpr auto &key(std::string key) noexcept
        {
            key_ = std::move(key);
            return *this;
        }
        constexpr auto &renderObject(std::unique_ptr<RenderObject> ro) noexcept
        {
            renderObject_ = std::move(ro);
            return *this;
        }
        constexpr auto &padding(EdgeInsetsGeometry padding) noexcept
        {
            padding_ = padding;
            return *this;
        }
        constexpr auto &child(std::unique_ptr<Widget> child) noexcept
        {
            child_ = std::move(child);
            return *this;
        }
        constexpr auto &child(WidgetBuild auto &&b) noexcept
        {
            return child(
                static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
        }

      private:
        std::string key_;
        std::unique_ptr<RenderObject> renderObject_;
        std::optional<EdgeInsetsGeometry> padding_;
        std::unique_ptr<Widget> child_;
    };
    static constexpr auto Padding(std::string key = {}) noexcept // NOLINT
    {
        return PaddingBuild{std::move(key)};
    }

    struct ConstrainedBoxWidget : Widget
    {
        constexpr ConstrainedBoxWidget(std::string key, std::unique_ptr<RenderObject> ro,
                                       BoxConstraints constraints,
                                       std::unique_ptr<Widget> child) noexcept
            : Widget(std::move(key), std::move(ro)), constraints_{constraints},
              child_{std::move(child)}
        {
            assert(constraints_.isNormalized());
        }

        constexpr void layout(BoxConstraints c) override
        {
            // Flutter: RenderConstrainedBox(additionalConstraints: constraints_)
            BoxConstraints effective = constraints_.enforce(c);

            if (child_)
            {
                Widget &childRef = *child_;
                childRef.layout(effective);
                // 自身尺寸 = 孩子的尺寸
                Self().size = childRef.size;
            }
            else
            {
                // Flutter: size = effective.constrain(Size.zero)
                Self().size = effective.smallest();
            }
        }

        constexpr void updateOffset(Offset offset) noexcept override
        {
            Self().offset = offset;
            if (child_)
            {
                // 无 margin/padding，孩子与自身同原点
                child_->updateOffset(offset);
            }
        }

        [[nodiscard]] constexpr std::span<const std::unique_ptr<Widget>> children()
            const noexcept override
        {
            if (!child_)
                return {};
            return std::span<const std::unique_ptr<Widget>>{&child_, 1};
        }

      private:
        BoxConstraints constraints_; // The additional constraints to impose on the child.
        std::unique_ptr<Widget> child_;
    };
    struct ConstrainedBoxBuild
    {
        constexpr explicit ConstrainedBoxBuild(std::string key) noexcept
            : key_(std::move(key))
        {
        }

        explicit operator std::unique_ptr<Widget>()
        {
            if (not constraints_.has_value())
                throw std::logic_error{"ConstrainedBoxWidget required constraints value"};
            return std::make_unique<ConstrainedBoxWidget>(
                std::move(key_), std::move(renderObject_), *constraints_,
                std::move(child_));
        }

        constexpr auto &key(std::string key) noexcept
        {
            key_ = std::move(key);
            return *this;
        }
        constexpr auto &renderObject(std::unique_ptr<RenderObject> ro) noexcept
        {
            renderObject_ = std::move(ro);
            return *this;
        }
        constexpr auto &constraints(BoxConstraints constraints) noexcept
        {
            constraints_ = constraints;
            return *this;
        }
        constexpr auto &child(std::unique_ptr<Widget> child) noexcept
        {
            child_ = std::move(child);
            return *this;
        }
        constexpr auto &child(WidgetBuild auto &&b) noexcept
        {
            return child(
                static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
        }

      private:
        std::string key_;
        std::unique_ptr<RenderObject> renderObject_;
        std::optional<BoxConstraints> constraints_;
        std::unique_ptr<Widget> child_;
    };
    static constexpr auto ConstrainedBox(std::string key = {}) noexcept // NOLINT
    {
        return ConstrainedBoxBuild{std::move(key)};
    }

    enum class OverflowBoxFit : std::uint8_t
    {
        max,          // 自身 = 父约束允许的最大尺寸（默认）
        deferToChild, // 自身 = 父约束对 child.size 夹取后的尺寸
    };
    struct OverflowBoxWidget : Widget
    {
        // NOLINTBEGIN
        constexpr OverflowBoxWidget(std::string key, std::unique_ptr<RenderObject> ro,
                                    Alignment alignment, std::optional<double> minWidth,
                                    std::optional<double> maxWidth,
                                    std::optional<double> minHeight,
                                    std::optional<double> maxHeight, OverflowBoxFit fit,
                                    std::unique_ptr<Widget> child) noexcept // NOLINTEND
            : Widget(std::move(key), std::move(ro)), alignment_{alignment},
              minWidth_{minWidth}, maxWidth_{maxWidth}, minHeight_{minHeight},
              maxHeight_{maxHeight}, fit_{fit}, child_{std::move(child)}
        {
        }

        constexpr void layout(BoxConstraints c) override
        {
            // 1) 用「显式覆盖值 + 从父约束继承」组合出 child 的约束
            const BoxConstraints inner = innerConstraints(c);

            if (child_)
            {
                Widget &childRef = *child_;

                // 2) child 用 inner 布局（可能溢出父约束，这正是 OverflowBox 的用意）
                childRef.layout(inner);

                // 3) 自身尺寸：deferToChild → 夹取 child.size；max → 父允许的最大
                Self().size = (fit_ == OverflowBoxFit::deferToChild)
                                  ? c.constrain(childRef.size)
                                  : c.biggest();
            }
            else
            {
                // 无 child：仍按 fit 决定自身尺寸
                Self().size = (fit_ == OverflowBoxFit::deferToChild)
                                  ? c.constrain(inner.smallest())
                                  : c.biggest();
            }
        }

        constexpr void updateOffset(Offset offset) noexcept override
        {
            Self().offset = offset;
            if (!child_)
                return;

            Widget &childRef = *child_;

            // child 可以大于 / 小于自身，因此 freeW / freeH 可能为负（溢出时）
            const double freeW = Self().size.width - childRef.size.width;
            const double freeH = Self().size.height - childRef.size.height;

            const double dx = offset.x + freeW * (alignment_.x + 1.0) * 0.5;
            const double dy = offset.y + freeH * (alignment_.y + 1.0) * 0.5;

            childRef.updateOffset({dx, dy});
        }

        [[nodiscard]] constexpr std::span<const std::unique_ptr<Widget>> children()
            const noexcept override
        {
            if (!child_)
                return {};
            return std::span<const std::unique_ptr<Widget>>{&child_, 1};
        }

      private:
        [[nodiscard]] constexpr BoxConstraints innerConstraints(
            BoxConstraints c) const noexcept
        {
            // 未提供的分量 → 沿用父约束；提供的分量 → 覆盖
            return BoxConstraints{
                minWidth_.value_or(c.minWidth()),
                maxWidth_.value_or(c.maxWidth()),
                minHeight_.value_or(c.minHeight()),
                maxHeight_.value_or(c.maxHeight()),
            };
        }

        Alignment alignment_;
        std::optional<double> minWidth_;
        std::optional<double> maxWidth_;
        std::optional<double> minHeight_;
        std::optional<double> maxHeight_;
        OverflowBoxFit fit_;
        std::unique_ptr<Widget> child_;
    };
    struct OverflowBoxBuild
    {
        constexpr explicit OverflowBoxBuild(std::string key) noexcept
            : key_(std::move(key))
        {
        }

        explicit operator std::unique_ptr<Widget>()
        {
            return std::make_unique<OverflowBoxWidget>(
                std::move(key_), std::move(renderObject_), alignment_, minWidth_,
                maxWidth_, minHeight_, maxHeight_, fit_, std::move(child_));
        }

        constexpr auto &key(std::string key) noexcept
        {
            key_ = std::move(key);
            return *this;
        }
        constexpr auto &renderObject(std::unique_ptr<RenderObject> ro) noexcept
        {
            renderObject_ = std::move(ro);
            return *this;
        }
        constexpr auto &alignment(Alignment alignment) noexcept
        {
            alignment_ = alignment;
            return *this;
        }
        constexpr auto &minWidth(double v) noexcept
        {
            minWidth_ = v;
            return *this;
        }
        constexpr auto &maxWidth(double v) noexcept
        {
            maxWidth_ = v;
            return *this;
        }
        constexpr auto &minHeight(double v) noexcept
        {
            minHeight_ = v;
            return *this;
        }
        constexpr auto &maxHeight(double v) noexcept
        {
            maxHeight_ = v;
            return *this;
        }
        constexpr auto &fit(OverflowBoxFit f) noexcept
        {
            fit_ = f;
            return *this;
        }
        constexpr auto &child(std::unique_ptr<Widget> child) noexcept
        {
            child_ = std::move(child);
            return *this;
        }
        constexpr auto &child(WidgetBuild auto &&b) noexcept
        {
            return child(
                static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
        }

      private:
        std::string key_;
        std::unique_ptr<RenderObject> renderObject_;
        Alignment alignment_ = Alignment::center;
        std::optional<double> minWidth_;
        std::optional<double> maxWidth_;
        std::optional<double> minHeight_;
        std::optional<double> maxHeight_;
        OverflowBoxFit fit_ = OverflowBoxFit::max;
        std::unique_ptr<Widget> child_;
    };
    static constexpr auto OverflowBox(std::string key = {}) noexcept // NOLINT
    {
        return OverflowBoxBuild{std::move(key)};
    }

    // =========================================================================
    // UnconstrainedBox
    //
    // 语义（对应 Flutter 里的 ConstraintsTransformBox）：
    //   constrainedAxis == null
    //       两轴都放开  → child 约束 = [0, inf] x [0, inf]
    //   constrainedAxis == horizontal
    //       保留宽度约束，高度放开 → child 约束 = [c.minW, c.maxW] x [0, inf]
    //   constrainedAxis == vertical
    //       保留高度约束，宽度放开 → child 约束 = [0, inf] x [c.minH, c.maxH]
    //
    // 自身尺寸 = 父约束夹取 child.size；child 相对自身按 alignment 对齐。
    // 若 child 溢出，对应方向会出现负 free 值（此处不报警，交由上层或调试期处理）。
    // =========================================================================
    struct UnconstrainedBoxWidget : Widget
    {
        // NOLINTBEGIN
        constexpr UnconstrainedBoxWidget(
            std::string key, std::unique_ptr<RenderObject> ro, Alignment alignment,
            std::optional<Axis> constrainedAxis,
            std::unique_ptr<Widget> child) noexcept // NOLINTEND
            : Widget(std::move(key), std::move(ro)), alignment_{alignment},
              constrainedAxis_{constrainedAxis}, child_{std::move(child)}
        {
        }

        constexpr void layout(BoxConstraints c) override
        {
            const BoxConstraints inner = transform(c);

            if (child_)
            {
                Widget &childRef = *child_;

                // child 用放开后的约束布局
                childRef.layout(inner);

                // 自身 = 父约束夹取 child.size（可能因为 child 溢出而被夹住）
                Self().size = c.constrain(childRef.size);
            }
            else
            {
                // 无 child：取父约束下的最小值
                Self().size = c.smallest();
            }
        }

        constexpr void updateOffset(Offset offset) noexcept override
        {
            Self().offset = offset;
            if (!child_)
                return;

            Widget &childRef = *child_;

            // child 可能比自身大（溢出），freeW / freeH 允许为负
            const double freeW = Self().size.width - childRef.size.width;
            const double freeH = Self().size.height - childRef.size.height;

            const double dx = offset.x + freeW * (alignment_.x + 1.0) * 0.5;
            const double dy = offset.y + freeH * (alignment_.y + 1.0) * 0.5;

            childRef.updateOffset({dx, dy});
        }

        [[nodiscard]] constexpr std::span<const std::unique_ptr<Widget>> children()
            const noexcept override
        {
            if (!child_)
                return {};
            return std::span<const std::unique_ptr<Widget>>{&child_, 1};
        }

      private:
        // 三种约束变换（对应 Flutter 的 BoxConstraintsTransform）
        [[nodiscard]] constexpr BoxConstraints transform(BoxConstraints c) const noexcept
        {
            constexpr auto inf = std::numeric_limits<double>::infinity();

            if (!constrainedAxis_.has_value())
            {
                // unconstrained: 两轴都放开
                return BoxConstraints{0.0, inf, 0.0, inf};
            }

            if (*constrainedAxis_ == Axis::horizontal)
            {
                // heightUnconstrained: 保留宽度，高度放开
                return BoxConstraints{c.minWidth(), c.maxWidth(), 0.0, inf};
            }

            // Axis::vertical → widthUnconstrained: 保留高度，宽度放开
            return BoxConstraints{0.0, inf, c.minHeight(), c.maxHeight()};
        }

        Alignment alignment_;
        std::optional<Axis> constrainedAxis_;
        std::unique_ptr<Widget> child_;
    };
    struct UnconstrainedBoxBuild
    {
        constexpr explicit UnconstrainedBoxBuild(std::string key) noexcept
            : key_(std::move(key))
        {
        }

        explicit operator std::unique_ptr<Widget>()
        {
            return std::make_unique<UnconstrainedBoxWidget>(
                std::move(key_), std::move(renderObject_), alignment_, constrainedAxis_,
                std::move(child_));
        }

        constexpr auto &key(std::string key) noexcept
        {
            key_ = std::move(key);
            return *this;
        }

        constexpr auto &renderObject(std::unique_ptr<RenderObject> ro) noexcept
        {
            renderObject_ = std::move(ro);
            return *this;
        }

        constexpr auto &alignment(Alignment alignment) noexcept
        {
            alignment_ = alignment;
            return *this;
        }

        constexpr auto &constrainedAxis(Axis axis) noexcept
        {
            constrainedAxis_ = axis;
            return *this;
        }
        constexpr auto &clearConstrainedAxis() noexcept
        {
            constrainedAxis_.reset();
            return *this;
        }

        constexpr auto &child(std::unique_ptr<Widget> child) noexcept
        {
            child_ = std::move(child);
            return *this;
        }
        constexpr auto &child(WidgetBuild auto &&b) noexcept
        {
            return child(
                static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
        }

      private:
        std::string key_;
        std::unique_ptr<RenderObject> renderObject_;
        Alignment alignment_ = Alignment::center;
        std::optional<Axis> constrainedAxis_;
        std::unique_ptr<Widget> child_;
    };
    static constexpr auto UnconstrainedBox(std::string key = {}) noexcept // NOLINT
    {
        return UnconstrainedBoxBuild{std::move(key)};
    }

    struct LimitedBoxWidget : Widget
    {
        // NOLINTBEGIN
        constexpr LimitedBoxWidget(std::string key, std::unique_ptr<RenderObject> ro,
                                   double maxWidth, double maxHeight,
                                   std::unique_ptr<Widget> child) noexcept // NOLINTEND
            : Widget(std::move(key), std::move(ro)), maxWidth_{maxWidth},
              maxHeight_{maxHeight}, child_{std::move(child)}
        {
            assert(maxWidth_ >= 0.0);
            assert(maxHeight_ >= 0.0);
        }

        constexpr void layout(BoxConstraints c) override
        {
            const BoxConstraints inner = innerConstraints(c);

            if (child_)
            {
                Widget &childRef = *child_;
                childRef.layout(inner);
                Self().size = c.constrain(childRef.size);
            }
            else
            {
                Self().size = c.constrain(inner.smallest());
            }
        }

        constexpr void updateOffset(Offset offset) noexcept override
        {
            Self().offset = offset;
            if (child_)
                child_->updateOffset(offset);
        }

        [[nodiscard]] constexpr std::span<const std::unique_ptr<Widget>> children()
            const noexcept override
        {
            if (!child_)
                return {};
            return std::span<const std::unique_ptr<Widget>>{&child_, 1};
        }

      private:
        [[nodiscard]] constexpr BoxConstraints innerConstraints(
            BoxConstraints c) const noexcept
        {
            constexpr auto inf = std::numeric_limits<double>::infinity();

            return BoxConstraints{
                c.minWidth(),
                c.maxWidth() == inf ? maxWidth_ : c.maxWidth(),
                c.minHeight(),
                c.maxHeight() == inf ? maxHeight_ : c.maxHeight(),
            };
        }

        double maxWidth_;
        double maxHeight_;
        std::unique_ptr<Widget> child_;
    };
    struct LimitedBoxBuild
    {
        constexpr explicit LimitedBoxBuild(std::string key) noexcept
            : key_(std::move(key))
        {
        }

        explicit operator std::unique_ptr<Widget>()
        {
            return std::make_unique<LimitedBoxWidget>(std::move(key_),
                                                      std::move(renderObject_), maxWidth_,
                                                      maxHeight_, std::move(child_));
        }

        constexpr auto &key(std::string key) noexcept
        {
            key_ = std::move(key);
            return *this;
        }

        constexpr auto &renderObject(std::unique_ptr<RenderObject> ro) noexcept
        {
            renderObject_ = std::move(ro);
            return *this;
        }

        constexpr auto &maxWidth(double maxWidth) noexcept
        {
            maxWidth_ = maxWidth;
            return *this;
        }

        constexpr auto &maxHeight(double maxHeight) noexcept
        {
            maxHeight_ = maxHeight;
            return *this;
        }

        constexpr auto &child(std::unique_ptr<Widget> child) noexcept
        {
            child_ = std::move(child);
            return *this;
        }

        constexpr auto &child(WidgetBuild auto &&b) noexcept
        {
            return child(
                static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
        }

      private:
        static constexpr auto inf = std::numeric_limits<double>::infinity();

        std::string key_;
        std::unique_ptr<RenderObject> renderObject_;
        double maxWidth_{inf};
        double maxHeight_{inf};
        std::unique_ptr<Widget> child_;
    };
    static constexpr auto LimitedBox(std::string key = {}) noexcept // NOLINT
    {
        return LimitedBoxBuild{std::move(key)};
    }

    // =========================================================================
    // AspectRatio
    //
    // 语义（对应 Flutter RenderAspectRatio）：
    //   aspectRatio = width / height，必须 > 0。
    //
    //   1. 先把宽度设成 maxWidth；若宽度无界，则取 height * aspectRatio
    //      （height = maxHeight）或者 0（两轴都无界）。
    //   2. 由 width 推出 height = width / aspectRatio。
    //   3. 依次做 4 步“夹取修正”，保证 height ∈ [minH, maxH] 且 width ∈ [minW, maxW]：
    //        · height > maxHeight  → 用 maxHeight 反推 width
    //        · width  < minWidth   → 用 minWidth  反推 height
    //        · height < minHeight  → 用 minHeight 反推 width
    //   4. size = c.constrain(Size(width, height))
    //   5. child 用 BoxConstraints::tight(size) 布局（保证子级不会改变比例）
    //
    //   无 child 时：size = c.smallest()。
    // =========================================================================
    struct AspectRatioWidget : Widget
    {
        // NOLINTBEGIN
        constexpr AspectRatioWidget(std::string key, std::unique_ptr<RenderObject> ro,
                                    double aspectRatio,
                                    std::unique_ptr<Widget> child) noexcept // NOLINTEND
            : Widget(std::move(key), std::move(ro)), aspectRatio_{aspectRatio},
              child_{std::move(child)}
        {
            assert(aspectRatio_ > 0.0);
        }

        constexpr void layout(BoxConstraints c) override
        {
            if (!child_)
            {
                Self().size = c.smallest();
                return;
            }

            // ── 1. 决定初始宽度
            double width = c.maxWidth();
            if (!c.hasBoundedWidth())
            {
                width = c.hasBoundedHeight() ? c.maxHeight() * aspectRatio_ : 0.0;
            }

            // ── 2. 由宽度反推高度
            double height = width / aspectRatio_;

            // ── 3. 四步夹取修正（顺序与 Flutter 完全一致）
            if (height > c.maxHeight())
            {
                height = c.maxHeight();
                width = height * aspectRatio_;
            }
            if (width < c.minWidth())
            {
                width = c.minWidth();
                height = width / aspectRatio_;
            }
            if (height < c.minHeight())
            {
                height = c.minHeight();
                width = height * aspectRatio_;
            }

            // ── 4. 自身尺寸 = 约束夹取后的 (w, h)
            Self().size = c.constrain(Size{width, height});

            // ── 5. child 用 tight 约束布局（保证 child 内部不改变比例）
            child_->layout(BoxConstraints::tight(Self().size));
        }

        constexpr void updateOffset(Offset offset) noexcept override
        {
            Self().offset = offset;
            if (child_)
            {
                // AspectRatio 无 margin/padding，child 与自身同原点
                child_->updateOffset(offset);
            }
        }

        [[nodiscard]] constexpr std::span<const std::unique_ptr<Widget>> children()
            const noexcept override
        {
            if (!child_)
                return {};
            return std::span<const std::unique_ptr<Widget>>{&child_, 1};
        }

      private:
        double aspectRatio_;
        std::unique_ptr<Widget> child_;
    };
    struct AspectRatioBuild
    {
        constexpr explicit AspectRatioBuild(std::string key, double aspectRatio) noexcept
            : key_(std::move(key)), aspectRatio_{aspectRatio}
        {
        }

        explicit operator std::unique_ptr<Widget>()
        {
            if (!(aspectRatio_ > 0.0))
                throw std::logic_error{"AspectRatio requires aspectRatio > 0"};
            return std::make_unique<AspectRatioWidget>(std::move(key_),
                                                       std::move(renderObject_),
                                                       aspectRatio_, std::move(child_));
        }

        constexpr auto &key(std::string key) noexcept
        {
            key_ = std::move(key);
            return *this;
        }

        constexpr auto &renderObject(std::unique_ptr<RenderObject> ro) noexcept
        {
            renderObject_ = std::move(ro);
            return *this;
        }

        constexpr auto &aspectRatio(double aspectRatio) noexcept
        {
            aspectRatio_ = aspectRatio;
            return *this;
        }

        constexpr auto &child(std::unique_ptr<Widget> child) noexcept
        {
            child_ = std::move(child);
            return *this;
        }
        constexpr auto &child(WidgetBuild auto &&b) noexcept
        {
            return child(
                static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
        }

      private:
        std::string key_;
        std::unique_ptr<RenderObject> renderObject_;
        double aspectRatio_;
        std::unique_ptr<Widget> child_;
    };
    static constexpr auto AspectRatio(std::string key,
                                      double aspectRatio) noexcept // NOLINT
    {
        return AspectRatioBuild{std::move(key), aspectRatio};
    }

    // =========================================================================
    // FractionallySizedBox
    //
    // 语义（对应 Flutter RenderFractionallySizedOverflowBox）：
    //   widthFactor  != null → child 收到严格宽度约束 = maxWidth * widthFactor
    //   widthFactor  == null → child 原样继承父的宽度约束
    //   heightFactor != null → child 收到严格高度约束 = maxHeight * heightFactor
    //   heightFactor == null → child 原样继承父的高度约束
    //
    //   自身尺寸 = c.constrain(child.size)（可能比 child 小，从而产生溢出）
    //   child 相对自身按 alignment 对齐
    // =========================================================================
    struct FractionallySizedBoxWidget : Widget
    {
        // NOLINTBEGIN
        constexpr FractionallySizedBoxWidget(
            std::string key, std::unique_ptr<RenderObject> ro, Alignment alignment,
            std::optional<double> widthFactor, std::optional<double> heightFactor,
            std::unique_ptr<Widget> child) noexcept // NOLINTEND
            : Widget(std::move(key), std::move(ro)), alignment_{alignment},
              widthFactor_{widthFactor}, heightFactor_{heightFactor},
              child_{std::move(child)}
        {
            assert(!widthFactor_.has_value() || *widthFactor_ >= 0.0);
            assert(!heightFactor_.has_value() || *heightFactor_ >= 0.0);
        }

        constexpr void layout(BoxConstraints c) override
        {
            const BoxConstraints inner = innerConstraints(c);

            if (child_)
            {
                Widget &childRef = *child_;

                // 1. child 用「按比例缩放后的约束」布局
                childRef.layout(inner);

                // 2. 自身尺寸 = 父约束夹取 child.size
                //    （若 widthFactor > 1，child 溢出，自身被夹回 maxWidth）
                Self().size = c.constrain(childRef.size);
            }
            else
            {
                // 3. 无 child：取缩放后约束下的最小尺寸（与 Flutter 一致）
                Self().size = c.constrain(inner.smallest());
            }
        }

        constexpr void updateOffset(Offset offset) noexcept override
        {
            Self().offset = offset;
            if (!child_)
                return;

            Widget &childRef = *child_;

            // free 值可能为负 → child 溢出时按 alignment 反向偏移（与 Flutter 溢出行为一致）
            const double freeW = Self().size.width - childRef.size.width;
            const double freeH = Self().size.height - childRef.size.height;

            const double dx = offset.x + freeW * (alignment_.x + 1.0) * 0.5;
            const double dy = offset.y + freeH * (alignment_.y + 1.0) * 0.5;

            childRef.updateOffset({dx, dy});
        }

        [[nodiscard]] constexpr std::span<const std::unique_ptr<Widget>> children()
            const noexcept override
        {
            if (!child_)
                return {};
            return std::span<const std::unique_ptr<Widget>>{&child_, 1};
        }

      private:
        // 对应 Flutter 的 RenderFractionallySizedOverflowBox._getInnerConstraints
        [[nodiscard]] constexpr BoxConstraints innerConstraints(
            BoxConstraints c) const noexcept
        {
            double minWidth = c.minWidth();
            double maxWidth = c.maxWidth();
            if (widthFactor_.has_value())
            {
                const double width = maxWidth * (*widthFactor_);
                minWidth = width;
                maxWidth = width;
            }

            double minHeight = c.minHeight();
            double maxHeight = c.maxHeight();
            if (heightFactor_.has_value())
            {
                const double height = maxHeight * (*heightFactor_);
                minHeight = height;
                maxHeight = height;
            }

            return BoxConstraints{minWidth, maxWidth, minHeight, maxHeight};
        }

        Alignment alignment_;
        std::optional<double> widthFactor_;
        std::optional<double> heightFactor_;
        std::unique_ptr<Widget> child_;
    };

    struct FractionallySizedBoxBuild
    {
        constexpr explicit FractionallySizedBoxBuild(std::string key) noexcept
            : key_(std::move(key))
        {
        }

        explicit operator std::unique_ptr<Widget>()
        {
            return std::make_unique<FractionallySizedBoxWidget>(
                std::move(key_), std::move(renderObject_), alignment_, widthFactor_,
                heightFactor_, std::move(child_));
        }

        constexpr auto &key(std::string key) noexcept
        {
            key_ = std::move(key);
            return *this;
        }

        constexpr auto &renderObject(std::unique_ptr<RenderObject> ro) noexcept
        {
            renderObject_ = std::move(ro);
            return *this;
        }

        constexpr auto &alignment(Alignment alignment) noexcept
        {
            alignment_ = alignment;
            return *this;
        }

        constexpr auto &widthFactor(double factor) noexcept
        {
            widthFactor_ = factor;
            return *this;
        }

        constexpr auto &heightFactor(double factor) noexcept
        {
            heightFactor_ = factor;
            return *this;
        }

        constexpr auto &clearWidthFactor() noexcept
        {
            widthFactor_.reset();
            return *this;
        }

        constexpr auto &clearHeightFactor() noexcept
        {
            heightFactor_.reset();
            return *this;
        }

        constexpr auto &child(std::unique_ptr<Widget> child) noexcept
        {
            child_ = std::move(child);
            return *this;
        }
        constexpr auto &child(WidgetBuild auto &&b) noexcept
        {
            return child(
                static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
        }

      private:
        std::string key_;
        std::unique_ptr<RenderObject> renderObject_;
        Alignment alignment_ = Alignment::center;
        std::optional<double> widthFactor_;
        std::optional<double> heightFactor_;
        std::unique_ptr<Widget> child_;
    };

    static constexpr auto FractionallySizedBox(std::string key = {}) noexcept // NOLINT
    {
        return FractionallySizedBoxBuild{std::move(key)};
    }

    // ===============================================================================================
    // flex 模式 [start]
    // ===============================================================================================
    // =========================================================================
    // FlexInfo —— Flex 对每个子节点的记账信息
    // 与 children_ 一一对应，仅存在于 Flex 内部，不进 Widget 抽象
    // =========================================================================
    struct FlexInfo
    {
        int flex = 0;                 // 0 表示非弹性
        FlexFit fit = FlexFit::tight; // flex > 0 时才有意义
    };

    //NOTE: Spacer 引入 takeChild。 这是部分flex 语义的。因为这些标签是有关联的。因为CPP和dart是不一样的
    // =========================================================================
    // FlexChildBuilder concept：任何能向 Flex 贡献 (child, flex, fit) 的构建器
    // =========================================================================
    template <typename B>
    concept FlexChildBuilder = requires(B &b) {
        { b.flexValue() } -> std::convertible_to<int>;
        { b.fitValue() } -> std::convertible_to<FlexFit>;
        { b.takeChild() } -> std::convertible_to<std::unique_ptr<Widget>>;
    };

    // =========================================================================
    // FlexibleBuild —— 构建期 DSL，运行时不是 Widget
    // 通过 FlexBuild::addChild(FlexibleBuild&) 被解包为 (child, flex, fit)
    // =========================================================================
    struct FlexibleBuild
    {
        constexpr explicit FlexibleBuild(std::string key = {}) noexcept
            : key_(std::move(key))
        {
        }

        constexpr auto &key(std::string k) noexcept
        {
            key_ = std::move(k);
            return *this;
        }
        constexpr auto &flex(int f) noexcept
        {
            flex_ = f;
            return *this;
        }
        constexpr auto &fit(FlexFit f) noexcept
        {
            fit_ = f;
            return *this;
        }

        constexpr auto &child(std::unique_ptr<Widget> c) noexcept
        {
            child_ = std::move(c);
            return *this;
        }
        constexpr auto &child(WidgetBuild auto &&b) noexcept
        {
            return child(
                static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
        }

        // 供 FlexBuild 使用
        [[nodiscard]] constexpr int flexValue() const noexcept
        {
            return flex_;
        }
        [[nodiscard]] constexpr FlexFit fitValue() const noexcept
        {
            return fit_;
        }
        [[nodiscard]] std::unique_ptr<Widget> takeChild() noexcept
        {
            return std::move(child_);
        }

      private:
        std::string key_;
        int flex_ = 1;
        FlexFit fit_ = FlexFit::loose; // Flutter 默认 loose
        std::unique_ptr<Widget> child_;
    };

    struct FlexWidget : Widget
    {
        constexpr FlexWidget(std::string key, std::unique_ptr<RenderObject> ro,
                             Axis direction, MainAxisAlignment mainAxisAlignment,
                             MainAxisSize mainAxisSize,
                             CrossAxisAlignment crossAxisAlignment,
                             std::optional<TextDirection> textDirection,
                             VerticalDirection verticalDirection,
                             std::optional<TextBaseline> textBaseline,
                             std::vector<std::unique_ptr<Widget>> children,
                             std::vector<FlexInfo> flexes) noexcept
            : Widget(std::move(key), std::move(ro)), direction_{direction},
              mainAxisAlignment_{mainAxisAlignment}, mainAxisSize_{mainAxisSize},
              crossAxisAlignment_{crossAxisAlignment}, textDirection_{textDirection},
              verticalDirection_{verticalDirection}, textBaseline_{textBaseline},
              children_{std::move(children)}, flexes_{std::move(flexes)}
        {
            assert(crossAxisAlignment_ != CrossAxisAlignment::baseline ||
                   textBaseline_.has_value());
            assert(children_.size() == flexes_.size());
        }

        constexpr void layout(BoxConstraints c) override
        {
            constexpr auto inf = std::numeric_limits<double>::infinity();
            const bool h = direction_ == Axis::horizontal;
            const double maxMain = h ? c.maxWidth() : c.maxHeight();
            const double maxCross = h ? c.maxHeight() : c.maxWidth();
            const bool canFlex = std::isfinite(maxMain);
            const bool stretch = crossAxisAlignment_ == CrossAxisAlignment::stretch;

            // ── 空 children 边界 ────────────────────────────────────────
            if (children_.empty())
            {
                const double main =
                    (mainAxisSize_ == MainAxisSize::max && canFlex) ? maxMain : 0.0;
                Self().size = c.constrain(h ? Size{main, 0.0} : Size{0.0, main});
                return;
            }

            // ── 第 1 遍：所有非弹性子节点用「主轴无界」约束 layout ──────
            int totalFlex = 0;
            double allocated = 0.0;
            double maxChildCross = 0.0;
            size_t lastFlexIdx = SIZE_MAX;

            for (size_t i = 0; i < children_.size(); ++i)
            {
                if (flexes_[i].flex > 0)
                {
                    totalFlex += flexes_[i].flex;
                    lastFlexIdx = i;
                }
                else
                {
                    const BoxConstraints cc =
                        h ? BoxConstraints{0.0, inf, stretch ? maxCross : 0.0, maxCross}
                          : BoxConstraints{stretch ? maxCross : 0.0, maxCross, 0.0, inf};
                    children_[i]->layout(cc);
                    allocated += h ? children_[i]->size.width : children_[i]->size.height;
                    const double cs =
                        h ? children_[i]->size.height : children_[i]->size.width;
                    if (cs > maxChildCross)
                        maxChildCross = cs;
                }
            }

            // ── 第 2 遍：把剩余主轴按 flex 分配，tight/loose 决定 min ───
            const double freeSpace = std::max(0.0, (canFlex ? maxMain : 0.0) - allocated);
            double allocatedFlexSpace = 0.0;

            if (totalFlex > 0)
            {
                const double spacePerFlex = canFlex ? (freeSpace / totalFlex) : inf;

                for (size_t i = 0; i < children_.size(); ++i)
                {
                    if (flexes_[i].flex <= 0)
                        continue;

                    const double maxChildExtent = !canFlex ? inf
                                                  : (i == lastFlexIdx)
                                                      ? (freeSpace - allocatedFlexSpace)
                                                      : (spacePerFlex * flexes_[i].flex);

                    const double minChildExtent =
                        (flexes_[i].fit == FlexFit::tight) ? maxChildExtent : 0.0;

                    const double minCrossSize = stretch ? maxCross : 0.0;
                    const BoxConstraints cc =
                        h ? BoxConstraints{minChildExtent, maxChildExtent, minCrossSize,
                                           maxCross}
                          : BoxConstraints{minCrossSize, maxCross, minChildExtent,
                                           maxChildExtent};
                    children_[i]->layout(cc);

                    allocated += h ? children_[i]->size.width : children_[i]->size.height;
                    allocatedFlexSpace += maxChildExtent;
                    const double cs =
                        h ? children_[i]->size.height : children_[i]->size.width;
                    if (cs > maxChildCross)
                        maxChildCross = cs;
                }
            }

            // ── 自身尺寸 ────────────────────────────────────────────────
            const double idealMain =
                (mainAxisSize_ == MainAxisSize::max && canFlex) ? maxMain : allocated;
            const double idealCross = stretch ? maxCross : maxChildCross;

            Self().size = c.constrain(h ? Size{idealMain, idealCross}
                                        : Size{idealCross, idealMain});
        }

        constexpr void updateOffset(Offset offset) noexcept override
        {
            Self().offset = offset;
            if (children_.empty())
                return;

            const bool h = direction_ == Axis::horizontal;
            const double selfMain = h ? Self().size.width : Self().size.height;
            const double selfCross = h ? Self().size.height : Self().size.width;

            double allocated = 0.0;
            for (auto &child : children_)
                allocated += h ? child->size.width : child->size.height;

            const double remaining = std::max(0.0, selfMain - allocated);
            const auto n = static_cast<double>(children_.size());

            double lead = 0.0, between = 0.0;
            switch (mainAxisAlignment_)
            {
            case MainAxisAlignment::start:
                break;
            case MainAxisAlignment::end:
                lead = remaining;
                break;
            case MainAxisAlignment::center:
                lead = remaining / 2.0;
                break;
            case MainAxisAlignment::spaceBetween:
                between = (n > 1.0) ? remaining / (n - 1.0) : 0.0;
                break;
            case MainAxisAlignment::spaceAround:
                between = remaining / n;
                lead = between / 2.0;
                break;
            case MainAxisAlignment::spaceEvenly:
                between = remaining / (n + 1.0);
                lead = between;
                break;
            }

            const bool startTopLeftMain =
                h ? (textDirection_.value_or(TextDirection::ltr) == TextDirection::ltr)
                  : (verticalDirection_ == VerticalDirection::down);
            const bool startTopLeftCross =
                h ? (verticalDirection_ == VerticalDirection::down)
                  : (textDirection_.value_or(TextDirection::ltr) == TextDirection::ltr);
            const bool flipMain = !startTopLeftMain;
            const bool flipCross = !startTopLeftCross;

            double cursor = lead;
            for (auto &child : children_)
            {
                const double childMain = h ? child->size.width : child->size.height;
                const double childCross = h ? child->size.height : child->size.width;

                const double lm = cursor;
                double lc = 0.0;
                switch (crossAxisAlignment_)
                {
                case CrossAxisAlignment::start:
                case CrossAxisAlignment::baseline:
                case CrossAxisAlignment::stretch:
                    lc = 0.0;
                    break;
                case CrossAxisAlignment::end:
                    lc = selfCross - childCross;
                    break;
                case CrossAxisAlignment::center:
                    lc = (selfCross - childCross) / 2.0;
                    break;
                }

                const double am = flipMain ? selfMain - lm - childMain : lm;
                const double ac = flipCross ? selfCross - lc - childCross : lc;

                double dx, dy;
                if (h)
                {
                    dx = offset.x + am;
                    dy = offset.y + ac;
                }
                else
                {
                    dx = offset.x + ac;
                    dy = offset.y + am;
                }

                child->updateOffset({dx, dy});
                cursor += childMain + between;
            }
        }

        [[nodiscard]] constexpr std::span<const std::unique_ptr<Widget>> children()
            const noexcept override
        {
            return children_;
        }

      private:
        Axis direction_;
        MainAxisAlignment mainAxisAlignment_;
        MainAxisSize mainAxisSize_;
        CrossAxisAlignment crossAxisAlignment_;
        std::optional<TextDirection> textDirection_;
        VerticalDirection verticalDirection_;
        std::optional<TextBaseline> textBaseline_;
        std::vector<std::unique_ptr<Widget>> children_;
        std::vector<FlexInfo> flexes_; // 与 children_ 一一对应
    };
    struct FlexBuild
    {
        constexpr FlexBuild(std::string key) noexcept : key_(std::move(key)) {}

        constexpr explicit operator std::unique_ptr<Widget>()
        {
            if (!direction_)
                throw std::logic_error{"Flex requires direction value"};
            return std::make_unique<FlexWidget>(
                std::move(key_), std::move(renderObject_), *direction_,
                mainAxisAlignment_, mainAxisSize_, crossAxisAlignment_, textDirection_,
                verticalDirection_, textBaseline_, std::move(children_),
                std::move(flexes_));
        }

        constexpr auto &key(std::string key) noexcept
        {
            key_ = std::move(key);
            return *this;
        }
        constexpr auto &renderObject(std::unique_ptr<RenderObject> ro) noexcept
        {
            renderObject_ = std::move(ro);
            return *this;
        }
        constexpr auto &direction(Axis v) noexcept
        {
            direction_ = v;
            return *this;
        }
        constexpr auto &mainAxisAlignment(MainAxisAlignment v) noexcept
        {
            mainAxisAlignment_ = v;
            return *this;
        }
        constexpr auto &mainAxisSize(MainAxisSize v) noexcept
        {
            mainAxisSize_ = v;
            return *this;
        }
        constexpr auto &crossAxisAlignment(CrossAxisAlignment v) noexcept
        {
            crossAxisAlignment_ = v;
            return *this;
        }
        constexpr auto &textDirection(TextDirection v) noexcept
        {
            textDirection_ = v;
            return *this;
        }
        constexpr auto &clearTextDirection() noexcept
        {
            textDirection_.reset();
            return *this;
        }
        constexpr auto &verticalDirection(VerticalDirection v) noexcept
        {
            verticalDirection_ = v;
            return *this;
        }
        constexpr auto &textBaseline(TextBaseline v) noexcept
        {
            textBaseline_ = v;
            return *this;
        }

        // =====================================================================
        // ── FlexBuild::addChild 三个重载 ─────────────────────────────────
        // (1) 普通子 Widget：flex = 0
        constexpr auto &addChild(std::unique_ptr<Widget> child) noexcept
        {
            children_.push_back(std::move(child));
            flexes_.push_back(FlexInfo{.flex = 0, .fit = FlexFit::tight});
            return *this;
        }
        // (2) WidgetBuild 概念（如 SizedBoxBuild / PaddingBuild …）：转成 Widget 走 (1)
        constexpr auto &addChild(WidgetBuild auto &&b) noexcept
        {
            return addChild(
                static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
        }
        // (3) FlexibleBuild / ExpandedBuild / SpacerBuild：解包为 (child, flex, fit)
        template <FlexChildBuilder B>
        constexpr auto &addChild(B &&fb) noexcept
        {
            children_.push_back(fb.takeChild());
            flexes_.push_back(FlexInfo{fb.flexValue(), fb.fitValue()});
            return *this;
        }
        // =====================================================================

        // ── Flexible DSL：解包为 (child, flex, fit) ──────────────────
        constexpr auto &addChild(FlexibleBuild &fb) noexcept
        {
            children_.push_back(fb.takeChild());
            flexes_.push_back(FlexInfo{fb.flexValue(), fb.fitValue()});
            return *this;
        }

      private:
        std::string key_;
        std::unique_ptr<RenderObject> renderObject_;
        std::optional<Axis> direction_;
        MainAxisAlignment mainAxisAlignment_ = MainAxisAlignment::start;
        MainAxisSize mainAxisSize_ = MainAxisSize::max;
        CrossAxisAlignment crossAxisAlignment_ = CrossAxisAlignment::center;
        std::optional<TextDirection> textDirection_;
        VerticalDirection verticalDirection_ = VerticalDirection::down;
        std::optional<TextBaseline> textBaseline_;
        std::vector<std::unique_ptr<Widget>> children_;
        std::vector<FlexInfo> flexes_;
    };
    static constexpr auto Flex(std::string key = {}) noexcept // NOLINT
    {
        return FlexBuild{std::move(key)};
    }

    // ===============================================================================================
    // Row / Column —— Flex 的语法糖
    //
    // 语义（对应 Dart 的 Row / Column）：
    //   Row    = Flex(direction: Axis::horizontal)
    //   Column = Flex(direction: Axis::vertical)
    //
    // 方向是固定值，调用方不可覆盖；其余字段（mainAxisAlignment / mainAxisSize /
    // crossAxisAlignment / textDirection / verticalDirection / textBaseline / children）
    // 全部继承自 FlexBuild。
    // ===============================================================================================
    struct RowBuild : FlexBuild
    {
        constexpr RowBuild(std::string key) noexcept : FlexBuild(std::move(key))
        {
            FlexBuild::direction(Axis::horizontal);
        }
        // Row 方向固定为 horizontal：删掉继承来的 direction() 以禁止覆盖
        FlexBuild &direction(Axis) noexcept = delete;
    };
    struct ColumnBuild : FlexBuild
    {
        constexpr ColumnBuild(std::string key) noexcept : FlexBuild(std::move(key))
        {
            FlexBuild::direction(Axis::vertical);
        }
        // Column 方向固定为 vertical：删掉继承来的 direction() 以禁止覆盖
        FlexBuild &direction(Axis) noexcept = delete;
    };
    static constexpr auto Row(std::string key = {}) noexcept // NOLINT
    {
        return RowBuild{std::move(key)};
    }
    static constexpr auto Column(std::string key = {}) noexcept // NOLINT
    {
        return ColumnBuild{std::move(key)};
    }

    //NOTE: ParentDataWidget<FlexParentData> 的解决是，引入 FlexibleBuild 重构 flex. 当前和flutter的有点不一样了。但是 FlexInfo 将传递方便很多。影响访问是明确的
    static constexpr auto Flexible(std::string key = {}) noexcept // NOLINT
    {
        return FlexibleBuild{std::move(key)};
    }

    // =========================================================================
    // ExpandedBuild —— Flexible(fit: FlexFit.tight) 的语法糖
    // 复用 FlexibleBuild 的全部字段与解包逻辑，只把 fit 锁死为 tight
    // =========================================================================
    struct ExpandedBuild : FlexibleBuild
    {
        constexpr explicit ExpandedBuild(std::string key = {}) noexcept
            : FlexibleBuild(std::move(key))
        {
            FlexibleBuild::fit(FlexFit::tight);
        }

        // 禁止外部覆盖 fit：Expanded 的语义就是 tight
        FlexibleBuild &fit(FlexFit) noexcept = delete;
    };
    static constexpr auto Expanded(std::string key = {}) noexcept // NOLINT
    {
        return ExpandedBuild{std::move(key)};
    }

    // NOTE: Spacer 生成一个携带 key 的 0×0 SizedBoxWidget，同时把 (flex, tight) 交给 Flex 记账。
    // NOTE: 唯一的改动是把 FlexBuild::addChild(FlexibleBuild&) 泛化成 concept 重载，让 Spacer 也能走同一条路
    /*
class Spacer extends StatelessWidget {
  const Spacer({super.key, this.flex = 1}) : assert(flex > 0);
  final int flex;
  Widget build(BuildContext context) => Expanded(
    flex: flex,
    child: const SizedBox.shrink(),   // 0×0 空盒
  );
}
关键点：
    Spacer 本身是 StatelessWidget，其 key 传给它自己；子树里是 Expanded(tight) → SizedBox.shrink()。

    find.byKey('s') 命中的是 Spacer，renderObject 回溯到它子树里的 RenderConstrainedBox，即 SizedBox.shrink() 的 renderObject。

    所以 Spacer 在运行时树里必须真实存在一个 Widget（不同于 Flexible），能被 find，能被 getSize 拿到尺寸。

    尺寸行为 = SizedBox(0,0) 在 tight 约束下被撑成 (分配的主轴 × 0 交叉轴)。
*/
    // =========================================================================
    // SpacerBuild —— Spacer 的等价物
    // 运行时树里生成一个真实存在的、携带 key 的 0×0 SizedBoxWidget；
    // 同时以 (flex, FlexFit::tight) 参与 Flex 的记账。
    // =========================================================================
    struct SpacerBuild
    {
        constexpr explicit SpacerBuild(std::string key = {}) noexcept
            : key_(std::move(key))
        {
            assert(flex_ > 0); // Dart: assert(flex > 0)
        }

        constexpr auto &key(std::string k) noexcept
        {
            key_ = std::move(k);
            return *this;
        }

        constexpr auto &flex(int f) noexcept
        {
            assert(f > 0); // Dart: assert(flex > 0)
            flex_ = f;
            return *this;
        }

        // ── 供 FlexBuild 解包（与 FlexibleBuild 相同接口）──────────────
        [[nodiscard]] constexpr int flexValue() const noexcept
        {
            return flex_;
        }
        [[nodiscard]] constexpr FlexFit fitValue() const noexcept
        {
            return FlexFit::tight;
        }

        [[nodiscard]] std::unique_ptr<Widget> takeChild() noexcept
        {
            // 0×0 空盒子，key 由 Spacer 提供
            return std::make_unique<SizedBoxWidget>(std::move(key_), nullptr,
                                                    std::nullopt, std::nullopt, nullptr);
        }

      private:
        std::string key_;
        int flex_ = 1;
    };
    static constexpr auto Spacer(std::string key = {}) noexcept // NOLINT
    {
        return SpacerBuild{std::move(key)};
    }

    enum class WrapAlignment : std::uint8_t
    {
        start,
        end,
        center,
        spaceBetween,
        spaceAround,
        spaceEvenly
    };
    enum class WrapCrossAlignment : std::uint8_t
    {
        start,
        end,
        center
    };
    struct WrapWidget : Widget
    {
        // NOLINTBEGIN
        constexpr WrapWidget(std::string key, std::unique_ptr<RenderObject> ro,
                             Axis direction, WrapAlignment alignment, double spacing,
                             WrapAlignment runAlignment, double runSpacing,
                             WrapCrossAlignment crossAxisAlignment,
                             std::optional<TextDirection> textDirection,
                             VerticalDirection verticalDirection,
                             std::vector<std::unique_ptr<Widget>> children) noexcept
            : Widget(std::move(key), std::move(ro)), direction_{direction},
              alignment_{alignment}, spacing_{spacing}, runAlignment_{runAlignment},
              runSpacing_{runSpacing}, crossAxisAlignment_{crossAxisAlignment},
              textDirection_{textDirection}, verticalDirection_{verticalDirection},
              children_{std::move(children)} // NOLINTEND
        {
        }

        constexpr void layout(BoxConstraints c) override
        {
            constexpr auto inf = std::numeric_limits<double>::infinity();
            const bool h = direction_ == Axis::horizontal;
            const double maxMain = h ? c.maxWidth() : c.maxHeight();

            // ── 空 children：返回 constraints.smallest ────────────────
            if (children_.empty())
            {
                Self().size = c.smallest();
                runs_.clear();
                return;
            }

            // ── flip 判定（严格按 RenderWrap.performLayout）────────────
            flipMainAxis_ =
                h ? (textDirection_.value_or(TextDirection::ltr) == TextDirection::rtl)
                  : (verticalDirection_ == VerticalDirection::up);
            flipCrossAxis_ =
                h ? (verticalDirection_ == VerticalDirection::up)
                  : (textDirection_.value_or(TextDirection::ltr) == TextDirection::rtl);

            // ── 子节点约束：只约束主轴 max，其他无界 ──────────────────
            const BoxConstraints childConstraints =
                h ? BoxConstraints{0.0, maxMain, 0.0, inf}
                  : BoxConstraints{0.0, inf, 0.0, maxMain};

            // ── 阶段 1：逐个子节点布局并分组为 runs ───────────────────
            runs_.clear();
            double runMain = 0.0;
            double runCross = 0.0;
            size_t runStart = 0;
            int childCount = 0;
            double mainAxisExtent = 0.0;
            double crossAxisExtent = 0.0;

            for (size_t i = 0; i < children_.size(); ++i)
            {
                auto &child = children_[i];
                child->layout(childConstraints);

                const double childMain = h ? child->size.width : child->size.height;
                const double childCross = h ? child->size.height : child->size.width;

                // 换行判定：runMain + spacing + childMain > mainAxisLimit
                if (childCount > 0 && runMain + spacing_ + childMain > maxMain)
                {
                    mainAxisExtent = std::max(mainAxisExtent, runMain);
                    crossAxisExtent += runCross;
                    if (!runs_.empty())
                        crossAxisExtent += runSpacing_;
                    runs_.push_back(RunInfo{runMain, runCross, runStart,
                                            static_cast<size_t>(childCount)});
                    runMain = 0.0;
                    runCross = 0.0;
                    childCount = 0;
                    runStart = i;
                }

                runMain += childMain;
                if (childCount > 0)
                    runMain += spacing_;
                runCross = std::max(runCross, childCross);
                ++childCount;
            }
            // flush 最后一个 run
            if (childCount > 0)
            {
                mainAxisExtent = std::max(mainAxisExtent, runMain);
                crossAxisExtent += runCross;
                if (!runs_.empty())
                    crossAxisExtent += runSpacing_;
                runs_.push_back(RunInfo{runMain, runCross, runStart,
                                        static_cast<size_t>(childCount)});
            }

            // ── 阶段 2：自身尺寸 ──────────────────────────────────────
            Self().size = c.constrain(h ? Size{mainAxisExtent, crossAxisExtent}
                                        : Size{crossAxisExtent, mainAxisExtent});

            // ── 缓存给 updateOffset 用 ────────────────────────────────
            containerMainExt_ = h ? Self().size.width : Self().size.height;
            containerCrossExt_ = h ? Self().size.height : Self().size.width;
            totalMainExt_ = mainAxisExtent;
            totalCrossExt_ = crossAxisExtent;
        }

        constexpr void updateOffset(Offset offset) noexcept override
        {
            Self().offset = offset;
            if (children_.empty() || runs_.empty())
                return;

            const bool h = direction_ == Axis::horizontal;

            // ── runs 在交叉轴对齐 ────────────────────────────────────
            const double crossAxisFreeSpace =
                std::max(0.0, containerCrossExt_ - totalCrossExt_);
            const int runCount = static_cast<int>(runs_.size());

            double runLeadingSpace = 0.0;
            double runBetweenSpace = 0.0;
            switch (runAlignment_)
            {
            case WrapAlignment::start:
                break;
            case WrapAlignment::end:
                runLeadingSpace = crossAxisFreeSpace;
                break;
            case WrapAlignment::center:
                runLeadingSpace = crossAxisFreeSpace / 2.0;
                break;
            case WrapAlignment::spaceBetween:
                runBetweenSpace =
                    runCount > 1 ? crossAxisFreeSpace / (runCount - 1) : 0.0;
                break;
            case WrapAlignment::spaceAround:
                runBetweenSpace = crossAxisFreeSpace / runCount;
                runLeadingSpace = runBetweenSpace / 2.0;
                break;
            case WrapAlignment::spaceEvenly:
                runBetweenSpace = crossAxisFreeSpace / (runCount + 1);
                runLeadingSpace = runBetweenSpace;
                break;
            }
            runBetweenSpace += runSpacing_; // ← 基础 runSpacing

            double crossAxisOffset =
                flipCrossAxis_ ? containerCrossExt_ - runLeadingSpace : runLeadingSpace;

            for (int i = 0; i < runCount; ++i)
            {
                const auto &run = runs_[i];
                const double runMain = run.mainSize;
                const double runCross = run.crossSize;
                const int cnt = static_cast<int>(run.childCount);

                // ── run 内主轴对齐 ────────────────────────────────────
                const double mainAxisFreeSpace =
                    std::max(0.0, containerMainExt_ - runMain);

                double childLeadingSpace = 0.0;
                double childBetweenSpace = 0.0;
                switch (alignment_)
                {
                case WrapAlignment::start:
                    break;
                case WrapAlignment::end:
                    childLeadingSpace = mainAxisFreeSpace;
                    break;
                case WrapAlignment::center:
                    childLeadingSpace = mainAxisFreeSpace / 2.0;
                    break;
                case WrapAlignment::spaceBetween:
                    childBetweenSpace = cnt > 1 ? mainAxisFreeSpace / (cnt - 1) : 0.0;
                    break;
                case WrapAlignment::spaceAround:
                    childBetweenSpace = mainAxisFreeSpace / cnt;
                    childLeadingSpace = childBetweenSpace / 2.0;
                    break;
                case WrapAlignment::spaceEvenly:
                    childBetweenSpace = mainAxisFreeSpace / (cnt + 1);
                    childLeadingSpace = childBetweenSpace;
                    break;
                }
                childBetweenSpace += spacing_; // ← 基础 spacing

                double childMainPosition = flipMainAxis_
                                               ? containerMainExt_ - childLeadingSpace
                                               : childLeadingSpace;

                // flipCrossAxis 时，run 的交叉轴起点先向下/向上移动一个 runCross
                if (flipCrossAxis_)
                    crossAxisOffset -= runCross;

                for (size_t k = 0; k < run.childCount; ++k)
                {
                    auto &child = children_[run.startIdx + k];
                    const double childMain = h ? child->size.width : child->size.height;
                    const double childCross = h ? child->size.height : child->size.width;

                    // ── run 内交叉轴对齐（考虑 flipCrossAxis）────────
                    const double freeSpace = runCross - childCross;
                    double childCrossOffset = 0.0;
                    switch (crossAxisAlignment_)
                    {
                    case WrapCrossAlignment::start:
                        childCrossOffset = flipCrossAxis_ ? freeSpace : 0.0;
                        break;
                    case WrapCrossAlignment::end:
                        childCrossOffset = flipCrossAxis_ ? 0.0 : freeSpace;
                        break;
                    case WrapCrossAlignment::center:
                        childCrossOffset = freeSpace / 2.0;
                        break;
                    }

                    // flipMainAxis 时，主轴位置先回退一个 childMain
                    if (flipMainAxis_)
                        childMainPosition -= childMain;

                    const double mPos = childMainPosition;
                    const double cPos = crossAxisOffset + childCrossOffset;

                    double dx, dy;
                    if (h)
                    {
                        dx = offset.x + mPos;
                        dy = offset.y + cPos;
                    }
                    else
                    {
                        dx = offset.x + cPos;
                        dy = offset.y + mPos;
                    }

                    child->updateOffset({dx, dy});

                    if (flipMainAxis_)
                        childMainPosition -= childBetweenSpace;
                    else
                        childMainPosition += childMain + childBetweenSpace;
                }

                // ── 进入下一个 run ───────────────────────────────────
                if (flipCrossAxis_)
                    crossAxisOffset -= runBetweenSpace;
                else
                    crossAxisOffset += runCross + runBetweenSpace;
            }
        }

        [[nodiscard]] constexpr std::span<const std::unique_ptr<Widget>> children()
            const noexcept override
        {
            return children_;
        }

      private:
        struct RunInfo
        {
            double mainSize;   // 该 run 的主轴尺寸（含 run 内 spacing）
            double crossSize;  // 该 run 的交叉轴尺寸
            size_t startIdx;   // children_ 中的起始下标
            size_t childCount; // 该 run 中子节点数
        };

        Axis direction_;
        WrapAlignment alignment_;
        double spacing_;
        WrapAlignment runAlignment_;
        double runSpacing_;
        WrapCrossAlignment crossAxisAlignment_;
        std::optional<TextDirection> textDirection_;
        VerticalDirection verticalDirection_;
        std::vector<std::unique_ptr<Widget>> children_;

        // layout 阶段填充；updateOffset 阶段读取
        std::vector<RunInfo> runs_;
        bool flipMainAxis_ = false;
        bool flipCrossAxis_ = false;
        double containerMainExt_ = 0.0;  // 自身尺寸在主轴的投影（constrain 后）
        double containerCrossExt_ = 0.0; // 自身尺寸在交叉轴的投影（constrain 后）
        double totalMainExt_ = 0.0;      // 所有 run 累加主轴尺寸
        double totalCrossExt_ = 0.0;     // 所有 run 累加交叉轴尺寸（含 runSpacing）
    };
    struct WrapBuild
    {
        constexpr explicit WrapBuild(std::string key = {}) noexcept : key_(std::move(key))
        {
        }

        constexpr auto &key(std::string k) noexcept
        {
            key_ = std::move(k);
            return *this;
        }
        constexpr auto &renderObject(std::unique_ptr<RenderObject> ro) noexcept
        {
            renderObject_ = std::move(ro);
            return *this;
        }
        constexpr auto &direction(Axis v) noexcept
        {
            direction_ = v;
            return *this;
        }
        constexpr auto &alignment(WrapAlignment v) noexcept
        {
            alignment_ = v;
            return *this;
        }
        constexpr auto &spacing(double v) noexcept
        {
            spacing_ = v;
            return *this;
        }
        constexpr auto &runAlignment(WrapAlignment v) noexcept
        {
            runAlignment_ = v;
            return *this;
        }
        constexpr auto &runSpacing(double v) noexcept
        {
            runSpacing_ = v;
            return *this;
        }
        constexpr auto &crossAxisAlignment(WrapCrossAlignment v) noexcept
        {
            crossAxisAlignment_ = v;
            return *this;
        }
        constexpr auto &textDirection(TextDirection v) noexcept
        {
            textDirection_ = v;
            return *this;
        }
        constexpr auto &clearTextDirection() noexcept
        {
            textDirection_.reset();
            return *this;
        }
        constexpr auto &verticalDirection(VerticalDirection v) noexcept
        {
            verticalDirection_ = v;
            return *this;
        }

        constexpr auto &addChild(std::unique_ptr<Widget> child) noexcept
        {
            children_.push_back(std::move(child));
            return *this;
        }
        constexpr auto &addChild(WidgetBuild auto &&b) noexcept
        {
            return addChild(
                static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
        }

        constexpr explicit operator std::unique_ptr<Widget>()
        {
            return std::make_unique<WrapWidget>(
                std::move(key_), std::move(renderObject_), direction_, alignment_,
                spacing_, runAlignment_, runSpacing_, crossAxisAlignment_, textDirection_,
                verticalDirection_, std::move(children_));
        }

      private:
        std::string key_;
        std::unique_ptr<RenderObject> renderObject_;
        Axis direction_ = Axis::horizontal;
        WrapAlignment alignment_ = WrapAlignment::start;
        double spacing_ = 0.0;
        WrapAlignment runAlignment_ = WrapAlignment::start;
        double runSpacing_ = 0.0;
        WrapCrossAlignment crossAxisAlignment_ = WrapCrossAlignment::start;
        std::optional<TextDirection> textDirection_;
        VerticalDirection verticalDirection_ = VerticalDirection::down;
        std::vector<std::unique_ptr<Widget>> children_;
    };
    static constexpr auto Wrap(std::string key = {}) noexcept // NOLINT
    {
        return WrapBuild{std::move(key)};
    }

    /*
你的模型：
    layout       → 每个 widget 的 (W, H) + Offset 全部确定
    每帧 update  → 把 Model 矩阵上传 shader
    render       → 画像素
Flow 的模型：
    layout       → 只确定 (W, H)，Offset 待定
    paint 阶段   → 回调 delegate，现场算 Matrix4
    render       → 画像素
NOTE: 我不需要，我会将这些信息和renderobject 绑定，更新model矩阵更直接
*/

    // =========================================================================
    // StackFit
    // =========================================================================
    enum class StackFit : std::uint8_t
    {
        loose,
        expand,
        passthrough
    };
    // =========================================================================
    // StackChildInfo —— Stack 对每个子节点的记账（与 children_ 平行）
    // 未设置任何属性 = 非定位；任一非 null = 定位
    // =========================================================================
    struct StackChildInfo
    {
        std::optional<double> left, top, right, bottom, width, height;

        [[nodiscard]] bool isPositioned() const noexcept
        {
            return left.has_value() || top.has_value() || right.has_value() ||
                   bottom.has_value() || width.has_value() || height.has_value();
        }
    };
    // =========================================================================
    // PositionedBuild —— Stack 的定位 DSL（构建期，运行时不是 Widget）
    // =========================================================================
    struct PositionedBuild
    {
        std::optional<double> left_, top_, right_, bottom_, width_, height_;
        std::unique_ptr<Widget> child_;

        auto &left(double v) noexcept
        {
            left_ = v;
            return *this;
        }
        auto &top(double v) noexcept
        {
            top_ = v;
            return *this;
        }
        auto &right(double v) noexcept
        {
            right_ = v;
            return *this;
        }
        auto &bottom(double v) noexcept
        {
            bottom_ = v;
            return *this;
        }
        auto &width(double v) noexcept
        {
            width_ = v;
            return *this;
        }
        auto &height(double v) noexcept
        {
            height_ = v;
            return *this;
        }

        // 便捷：四边都为 0（对应 Positioned.fill）
        auto &fill() noexcept
        {
            left_ = 0;
            top_ = 0;
            right_ = 0;
            bottom_ = 0;
            return *this;
        }

        // 便捷：fromRect(left, top, width, height)
        auto &fromRect(double l, double t, double w, double h) noexcept
        {
            left_ = l;
            top_ = t;
            width_ = w;
            height_ = h;
            return *this;
        }

        // 便捷：fromRelativeRect(left, top, right, bottom)
        auto &fromLTRB(double l, double t, double r, double b) noexcept
        {
            left_ = l;
            top_ = t;
            right_ = r;
            bottom_ = b;
            return *this;
        }

        // 便捷：directional（把 start/end 按 textDirection 解析为 left/right）
        auto &directional(TextDirection dir, double start, double top,
                          std::optional<double> end = std::nullopt,
                          std::optional<double> bottom = std::nullopt) noexcept
        {
            if (dir == TextDirection::ltr)
            {
                left_ = start;
                if (end)
                    right_ = *end;
            }
            else
            {
                right_ = start;
                if (end)
                    left_ = *end;
            }
            top_ = top;
            if (bottom)
                bottom_ = *bottom;
            return *this;
        }

        auto &child(std::unique_ptr<Widget> c) noexcept
        {
            child_ = std::move(c);
            return *this;
        }
        auto &child(WidgetBuild auto &&b) noexcept
        {
            return child(
                static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
        }

        [[nodiscard]] StackChildInfo info() const noexcept
        {
            return StackChildInfo{left_, top_, right_, bottom_, width_, height_};
        }

        [[nodiscard]] std::unique_ptr<Widget> takeChild() noexcept
        {
            return std::move(child_);
        }
    };
    static constexpr auto Positioned() noexcept // NOLINT
    {
        return PositionedBuild{};
    }
    // =========================================================================
    // StackWidget
    // =========================================================================
    struct StackWidget : Widget
    {
        StackWidget(std::string key, std::unique_ptr<RenderObject> ro,
                    Alignment alignment, StackFit fit,
                    std::vector<std::unique_ptr<Widget>> children,
                    std::vector<StackChildInfo> infos) noexcept
            : Widget(std::move(key), std::move(ro)), alignment_{alignment}, fit_{fit},
              children_{std::move(children)}, infos_{std::move(infos)}
        {
            assert(children_.size() == infos_.size());
        }

        void layout(BoxConstraints c) override
        {
            // ── 空 children ─────────────────────────────────────────
            if (children_.empty())
            {
                const Size big = c.biggest();
                const bool finite = std::isfinite(big.width) && std::isfinite(big.height);
                Self().size = finite ? big : c.smallest();
                return;
            }

            // ── 非定位子节点的约束（按 fit 变换）────────────────────
            BoxConstraints npc;
            switch (fit_)
            {
            case StackFit::loose:
                npc = c.loosen();
                break;
            case StackFit::expand:
                npc = BoxConstraints::tight(c.biggest());
                break;
            case StackFit::passthrough:
                npc = c;
                break;
            }

            // ── 布局非定位子节点，累加最大尺寸 ─────────────────────
            bool hasNonPositioned = false;
            double width = c.minWidth();
            double height = c.minHeight();

            for (size_t i = 0; i < children_.size(); ++i)
            {
                if (!infos_[i].isPositioned())
                {
                    hasNonPositioned = true;
                    children_[i]->layout(npc);
                    width = std::max(width, children_[i]->size.width);
                    height = std::max(height, children_[i]->size.height);
                }
            }

            // ── Stack 尺寸 ─────────────────────────────────────────
            Self().size = hasNonPositioned ? Size{width, height} : c.biggest();

            // ── 布局定位子节点 ─────────────────────────────────────
            for (size_t i = 0; i < children_.size(); ++i)
            {
                if (infos_[i].isPositioned())
                    layoutPositionedChild(*children_[i], infos_[i]);
            }
        }

        constexpr void updateOffset(Offset offset) noexcept override
        {
            Self().offset = offset;

            for (size_t i = 0; i < children_.size(); ++i)
            {
                auto &child = children_[i];
                auto &info = infos_[i];

                double x, y;
                if (!info.isPositioned())
                {
                    const double freeW = Self().size.width - child->size.width;
                    const double freeH = Self().size.height - child->size.height;
                    x = freeW * (alignment_.x + 1.0) / 2.0;
                    y = freeH * (alignment_.y + 1.0) / 2.0;
                }
                else
                {
                    // 水平
                    if (info.left)
                        x = *info.left;
                    else if (info.right)
                        x = Self().size.width - *info.right - child->size.width;
                    else
                    {
                        const double freeW = Self().size.width - child->size.width;
                        x = freeW * (alignment_.x + 1.0) / 2.0;
                    }
                    // 垂直
                    if (info.top)
                        y = *info.top;
                    else if (info.bottom)
                        y = Self().size.height - *info.bottom - child->size.height;
                    else
                    {
                        const double freeH = Self().size.height - child->size.height;
                        y = freeH * (alignment_.y + 1.0) / 2.0;
                    }
                }

                child->updateOffset({offset.x + x, offset.y + y});
            }
        }

        [[nodiscard]] constexpr std::span<const std::unique_ptr<Widget>> children()
            const noexcept override
        {
            return children_;
        }

      private:
        // 对应 RenderStack.layoutPositionedChild
        void layoutPositionedChild(Widget &child, const StackChildInfo &info)
        {
            BoxConstraints cc{0.0, BoxConstraints::inf, 0.0, BoxConstraints::inf};

            if (info.left && info.right)
            {
                const double w = Self().size.width - *info.right - *info.left;
                cc = BoxConstraints{w, w, cc.minHeight(), cc.maxHeight()};
            }
            else if (info.width)
            {
                const double w = *info.width;
                cc = BoxConstraints{w, w, cc.minHeight(), cc.maxHeight()};
            }

            if (info.top && info.bottom)
            {
                const double h = Self().size.height - *info.bottom - *info.top;
                cc = BoxConstraints{cc.minWidth(), cc.maxWidth(), h, h};
            }
            else if (info.height)
            {
                const double h = *info.height;
                cc = BoxConstraints{cc.minWidth(), cc.maxWidth(), h, h};
            }

            child.layout(cc);
        }

        Alignment alignment_ =
            Alignment::topLeft; // = AlignmentDirectional.topStart (ltr)
        StackFit fit_ = StackFit::loose;
        std::vector<std::unique_ptr<Widget>> children_;
        std::vector<StackChildInfo> infos_;
    };
    // =========================================================================
    // StackBuild / Stack
    // =========================================================================
    struct StackBuild
    {
        explicit StackBuild(std::string key = {}) noexcept : key_(std::move(key)) {}

        auto &key(std::string k) noexcept
        {
            key_ = std::move(k);
            return *this;
        }
        auto &renderObject(std::unique_ptr<RenderObject> ro) noexcept
        {
            renderObject_ = std::move(ro);
            return *this;
        }
        auto &alignment(Alignment a) noexcept
        {
            alignment_ = a;
            return *this;
        }
        auto &fit(StackFit f) noexcept
        {
            fit_ = f;
            return *this;
        }

        // 非定位子节点
        auto &addChild(std::unique_ptr<Widget> child) noexcept
        {
            children_.push_back(std::move(child));
            infos_.push_back({}); // 未设置任何属性 → 非定位
            return *this;
        }
        auto &addChild(WidgetBuild auto &&b) noexcept
        {
            return addChild(
                static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
        }

        // 定位子节点
        auto &addChild(PositionedBuild &pb) noexcept
        {
            infos_.push_back(pb.info());
            children_.push_back(pb.takeChild());
            return *this;
        }

        explicit operator std::unique_ptr<Widget>()
        {
            return std::make_unique<StackWidget>(
                std::move(key_), std::move(renderObject_), alignment_, fit_,
                std::move(children_), std::move(infos_));
        }

      private:
        std::string key_;
        std::unique_ptr<RenderObject> renderObject_;
        Alignment alignment_ = Alignment::topLeft;
        StackFit fit_ = StackFit::loose;
        std::vector<std::unique_ptr<Widget>> children_;
        std::vector<StackChildInfo> infos_;
    };
    static constexpr auto Stack(std::string key = {}) noexcept // NOLINT
    {
        return StackBuild{std::move(key)};
    }

    ///NORE: Baseline 是针对文本布局的，现在不需要

    struct OffstageWidget : Widget
    {
        OffstageWidget(std::string key, std::unique_ptr<RenderObject> ro, bool offstage,
                       std::unique_ptr<Widget> child) noexcept
            : Widget(std::move(key), std::move(ro)), offstage_{offstage},
              child_{std::move(child)}
        {
        }

        void layout(BoxConstraints c) override
        {
            // child 无论 offstage 与否都拿到原约束并 layout
            if (child_)
                child_->layout(c);

            // 自身 size：
            //   offstage=true  → constraints.smallest
            //   offstage=false → child.size（无 child 时也是 smallest）
            if (offstage_ || !child_)
                Self().size = c.smallest();
            else
                Self().size = child_->size;
        }

        constexpr void updateOffset(Offset offset) noexcept override
        {
            Self().offset = offset;
            if (child_)
                child_->updateOffset(offset); // 相对 Offstage 恒为 (0,0)
        }
        [[nodiscard]] constexpr std::span<const std::unique_ptr<Widget>> children()
            const noexcept override
        {
            if (!child_)
                return {};
            return std::span<const std::unique_ptr<Widget>>{&child_, 1};
        }

      private:
        bool offstage_;
        std::unique_ptr<Widget> child_;
    };
    struct OffstageBuild
    {
        explicit OffstageBuild(std::string key = {}) noexcept : key_(std::move(key)) {}
        explicit operator std::unique_ptr<Widget>()
        {
            return std::make_unique<OffstageWidget>(
                std::move(key_), std::move(renderObject_), offstage_, std::move(child_));
        }

        auto &key(std::string k) noexcept
        {
            key_ = std::move(k);
            return *this;
        }
        auto &renderObject(std::unique_ptr<RenderObject> ro) noexcept
        {
            renderObject_ = std::move(ro);
            return *this;
        }
        auto &offstage(bool v) noexcept
        {
            offstage_ = v;
            return *this;
        }

        auto &child(std::unique_ptr<Widget> c) noexcept
        {
            child_ = std::move(c);
            return *this;
        }
        auto &child(WidgetBuild auto &&b) noexcept
        {
            return child(
                static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
        }

      private:
        std::string key_;
        std::unique_ptr<RenderObject> renderObject_;
        bool offstage_ = true;
        std::unique_ptr<Widget> child_;
    };
    static constexpr auto Offstage(std::string key = {}) noexcept // NOLINT
    {
        return OffstageBuild{std::move(key)};
    }

    // NOTE: CustomSingleChildLayout 就是把这个决策权直接暴露给用户的 widget
    // =========================================================================
    // SingleChildLayoutDelegate —— 用户自定义单子节点布局的接口
    // 三个方法全部在 layout 阶段调用，返回值直接决定 W/H + Offset
    // =========================================================================
    struct SingleChildLayoutDelegate
    {
        virtual ~SingleChildLayoutDelegate() = default;

        /// 返回自身想要的尺寸，会被父约束 constrain
        virtual Size getSize(BoxConstraints c) const
        {
            return c.biggest();
        }

        /// 返回传给 child 的约束（不会被框架再处理，delegate 负责合理性）
        virtual BoxConstraints getConstraintsForChild(BoxConstraints c) const
        {
            return c;
        }

        /// 返回 child 相对自身的偏移
        virtual Offset getPositionForChild(Size selfSize, Size childSize) const
        {
            return {0.0, 0.0};
        }
    };
    struct CustomSingleChildLayoutWidget : Widget
    {
        CustomSingleChildLayoutWidget(std::string key, std::unique_ptr<RenderObject> ro,
                                      std::unique_ptr<SingleChildLayoutDelegate> delegate,
                                      std::unique_ptr<Widget> child) noexcept
            : Widget(std::move(key), std::move(ro)), delegate_{std::move(delegate)},
              child_{std::move(child)}
        {
            assert(delegate_);
        }

        void layout(BoxConstraints c) override
        {
            // 1. 自身尺寸 = constrain(delegate.getSize(c))
            Self().size = c.constrain(delegate_->getSize(c));

            // 2. child 用 delegate 给定的约束 layout
            if (child_)
                child_->layout(delegate_->getConstraintsForChild(c));
        }

        void updateOffset(Offset offset) noexcept override
        {
            Self().offset = offset;
            if (!child_)
                return;

            // 3. 位置 = delegate.getPositionForChild(自身 size, child size)
            const Offset p = delegate_->getPositionForChild(Self().size, child_->size);
            child_->updateOffset({offset.x + p.x, offset.y + p.y});
        }

        [[nodiscard]] constexpr std::span<const std::unique_ptr<Widget>> children()
            const noexcept override
        {
            if (!child_)
                return {};
            return std::span<const std::unique_ptr<Widget>>{&child_, 1};
        }

      private:
        std::unique_ptr<SingleChildLayoutDelegate> delegate_;
        std::unique_ptr<Widget> child_;
    };
    struct CustomSingleChildLayoutBuild
    {
        explicit CustomSingleChildLayoutBuild(std::string key = {}) noexcept
            : key_(std::move(key))
        {
        }

        auto &key(std::string k) noexcept
        {
            key_ = std::move(k);
            return *this;
        }

        auto &renderObject(std::unique_ptr<RenderObject> ro) noexcept
        {
            renderObject_ = std::move(ro);
            return *this;
        }

        auto &delegate(std::unique_ptr<SingleChildLayoutDelegate> d) noexcept
        {
            delegate_ = std::move(d);
            return *this;
        }

        auto &child(std::unique_ptr<Widget> c) noexcept
        {
            child_ = std::move(c);
            return *this;
        }
        auto &child(WidgetBuild auto &&b) noexcept
        {
            return child(
                static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
        }

        explicit operator std::unique_ptr<Widget>()
        {
            if (!delegate_)
                throw std::logic_error{"CustomSingleChildLayout requires a delegate"};
            return std::make_unique<CustomSingleChildLayoutWidget>(
                std::move(key_), std::move(renderObject_), std::move(delegate_),
                std::move(child_));
        }

      private:
        std::string key_;
        std::unique_ptr<RenderObject> renderObject_;
        std::unique_ptr<SingleChildLayoutDelegate> delegate_;
        std::unique_ptr<Widget> child_;
    };
    static constexpr auto CustomSingleChildLayout(std::string key = {}) noexcept // NOLINT
    {
        return CustomSingleChildLayoutBuild{std::move(key)};
    }

    // =========================================================================
    // MultiChildLayoutContext —— delegate.performLayout 里用来操作 children
    // hasChild / layoutChild / positionChild 对应 Flutter 的同名 API
    // =========================================================================
    struct MultiChildLayoutContext
    {
        Size size{};
        std::function<bool(const std::string &)> hasChild;
        std::function<Size(const std::string &, BoxConstraints)> layoutChild;
        std::function<void(const std::string &, Offset)> positionChild;
    };
    // =========================================================================
    // MultiChildLayoutDelegate
    // =========================================================================
    struct MultiChildLayoutDelegate
    {
        virtual ~MultiChildLayoutDelegate() = default;

        /// 返回自身想要的尺寸，被父约束 constrain
        virtual Size getSize(BoxConstraints c) const
        {
            return c.biggest();
        }

        /// 在 layout 阶段调用：用 ctx.layoutChild / ctx.positionChild 摆放 children
        virtual void performLayout(Size size, MultiChildLayoutContext &ctx) const = 0;
    };
    // =========================================================================
    // LayoutIdBuild —— 把 (id, child) 绑定起来给 delegate 引用
    // 运行时不是 Widget，被 CustomMultiChildLayoutBuild 解包
    // =========================================================================
    struct LayoutIdBuild
    {
        std::string id_;
        std::unique_ptr<Widget> child_;

        explicit LayoutIdBuild(std::string id) : id_{std::move(id)} {}

        auto &child(std::unique_ptr<Widget> c)
        {
            child_ = std::move(c);
            return *this;
        }
        auto &child(WidgetBuild auto &&b)
        {
            return child(
                static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
        }
    };
    static auto LayoutId(std::string id) noexcept // NOLINT
    {
        return LayoutIdBuild{std::move(id)};
    }
    struct CustomMultiChildLayoutWidget : Widget
    {
        CustomMultiChildLayoutWidget(std::string key, std::unique_ptr<RenderObject> ro,
                                     std::unique_ptr<MultiChildLayoutDelegate> delegate,
                                     std::vector<std::unique_ptr<Widget>> children,
                                     std::vector<std::string> ids) noexcept
            : Widget(std::move(key), std::move(ro)), delegate_{std::move(delegate)},
              children_{std::move(children)}, ids_{std::move(ids)}
        {
            assert(delegate_);
            assert(children_.size() == ids_.size());
            for (size_t i = 0; i < ids_.size(); ++i)
                idToIndex_[ids_[i]] = i;
            positions_.resize(children_.size(), Offset{0.0, 0.0});
        }

        void layout(BoxConstraints c) override
        {
            // 1. 自身尺寸 = constrain(delegate.getSize(c))
            Self().size = c.constrain(delegate_->getSize(c));

            // 2. 重置所有 child 位置为 (0, 0)
            std::fill(positions_.begin(), positions_.end(), Offset{0.0, 0.0});

            // 3. 构造 context 交给 delegate
            MultiChildLayoutContext ctx;
            ctx.size = Self().size;
            ctx.hasChild = [this](const std::string &id) -> bool {
                return idToIndex_.count(id) > 0;
            };
            ctx.layoutChild = [this](const std::string &id, BoxConstraints cc) -> Size {
                auto it = idToIndex_.find(id);
                if (it == idToIndex_.end())
                    return {0.0, 0.0};
                auto &child = children_[it->second];
                child->layout(cc);
                return child->size;
            };
            ctx.positionChild = [this](const std::string &id, Offset o) {
                auto it = idToIndex_.find(id);
                if (it == idToIndex_.end())
                    return;
                positions_[it->second] = o;
            };

            delegate_->performLayout(Self().size, ctx);
        }

        void updateOffset(Offset offset) noexcept override
        {
            Self().offset = offset;
            for (size_t i = 0; i < children_.size(); ++i)
                children_[i]->updateOffset({
                    offset.x + positions_[i].x,
                    offset.y + positions_[i].y,
                });
        }

        [[nodiscard]] constexpr std::span<const std::unique_ptr<Widget>> children()
            const noexcept override
        {
            return children_;
        }

      private:
        std::unique_ptr<MultiChildLayoutDelegate> delegate_;
        std::vector<std::unique_ptr<Widget>> children_;
        std::vector<std::string> ids_;
        std::unordered_map<std::string, size_t> idToIndex_;
        std::vector<Offset> positions_;
    };
    struct CustomMultiChildLayoutBuild
    {
        explicit CustomMultiChildLayoutBuild(std::string key = {}) noexcept
            : key_(std::move(key))
        {
        }

        auto &key(std::string k) noexcept
        {
            key_ = std::move(k);
            return *this;
        }

        auto &renderObject(std::unique_ptr<RenderObject> ro) noexcept
        {
            renderObject_ = std::move(ro);
            return *this;
        }

        auto &delegate(std::unique_ptr<MultiChildLayoutDelegate> d) noexcept
        {
            delegate_ = std::move(d);
            return *this;
        }

        auto &addChild(LayoutIdBuild &lib) noexcept
        {
            ids_.push_back(lib.id_);
            children_.push_back(std::move(lib.child_));
            return *this;
        }

        explicit operator std::unique_ptr<Widget>()
        {
            if (!delegate_)
                throw std::logic_error{"CustomMultiChildLayout requires a delegate"};
            return std::make_unique<CustomMultiChildLayoutWidget>(
                std::move(key_), std::move(renderObject_), std::move(delegate_),
                std::move(children_), std::move(ids_));
        }

      private:
        std::string key_;
        std::unique_ptr<RenderObject> renderObject_;
        std::unique_ptr<MultiChildLayoutDelegate> delegate_;
        std::vector<std::unique_ptr<Widget>> children_;
        std::vector<std::string> ids_;
    };
    static auto CustomMultiChildLayout(std::string key = {}) noexcept // NOLINT
    {
        return CustomMultiChildLayoutBuild{std::move(key)};
    }

    struct RotatedBoxWidget : Widget
    {
        RotatedBoxWidget(std::string key, std::unique_ptr<RenderObject> ro,
                         int quarterTurns, std::unique_ptr<Widget> child) noexcept
            : Widget(std::move(key), std::move(ro)), quarterTurns_{quarterTurns},
              child_{std::move(child)}
        {
        }

        void layout(BoxConstraints c) override
        {
            if (!child_)
            {
                Self().size = c.smallest();
                return;
            }

            const bool odd = (quarterTurns_ % 2) != 0;

            // 奇数 quarterTurns：约束宽高互换
            const BoxConstraints childC =
                odd ? BoxConstraints{c.minHeight(), c.maxHeight(), c.minWidth(),
                                     c.maxWidth()}
                    : c;

            child_->layout(childC);

            // 自身 size = constrain(旋转后 childSize)
            Size s = child_->size;
            if (odd)
                std::swap(s.width, s.height);
            Self().size = c.constrain(s);
        }

        void updateOffset(Offset offset) noexcept override
        {
            Self().offset = offset;
            if (!child_)
                return;

            const int q = ((quarterTurns_ % 4) + 4) % 4;
            const Size cs = child_->size; // 未旋转的 child 尺寸

            Offset p;
            switch (q)
            {
            case 0:
                p = {0.0, 0.0};
                break;
            case 1:
                p = {cs.height, 0.0};
                break;
            case 2:
                p = {cs.width, cs.height};
                break;
            default:
                p = {0.0, cs.width};
                break; // q == 3
            }

            child_->updateOffset({offset.x + p.x, offset.y + p.y});
        }

        [[nodiscard]] constexpr std::span<const std::unique_ptr<Widget>> children()
            const noexcept override
        {
            if (!child_)
                return {};
            return std::span<const std::unique_ptr<Widget>>{&child_, 1};
        }

      private:
        int quarterTurns_;
        std::unique_ptr<Widget> child_;
    };
    struct RotatedBoxBuild
    {
        explicit RotatedBoxBuild(std::string key = {}) noexcept : key_(std::move(key)) {}

        auto &key(std::string k) noexcept
        {
            key_ = std::move(k);
            return *this;
        }
        auto &renderObject(std::unique_ptr<RenderObject> ro) noexcept
        {
            renderObject_ = std::move(ro);
            return *this;
        }
        auto &quarterTurns(int q) noexcept
        {
            quarterTurns_ = q;
            return *this;
        }

        auto &child(std::unique_ptr<Widget> c) noexcept
        {
            child_ = std::move(c);
            return *this;
        }
        auto &child(WidgetBuild auto &&b) noexcept
        {
            return child(
                static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
        }

        explicit operator std::unique_ptr<Widget>()
        {
            return std::make_unique<RotatedBoxWidget>(std::move(key_),
                                                      std::move(renderObject_),
                                                      quarterTurns_, std::move(child_));
        }

      private:
        std::string key_;
        std::unique_ptr<RenderObject> renderObject_;
        int quarterTurns_ = 0;
        std::unique_ptr<Widget> child_;
    };
    static constexpr auto RotatedBox(std::string key = {}) noexcept // NOLINT
    {
        return RotatedBoxBuild{std::move(key)};
    }

    // ===============================================================================================
    // flex 模式 [end]
    // ===============================================================================================
}; // namespace ui_new

// shader_data::Glyph
auto &glyphPool()
{
    using Pool = soa_vector<shader_data::Glyph>;
    static Pool pool{};
    return pool;
}
using GlyphPool = std::remove_reference_t<decltype(glyphPool())>;

// ────────────────────────────────────────────────────────────
// 底层：几何量 → Glyph（纯函数，不分配、不入队）
// ────────────────────────────────────────────────────────────
static constexpr auto make_glyph =
    [](const auto &g, float W, float H, float fontSizePx, float cursorX, float baselineY,
       uint64_t data, uint32_t entity_index, uint32_t hover_fn_id, glm::vec4 color,
       uint32_t modulateFlag) -> shader_data::Glyph {
    // JSON 的 plane_bounds 跟随 atlas.yOrigin：
    //   "top"    -> y 向下（top 为负、bottom 为正）
    //   "bottom" -> y 向上（top 为正、bottom 为负）
    // HarfBuzz 的 offset 是 y 向上；屏幕 y 向下，所以 offset_y 用减。
    const bool y_down = (g.font_ctx->font.atlas.yOrigin == "top");
    const float ydir = y_down ? 1.0f : -1.0f;
    const float ox = static_cast<float>(g.offset_x) * fontSizePx;
    const float oy = static_cast<float>(g.offset_y) * fontSizePx;
    float leftPx = cursorX + ox + static_cast<float>(g.plane_bounds.left) * fontSizePx;
    float rightPx = cursorX + ox + static_cast<float>(g.plane_bounds.right) * fontSizePx;
    float topPx =
        baselineY - oy + ydir * static_cast<float>(g.plane_bounds.top) * fontSizePx;
    float bottomPx =
        baselineY - oy + ydir * static_cast<float>(g.plane_bounds.bottom) * fontSizePx;
    float w = rightPx - leftPx;
    float h = bottomPx - topPx;

    float cx = ((leftPx + w * 0.5f) / W) * 2.0f - 1.0f;
    float cy = ((topPx + h * 0.5f) / H) * 2.0f - 1.0f;

    UvTransform uv;
    // V 轴向下增长；quad 的 v=0 在顶部，所以 scale.y = bottom - top（正），
    // offset.y = top。原来用 (top-bottom, bottom) 是配合负高度的几何翻转，
    // 现在几何 h 已为正，UV 必须同步正过来，否则字是倒的。
    uv.scale = glm::vec2(static_cast<float>(g.uv_bounds.right - g.uv_bounds.left),
                         static_cast<float>(g.uv_bounds.bottom - g.uv_bounds.top));
    uv.offset = glm::vec2(static_cast<float>(g.uv_bounds.left),
                          static_cast<float>(g.uv_bounds.top));

    shader_data::Glyph glyph{};
    glyph.data = data;
    glyph.entity_index = entity_index;
    glyph.textureIndex = g.font_ctx->bind.texture_index;
    glyph.samplerIndex = g.font_ctx->bind.sampler_index;
    glyph.fontType = static_cast<uint32_t>(g.font_ctx->type);
    glyph.pxRange =
        static_cast<float>(g.font_ctx->font.atlas.distanceRange.value_or(0.0));
    glyph.modulateFlag = modulateFlag;
    glyph.color = color;
    glyph.model =
        glm::translate(glm::mat4(1.0f), glm::vec3(cx, cy, 0.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(w / W * 2.0f, h / H * 2.0f, 1.0f));
    glyph.uvTransform = uv;
    glyph.hover_fn = hover_fn_id;
    return glyph;
};
// ────────────────────────────────────────────────────────────
// 中层：一个字形 → 分配 + 入队（两块共用）
// 空字形（空格等 plane_bounds 全零）直接忽略，返回 false
// ────────────────────────────────────────────────────────────
static constexpr auto gen_glyph = [](const auto &g, float W, float H, float fontSizePx,
                                     float cursorX, float baselineY, uint64_t data,
                                     uint32_t hover_fn_id, glm::vec4 color,
                                     uint32_t modulateFlag) {
    auto entity_index = glyphPool().allocate();
    shader_data::Glyph glyph = make_glyph(g, W, H, fontSizePx, cursorX, baselineY, data,
                                          entity_index, hover_fn_id, color, modulateFlag);
    auto proxy = glyphPool().make_soa_value(entity_index, glyph);
    return std::make_pair(std::move(glyph), std::move(proxy));
};

auto &uiRectPool()
{
    using Pool = soa_vector<shader_data::UiRect>;
    static Pool pool{};
    return pool;
}
auto &rectanglePool()
{
    using Pool = soa_vector<shader_data::Rectangle>;
    static Pool pool{};
    return pool;
}

// NOTE: hover在帧稳定才触发，noexcept 没有问题
static constexpr uint64_t getShaderData(object_key key) noexcept
{
    using namespace shader_data;
    auto [object_type, entity_index] = key;
    switch (object_type)
    {
    case Glyph::type_id:
        return glyphPool().template get<"data">(entity_index);
    case UiRect::type_id:
        return uiRectPool().template get<"data">(entity_index);
    case Rectangle::type_id:
        return rectanglePool().template get<"data">(entity_index);
    }
    fatal_error(std::format("no handle for entity_index: {}", entity_index));
}

//diff: [test_dod24.cpp] end [替换旧的布局，并更新录制的算法]

//diff: [test_dod22] end

// ═══════════════════════════════════════════════════════════════════════
// 【原样保留】事件系统
// ═══════════════════════════════════════════════════════════════════════
using mcs::vulkan::meta::class_utils;
template <typename... Extra>
struct type_erased_invoker
{
    static consteval std::optional<std::meta::info> find_function(std::meta::info cls,
                                                                  static_string name)
    {
        return class_utils::find_member(
            cls, name,
            [](std::meta::info m) -> bool {
                return std::meta::is_function(m) || std::meta::is_function_template(m);
            },
            std::meta::access_context::current());
    }
    static consteval std::optional<std::meta::info> find_nonstatic_data_member(
        std::meta::info cls, static_string name)
    {
        return class_utils::find_member(
            cls, name,
            [](std::meta::info m) -> bool {
                return std::meta::is_nonstatic_data_member(m);
            },
            std::meta::access_context::current());
    }

    using invoke_type = void (*)(void *ptr, const void *type, void *data,
                                 Extra...) noexcept;
    constexpr void invoke(void *ptr, const void *type, void *data, Extra... e) noexcept
    {
        assert(invoke_ != nullptr);
        invoke_(ptr, type, data, e...);
    }

    type_erased_invoker() = default;
    constexpr explicit type_erased_invoker(invoke_type invoke) noexcept : invoke_{invoke}
    {
    }

    template <class Obj, static_string name>
    struct invoke_type_info
    {
        constexpr static bool in_function = find_function(^^Obj, name).has_value();
        constexpr static bool in_data_member =
            find_nonstatic_data_member(^^Obj, name).has_value();

        template <class... Args>
        static consteval auto can_noexcept_invoke()
        {
            // NOTE: 默认反射的是当前的类型，而不是从父类继承下来的
            if constexpr (requires(Obj &&obj, Args &&...args) {
                              {
                                  obj.template invoke<name>(std::forward<Args>(args)...)
                              } noexcept;
                          })
                return true;
            else if constexpr (in_function || in_data_member)
            {
                constexpr auto m = in_function ? *find_function(^^Obj, name)
                                               : *find_nonstatic_data_member(^^Obj, name);
                return requires(Obj &&obj, Args &&...args) {
                    { obj.[:m:](std::forward<Args>(args)...) } noexcept;
                } || requires(Obj &&obj, Args &&...args) {
                    { obj.template[:m:](std::forward<Args>(args)...) } noexcept;
                };
            }
            else
                return false;
        }
    };

    template <static_string name, typename Obj, typename... Ts>
    static constexpr void dispatch_invoke(Obj *ptr, std::tuple<Ts...> *, const void *type,
                                          void *p, Extra &&...e) noexcept
        requires(
            (invoke_type_info<Obj, name>::template can_noexcept_invoke<Ts, Extra...>() ||
             ...))
    {
        using Info = invoke_type_info<Obj, name>;
        using mcs::vulkan::runtime_type_v;
        auto &obj = *ptr;
        auto invoke = [&]<typename... Args>(Args &&...args) noexcept {
            if constexpr (requires() {
                              {
                                  obj.template invoke<name>(std::forward<Args>(args)...)
                              } noexcept;
                          })
                obj.template invoke<name>(std::forward<Args>(args)...);
            else
            {
                constexpr auto m = Info::in_function
                                       ? *find_function(^^Obj, name)
                                       : *find_nonstatic_data_member(^^Obj, name);
                if constexpr (requires() {
                                  { obj.[:m:](std::forward<Args>(args)...) } noexcept;
                              })
                    obj.[:m:](std::forward<Args>(args)...);
                else
                    obj.template[:m:](std::forward<Args>(args)...);
            }
        };
        ((Info::template can_noexcept_invoke<Ts, Extra...>() &&
          type == runtime_type_v<Ts> &&
          (invoke(*static_cast<Ts *>(p), std::forward<Extra>(e)...), true)) ||
         ...);
    }

    template <static_string callable_name, typename data_tuple, class T>
    static constexpr auto build() noexcept
    {
        using object_type = std::remove_cvref_t<T>;
        return type_erased_invoker{invoke_type{
            [](void *ptr, const void *type, void *data, Extra... args) noexcept {
                auto *obj = static_cast<object_type *>(ptr);
                dispatch_invoke<callable_name, object_type>(
                    obj, static_cast<data_tuple *>(nullptr), type, data, args...);
            }}};
    }

  private:
    invoke_type invoke_{};
};

template <class Handler>
class multicast_sink
{
  public:
    using index_t = std::uint32_t;
    static constexpr index_t npos = ~index_t{0};

    struct handle
    {
        index_t slot = npos;
        std::uint64_t gen = 0;
        [[nodiscard]] constexpr bool valid() const noexcept
        {
            return slot != npos;
        }
        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return valid();
        }
    };

    class scoped_binding
    {
        multicast_sink *owner_ = nullptr;
        handle h_{};

      public:
        scoped_binding() noexcept = default;

        scoped_binding(multicast_sink &o, handle hh) noexcept : owner_(&o), h_(hh) {}

        constexpr scoped_binding(scoped_binding &&other) noexcept
            : owner_(std::exchange(other.owner_, nullptr)),
              h_(std::exchange(other.h_, {}))
        {
        }
        constexpr scoped_binding &operator=(scoped_binding &&other) noexcept
        {
            if (this != &other)
            {
                release();
                owner_ = std::exchange(other.owner_, nullptr);
                h_ = std::exchange(other.h_, {});
            }
            return *this;
        }
        scoped_binding(const scoped_binding &) = delete;
        scoped_binding &operator=(const scoped_binding &) = delete;

        constexpr ~scoped_binding() noexcept
        {
            release();
        }

        constexpr void release() noexcept
        {
            if (owner_ && h_.valid())
                owner_->unbind(h_);
            owner_ = nullptr;
            h_ = {};
        }
        constexpr void activate() noexcept
        {
            if (owner_ && h_.valid())
                owner_->activate(h_);
        }
        constexpr void deactivate() noexcept
        {
            if (owner_ && h_.valid())
                owner_->deactivate(h_);
        }
        constexpr bool valid() const noexcept
        {
            return owner_ && h_.valid();
        }
        constexpr handle get_handle() const noexcept
        {
            return h_;
        }
    };

    constexpr multicast_sink() = default;
    multicast_sink(const multicast_sink &) = delete;
    multicast_sink &operator=(const multicast_sink &) = delete;

    // 可能抛 bad_alloc（slots_ realloc）
    constexpr handle bind(void *obj, Handler handler)
    {
        index_t idx;
        if (distributing_ || free_head_ == npos)
        {
            idx = static_cast<index_t>(slots_.size());
            slot s;
            s.obj = obj;
            s.handler = handler;
            s.gen = 1;
            s.occupied = true;
            s.active = true;
            slots_.push_back(s);
        }
        else
        {
            idx = free_head_;
            auto &s = slots_[idx];
            free_head_ = s.free_next;
            s.free_next = npos;
            s.obj = obj;
            s.handler = handler;
            s.gen += 1;
            s.occupied = true;
            s.active = true;
        }
        slots_[idx].active_pos = static_cast<index_t>(active_list_.size());
        active_list_.push_back(idx);
        return {idx, slots_[idx].gen};
    }

    // 分发外：swap-remove active_list_ + 进 free_head_。分发中：只标 occupied。
    constexpr void unbind(handle h) noexcept
    {
        if (h.slot >= slots_.size())
            return;
        auto &s = slots_[h.slot];
        if (!s.occupied || s.gen != h.gen)
            return;
        s.occupied = false;
        s.obj = nullptr;
        s.handler = {};
        s.free_next = free_head_;
        free_head_ = h.slot;
        if (!distributing_)
            swap_remove_active_(h.slot);
        else
            dirty_ = true;
    }

    // ★ attach 只做 sink 层封装：obj + handler，无类型擦除
    scoped_binding attach(void *obj, Handler handler)
    {
        return scoped_binding{*this, bind(obj, std::move(handler))};
    }
    void attach(scoped_binding &out, void *obj, Handler handler)
    {
        out = attach(obj, std::move(handler));
    }

    constexpr void activate(handle h) noexcept
    {
        if (h.slot >= slots_.size())
            return;
        auto &s = slots_[h.slot];
        if (!s.occupied || s.gen != h.gen)
            return;
        if (s.active)
            return;
        s.active = true;
        if (s.active_pos == npos)
        {
            s.active_pos = static_cast<index_t>(active_list_.size());
            active_list_.push_back(h.slot);
        }
    }

    constexpr void deactivate(handle h) noexcept
    {
        if (h.slot >= slots_.size())
            return;
        auto &s = slots_[h.slot];
        if (!s.occupied || s.gen != h.gen)
            return;
        if (!s.active)
            return;
        s.active = false;
        if (!distributing_)
            swap_remove_active_(h.slot);
        else
            dirty_ = true;
    }

    template <class... Args>
    constexpr void dispatch(Args &&...args) noexcept
    {
        const bool outer = !distributing_;
        if (outer)
        {
            distributing_ = true;
            n_snapshot_ = static_cast<index_t>(active_list_.size());
        }
        for (index_t i = 0; i < n_snapshot_; ++i)
        {
            index_t idx = active_list_[i];
            auto &s = slots_[idx];
            if (!s.occupied || !s.active)
                continue;
            s.handler.invoke(s.obj, std::forward<Args>(args)...);
        }
        if (outer)
        {
            distributing_ = false;
            if (dirty_)
            {
                compact_active_();
                dirty_ = false;
            }
        }
    }

    [[nodiscard]] constexpr const Handler *handler_of(handle h) const noexcept
    {
        if (h.slot >= slots_.size())
            return nullptr;
        const auto &s = slots_[h.slot];
        if (!s.occupied || s.gen != h.gen)
            return nullptr;
        return &s.handler;
    }
    [[nodiscard]] constexpr void *context_of(handle h) const noexcept
    {
        if (h.slot >= slots_.size())
            return nullptr;
        const auto &s = slots_[h.slot];
        if (!s.occupied || s.gen != h.gen)
            return nullptr;
        return s.obj;
    }

    template <class... Args>
    constexpr void invoke(handle h, Args &&...args) const noexcept
    {
        if (h.slot >= slots_.size())
            return;
        const auto &s = slots_[h.slot];
        if (!s.occupied || s.gen != h.gen)
            return;
        s.handler.invoke(s.obj, std::forward<Args>(args)...);
    }

    [[nodiscard]] constexpr std::size_t slot_count() const noexcept
    {
        return slots_.size();
    }
    [[nodiscard]] constexpr std::size_t live_count() const noexcept
    {
        return active_list_.size();
    }
    [[nodiscard]] constexpr std::size_t active_list_size() const noexcept
    {
        return active_list_.size();
    }

    constexpr void reserve(std::size_t n)
    {
        slots_.reserve(n);
    }

  private:
    struct slot
    {
        void *obj = nullptr;
        Handler handler{};
        index_t free_next = npos;
        index_t active_pos = npos;
        std::uint64_t gen = 0;
        bool occupied = false;
        bool active = true;
    };

    std::vector<slot> slots_;
    index_t free_head_ = npos;
    std::vector<index_t> active_list_;
    index_t n_snapshot_ = 0;
    bool distributing_ = false;
    bool dirty_ = false;

    constexpr void swap_remove_active_(index_t idx) noexcept
    {
        auto &s = slots_[idx];
        index_t p = s.active_pos;
        if (p == npos)
            return;
        index_t last = static_cast<index_t>(active_list_.size() - 1);
        if (p != last)
        {
            index_t moved = active_list_[last];
            active_list_[p] = moved;
            slots_[moved].active_pos = p;
        }
        active_list_.pop_back();
        s.active_pos = npos;
    }

    constexpr void compact_active_() noexcept
    {
        index_t write = 0;
        for (index_t read = 0; read < active_list_.size(); ++read)
        {
            index_t idx = active_list_[read];
            auto &s = slots_[idx];
            if (s.occupied && s.active)
            {
                active_list_[write] = idx;
                s.active_pos = write;
                ++write;
            }
            else
            {
                s.active_pos = npos;
            }
        }
        active_list_.resize(write);
    }
};
struct glfw_input_translation
{
    using time_point_type = std::chrono::steady_clock::time_point;

    using keyboard_event = mcs::vulkan::event::keyboard_event;
    using mousebutton_event = mcs::vulkan::event::mousebutton_event;
    using scroll_event = mcs::vulkan::event::scroll_event;
    using position2d_event = mcs::vulkan::event::position2d_event;
    using cursor_enter_event = mcs::vulkan::event::cursor_enter_event;

    using char_event = mcs::vulkan::event::char_event;
    using drop_event = mcs::vulkan::event::drop_event;

    using window_focus_event = mcs::vulkan::event::window_focus_event;
    using window_pos_event = mcs::vulkan::event::window_pos_event;
    using window_size_event = mcs::vulkan::event::window_size_event;
    using window_iconify_event = mcs::vulkan::event::window_iconify_event;
    using window_maximize_event = mcs::vulkan::event::window_maximize_event;
    using framebuffer_size_event = mcs::vulkan::event::framebuffer_size_event;

    using data_tuple =
        std::tuple<keyboard_event, mousebutton_event, scroll_event, position2d_event,
                   cursor_enter_event, char_event, drop_event, window_focus_event,
                   window_pos_event, window_size_event, window_iconify_event,
                   window_maximize_event, framebuffer_size_event>;
    using proxy_class_type = type_erased_invoker<>;

    template <typename event_type>
    struct event_dispatcher
    {
        constexpr static auto &instance() noexcept
        {
            namespace event = mcs::vulkan::event;
            if constexpr (std::is_same_v<keyboard_event, event_type>)
                return event::keyboard_event_dispatcher::instance();
            else if constexpr (std::is_same_v<mousebutton_event, event_type>)
                return event::mousebutton_event_dispatcher::instance();
            else if constexpr (std::is_same_v<scroll_event, event_type>)
                return event::scroll_event_dispatcher::instance();
            else if constexpr (std::is_same_v<position2d_event, event_type>)
                return event::position2d_event_dispatcher::instance();
            else if constexpr (std::is_same_v<cursor_enter_event, event_type>)
                return event::cursor_enter_event_dispatcher::instance();
            else if constexpr (std::is_same_v<char_event, event_type>)
                return event::char_event_dispatcher::instance();
            else if constexpr (std::is_same_v<drop_event, event_type>)
                return event::drop_event_dispatcher::instance();
            else if constexpr (std::is_same_v<window_focus_event, event_type>)
                return event::window_focus_event_dispatcher::instance();
            else if constexpr (std::is_same_v<window_pos_event, event_type>)
                return event::window_pos_event_dispatcher::instance();
            else if constexpr (std::is_same_v<window_size_event, event_type>)
                return event::window_size_event_dispatcher::instance();
            else if constexpr (std::is_same_v<window_iconify_event, event_type>)
                return event::window_iconify_event_dispatcher::instance();
            else if constexpr (std::is_same_v<window_maximize_event, event_type>)
                return event::window_maximize_event_dispatcher::instance();
            else if constexpr (std::is_same_v<framebuffer_size_event, event_type>)
                return event::framebuffer_size_event_dispatcher::instance();
            else
                static_assert(false, "not find dispatcher");
        }
    };

    template <typename... Events>
    static constexpr void subscribe_all(void *ptr, std::tuple<Events...> *)
    {
        (event_dispatcher<Events>::instance().subscribe(ptr, &forwarding<Events>), ...);
    }
    template <typename... Events>
    static constexpr void unsubscribe_all(void *ptr, std::tuple<Events...> *) noexcept
    {
        static_assert((noexcept(event_dispatcher<Events>::instance().unsubscribe(
                           ptr, &forwarding<Events>)) &&
                       ...),
                      "~class() need noexcept is true");
        (event_dispatcher<Events>::instance().unsubscribe(ptr, &forwarding<Events>), ...);
    }
    constexpr explicit glfw_input_translation()
    {
        subscribe_all(this, static_cast<data_tuple *>(nullptr));
    }
    constexpr ~glfw_input_translation() noexcept
    {
        unsubscribe_all(this, static_cast<data_tuple *>(nullptr));
    }
    glfw_input_translation(const glfw_input_translation &) = delete;
    glfw_input_translation(glfw_input_translation &&) = delete;
    glfw_input_translation &operator=(const glfw_input_translation &) = delete;
    glfw_input_translation &operator=(glfw_input_translation &&) = delete;

    using multicast_t = multicast_sink<proxy_class_type>;
    using handle = multicast_t::handle;
    using scoped_binding = multicast_t::scoped_binding;

    template <typename event_type>
    constexpr static void forwarding(void *self, event_type e) noexcept
    {
        auto *impl = static_cast<glfw_input_translation *>(self);
        using mcs::vulkan::runtime_type_v;
        impl->sinks_.dispatch(runtime_type_v<event_type>, &e);
    }

    // ★ 类型擦除在这里发生：Obj → (void*, proxy_class_type)
    template <typename Obj>
        requires(requires() {
            proxy_class_type::build<"input_event", data_tuple, Obj>();
        })
    constexpr scoped_binding attach(Obj &obj)
    {
        return sinks_.attach(&obj,
                             proxy_class_type::build<"input_event", data_tuple, Obj>());
    }

    template <typename Obj>
        requires(requires() {
            proxy_class_type::build<"input_event", data_tuple, Obj>();
        })
    constexpr void attach(scoped_binding &out, Obj &obj)
    {
        out = attach(obj);
    }
    constexpr void reserve(std::size_t n)
    {
        sinks_.reserve(n);
    }

  private:
    multicast_t sinks_;
};

namespace intent
{
    using mcs::vulkan::meta::static_string;
    using time_point = std::chrono::milliseconds;

    using mcs::vulkan::meta::template_args;

    // 下标类型，一处可换。
    //   约定：**位置 / 编号 / 扫描计数**用它；**长度与容量**（kinds.size()、
    //   数组 extent、位图字数）仍用 std::size_t —— 后者是"多少个"，不是"第几个"。
    //   想压窄（std::uint32_t / std::uint16_t）只改这一行；改完还写着
    //   std::size_t 的地方，恰好就是"长度 → 下标"需要复核的边界。
    using index_t = std::size_t;

    // ═══════════════════════════════════════════════════════════════════
    // 0. 词汇
    // ═══════════════════════════════════════════════════════════════════

    // 识别器对仲裁层唯一能说的话
    //
    //   idle  —— 我没有 claim
    //   hold  —— 我有一个 claim，但这一帧没有事件（参与压制，不发射）
    //   ready —— 我有一个 claim，这一帧有事件（可以被允许发射）
    //
    // ready 不是“交互结束”，而是“这一帧有一个事件要发”。一个长 claim
    // （drag）可以在 hold/ready 之间来回很多次；非流式只是 ready 一次的特化。
    //
    // 没有 done / aborted / reset：帧不改变 claim 时，识别器把当前状态
    // 原样再说一遍；自己放弃就回 idle（放弃不是判决，不产生事件）。
    enum class claim_state : std::uint8_t
    {
        idle,
        hold,
        ready
    };

    enum class verdict : std::uint8_t
    {
        accepted,  // 赢
        superseded // 让位
    };

    // ═══════════════════════════════════════════════════════════════════
    // 0.1 识别器 ↔ 裁决器：四个方法，两个方向
    //
    //   识别器 → session（每次 feed / tick 的返回值）：
    //     ① idle / hold / ready —— 我现在是什么
    //     ② 吃哪种帧            —— 编译期由 feed 重载决定；law_of 用它算 accepted-frame 集合
    //
    //   arbiter → 识别器（裁决之后，只可能调一个）：
    //     ③ win()  —— 允许发射这一帧的事件；是否结束 claim 由识别器决定
    //     ④ lose() —— 更具体的赢了：丢掉并结束这条 claim
    //
    //   裁决器本身只看到 report[] 和静态表；它不调用识别器。
    //   调用 win/lose 的是 session。
    //
    //   没有 reset。识别器的内部状态只由它自己写；session 没有它的指针，
    //   也不在裁决之后替它"清一下"。win/lose 是消息，不是复位。
    //
    //   ── 识别器的义务（裁决器安全/活性证明依赖这六条）──────────────
    //   R1 一槽一条：同一时刻一个 kind 至多一条活着的 claim。
    //   R2 单调：ready 的 claim 在收到 win/lose 之前不能自己降回 idle/hold。
    //   R3 收到 win/lose 后，该 claim 必须结束；同一交互不得再次申报。
    //   R4 feed/tick 只读自己的状态、无其它副作用、noexcept；tick 只推进时间。
    //   R5 及时：一次交互从它的第一帧起就必须有 claim（hold），不能等
    //      更低优先级的解释已经赢了之后再补报。
    //   R6 事件消费：win 之后不得对同一个事件再次 ready；下一个 ready 必须
    //      是新事件。长 claim 可以多次 ready，但每次都是不同事件。
    // ═══════════════════════════════════════════════════════════════════
    namespace recognizer
    {
        // 识别器只做三件事：吃帧、过时间、投递/退休。
        // 裁决器只吃 claim_state 数组，不认识 Frame、payload、span。
        template <class M, class Self, class Frame>
        concept can_feed = requires(M &m, Self &self, const Frame &f, time_point t) {
            { m.feed(self, f, t) } noexcept -> std::same_as<claim_state>;
        };
    } // namespace recognizer

    // 一条观测记录（定长、可拷贝、不分配）
    struct observation
    {
        std::string_view name{};
        verdict v{};
        time_point at{}; // 裁决时刻
    };

    // ── 观测：只写不读，不参与判决。整块可换 ──
    //    trace：定长记录，满了计数丢弃（不覆盖、不分配）
    struct trace
    {
        static constexpr std::size_t capacity = 64;
        std::array<observation, capacity> buf{};
        std::size_t n = 0, dropped_n = 0;

        constexpr void push(const observation &e) noexcept
        {
            if (n < capacity)
                buf[n++] = e;
            else
                ++dropped_n;
        }
        constexpr std::span<const observation> events() const noexcept
        {
            return {buf.data(), n};
        }
        constexpr void clear() noexcept
        {
            n = 0;
        }
        constexpr std::size_t dropped() const noexcept
        {
            return dropped_n;
        }
    };

    // 无观测版本：编译期被 if constexpr 剪掉，不产生任何记录。
    struct null_trace
    {
    };

    // ═══════════════════════════════════════════════════════════════════
    // 1. plan：编译期的声明表（L1 同态律）
    //      kinds  : 每片叶子的名字 / 回调名
    //      awaits : (i,j) —— i 是 j 的抽象版
    //      excl   : (i,j) —— i 与 j 不能共存
    // ═══════════════════════════════════════════════════════════════════
    struct kind_desc
    {
        static_string name{};    // 日志标签：不参与查找/分发，不要求唯一
        static_string fn_name{}; // 回调的名字：invoke<fn_name>，可以多个 kind 共用一个

        friend constexpr bool operator==(const kind_desc &, const kind_desc &) = default;
    };
    struct plan_data
    {
        std::vector<kind_desc> kinds;
        // 下标一律 index_t：它只可能来自 kinds.size() 或循环计数，
        // 负数不是"约定上不该有"，而是根本产生不出来 —— 也没有 -1 哨兵的需求。
        std::vector<std::pair<index_t, index_t>> awaits;
        std::vector<std::pair<index_t, index_t>> excl;

        // 拼接：b 追加到a, b 的所有编号 + base （换坐标系）
        friend consteval plan_data operator+(plan_data a, const plan_data &b)
        {
            const index_t base = static_cast<index_t>(a.kinds.size());
            a.kinds.append_range(b.kinds);
            for (auto [i, j] : b.awaits)
                a.awaits.push_back({i + base, j + base});
            for (auto [i, j] : b.excl)
                a.excl.push_back({i + base, j + base});
            return a;
        }
    };

    // 叶子 = 纯类型：两个名字 + 状态机类型。
    //   它不持有状态机：状态机住在 ui 的扁平元组里。
    //
    //   两个名字各管一摊，不耦合：
    //     Name   —— kind 的身份。进日志、蓝图、报错；要求唯一（它是"谁"）。
    //     FnName —— 回调的名字。sink 只会做 invoke<FnName>(payload)；
    //               多个 kind 可以共用同一个回调。
    //   “能吃哪些帧”不再手写：由 feed 重载在 law_of 里编译期算出来，
    //   关系两端能不能相遇也由这个集合决定，不靠 channel 字符串。
    template <class Fsm>
    struct leaf
    {
        using protocol = Fsm; // 叶子协议：我的状态机类型（反射靠它认出叶子）
        static constexpr auto name = Fsm::name;       // kind 的身份
        static constexpr auto fn_name = Fsm::fn_name; // 回调的名字（invoke<fn_name>）
        using consume_t = Fsm::consume_t;

        static consteval plan_data plan()
        {
            return plan_data{.kinds = {{name, fn_name}}, .awaits = {}, .excl = {}};
        }
    };

    // 并行：不加任何关系
    template <class... Rs>
    struct all_of
    {
        using children_t = std::tuple<Rs...>; // 组合子协议（仅类型）

        static consteval plan_data plan()
        {
            plan_data p;
            ((p = p + Rs::plan()), ...);
            return p;
        }
    };

    // 互斥：任意两个不同子树之间填满 excl
    template <class... Rs>
    struct one_of
    {
        using children_t = std::tuple<Rs...>;

        static consteval plan_data plan()
        {
            constexpr std::size_t K = sizeof...(Rs);
            std::array<plan_data, K> parts{}; //收集每个子树的 plan_data
            index_t k = 0;
            ((parts[k++] = Rs::plan()), ...);

            // 前缀和 off 与拼接 p
            plan_data p;
            std::array<index_t, K + 1> off{};
            for (std::size_t i = 0; i < K; ++i)
            {
                off[i + 1] = off[i] + static_cast<index_t>(parts[i].kinds.size());
                p = p + parts[i];
            }
            /*
            外层 (a, b)：枚举所有不同的子树对，并且强制 a < b（按声明序，a 在前）。
            内层 (x, y)：x 遍历子树 a 的所有叶子下标，y 遍历子树 b 的所有叶子下标。
            把每一对 (x, y) 加入 excl
            */
            // NOTE: 任意两个不同子树之间的叶子两两互斥
            for (index_t a = 0; a < K; ++a)
                for (index_t b = a + 1; b < K; ++b)
                    for (index_t x = off[a]; x < off[a + 1]; ++x)
                        for (index_t y = off[b]; y < off[b + 1]; ++y)
                            p.excl.push_back({x, y});
            return p;
        }
    };

    // 细化：A 里每片叶子都等 B 里每片叶子的失败
    template <class A, class B>
    struct refines
    {
        using children_t = std::tuple<A, B>;

        static consteval plan_data plan()
        {
            auto pa = A::plan();
            auto pb = B::plan();
            const index_t na = static_cast<index_t>(pa.kinds.size());
            auto p = pa + pb; // NOTE: 先简单拼接，再在下面添加边
            // NOTE: A 的元素  全部 等 B 的元素
            for (index_t i = 0; i < na; ++i)
                for (index_t j = na; j < static_cast<index_t>(p.kinds.size()); ++j)
                    p.awaits.push_back({i, j});
            return p;
        }
    };

    // ═══════════════════════════════════════════════════════════════════
    // 2. validate：坏结构编译不过（00-goal 第 3 条）
    //    ① 无自环 ② 细化×互斥不矛盾 ③ awaits 无环
    //
    //    “关系两端能不能相遇”不在这里：那要看 feed 重载，是
    //    law_of 里按 accepted-frame 集合做的编译期检查。
    //    name 只是日志标签，不参与查找/分发，所以**不要求唯一**；
    //    fn_name 是回调名，也不查重 —— 多个 kind 共用一个回调是合法用法。
    //    真正的结构约束是“同一个 FSM 类型不能在同一棵树里出现两次”，
    //    那个在 shape 里查。
    // ═══════════════════════════════════════════════════════════════════
    consteval void validate(const plan_data &p)
    {
        const std::size_t n = p.kinds.size(); // 多少个 kind：这是长度

        for (auto [i, j] : p.awaits)
            if (i == j)
                throw "awaits self-loop";
        for (auto [i, j] : p.excl)
            if (i == j)
                throw "excl self-loop";
        for (auto [i, j] : p.awaits)
            for (auto [x, y] : p.excl)
                if ((i == x && j == y) || (i == y && j == x))
                    throw "refines x excl conflict";

        // awaits 无环检查（拓扑排序 / Kahn）。
        //
        // 语义：awaits 里的 (i, j) 读作「i 在等 j」。
        // 有环 ⇔ 存在一组节点互相等待（死锁），细化关系不成立 → throw。
        //
        // 做法（三句话）：
        //   1. indeg[i] = 「i 还欠几个人没等到」（不是图论入度，名字别较真）
        //   2. 反复找 indeg == 0 的节点（谁也不欠 → 自由），完成它，
        //      再让所有「在等它」的节点少欠一个（--indeg）
        //   3. 某一轮一个自由的都没找到 → 剩下的互相等 → 有环
        //
        // 复杂度 O(n² · |awaits|)：每完成一个节点就重扫整张 awaits 表。
        // max_kinds = 64 的注释里说「爆点在 validate 的 Kahn」就是指这里。
        std::vector<std::size_t> indeg(n, 0); // indeg[i] = i 还欠几个"等"
        for (auto [i, j] : p.awaits)
            ++indeg[i]; // i 等 j：i 欠一个

        std::vector<bool> done(n, false);     // done[i]  = i 是否已完成
        for (std::size_t left = n; left > 0;) // left = 还没完成的节点数
        {
            bool prog = false; // 本轮是否至少完成了一个
            for (index_t i = 0; i < n; ++i)
                if (!done[i] && indeg[i] == 0) // 不欠任何人了 → 自由
                {
                    done[i] = true;
                    --left;
                    prog = true;
                    // 完成 i 之后，所有"在等 i"的 x 都少欠一个。
                    // (x, y) 且 y == i 就是「x 在等 i」。
                    for (auto [x, y] : p.awaits)
                        if (y == i)
                            --indeg[x];
                    // 注意：内层 for 不 break，本轮内新变自由的节点会顺手处理。
                }
            if (!prog) // 一轮下来没人自由 → 死锁 → 有环
                throw "refines cycle";
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // 3. bset：一张位图，也是这套语义里唯一的"集合"
    //
    //    ── 位 i 的含义由**使用位置**决定，不在类型里 ──
    //      law::beats[i]   第 j 位 = "i 打得赢 j"      （行 = 我赢谁）
    //      live          第 i 位 = "第 i 个 kind 有活着的 claim"
    //      ready         第 i 位 = "本拍有事件"
    //      advance       第 i 位 = "本拍被这一帧推进过"
    //
    //    ── 为什么是它：裁决的每一步都是集合运算，一一对应 ──
    //      blocked = ∪ 行(活着的)        →  for j: blocked |= beats[j]
    //      win     = ready ∖ blocked     →  ready & ~blocked
    //      lose    = ∪ 行(赢家) ∩ live   →  for i: lose |= beats[i]; lose &= live
    //    "谁赢 i"没有单独存：用行并回答（∪行(j) 就是"能赢这些人的全体"），
    //    所以 blocked 只需要行，不需要转置表。
    //
    //    ── 关于 64：S 没有 64 这个上限 ──
    //      位数 = ⌈S/64⌉ 个字，64 只是一个字的宽度（S=70 就是两个字，S=6 是一个字）。
    //      代价是平方的：beats 占 S×⌈S/64⌉×8 字节；每帧 blocked 要 S×⌈S/64⌉ 次字或；
    //      编译期 closure 是 S²×⌈S/64⌉。先撞墙的其实不是矩阵，是 validate 的 Kahn
    //      （它每个节点重扫一遍 awaits 列表；S=200 会撞 GCC 的 constexpr 运算上限）。
    //
    //    ── 约定（改代码时请保持）──
    //      · 只由 set/clr 产生，合法区间永远是 [0,S)
    //      · ~ 只在 [0,S) 内闭合（尾字抹零）⇒ 任何 x & ~y 都不需要前提
    //      · 判空用 none()；不要用 !has(0) 代替
    //      · | 是并、& 是交、差集写 x & ~y（不是 x ∖ y）
    // ═══════════════════════════════════════════════════════════════════
    template <std::size_t S>
    struct bset
    {
        static constexpr std::size_t W = (S + 63) / 64;
        std::array<std::uint64_t, W> w{};

        constexpr void set(index_t i) noexcept
        {
            w[i / 64] |= std::uint64_t{1} << (i % 64);
        }
        constexpr void clr(index_t i) noexcept
        {
            w[i / 64] &= ~(std::uint64_t{1} << (i % 64));
        }
        // "这个集合里有 i 吗"：读一位，不写任何东西。
        // 同一个函数在三种角色上意思不同，由上下文给：
        //   law::beats[i].has(j)  → i 打得赢 j 吗
        //   live.has(i)           → 第 i 个 kind 有活着的 claim 吗
        //   advance.has(i)        → 本帧推进了这一列吗
        constexpr bool has(index_t i) const noexcept
        {
            return (w[i / 64] >> (i % 64)) & 1;
        }
        // 只遍历置位：O(popcount)，不是 O(S)
        template <class Fn>
        constexpr void for_each(Fn &&f) const noexcept
        {
            for (std::size_t k = 0; k < W; ++k)
            {
                auto x = w[k];
                while (x)
                {
                    const auto b = std::countr_zero(x);
                    const auto i = static_cast<index_t>(k * 64 + b);
                    if (i < static_cast<index_t>(S))
                        f(i);
                    x &= x - 1;
                }
            }
        }
        // 空吗？"没人能赢"、"没有结论"、"表清空" 都用它
        constexpr bool none() const noexcept
        {
            for (auto x : w)
                if (x)
                    return false;
            return true;
        }
        constexpr bset &operator|=(const bset &b) noexcept
        {
            for (std::size_t k = 0; k < W; ++k)
                w[k] |= b.w[k];
            return *this;
        }
        constexpr bset &operator&=(const bset &b) noexcept
        {
            for (std::size_t k = 0; k < W; ++k)
                w[k] &= b.w[k];
            return *this;
        }
        friend constexpr bset operator|(bset a, const bset &b) noexcept
        {
            return a |= b;
        }
        friend constexpr bset operator&(bset a, const bset &b) noexcept
        {
            return a &= b;
        }
        friend constexpr bset operator~(bset a) noexcept
        {
            for (std::size_t k = 0; k < W; ++k)
                a.w[k] = ~a.w[k];
            // 补集只在 [0,S) 里闭合：高位一律抹掉，
            // 调用方就不用替它记着"左边那个操作数是干净的"
            if constexpr (S % 64 != 0)
                a.w[W - 1] &= (std::uint64_t{1} << (S % 64)) - 1;
            return a;
        }
        constexpr bool operator==(const bset &) const noexcept = default;
    };
    // ═══════════════════════════════════════════════════════════════════
    // 4. 树的形状（shape）—— 树只是类型，不产生任何对象
    //
    //    一个类型说清几件事，全部是编译期：
    //      leaf_count     : 几片叶子
    //      recognizers    : 它们的状态机元组（声明序）
    //      consume_t      : 所有 leaf consume_t 的去重并集
    //      consume_of<I>  : 第 I 片叶子自己的 consume_t（不去重）
    //      unique_machines: 同一个 FSM 类型没有出现两次
    //
    //    "这棵树认不认识帧 F"不在这里另开一个成员：用 advance_mask<Tree,F>() 回答，
    //    它就是"哪些列认识 F"的位图，空就是不认识（见下面 4.3）。
    // ═══════════════════════════════════════════════════════════════════
    namespace detail
    {
        // 组合子 = 声明了成员类型别名 children_t 的类。
        // 子节点 = children_t（std::tuple<Rs...>）的模板实参。
        consteval std::vector<std::meta::info> child_types(std::meta::info node)
        {
            if (!std::meta::is_class_type(node))
                return {};

            constexpr auto ctx = std::meta::access_context::current();
            for (auto m : std::meta::members_of(node, ctx))
            {
                if (!std::meta::has_identifier(m) ||
                    std::meta::identifier_of(m) != "children_t")
                    continue;
                if (!std::meta::is_type_alias(m))
                    continue;

                auto tup = std::meta::dealias(m); // std::tuple<Rs...>
                std::vector<std::meta::info> out;
                for (auto a : std::meta::template_arguments_of(tup))
                    if (std::meta::is_type(a))
                        out.push_back(std::meta::remove_cvref(a));
                return out;
            }
            return {};
        }

        // 按声明序收集叶子（叶子 = 声明了 protocol 的节点）
        consteval std::vector<std::meta::info> leaf_infos(std::meta::info node)
        {
            constexpr auto ctx = std::meta::access_context::current();
            std::vector<std::meta::info> out;
            for (auto m : std::meta::members_of(node, ctx))
                if (std::meta::has_identifier(m) &&
                    std::meta::identifier_of(m) == "protocol")
                {
                    out.push_back(node);
                    return out;
                }

            for (auto c : child_types(node))
                for (auto x : leaf_infos(c))
                    out.push_back(x);
            return out;
        }

        template <class... Ls>
        struct shape
        {
            static constexpr std::size_t leaf_count = sizeof...(Ls);
            using recognizers = std::tuple<typename Ls::protocol...>;

            // 同一个 FSM 类型出现两次 ⇒ 两个无法区分的 claim；这是结构错误。
            // name 是日志标签，不承担这个约束。
            static constexpr bool unique_machines = []() consteval {
                constexpr std::size_t N = sizeof...(Ls);
                std::array<std::meta::info, N> m{^^typename Ls::protocol...};
                for (std::size_t i = 0; i < N; ++i)
                    for (std::size_t j = i + 1; j < N; ++j)
                        if (m[i] == m[j])
                            return false;
                return true;
            }();
            static_assert(unique_machines, "同一个 FSM 类型不能在同一棵树里出现两次");

            // 所有 leaf 的 consume_t 并集，按第一次出现的顺序去重。
            static consteval auto consume_args()
            {
                std::vector<std::meta::info> out;
                auto add = [&out]<class L>() {
                    for (auto a : template_args<typename L::protocol::consume_t>())
                    {
                        bool seen = false;
                        for (auto b : out)
                            if (a == b)
                            {
                                seen = true;
                                break;
                            }
                        if (!seen)
                            out.push_back(a);
                    }
                };
                (add.template operator()<Ls>(), ...);
                return out;
            }

            using consume_t =
                typename[:std::meta::substitute(^^std::tuple, consume_args()):];
            // consume_t 只用于编译期校验（关系两端有没有共同 change）；
            // 它不是运行期路由表，路由由 feed(frame<T>) 的重载完成。

            // 第 I 个 leaf 自己的 consume_t：不去重，用来和 I 对账。
            template <std::size_t I>
            using consume_of = typename std::tuple_element_t<I, recognizers>::consume_t;
        };

        template <class Tree>
        using shape_of = typename[:std::meta::substitute(^^shape, leaf_infos(^^Tree)):];

        // T 是否在 Tuple 的模板实参里
        template <class T, class Tuple>
        static consteval bool in_tuple() noexcept
        {
            for (auto a : template_args<Tuple>())
                if (std::meta::dealias(a) == std::meta::dealias(^^T))
                    return true;
            return false;
        }

    } // namespace detail

    // ═══════════════════════════════════════════════════════════════════
    // 4.3 advance_mask<Tree, Frame>()：这一帧会**推进**哪些列
    // 判断当前的 Frame 是否能够推进。就是Tree是否有识别器使用这个类型的输入
    // ═══════════════════════════════════════════════════════════════════
    template <class Tree, class Self, class Frame>
    consteval auto advance_mask() noexcept
    {
        using Shape = typename detail::shape_of<Tree>;
        using Recognizers = Shape::recognizers;
        constexpr std::size_t S = Shape::leaf_count;
        bset<S> m{};
        template for (constexpr auto I : std::ranges::views::indices(S))
        {
            if constexpr (recognizer::can_feed<std::tuple_element_t<I, Recognizers>, Self,
                                               Frame>)
                m.set(static_cast<index_t>(I));
        }
        return m;
    }

    // ═══════════════════════════════════════════════════════════════════
    // 4.2 一片树最多多少片叶子：硬上限 64
    //
    //   64 不是"位图装不下"——位图是 ⌈S/64⌉ 个字，任意 S 都放得下。
    //   它是**当前实现的编译期上限**，实测出来的：
    //     稠密 refines（A 侧 S/2 × B 侧 S/2，边数 = S²/4）
    //       S=70 → 能编译
    //       S=96 → 'constexpr' operation count exceeds limit of 33554432
    //     爆点不在矩阵（closure 只要 S²×⌈S/64⌉），在 validate 的 Kahn：
    //     它每处理一个节点就把整个 awaits 列表重扫一遍，约 O(n²·E)。
    //
    //   所以这里写死 64（一个 uint64 位图正好装下），宁可编译期一句人话报错，
    //   也不要让人撞一屏模板展开。要放宽：先把 validate 的 Kahn 改成 O(n+E)，
    //   再把这个数调大。
    // ═══════════════════════════════════════════════════════════════════
    inline constexpr std::size_t max_kinds = 64;

    // ═══════════════════════════════════════════════════════════════════
    // 4.5 传递闭包（位行 Warshall）：i→k、k→j ⇒ i→j
    //     单独拎出来，是因为它可以被单独喂数据测试（见 test_matrix_series.cpp B4）。
    //     在 law_of::beats 里它跑一次；测试里可以用一条手搭的 3 链看它确实补边。
    // ═══════════════════════════════════════════════════════════════════
    template <std::size_t S>
    consteval std::array<bset<S>, S> closure(std::array<bset<S>, S> m)
    {
        for (std::size_t k = 0; k < S; ++k)
            for (std::size_t i = 0; i < S; ++i)
                if (m[i].has(k))
                    m[i] |= m[k];
        return m;
    }

    // ═══════════════════════════════════════════════════════════════════
    // 5. 法则：一张矩阵说完所有裁决
    //
    //    beats[i] = 第 i 行 = "i 打得赢谁"。两条来源，都是声明里已经说过的：
    //      refines<A,B> ：B 的每片叶子打败 A 的每片叶子（更具体的赢）
    //      one_of<R...> ：先声明的打败后声明的（互斥对之间唯一的平局规则）
    //    再跑一遍传递闭包，矩阵自带传递性 —— 运行期不再推导任何东西。
    //
    //    构造完断言对角线为空 ⇒ 矩阵是**严格偏序** ⇒ 极大元唯一
    //    ⇒ 下面的选择律与"谁先被看"无关。这就是自然选择。
    // ═══════════════════════════════════════════════════════════════════
    template <class Tree>
    struct law_of
    {
        using shape = detail::shape_of<Tree>;
        // 所有 leaf 的 consume_t 去重并集；运行期裁决只读 beats，不读它。
        using change_types = typename shape::consume_t;
        static constexpr std::size_t S = shape::leaf_count;
        static constexpr std::size_t C = std::tuple_size_v<change_types>;

        // 上限先断言：S 超过 max_kinds 就别往下算了（理由见 max_kinds 的注释）
        static_assert(S >= 1, "一片叶子都没有的树没有意义");
        static_assert(S <= max_kinds,
                      "叶子太多：上限见 intent::max_kinds（=64）。"
                      "位图放得下，是 validate 的 Kahn 会撞编译期运算上限");
        static_assert(C >= 1, "change_types 不能为空");

        // 行：i 打得赢谁
        static constexpr std::array<bset<S>, S> beats = []() consteval {
            std::array<bset<S>, S> m{};
            const auto p = Tree::plan();
            validate(p);                 // 无自环 / 细化×互斥 / awaits 无环
            for (auto [a, b] : p.awaits) // a 等 b ⇒ b 赢 a
                m[b].set(a);
            for (auto [a, b] : p.excl) // 互斥对是无序的：谁先声明谁赢
            {
                if (b < a)
                    std::swap(a, b); // 让 a 永远是"先声明的那一个"
                m[a].set(b);         // 于是直接写：先声明的赢
            }
            m = closure<S>(m); // 传递闭包（位行 Warshall）
            for (index_t i = 0; i < S; ++i)
                if (m[i].has(i))
                    throw "beats 有环：refines / one_of 互相指向，结构不成立";

            // 每个 kind 消费哪些 change：来自它自己的 consume_t（不去重）。
            // consumes 只在编译期用，不参与运行期裁决；运行期路由是
            // feed(frame<T>) 的重载。这里只回答一条边两端有没有共同 change。
            std::array<bset<C>, S> consumes{};
            template for (constexpr auto I : std::ranges::views::indices(S))
            {
                using owned = typename shape::template consume_of<I>;
                template for (constexpr auto J : std::ranges::views::indices(C))
                {
                    using Ch = std::tuple_element_t<J, change_types>;
                    if constexpr (detail::in_tuple<Ch, owned>())
                        consumes[I].set(J);
                }
                if (consumes[I].none())
                    throw "kind 不消费任何 input_frame change";
            }

            // 关系两端必须能相遇：否则这条边是数据/实现不匹配，编译期就拒绝。
            for (index_t i = 0; i < S; ++i)
                for (index_t j = 0; j < S; ++j)
                    if (m[i].has(j) && (consumes[i] & consumes[j]).none())
                        throw "beats 跨 change：关系两端没有共同变化类型";
            return m;
        }();

        // 每槽的名字（播报用）—— 和矩阵同一个下标。
        //   static_string 本体只有一个 const char*（8 B），string_view 是 16 B；
        //   名字很短、播报又稀疏，view() 时那次 strlen 可忽略，换来这张表减半。
        static constexpr std::array<static_string, S> names = []() consteval {
            std::array<static_string, S> r{};
            const auto p = Tree::plan();
            for (index_t i = 0; i < S; ++i)
                r[i] = p.kinds[i].name;
            return r;
        }();
    };

    // ═══════════════════════════════════════════════════════════════════
    // 6. 裁决器：一个纯函数
    //
    //    输入：report[N]（每个识别器最近一次上报的 claim_state）
    //          beats[N]（编译期静态表）
    //    输出：win / lose 两个位图
    //
    //    它不知道 Frame、arena_event、payload、span、识别器类型，
    //    也不调用任何识别器。调用 win/lose 是 arbiter 的事。
    // ═══════════════════════════════════════════════════════════════════
    template <std::size_t S>
    struct decision
    {
        bset<S> win{}, lose{}; // 不相交；win 是反链
    };

    // 裁决
    template <std::size_t S>
    constexpr decision<S> arbitrate(const std::array<claim_state, S> &report,
                                    const std::array<bset<S>, S> &beats) noexcept
    {
        // ① 一次扫，建 live / ready；这是局部量，不保存为成员
        bset<S> live{}, ready{};
        for (index_t i = 0; i < S; ++i)
        {
            if (report[i] != claim_state::idle)
                live.set(i);
            if (report[i] == claim_state::ready)
                ready.set(i);
        }
        if (ready.none())
            return {}; // 没 ready 就没 win，没 win 就没 lose

        // ② 只遍历 live 的置位
        bset<S> blocked{};
        live.for_each([&](index_t j) noexcept { blocked |= beats[j]; });

        // ③ ready 里的极大元
        const bset<S> win = ready & ~blocked;

        // ④ 只遍历 win 的置位
        bset<S> lose{};
        win.for_each([&](index_t i) noexcept { lose |= beats[i]; });
        lose &= live & ~win;

        return {win, lose};
    }

    // ═══════════════════════════════════════════════════════════════════
    // 9. arbiter：识别器和裁决器的唯一接线处
    // ═══════════════════════════════════════════════════════════════════
    template <class Tree>
    struct arbiter
    {
        using shape = detail::shape_of<Tree>;
        using law = law_of<Tree>;
        static constexpr std::size_t S = shape::leaf_count;
        using Recognizers = typename shape::recognizers;

        Recognizers recognizers{};
        std::array<claim_state, S> reports{}; // 最近一次上报；唯一状态源

        // 无观测版本
        template <class Self, class Frame>
        constexpr void feed(Self &self, const Frame &f, time_point now) noexcept
        {
            null_trace t{};
            feed<Self, Frame, null_trace>(self, f, now, t);
        }

        // 带 Trace 模板参数的版本：由 debug_arbiter 调用
        template <class Self, class Frame, class Trace>
        constexpr void feed(Self &self, const Frame &f, time_point now, Trace &t) noexcept
        {
            constexpr bset<S> advance = advance_mask<Tree, Self, Frame>();
            if constexpr (advance.none())
                return; // 没有列接受这个 Frame：不推进，也不再裁决

            template for (constexpr auto I : std::ranges::views::indices(S))
            {
                if constexpr (advance.has(static_cast<index_t>(I)))
                    reports[I] = std::get<I>(recognizers).feed(self, f, now);
            }
            resolve(self, now, t);
        }

        // 无观测版本
        template <class Self>
        constexpr void tick(Self &self, time_point now) noexcept
        {
            null_trace t{};
            tick<Self, null_trace>(self, now, t);
        }

        // 带 Trace 模板参数的版本：由 debug_arbiter 调用
        template <class Self, class Trace>
        constexpr void tick(Self &self, time_point now, Trace &t) noexcept
        {
            template for (constexpr auto I : std::ranges::views::indices(S))
            {
                reports[I] = std::get<I>(recognizers).tick(self, now);
            }
            resolve(self, now, t);
        }

      private:
        // report -> verdict -> 落地。先让位，再放行。
        template <class Self, class Trace>
        constexpr void resolve(Self &self, time_point now, Trace &t) noexcept
        {
            const auto d = arbitrate(reports, law::beats);

            template for (constexpr auto I : std::ranges::views::indices(S))
            {
                if (d.lose.has(I))
                    deliver<I>(self, false, now, t);
            }
            template for (constexpr auto I : std::ranges::views::indices(S))
            {
                if (d.win.has(I))
                    deliver<I>(self, true, now, t);
            }
        }

        // 唯一副作用点：win/lose 返回“裁决之后我是什么状态”，
        // 立刻写回 report；下一拍不会拿旧的 ready 再判一次。
        template <std::size_t Flat, class Self, class Trace>
        constexpr void deliver(Self &self, bool won, time_point at, Trace &t) noexcept
        {
            auto &m = std::get<Flat>(recognizers);
            reports[Flat] = won ? m.win(self) : m.lose(self);

            if constexpr (!std::is_same_v<Trace, null_trace>)
                t.push({law::names[Flat].view(),
                        won ? verdict::accepted : verdict::superseded, at});
        }
    };

    // ═══════════════════════════════════════════════════════════════════
    // 10. debug_arbiter:  arbiter + 观测
    //
    //   只覆盖 feed/tick 的无观测版本，显式把 trace 作为模板参数传下去。
    //   主 arbiter 没有 trace 成员、没有虚函数；
    // ═══════════════════════════════════════════════════════════════════
    template <class Tree, class Trace = trace>
    struct debug_arbiter : arbiter<Tree>
    {
        using law = law_of<Tree>;
        using base = arbiter<Tree>;

        Trace obs{};

        template <class Self, class Frame>
        constexpr void feed(Self &self, const Frame &f, time_point now) noexcept
        {
            base::template feed<Self, Frame, Trace>(self, f, now, obs);
        }

        template <class Self>
        constexpr void tick(Self &self, time_point now) noexcept
        {
            base::template tick<Self, Trace>(self, now, obs);
        }

        template <class Self>
        constexpr decltype(auto) events(this Self &&self) noexcept
            requires(requires() { std::forward_like<Self>(self.obs).events(); })
        {
            return std::forward_like<Self>(self.obs).events();
        }
        template <class Self>
        constexpr void clear_events(this Self &&self) noexcept
            requires(requires() { std::forward_like<Self>(self.obs).clear(); })
        {
            std::forward_like<Self>(self.obs).clear();
        }
        template <class Self>
        constexpr decltype(auto) dropped(this Self &&self) noexcept
            requires(requires() { std::forward_like<Self>(self.obs).dropped(); })
        {
            return std::forward_like<Self>(self.obs).dropped();
        }
    };

} // namespace intent

// ═══════════════════════════════════════════════════════════════════════════
//  arena_event：GLFW 与识别器之间的跨平台中间层
//    event_context：变化前仍成立的全部状态（keys / buttons / modifier / pointer）
//    frame<T>：context + 本帧变化 T（T 是具体的 change 类型）
//  FSM 只看 input_frame::frame<T>，不看 GLFW 事件。
// ═══════════════════════════════════════════════════════════════════════════
using mcs::vulkan::event::keyboard_event;
using mcs::vulkan::event::scroll_event;
using mcs::vulkan::event::mousebutton_event;
using mcs::vulkan::event::position2d_event;
using mcs::vulkan::event::cursor_enter_event;
using mcs::vulkan::event::Key;
using mcs::vulkan::event::MouseButtons;
using mcs::vulkan::event::ModifierKey;
using mcs::vulkan::event::Action;

struct input_frame
{

    enum class PointerDevice : std::uint8_t
    {
        Unknown,
        Touch,
        Mouse,
        Stylus,
        InvertedStylus,
        Trackpad
    };

    using Key = mcs::vulkan::event::Key;
    using scroll_event = mcs::vulkan::event::scroll_event;
    using cursor_enter_event = mcs::vulkan::event::cursor_enter_event;

    enum class MouseButton : std::uint8_t
    {
        eUNDEFINED = 0xFF,
        eMOUSE_BUTTON_1 = 0,
        eMOUSE_BUTTON_2 = 1,
        eMOUSE_BUTTON_3 = 2,
        eMOUSE_BUTTON_4 = 3,
        eMOUSE_BUTTON_5 = 4,
        eMOUSE_BUTTON_6 = 5,
        eMOUSE_BUTTON_7 = 6,
        eMOUSE_BUTTON_8 = 7,
        eMOUSE_BUTTON_LAST = eMOUSE_BUTTON_8,
        eMOUSE_BUTTON_LEFT = eMOUSE_BUTTON_1,
        eMOUSE_BUTTON_RIGHT = eMOUSE_BUTTON_2,
        eMOUSE_BUTTON_MIDDLE = eMOUSE_BUTTON_3,
        eSIZE = static_cast<std::uint8_t>(eMOUSE_BUTTON_LAST) + 1
    };

    enum class HeldState : std::uint8_t
    {
        Up,
        Down
    };
    enum class Transition : std::uint8_t
    {
        Press,
        Release
    };
    enum class PointerState : std::uint8_t
    {
        Outside,
        Hover,
        Down,
        DownMove
    };

    struct event_context
    {
        std::array<HeldState, std::to_underlying(Key::eSIZE)> keys{};
        std::array<HeldState, static_cast<std::size_t>(MouseButton::eSIZE)> buttons{};
        ModifierKey modifier{};

        PointerState pointer_state = PointerState::Outside;
        PointerDevice pointer_device = PointerDevice::Unknown;
        double pointer_x = 0.0;
        double pointer_y = 0.0;
        bool pointer_position_valid = false;

        constexpr decltype(auto) get_keyboard_event(this auto &&self, Key k) noexcept
        {
            return std::forward_like<decltype(self)>(self.keys[std::to_underlying(k)]);
        }

        constexpr decltype(auto) get_mousebutton_event(this auto &&self,
                                                       MouseButton b) noexcept
        {
            return std::forward_like<decltype(self)>(self.buttons[std::to_underlying(b)]);
        }

        constexpr bool any_button_down() const noexcept
        {
            for (auto s : buttons)
                if (s == HeldState::Down)
                    return true;
            return false;
        }
    };

    struct key_change
    {
        Key key{};
        Transition transition{};
        bool repeat{};
        ModifierKey modifier{};
    };

    struct button_change
    {
        MouseButton button{};
        Transition transition{};
        bool repeat{};
        ModifierKey modifier{};
    };

    struct move_change
    {
        double x = 0.0, y = 0.0;
        double dx = 0.0, dy = 0.0;
    };

    struct char_change
    {
        unsigned codepoint = 0;
    };

    template <class T>
    struct frame
    {
        event_context context;
        T change;
    };

    // NOTE: 枚举转发到下游的全部类型
    template <class T>
    static consteval bool is_change()
    {
        return std::same_as<T, key_change> || std::same_as<T, button_change> ||
               std::same_as<T, move_change> ||
               std::same_as<T, mcs::vulkan::event::scroll_event> ||
               std::same_as<T, mcs::vulkan::event::cursor_enter_event> ||
               std::same_as<T, char_change>;
    }

    template <class... Ts>
    struct changes_checked
    {
        static_assert((is_change<Ts>() && ...),
                      "consume_t 只允许 input_frame 的 change 类型");
        using type = std::tuple<Ts...>;
    };

    template <class... Ts>
    using changes = typename changes_checked<Ts...>::type;

    template <class PlatformButton>
    static constexpr MouseButton map_button(PlatformButton b) noexcept
    {
        using P = PlatformButton;
        switch (b)
        {
        case P::eMOUSE_BUTTON_LEFT:
            return MouseButton::eMOUSE_BUTTON_LEFT;
        case P::eMOUSE_BUTTON_RIGHT:
            return MouseButton::eMOUSE_BUTTON_RIGHT;
        case P::eMOUSE_BUTTON_MIDDLE:
            return MouseButton::eMOUSE_BUTTON_MIDDLE;
        case P::eMOUSE_BUTTON_4:
            return MouseButton::eMOUSE_BUTTON_4;
        case P::eMOUSE_BUTTON_5:
            return MouseButton::eMOUSE_BUTTON_5;
        case P::eMOUSE_BUTTON_6:
            return MouseButton::eMOUSE_BUTTON_6;
        case P::eMOUSE_BUTTON_7:
            return MouseButton::eMOUSE_BUTTON_7;
        case P::eMOUSE_BUTTON_8:
            return MouseButton::eMOUSE_BUTTON_8;
        case P::eMOUSE_BUTTON_LAST:
            return MouseButton::eMOUSE_BUTTON_LAST;
        default:
            return MouseButton::eUNDEFINED;
        }
    }

    template <class Event>
    static constexpr auto build_change(const event_context &ctx, const Event &e) noexcept
    {
        using T = std::remove_cvref_t<Event>;

        if constexpr (std::same_as<T, mcs::vulkan::event::keyboard_event>)
        {
            return frame<key_change>{ctx, key_change{e.key,
                                                     (e.press() || e.repeat())
                                                         ? Transition::Press
                                                         : Transition::Release,
                                                     e.repeat(), e.modifier_key}};
        }
        else if constexpr (std::same_as<T, mcs::vulkan::event::mousebutton_event>)
        {
            return frame<button_change>{ctx, button_change{map_button(e.button),
                                                           (e.press() || e.repeat())
                                                               ? Transition::Press
                                                               : Transition::Release,
                                                           e.repeat(), e.modifier_key}};
        }
        else if constexpr (std::same_as<T, mcs::vulkan::event::position2d_event>)
        {
            const double dx = ctx.pointer_position_valid ? e.xpos - ctx.pointer_x : 0.0;
            const double dy = ctx.pointer_position_valid ? e.ypos - ctx.pointer_y : 0.0;
            return frame<move_change>{ctx, move_change{e.xpos, e.ypos, dx, dy}};
        }
        else if constexpr (std::same_as<T, mcs::vulkan::event::scroll_event>)
        {
            return frame<mcs::vulkan::event::scroll_event>{ctx, e};
        }
        else if constexpr (std::same_as<T, mcs::vulkan::event::cursor_enter_event>)
        {
            return frame<mcs::vulkan::event::cursor_enter_event>{ctx, e};
        }
        else if constexpr (std::same_as<T, mcs::vulkan::event::char_event>)
        {
            return frame<char_change>{ctx, char_change{e.codepoint}};
        }
        // 其余事件没有 return：auto 推导为 void，调用方不转发。
    }

    template <class T>
    static constexpr void apply_change(event_context &ctx, const frame<T> &f) noexcept
    {
        using U = std::remove_cvref_t<T>;

        if constexpr (std::same_as<U, key_change>)
        {
            ctx.keys[std::to_underlying(f.change.key)] =
                f.change.transition == Transition::Press ? HeldState::Down
                                                         : HeldState::Up;
            ctx.modifier = f.change.modifier;
        }
        else if constexpr (std::same_as<U, button_change>)
        {
            ctx.buttons[std::to_underlying(f.change.button)] =
                f.change.transition == Transition::Press ? HeldState::Down
                                                         : HeldState::Up;
            ctx.modifier = f.change.modifier;

            if (f.change.transition == Transition::Press)
            {
                ctx.pointer_device = PointerDevice::Mouse;
                if (ctx.pointer_state != PointerState::DownMove)
                    ctx.pointer_state = PointerState::Down;
            }
            else
            {
                if (ctx.any_button_down())
                {
                    if (ctx.pointer_state != PointerState::DownMove)
                        ctx.pointer_state = PointerState::Down;
                }
                else
                {
                    ctx.pointer_state = (ctx.pointer_state == PointerState::Outside)
                                            ? PointerState::Outside
                                            : PointerState::Hover;
                }
            }
        }
        else if constexpr (std::same_as<U, move_change>)
        {
            ctx.pointer_x = f.change.x;
            ctx.pointer_y = f.change.y;
            ctx.pointer_position_valid = true;
            ctx.pointer_state =
                ctx.any_button_down() ? PointerState::DownMove : PointerState::Hover;
            if (ctx.pointer_device == PointerDevice::Unknown)
                ctx.pointer_device = PointerDevice::Mouse;
        }
        else if constexpr (std::same_as<U, mcs::vulkan::event::cursor_enter_event>)
        {
            if (f.change.value)
            {
                ctx.pointer_device = PointerDevice::Mouse;
                ctx.pointer_state =
                    ctx.any_button_down() ? PointerState::Down : PointerState::Hover;
            }
            else
            {
                ctx.pointer_device = PointerDevice::Unknown;
                ctx.pointer_state = PointerState::Outside;
                ctx.pointer_position_valid = false;
            }
        }
        // scroll / char：不改变 ctx
    }
};

namespace gesture
{
    using intent::claim_state;
    using intent::time_point;

    struct none_payload
    {
        friend constexpr bool operator==(none_payload, none_payload) = default;
    };

    template <class T>
    inline constexpr bool is_none_payload = std::is_same_v<T, none_payload>;

    namespace state
    {
        constexpr claim_state latch(claim_state &cur, claim_state s) noexcept
        {
            if (cur == claim_state::ready && s != claim_state::idle)
                s = claim_state::ready;
            cur = s;
            return s;
        }
        constexpr claim_state hold(claim_state &cur) noexcept
        {
            return latch(cur, claim_state::hold);
        }
        constexpr claim_state idle(claim_state &cur) noexcept
        {
            return latch(cur, claim_state::idle);
        }
        constexpr claim_state ready(claim_state &cur) noexcept
        {
            return latch(cur, claim_state::ready);
        }
        constexpr claim_state keep(claim_state &cur) noexcept
        {
            return latch(cur, cur);
        }
    } // namespace state

    // 非局部 base：name / fn_name / policy / payload_t 都在库里
    template <class Payload, static_string Name, static_string Fn, class Policy>
    struct trait_base
    {
        using payload_t = Payload;
        static constexpr auto name = Name;
        static constexpr auto fn_name = Fn;
        using policy = Policy;
    };

    // 数据槽：只有 data；none_payload 时为空
    template <class Trait, bool = is_none_payload<typename Trait::payload_t>>
    struct slot;

    template <class Trait>
    struct slot<Trait, false>
    {
        using payload_t = typename Trait::payload_t;
        [[no_unique_address]] payload_t data{};

        template <class Event>
        constexpr void collect(time_point t, const Event &e) noexcept
        {
            Trait::collect(t, data, e);
        }
    };

    template <class Trait>
    struct slot<Trait, true>
    {
        template <class... Args>
        constexpr void collect(Args &&...) noexcept
        {
        }
    };

    template <class Policy>
    struct press_watch
    {
        bool down{};
        time_point down_at{};
        double down_x{};
        double down_y{};

        constexpr int operator()(const input_frame::frame<input_frame::button_change> &f,
                                 time_point now) noexcept
        {
            const auto &b = f.change;
            if (b.button != Policy::button)
                return 0;
            if (b.transition == input_frame::Transition::Press)
            {
                if (b.repeat)
                    return 0;
                down = true;
                down_at = now;
                down_x = f.context.pointer_x;
                down_y = f.context.pointer_y;
                return 1;
            }
            if (!down)
                return 0;
            down = false;
            const bool quick = now - down_at <= time_point{Policy::tap_max_ms};
            const bool still =
                std::hypot(f.context.pointer_x - down_x, f.context.pointer_y - down_y) <=
                Policy::slop_px;
            return (quick && still) ? 2 : -1;
        }

        constexpr void clear() noexcept
        {
            down = false;
        }
    };

    // ── 语义事件 ────────────────────────────────────────────────────
    struct click_event
    {
        double x{}, y{};
        time_point at{};
    };
    struct multi_click_event
    {
        double x{}, y{};
        int count{};
        time_point at{};
    };
    enum class drag_phase : std::uint8_t
    {
        begin,
        move,
        end
    };
    struct drag_event
    {
        drag_phase phase{};
        double x{}, y{}, dx{}, dy{};
        double total_dx{}, total_dy{};
        time_point at{};
        ModifierKey modifier{};
    };
    struct wheel_event
    {
        int delta{};
        time_point at{};
    };
    struct key_event
    {
        Key key{};
        input_frame::Transition transition{};
        time_point at{};
    };

    // ── click_template ──────────────────────────────────────────────
    template <class Trait>
    struct click_template
    {
        using policy = typename Trait::policy;
        static constexpr auto name = Trait::name;
        static constexpr auto fn_name = Trait::fn_name;
        using consume_t = input_frame::changes<input_frame::button_change>;

        claim_state cur{};
        press_watch<policy> watch{};
        slot<Trait> in{};

        template <class Self>
        constexpr claim_state feed(
            Self &self, const input_frame::frame<input_frame::button_change> &f,
            time_point now) noexcept
        {
            switch (watch(f, now))
            {
            case 1:
                return state::hold(cur);
            case 2:
                in.collect(now,
                           click_event{f.context.pointer_x, f.context.pointer_y, now});
                return state::ready(cur);
            case -1:
                return state::idle(cur);
            default:
                return watch.down ? state::hold(cur) : state::keep(cur);
            }
        }

        template <class Self>
        constexpr claim_state tick(Self &, time_point) noexcept
        {
            return state::keep(cur);
        }

        template <class Self>
        constexpr claim_state win(Self &self) noexcept
        {
            self.template invoke<fn_name>(in.data);
            cur = claim_state::idle;
            return cur;
        }

        template <class Self>
        constexpr claim_state lose(Self &) noexcept
        {
            watch.clear();
            cur = claim_state::idle;
            return cur;
        }
    };

    // ── multi_click_template ────────────────────────────────────────
    template <class Trait>
    struct multi_click_template
    {
        using policy = typename Trait::policy;
        static constexpr auto name = Trait::name;
        static constexpr auto fn_name = Trait::fn_name;
        using consume_t = input_frame::changes<input_frame::button_change>;

        claim_state cur{};
        press_watch<policy> watch{};
        slot<Trait> in{};
        int clicks{};
        time_point last{};
        bool armed{};

        template <class Self>
        constexpr claim_state feed(
            Self &self, const input_frame::frame<input_frame::button_change> &f,
            time_point now) noexcept
        {
            if (armed && now - last > time_point{policy::gap_ms})
            {
                reset_chain();
                (void)state::idle(cur);
            }
            switch (watch(f, now))
            {
            case 1:
                return state::hold(cur);
            case 2: {
                ++clicks;
                armed = true;
                last = now;
                if (clicks >= policy::count)
                {
                    const int count = clicks;
                    reset_chain();
                    in.collect(now, multi_click_event{f.context.pointer_x,
                                                      f.context.pointer_y, count, now});
                    return state::ready(cur);
                }
                return state::hold(cur);
            }
            case -1:
                reset_chain();
                return state::idle(cur);
            default:
                return (armed || clicks > 0 || watch.down) ? state::hold(cur)
                                                           : state::keep(cur);
            }
        }

        template <class Self>
        constexpr claim_state tick(Self &, time_point now) noexcept
        {
            if (armed && now - last > time_point{policy::gap_ms})
            {
                reset_chain();
                return state::idle(cur);
            }
            return state::keep(cur);
        }

        template <class Self>
        constexpr claim_state win(Self &self) noexcept
        {
            self.template invoke<fn_name>(in.data);
            reset_chain();
            cur = claim_state::idle;
            return cur;
        }

        template <class Self>
        constexpr claim_state lose(Self &) noexcept
        {
            reset_chain();
            cur = claim_state::idle;
            return cur;
        }

        constexpr void reset_chain() noexcept
        {
            clicks = 0;
            armed = false;
            watch.clear();
        }
    };

    // ── drag_template ───────────────────────────────────────────────
    template <class Trait>
    struct drag_template
    {
        using policy = typename Trait::policy;
        static constexpr auto name = Trait::name;
        static constexpr auto fn_name = Trait::fn_name;
        using consume_t =
            input_frame::changes<input_frame::button_change, input_frame::move_change>;

        claim_state cur{};
        slot<Trait> in{};
        double down_x{}, down_y{}, last_x{}, last_y{};
        bool down{}, dragging{}, finished_{};

        template <class Self>
        constexpr claim_state feed(
            Self &self, const input_frame::frame<input_frame::button_change> &f,
            time_point now) noexcept
        {
            if (f.change.button != policy::button)
                return state::keep(cur);
            if (f.change.transition == input_frame::Transition::Press)
            {
                if (f.change.repeat)
                    return state::keep(cur);
                if (cur != claim_state::idle)
                    (void)state::idle(cur);
                down = true;
                dragging = false;
                down_x = last_x = f.context.pointer_x;
                down_y = last_y = f.context.pointer_y;
                return state::hold(cur);
            }
            down = false;
            if (!dragging)
                return state::idle(cur);
            dragging = false;
            finished_ = true;
            in.collect(
                now, drag_event{drag_phase::end, f.context.pointer_x, f.context.pointer_y,
                                0.0, 0.0, f.context.pointer_x - down_x,
                                f.context.pointer_y - down_y, now, f.context.modifier});
            return state::ready(cur);
        }

        template <class Self>
        constexpr claim_state feed(Self &self,
                                   const input_frame::frame<input_frame::move_change> &f,
                                   time_point now) noexcept
        {
            if (!down)
                return state::keep(cur);
            const auto &m = f.change;
            if (!dragging)
            {
                if (std::hypot(m.x - down_x, m.y - down_y) <= policy::slop_px)
                {
                    last_x = m.x;
                    last_y = m.y;
                    return state::hold(cur);
                }
                dragging = true;
                last_x = m.x;
                last_y = m.y;
                in.collect(now,
                           drag_event{drag_phase::begin, m.x, m.y, 0.0, 0.0, m.x - down_x,
                                      m.y - down_y, now, f.context.modifier});
                return state::ready(cur);
            }
            const double dx = m.x - last_x;
            const double dy = m.y - last_y;
            last_x = m.x;
            last_y = m.y;
            in.collect(now, drag_event{drag_phase::move, m.x, m.y, dx, dy, m.x - down_x,
                                       m.y - down_y, now, f.context.modifier});
            return state::ready(cur);
        }

        template <class Self>
        constexpr claim_state tick(Self &, time_point) noexcept
        {
            return state::keep(cur);
        }

        template <class Self>
        constexpr claim_state win(Self &self) noexcept
        {
            self.template invoke<fn_name>(in.data);
            cur = finished_ ? claim_state::idle : claim_state::hold;
            finished_ = false;
            return cur;
        }

        template <class Self>
        constexpr claim_state lose(Self &) noexcept
        {
            down = false;
            dragging = false;
            finished_ = false;
            cur = claim_state::idle;
            return cur;
        }
    };

    // ── hover_template（none） ─────────────────────────────────────
    template <class Trait>
    struct hover_template
    {
        static constexpr auto name = Trait::name;
        static constexpr auto fn_name = Trait::fn_name;
        using consume_t = input_frame::changes<input_frame::cursor_enter_event>;

        claim_state cur{};
        slot<Trait> in{};

        template <class Self>
        constexpr claim_state feed(
            Self &self, const input_frame::frame<input_frame::cursor_enter_event> &f,
            time_point) noexcept
        {
            if (f.change.value)
                return state::ready(cur);
            return state::keep(cur);
        }

        template <class Self>
        constexpr claim_state tick(Self &, time_point) noexcept
        {
            return state::keep(cur);
        }

        template <class Self>
        constexpr claim_state win(Self &self) noexcept
        {
            self.template invoke<fn_name>();
            cur = claim_state::idle;
            return cur;
        }

        template <class Self>
        constexpr claim_state lose(Self &) noexcept
        {
            cur = claim_state::idle;
            return cur;
        }
    };

    // ── wheel_template ─────────────────────────────────────────────
    template <class Trait>
    struct wheel_template
    {
        static constexpr auto name = Trait::name;
        static constexpr auto fn_name = Trait::fn_name;
        using consume_t = input_frame::changes<scroll_event>;

        claim_state cur{};
        slot<Trait> in{};

        template <class Self>
        constexpr claim_state feed(Self &self, const input_frame::frame<scroll_event> &f,
                                   time_point now) noexcept
        {
            in.collect(now, wheel_event{static_cast<int>(f.change.yoffset), now});
            return state::ready(cur);
        }

        template <class Self>
        constexpr claim_state tick(Self &, time_point) noexcept
        {
            return state::keep(cur);
        }

        template <class Self>
        constexpr claim_state win(Self &self) noexcept
        {
            self.template invoke<fn_name>(in.data);
            cur = claim_state::idle;
            return cur;
        }

        template <class Self>
        constexpr claim_state lose(Self &) noexcept
        {
            cur = claim_state::idle;
            return cur;
        }
    };

    // ── key_template（none） ───────────────────────────────────────
    template <class Trait>
    struct key_template
    {
        using policy = typename Trait::policy;
        static constexpr auto name = Trait::name;
        static constexpr auto fn_name = Trait::fn_name;
        using consume_t = input_frame::changes<input_frame::key_change>;

        claim_state cur{};
        slot<Trait> in{};

        template <class Self>
        constexpr claim_state feed(Self &self,
                                   const input_frame::frame<input_frame::key_change> &f,
                                   time_point) noexcept
        {
            if (f.change.transition == input_frame::Transition::Press &&
                !f.change.repeat && f.change.key == policy::key)
                return state::ready(cur);
            return state::keep(cur);
        }

        template <class Self>
        constexpr claim_state tick(Self &, time_point) noexcept
        {
            return state::keep(cur);
        }

        template <class Self>
        constexpr claim_state win(Self &self) noexcept
        {
            self.template invoke<fn_name>();
            cur = claim_state::idle;
            return cur;
        }

        template <class Self>
        constexpr claim_state lose(Self &) noexcept
        {
            cur = claim_state::idle;
            return cur;
        }
    };

    // ── key_observer_template ──────────────────────────────────────
    template <class Trait>
    struct key_observer_template
    {
        static constexpr auto name = Trait::name;
        static constexpr auto fn_name = Trait::fn_name;
        using consume_t = input_frame::changes<input_frame::key_change>;

        claim_state cur{};
        slot<Trait> in{};

        template <class Self>
        constexpr claim_state feed(Self &self,
                                   const input_frame::frame<input_frame::key_change> &f,
                                   time_point now) noexcept
        {
            if (f.change.repeat)
                return state::keep(cur);
            in.collect(now, key_event{f.change.key, f.change.transition, now});
            return state::ready(cur);
        }

        template <class Self>
        constexpr claim_state tick(Self &, time_point) noexcept
        {
            return state::keep(cur);
        }

        template <class Self>
        constexpr claim_state win(Self &self) noexcept
        {
            self.template invoke<fn_name>(in.data);
            cur = claim_state::idle;
            return cur;
        }

        template <class Self>
        constexpr claim_state lose(Self &) noexcept
        {
            cur = claim_state::idle;
            return cur;
        }
    };

    // ── chord_template ─────────────────────────────────────────────
    template <class Trait>
    struct chord_template
    {
        static constexpr auto name = Trait::name;
        static constexpr auto fn_name = Trait::fn_name;
        using consume_t = input_frame::changes<input_frame::key_change>;

        claim_state cur{};
        slot<Trait> in{};

        template <class Self>
        constexpr claim_state feed(Self &self,
                                   const input_frame::frame<input_frame::key_change> &f,
                                   time_point now) noexcept
        {
            if (auto e = Trait::match(f.change, now))
            {
                in.collect(now, *e);
                return state::ready(cur);
            }
            return state::keep(cur);
        }

        template <class Self>
        constexpr claim_state tick(Self &, time_point) noexcept
        {
            return state::keep(cur);
        }

        template <class Self>
        constexpr claim_state win(Self &self) noexcept
        {
            self.template invoke<fn_name>(in.data);
            cur = claim_state::idle;
            return cur;
        }

        template <class Self>
        constexpr claim_state lose(Self &) noexcept
        {
            cur = claim_state::idle;
            return cur;
        }
    };
} // namespace gesture

// ═══════════════════════════════════════════════════════════════════════
//  mcs::app —— 数据 + Trait + tree + arena
// ═══════════════════════════════════════════════════════════════════════
namespace mcs::app
{
    using intent::time_point;

    inline constexpr std::int64_t click_max_ms = 250;
    inline constexpr std::int64_t dbl_gap_ms = 300;
    inline constexpr std::int64_t triple_gap_ms = 250;
    inline constexpr double drag_slop_px = 4.0;

    inline time_point now_ms() noexcept
    {
        static const auto t0 = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<time_point>(std::chrono::steady_clock::now() -
                                                      t0);
    }

    namespace policy
    {
        struct click
        {
            static constexpr auto button = input_frame::MouseButton::eMOUSE_BUTTON_LEFT;
            static constexpr std::int64_t tap_max_ms = mcs::app::click_max_ms;
            static constexpr double slop_px = mcs::app::drag_slop_px;
        };
        struct multi
        {
            static constexpr auto button = input_frame::MouseButton::eMOUSE_BUTTON_LEFT;
            static constexpr std::int64_t tap_max_ms = mcs::app::click_max_ms;
            static constexpr double slop_px = mcs::app::drag_slop_px;
            static constexpr int count = 2;
            static constexpr std::int64_t gap_ms = mcs::app::dbl_gap_ms;
        };
        struct multi3
        {
            static constexpr auto button = input_frame::MouseButton::eMOUSE_BUTTON_LEFT;
            static constexpr std::int64_t tap_max_ms = mcs::app::click_max_ms;
            static constexpr double slop_px = mcs::app::drag_slop_px;
            static constexpr int count = 3;
            static constexpr std::int64_t gap_ms = mcs::app::triple_gap_ms;
        };
        struct drag
        {
            static constexpr auto button = input_frame::MouseButton::eMOUSE_BUTTON_LEFT;
            static constexpr double slop_px = mcs::app::drag_slop_px;
        };
        struct hover
        {
        };
        struct wheel
        {
        };
        struct key_a
        {
            static constexpr auto key = Key::eA;
        };
        struct key_c
        {
            static constexpr auto key = Key::eC;
        };
        struct shortcut
        {
        };
    } // namespace policy

    namespace detail
    {
        enum class command : std::uint8_t
        {
            copy,
            copy_plain,
            paste,
            save,
            notify
        };
        constexpr std::string_view command_name(command c) noexcept
        {
            switch (c)
            {
            case command::copy:
                return "copy";
            case command::copy_plain:
                return "copy_plain";
            case command::paste:
                return "paste";
            case command::save:
                return "save";
            case command::notify:
                return "notify";
            }
            return "?";
        }

        struct shortcut_event
        {
            std::array<command, 2> items{};
            int n = 0;
        };

        struct counters
        {
            int click{};
            int double_click{};
            int triple_click{};
            int hover{};
            int wheel{};
            int key_a{};
            int key_c{};
            int shortcut{};
            int any_key{};
            int commands{};
            int drag_begin{};
            int drag_move{};
            int drag_end{};
            double x{}, y{};
            double total_dx{}, total_dy{};
            double move_x{}, move_y{}, move_dx{}, move_dy{};
            int delta{};
            friend constexpr bool operator==(const counters &,
                                             const counters &) = default;
        };

        template <class Self>
        inline time_point now_of(Self &self) noexcept
        {
            return self.manual_clock ? self.now : now_ms();
        }

        constexpr std::string_view to_string(intent::verdict v) noexcept
        {
            return v == intent::verdict::accepted ? "acc" : "sup";
        }

        template <class App>
        inline void report(const App &app) noexcept
        {
            for (const auto &e : app.arbiter.events())
            {
                if (e.name == "drag" || e.name == "hover")
                    continue;
                std::println("  [t={:>6}ms] {:<18} {:<3}", e.at.count(), e.name,
                             to_string(e.v));
            }
        }
    } // namespace detail

    // ── arena（局部 Trait：只写 payload + collect）────────────────
    struct click_payload
    {
        double x{}, y{};
        time_point at{};
    };
    struct Trait_click : gesture::trait_base<click_payload, static_string{"click"},
                                             static_string{"on_click"}, policy::click>
    {
        static void collect(time_point, payload_t &d,
                            const gesture::click_event &e) noexcept
        {
            d = {e.x, e.y, e.at};
        }
    };
    using click_template_recognizer = gesture::click_template<Trait_click>;

    inline auto make_arena()
    {

        struct multi_click_payload
        {
            double x{}, y{};
            int count{};
            time_point at{};
        };
        struct drag_payload
        {
            gesture::drag_phase phase{};
            double x{}, y{}, dx{}, dy{};
            double total_dx{}, total_dy{};
            ModifierKey modifier{};
            time_point at{};
        };
        struct wheel_payload
        {
            int delta{};
            time_point at{};
        };
        struct any_key_payload
        {
            Key key{};
            input_frame::Transition transition{};
            time_point at{};
        };

        // name / fn_name / policy / payload_t 都在 base；局部只写 payload + collect

        struct Trait_double_click
            : gesture::trait_base<multi_click_payload, static_string{"double_click"},
                                  static_string{"on_double_click"}, policy::multi>
        {
            static void collect(time_point, payload_t &d,
                                const gesture::multi_click_event &e) noexcept
            {
                d = {e.x, e.y, e.count, e.at};
            }
        };

        struct Trait_triple_click
            : gesture::trait_base<multi_click_payload, static_string{"triple_click"},
                                  static_string{"on_triple_click"}, policy::multi3>
        {
            static void collect(time_point, payload_t &d,
                                const gesture::multi_click_event &e) noexcept
            {
                d = {e.x, e.y, e.count, e.at};
            }
        };

        struct Trait_drag : gesture::trait_base<drag_payload, static_string{"drag"},
                                                static_string{"on_drag"}, policy::drag>
        {
            static void collect(time_point, payload_t &d,
                                const gesture::drag_event &e) noexcept
            {
                d = {e.phase,    e.x,        e.y,        e.dx, e.dy,
                     e.total_dx, e.total_dy, e.modifier, e.at};
            }
        };

        struct Trait_hover
            : gesture::trait_base<gesture::none_payload, static_string{"hover"},
                                  static_string{"on_hover"}, policy::hover>
        {
        };

        struct Trait_wheel : gesture::trait_base<wheel_payload, static_string{"wheel"},
                                                 static_string{"on_wheel"}, policy::wheel>
        {
            static void collect(time_point, payload_t &d,
                                const gesture::wheel_event &e) noexcept
            {
                d = {e.delta, e.at};
            }
        };

        struct Trait_key_a
            : gesture::trait_base<gesture::none_payload, static_string{"key_a"},
                                  static_string{"on_key_a"}, policy::key_a>
        {
        };

        struct Trait_key_c
            : gesture::trait_base<gesture::none_payload, static_string{"key_c"},
                                  static_string{"on_key_c"}, policy::key_c>
        {
        };

        struct Trait_shortcut
            : gesture::trait_base<detail::shortcut_event, static_string{"shortcut"},
                                  static_string{"on_shortcut"}, policy::shortcut>
        {
            static std::optional<payload_t> match(const input_frame::key_change &c,
                                                  time_point) noexcept
            {
                if (c.transition != input_frame::Transition::Press || c.repeat)
                    return std::nullopt;
                const bool ctrl = c.modifier.hasAll(ModifierKey::Control());
                const bool shift = c.modifier.hasAll(ModifierKey::Shift());
                const bool alt = c.modifier.hasAll(ModifierKey::Alt());
                if (!ctrl || alt)
                    return std::nullopt;
                payload_t r{};
                if (c.key == Key::eC)
                {
                    r.items[0] =
                        shift ? detail::command::copy_plain : detail::command::copy;
                    r.n = 1;
                    return r;
                }
                if (c.key == Key::eS)
                {
                    r.items[0] = detail::command::save;
                    r.items[1] = detail::command::notify;
                    r.n = 2;
                    return r;
                }
                if (c.key == Key::eV)
                {
                    r.items[0] = detail::command::paste;
                    r.n = 1;
                    return r;
                }
                return std::nullopt;
            }
            static void collect(time_point, payload_t &d, const payload_t &e) noexcept
            {
                d = e;
            }
        };

        struct Trait_any_key
            : gesture::trait_base<any_key_payload, static_string{"any_key"},
                                  static_string{"on_any_key"}, policy::hover>
        {
            static void collect(time_point, payload_t &d,
                                const gesture::key_event &e) noexcept
            {
                d = {e.key, e.transition, e.at};
            }
        };

        using tree = intent::all_of<
            intent::refines<
                intent::leaf<click_template_recognizer>,
                intent::all_of<intent::leaf<gesture::drag_template<Trait_drag>>,
                               intent::refines<intent::leaf<gesture::multi_click_template<
                                                   Trait_double_click>>,
                                               intent::leaf<gesture::multi_click_template<
                                                   Trait_triple_click>>>>>,
            intent::leaf<gesture::hover_template<Trait_hover>>,
            intent::leaf<gesture::wheel_template<Trait_wheel>>,
            intent::refines<intent::leaf<gesture::key_template<Trait_key_c>>,
                            intent::leaf<gesture::chord_template<Trait_shortcut>>>,
            intent::leaf<gesture::key_template<Trait_key_a>>,
            intent::leaf<gesture::key_observer_template<Trait_any_key>>>;

        return make_aggregate<"arena">(
            field<"click">(Trait_click::payload_t{}),
            field<"double_click">(Trait_double_click::payload_t{}),
            field<"triple_click">(Trait_triple_click::payload_t{}),
            field<"drag">(Trait_drag::payload_t{}),
            field<"wheel">(Trait_wheel::payload_t{}),
            field<"shortcut">(Trait_shortcut::payload_t{}),
            field<"key_event">(Trait_any_key::payload_t{}),
            field<"arbiter">(intent::debug_arbiter<tree>{}),
            field<"counters">(detail::counters{}),
            field<"input_state">(input_frame::event_context{}),
            field<"now">(time_point{}), field<"manual_clock">(false),
            field<"keep_trace">(false), field<"verbose">(true), field<"frames">(0u),
            field<"input_binding">(glfw_input_translation::scoped_binding{}),

            method<"on_click">([](auto &self, const Trait_click::payload_t &e) noexcept {
                ++self.counters.click;
                self.click = e;
                self.counters.x = e.x;
                self.counters.y = e.y;
                if (self.verbose)
                    std::println("  >>> 单击    (x={:.1f}, y={:.1f})", e.x, e.y);
            }),
            method<"on_double_click">(
                [](auto &self, const Trait_double_click::payload_t &e) noexcept {
                    ++self.counters.double_click;
                    self.double_click = e;
                    self.counters.x = e.x;
                    self.counters.y = e.y;
                    if (self.verbose)
                        std::println("  >>> 双击    (x={:.1f}, y={:.1f}, count={})", e.x,
                                     e.y, e.count);
                }),
            method<"on_triple_click">(
                [](auto &self, const Trait_triple_click::payload_t &e) noexcept {
                    ++self.counters.triple_click;
                    self.triple_click = e;
                    self.counters.x = e.x;
                    self.counters.y = e.y;
                    if (self.verbose)
                        std::println("  >>> 三击    (x={:.1f}, y={:.1f}, count={})", e.x,
                                     e.y, e.count);
                }),
            method<"on_drag">([](auto &self, const Trait_drag::payload_t &e) noexcept {
                self.drag = e;
                switch (e.phase)
                {
                case gesture::drag_phase::begin:
                    ++self.counters.drag_begin;
                    if (self.verbose)
                        std::println("  >>> 拖拽·开始 (起点 {:.1f},{:.1f})", e.x, e.y);
                    break;
                case gesture::drag_phase::move:
                    ++self.counters.drag_move;
                    self.counters.move_x = e.x;
                    self.counters.move_y = e.y;
                    self.counters.move_dx = e.dx;
                    self.counters.move_dy = e.dy;
                    if (self.verbose)
                        std::println(
                            "  >>> 拖拽·移动 (到 {:.1f},{:.1f}; d={:.1f},{:.1f})", e.x,
                            e.y, e.dx, e.dy);
                    break;
                case gesture::drag_phase::end:
                    ++self.counters.drag_end;
                    self.counters.x = e.x;
                    self.counters.y = e.y;
                    self.counters.total_dx = e.total_dx;
                    self.counters.total_dy = e.total_dy;
                    if (self.verbose)
                        std::println(
                            "  >>> 拖拽·释放 (到 {:.1f},{:.1f}; 总位移 {:.1f},{:.1f})",
                            e.x, e.y, e.total_dx, e.total_dy);
                    break;
                }
            }),
            method<"on_hover">([](auto &self) noexcept {
                ++self.counters.hover;
                if (self.verbose)
                    std::println("  >>> 悬停    进入窗口");
            }),
            method<"on_wheel">([](auto &self, const Trait_wheel::payload_t &e) noexcept {
                ++self.counters.wheel;
                self.wheel = e;
                self.counters.delta = e.delta;
                if (self.verbose)
                    std::println("  >>> 滚轮    (delta={})", e.delta);
            }),
            method<"on_key_a">([](auto &self) noexcept {
                ++self.counters.key_a;
                if (self.verbose)
                    std::println("  >>> 键盘 A");
            }),
            method<"on_key_c">([](auto &self) noexcept {
                ++self.counters.key_c;
                if (self.verbose)
                    std::println("  >>> 键盘 C");
            }),
            method<"on_shortcut">(
                [](auto &self, const Trait_shortcut::payload_t &e) noexcept {
                    ++self.counters.shortcut;
                    self.shortcut = e;
                    self.counters.commands += e.n;
                    if (self.verbose)
                    {
                        std::print("  >>> 快捷键");
                        for (int i = 0; i < e.n; ++i)
                            std::print(" {}", detail::command_name(e.items[i]));
                        std::println("");
                    }
                }),
            method<"on_any_key">([](auto &self,
                                    const Trait_any_key::payload_t &e) noexcept {
                ++self.counters.any_key;
                self.key_event = e;
                if (self.verbose)
                    std::println("  >>> HUD key={} {}", static_cast<int>(e.key),
                                 e.transition == input_frame::Transition::Press ? "down"
                                                                                : "up");
            }),

            method<"tick">([](auto &self) noexcept {
                const auto now = detail::now_of(self);
                self.now = now;
                self.arbiter.tick(self, now);
                if (self.verbose)
                    detail::report(self);
                if (!self.keep_trace)
                    self.arbiter.clear_events();
                ++self.frames;
            }),

            method<"input_event">([](auto &self, auto e) noexcept {
                const auto now = detail::now_of(self);
                self.now = now;
                using frame_t = decltype(input_frame::build_change(self.input_state, e));
                if constexpr (!std::is_void_v<frame_t>)
                {
                    auto f = input_frame::build_change(self.input_state, e);
                    self.arbiter.feed(self, f, now);
                    input_frame::apply_change(self.input_state, f);
                }
            }));
    }

} // namespace mcs::app

namespace app = mcs::app;
auto &inputTranslation()
{
    static auto input = glfw_input_translation{};
    return input;
}

namespace render
{
    // NOTE: 实现 RenderObject 的匿名实现类的效果
    template <class Agg>
    struct AggRenderObject final : ui_new::RenderObject
    {
        Agg agg;

        constexpr explicit AggRenderObject(Agg a) noexcept : agg(std::move(a)) {}

        constexpr void render(ui_new::ScreenWidget *s, ui_new::Widget *owner,
                              render_context &ctx) override
        {
            agg.template invoke<"render">(s, owner, ctx);
        }
    };
    template <typename Agg, static_string hover_fn_name>
    struct HoverFn
    {
        static constexpr auto handler(picking_result r, bool enter, InputCtx &inputCtx,
                                      ui_new::ScreenWidget *screen) noexcept
        {
            uint64_t ptr = getShaderData(r.key);
            auto *agg = reinterpret_cast<Agg *>(ptr);
            agg->template invoke<hover_fn_name>(r, enter, inputCtx, screen);
        };
        static constexpr auto index()
        {
            static const uint32_t fn = hoverPool().bind(&handler);
            return fn;
        }
    };

    static constexpr auto postBindInputTranslation = [](auto &agg) {
        inputTranslation().attach(agg.input_binding, agg);
    };

    template <class Agg, class Handler = decltype(postBindInputTranslation)>
    [[nodiscard]] constexpr auto asRenderObject(
        Agg &&a, Handler postHandler = postBindInputTranslation)
    {
        using D = std::decay_t<Agg>;
        auto renderObject = std::make_unique<AggRenderObject<D>>(std::forward<Agg>(a));
        if constexpr (requires() { postHandler(renderObject->agg); })
            postHandler(renderObject->agg);
        return renderObject;
    }
}; // namespace render
//diff: [test_intent] end

// ═══════════════════════════════════════════════════════════════════════
// table_demo：一张“数据 + 算法”的表
//
//   类型层：column<...> 是 part，table_schema<...> 是 composed type
//   对象层：make_table<Schema>(rows, font) 造出“数据 + 端口”的聚合
//   算法层：solve_columns / window_for / shape_cell / push_rect / push_text
//
//   没有为表新增任何基类；它和 TextBox 一样，只是一个带
//   field<> + method<"render"> 的聚合，靠 asRenderObject 上桌。
// ═══════════════════════════════════════════════════════════════════════
namespace table_demo
{
    using namespace ui_new;

    // ── 1. 类型层：列 = part ────────────────────────────────────────
    template <static_string Name, int Width, int Weight = 0>
    struct column
    {
        static constexpr auto name = Name;
        static constexpr float width = static_cast<float>(Width);
        static constexpr float weight = static_cast<float>(Weight);
    };

    // ── 2. 类型层：表 = 列的 composed_type ──────────────────────────
    template <class... Cols>
    struct table_schema
    {
        static constexpr std::size_t count = sizeof...(Cols);
        static constexpr std::array<static_string, count> names{Cols::name...};
        static constexpr std::array<float, count> widths{Cols::width...};
        static constexpr std::array<float, count> weights{Cols::weight...};
    };

    // ── 3. 运行期数据 ──────────────────────────────────────────────
    template <std::size_t N>
    struct table_metrics
    {
        std::array<float, N> x{};
        std::array<float, N> w{};
    };

    struct table_prefs
    {
        float pad = 12.0f;      // 列间距（px）
        float row_h = 30.0f;    // 行高（px）
        float row_gap = 2.0f;   // 行间距（px）
        float header_h = 40.0f; // 表头高（px）
        int selected = 3;       // 选中行（演示状态）
    };

    struct row_window
    {
        std::size_t first = 0;
        std::size_t last = 0;
    };

    // ── 4. 算法层：列宽求解（fixed + flex，余数给最后一列）──────────
    template <class Schema>
    table_metrics<Schema::count> solve_columns(float avail, const table_prefs &prefs,
                                               float scale = 1.0f)
    {
        constexpr std::size_t N = Schema::count;
        table_metrics<N> m{};
        float fixed = 0.0f, weight = 0.0f;
        for (std::size_t i = 0; i < N; ++i)
        {
            if (Schema::weights[i] > 0.0f)
                weight += Schema::weights[i];
            else
                fixed += Schema::widths[i] * scale;
        }
        const float inner = std::max(0.0f, avail - prefs.pad * static_cast<float>(N + 1));
        const float flex = std::max(0.0f, inner - fixed);
        float x = prefs.pad;
        for (std::size_t i = 0; i < N; ++i)
        {
            const float w = Schema::weights[i] > 0.0f ? flex * Schema::weights[i] / weight
                                                      : Schema::widths[i] * scale;
            m.x[i] = x;
            m.w[i] = static_cast<float>(static_cast<int>(w));
            x += m.w[i] + prefs.pad;
        }
        m.w[N - 1] += std::max(0.0f, avail - x);
        return m;
    }

    // ── 5. 算法层：可见行窗口（定高，O(1)）─────────────────────────
    inline row_window window_for(float scroll, float body_h, std::size_t n, float step)
    {
        if (n == 0 || step <= 0.0f)
            return {};
        const std::size_t first = static_cast<std::size_t>(scroll / step);
        const std::size_t span = static_cast<std::size_t>(body_h / step) + 2;
        return {std::min(first, n), std::min(n, first + span)};
    }

    // ── 6. 算法层：一个单元格 → glyph 模板（相对 cell 原点）──────────
    struct cell_glyphs
    {
        std::vector<shader_data::Glyph> glyphs;
        float advance = 0.0f;
    };

    template <class Font>
    cell_glyphs shape_cell(Font &font, std::string_view text, float font_px, float vw,
                           float vh)
    {
        namespace font_ns = mcs::vulkan::font;
        cell_glyphs out;
        if (text.empty())
            return out;

        std::string owned{text};
        auto norm = font_ns::utf8proc::normalize(owned.c_str());
        auto analyze = font_ns::bidi::analyze(norm.codepoints, 0);
        auto runs = font_ns::assign_fonts(analyze, font);
        auto shaped = font_ns::harfbuzz::shape(analyze.mirrored_codepoints, runs,
                                               font.notdefFont());

        float cursor = 0.0f;
        for (const auto &run : shaped)
            for (const auto &g : run)
            {
                if (!(g.plane_bounds == decltype(g.plane_bounds){}))
                    out.glyphs.push_back(make_glyph(g, vw, vh, font_px, cursor, 0.0f, 0,
                                                    0, ~0u, glm::vec4(1.0f), 1));
                cursor += g.advance_x * font_px;
            }
        out.advance = cursor;
        return out;
    }

    // ── 7. 发射器：像素矩形 → Rectangle 实例 ───────────────────────
    inline void push_rect(std::vector<shader_data::Rectangle> &out, float x, float y,
                          float w, float h, glm::vec4 color, float vw, float vh,
                          float radius = 0.0f)
    {
        shader_data::Rectangle r{};
        r.effects = shader_data::FX_ROUNDED | shader_data::FX_FILL;
        for (auto &c : r.colors)
            c = color;
        const float cx = (x + w * 0.5f) / vw * 2.0f - 1.0f;
        const float cy = (y + h * 0.5f) / vh * 2.0f - 1.0f;
        const float sx = w / vw * 2.0f;
        const float sy = h / vh * 2.0f;
        r.model = glm::translate(glm::mat4(1.0f), glm::vec3(cx, cy, 0.0f));
        r.vertexTransform.matrix = glm::scale(glm::mat4(1.0f), glm::vec3(sx, sy, 1.0f));
        r.uvTransform = UvTransform::identity();
        r.size = glm::vec2(sx, sy);
        r.radiusSoftness = glm::vec4(radius, 0.0015f, 0.0f, 0.0f);
        out.push_back(r);
    }

    // ── 8. 发射器：glyph 模板 + 像素平移 → Glyph 实例 ───────────────
    inline void push_text(std::vector<shader_data::Glyph> &out, const cell_glyphs &cg,
                          float x, float baseline, float vw, float vh, glm::vec4 color)
    {
        const float dx = x / vw * 2.0f;
        const float dy = baseline / vh * 2.0f;
        for (auto g : cg.glyphs)
        {
            g.model[3][0] += dx;
            g.model[3][1] += dy;
            g.color = color;
            g.modulateFlag = 1;
            g.hover_fn = ~0u;
            out.push_back(g);
        }
    }

    // ── 9. 具体表：列 schema + 行数据 ──────────────────────────────
    using demo_schema =
        table_schema<column<"Name", 220>, column<"Status", 0, 1>, column<"Size", 80>,
                     column<"Modified", 130>, column<"Action", 70>>;

    using demo_row = std::array<std::string, demo_schema::count>;

    inline std::vector<demo_row> demo_rows()
    {
        std::vector<demo_row> rows;
        const char *names[] = {"atlas.msdf", "ui.frag", "table.vert", "scene.gltf",
                               "font.json"};
        const char *status[] = {"ready", "busy", "ready", "error", "ready"};
        for (int i = 0; i < 18; ++i)
        {
            rows.push_back(demo_row{
                std::string(names[i % 5]) + "#" + std::to_string(i),
                status[i % 5],
                std::to_string(12 + i * 7) + ".5 KB",
                "2026-10-" + std::to_string(10 + i % 20),
                (i % 3) ? "open" : "edit",
            });
        }
        return rows;
    }

    // ── 10. 对象层：表 = 数据 + 端口 ───────────────────────────────
    template <class Schema>
    auto make_table(std::vector<std::array<std::string, Schema::count>> rows,
                    FontSelect *font)
    {
        using metrics_t = table_metrics<Schema::count>;
        return make_aggregate<"Table">(
            field<"rows">(std::move(rows)), field<"font">(font),
            field<"prefs">(table_prefs{}), field<"metrics">(metrics_t{}),
            field<"headers">(std::vector<cell_glyphs>{}),
            field<"cells">(std::vector<cell_glyphs>{}),
            field<"viewport">(glm::vec2{0.0f, 0.0f}), field<"built">(false),

            // 缓存：viewport 变了才重新整形
            method<"rebuild">([](auto &self, float vw, float vh) {
                self.viewport = glm::vec2{vw, vh};
                self.headers.clear();
                self.cells.clear();
                self.headers.reserve(Schema::count);
                self.cells.reserve(self.rows.size() * Schema::count);
                for (std::size_t c = 0; c < Schema::count; ++c)
                    self.headers.push_back(
                        shape_cell(*self.font, Schema::names[c].view(), 15.0f, vw, vh));
                for (const auto &row : self.rows)
                    for (std::size_t c = 0; c < Schema::count; ++c)
                        self.cells.push_back(
                            shape_cell(*self.font, row[c], 15.0f, vw, vh));
                self.built = true;
            }),

            method<"render">([](auto &self, ui_new::ScreenWidget *, ui_new::Widget *owner,
                                render_context &ctx) {
                const auto &viewports = ctx.drawRecorder.currentDynamic.viewports;
                if (viewports.empty())
                    return;
                // 用真实 framebuffer 尺寸，不用 ScreenWidget 的逻辑尺寸
                const float vw = viewports[0].width;
                const float vh = viewports[0].height;
                if (!self.built || self.viewport.x != vw || self.viewport.y != vh)
                    self.template invoke<"rebuild">(vw, vh);
                if (self.rows.empty())
                    return;

                constexpr std::size_t N = Schema::count;
                const auto &prefs = self.prefs;
                const float margin = 40.0f;
                const float top = 100.0f;
                const float x0 = static_cast<float>(owner->offset.x) + margin;
                const float y0 = static_cast<float>(owner->offset.y) + top;
                const float w = vw - margin * 2.0f;
                const float h = vh - top - margin;
                if (w < 260.0f || h < 160.0f)
                    return;

                const float scale = vw / 800.0f;
                self.metrics = solve_columns<Schema>(w, prefs, scale);
                const auto &m = self.metrics;
                const float step = prefs.row_h + prefs.row_gap;
                const float body_h = h - prefs.header_h;
                // 不要在 render 里读 clock：两个 frame-in-flight 的 recorder
                // 是在不同时刻录的，会各自存下不同的 scroll；交替呈现时
                // 整张表就在两个位置之间抖。ABCDE 是静态的，所以没事。
                // 要做动画，应由每帧唯一的 tick 更新 scroll，并每帧重录。
                const float scroll = 0.0f;
                const auto win = window_for(scroll, body_h, self.rows.size(), step);

                // ── 阶段 1：外框 + 表头（全屏裁剪）─────────────────
                std::vector<shader_data::Rectangle> rects;
                std::vector<shader_data::Glyph> glyphs;
                rects.reserve(32);
                glyphs.reserve(N * 8);

                push_rect(rects, x0, y0, w, h, {0.10f, 0.11f, 0.14f, 0.97f}, vw, vh,
                          0.018f);
                push_rect(rects, x0, y0, w, prefs.header_h, {0.17f, 0.19f, 0.25f, 1.0f},
                          vw, vh, 0.01f);
                for (std::size_t c = 0; c < N; ++c)
                    push_text(glyphs, self.headers[c], x0 + m.x[c], y0 + 26.0f, vw, vh,
                              {0.85f, 0.87f, 0.92f, 1.0f});
                for (std::size_t c = 1; c < N; ++c)
                    push_rect(rects, x0 + m.x[c] - prefs.pad * 0.5f, y0 + 6.0f, 1.0f,
                              prefs.header_h - 12.0f, {1, 1, 1, 0.08f}, vw, vh);
                if (!rects.empty())
                    ctx.drawRecorder.addInstances(
                        std::span<const shader_data::Rectangle>{rects});
                if (!glyphs.empty())
                    ctx.drawRecorder.addInstances(
                        std::span<const shader_data::Glyph>{glyphs});

                // ── 阶段 2：行内容（裁剪到 body）───────────────────
                rects.clear();
                glyphs.clear();
                const VkRect2D body_sc{
                    .offset = {static_cast<int32_t>(x0),
                               static_cast<int32_t>(y0 + prefs.header_h)},
                    .extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(body_h)}};
                const auto saved = ctx.drawRecorder.currentDynamic.scissors;
                ctx.drawRecorder.setScissors(std::span<const VkRect2D>{&body_sc, 1});

                const int hot_row = -1;
                for (std::size_t i = win.first; i < win.last; ++i)
                {
                    const float ry =
                        y0 + prefs.header_h + static_cast<float>(i) * step - scroll;
                    glm::vec4 bg = (i % 2) ? glm::vec4{0.13f, 0.14f, 0.18f, 0.95f}
                                           : glm::vec4{0.15f, 0.16f, 0.21f, 0.95f};
                    if (static_cast<int>(i) == hot_row)
                        bg = {0.19f, 0.22f, 0.30f, 0.98f};
                    if (static_cast<int>(i) == prefs.selected)
                        bg = {0.19f, 0.31f, 0.44f, 0.98f};
                    push_rect(rects, x0, ry, w, prefs.row_h, bg, vw, vh, 0.004f);

                    for (std::size_t c = 0; c < N; ++c)
                    {
                        glm::vec4 fg{0.84f, 0.86f, 0.91f, 1.0f};
                        if (c == 1)
                        {
                            if (self.rows[i][c] == "ready")
                                fg = {0.45f, 0.85f, 0.55f, 1.0f};
                            else if (self.rows[i][c] == "busy")
                                fg = {0.95f, 0.75f, 0.35f, 1.0f};
                            else
                                fg = {0.95f, 0.45f, 0.45f, 1.0f};
                        }
                        if (c == 4)
                            fg = {0.55f, 0.72f, 0.95f, 1.0f};
                        push_text(glyphs, self.cells[i * N + c], x0 + m.x[c], ry + 21.0f,
                                  vw, vh, fg);
                    }
                }
                if (!rects.empty())
                    ctx.drawRecorder.addInstances(
                        std::span<const shader_data::Rectangle>{rects});
                if (!glyphs.empty())
                    ctx.drawRecorder.addInstances(
                        std::span<const shader_data::Glyph>{glyphs});
                ctx.drawRecorder.setScissors(std::span<const VkRect2D>{saved});

                // ── 阶段 3：外框线 + 列线 + 滚动条（全屏裁剪）───────
                rects.clear();
                const glm::vec4 line{1, 1, 1, 0.16f};
                push_rect(rects, x0, y0, w, 1.0f, line, vw, vh);
                push_rect(rects, x0, y0 + h - 1.0f, w, 1.0f, line, vw, vh);
                push_rect(rects, x0, y0, 1.0f, h, line, vw, vh);
                push_rect(rects, x0 + w - 1.0f, y0, 1.0f, h, line, vw, vh);
                for (std::size_t c = 1; c < N; ++c)
                    push_rect(rects, x0 + m.x[c] - prefs.pad * 0.5f, y0 + prefs.header_h,
                              1.0f, h - prefs.header_h, {1, 1, 1, 0.07f}, vw, vh);

                // 静态表：先不画滚动条（没有滚动状态时画了反而误导）
                if (!rects.empty())
                    ctx.drawRecorder.addInstances(
                        std::span<const shader_data::Rectangle>{rects});
            }));
    }
} // namespace table_demo

int main()
try
{
#ifdef VERT_SHADER_PATH
    std::cout << "VERT_SHADER_PATH: " << VERT_SHADER_PATH << '\n';
#endif
#ifdef FRAG_SHADER_PATH
    std::cout << "FRAG_SHADER_PATH: " << FRAG_SHADER_PATH << '\n';
#endif
    auto hardwareCtx = init();
    auto &window = *hardwareCtx.window.get();
    auto &physical_device = *hardwareCtx.physicalDevice.get();
    auto &surface = *hardwareCtx.surface.get();
    auto &device = *hardwareCtx.device.get();
    auto &GRAPHICS_AND_PRESENT = *hardwareCtx.queue.get();
    auto &commandPool = *hardwareCtx.commandPool.get();
    auto &commandBuffers = *hardwareCtx.commandBuffers.get();

    auto &swapchainBuild = hardwareCtx.swapchainBuild;
    auto &swapchain = hardwareCtx.swapchain;
    auto &frameContext = hardwareCtx.frameContext;

    auto descriptorCtx = descriptorInit(hardwareCtx);
    auto &descriptorSetLayout = *descriptorCtx.descriptorSetLayout.get();
    auto &descriptorPool = *descriptorCtx.descriptorPool.get();
    auto &descriptorSets = *descriptorCtx.descriptorSets.get();
    auto &uniformBuffers = *descriptorCtx.uniformBuffers.get();
    auto &textureManager = *descriptorCtx.textureManager.get();
    auto &samplerManager = *descriptorCtx.samplerManager.get();

    auto &descriptorSetManager = descriptorCtx.descriptorSetManager;

    static uint32_t UITextureIndex =
        textureManager.find_slot_by_name(texture_ui_key).value();

    auto fontCtx = initFont(hardwareCtx, descriptorCtx);
    auto &fontFactory = *fontCtx.fontFactory.get();
    auto &fontSelect = fontCtx.fontSelect;
    using FontContext = std::remove_cvref_t<decltype(fontFactory)>::font_context_type;

    descriptorSetManager.update_uniform_buffer();
    descriptorSetManager.update_texture(textureManager.view_used_indexes());
    descriptorSetManager.update_sampler(samplerManager.view_used_indexes());

    auto inputDataCtx = inputInit(swapchain.refImageExtent().width /
                                  static_cast<float>(swapchain.refImageExtent().height));
    auto inputCtx = InputCtx(inputDataCtx);
    using input_type = InputCtx;

    //diff: [test_dod12] end

    auto meshManager = initMeshManager();
    auto &[allVertices, allIndices, meshMap] = meshManager;
    auto drawRecorder = initDrawRecorder(device);
    for (auto &recorder : drawRecorder)
    {
        recorder.globalVertexBuffer.write(0, allVertices.data(),
                                          allVertices.size() * sizeof(allVertices[0]));
        recorder.globalIndexBuffer.write(0, allIndices.data(),
                                         allIndices.size() * sizeof(allIndices[0]));
    }

    // ============================================================
    // 简单 Glyph 测试：只 shape 一个字符 "A"，生成 1 个 Glyph 实例。
    // 目标：验证迁移后的着色器配置（test_dod19.vert/frag，Glyph-only，
    //       移植自 test_sdf）能从实例堆读取 Glyph 并正确渲染 MSDF 字形。
    // ============================================================
    auto textResult = run_text_pipeline(fontSelect, u8"A", "zh-CN");
    const auto &shapeResult = textResult.shape_result;
    using ShapeResult = std::remove_cvref_t<decltype(shapeResult)>;
    // ── 文本 Unicode：真实 shape 结果驱动的多语言行 ──────────────────────
    auto shapeLine = [&](const char *utf8) {
        text_line line;
        auto res = run_text_pipeline(fontSelect, utf8, "zh-CN");
        for (const auto &run : res.shape_result)
        {
            for (const auto &g : run)
            {
                text_glyph tg{};
                tg.uv = {static_cast<float>(g.uv_bounds.left),
                         static_cast<float>(g.uv_bounds.top),
                         static_cast<float>(g.uv_bounds.right),
                         static_cast<float>(g.uv_bounds.bottom)};
                tg.plane = {static_cast<float>(g.plane_bounds.left),
                            static_cast<float>(g.plane_bounds.top),
                            static_cast<float>(g.plane_bounds.right),
                            static_cast<float>(g.plane_bounds.bottom)};
                tg.advance = static_cast<float>(g.advance_x);
                tg.tex = g.font_ctx->bind.texture_index;
                tg.samp = g.font_ctx->bind.sampler_index;
                // FontType 决定着色器走距离场还是位图采样
                using FT = mcs::vulkan::font::FontType;
                const FT ftype = g.font_ctx->type;
                const bool isDistance =
                    (ftype == FT::eSDF || ftype == FT::ePSDF ||
                     ftype == FT::eMSDF || ftype == FT::eMTSDF);
                tg.fontType = isDistance ? 0u : 1u;
                line.glyphs.push_back(tg);
                line.width += tg.advance + static_cast<float>(g.offset_x);
            }
        }
        return line;
    };
    // ASCII 字形表：一次 shape 全部可打印字符，之后所有标签都走这张表
    //   配对用 HarfBuzz 的 logical_idx（连字也不会错位）
    {
        auto &glyphs = uiAsciiGlyphs();
        const char *corpus = " !\"#$%&'()*+,-./"
                             "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
                             "abcdefghijklmnopqrstuvwxyz{|}~";
        auto ascii = shapeLine(corpus);
        for (const auto &g : ascii.glyphs)
        {
            (void)g;
        }
        std::size_t idx = 0;
        for (const auto &run :
             run_text_pipeline(fontSelect, corpus, "zh-CN").shape_result)
        {
            for (const auto &g : run)
            {
                const std::size_t ci = g.logical_idx;
                if (ci >= std::strlen(corpus))
                    continue;
                text_glyph tg{};
                tg.uv = {static_cast<float>(g.uv_bounds.left),
                         static_cast<float>(g.uv_bounds.top),
                         static_cast<float>(g.uv_bounds.right),
                         static_cast<float>(g.uv_bounds.bottom)};
                tg.plane = {static_cast<float>(g.plane_bounds.left),
                            static_cast<float>(g.plane_bounds.top),
                            static_cast<float>(g.plane_bounds.right),
                            static_cast<float>(g.plane_bounds.bottom)};
                tg.advance = static_cast<float>(g.advance_x);
                tg.tex = g.font_ctx->bind.texture_index;
                tg.samp = g.font_ctx->bind.sampler_index;
                glyphs[static_cast<char32_t>(static_cast<unsigned char>(corpus[ci]))] =
                    tg;
                ++idx;
            }
        }
        std::println("[TEXT] ascii glyph table: {} codepoints", glyphs.size());
    }
    {
        auto &lines = uiUnicodeLines();
        lines[0] = shapeLine("UI ABILITY");
        lines[1] = shapeLine("你好，世界");
        lines[2] = shapeLine("مرحبا");
        lines[3] = shapeLine("😀 OK"); // 😀 在 atlas 里（🙂 不在）
        for (int i = 0; i < 4; ++i)
        {
            const auto &ln = lines[static_cast<std::size_t>(i)];
            std::println("[TEXT] line {}: {} glyphs tex={}", i, ln.glyphs.size(),
                         ln.glyphs.empty() ? 0u : ln.glyphs[0].tex);
        }
    }

    // 离屏层：blur / backdrop / group 的真实渲染目标（每帧 4 张）
    initUiLayers(device, commandPool, GRAPHICS_AND_PRESENT,
                 swapchainBuild.refImageFormat(),
                 static_cast<uint32_t>(
                     samplerManager.find_slot_by_name(font_linear_sampler_key).value()),
                 textureManager, descriptorSetManager);

    // 真实 UI 纹理：九宫格源图 / alpha 遮罩 / 带 mip 链的棋盘
    initUiTextures(device, commandPool, GRAPHICS_AND_PRESENT,
                   static_cast<uint32_t>(
                       samplerManager.find_slot_by_name(ui_clamp_sampler_key).value()),
                   textureManager, descriptorSetManager);

    // 字体图集是在 run_text_pipeline 里懒加载的（槽位 4 起），
    // 必须在这之后再把新槽位写进描述符数组，否则着色器采样到的是未绑定的槽位。
    descriptorSetManager.update_texture(textureManager.view_used_indexes());
    descriptorSetManager.update_sampler(samplerManager.view_used_indexes());

    auto mainPipelineCtx = initPipeline(hardwareCtx, descriptorCtx);
    auto &[pipelineLayout, pipelineOpaque3D, pipelineTransparent3D, pipelineOpaqueUI,
           pipelineTransparentUI, pipelineUILayer, swapchainAttachments,
           pickingAttachments] = mainPipelineCtx;
    auto &[depthResourcesBuild, depthResource, msaaResourcesBuild, msaaResource] =
        swapchainAttachments;
    auto &[pickResourcesBuild, pickResource, resolveResourcesBuild, resolveResource,
           pickingFrames, pickMouse] = pickingAttachments;

    struct record_info
    {
        uint32_t current_frame;
        uint32_t image_index;
    };

    auto pickCtx =
        make_aggregate_ref<"pickCtx", "pickResourcesBuild", "pickResource",
                           "resolveResourcesBuild", "resolveResource", "frames", "mouse">(
            pickResourcesBuild, pickResource, resolveResourcesBuild, resolveResource,
            pickingFrames, pickMouse);
    // 路径描边段缓冲（批处理）：每段 3 个 vec4（4 角 + 2 端点），走 binding 3
    shader_data::BufferResource pathSegments{
        device, 64u * 1024u, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
    {
        VkDescriptorBufferInfo bufferInfo{
            .buffer = pathSegments.buffer.buffer(), .offset = 0, .range = VK_WHOLE_SIZE};
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            VkWriteDescriptorSet write{.sType = sType<VkWriteDescriptorSet>(),
                                       .dstSet = descriptorSets[i],
                                       .dstBinding = 3,
                                       .descriptorCount = 1,
                                       .descriptorType =
                                           VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                       .pBufferInfo = &bufferInfo};
            device.updateDescriptorSets(1, &write, 0, nullptr);
        }
    }

    // 调试用：把 swapchain 图像整帧拷回 CPU，写成 PPM（验证靠像素，不靠屏幕截图）
    shader_data::BufferResource frameDump{
        device,
        static_cast<VkDeviceSize>(swapchain.refImageExtent().width) *
            swapchain.refImageExtent().height * 4u,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_SHARING_MODE_EXCLUSIVE,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};

    auto globalCtx =
        make_aggregate_ref<"globalCtx", "device", "window", "surface", "swapchainBuild",
                           "swapchain", "frameContext", "commandPool", "commandBuffers",
                           "queue", "meshMap", "frameDump", "pathSegments">(
            device, window, surface, swapchainBuild, swapchain, frameContext, commandPool,
            commandBuffers, GRAPHICS_AND_PRESENT, meshMap, frameDump, pathSegments);

    auto mainCtx =
        make_aggregate_ref<"mainCtx", "pipelineOpaque3D", "pipelineTransparent3D",
                           "pipelineOpaqueUI", "pipelineTransparentUI", "pipelineUILayer",
                           "pipelineLayout", "depthResourcesBuild", "depthResource",
                           "msaaResourcesBuild", "msaaResource">(
            pipelineOpaque3D, pipelineTransparent3D, pipelineOpaqueUI,
            pipelineTransparentUI, pipelineUILayer, pipelineLayout, depthResourcesBuild,
            depthResource, msaaResourcesBuild, msaaResource);

    auto mainShaderCtx = make_aggregate_ref<"mainShaderCtx", "drawRecorder",
                                            "uniformBuffers", "descriptorSets">(
        drawRecorder, uniformBuffers, descriptorSets);

    // ===== hover：全局唯一一份 实体→函数 关联（外键 = 池实体下标）=====
    using recognizer_tree = intent::leaf<app::click_template_recognizer>;
    auto textBox = make_aggregate<"TextBox">(
        // ── 数据字段 ─────────────────────────────────────────
        field<"text">(std::string("CD")),
        field<"textGlyphProxies">(std::vector<proxy_value<GlyphPool>>{}),
        field<"cachedGlyphs">(std::vector<shader_data::Glyph>{}),
        field<"cachedOwnerOffset">(ui_new::Offset{-1e30, -1e30}),
        field<"cachedViewport">(glm::vec2{-1.0f, -1.0f}),
        field<"owner">(static_cast<ui_new::Widget *>(nullptr)),

        // ════════════════════════════════════════════════════════
        // hover —— 用户逻辑，就地写
        // ════════════════════════════════════════════════════════
        method<"hover_0">([](auto &&self, picking_result r, bool enter,
                             InputCtx &inputCtx, ui_new::ScreenWidget *screen) noexcept {
            std::println("[hover_0] type={} entity={} primitive={} "
                         "ver={} {} (hover_fn={}) text={}",
                         r.key.object_type, r.key.entity_index, r.primitive_id(),
                         r.render_version(), enter ? "ENTER" : "LEAVE", r.hover_fn,
                         self.text);
            //NOTE: 是有Self的类型信息的。enter 决定加载和卸载，以及当前的处理函数
        }),
        method<"hover_1">([](auto &&self, picking_result r, bool enter,
                             InputCtx &inputCtx, ui_new::ScreenWidget *screen) noexcept {
            std::println("[hover_1] type={} entity={} primitive={} "
                         "ver={} {} (hover_fn={}) text={}",
                         r.key.object_type, r.key.entity_index, r.primitive_id(),
                         r.render_version(), enter ? "ENTER" : "LEAVE", r.hover_fn,
                         self.text);
        }),

        // ════════════════════════════════════════════════════════
        // render —— 只负责「缓存校验 + 提交」
        // ════════════════════════════════════════════════════════
        method<"render">([](auto &&self, ui_new::ScreenWidget *screen,
                            ui_new::Widget *owner, render_context &ctx) {
            (void)screen;

            const auto &viewports = ctx.drawRecorder.currentDynamic.viewports;
            if (viewports.empty())
                return;

            const float windowWidth = viewports[0].width;
            const float windowHeight = viewports[0].height;
            const ui_new::Offset off = owner->offset;

            const bool cacheValid = !self.cachedGlyphs.empty() &&
                                    self.cachedOwnerOffset.x == off.x &&
                                    self.cachedOwnerOffset.y == off.y &&
                                    self.cachedViewport.x == windowWidth &&
                                    self.cachedViewport.y == windowHeight;

            if (!cacheValid)
                self.template invoke<"rebuildCache">(owner, windowWidth, windowHeight,
                                                     ctx);

            if (!self.cachedGlyphs.empty())
                ctx.drawRecorder.addInstances(
                    std::span<const shader_data::Glyph>(self.cachedGlyphs));
        }),

        // ════════════════════════════════════════════════════════
        // rebuildCache —— 原 TextRenderObject::rebuildCache 整体
        // ════════════════════════════════════════════════════════
        method<"rebuildCache">([](auto &&self, ui_new::Widget *owner, float windowWidth,
                                  float windowHeight, render_context &ctx) {
            self.textGlyphProxies.clear();
            self.cachedGlyphs.clear();

            self.owner = owner;

            const float fontSizePx = 24.0f; // 两块原本就是同一个值，提到最外层

            // ★ 反查钥匙 owner
            const uint64_t data_ptr = reinterpret_cast<uint64_t>(&self);
            using Self = std::remove_reference_t<decltype(self)>;

            auto emit_one = [&](const auto &g, float cursorX, float baselineY,
                                uint32_t hover_fn_id) -> bool {
                if (g.plane_bounds == decltype(g.plane_bounds){})
                    return false;
                auto [glyph, proxy] =
                    gen_glyph(g, windowWidth, windowHeight, fontSizePx, cursorX,
                              baselineY, data_ptr, hover_fn_id, glm::vec4(1.0f), 1);
                self.textGlyphProxies.push_back(std::move(proxy));
                self.cachedGlyphs.push_back(std::move(glyph));
                return true;
            };

            // ────────────────────────────────────────────────────────────
            // 上层：一整段 shape result → 流式 emit
            //   · 空字形也推进 cursor（advance 已含空格宽度）
            //   · baseline 固定，cursorX 从 startX 起累加
            // ────────────────────────────────────────────────────────────
            auto emit_shape_stream = [&](const auto &shapeResult, float startX,
                                         float baselineY, uint32_t hover_fn_id) {
                float cursorX = startX;
                for (const auto &run : shapeResult)
                    for (const auto &g : run)
                    {
                        emit_one(g, cursorX, baselineY, hover_fn_id);
                        cursorX += g.advance_x * fontSizePx;
                    }
            };

            // ============ 第一块：五位置单字符 ============
            //   布局策略与第二块不同：不是流式，而是"每个字符独立对齐到固定锚点"
            //   所以这里不走 emit_shape_stream，只复用 emit_one。
            {
                auto textResult = run_text_pipeline(ctx.fontSelect, "ABCDE", "zh-CN");
                const auto &shapeResult = textResult.shape_result;

                std::vector<const std::remove_cvref_t<decltype(shapeResult[0][0])> *>
                    glyphPtrs;
                for (const auto &run : shapeResult)
                    for (const auto &g : run)
                        glyphPtrs.push_back(&g);

                if (glyphPtrs.size() >= 5)
                {
                    const float margin = 20.0f;
                    std::array<glm::vec2, 5> positions = {
                        glm::vec2(margin, margin),
                        glm::vec2(windowWidth - margin - fontSizePx, margin),
                        glm::vec2(windowWidth - margin - fontSizePx,
                                  windowHeight - margin - fontSizePx),
                        glm::vec2(margin, windowHeight - margin - fontSizePx),
                        glm::vec2(windowWidth * 0.5f, windowHeight * 0.5f)};
                    const uint32_t hfn = render::HoverFn<Self, "hover_0">::index();
                    for (int i = 0; i < 5; ++i)
                    {
                        const auto &g = *glyphPtrs[i];
                        // 把字形的 plane_bounds 对齐到 positions[i]
                        float cursorX = positions[i].x - g.plane_bounds.left * fontSizePx;
                        // plane_bounds.top 在 y-down 里是负数；baseline 要减去它
                        // 才能让字形的视觉顶部落在 positions[i].y。
                        float baselineY =
                            positions[i].y - g.plane_bounds.top * fontSizePx;
                        emit_one(g, cursorX, baselineY, hfn);
                    }
                }
            }

            // ============ 第二块：正式文本（顶部居中）============
            if (!self.text.empty())
            {
                auto textResult =
                    run_text_pipeline(ctx.fontSelect, self.text.data(), "zh-CN");
                const auto &shapeResult = textResult.shape_result;

                // 总宽 → 居中起点
                float totalWidth = 0.0f;
                for (const auto &run : shapeResult)
                    for (const auto &g : run)
                        totalWidth += g.advance_x * fontSizePx;

                const float topMargin = 20.0f;
                const float baselineY = topMargin + fontSizePx;
                const float startX = (windowWidth - totalWidth) * 0.5f;
                const uint32_t hfn = render::HoverFn<Self, "hover_1">::index();
                emit_shape_stream(shapeResult, startX, baselineY, hfn);
            }

            self.cachedOwnerOffset = owner->offset;
            self.cachedViewport = {windowWidth, windowHeight};
        }),
        // input 相关定义的数据段
        field<"arbiter">(intent::debug_arbiter<recognizer_tree>{}),
        field<"input_state">(input_frame::event_context{}),
        field<"input_binding">(glfw_input_translation::scoped_binding{}),
        field<"click">(app::Trait_click::payload_t{}),
        method<"on_click">([](auto &self, const app::Trait_click::payload_t &e) noexcept {
            self.click = e;
            std::println("arena: on_click");
        }),
        method<"tick">([](auto &self) noexcept {
            const auto now = app::now_ms();
            self.arbiter.tick(self, now);
        }),
        method<"input_event">([](auto &self, auto e) noexcept {
            using frame_t = decltype(input_frame::build_change(self.input_state, e));
            if constexpr (!std::is_void_v<frame_t>)
            {
                auto f = input_frame::build_change(self.input_state, e);
                const auto now = app::now_ms();
                self.arbiter.feed(self, f, now);
                input_frame::apply_change(self.input_state, f);
            }
        })
        //
    );
    // 表：schema 在类型层，rows/prefs 在对象层，render 里只调算法
    auto tableBox = table_demo::make_table<table_demo::demo_schema>(
        table_demo::demo_rows(), &fontSelect);

    ui_new::ScreenWidget screen{ui_new::Size{WIDTH, HEIGHT},
                                ui_new::Container("panel")
                                    .renderObject(render::asRenderObject(
                                        std::move(tableBox), [](auto &) noexcept {}))
                                    .child(ui_new::Container("rectBox").renderObject(
                                        render::asRenderObject(std::move(textBox))))};

    shader_data::hover_manager hoverManager{.screen = &screen};
    // 新的 soaCtx 仅包含 uiRects 和 uiWireRects
    auto soaCtx = make_aggregate_ref<"soaCtx", "screen">(screen);

    // record_info
    auto recordCtx = make_aggregate<"recordCtx", "info">(record_info{});
    auto world =
        make_aggregate_ref<"world", "globalCtx", "mainCtx", "mainShaderCtx", "pickCtx",
                           "recordCtx", "hoverPool", "hoverManager", "fontCtx">(
            globalCtx, mainCtx, mainShaderCtx, pickCtx, recordCtx, hoverPool(),
            hoverManager, fontCtx);
    using world_type = decltype(world);

    using data_type = decltype(soaCtx);

    // diff: [test_dod2] start:  world之后 才能调用 数据API

    // init data

    // 随机生成两个纹理索引和采样器索引

    //diff: [test_dod3] end

    // diff: [test_dod2] end

    static constexpr auto views_matrix_update = [](world_type &world,
                                                   const input_type &inputCtx,
                                                   data_type &soaCtx) {
        const auto &input = inputCtx.input;
        auto &camera = inputCtx.camera; // 类型现在是 GenCamera<...>

        using mcs::vulkan::event::Key;
        const float step = 0.1f;

        // 移动方向由 ViewMatrixObject 的 moveForward/moveRight 等自动处理手系
        if (input.isKeyPressedOrRepeat(Key::eW))
            camera.refView().moveForward(step);
        if (input.isKeyPressedOrRepeat(Key::eS))
            camera.refView().moveForward(-step);
        if (input.isKeyPressedOrRepeat(Key::eA))
            camera.refView().moveRight(-step);
        if (input.isKeyPressedOrRepeat(Key::eD))
            camera.refView().moveRight(step);
        if (input.isKeyPressedOrRepeat(Key::eQ))
            camera.refView().moveUp(-step);
        if (input.isKeyPressedOrRepeat(Key::eE))
            camera.refView().moveUp(step);

        // ========== Ctrl+右键旋转视角 ==========
        {
            static mcs::vulkan::event::position2d_event lastRightPos{};
            static bool isRightDragging = false;
            using mcs::vulkan::event::MouseButtons;

            bool ctrlPressed = input.isKeyPressedOrRepeat(Key::eLEFT_CONTROL) ||
                               input.isKeyPressedOrRepeat(Key::eRIGHT_CONTROL);
            bool curRightPressed =
                input.isMouseButtonPressed(MouseButtons::eMOUSE_BUTTON_RIGHT);

            // clang-format off
            struct DefaultMode {};
            struct PitchOnlyMode {};
            struct YawOnlyMode {};
            struct RollMode {};
            // clang-format on
            using CameraRotateMode =
                std::variant<DefaultMode, PitchOnlyMode, YawOnlyMode, RollMode>;
            constexpr auto getCameraRotateMode =
                [](bool alt, bool shift) noexcept -> CameraRotateMode {
                if (alt && shift)
                    return RollMode{};
                if (alt)
                    return PitchOnlyMode{};
                if (shift)
                    return YawOnlyMode{};
                return DefaultMode{};
            };

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
                        const float sens = 0.005f;
                        auto [rawYaw, rawPitch] =
                            camera::VulkanNDCConfig::ScreenDragToCameraYawPitch(dx, dy,
                                                                                sens);

                        auto &viewObj = camera.refView();
                        glm::vec3 forward = viewObj.getForward();
                        constexpr float maxPitch = glm::radians(89.0f);
                        float curPitch = std::asin(glm::clamp(forward.y, -1.0f, 1.0f));

                        bool altPressed = input.isKeyPressedOrRepeat(Key::eLEFT_ALT) ||
                                          input.isKeyPressedOrRepeat(Key::eRIGHT_ALT);
                        bool shiftPressed =
                            input.isKeyPressedOrRepeat(Key::eLEFT_SHIFT) ||
                            input.isKeyPressedOrRepeat(Key::eRIGHT_SHIFT);

                        glm::quat deltaRot = match(
                            getCameraRotateMode(altPressed, shiftPressed),
                            [&](DefaultMode) noexcept {
                                float pitch =
                                    glm::clamp(curPitch + rawPitch, -maxPitch, maxPitch) -
                                    curPitch;
                                return glm::angleAxis(rawYaw, glm::vec3(0, 1, 0)) *
                                       glm::angleAxis(pitch, viewObj.getRight());
                            },
                            [&](PitchOnlyMode) noexcept {
                                float pitch =
                                    glm::clamp(curPitch + rawPitch, -maxPitch, maxPitch) -
                                    curPitch;
                                return glm::angleAxis(pitch, viewObj.getRight());
                            },
                            [&](YawOnlyMode) noexcept {
                                return glm::angleAxis(rawYaw, glm::vec3(0, 1, 0));
                            },
                            [&](RollMode) noexcept {
                                float rollAngle = rawYaw;
                                return glm::angleAxis(rollAngle, forward);
                            });
                        viewObj.rotateWorld(deltaRot);
                    }
                    lastRightPos = cur;
                }
            }
            else
            {
                isRightDragging = false;
            }
        }
    };

    static constexpr auto views_perspective_update =
        [](world_type &world, const input_type &inputCtx, data_type &soaCtx) {
            const auto &input = inputCtx.input;
            auto &camera = inputCtx.camera;
            using mcs::vulkan::event::Key;

            constexpr float stepDeg = 1.0f; // 度数步长
            constexpr float aspectStep = 0.1f;
            auto &proj = camera.refProjection();

            // FOV 增大（限制不超过 179°）
            if (input.isKeyPressedOrRepeat(Key::eR))
                proj.adjustFovSafe(stepDeg);

            // FOV 减小（必须大于 0°）
            if (input.isKeyPressedOrRepeat(Key::eF))
                proj.adjustFovSafe(-stepDeg);

            // 宽高比增大（无上限，无需保护）
            if (input.isKeyPressedOrRepeat(Key::eT))
                proj.adjustAspect(aspectStep);
            // 宽高比减小（必须大于 0）
            if (input.isKeyPressedOrRepeat(Key::eG))
                proj.adjustAspectSafe(-aspectStep);

            // 近平面减小（必须大于 0）
            if (input.isKeyPressedOrRepeat(Key::eH))
                proj.adjustNearSafe(-stepDeg);
            // 近平面增大（无上限，无需保护）
            if (input.isKeyPressedOrRepeat(Key::eY))
                proj.adjustNearSafe(stepDeg);
            // 远平面增大（无上限，无需保护）
            if (input.isKeyPressedOrRepeat(Key::eU))
                proj.adjustFarSafe(stepDeg);
            // 远平面减小（必须大于 near）
            if (input.isKeyPressedOrRepeat(Key::eJ))
                proj.adjustFarSafe(-stepDeg);

            // 打印当前值
            // std::print("fov: {} deg, aspect: {}, near: {}, far: {}\n", proj.getFov(),
            //            proj.getAspect(), proj.getNear(), proj.getFar());
        };

    // diff: [test_model_matrix3] 小小调整

    static constexpr auto views_ui_camera_update = [](world_type &world,
                                                      const input_type &inputCtx,
                                                      data_type &soaCtx) noexcept {
        const auto &input = inputCtx.input;
        auto &uiCam = inputCtx.uiCamera;
        using mcs::vulkan::event::Key;
        using mcs::vulkan::event::MouseButtons;

        // 窗口尺寸，用于拖拽坐标转换
        auto &swapchain = world.globalCtx.swapchain;
        auto extent = swapchain.refImageExtent();
        float w = static_cast<float>(extent.width);
        float h = static_cast<float>(extent.height);

        // ---------- Alt 修饰键 ----------
        bool alt = input.isKeyPressedOrRepeat(Key::eLEFT_ALT) ||
                   input.isKeyPressedOrRepeat(Key::eRIGHT_ALT);
        if (!alt)
            return;

        // clang-format off
        struct UiTranslateKey { float dx, dy; };
        struct UiDrag { float ndcDx, ndcDy; };
        struct UiZoom { float factor; };
        struct UiRotate { float angle; glm::vec3 axis; };
        struct UiReset {};
        using UiAction = std::variant<UiTranslateKey, UiDrag, UiZoom, UiRotate, UiReset>;
        // clang-format on

        // 统一的执行器（每次直接 match）
        auto exec = [&](const UiAction &action) noexcept {
            match(
                action,
                [&](const UiTranslateKey &t) noexcept {
                    uiCam.refView().moveRight(t.dx);
                    uiCam.refView().moveUp(t.dy);
                },
                [&](const UiDrag &d) noexcept {
                    uiCam.refView().moveRight(d.ndcDx);
                    uiCam.refView().moveUp(d.ndcDy);
                },
                [&](const UiZoom &z) noexcept {
                    uiCam.refProjection().scaleOrthoView(z.factor);
                },
                [&](const UiRotate &r) noexcept {
                    uiCam.refView().rotateWorld(
                        glm::angleAxis(r.angle, glm::normalize(r.axis)));
                },
                [&](const UiReset &) noexcept {
                    uiCam.refView().setPosition(glm::vec3(0.0f));
                    uiCam.refView().setOrientation(glm::identity<glm::quat>());
                    uiCam.refProjection().setOrthoBounds(-1.0f, 1.0f, -1.0f, 1.0f);
                });
        };

        // ---------- 按键平移（Alt + WASD） ----------
        constexpr float keyStep = 0.02f;
        if (input.isKeyPressedOrRepeat(Key::eW))
            exec(UiTranslateKey{0.0f, keyStep}); // 向上
        if (input.isKeyPressedOrRepeat(Key::eS))
            exec(UiTranslateKey{0.0f, -keyStep}); // 向下
        if (input.isKeyPressedOrRepeat(Key::eA))
            exec(UiTranslateKey{-keyStep, 0.0f}); // 向左
        if (input.isKeyPressedOrRepeat(Key::eD))
            exec(UiTranslateKey{keyStep, 0.0f}); // 向右

        // ---------- 缩放（Alt + E/Q） ----------
        constexpr float zoomFactor = 1.05f;
        if (input.isKeyPressedOrRepeat(Key::eE))
            exec(UiZoom{1.0f / zoomFactor}); // 放大
        if (input.isKeyPressedOrRepeat(Key::eQ))
            exec(UiZoom{zoomFactor}); // 缩小

        // ---------- 旋转 ----------
        // 旋转（Alt + R/F 绕 Z，Alt + T/G 绕 Y，Alt + Y/H 绕 X）
        constexpr float rotAngle = glm::radians(10.0f);
        if (input.isKeyPressedOrRepeat(Key::eR))
            exec(UiRotate{rotAngle, glm::vec3(0, 0, 1)});
        if (input.isKeyPressedOrRepeat(Key::eF))
            exec(UiRotate{-rotAngle, glm::vec3(0, 0, 1)});

        if (input.isKeyPressedOrRepeat(Key::eT))
            exec(UiRotate{rotAngle, glm::vec3(0, 1, 0)});
        if (input.isKeyPressedOrRepeat(Key::eG))
            exec(UiRotate{-rotAngle, glm::vec3(0, 1, 0)});
        if (input.isKeyPressedOrRepeat(Key::eY))
            exec(UiRotate{rotAngle, glm::vec3(1, 0, 0)});
        if (input.isKeyPressedOrRepeat(Key::eH))
            exec(UiRotate{-rotAngle, glm::vec3(1, 0, 0)});

        // ---------- 拖拽平移（Alt + 左键） ----------
        {
            static bool wasDragging = false;
            static glm::dvec2 lastCursor{0.0, 0.0};
            bool leftPressed =
                input.isMouseButtonPressed(MouseButtons::eMOUSE_BUTTON_LEFT);

            if (leftPressed)
            {
                auto cur = input.cursorPos();
                if (!wasDragging)
                {
                    lastCursor = {cur.xpos, cur.ypos};
                    wasDragging = true;
                }
                else
                {
                    float dx = static_cast<float>(cur.xpos - lastCursor.x);
                    float dy = static_cast<float>(cur.ypos - lastCursor.y);
                    float ndcDx = (dx / w) * 2.0f;
                    float ndcDy = (dy / h) * 2.0f;
                    // 注意：拖拽鼠标向上（dy<0） => UI 整体上移 => 相机需下移
                    // 相机需要反向移动才能让 UI 跟随鼠标
                    exec(UiDrag{-ndcDx, -ndcDy}); // 之前只有 y 取负，现已修正 x 也取负
                    lastCursor = {cur.xpos, cur.ypos};
                }
            }
            else
            {
                wasDragging = false;
            }
        }

        // ---------- 重置（Alt + Z） ----------
        if (input.isKeyPressedOrRepeat(Key::eZ))
            exec(UiReset{});
    };

    // diff: [test_model_matrix2] end
    static constexpr auto updateVertexData = [](world_type &world, input_type &inputCtx,
                                                data_type &soaCtx,
                                                uint32_t currentFrame) noexcept {

    };

    // diff: [test_indirectdraw] start prepareBatch：动态分配合批资源并填充数据 // NOLINTNEXTLINE
    static constexpr auto prepareBatch = [](world_type &world, input_type &inputCtx,
                                            data_type &soaCtx, uint32_t currentFrame) {
        auto &globalCtx = world.globalCtx;
        auto &mainShaderCtx = world.mainShaderCtx;

        auto &meshMap = globalCtx.meshMap;
    };

    // diff: [test_indirectdraw] end
    // diff: [test_dod5] start
    // ═══════════════════════════════════════════════════════════════════════
    // UI 命令层（C 风格）
    //
    //   一条 UI 命令 = 一个全屏三角形 + 一组推送常量：
    //       setScissor    → 这条命令的裁剪
    //       pushConstants → 这条命令的全部参数（mode/rect/颜色/几何）
    //       draw(3)       → 全屏三角形，形状由片元着色器按 mode 求 SDF
    //
    //   能力全部在 test_ui_ability.frag 的 mode 表里（与下面的枚举一一对应），
    //   这里只按顺序记录命令，不做批处理、不做间接绘制、没有后端对象。
    //
    //   按约定：除 draw 之外，mainPipeline.xxx 全部留空。
    // ═══════════════════════════════════════════════════════════════════════
    enum ui_mode : int
    {
        MODE_FILL = 0,        // 圆角矩形填充
        MODE_LINEAR,          // 线性渐变
        MODE_RADIAL,          // 径向渐变
        MODE_CONIC,           // 圆锥渐变
        MODE_ARC,             // 圆弧/环
        MODE_OUTER_SHADOW,    // 外阴影
        MODE_INNER_SHADOW,    // 内阴影
        MODE_PERCORNER,       // 每角半径
        MODE_ALPHA_MASK,      // alpha 遮罩
        MODE_PATH_AA,         // 路径解析抗锯齿
        MODE_DASH,            // 虚线描边
        MODE_ROTATE,          // 旋转
        MODE_SKEW,            // 斜切
        MODE_NINE_SLICE,      // 九宫格
        MODE_EVENODD,         // 奇偶填充
        MODE_GLYPH,           // MSDF 字形
        MODE_SRGB,            // sRGB / 线性渐变
        MODE_BLUR,            // 模糊
        MODE_BACKDROP,        // 背景模糊
        MODE_GROUP,           // 组不透明度
        MODE_MIPMAP,          // mipmap
        MODE_PICKING,         // 拾取
        MODE_BLEND,           // 混合模式
        MODE_LAYER_BLUR,      // 离屏可分离模糊（y=方向 z=半径 w=源纹理 aux.x=源采样器）
        MODE_LAYER_COMPOSITE, // 离屏合成（y=不透明度 z=源纹理 w=源采样器 aux.x=圆角）
        MODE_CHECKER,         // 棋盘背景内容（背景模糊的来源）
        MODE_NINE_SLICE_TEX,  // 纹理九宫格（y=源边框 z=源纹理 w=源采样器 aux.xy=源尺寸）
        MODE_ALPHA_MASK_TEX,  // 纹理 alpha 遮罩（z=纹理 w=源采样器）
        MODE_MIPMAP_TEX,      // 真 mip 采样（y=最大 lod z=纹理 w=源采样器）
        MODE_CLIP_MASK,       // 模板裁剪遮罩（形状外 discard）
        MODE_SOLID,           // 纯色覆盖（几何 / 模板覆盖 pass 用）
        MODE_SOLID_GEOM,      // 几何覆盖（颜色写死）
        MODE_PATH_STROKE,     // 路径描边：解析覆盖率 AA（y=半宽 z=1 开 AA）
        MODE_GLYPH_BITMAP,    // 位图字形（emoji）：图集 RGBA，不走距离场
    };

    // 几何入口标记（PushData.geomEntry 是专用字段）：0 = 普通全屏三角形
    constexpr uint32_t kGeomStroke = 1u;       // 描边批处理（段数据在 storage buffer）
    constexpr uint32_t kGeomFan = 2u;          // 填充扇形批处理（顶点在 buffer 高位区）
    constexpr uint32_t kFanVertexBase = 2048u; // 与 VS 里 kFanBase 一致

    static constexpr auto mainPipeline = make_aggregate<"mainPipeline">(
        // NOTE: 布局转换 / 开始渲染 / 管线状态 / 结束渲染 都并进 draw 了
        method<"transitionImageLayout">([](world_type &, input_type &, data_type &) {}),
        method<"beginRendering">([](world_type &, input_type &, data_type &) {}),
        method<"setPipelineState">([](world_type &, input_type &, data_type &) {}),
        method<"draw">([](world_type &world, input_type &inputCtx, data_type &soaCtx) {
            auto &globalCtx = world.globalCtx;
            auto &mainCtx = world.mainCtx;
            auto &mainShaderCtx = world.mainShaderCtx;
            auto &recordCtx = world.recordCtx;
            auto &pickCtx = world.pickCtx;

            auto &swapchain = globalCtx.swapchain;
            auto &commandBuffers = globalCtx.commandBuffers;

            // ↓↓↓ 就是它：一个 VkCommandBuffer，后面全是 vkCmd* 级别的调用 ↓↓↓
            const auto &[currentFrame, imageIndex] = recordCtx.info;
            const auto &commandBuffer = commandBuffers[currentFrame];

            auto &pipelineLayout = mainCtx.pipelineLayout;
            auto &descriptorSets = mainShaderCtx.descriptorSets;
            auto descriptorSet = descriptorSets[currentFrame];

            const VkExtent2D extent = swapchain.refImageExtent();
            const float W = static_cast<float>(extent.width);
            const float H = static_cast<float>(extent.height);

            VkImage image = swapchain.image(imageIndex);
            const VkImage depthImage = mainCtx.depthResource.image();
            const VkImage msaaImage = mainCtx.msaaResource.image();
            const VkImage pickingImage = pickCtx.pickResource.image();
            const VkImage resolveImage = pickCtx.resolveResource.image();

            // 深度附件：只有带模板分量的格式才能声明 STENCIL aspect，
            // 否则 D32_SFLOAT 这类纯深度格式会触发 VUID-09594 / 09601。
            const bool hasStencil = mainCtx.depthResourcesBuild.hasStencilComponent();
            const VkImageAspectFlags depthAspect =
                hasStencil ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
                           : VK_IMAGE_ASPECT_DEPTH_BIT;

            // ── 1. 布局转换 ─────────────────────────────────────────────
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
                    .aspect_mask = depthAspect,
                    .src = {.layout = VK_IMAGE_LAYOUT_UNDEFINED,
                            .access_mask = VK_ACCESS_2_NONE,
                            .stage_mask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT},
                    .dst = {.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                            .access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                            .stage_mask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                          VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT}},
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
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT}});

            // ── 2. 离屏 pass：blur / group opacity / backdrop blur ────────
            //   先把内容画到离屏层（320x192），再模糊 / 按组 alpha 合成回主通道。
            //   层 A=模糊结果 B=模糊临时 C=组 D=背景
            auto &frameLayers = uiLayers().layers[currentFrame];
            constexpr VkExtent2D LEXT = ui_layer_ctx::kExtent;
            const VkViewport layerViewport{.x = 0.0F,
                                           .y = 0.0F,
                                           .width = static_cast<float>(LEXT.width),
                                           .height = static_cast<float>(LEXT.height),
                                           .minDepth = 0.0F,
                                           .maxDepth = 1.0F};
            const VkRect2D layerScissor{{0, 0}, LEXT};

            auto layerToColor = [&](VkImage layerImage) {
                my_render::transition_image_layout(
                    commandBuffer,
                    my_render::image_info{
                        .image = layerImage,
                        .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .src = {.layout = VK_IMAGE_LAYOUT_UNDEFINED,
                                .access_mask = VK_ACCESS_2_NONE,
                                .stage_mask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT},
                        .dst = {.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                .access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                .stage_mask =
                                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT}});
            };
            auto layerToSampled = [&](VkImage layerImage) {
                my_render::transition_image_layout(
                    commandBuffer,
                    my_render::image_info{
                        .image = layerImage,
                        .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .src = {.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                .access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                .stage_mask =
                                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT},
                        .dst = {.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                .access_mask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                                .stage_mask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT}});
            };
            auto layerBegin = [&](VkImageView layerView) {
                VkRenderingAttachmentInfo attachment{
                    .sType = sType<VkRenderingAttachmentInfo>(),
                    .imageView = layerView,
                    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                    .clearValue = {.color = {.float32 = {0.0F, 0.0F, 0.0F, 0.0F}}}};
                commandBuffer.beginRendering(
                    {.sType = sType<VkRenderingInfo>(),
                     .renderArea = {.offset = {.x = 0, .y = 0}, .extent = LEXT},
                     .layerCount = 1,
                     .colorAttachmentCount = 1,
                     .pColorAttachments = &attachment,
                     .pDepthAttachment = nullptr,
                     .pStencilAttachment = nullptr});
                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *mainCtx.pipelineUILayer);
                commandBuffer.bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                 *pipelineLayout, 0, 1, &descriptorSet, 0,
                                                 nullptr);
                commandBuffer.setViewport(0, std::span{&layerViewport, 1});
            };
            // 层内一条命令：裁剪固定为整层
            auto layerCmd = [&](int mode, VkRect2D box, glm::vec4 color, glm::vec4 color2,
                                glm::vec3 params, glm::vec4 aux) {
                PushData pc{};
                pc.geomEntry = 0; // 普通全屏三角形（1/2 是几何入口，不能再用）
                pc.uiRect = {static_cast<float>(box.offset.x),
                             static_cast<float>(box.offset.y),
                             static_cast<float>(box.extent.width),
                             static_cast<float>(box.extent.height)};
                pc.uiColor = color;
                pc.uiColor2 = color2;
                pc.uiParams = {static_cast<float>(mode), params.x, params.y, params.z};
                pc.uiAux = aux;
                pc.uiPick = {0xFFFFFFFFu, 0xFFFFFFFFu, 0u, 0xFFFFFFFFu};
                commandBuffer.setScissor(0, std::span{&layerScissor, 1});
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(PushData), &pc);
                commandBuffer.draw(3, 1, 0, 0);
            };
            const VkRect2D fullLayer{{0, 0}, LEXT};

            // A：模糊演示的内容（两个圆角块 + 一个圆环）
            layerToColor(frameLayers[0].image);
            layerBegin(frameLayers[0].view);
            layerCmd(MODE_FILL, {{20, 30}, {150, 120}}, {0.95F, 0.55F, 0.25F, 1.0F}, {},
                     {18.0F, 0.0F, 0.0F}, {});
            layerCmd(MODE_FILL, {{130, 60}, {150, 110}}, {0.30F, 0.65F, 0.95F, 0.9F}, {},
                     {24.0F, 0.0F, 0.0F}, {});
            layerCmd(MODE_ARC, fullLayer, {1.0F, 1.0F, 1.0F, 0.9F}, {},
                     {70.0F, 14.0F, 0.0F}, {-2.2F, 1.9F, 0.0F, 0.0F});
            commandBuffer.endRendering();
            layerToSampled(frameLayers[0].image);

            // A →(横) B →(纵) A：可分离高斯模糊
            layerToColor(frameLayers[1].image);
            layerBegin(frameLayers[1].view);
            layerCmd(MODE_LAYER_BLUR, fullLayer, {}, {},
                     {0.0F, 3.0F, static_cast<float>(frameLayers[0].texture)},
                     {static_cast<float>(frameLayers[0].sampler), 0.0F, 0.0F, 0.0F});
            commandBuffer.endRendering();
            layerToSampled(frameLayers[1].image);

            layerToColor(frameLayers[0].image);
            layerBegin(frameLayers[0].view);
            layerCmd(MODE_LAYER_BLUR, fullLayer, {}, {},
                     {1.0F, 3.0F, static_cast<float>(frameLayers[1].texture)},
                     {static_cast<float>(frameLayers[1].sampler), 0.0F, 0.0F, 0.0F});
            commandBuffer.endRendering();
            layerToSampled(frameLayers[0].image);

            // C：组内容（两个重叠的圆），整组最后按同一 alpha 合成
            layerToColor(frameLayers[2].image);
            layerBegin(frameLayers[2].view);
            layerCmd(MODE_FILL, {{26, 36}, {160, 120}}, {0.30F, 0.65F, 0.95F, 1.0F}, {},
                     {60.0F, 0.0F, 0.0F}, {});
            layerCmd(MODE_FILL, {{120, 36}, {160, 120}}, {0.95F, 0.45F, 0.30F, 0.6F}, {},
                     {60.0F, 0.0F, 0.0F}, {});
            commandBuffer.endRendering();
            layerToSampled(frameLayers[2].image);

            // D：背景内容（棋盘）→ 模糊 → 作为毛玻璃后面的“真实背景”
            layerToColor(frameLayers[3].image);
            layerBegin(frameLayers[3].view);
            layerCmd(MODE_CHECKER, fullLayer, {0.08F, 0.10F, 0.16F, 1.0F},
                     {0.55F, 0.70F, 0.95F, 1.0F}, {26.0F, 0.0F, 0.0F}, {});
            commandBuffer.endRendering();
            layerToSampled(frameLayers[3].image);

            layerToColor(frameLayers[1].image);
            layerBegin(frameLayers[1].view);
            layerCmd(MODE_LAYER_BLUR, fullLayer, {}, {},
                     {0.0F, 2.0F, static_cast<float>(frameLayers[3].texture)},
                     {static_cast<float>(frameLayers[3].sampler), 0.0F, 0.0F, 0.0F});
            commandBuffer.endRendering();
            layerToSampled(frameLayers[1].image);

            layerToColor(frameLayers[3].image);
            layerBegin(frameLayers[3].view);
            layerCmd(MODE_LAYER_BLUR, fullLayer, {}, {},
                     {1.0F, 2.0F, static_cast<float>(frameLayers[1].texture)},
                     {static_cast<float>(frameLayers[1].sampler), 0.0F, 0.0F, 0.0F});
            commandBuffer.endRendering();
            layerToSampled(frameLayers[3].image);

            // ── 2. beginRendering：MSAA 颜色 + 拾取 + 深度/模板 ─────────
            VkRenderingAttachmentInfo colorAttachment = {
                .sType = sType<VkRenderingAttachmentInfo>(),
                .imageView = mainCtx.msaaResource.imageView(),
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT,
                .resolveImageView = swapchain.imageView(imageIndex),
                .resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = {.color = {.float32 = {0.05F, 0.055F, 0.07F, 1.0F}}}};
            VkRenderingAttachmentInfo pickAttachment = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = pickCtx.pickResource.imageView(),
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT,
                .resolveImageView = pickCtx.resolveResource.imageView(),
                .resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = {.color = {.uint32 = {0xFFFFFFFFu, 0xFFFFFFFFu, 0u, 0u}}}};
            std::array attachments = {colorAttachment, pickAttachment};
            VkRenderingAttachmentInfo depthAttachment = {
                .sType = sType<VkRenderingAttachmentInfo>(),
                .imageView = mainCtx.depthResource.imageView(),
                .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = {.depthStencil = {.depth = 1.0F}}};

            commandBuffer.beginRendering(
                {.sType = sType<VkRenderingInfo>(),
                 .renderArea = {.offset = {.x = 0, .y = 0}, .extent = extent},
                 .layerCount = 1,
                 .colorAttachmentCount = static_cast<uint32_t>(attachments.size()),
                 .pColorAttachments = attachments.data(),
                 .pDepthAttachment = &depthAttachment,
                 .pStencilAttachment = hasStencil ? &depthAttachment : nullptr});

            // ── 3. 管线 / 描述符 / 视口 ─────────────────────────────────
            commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       *mainCtx.pipelineTransparentUI);
            commandBuffer.bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                             *pipelineLayout, 0, 1, &descriptorSet, 0,
                                             nullptr);
            const VkViewport viewport{.x = 0.0F,
                                      .y = 0.0F,
                                      .width = W,
                                      .height = H,
                                      .minDepth = 0.0F,
                                      .maxDepth = 1.0F};
            commandBuffer.setViewport(0, std::span{&viewport, 1});

            // ── 4. 命令表 ───────────────────────────────────────────────
            // 一条命令 = 矩形 + 两个颜色 + 参数 + 裁剪；params.x 由 mode 填
            struct ui_cmd
            {
                glm::vec4 rect; // xy=左上(px) zw=尺寸(px)
                glm::vec4 color;
                glm::vec4 color2;
                glm::vec4 params; // x=mode, y/z/w=模式参数
                glm::vec4 aux;
                glm::uvec4 pick; // x=命令 id y=实体 z=(version<<16)|primitive w=hover_fn
                VkRect2D scissor;
                int pipe{
                    0}; // 0=UI(over) 1=add 2=multiply 3=erase 4=maskA 5=maskB 6=clipped
                uint32_t ref{0};              // 模板参考值
                int pathKind{0};              // 0=无 1=even-odd 填充 2=描边
                uint32_t pathId{0xFFFFFFFFu}; // ui_path 注册表里的 PathId
                uint32_t pathGen{0};          // PathId 所属的 generation
            };
            std::array<ui_cmd, 1024> cmds{};
            uint32_t cmdCount = 0;

            // 每个命令都可以带自己的拾取 id：id=0xFFFFFFFF 表示不参与拾取
            const uint32_t pickVersion = soaCtx.screen.renderVersion();
            auto add = [&](int mode, VkRect2D box, glm::vec4 color, glm::vec4 color2 = {},
                           glm::vec3 params = {}, glm::vec4 aux = {},
                           VkRect2D scissor = {}, uint32_t id = 0xFFFFFFFFu,
                           uint32_t entity = 0u, uint32_t primitive = 0u, int pipe = 0,
                           uint32_t stencilRef = 0u, int pathKind = 0,
                           uint32_t pathId = 0xFFFFFFFFu, uint32_t pathGen = 0u) {
                const uint32_t packedPick =
                    ((pickVersion & 0xFFFFu) << 16) | (primitive & 0xFFFFu);
                if (cmdCount >= cmds.size())
                    return;
                VkRect2D sc = (scissor.extent.width == 0u || scissor.extent.height == 0u)
                                  ? box
                                  : scissor;
                const int32_t x0 = std::max<int32_t>(sc.offset.x, 0);
                const int32_t y0 = std::max<int32_t>(sc.offset.y, 0);
                const int32_t x1 =
                    std::min<int32_t>(sc.offset.x + static_cast<int32_t>(sc.extent.width),
                                      static_cast<int32_t>(extent.width));
                const int32_t y1 = std::min<int32_t>(
                    sc.offset.y + static_cast<int32_t>(sc.extent.height),
                    static_cast<int32_t>(extent.height));
                if (x1 <= x0 || y1 <= y0)
                    return;
                cmds[cmdCount++] = ui_cmd{
                    {static_cast<float>(box.offset.x), static_cast<float>(box.offset.y),
                     static_cast<float>(box.extent.width),
                     static_cast<float>(box.extent.height)},
                    color,
                    color2,
                    {static_cast<float>(mode), params.x, params.y, params.z},
                    aux,
                    {id, entity, packedPick, 0xFFFFFFFFu},
                    {{x0, y0},
                     {static_cast<uint32_t>(x1 - x0), static_cast<uint32_t>(y1 - y0)}},
                    pipe,
                    stencilRef,
                    pathKind,
                    pathId,
                    pathGen};
            };
            // 便捷入口：要换管线/设模板参考时用它，避免手写 id/entity/primitive 位置参数
            auto addPipe = [&](int mode, VkRect2D box, glm::vec4 color, glm::vec4 color2,
                               glm::vec3 params, glm::vec4 aux, VkRect2D scissor,
                               int pipe, uint32_t ref) {
                add(mode, box, color, color2, params, aux, scissor, 0xFFFFFFFFu, 0u, 0u,
                    pipe, ref);
            };

            // 字形表：由 shape 结果构建（ASCII 全集），不再硬编码 A–Z，
            // 每个字形带自己的 font atlas（fallback 后可能来自不同字体）
            const auto &asciiGlyphs = uiAsciiGlyphs();

            // 唯一的"发一个字形命令"的地方：text() 与 textRich() 都走它
            auto emitGlyph = [&](float pen, float y, float px, glm::vec4 color,
                                 const text_glyph &g, VkRect2D clip) -> float {
                const float gw = (g.plane.z - g.plane.x) * px;
                const float gh = (g.plane.w - g.plane.y) * px;
                if (gw > 0.5F && gh > 0.5F)
                    add((g.fontType == 1u) ? MODE_GLYPH_BITMAP : MODE_GLYPH,
                        {{static_cast<int32_t>(pen + g.plane.x * px),
                          static_cast<int32_t>(y + g.plane.y * px)},
                         {static_cast<uint32_t>(std::ceil(gw)),
                          static_cast<uint32_t>(std::ceil(gh))}},
                        color, {},
                        {2.0F, static_cast<float>(g.tex), static_cast<float>(g.samp)},
                        {g.uv.x, g.uv.y, g.uv.z, g.uv.w}, clip);
                return pen + px * g.advance;
            };

            // 一行 MSDF 文字 = 一串 GLYPH 命令（文字也是命令）
            //   字形数据全部来自 shape 结果（uiAsciiGlyphs），每个字形自带 atlas
            auto text = [&](float x, float y, float px, glm::vec4 color,
                            std::string_view str, VkRect2D clip = {}) -> float {
                float pen = x;
                // UTF-8 解码 → 码点 → 字形
                std::size_t i = 0;
                while (i < str.size())
                {
                    const auto b0 = static_cast<unsigned char>(str[i]);
                    char32_t cp = b0;
                    std::size_t len = 1;
                    if ((b0 & 0x80U) == 0U)
                    {
                        cp = b0;
                    }
                    else if ((b0 & 0xE0U) == 0xC0U && i + 1 < str.size())
                    {
                        cp = ((b0 & 0x1FU) << 6) |
                             (static_cast<unsigned char>(str[i + 1]) & 0x3FU);
                        len = 2;
                    }
                    else if ((b0 & 0xF0U) == 0xE0U && i + 2 < str.size())
                    {
                        cp = ((b0 & 0x0FU) << 12) |
                             ((static_cast<unsigned char>(str[i + 1]) & 0x3FU) << 6) |
                             (static_cast<unsigned char>(str[i + 2]) & 0x3FU);
                        len = 3;
                    }
                    else if ((b0 & 0xF8U) == 0xF0U && i + 3 < str.size())
                    {
                        cp = ((b0 & 0x07U) << 18) |
                             ((static_cast<unsigned char>(str[i + 1]) & 0x3FU) << 12) |
                             ((static_cast<unsigned char>(str[i + 2]) & 0x3FU) << 6) |
                             (static_cast<unsigned char>(str[i + 3]) & 0x3FU);
                        len = 4;
                    }
                    i += len;

                    if (cp == U' ')
                    {
                        pen += px * 0.34F;
                        continue;
                    }
                    const auto it = asciiGlyphs.find(cp);
                    if (it == asciiGlyphs.end())
                    {
                        pen += px * 0.5F;
                        continue;
                    }
                    const text_glyph &g = it->second;
                    pen = emitGlyph(pen, y, px, color, g, clip);
                }
                return pen;
            };

            // 多语言一行：按 shape 结果的视觉顺序逐字形发命令（每字形自己的 atlas）
            auto textRich = [&](float x, float y, float px, glm::vec4 color,
                                const text_line &line) {
                float pen = x;
                for (const auto &g : line.glyphs)
                    pen = emitGlyph(pen, y, px, color, g, {});
                return pen;
            };

            // ── 4.1 页面 + 顶栏 ─────────────────────────────────────────
            add(MODE_FILL, {{0, 0}, extent}, {0.055F, 0.06F, 0.075F, 1.0F});
            add(MODE_LINEAR, {{0, 0}, {extent.width, 56u}}, {0.10F, 0.45F, 0.85F, 1.0F},
                {0.55F, 0.22F, 0.80F, 1.0F}, {14.0F, 0.0F, 0.0F},
                {1.0F, 0.25F, 0.0F, 0.0F});
            text(20.0F, 42.0F, 30.0F, {1.0F, 1.0F, 1.0F, 1.0F}, "UI ABILITY");

            // ── 文本 Unicode 验收区（页面下方空白处）────────────────────
            {
                const float y0 = static_cast<float>(H) - 96.0F;
                for (int i = 0; i < 4; ++i)
                {
                    const glm::vec4 c = (i == 2) ? glm::vec4{0.55F, 0.85F, 1.0F, 1.0F}
                                                 : glm::vec4{0.95F, 0.95F, 0.95F, 1.0F};
                    textRich(20.0F + static_cast<float>(i) * 300.0F, y0, 42.0F, c,
                             uiUnicodeLines()[static_cast<std::size_t>(i)]);
                }
            }

            // ── 4.2 图块布局：固定像素尺寸 + 流式排布 ───────────────────
            //   图块尺寸是像素常量（不随窗口缩放），列/行数由窗口大小算出来：
            //   窗口变大 → 能摆下更多案例；窗口变小 → 自动减少，图块本身不变形。
            constexpr float kTileW = 132.0F;
            constexpr float kTileH = 106.0F;
            constexpr float kGap = 6.0F;
            const float gridTop = 64.0F;
            const int kCols = std::max(1, static_cast<int>((W - kGap) / (kTileW + kGap)));
            const int kRows =
                std::max(1, static_cast<int>((H - gridTop - kGap) / (kTileH + kGap)));
            const int kCapacity = kCols * kRows;
            auto cell = [&](int index) {
                const int cx = index % kCols;
                const int cy = index / kCols;
                return VkRect2D{
                    {static_cast<int32_t>(kGap +
                                          static_cast<float>(cx) * (kTileW + kGap)),
                     static_cast<int32_t>(gridTop +
                                          static_cast<float>(cy) * (kTileH + kGap))},
                    {static_cast<uint32_t>(kTileW), static_cast<uint32_t>(kTileH)}};
            };
            // 是否落在可见范围内（越界的图块整格不画）
            auto inView = [&](int index) {
                return index / kCols < kRows;
            };

            // ═══════════════════════════════════════════════════════
            // 几何前端：展平 / 三角化 / 描边（纯函数，shared）
            // ═══════════════════════════════════════════════════════
            auto flatten_cubic = [](glm::vec2 p0, glm::vec2 p1, glm::vec2 p2,
                                    glm::vec2 p3, int steps) {
                std::vector<glm::vec2> out;
                out.reserve(static_cast<std::size_t>(steps));
                for (int i = 0; i < steps; ++i)
                {
                    const float t = static_cast<float>(i) / static_cast<float>(steps);
                    const float u = 1.0F - t;
                    out.push_back(u * u * u * p0 + 3.0F * u * u * t * p1 +
                                  3.0F * u * t * t * p2 + t * t * t * p3);
                }
                return out;
            };
            auto fanTriangles = [](const ui_path::contour &c) {
                std::vector<glm::vec2> tris;
                const auto &p = c.pts;
                if (p.size() < 3)
                    return tris;
                for (std::size_t i = 1; i + 1 < p.size(); ++i)
                {
                    tris.push_back(p[0]);
                    tris.push_back(p[i]);
                    tris.push_back(p[i + 1]);
                }
                return tris;
            };
            // 描边段（miter 接头）：每段 20 float = 5 个 vec4
            //   角点 = 顶点处的斜接点（相邻两段偏移线的交点），不是 p ± d*half
            //   每个顶点算一次左右斜接点，相邻段共用 → 接头天然闭合、不出刺
            auto strokeSegments = [&](const ui_path::contour &c, float half,
                                      glm::vec2 shift, std::vector<float> &out) {
                const auto &p = c.pts;
                const std::size_t n = p.size();
                if (n < 2)
                    return;
                const std::size_t segCount = c.closed ? n : (n - 1);
                constexpr float kMiterLimit = 4.0F; // 斜接长度上限（× 半宽）

                auto pos = [&](std::size_t i) {
                    return p[i] + shift;
                };
                auto dirOf = [&](std::size_t i, std::size_t j) { // i -> j 的单位方向
                    const glm::vec2 d = pos(j) - pos(i);
                    const float l = std::sqrt(d.x * d.x + d.y * d.y);
                    return l > 1e-5F ? d / l : glm::vec2{1.0F, 0.0F};
                };

                // 逐顶点预计算：左右斜接点 + 角平分平面法线（朝前）
                std::vector<glm::vec2> leftPt(n), rightPt(n), planeN(n);
                for (std::size_t i = 0; i < n; ++i)
                {
                    const std::size_t prev = (i + n - 1) % n;
                    const std::size_t next = (i + 1) % n;
                    const glm::vec2 d0 = dirOf(prev, i); // 入边方向
                    const glm::vec2 d1 = dirOf(i, next); // 出边方向
                    const glm::vec2 n0{-d0.y, d0.x};
                    const glm::vec2 n1{-d1.y, d1.x};
                    glm::vec2 m = n0 + n1;
                    const float ml = std::sqrt(m.x * m.x + m.y * m.y);
                    if (ml < 1e-4F)
                        m = n0;
                    else
                        m /= ml;
                    float denom = glm::dot(m, n0);
                    if (denom < 1e-3F)
                        denom = 1e-3F;
                    // 斜接长度 = half / cos(半角)，超 limit 就夹住（miter-clip，
                    // 视觉上等同 bevel，但不需要额外三角形，也不会拉出长刺）
                    const float scale = std::min(half / denom, half * kMiterLimit);
                    leftPt[i] = pos(i) + m * scale;
                    rightPt[i] = pos(i) - m * scale;
                    glm::vec2 bis = d0 + d1; // 角平分平面法线（朝前）
                    const float bl = std::sqrt(bis.x * bis.x + bis.y * bis.y);
                    planeN[i] = bl > 1e-4F ? bis / bl : d1;
                }

                for (std::size_t i = 0; i < segCount; ++i)
                {
                    const std::size_t j = (i + 1) % n;
                    const glm::vec2 a = pos(i);
                    const glm::vec2 b = pos(j);
                    glm::vec2 d = b - a;
                    const float len = std::sqrt(d.x * d.x + d.y * d.y);
                    if (len < 1e-3F)
                        continue;
                    d /= len;
                    const glm::vec2 nn{-d.y, d.x};

                    // 胶囊角点：沿段两端各外推 half，保证圆头不被 quad 切掉
                    const glm::vec2 ea = a - d * half;
                    const glm::vec2 eb = b + d * half;
                    const glm::vec2 q0 = ea + nn * half;
                    const glm::vec2 q1 = eb + nn * half;
                    const glm::vec2 q2 = ea - nn * half;
                    const glm::vec2 q3 = eb - nn * half;
                    const glm::vec2 planeA = planeN[i];
                    const glm::vec2 planeB = -planeN[j];
                    out.push_back(q0.x / W * 2.0F - 1.0F);
                    out.push_back(q0.y / H * 2.0F - 1.0F);
                    out.push_back(q1.x / W * 2.0F - 1.0F);
                    out.push_back(q1.y / H * 2.0F - 1.0F);
                    out.push_back(q2.x / W * 2.0F - 1.0F);
                    out.push_back(q2.y / H * 2.0F - 1.0F);
                    out.push_back(q3.x / W * 2.0F - 1.0F);
                    out.push_back(q3.y / H * 2.0F - 1.0F);
                    out.push_back(a.x);
                    out.push_back(a.y);
                    out.push_back(b.x);
                    out.push_back(b.y);
                    out.push_back(nn.x);
                    out.push_back(nn.y);
                    out.push_back(planeA.x);
                    out.push_back(planeA.y);
                    out.push_back(planeB.x);
                    out.push_back(planeB.y);
                    out.push_back(0.0F);
                    out.push_back(0.0F);
                }
            };

            // 注册本帧的 path（PathId 只在同一 generation 内有效）
            auto &pathRegistry = ui_path::get();
            pathRegistry.reset();
            const uint32_t idFill = [&]() {
                const VkRect2D box = cell(14);
                const float cx = static_cast<float>(box.offset.x) +
                                 static_cast<float>(box.extent.width) * 0.5F;
                const float cy = static_cast<float>(box.offset.y) +
                                 static_cast<float>(box.extent.height) * 0.5F;
                const float rad = std::min(static_cast<float>(box.extent.width),
                                           static_cast<float>(box.extent.height)) *
                                  0.44F;
                ui_path::path p;
                ui_path::contour star;
                const int order[5] = {0, 2, 4, 1, 3};
                for (int i = 0; i < 5; ++i)
                {
                    const float ang =
                        -1.5707963F + static_cast<float>(order[i]) * 1.2566371F;
                    star.pts.emplace_back(cx + std::cos(ang) * rad,
                                          cy + std::sin(ang) * rad);
                }
                p.push_back(std::move(star));
                const float r2 = rad * 0.22F;
                const float k = 0.5523F;
                const glm::vec2 c{cx, cy};
                const glm::vec2 dirs[4] = {
                    {r2, 0.0F}, {0.0F, r2}, {-r2, 0.0F}, {0.0F, -r2}};
                ui_path::contour ring;
                for (int q = 0; q < 4; ++q)
                {
                    const glm::vec2 p0 = c + dirs[q];
                    const glm::vec2 p3 = c + dirs[(q + 1) % 4];
                    const glm::vec2 p1 = p0 + glm::vec2{-dirs[q].y, dirs[q].x} * k;
                    const glm::vec2 p2 =
                        p3 - glm::vec2{-dirs[(q + 1) % 4].y, dirs[(q + 1) % 4].x} * k;
                    const auto seg = flatten_cubic(p0, p1, p2, p3, 12);
                    ring.pts.insert(ring.pts.end(), seg.begin(), seg.end());
                }
                p.push_back(std::move(ring));
                return pathRegistry.add(std::move(p));
            }();
            // path_aa 图块分左右两半，对齐 test_ui_path_aa 的两种语义：
            //   左：填充三角形，边缘靠 MSAA（就是 test_ui_path_aa 的做法）
            //   右：闭合贝塞尔描边，边缘靠片元解析覆盖率
            const VkRect2D pathTile = cell(9);
            const VkRect2D msaaBox{
                {pathTile.offset.x, pathTile.offset.y},
                {pathTile.extent.width / 2u - 2u, pathTile.extent.height}};
            const VkRect2D strokeBox{
                {pathTile.offset.x + static_cast<int32_t>(pathTile.extent.width / 2u) + 2,
                 pathTile.offset.y},
                {pathTile.extent.width / 2u - 2u, pathTile.extent.height}};

            // 左半：一个三角形（填充，不用解析 AA）
            const uint32_t idTri = [&]() {
                ui_path::contour tri;
                const float x0 = static_cast<float>(msaaBox.offset.x) + 10.0F;
                const float y0 = static_cast<float>(msaaBox.offset.y) + 10.0F;
                const float w = static_cast<float>(msaaBox.extent.width) - 20.0F;
                const float h = static_cast<float>(msaaBox.extent.height) - 20.0F;
                tri.pts.emplace_back(x0 + w * 0.5F, y0);           // 顶点
                tri.pts.emplace_back(x0, y0 + h);                  // 左下
                tri.pts.emplace_back(x0 + w, y0 + h);              // 右下
                ui_path::path p;
                p.push_back(std::move(tri));
                return pathRegistry.add(std::move(p));
            }();

            const uint32_t idStroke = [&]() {
                const VkRect2D box = strokeBox;
                const glm::vec2 c{static_cast<float>(box.offset.x) +
                                      static_cast<float>(box.extent.width) * 0.5F,
                                  static_cast<float>(box.offset.y) +
                                      static_cast<float>(box.extent.height) * 0.5F};
                const float rx = static_cast<float>(box.extent.width) * 0.30F;
                const float ry = static_cast<float>(box.extent.height) * 0.26F;
                const float k = 0.5523F;
                const glm::vec2 dirs[4] = {
                    {rx, 0.0F}, {0.0F, ry}, {-rx, 0.0F}, {0.0F, -ry}};
                ui_path::contour blob;
                for (int q = 0; q < 4; ++q)
                {
                    const glm::vec2 p0 = c + dirs[q];
                    const glm::vec2 p3 = c + dirs[(q + 1) % 4];
                    const glm::vec2 p1 = p0 + glm::vec2{-dirs[q].y, dirs[q].x} * k;
                    const glm::vec2 p2 =
                        p3 - glm::vec2{-dirs[(q + 1) % 4].y, dirs[(q + 1) % 4].x} * k;
                    const auto seg = flatten_cubic(p0, p1, p2, p3, 24);
                    blob.pts.insert(blob.pts.end(), seg.begin(), seg.end());
                }
                ui_path::path p;
                p.push_back(std::move(blob));
                return pathRegistry.add(std::move(p));
            }();

            // 一条 path 命令对应的子 pass：由 emit 循环按 pathKind 分发
            auto drawPathCommand = [&](const ui_cmd &c) {
                if (c.pathGen != pathRegistry.generation || !pathRegistry.valid(c.pathId))
                {
                    std::println("[path] stale PathId {} (gen {} != {})", c.pathId,
                                 c.pathGen, pathRegistry.generation);
                    return;
                }
                const auto &path = pathRegistry.get(c.pathId);
                if (c.pathKind == 1)
                {
                    // even-odd 填充：每条轮廓一次 INVERT draw + EQUAL 0xFF 覆盖
                    std::vector<float> packed;
                    std::vector<uint32_t> triCounts;
                    for (const auto &contour : path)
                    {
                        const auto tris = fanTriangles(contour);
                        triCounts.push_back(static_cast<uint32_t>(tris.size()));
                        for (const auto &pt : tris)
                        {
                            packed.push_back(pt.x / W * 2.0F - 1.0F);
                            packed.push_back(pt.y / H * 2.0F - 1.0F);
                        }
                    }
                    const std::size_t byteOffset =
                        static_cast<std::size_t>(kFanVertexBase) * 2u * sizeof(float);
                    globalCtx.pathSegments.write(byteOffset, packed.data(),
                                                 sizeof(float) * packed.size());
                    commandBuffer.setScissor(0, std::span{&c.scissor, 1});
                    commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                               *(*uiPipelines().pipes[7]));
                    commandBuffer.setStencilReference(VK_STENCIL_FACE_FRONT_AND_BACK, 0u);
                    PushData fan{};
                    fan.geomEntry = kGeomFan;
                    fan.uiParams =
                        glm::vec4{static_cast<float>(MODE_SOLID), 0.0F, 0.0F, 0.0F};
                    fan.uiColor = c.color;
                    commandBuffer.pushConstants(*pipelineLayout,
                                                VK_SHADER_STAGE_VERTEX_BIT |
                                                    VK_SHADER_STAGE_FRAGMENT_BIT,
                                                0, sizeof(PushData), &fan);
                    uint32_t base = 0;
                    for (const uint32_t count : triCounts)
                    {
                        commandBuffer.draw(count, 1, kFanVertexBase + base, 0);
                        base += count;
                    }
                    commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                               *(*uiPipelines().pipes[6]));
                    commandBuffer.setStencilReference(VK_STENCIL_FACE_FRONT_AND_BACK,
                                                      0xFFu);
                    PushData cover{};
                    cover.uiRect = {static_cast<float>(c.scissor.offset.x),
                                    static_cast<float>(c.scissor.offset.y),
                                    static_cast<float>(c.scissor.extent.width),
                                    static_cast<float>(c.scissor.extent.height)};
                    cover.uiParams =
                        glm::vec4{static_cast<float>(MODE_SOLID), 0.0F, 0.0F, 0.0F};
                    cover.uiColor = c.color;
                    commandBuffer.pushConstants(*pipelineLayout,
                                                VK_SHADER_STAGE_VERTEX_BIT |
                                                    VK_SHADER_STAGE_FRAGMENT_BIT,
                                                0, sizeof(PushData), &cover);
                    commandBuffer.draw(3, 1, 0, 0);
                }
                else if (c.pathKind == 2)
                {
                    // 描边：同一份 path → 两份数据（开/关 AA）→ 2 次 draw
                    const float half = c.params.y;
                    const float bh = static_cast<float>(c.scissor.extent.height);
                    std::vector<float> segs;
                    for (const auto &contour : path)
                        strokeSegments(contour, half, glm::vec2{0.0F, -bh * 0.18F}, segs);
                    const std::size_t floatsA = segs.size();
                    for (const auto &contour : path)
                        strokeSegments(contour, half, glm::vec2{0.0F, bh * 0.18F}, segs);
                    globalCtx.pathSegments.write(0, segs.data(),
                                                 sizeof(float) * segs.size());
                    const uint32_t segsA = static_cast<uint32_t>(floatsA / 20u);
                    const uint32_t segsB =
                        static_cast<uint32_t>((segs.size() - floatsA) / 20u);
                    commandBuffer.setScissor(0, std::span{&c.scissor, 1});
                    commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                               *(*uiPipelines().pipes[8]));
                    commandBuffer.setStencilReference(VK_STENCIL_FACE_FRONT_AND_BACK, 0u);
                    PushData stroke{};
                    stroke.geomEntry = kGeomStroke;
                    stroke.uiAux = c.color;
                    stroke.uiParams =
                        glm::vec4{static_cast<float>(MODE_PATH_STROKE), half, 1.0F, 1.0F};
                    commandBuffer.pushConstants(*pipelineLayout,
                                                VK_SHADER_STAGE_VERTEX_BIT |
                                                    VK_SHADER_STAGE_FRAGMENT_BIT,
                                                0, sizeof(PushData), &stroke);
                    commandBuffer.draw(6u * segsA, 1, 0, 0);
                    stroke.uiParams.z = 0.0F;
                    commandBuffer.pushConstants(*pipelineLayout,
                                                VK_SHADER_STAGE_VERTEX_BIT |
                                                    VK_SHADER_STAGE_FRAGMENT_BIT,
                                                0, sizeof(PushData), &stroke);
                    commandBuffer.draw(6u * segsB, 1, 6u * segsA, 0);
                }
                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *mainCtx.pipelineTransparentUI);
            };

            // 一条 path 命令：路径内容在注册表里，命令表只带 PathId
            auto addPath = [&](int kind, uint32_t id, VkRect2D box, glm::vec4 color,
                               glm::vec3 params = {}) {
                add(MODE_SOLID, box, color, {}, params, {}, box, 0xFFFFFFFFu, 0u, 0u, 0,
                    0u, kind, id, pathRegistry.generation);
            };

            // ── 4.3 能力表：一个 mode 一条命令 ──────────────────────────
            struct ability
            {
                int mode;
                glm::vec4 color;
                glm::vec4 color2;
                glm::vec3 params; // y/z/w（x 由 mode 填）
                glm::vec4 aux;
            };
            static const ability kAbilities[] = {
                {MODE_FILL, {0.28F, 0.56F, 0.95F, 1.0F}, {}, {18.0F, 0.0F, 0.0F}, {}},
                {MODE_LINEAR,
                 {0.95F, 0.35F, 0.35F, 1.0F},
                 {0.20F, 0.45F, 0.95F, 1.0F},
                 {12.0F, 0.0F, 0.0F},
                 {1.0F, 0.35F, 0.0F, 0.0F}},
                {MODE_RADIAL,
                 {0.98F, 0.82F, 0.30F, 1.0F},
                 {0.20F, 0.10F, 0.05F, 1.0F},
                 {90.0F, 12.0F, 0.0F},
                 {}},
                {MODE_CONIC,
                 {0.20F, 0.85F, 0.85F, 1.0F},
                 {0.85F, 0.25F, 0.70F, 1.0F},
                 {0.0F, 12.0F, 0.0F},
                 {}},
                {MODE_ARC,
                 {1.0F, 1.0F, 1.0F, 1.0F},
                 {},
                 {62.0F, 14.0F, 0.0F},
                 {-2.6F, 2.2F, 0.0F, 0.0F}},
                {MODE_OUTER_SHADOW,
                 {0.30F, 0.62F, 0.40F, 1.0F},
                 {0.0F, 0.0F, 0.0F, 0.75F},
                 {16.0F, 34.0F, 0.0F},
                 {6.0F, 10.0F, 0.0F, 0.0F}},
                {MODE_INNER_SHADOW,
                 {0.35F, 0.40F, 0.55F, 1.0F},
                 {0.0F, 0.0F, 0.0F, 0.85F},
                 {16.0F, 26.0F, 0.0F},
                 {}},
                {MODE_PERCORNER,
                 {0.92F, 0.55F, 0.20F, 1.0F},
                 {},
                 {28.0F, 4.0F, 28.0F},
                 {4.0F, 0.0F, 0.0F, 0.0F}},
                {MODE_ALPHA_MASK,
                 {0.85F, 0.30F, 0.35F, 1.0F},
                 {},
                 {10.0F, 14.0F, 0.0F},
                 {}},
                {MODE_PATH_AA, {0.40F, 0.80F, 0.45F, 1.0F}, {}, {0.0F, 0.0F, 0.0F}, {}},
                {MODE_DASH, {0.95F, 0.90F, 0.35F, 1.0F}, {}, {8.0F, 18.0F, 12.0F}, {}},
                {MODE_ROTATE, {0.85F, 0.40F, 0.75F, 1.0F}, {}, {0.55F, 10.0F, 0.0F}, {}},
                {MODE_SKEW,
                 {0.35F, 0.75F, 0.85F, 1.0F},
                 {},
                 {10.0F, 0.0F, 0.0F},
                 {0.35F, 0.0F, 0.0F, 0.0F}},
                {MODE_NINE_SLICE,
                 {},
                 {0.05F, 0.05F, 0.05F, 1.0F},
                 {12.0F, 14.0F, 0.0F},
                 {}},
                {MODE_EVENODD, {0.95F, 0.75F, 0.20F, 1.0F}, {}, {0.45F, 0.0F, 0.0F}, {}},
                {MODE_GLYPH, {}, {}, {}, {}}, // 占位：图块里单独画字
                {MODE_SRGB,
                 {0.10F, 0.10F, 0.10F, 1.0F},
                 {0.90F, 0.90F, 0.90F, 1.0F},
                 {12.0F, 0.0F, 0.0F},
                 {}},
                {MODE_BLUR,
                 {0.85F, 0.85F, 0.90F, 1.0F},
                 {0.10F, 0.11F, 0.14F, 1.0F},
                 {4.0F, 0.0F, 0.0F},
                 {}},
                {MODE_BACKDROP, {0.85F, 0.90F, 1.0F, 0.45F}, {}, {3.0F, 14.0F, 0.0F}, {}},
                {MODE_GROUP,
                 {0.30F, 0.65F, 0.95F, 1.0F},
                 {0.95F, 0.45F, 0.30F, 1.0F},
                 {0.0F, 0.45F, 0.0F},
                 {}},
                {MODE_MIPMAP,
                 {0.85F, 0.65F, 0.25F, 1.0F},
                 {0.15F, 0.12F, 0.08F, 1.0F},
                 {8.0F, 0.0F, 0.0F},
                 {}},
                {MODE_PICKING,
                 {0.55F, 0.85F, 0.95F, 1.0F},
                 {},
                 {20.0F, 7.0F, 0.0F},
                 {42.0F, 3.0F, 0.0F, 0.0F}},
                {MODE_BLEND,
                 {0.95F, 0.35F, 0.35F, 1.0F},
                 {0.12F, 0.14F, 0.20F, 1.0F},
                 {0.0F, 0.0F, 0.0F},
                 {}},
            };
            constexpr int kAbilityCount =
                static_cast<int>(sizeof(kAbilities) / sizeof(kAbilities[0]));

            // 每个图块一个稳定 id：整格都能被拾取命中（底板和能力命令共用同一个 id）
            constexpr uint32_t kTileIdBase = 200;
            const auto hoveredKey = world.hoverManager.cur.key; // 上一帧的拾取结果
            const int hoveredTile =
                (hoveredKey.object_type >= kTileIdBase &&
                 hoveredKey.object_type < kTileIdBase + 64)
                    ? static_cast<int>(hoveredKey.object_type - kTileIdBase)
                    : -1;

            for (int i = 0; i < kAbilityCount; ++i)
            {
                if (!inView(i))
                    continue;
                const VkRect2D box = cell(i);
                const uint32_t id = kTileIdBase + static_cast<uint32_t>(i);
                const uint32_t entity = static_cast<uint32_t>(i);
                const bool hot = (hoveredTile == i);
                // 底板（悬停时提亮，这是真的 hover 状态）
                add(MODE_FILL, box,
                    hot ? glm::vec4{0.17F, 0.19F, 0.24F, 1.0F}
                        : glm::vec4{0.11F, 0.12F, 0.15F, 1.0F},
                    {}, {10.0F, 0.0F, 0.0F}, {}, {}, id, entity);
                // 能力本体（内缩 8px）
                VkRect2D inner{{box.offset.x + 8, box.offset.y + 8},
                               {box.extent.width - 16u, box.extent.height - 16u}};
                const auto &ab = kAbilities[i];
                if (ab.mode == MODE_GLYPH)
                {
                    text(static_cast<float>(inner.offset.x) + 8.0F,
                         static_cast<float>(inner.offset.y) + 10.0F, 44.0F,
                         {1.0F, 1.0F, 1.0F, 1.0F}, "UI", box);
                    text(static_cast<float>(inner.offset.x) + 8.0F,
                         static_cast<float>(inner.offset.y) + 56.0F, 22.0F,
                         {0.75F, 0.80F, 0.90F, 1.0F}, "ATLAS", box);
                    continue;
                }
                if (ab.mode == MODE_DASH)
                {
                    // 虚线端点用图块局部坐标（着色器里的 p 就是图块局部像素）
                    const float lw = static_cast<float>(inner.extent.width);
                    const float lh = static_cast<float>(inner.extent.height);
                    add(MODE_DASH, inner, ab.color, ab.color2, ab.params,
                        {6.0F, lh * 0.5F, lw - 6.0F, lh * 0.5F}, {}, id, entity);
                    continue;
                }
                if (ab.mode == MODE_BLUR || ab.mode == MODE_BACKDROP ||
                    ab.mode == MODE_GROUP)
                {
                    // 真实离屏结果：blur→A(0)，group→C(2)，backdrop→D(3)
                    const auto &layer = (ab.mode == MODE_BLUR)    ? frameLayers[0]
                                        : (ab.mode == MODE_GROUP) ? frameLayers[2]
                                                                  : frameLayers[3];
                    const float opacity = (ab.mode == MODE_GROUP) ? 0.45F : 1.0F;
                    add(MODE_LAYER_COMPOSITE, inner, {}, {},
                        {opacity, static_cast<float>(layer.texture),
                         static_cast<float>(layer.sampler)},
                        {12.0F, 0.0F, 0.0F, 0.0F}, {}, id, entity);
                    if (ab.mode == MODE_BACKDROP)
                        add(MODE_FILL, inner, {0.86F, 0.91F, 1.0F, 0.22F}, {},
                            {14.0F, 0.0F, 0.0F}, {}, {}, id, entity);
                    continue;
                }
                if (ab.mode == MODE_EVENODD)
                    continue; // 面板已画
                if (ab.mode == MODE_BACKDROP)
                    continue; // 面板已画；真·背景模糊在第二段 pass 里画
                if (ab.mode == MODE_PATH_AA)
                    continue; // 面板已画；真路径描边（解析 AA）在命令循环之后
                if (ab.mode == MODE_BLEND)
                {
                    // 真实 Vulkan 混合：over / add / multiply 三条不同 blend state 的管线
                    add(MODE_FILL, inner, {0.09F, 0.10F, 0.15F, 1.0F}, {},
                        {8.0F, 0.0F, 0.0F});
                    const int32_t cx =
                        inner.offset.x + static_cast<int32_t>(inner.extent.width) / 2;
                    const int32_t cy =
                        inner.offset.y + static_cast<int32_t>(inner.extent.height) / 2;
                    const int32_t rad = static_cast<int32_t>(
                        std::min(inner.extent.width, inner.extent.height) / 2u);
                    auto disc = [&](int32_t dx, glm::vec4 color, int pipe) {
                        addPipe(MODE_FILL,
                                {{cx + dx - rad, cy - rad},
                                 {static_cast<uint32_t>(rad * 2),
                                  static_cast<uint32_t>(rad * 2)}},
                                color, {}, {static_cast<float>(rad), 0.0F, 0.0F}, {}, {},
                                pipe, 0u);
                    };
                    disc(-rad / 2, {0.95F, 0.45F, 0.35F, 0.85F}, 0); // over（默认）
                    disc(0, {0.35F, 0.85F, 0.55F, 0.85F}, 1);        // add
                    disc(rad / 2, {0.55F, 0.55F, 0.95F, 0.85F}, 2);  // multiply
                    // erase：blend = ZERO / ONE_MINUS_SRC_ALPHA —— 直接把已画内容"挖掉"
                    //   （挖在 over 圆上，能看见底板透出来）
                    addPipe(MODE_FILL,
                            {{cx - rad / 2 - rad / 4, cy - rad / 4},
                             {static_cast<uint32_t>(rad / 2),
                              static_cast<uint32_t>(rad / 2)}},
                            {1.0F, 1.0F, 1.0F, 1.0F}, {},
                            {static_cast<float>(rad / 4), 0.0F, 0.0F}, {}, {}, 3, 0u);
                    continue;
                }
                if (ab.mode == MODE_NINE_SLICE)
                {
                    // 真纹理九宫格：源图 96x96、边框 6px
                    const auto &tex = uiTextures();
                    add(MODE_NINE_SLICE_TEX, inner, {}, {},
                        {6.0F, static_cast<float>(tex.nineSlice),
                         static_cast<float>(tex.sampler)},
                        {static_cast<float>(tex.nineSliceExtent.width),
                         static_cast<float>(tex.nineSliceExtent.height), 0.0F, 0.0F},
                        {}, id, entity);
                    continue;
                }
                if (ab.mode == MODE_ALPHA_MASK)
                {
                    // 真纹理 alpha 遮罩
                    const auto &tex = uiTextures();
                    add(MODE_ALPHA_MASK_TEX, inner, ab.color, {},
                        {0.0F, static_cast<float>(tex.alphaMask),
                         static_cast<float>(tex.sampler)},
                        {}, {}, id, entity);
                    continue;
                }
                if (ab.mode == MODE_MIPMAP)
                {
                    // 真 mip 链采样：lod 0..3 四条横带
                    const auto &tex = uiTextures();
                    add(MODE_MIPMAP_TEX, inner, {}, {},
                        {7.0F, static_cast<float>(tex.checker),
                         static_cast<float>(tex.sampler)},
                        {}, {}, id, entity);
                    continue;
                }
                if (ab.mode == MODE_ARC)
                {
                    // 环半径按图块尺寸缩放，否则大半径会把整条环推到裁剪框外
                    const float lw = static_cast<float>(inner.extent.width);
                    const float lh = static_cast<float>(inner.extent.height);
                    const float radius = (lw < lh ? lw : lh) * 0.32F;
                    add(MODE_ARC, inner, ab.color, ab.color2,
                        {radius, ab.params.y, ab.params.z}, ab.aux, {}, id, entity);
                    continue;
                }
                add(ab.mode, inner, ab.color, ab.color2, ab.params, ab.aux, {}, id,
                    entity);
            }

            // ── 4.4 真实布局：度量 → 换行 / 省略号 / 富文本 / 光标 / 表格 ──
            //   度量也用 shape 结果的 advance（同一个字形表，不再有第二套数据）
            auto glyphOf = [&](char ch) -> const text_glyph * {
                const auto it = asciiGlyphs.find(
                    static_cast<char32_t>(static_cast<unsigned char>(ch)));
                return it == asciiGlyphs.end() ? nullptr : &it->second;
            };
            auto advanceOf = [&](char ch) -> float {
                if (ch == ' ')
                    return 0.34F;
                const text_glyph *g = glyphOf(ch);
                return g != nullptr ? g->advance : 0.5F;
            };
            // 文本度量：宽度完全由排版结果的 advance 累加而来
            auto measure = [&](std::string_view s, float px) -> float {
                float w = 0.0F;
                for (const char ch : s)
                    w += advanceOf(ch) * px;
                return w;
            };
            // 贪心换行：先按空格断词，单词超宽就按字符硬断
            auto wrap = [&](std::string_view s, float px, float maxW) {
                std::vector<std::string_view> lines;
                std::size_t i = 0;
                while (i < s.size())
                {
                    std::size_t j = i;
                    std::size_t lastSpace = std::string_view::npos;
                    float w = 0.0F;
                    while (j < s.size())
                    {
                        if (s[j] == ' ')
                            lastSpace = j;
                        const float adv = advanceOf(s[j]) * px;
                        if (j > i && w + adv > maxW)
                            break;
                        w += adv;
                        ++j;
                    }
                    std::size_t end = j;
                    if (j < s.size() && lastSpace != std::string_view::npos &&
                        lastSpace > i)
                        end = lastSpace; // 在最后一个空格处断行
                    lines.push_back(s.substr(i, end - i));
                    i = end;
                    while (i < s.size() && s[i] == ' ')
                        ++i;
                }
                return lines;
            };
            // 宽度受限的省略：先尽量放字，放不下就退字，给 "..." 留出宽度
            const float dotWidth = 0.34F + 0.18F; // 点 + 间距
            auto fitEllipsis = [&](std::string_view s, float px, float maxW) {
                const float dots = 3.0F * dotWidth * px;
                if (measure(s, px) <= maxW)
                    return std::pair<std::string_view, bool>{s, false};
                std::size_t n = s.size();
                while (n > 0 && measure(s.substr(0, n), px) + dots > maxW)
                    --n;
                return std::pair<std::string_view, bool>{s.substr(0, n), true};
            };

            // 23：状态机（hover / pressed 来自真实拾取和鼠标按键）
            {
                const VkRect2D box = cell(23);
                const uint32_t id = kTileIdBase + 23u;
                const bool hot = (hoveredTile == 23);
                const bool pressed = hot && inputCtx.input.isMouseButtonPressed(
                                                MouseButtons::eMOUSE_BUTTON_LEFT);
                const glm::vec4 color = pressed ? glm::vec4{0.16F, 0.42F, 0.70F, 1.0F}
                                        : hot   ? glm::vec4{0.34F, 0.50F, 0.72F, 1.0F}
                                                : glm::vec4{0.22F, 0.24F, 0.30F, 1.0F};
                add(MODE_FILL, box, {0.11F, 0.12F, 0.15F, 1.0F}, {}, {10.0F, 0.0F, 0.0F},
                    {}, {}, id, 23u);
                VkRect2D btn{{box.offset.x + 10, box.offset.y + 10},
                             {box.extent.width - 20u, box.extent.height - 20u}};
                add(MODE_FILL, btn, color, {}, {8.0F, 0.0F, 0.0F}, {}, {}, id, 23u);
                text(static_cast<float>(btn.offset.x) + 10.0F,
                     static_cast<float>(btn.offset.y) +
                         static_cast<float>(btn.extent.height) * 0.62F,
                     15.0F, {1.0F, 1.0F, 1.0F, 0.92F},
                     pressed ? "PRESS" : (hot ? "HOVER" : "IDLE"));
            }
            // 24：表格布局（权重解列宽 + 只画可见行 + 单元格省略号）
            {
                const VkRect2D box = cell(24);
                const uint32_t id = kTileIdBase + 24u;
                add(MODE_FILL, box, {0.11F, 0.12F, 0.15F, 1.0F}, {}, {10.0F, 0.0F, 0.0F},
                    {}, {}, id, 24u);
                const float pad = 6.0F;
                const float headerH = 16.0F;
                const float rowH = 15.0F;
                const float x0 = static_cast<float>(box.offset.x) + pad;
                const float y0 = static_cast<float>(box.offset.y) + pad;
                const float w = static_cast<float>(box.extent.width) - pad * 2.0F;
                const float h = static_cast<float>(box.extent.height) - pad * 2.0F;
                // 列宽由权重求出来，不是写死的像素
                const float weight[3] = {1.9F, 1.55F, 1.55F};
                const float weightSum = weight[0] + weight[1] + weight[2];
                float colX[4]{x0, 0.0F, 0.0F, x0 + w};
                for (int c = 1; c < 3; ++c)
                    colX[c] = colX[c - 1] + w * weight[c - 1] / weightSum;
                // 表头
                add(MODE_FILL,
                    {{static_cast<int32_t>(x0), static_cast<int32_t>(y0)},
                     {static_cast<uint32_t>(w), static_cast<uint32_t>(headerH)}},
                    {0.19F, 0.22F, 0.29F, 1.0F}, {}, {3.0F, 0.0F, 0.0F}, {}, {}, id, 24u);
                text(x0 + 3.0F, y0 + 12.0F, 10.0F, {0.85F, 0.88F, 0.94F, 1.0F}, "NAME",
                     box);
                text(colX[1] + 3.0F, y0 + 12.0F, 10.0F, {0.85F, 0.88F, 0.94F, 1.0F},
                     "STATE", box);
                // 可见行数由高度算出来，只画放得下的行（真实的窗口化）
                const int totalRows = 64;
                const int visibleRows =
                    static_cast<int>(std::max(0.0F, (h - headerH) / rowH));
                for (int r = 0; r < visibleRows; ++r)
                {
                    const float ry = y0 + headerH + static_cast<float>(r) * rowH;
                    add(MODE_FILL,
                        {{static_cast<int32_t>(x0), static_cast<int32_t>(ry)},
                         {static_cast<uint32_t>(w), static_cast<uint32_t>(rowH - 1.0F)}},
                        (r % 2) ? glm::vec4{0.13F, 0.14F, 0.18F, 1.0F}
                                : glm::vec4{0.15F, 0.16F, 0.21F, 1.0F},
                        {}, {1.0F, 0.0F, 0.0F}, {}, {}, id, 24u);
                    // 单元格：按列宽做度量 + 省略号截断
                    const std::string name =
                        std::string("ROW ") + static_cast<char>('A' + (r % 26));
                    const std::string stateText =
                        (r % 3 == 0) ? "READY" : (r % 3 == 1 ? "BUSY" : "IDLE");
                    const auto [nameFit, nameCut] =
                        fitEllipsis(name, 9.0F, colX[1] - colX[0] - 6.0F);
                    const auto [stateFit, stateCut] =
                        fitEllipsis(stateText, 9.0F, colX[2] - colX[1] - 6.0F);
                    text(colX[0] + 3.0F, ry + rowH * 0.78F, 9.0F,
                         {0.84F, 0.86F, 0.91F, 1.0F}, nameFit, box);
                    if (nameCut)
                    {
                        const float dotX = colX[0] + 3.0F + measure(nameFit, 9.0F);
                        for (int d = 0; d < 3; ++d)
                            add(MODE_FILL,
                                {{static_cast<int32_t>(dotX +
                                                       static_cast<float>(d) * 3.0F),
                                  static_cast<int32_t>(ry + rowH * 0.72F)},
                                 {1u, 1u}},
                                {0.84F, 0.86F, 0.91F, 1.0F}, {}, {0.5F, 0.0F, 0.0F}, {},
                                {}, id, 24u);
                    }
                    text(colX[1] + 3.0F, ry + rowH * 0.78F, 9.0F,
                         (r % 3 == 0)   ? glm::vec4{0.45F, 0.85F, 0.55F, 1.0F}
                         : (r % 3 == 1) ? glm::vec4{0.95F, 0.75F, 0.35F, 1.0F}
                                        : glm::vec4{0.95F, 0.45F, 0.45F, 1.0F},
                         stateFit, box);
                    if (stateCut)
                    {
                        const float dotX = colX[1] + 3.0F + measure(stateFit, 9.0F);
                        for (int d = 0; d < 3; ++d)
                            add(MODE_FILL,
                                {{static_cast<int32_t>(dotX +
                                                       static_cast<float>(d) * 3.0F),
                                  static_cast<int32_t>(ry + rowH * 0.72F)},
                                 {1u, 1u}},
                                {0.90F, 0.92F, 0.96F, 1.0F}, {}, {0.5F, 0.0F, 0.0F}, {},
                                {}, id, 24u);
                    }
                }
                // 列分隔线
                for (int c = 1; c < 3; ++c)
                    add(MODE_FILL,
                        {{static_cast<int32_t>(colX[c]), static_cast<int32_t>(y0)},
                         {1u, static_cast<uint32_t>(h)}},
                        {1.0F, 1.0F, 1.0F, 0.10F}, {}, {0.0F, 0.0F, 0.0F}, {}, {}, id,
                        24u);
                if (totalRows > visibleRows)
                    add(MODE_FILL,
                        {{static_cast<int32_t>(x0 + w - 3.0F), static_cast<int32_t>(y0)},
                         {3u, static_cast<uint32_t>(h)}},
                        {1.0F, 1.0F, 1.0F, 0.08F}, {}, {1.0F, 0.0F, 0.0F}, {}, {}, id,
                        24u);
            }
            // 25：文本换行（真实宽度约束下的贪心换行）
            {
                const VkRect2D box = cell(25);
                const uint32_t id = kTileIdBase + 25u;
                add(MODE_FILL, box, {0.11F, 0.12F, 0.15F, 1.0F}, {}, {10.0F, 0.0F, 0.0F},
                    {}, {}, id, 25u);
                const float x0 = static_cast<float>(box.offset.x) + 8.0F;
                float y = static_cast<float>(box.offset.y) + 18.0F;
                const float maxW = static_cast<float>(box.extent.width) - 16.0F;
                for (const auto &line : wrap("WRAP TEXT BY WIDTH", 15.0F, maxW))
                {
                    text(x0, y, 15.0F, {0.90F, 0.92F, 0.96F, 1.0F}, line, box);
                    y += 18.0F;
                }
                // 宽度示意线，说明换行是按这个宽度断的
                add(MODE_FILL,
                    {{static_cast<int32_t>(x0), static_cast<int32_t>(y - 4.0F)},
                     {static_cast<uint32_t>(maxW), 1u}},
                    {1.0F, 1.0F, 1.0F, 0.15F}, {}, {0.0F, 0.0F, 0.0F}, {}, {}, id, 25u);
            }
            // 26：省略号（度量 + 截断 + 三个点）
            {
                const VkRect2D box = cell(26);
                const uint32_t id = kTileIdBase + 26u;
                add(MODE_FILL, box, {0.11F, 0.12F, 0.15F, 1.0F}, {}, {10.0F, 0.0F, 0.0F},
                    {}, {}, id, 26u);
                const float x0 = static_cast<float>(box.offset.x) + 8.0F;
                const float y0 = static_cast<float>(box.offset.y) + 18.0F;
                const float maxW = static_cast<float>(box.extent.width) - 16.0F;
                const auto [fit, cut] = fitEllipsis("SELECT TEXT", 16.0F, maxW);
                float pen = text(x0, y0, 16.0F, {0.92F, 0.93F, 0.97F, 1.0F}, fit, box);
                if (cut)
                    for (int d = 0; d < 3; ++d)
                        add(MODE_FILL,
                            {{static_cast<int32_t>(pen + static_cast<float>(d) * 4.0F),
                              static_cast<int32_t>(y0 - 2.0F)},
                             {2u, 2u}},
                            {0.92F, 0.93F, 0.97F, 1.0F}, {}, {1.0F, 0.0F, 0.0F}, {}, {},
                            id, 26u);
                // 截断边界 = maxW，画一条竖线示意
                add(MODE_FILL,
                    {{static_cast<int32_t>(x0 + maxW), static_cast<int32_t>(y0 - 12.0F)},
                     {1u, 18u}},
                    {1.0F, 0.5F, 0.5F, 0.55F}, {}, {0.0F, 0.0F, 0.0F}, {}, {}, id, 26u);
            }
            // 27：富文本（多 run，各自字号与颜色，按 advance 串起来）
            {
                const VkRect2D box = cell(27);
                const uint32_t id = kTileIdBase + 27u;
                add(MODE_FILL, box, {0.11F, 0.12F, 0.15F, 1.0F}, {}, {10.0F, 0.0F, 0.0F},
                    {}, {}, id, 27u);
                struct run
                {
                    std::string_view text;
                    glm::vec4 color;
                    float scale;
                };
                const run runs[] = {
                    {"RICH ", {0.95F, 0.45F, 0.40F, 1.0F}, 1.0F},
                    {"TEXT ", {0.45F, 0.80F, 0.95F, 1.0F}, 1.4F},
                    {"MIX", {0.70F, 0.90F, 0.50F, 1.0F}, 0.8F},
                };
                float pen = static_cast<float>(box.offset.x) + 8.0F;
                const float baseline = static_cast<float>(box.offset.y) + 34.0F;
                for (const auto &r : runs)
                {
                    pen = text(pen, baseline, 15.0F * r.scale, r.color, r.text, box);
                    pen += 3.0F;
                }
            }
            // 28：光标 + 选区（按文本度量定位，不是固定矩形）
            {
                const VkRect2D box = cell(28);
                const uint32_t id = kTileIdBase + 28u;
                add(MODE_FILL, box, {0.11F, 0.12F, 0.15F, 1.0F}, {}, {10.0F, 0.0F, 0.0F},
                    {}, {}, id, 28u);
                constexpr std::string_view content = "SELECT TEXT";
                const float px = 14.0F;
                const float x0 = static_cast<float>(box.offset.x) + 8.0F;
                const float baseline = static_cast<float>(box.offset.y) + 30.0F;
                const float lineH = 18.0F;
                // 光标位置由鼠标 x 反算（真的做文本度量反查），再夹到 [0, len]
                const float localX =
                    static_cast<float>(inputCtx.input.cursorPos().xpos) - x0;
                float acc = 0.0F;
                int caret = 0;
                while (caret < static_cast<int>(content.size()) &&
                       acc + advanceOf(content[static_cast<std::size_t>(caret)]) * px <
                           localX)
                {
                    acc += advanceOf(content[static_cast<std::size_t>(caret)]) * px;
                    ++caret;
                }
                const float caretX =
                    x0 + measure(content.substr(0, static_cast<std::size_t>(caret)), px);
                // 选区 = [0, caret)
                if (caret > 0)
                    add(MODE_FILL,
                        {{static_cast<int32_t>(x0),
                          static_cast<int32_t>(baseline - lineH * 0.8F)},
                         {static_cast<uint32_t>(caretX - x0),
                          static_cast<uint32_t>(lineH)}},
                        {0.20F, 0.35F, 0.60F, 0.85F}, {}, {2.0F, 0.0F, 0.0F}, {}, {}, id,
                        28u);
                text(x0, baseline, px, {0.95F, 0.95F, 1.0F, 1.0F}, content, box);
                add(MODE_FILL,
                    {{static_cast<int32_t>(caretX),
                      static_cast<int32_t>(baseline - 13.0F)},
                     {2u, 17u}},
                    {1.0F, 1.0F, 1.0F, 0.95F}, {}, {1.0F, 0.0F, 0.0F}, {}, {}, id, 28u);
            }
            // 29：裁剪（滚动裁剪 / 嵌套圆角裁剪都靠 scissor 交集）
            {
                const VkRect2D box = cell(29);
                const uint32_t id = kTileIdBase + 29u;
                add(MODE_FILL, box, {0.11F, 0.12F, 0.15F, 1.0F}, {}, {10.0F, 0.0F, 0.0F},
                    {}, {}, id, 29u);
                const uint32_t halfW = box.extent.width / 2u;
                const VkRect2D left{{box.offset.x + 6, box.offset.y + 6},
                                    {halfW - 10u, box.extent.height - 12u}};
                const VkRect2D right{
                    {box.offset.x + static_cast<int32_t>(halfW) + 2, box.offset.y + 6},
                    {halfW - 10u, box.extent.height - 12u}};
                // 左：滚动裁剪 —— 真的滚动（滚轮驱动）：
                //   1) 偏移量累积成 scroll，行位置 = 基准 + scroll
                //   2) 只画落在裁剪框里的行（窗口化），其余根本不发命令
                //   3) 每帧重录命令表，所以滚起来是连续的
                {
                    static float scroll = 0.0F;
                    // 滚轮值只有真实事件才有限；默认哨兵（NaN/极大值）不能参与运算
                    const double wheel = inputCtx.input.scroll().yoffset;
                    if (std::isfinite(wheel) && std::abs(wheel) < 100.0)
                        scroll += static_cast<float>(wheel) * 24.0F;
                    scroll = std::fmod(scroll, 400.0F);
                    const float rowH = 26.0F;
                    const int32_t first =
                        static_cast<int32_t>(std::floor(-scroll / rowH)) - 1;
                    const int32_t rows =
                        static_cast<int32_t>(left.extent.height / rowH) + 3;
                    for (int32_t r = 0; r < rows; ++r)
                    {
                        const int32_t rowIndex = first + r;
                        const float ry = static_cast<float>(left.offset.y) +
                                         static_cast<float>(rowIndex) * rowH + scroll;
                        if (ry + rowH < static_cast<float>(left.offset.y) ||
                            ry > static_cast<float>(left.offset.y + left.extent.height))
                            continue; // 完全在裁剪框外：不发命令
                        add(MODE_FILL,
                            {{left.offset.x, static_cast<int32_t>(ry)},
                             {left.extent.width, static_cast<uint32_t>(rowH - 2.0F)}},
                            (rowIndex % 2) ? glm::vec4{0.22F, 0.40F, 0.62F, 1.0F}
                                           : glm::vec4{0.30F, 0.52F, 0.78F, 1.0F},
                            {}, {4.0F, 0.0F, 0.0F}, {}, left, id, 29u);
                    }
                    // 滚动条：长度按可见比例、位置按偏移
                    const float total = 400.0F;
                    const float ratio = static_cast<float>(left.extent.height) / total;
                    const int32_t barH = static_cast<int32_t>(
                        static_cast<float>(left.extent.height) * ratio);
                    const int32_t barY =
                        left.offset.y +
                        static_cast<int32_t>(
                            static_cast<float>(left.extent.height -
                                               static_cast<uint32_t>(barH)) *
                            (std::fmod(-scroll,
                                       total - static_cast<float>(left.extent.height)) /
                             std::max(1.0F,
                                      total - static_cast<float>(left.extent.height))));
                    add(MODE_FILL,
                        {{left.offset.x + static_cast<int32_t>(left.extent.width) - 3,
                          barY},
                         {3u, static_cast<uint32_t>(barH)}},
                        {1.0F, 1.0F, 1.0F, 0.45F}, {}, {1.0F, 0.0F, 0.0F}, {}, {}, id,
                        29u);
                }
                // 右：模板(stencil) 嵌套圆角裁剪 —— 不是 scissor 求交
                //   1) 外层圆角矩形：ALWAYS + INCR  → 区域变 1
                //   2) 内层圆角矩形：EQUAL 1 + INCR → 交集区域变 2
                //   3) 内容：        EQUAL 2 + KEEP → 只画交集，圆角保持
                const int32_t discR = static_cast<int32_t>(right.extent.width / 3);
                const VkRect2D innerShape{
                    {right.offset.x + static_cast<int32_t>(right.extent.width) - discR,
                     right.offset.y - discR},
                    {static_cast<uint32_t>(discR * 2), static_cast<uint32_t>(discR * 2)}};
                addPipe(MODE_CLIP_MASK, right, {}, {}, {20.0F, 0.0F, 0.0F}, {}, right, 4,
                        0u); // 外层：圆角矩形 → 模板 1
                addPipe(MODE_CLIP_MASK, innerShape, {}, {},
                        {static_cast<float>(discR), 0.0F, 0.0F}, {}, innerShape, 5,
                        1u); // 内层：圆 → 只在外层内再 +1，交集变 2
                addPipe(MODE_CONIC, right, {0.98F, 0.85F, 0.30F, 1.0F},
                        {0.25F, 0.75F, 0.60F, 1.0F}, {0.0F, 12.0F, 0.0F}, {}, right, 6,
                        2u); // 内容：只画模板 == 2 的地方
            }

            // 页面内容：玻璃图块下面的彩色壁纸 + 高频棋盘
            //   （玻璃本身比图块内缩一圈，四周留一圈"清晰"的壁纸作对照）
            if (inView(18))
            {
                const VkRect2D wallpaper = cell(18);
                add(MODE_RADIAL, wallpaper, {0.98F, 0.30F, 0.45F, 1.0F},
                    {0.15F, 0.10F, 0.30F, 1.0F}, {110.0F, 16.0F, 0.0F}, {});
                add(MODE_CONIC, wallpaper, {0.20F, 0.95F, 0.75F, 1.0F},
                    {0.98F, 0.85F, 0.20F, 1.0F}, {0.0F, 16.0F, 0.0F}, {});
                add(MODE_CHECKER, wallpaper, {0.98F, 0.98F, 0.98F, 0.55F},
                    {0.15F, 0.18F, 0.30F, 0.55F}, {7.0F, 0.0F, 0.0F}, {});
            }

            // ── 4.5 悬停高亮：picking 命中的图块描边 ────────────────────
            if (hoveredTile >= 0 && hoveredTile < kCapacity)
            {
                const VkRect2D box = cell(hoveredTile);
                const glm::vec4 line{1.0F, 1.0F, 1.0F, 0.55F};
                add(MODE_FILL, {{box.offset.x, box.offset.y}, {box.extent.width, 2u}},
                    line, {}, {1.0F, 0.0F, 0.0F});
                add(MODE_FILL,
                    {{box.offset.x,
                      box.offset.y + static_cast<int32_t>(box.extent.height) - 2},
                     {box.extent.width, 2u}},
                    line, {}, {1.0F, 0.0F, 0.0F});
                add(MODE_FILL, {{box.offset.x, box.offset.y}, {2u, box.extent.height}},
                    line, {}, {1.0F, 0.0F, 0.0F});
                add(MODE_FILL,
                    {{box.offset.x + static_cast<int32_t>(box.extent.width) - 2,
                      box.offset.y},
                     {2u, box.extent.height}},
                    line, {}, {1.0F, 0.0F, 0.0F});
            }

            // path 命令：even-odd 填充 + 描边，都由 emit 循环按 PathId 分发
            addPath(1, idFill, cell(14), {0.95F, 0.78F, 0.22F, 1.0F});
            // 左：三角形填充 + MSAA 边缘（test_ui_path_aa 的语义）
            addPath(1, idTri, msaaBox, {0.55F, 0.80F, 1.0F, 1.0F});
            // 右：贝塞尔描边 + 片元解析覆盖率（解析 AA 的语义）
            addPath(2, idStroke, strokeBox, {0.95F, 0.85F, 0.35F, 1.0F},
                    {4.0F, 0.0F, 0.0F});

            {
                uint32_t nPath = 0;
                for (uint32_t k = 0; k < cmdCount; ++k)
                    if (cmds[k].pathKind != 0)
                        ++nPath;
                static bool once = false;
                if (!once)
                {
                    once = true;
                    std::println("[CMDS] total={} path={} firstMode={} firstPathKind={}",
                                 cmdCount, nPath, (int)cmds[0].params.x,
                                 cmds[0].pathKind);
                }
            }
            // ── 5. 逐条录制：管线(按需) + 模板参考(按需) + setScissor + pushConstants + draw
            VkPipeline boundPipe = *mainCtx.pipelineTransparentUI;
            uint32_t boundRef = 0xFFFFFFFFu;
            for (uint32_t i = 0; i < cmdCount; ++i)
            {
                const ui_cmd &c = cmds[i];

                if (c.pathKind != 0)
                {
                    drawPathCommand(c); // 解析 PathId → 执行子 pass
                    boundPipe = *mainCtx.pipelineTransparentUI;
                    continue;
                }

                PushData pc{};
                // 0 = 普通全屏三角形；1/2 已被批处理几何入口（描边/扇形）占用
                pc.geomEntry = 0;
                pc.uiRect = c.rect;
                pc.uiColor = c.color;
                pc.uiColor2 = c.color2;
                pc.uiParams = c.params;
                pc.uiAux = c.aux;
                pc.uiPick = c.pick;

                const VkPipeline wantPipe = (c.pipe == 0)
                                                ? *mainCtx.pipelineTransparentUI
                                                : *(*uiPipelines().pipes[c.pipe]);
                if (wantPipe != boundPipe)
                {
                    commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, wantPipe);
                    boundPipe = wantPipe;
                }
                // 模板参考值是动态状态：每次绑定带模板的管线后都要重新设置一次
                if (c.pipe != 0)
                {
                    commandBuffer.setStencilReference(VK_STENCIL_FACE_FRONT_AND_BACK,
                                                      c.ref);
                }

                commandBuffer.setScissor(0, std::span{&c.scissor, 1});
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(PushData), &pc);
                commandBuffer.draw(3, 1, 0, 0);
            }

            // ── 6. endRendering ─────────────────────────────────────────
            commandBuffer.endRendering();

            // ── 7. 拾取回读（鼠标位置一个像素）+ 呈现转换 ───────────────
            {
                auto &pickingFrames = pickCtx.frames;
                const auto &mousePos = pickCtx.mouse.pos;

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

                const int32_t pickX =
                    std::clamp(static_cast<int32_t>(mousePos.x), 0,
                               std::max(0, static_cast<int32_t>(extent.width) - 1));
                const int32_t pickY =
                    std::clamp(static_cast<int32_t>(mousePos.y), 0,
                               std::max(0, static_cast<int32_t>(extent.height) - 1));
                commandBuffer.copyImageToBuffer(
                    resolveImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    pickingFrames[currentFrame].buffer(),
                    std::array<VkBufferImageCopy, 1>{VkBufferImageCopy{
                        .bufferOffset = 0,
                        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                        .imageOffset = {pickX, pickY, 0},
                        .imageExtent = {1, 1, 1}}});
            }

            // ── 7.5 真·背景模糊：把"已经画好的这一帧"blit 到离屏层再模糊
            //   1) swapchain(已 resolve) → TRANSFER_SRC，blit 到 layer D（顺便降采样）
            //   2) D→B 横糊、B→D 纵糊
            //   3) 第二段 rendering（LOAD，不清空）把模糊结果合成到 backdrop 图块
            {
                if (!inView(18))
                    return;
                const VkRect2D cellBox = cell(18);
                const VkRect2D tile{
                    {cellBox.offset.x + 12, cellBox.offset.y + 12},
                    {cellBox.extent.width - 24u, cellBox.extent.height - 24u}};
                auto &scratch = frameLayers[1];
                auto &blurred = frameLayers[3];

                my_render::transition_image_layout(
                    commandBuffer,
                    my_render::image_info{
                        .image = image,
                        .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .src = {.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                .access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                .stage_mask =
                                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT},
                        .dst = {.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                .access_mask = VK_ACCESS_2_TRANSFER_READ_BIT,
                                .stage_mask = VK_PIPELINE_STAGE_2_TRANSFER_BIT}});
                layerToColor(
                    blurred.image); // 顺手把层转到可写（TRANSFER_DST 由下一句接）
                my_render::transition_image_layout(
                    commandBuffer,
                    my_render::image_info{
                        .image = blurred.image,
                        .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .src = {.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                .access_mask = VK_ACCESS_2_NONE,
                                .stage_mask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT},
                        .dst = {.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                .access_mask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                .stage_mask = VK_PIPELINE_STAGE_2_TRANSFER_BIT}});
                commandBuffer.blitImage(
                    image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, blurred.image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    std::array<VkImageBlit, 1>{VkImageBlit{
                        .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                        .srcOffsets = {{0, 0, 0},
                                       {static_cast<int32_t>(extent.width),
                                        static_cast<int32_t>(extent.height), 1}},
                        .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                        .dstOffsets =
                            {{0, 0, 0},
                             {static_cast<int32_t>(ui_layer_ctx::kExtent.width),
                              static_cast<int32_t>(ui_layer_ctx::kExtent.height), 1}}}},
                    VK_FILTER_LINEAR);

                // 模糊：D→B（横）、B→D（纵）
                auto blurStep = [&](const ui_layer_ctx::layer &srcLayer,
                                    const ui_layer_ctx::layer &dstLayer, float dir,
                                    float radius) {
                    my_render::transition_image_layout(
                        commandBuffer,
                        my_render::image_info{
                            .image = srcLayer.image,
                            .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                            .src = {.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                    .access_mask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                    .stage_mask = VK_PIPELINE_STAGE_2_TRANSFER_BIT},
                            .dst = {.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    .access_mask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                                    .stage_mask =
                                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT}});
                    layerToColor(dstLayer.image);
                    layerBegin(dstLayer.view);
                    layerCmd(MODE_LAYER_BLUR, fullLayer, {}, {},
                             {dir, radius, static_cast<float>(srcLayer.texture)},
                             {static_cast<float>(srcLayer.sampler), 0.0F, 0.0F, 0.0F});
                    commandBuffer.endRendering();
                    layerToSampled(dstLayer.image);
                };
                blurStep(blurred, scratch, 0.0F, 2.0F);
                {
                    // B 已在 SHADER_READ；把 D 重新当写目标
                    my_render::transition_image_layout(
                        commandBuffer,
                        my_render::image_info{
                            .image = blurred.image,
                            .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                            .src = {.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    .access_mask = VK_ACCESS_2_NONE,
                                    .stage_mask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT},
                            .dst = {
                                .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                .access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                .stage_mask =
                                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT}});
                }
                layerBegin(blurred.view);
                layerCmd(MODE_LAYER_BLUR, fullLayer, {}, {},
                         {1.0F, 2.0F, static_cast<float>(scratch.texture)},
                         {static_cast<float>(scratch.sampler), 0.0F, 0.0F, 0.0F});
                commandBuffer.endRendering();
                layerToSampled(blurred.image);

                // swapchain 转回可写
                my_render::transition_image_layout(
                    commandBuffer,
                    my_render::image_info{
                        .image = image,
                        .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .src = {.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                .access_mask = VK_ACCESS_2_TRANSFER_READ_BIT,
                                .stage_mask = VK_PIPELINE_STAGE_2_TRANSFER_BIT},
                        .dst = {.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                .access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                .stage_mask =
                                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT}});

                // 第二段 pass：LOAD（保留已有画面），只画这块毛玻璃
                VkRenderingAttachmentInfo colorLoad = colorAttachment;
                colorLoad.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                VkRenderingAttachmentInfo pickLoad = pickAttachment;
                pickLoad.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                VkRenderingAttachmentInfo depthLoad = depthAttachment;
                depthLoad.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                std::array loadAttachments = {colorLoad, pickLoad};
                commandBuffer.beginRendering(
                    {.sType = sType<VkRenderingInfo>(),
                     .renderArea = {.offset = {.x = 0, .y = 0}, .extent = extent},
                     .layerCount = 1,
                     .colorAttachmentCount =
                         static_cast<uint32_t>(loadAttachments.size()),
                     .pColorAttachments = loadAttachments.data(),
                     .pDepthAttachment = &depthLoad,
                     .pStencilAttachment = hasStencil ? &depthLoad : nullptr});
                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *mainCtx.pipelineTransparentUI);
                commandBuffer.bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                 *pipelineLayout, 0, 1, &descriptorSet, 0,
                                                 nullptr);
                commandBuffer.setViewport(0, std::span{&viewport, 1});

                PushData glass{};
                glass.geomEntry = 0; // 普通全屏三角形
                glass.uiRect = {static_cast<float>(tile.offset.x),
                                static_cast<float>(tile.offset.y),
                                static_cast<float>(tile.extent.width),
                                static_cast<float>(tile.extent.height)};
                glass.uiParams = {static_cast<float>(MODE_LAYER_COMPOSITE), 1.0F,
                                  static_cast<float>(blurred.texture),
                                  static_cast<float>(blurred.sampler)};
                // aux.yz = 整屏尺寸：告诉着色器“层里存的是整屏”，按屏幕位置取 uv
                glass.uiAux = {12.0F, W, H, 0.0F};
                commandBuffer.setScissor(0, std::span{&tile, 1});
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(PushData), &glass);
                commandBuffer.draw(3, 1, 0, 0);
                // 玻璃面板本身（半透明，能看见后面的模糊内容）
                glass.uiParams = {static_cast<float>(MODE_FILL), 14.0F, 0.0F, 0.0F};
                glass.uiColor = {0.88F, 0.92F, 1.0F, 0.18F};
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(PushData), &glass);
                commandBuffer.draw(3, 1, 0, 0);
                commandBuffer.endRendering();
            }

            // ── 8. 调试：第 120 帧把整帧拷回 CPU，写成 PPM（验证用像素，不靠屏幕截图）
            static uint32_t frameCounter = 0;
            static bool dumpPending = false;
            const bool dumpThisFrame = (++frameCounter == 120u);
            if (dumpPending) // 上一帧的 copy 已执行完，这时读回缓冲才有效
            {
                dumpPending = false;
                globalCtx.device.waitIdle();
                const auto *px =
                    static_cast<const uint8_t *>(globalCtx.frameDump.buffer.mapPtr());
                std::FILE *f = std::fopen("ui_ability_frame.ppm", "wb");
                if (f != nullptr)
                {
                    std::fprintf(f, "P6\n%u %u\n255\n", extent.width, extent.height);
                    for (uint32_t i = 0; i < extent.width * extent.height; ++i)
                    {
                        const uint8_t rgb[3] = {px[i * 4 + 2], px[i * 4 + 1],
                                                px[i * 4 + 0]};
                        std::fwrite(rgb, 1, 3, f);
                    }
                    std::fclose(f);
                    std::println("[DUMP] wrote ui_ability_frame.ppm ({}x{})",
                                 extent.width, extent.height);
                }
            }
            if (dumpThisFrame)
            {
                my_render::transition_image_layout(
                    commandBuffer,
                    my_render::image_info{
                        .image = image,
                        .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .src = {.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                .access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                .stage_mask =
                                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT},
                        .dst = {.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                .access_mask = VK_ACCESS_2_TRANSFER_READ_BIT,
                                .stage_mask = VK_PIPELINE_STAGE_2_TRANSFER_BIT}});
                commandBuffer.copyImageToBuffer(
                    image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    globalCtx.frameDump.buffer.buffer(),
                    std::array<VkBufferImageCopy, 1>{VkBufferImageCopy{
                        .bufferOffset = 0,
                        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                        .imageOffset = {0, 0, 0},
                        .imageExtent = {extent.width, extent.height, 1}}});
            }

            // 呈现前转换
            my_render::transition_image_layout(
                commandBuffer,
                my_render::image_info{
                    .image = image,
                    .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .src = {.layout = dumpThisFrame
                                          ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                                          : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            .access_mask = dumpThisFrame
                                               ? VK_ACCESS_2_TRANSFER_READ_BIT
                                               : VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            .stage_mask =
                                dumpThisFrame
                                    ? VK_PIPELINE_STAGE_2_TRANSFER_BIT
                                    : VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT},
                    .dst = {.layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                            .access_mask = VK_ACCESS_2_NONE,
                            .stage_mask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT}});

            if (dumpThisFrame)
                dumpPending = true;
        }),
        method<"endRendering">([](world_type &, input_type &, data_type &) {}));

    // NOLINTNEXTLINE
    static constexpr auto recordCommandBuffer =
        [](world_type &world, input_type &inputCtx, data_type &soaCtx) {
            auto &globalCtx = world.globalCtx;
            auto &recordCtx = world.recordCtx;
            auto &commandBuffers = globalCtx.commandBuffers;

            auto [currentFrame, imageIndex] = recordCtx.info;
            const auto &commandBuffer = commandBuffers[currentFrame];

            commandBuffer.begin({.sType = sType<VkCommandBufferBeginInfo>()});
            mainPipeline.draw(world, inputCtx, soaCtx); // 全部 vkCmd* 都在 draw 里
            commandBuffer.end();
        };
    // diff: [test_dod5] end

    // NOLINTNEXTLINE
    static constexpr auto recreateSwapChain = [](world_type &world, input_type &inputCtx,
                                                 data_type &soaCtx) constexpr {
        auto &globalCtx = world.globalCtx;
        auto &pickCtx = world.pickCtx;
        auto &mainCtx = world.mainCtx;

        auto &device = globalCtx.device;
        auto &surface = globalCtx.surface;
        auto &swapchainBuild = globalCtx.swapchainBuild;
        auto &swapchain = globalCtx.swapchain;
        auto &camera = inputCtx.camera;
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

        auto &screen = soaCtx.screen;

        surface.waitGoodFramebufferSize();
        device.waitIdle();

        swapchain = swapchainBuild.rebuild(device, surface);
        auto newExtent = swapchain.refImageExtent();
        VkExtent3D imageExtent = {
            .width = newExtent.width, .height = newExtent.height, .depth = 1};

        screen.updateSize(newExtent.width, newExtent.height);

        msaaResource = msaaResourcesBuild.setCreateInfoExtent(imageExtent).build(device);
        depthResource =
            depthResourcesBuild.setCreateInfoExtent(imageExtent).build(device);
        camera.refProjection().setAspect(newExtent.width /
                                         static_cast<float>(newExtent.height));

        pickResource = pickResourcesBuild.setCreateInfoExtent(imageExtent).build(device);
        resolveResource =
            resolveResourcesBuild.setCreateInfoExtent(imageExtent).build(device);

        pickMouse.valid = false;
        frameContext =
            std::decay_t<decltype(frameContext)>(device, swapchain.imagesSize());
    };
    // diff: [test_dod5] start

    static constexpr auto limit_frame_rate = [](input_type &inputCtx,
                                                float targetFPS) noexcept {
        auto &clk = inputCtx.clock;
        using namespace std::chrono;
        using Clock = FrameClock::Clock;
        using FloatSeconds = duration<float>;

        const auto targetFrameDuration = FloatSeconds(1.0f / targetFPS);
        auto now = Clock::now();

        // 计算下一帧的理想开始时间（基于上一帧结束时间）
        auto nextFrameTime =
            clk.lastTime + duration_cast<Clock::duration>(targetFrameDuration);

        // 如果当前时间已经晚于理想时间，说明上一帧超时，不等待
        if (now < nextFrameTime)
        {
            std::this_thread::sleep_until(nextFrameTime);
            // sleep 后重新获取精确时间，更新 lastTime，避免累积误差
            clk.lastTime = Clock::now();
        }
        else
        {
            // 已经超时，直接更新 lastTime 为当前时间
            clk.lastTime = now;
        }
    };
    static constexpr auto tickClock = [](input_type &inputCtx) noexcept {
        auto &clk = inputCtx.clock;
        auto now = FrameClock::Clock::now();
        if (clk.lastTime.time_since_epoch().count() != 0)
        {
            float raw = std::chrono::duration<float>(now - clk.lastTime).count();
            clk.deltaTime = glm::clamp(raw, 0.001f, 0.1f);
        }
        clk.lastTime = now;
    };
    static constexpr auto FrameRateMonitor = [](world_type &world, input_type &inputCtx,
                                                data_type &soaCtx) {
        auto &clk = inputCtx.clock;
        static auto lastPrint =
            FrameClock::Clock::now(); // 这里的 static 只用于打印间隔，无伤大雅
        static int frames = 0;
        ++frames;
        auto now = FrameClock::Clock::now();
        float elapsed = std::chrono::duration<float>(now - lastPrint).count();
        if (elapsed >= 1.0f)
        {
            float fps = frames / elapsed;
            std::println("[FPS] {:5.1f} (avg frame: {:.3f} ms)", fps, 1000.0f / fps);
            frames = 0;
            lastPrint = now;
        }
    };
    // diff: [test_dod5] end
    static constexpr auto InputController = [](world_type &world, input_type &inputCtx,
                                               data_type &soaCtx) {
        auto &pickCtx = world.pickCtx;
        auto &globalCtx = world.globalCtx;

        auto &input = inputCtx.input;
        auto &pickMouse = pickCtx.mouse;
        auto &swapchain = globalCtx.swapchain;

        Window::pollEvents();

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

        // 无 UI 组件，无需额外输入控制
        // inputController(world, inputCtx, soaCtx); // 已空
    };

    //NOTE: 下面的内联做的更好，编译期的数据更利于优化
    constexpr auto marking_function = ^^decltype([](auto &&...) noexcept {});
    static constexpr auto task_graph = make_task<
        init_task<
            {.name = "input_start", .function = marking_function},
            {.name = "input_end", .function = marking_function},
            {.name = "after_waitForfences_start", .function = marking_function},
            {.name = "after_waitForfences_end", .function = marking_function},
            {.name = "before_recordCommandBuffer_start", .function = marking_function},
            {.name = "before_recordCommandBuffer_end", .function = marking_function}>,
        [] {
            return schedulable_task{
                .task = {.name = "input_handle",
                         .function =
                             ^^decltype([](world_type &world, input_type &inputCtx,
                                           data_type &soaCtx) {
                                 tickClock(inputCtx); // ① 唯一的时间推进
                                 limit_frame_rate(inputCtx,
                                                  60.0f); // ② 帧率限制（可选）
                                 InputController(world, inputCtx, soaCtx);
                                 views_matrix_update(world, inputCtx, soaCtx);
                                 views_perspective_update(world, inputCtx, soaCtx);
                                 views_ui_camera_update(world, inputCtx, soaCtx);
                                 // model_update 已空，可删除此行
                                 FrameRateMonitor(world, inputCtx,
                                                  soaCtx);     // ③ 纯监控
                                 inputCtx.input.scroll() = {}; // 滚轮清零放在最后
                             })},
                .befores = {"input_start"},
                .afters = {"input_end"}};
        },
        [] {
            return schedulable_task{
                .task =
                    {.name = "waitforfences_post_processing",
                     .function = ^^decltype([](world_type &world, input_type &inputCtx,
                                               data_type &soaCtx) {
                         auto &globalCtx = world.globalCtx;
                         auto &pickCtx = world.pickCtx;

                         auto &frameContext = globalCtx.frameContext;

                         auto &pickMouse = pickCtx.mouse;
                         auto &pickingFrames = pickCtx.frames;

                         auto &currentFrame = frameContext.currentFrame;

                         // 等待栅栏后，正式获取图像前
                         // currentFrame == 0 时拾取回读未就绪，不当作"离开"
                         if (currentFrame > 0)
                         {
                             uint32_t readIdx =
                                 (currentFrame - 1 + MAX_FRAMES_IN_FLIGHT) %
                                 MAX_FRAMES_IN_FLIGHT;
                             //diff: [test_dod19] start: 全部逻辑交给 hover_manager（不耦合全局函数）
                             auto *data = static_cast<picking_result *>(
                                 pickingFrames[readIdx].mapPtr());
                             picking_result r = *data;
                             // ★ 版本不匹配 → 旧世界的 picking，作废悬停状态，丢弃
                             if (r.render_version() != soaCtx.screen.renderVersion())
                             {
                                 world.hoverManager.resetPicking();
                                 return;
                             }

                             if (!pickMouse.valid)
                                 r.key.object_type =
                                     0xFFFFFFFF; // 光标不在窗口 = 无命中（自动 leave）

                             // [PICKDBG] 定位用：每帧打印 GPU 回读原始值（可删）
                             std::println("[PICKDBG] frame={} valid={} type={:#x} "
                                          "entity={} primitive={} ver={} hover_fn={}",
                                          currentFrame, pickMouse.valid,
                                          r.key.object_type, r.key.entity_index,
                                          r.primitive_id(), r.render_version(),
                                          r.hover_fn);
                             world.hoverManager.hover(r, world.hoverPool, inputCtx);
                             //diff: [test_dod19] end
                         }
                     })},
                .befores = {"after_waitForfences_start"},
                .afters = {"after_waitForfences_end"},
            };
        },
        [] {
            return schedulable_task{
                .task = {.name = "update_upload_data",
                         .function =
                             ^^decltype([](world_type &world, input_type &inputCtx,
                                           data_type &soaCtx) {
                                 auto &globalCtx = world.globalCtx;
                                 auto &mainShaderCtx = world.mainShaderCtx;

                                 auto &frameContext = globalCtx.frameContext;
                                 auto &camera = inputCtx.camera;

                                 auto &currentFrame = frameContext.currentFrame;

                                 auto &uniformBuffers = mainShaderCtx.uniformBuffers;

                                 //diff: [test_dod3] start
                                 // update 已空，删除
                                 // updateVertexData 已空，删除
                                 prepareBatch(world, inputCtx, soaCtx, currentFrame);
                                 //diff: [test_dod3] end
                             })},
                .befores = {"before_recordCommandBuffer_start"},
                .afters = {"before_recordCommandBuffer_end"}};
        }>{};
    std::cout << "task_graph: [begin]\n"
              << task_graph.task_sequence_string_detail() << "\ntask_graph: [end]\n";

    // NOLINTNEXTLINE
    static constexpr auto drawFrame = [](world_type &world, input_type &inputCtx,
                                         data_type &soaCtx) constexpr {
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
        task_graph.invoke_ranges<"after_waitForfences_start", "after_waitForfences_end">(
            world, inputCtx, soaCtx);

        auto [result, imageIndex] = swapchain.acquireNextImage(
            UINT64_MAX, presentCompleteSemaphore[semaphoreIndex], nullptr);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        {
            recreateSwapChain(world, inputCtx, soaCtx);
            return;
        }
        if (result != VK_SUCCESS)
            throw std::runtime_error("failed to acquire swap chain image!");
        device.resetFences(1, inFlightFences[currentFrame]);

        task_graph.invoke_ranges<"before_recordCommandBuffer_start",
                                 "before_recordCommandBuffer_end">(world, inputCtx,
                                                                   soaCtx);

        const auto &commandBuffer = commandBuffers[currentFrame];
        commandBuffer.reset({});
        recordCtx.info = {.current_frame = currentFrame, .image_index = imageIndex};
        recordCommandBuffer(world, inputCtx, soaCtx);

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
            recreateSwapChain(world, inputCtx, soaCtx);
        }
        semaphoreIndex = (semaphoreIndex + 1) % presentCompleteSemaphore.size();
        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    };
    uint32_t autoExitFrames = 0;
    while (globalCtx.window.shouldClose() == 0)
    {
        task_graph.invoke_ranges<"input_start", "input_end">(world, inputCtx, soaCtx);
        drawFrame(world, inputCtx, soaCtx);
        // 可选自动退出：只给验证用（DSH_AUTOEXIT 设了就渲染 150 帧后退出），
        // 这样退出路径（含 device 销毁）也能被 validation 覆盖到
        if (std::getenv("DSH_AUTOEXIT") != nullptr && ++autoExitFrames >= 150)
            break;
    }
    device.waitIdle();

    // 必须在 device 销毁前清掉静态管线：函数内 static 的析构晚于 device，
    // 否则退出时每条管线都会报 "VkPipeline ... has not been destroyed"
    uiPipelines().reset();

    std::cout << "main done\n";
    return 0;
}
catch (std::exception &e)
{
    std::println("main catch exception: {}", e.what());
}
