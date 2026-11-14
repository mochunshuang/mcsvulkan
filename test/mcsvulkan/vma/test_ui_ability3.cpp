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
using mcs::vulkan::tool::pNext;
using mcs::vulkan::tool::Flags;

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
    constexpr static VkImageMemoryBarrier2 to_barrier(const image_info &info) noexcept
    {
        return VkImageMemoryBarrier2{
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
                                 .layerCount = VK_REMAINING_ARRAY_LAYERS}};
    }

    constexpr static void transition_image_layout(
        const CommandBufferView &commandBuffer,
        std::span<const VkImageMemoryBarrier2> barriers) noexcept
    {
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
    template <std::same_as<image_info>... T>
    constexpr static void transition_image_layout(const CommandBufferView &commandBuffer,
                                                  T... info) noexcept
    {
        std::array<VkImageMemoryBarrier2, sizeof...(T)> barriers{to_barrier(info)...};
        transition_image_layout(commandBuffer, barriers);
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

#include "test_ui_ability3_dao.hpp"
#include "test_ui_ability3_shader_buffers.hpp"

using PushData = shader_data::PushData;
namespace command
{
    struct command_context_data
    {
        // 录制过程需要的API,关联的全部类型+数据
    };

    struct per_draw_key
    {
    };

    // 在 beginRendering 之前或 endRendering 之后记录.避免块内同步。块内需要额外扩展
    struct transition_image
    {
        using image_info = my_render::image_info;
        std::vector<VkImageMemoryBarrier2> data;
        constexpr auto apply(const CommandBufferView &commandBuffer) const noexcept
        {
            if (data.size() > 0)
                my_render::transition_image_layout(commandBuffer, data);
        }
    };

    /*
    typedef struct VkRenderingInfo {
        VkStructureType                     sType;
        const void*                         pNext;
        VkRenderingFlags                    flags;
        VkRect2D                            renderArea;
        uint32_t                            layerCount;
        uint32_t                            viewMask;
        uint32_t                            colorAttachmentCount;
        const VkRenderingAttachmentInfo*    pColorAttachments;
        const VkRenderingAttachmentInfo*    pDepthAttachment;
        const VkRenderingAttachmentInfo*    pStencilAttachment;
    } VkRenderingInfo;
    */
    struct rendering_info
    {
        struct rendering_info_submit
        {
            std::vector<VkRenderingAttachmentInfo> colorAttachments;
            std::optional<VkRenderingAttachmentInfo> depthAttachment;
            std::optional<VkRenderingAttachmentInfo> stencilAttachment;

            constexpr void apply(const rendering_info &info)
            {
                colorAttachments.resize(info.colorAttachments.size());
                for (size_t i = 0; i < info.colorAttachments.size(); ++i)
                    colorAttachments[i] = info.colorAttachments[i];

                depthAttachment = info.depthAttachment
                                      ? *info.depthAttachment
                                      : std::optional<VkRenderingAttachmentInfo>{};
                stencilAttachment = info.stencilAttachment
                                        ? *info.stencilAttachment
                                        : std::optional<VkRenderingAttachmentInfo>{};
            }

            [[nodiscard]] constexpr VkRenderingInfo operator()(
                const rendering_info &info) const noexcept
            {
                return {.sType = sType<VkRenderingInfo>(),
                        .pNext = *info.pNext,
                        .flags = info.flags,
                        .renderArea = info.renderArea,
                        .layerCount = info.layerCount,
                        .viewMask = info.viewMask,
                        .colorAttachmentCount =
                            static_cast<uint32_t>(colorAttachments.size()),
                        .pColorAttachments = colorAttachments.data(),
                        .pDepthAttachment =
                            depthAttachment ? &(*depthAttachment) : nullptr,
                        .pStencilAttachment =
                            stencilAttachment ? &(*stencilAttachment) : nullptr};
            }
        };

        /*
        typedef struct VkRenderingAttachmentInfo {
            VkStructureType          sType;
            const void*              pNext;
            VkImageView              imageView;
            VkImageLayout            imageLayout;
            VkResolveModeFlagBits    resolveMode;
            VkImageView              resolveImageView;
            VkImageLayout            resolveImageLayout;
            VkAttachmentLoadOp       loadOp;
            VkAttachmentStoreOp      storeOp;
            VkClearValue             clearValue;
        } VkRenderingAttachmentInfo;
        */
        struct rendering_attachment // NOLINTBEGIN
        {
            [[nodiscard]] operator VkRenderingAttachmentInfo() const noexcept
            {
                return {.sType = sType<VkRenderingAttachmentInfo>(),
                        .pNext = *pNext,
                        .imageView = imageView,
                        .imageLayout = imageLayout,
                        .resolveMode = resolveMode,
                        .resolveImageView = resolveImageView,
                        .resolveImageLayout = resolveImageLayout,
                        .loadOp = loadOp,
                        .storeOp = storeOp,
                        .clearValue = clearValue};
            }
            pNext pNext{};
            VkImageView imageView{};
            VkImageLayout imageLayout{};
            VkResolveModeFlagBits resolveMode{};
            VkImageView resolveImageView{};
            VkImageLayout resolveImageLayout{};
            VkAttachmentLoadOp loadOp{};
            VkAttachmentStoreOp storeOp{};
            VkClearValue clearValue{};
        }; // NOLINTEND
        // NOLINTBEGIN
        pNext pNext{};
        Flags<VkRenderingFlagBits> flags{};
        VkRect2D renderArea{};
        uint32_t layerCount{};
        uint32_t viewMask{};
        std::vector<rendering_attachment> colorAttachments;
        std::optional<rendering_attachment> depthAttachment;
        std::optional<rendering_attachment> stencilAttachment;
    }; // NOLINTEND

    struct rendering_scope_build
    {
        rendering_scope_build() = default;

        void setbeginRendering(rendering_info info)
        {
            beginRendering_ = std::move(info);
            __beginRendering_.apply(beginRendering_);
        }
        constexpr auto apply(const CommandBufferView &commandBuffer,
                             command_context_data &context_data) const noexcept
        {
            commandBuffer.beginRendering(__beginRendering_(beginRendering_));
            commandBuffer.endRendering();
        }

      private:
        rendering_info beginRendering_;
        rendering_info::rendering_info_submit __beginRendering_;
    };

    struct command_pass
    {
        transition_image transitions;
        rendering_scope_build renderingBuild;

        constexpr auto operator()(const CommandBufferView &commandBuffer,
                                  command_context_data &context_data) noexcept
        {
            transitions.apply(commandBuffer);
            renderingBuild.apply(commandBuffer, context_data);
        }
    };

    struct command_recorder
    {
        std::vector<command_pass> pass;

        constexpr auto operator()(const CommandBufferView &commandBuffer,
                                  command_context_data &context_data) noexcept
        {
        }
    };

}; // namespace command
//
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
                     // NOTE: test_intent8.frag 用 gl_PrimitiveID，SPIR-V 会声明 Geometry
                     // capability；必须在 device 上打开 geometryShader。
                     .geometryShader = VK_TRUE,
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
                       features2.features.geometryShader &&
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
    std::array<std::optional<mcs::vulkan::Pipeline>, 12> pipes{};
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

    // [08-4] 专用：blend OFF + 只写 R/G，B/A 保留 dst；用来观察 colorWriteMask。
    auto maskRG = blendOver;
    maskRG.blendEnable = VK_FALSE;
    maskRG.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT;
    uiPipelines().pipes[9] = makePipelineEx(maskRG, VK_FALSE, stencilAlwaysIncr, 0);

    // [08-7] 专用：blend ON + 只写 R/G（和 pipes[9] 只差 blendEnable）。
    auto maskRGBlend = maskRG;
    maskRGBlend.blendEnable = VK_TRUE;
    uiPipelines().pipes[10] = makePipelineEx(maskRGBlend, VK_FALSE, stencilAlwaysIncr, 0);

    // [08-3/4] 专用：stencil EQUAL + blend OFF。stencil pass 只决定“留/丢”，
    // 不会顺带发生 blend；source 通过后是直接写红色，不是混合。
    const auto noBlendAll = VkPipelineColorBlendAttachmentState{
        .blendEnable = VK_FALSE,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    uiPipelines().pipes[11] =
        makePipelineEx(noBlendAll, VK_TRUE, stencilEqualKeep,
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

namespace shader_data
{

    // NOTE:    ?
    // NOTE:  ?shader ?

    // ===================== hover  ?+   =====================
    //  ? ?  =  ?hover  ?
    //  ?hover_fn over_fn = 0xFFFFFFFF  ?
    struct hover_pool
    {
        using hover_callback_t = void (*)(picking_result, bool, InputCtx &,
                                          ui_new::ScreenWidget *) noexcept;
        std::vector<hover_callback_t> hover_fns;

        //  ?hover  ?  ?
        uint32_t bind(hover_callback_t fn)
        {
            hover_fns.push_back(std::move(fn));
            return hover_fns.size() - 1;
        }
        //  0xFFFFFFFF =  ?=
        void call(uint32_t entity, const picking_result &r, bool enter,
                  InputCtx &inputCtx, ui_new::ScreenWidget *screen) noexcept
        {
            if (entity == ~0U)
                return;
            // 没有注册任何 hover 回调时（例如 ability3 的最小矩形），
            // picking 里回读到的 hover_fn 可能是 0；这里直接忽略，不 assert。
            if (entity >= hover_fns.size())
                return;
            hover_fns[entity](r, enter, inputCtx, screen);
        }
    };

    //  ?cur ?cur   pre  leave ?
    struct hover_manager
    {
        static constexpr auto hover_leave = false;
        static constexpr auto hover_enter = true;
        ui_new::ScreenWidget *screen = nullptr;
        picking_result cur{object_key{0xFFFFFFFF, 0}, 0, 0}; //  ey   =

        constexpr void resetPicking() noexcept
        {
            cur = {object_key{0xFFFFFFFF, 0}, 0, 0};
        }

        constexpr void hover(const picking_result &r, hover_pool &pool,
                             InputCtx &inputCtx) noexcept
        {
            static_assert(0xFFFFFFFF == uint32_t{~0U});
            if (cur.key == r.key)
                return; //
            if (cur.key.object_type != 0xFFFFFFFF && cur.hover_fn != ~0U)
                pool.call(cur.hover_fn, cur, hover_leave, inputCtx, screen);
            cur = r;
            if (r.key.object_type != 0xFFFFFFFF && r.hover_fn != ~0U)
                pool.call(r.hover_fn, r, hover_enter, inputCtx, screen);
        }
    };

}; // namespace shader_data

constexpr auto &hoverPool() noexcept
{
    static shader_data::hover_pool hoverPool{};
    return hoverPool;
}
struct render_context
{
    command::command_recorder &commandRecorder;
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

// NOTE: hover在帧稳定才触发，noexcept 没有问题

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
                const bool isDistance = (ftype == FT::eSDF || ftype == FT::ePSDF ||
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

    // command 语料的数据区：容量是“素材”，不是工程尺寸。
    //   heap: 64 KiB（Shape/PathFill/PathStroke/NoiseQuad 四段连续数组）
    //   vertex/index: 1 KiB 各够 quad 副本、16-bit 索引和多套 indirect 参数
    //   indirect/constant: 8 KiB / 4 KiB 够所有 case 的 command + CommandConstant
    constexpr VkDeviceSize kCorpusHeapBytes = 64u * 1024u;
    constexpr VkDeviceSize kCorpusVertexBytes = 1024u;
    constexpr VkDeviceSize kCorpusIndexBytes = 1024u;
    constexpr VkDeviceSize kCorpusIndirectBytes = 8u * 1024u;
    constexpr VkDeviceSize kCorpusConstantBytes = 4u * 1024u;
    auto shaderBuffers = shader_data::ShaderBuffers(
        device, kCorpusVertexBytes, kCorpusIndexBytes, kCorpusHeapBytes,
        kCorpusIndirectBytes, kCorpusConstantBytes);

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

    auto globalCtx =
        make_aggregate_ref<"globalCtx", "device", "window", "surface", "swapchainBuild",
                           "swapchain", "frameContext", "commandPool", "commandBuffers",
                           "queue", "meshMap">(
            device, window, surface, swapchainBuild, swapchain, frameContext, commandPool,
            commandBuffers, GRAPHICS_AND_PRESENT, meshMap);

    auto mainCtx =
        make_aggregate_ref<"mainCtx", "pipelineOpaque3D", "pipelineTransparent3D",
                           "pipelineOpaqueUI", "pipelineTransparentUI", "pipelineUILayer",
                           "pipelineLayout", "depthResourcesBuild", "depthResource",
                           "msaaResourcesBuild", "msaaResource", "shaderBuffers">(
            pipelineOpaque3D, pipelineTransparent3D, pipelineOpaqueUI,
            pipelineTransparentUI, pipelineUILayer, pipelineLayout, depthResourcesBuild,
            depthResource, msaaResourcesBuild, msaaResource, shaderBuffers);

    auto mainShaderCtx =
        make_aggregate_ref<"mainShaderCtx", "uniformBuffers", "descriptorSets">(
            uniformBuffers, descriptorSets);

    ui_new::ScreenWidget screen{ui_new::Size{WIDTH, HEIGHT}, ui_new::Container("panel")};

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

            // ↓↓↓ 一个 VkCommandBuffer；下面全部是裸 vkCmd*，没有 CommandBuilder ↓↓↓
            const auto &[currentFrame, imageIndex] = recordCtx.info;
            const auto &commandBuffer = commandBuffers[currentFrame];

            auto &pipelineLayout = mainCtx.pipelineLayout;
            auto &descriptorSets = mainShaderCtx.descriptorSets;
            auto &uniformBuffers = mainShaderCtx.uniformBuffers;
            const VkDescriptorSet descriptorSet = descriptorSets[currentFrame];
            const VkDescriptorSet descriptorSetAlternate =
                descriptorSets[(currentFrame + 1) % MAX_FRAMES_IN_FLIGHT];

            const VkExtent2D extent = swapchain.refImageExtent();
            const float W = static_cast<float>(extent.width);
            const float H = static_cast<float>(extent.height);

            const VkImage image = swapchain.image(imageIndex);
            const VkImage depthImage = mainCtx.depthResource.image();
            const VkImage msaaImage = mainCtx.msaaResource.image();
            const VkImage pickingImage = pickCtx.pickResource.image();
            const VkImage resolveImage = pickCtx.resolveResource.image();

            auto &sb = mainCtx.shaderBuffers;
            const ui_dao::view view{W, H};

            // ════════════════════════════════════════════════════════════════
            // 数据协议（先看这里，再看 case）
            //
            //   一个 PushData 只描述“这一条 draw 从哪里取数据”：
            //     vertexAddress          → Vertex[] 基址
            //     instanceAddress        → 实例堆基址
            //     commandConstantsAddress→ CommandConstant[] 基址
            //     cameraIndex            → UBO 里的相机
            //     renderVersion          → 拾取打包
            //
            //   直接 draw 时 gl_DrawIDARB == 0，所以每次 push 只指向“这一条 draw
            //   对应的那一个 CommandConstant”；CommandConstant{program,address_offset}
            //   里的 address_offset 是相对 instanceAddress 的字节偏移，program 决定
            //   实例行的结构体大小。
            //
            //   indirect 多 draw 时 push 指向 CommandConstant[] 的首地址；第 i 条
            //   indirect command 的 gl_DrawIDARB == i，于是 shader 用
            //   consts[gl_DrawIDARB] 选 program/address_offset。实例行由
            //   gl_InstanceIndex 选，所以一次 drawIndexedIndirect(count=N) 可以
            //   同时画 N 个不同 program、不同数据的实例。
            //
            //   本语料把数据原地造在一个 64 KiB 的 heap 里：
            //     Shape[] | PathFill[] | PathStroke[] | NoiseQuad[]
            //   四段连续数组，各自按 shader 的 sizeof 步进；CommandConstant[] 和
            //   indirect command[] 放在各自的 buffer。下面的 add_* 只做写数据，
            //   不包装任何 vkCmd*，每个 case 的 C API 顺序仍然是手写的。
            // ════════════════════════════════════════════════════════════════
            // ════════════════════════════════════════════════════════════════
            // C API 索引（给 command 命名空间当原始素材；详细解释在各 case 前）
            //
            //   状态类：BindPipeline / BindDescriptorSets / SetViewport / SetScissor /
            //           BindIndexBuffer / BindVertexBuffers / SetStencilReference
            //   数据类：PushConstants / DrawIndexed / Draw / DrawIndexedIndirect /
            //           DrawIndirect / VkDrawIndexedIndirectCommand / CommandConstant
            //   顺序类：state-before-pipeline / set-before-pipeline / push-per-draw
            //   scope 类：BeginRendering / EndRendering / loadOp / renderArea /
            //             suspend-resume
            //   同步/搬运类：PipelineBarrier2（只放 scope 外）/ CopyImageToBuffer /
            //                Transition（只放 scope 外）
            //
            // [00] BeginR → BindPipeline → BindDescriptorSets → SetViewport →
            //      SetScissor → BindIndexBuffer → PushConstants → DrawIndexed → EndR
            // [01] SetViewport → SetScissor → PushConstants → DrawIndexed(6,4)
            // [02] SetViewport → SetScissor → Push → DrawIndexed(firstInstance=0) →
            //      DrawIndexed(firstInstance=1)
            // [03] SetViewport → SetScissor → Push(A) → DrawIndexed → Push(B) →
            //      DrawIndexed
            // [04] BindPipeline(A) → SetViewport → SetScissor → Push → DrawIndexed →
            //      BindPipeline(B) → Push → DrawIndexed
            // [05] SetViewport → SetScissor → BindDescriptorSets(A) → Push →
            //      DrawIndexed → BindDescriptorSets(B) → DrawIndexed
            // [06] SetScissor → SetViewport(A) → Push → DrawIndexed →
            //      SetViewport(B) → DrawIndexed
            // [07] SetViewport → SetScissor(A) → Push → DrawIndexed →
            //      SetScissor(B) → DrawIndexed
            // [08] BindIndexBuffer(0) → Push → DrawIndexed(6,firstInstance=0) →
            //      BindIndexBuffer(24) → DrawIndexed(3,firstInstance=1)
            // [08-1] BindPipeline(opaque3D) → SetViewport → SetScissor →
            //        BindIndexBuffer(0) → Push → DrawIndexed(Shape0) →
            //        DrawIndexed(Shape1, depth pass)
            // [08-2] 同 [08-1]，第二条 firstInstance 1→2（Shape2 z=.75），depth fail
            // [08-3] BindPipeline(pipe4) → SetViewport → SetScissor →
            //        BindIndexBuffer(0) → Push → SetStencilReference(0) →
            //        DrawIndexed(base, stencil setup) →
            //        BindPipeline(opaque) → DrawIndexed(base, color) →
            //        BindPipeline(pipe11) → SetStencilReference(1) →
            //        DrawIndexed(source, stencil pass, blend OFF)
            // [08-4] 同 [08-3]，pipe11 的 SetStencilReference 改成 0，stencil fail
            // [08-5] BindPipeline(opaque) → ... → DrawIndexed(base) →
            //        BindPipeline(transparent) → DrawIndexed(source, blend ON, mask ALL)
            // [08-6] 同 [08-5]，第二条 pipeline 改成 opaqueUI（blend OFF, mask ALL）
            // [08-7] 同 [08-5]，第二条 pipeline 改成 maskRGBlend（blend ON, mask R|G）
            // [08-8] 同 [08-5]，第二条 pipeline 改成 maskRG（blend OFF, mask R|G）
            // [09] BindIndexBuffer(UINT32,0) → DrawIndexed →
            //      BindIndexBuffer(UINT16,72) → DrawIndexed
            // [10] BindPipeline(transparent) → BindDescriptorSets → SetViewport →
            //      SetScissor → BindIndexBuffer(24) → Push →
            //      DrawIndexed(3) → Draw(3,firstVertex=0)
            // [11] BindPipeline(opaque) → BindIndexBuffer → SetViewport → SetScissor →
            //      Push → DrawIndexed(6) → DrawIndexed(3,firstIndex=1,vertexOffset=4)
            // [12] BindPipeline(transparent) → BindDescriptorSets → SetViewport →
            //      SetScissor → BindIndexBuffer → Push → DrawIndexedIndirect(count=1)
            // [13] BindPipeline(transparent) → BindDescriptorSets → SetViewport →
            //      SetScissor → BindIndexBuffer → Push(cS) →
            //      DrawIndexedIndirect(count=2)
            // [14] SetViewport → SetScissor → Push → DrawIndexed(6,3,firstInstance=1)
            // [15] BindVertexBuffers → BindPipeline(transparent) → BindDescriptorSets →
            //      SetViewport → SetScissor → BindIndexBuffer → Push → DrawIndexed
            // [16] SetViewport → SetScissor → BindDescriptorSets → BindIndexBuffer →
            //      BindPipeline(opaque) → Push → DrawIndexed
            // [17] PB → BeginR(LOAD) → BindPipeline → BindDescriptorSets → SetViewport →
            //      SetScissor → BindIndexBuffer → Push(A) → DrawIndexed → EndR →
            //      PB → BeginR(LOAD) → Push(B) → DrawIndexed → EndR
            // [18] PB → BeginR(renderArea=tile, CLEAR) → BindPipeline →
            //      BindDescriptorSets → SetViewport → SetScissor → BindIndexBuffer →
            //      Push → DrawIndexed → EndR
            // [19] PB → BeginR(SUSPENDING) → BindPipeline → BindDescriptorSets →
            //      SetViewport → SetScissor → BindIndexBuffer → Push(A) →
            //      DrawIndexed → EndR → BeginR(RESUMING) → BindPipeline →
            //      BindDescriptorSets → SetViewport → SetScissor → BindIndexBuffer →
            //      Push(B) → DrawIndexed → EndR
            // [20] Transition(resolve→TRANSFER_SRC) → CopyImageToBuffer →
            //      Transition(resolve→COLOR) → PB → BeginR(LOAD) → BindPipeline →
            //      BindDescriptorSets → SetViewport → SetScissor → BindIndexBuffer →
            //      Push → DrawIndexed → EndR
            // [21] PB → BeginR(LOAD) → BindPipeline(transparent) →
            //      BindDescriptorSets → SetViewport → SetScissor → Push →
            //      DrawIndirect(count=1) → EndR
            // [22] BindPipeline(opaque3D) → BindDescriptorSets → SetViewport →
            //      SetScissor → BindIndexBuffer → Push(A) → DrawIndexed →
            //      Push(B) → DrawIndexed
            // [23] BindPipeline(pipe6) → BindDescriptorSets → SetViewport →
            //      SetScissor → BindIndexBuffer → SetStencilReference(0) → Push(A) →
            //      DrawIndexed → SetStencilReference(1) → Push(B) → DrawIndexed
            //
            // 共享 A scope 的 BeginR/EndR 不在每个 case 里重复：它在 [00] 前，
            // [23] 后；B 组每个 case 自己带 scope。这样每个 {} 就是一段可抽取的
            // C API 片段，同时保留“状态在 begin/end 之间是否粘滞”的真实边界。
            // ════════════════════════════════════════════════════════════════
            constexpr std::uint32_t kShapeStride = sizeof(shader_data::Shape);
            constexpr std::uint32_t kFillStride = sizeof(shader_data::PathFill);
            constexpr std::uint32_t kStrokeStride = sizeof(shader_data::PathStroke);
            constexpr std::uint32_t kNoiseStride = sizeof(shader_data::NoiseQuad);

            constexpr std::uint32_t kShapeBase = 0;
            constexpr std::uint32_t kFillBase = 64u * kShapeStride;
            constexpr std::uint32_t kStrokeBase = kFillBase + 32u * kFillStride;
            constexpr std::uint32_t kNoiseBase = kStrokeBase + 8u * kStrokeStride;

            std::uint32_t shapeCursor = 0;
            std::uint32_t fillCursor = 0;
            std::uint32_t strokeCursor = 0;
            std::uint32_t noiseCursor = 0;
            std::uint32_t constantCursor = 0;
            std::uint32_t indirectCursor = 0;

            const auto add_shape = [&](shader_data::Shape s) -> std::uint32_t {
                const std::uint32_t off = kShapeBase + shapeCursor * kShapeStride;
                s.hover_fn = 0xFFFFFFFFu;
                sb.globalHeapBuffer.write(off, &s, sizeof(s));
                ++shapeCursor;
                return off;
            };
            const auto add_fill = [&](shader_data::PathFill f) -> std::uint32_t {
                const std::uint32_t off = kFillBase + fillCursor * kFillStride;
                f.hover_fn = 0xFFFFFFFFu;
                sb.globalHeapBuffer.write(off, &f, sizeof(f));
                ++fillCursor;
                return off;
            };
            const auto add_stroke = [&](shader_data::PathStroke s) -> std::uint32_t {
                const std::uint32_t off = kStrokeBase + strokeCursor * kStrokeStride;
                s.hover_fn = 0xFFFFFFFFu;
                sb.globalHeapBuffer.write(off, &s, sizeof(s));
                ++strokeCursor;
                return off;
            };
            const auto add_noise = [&](shader_data::NoiseQuad n) -> std::uint32_t {
                const std::uint32_t off = kNoiseBase + noiseCursor * kNoiseStride;
                n.hover_fn = 0xFFFFFFFFu;
                sb.globalHeapBuffer.write(off, &n, sizeof(n));
                ++noiseCursor;
                return off;
            };
            const auto add_const = [&](std::uint32_t program,
                                       std::uint32_t heapOffset) -> std::uint32_t {
                const std::uint32_t off =
                    constantCursor * sizeof(shader_data::CommandConstant);
                const shader_data::CommandConstant cc{.program = program,
                                                      .address_offset = heapOffset};
                sb.commandConstantsBuffer.write(off, &cc, sizeof(cc));
                ++constantCursor;
                return off;
            };
            const auto add_indirect_indexed =
                [&](const VkDrawIndexedIndirectCommand &cmd) -> VkDeviceSize {
                const VkDeviceSize off = static_cast<VkDeviceSize>(indirectCursor) *
                                         sizeof(VkDrawIndexedIndirectCommand);
                sb.indirectDrawBuffer.write(off, &cmd, sizeof(cmd));
                ++indirectCursor;
                return off;
            };
            const auto add_indirect =
                [&](const VkDrawIndirectCommand &cmd) -> VkDeviceSize {
                const VkDeviceSize off = static_cast<VkDeviceSize>(indirectCursor) *
                                         sizeof(VkDrawIndirectCommand);
                sb.indirectDrawBuffer.write(off, &cmd, sizeof(cmd));
                ++indirectCursor;
                return off;
            };

            shader_data::PushData base_pc{};
            base_pc.vertexAddress = sb.globalVertexBuffer.address;
            base_pc.instanceAddress = sb.globalHeapBuffer.address;
            base_pc.commandConstantsAddress = sb.commandConstantsBuffer.address;
            base_pc.cameraIndex = 0;
            base_pc.renderVersion = 0;

            const auto pc_at = [&](std::uint32_t constOffset,
                                   std::uint32_t renderVersion) {
                shader_data::PushData p = base_pc;
                p.commandConstantsAddress =
                    sb.commandConstantsBuffer.address + constOffset;
                p.renderVersion = renderVersion;
                return p;
            };

            // 顶点/索引也是原地造。四份几何：
            //   0        ：quadVerts[0..3]
            //   4        ：同一 quad 的副本，给 vertexOffset 用
            //   8        ：non-indexed 的 6 顶点展开
            const std::array<Vertex, 4> quadVerts = {
                Vertex{{-0.5F, -0.5F, 0.0F}, {0.0F, 0.0F}},
                Vertex{{0.5F, -0.5F, 0.0F}, {1.0F, 0.0F}},
                Vertex{{0.5F, 0.5F, 0.0F}, {1.0F, 1.0F}},
                Vertex{{-0.5F, 0.5F, 0.0F}, {0.0F, 1.0F}}};
            const std::array<Vertex, 6> nonIndexedVerts = {quadVerts[0], quadVerts[1],
                                                           quadVerts[2], quadVerts[0],
                                                           quadVerts[2], quadVerts[3]};
            constexpr std::uint32_t kNonIndexedVertex = 8;

            const std::array<std::uint32_t, 6> quadIdx = {0, 1, 2, 0, 2, 3};
            const std::array<std::uint32_t, 3> triIdx = {0, 1, 2};
            const std::array<std::uint16_t, 6> idx16 = {0, 1, 2, 0, 2, 3};
            constexpr VkDeviceSize kTriIdxOffset = sizeof(quadIdx);
            constexpr VkDeviceSize kIndex16Offset = 3u * sizeof(quadIdx);

            sb.globalVertexBuffer.write(0, quadVerts.data(), sizeof(quadVerts));
            sb.globalVertexBuffer.write(sizeof(quadVerts), quadVerts.data(),
                                        sizeof(quadVerts));
            sb.globalVertexBuffer.write(2u * sizeof(quadVerts), nonIndexedVerts.data(),
                                        sizeof(nonIndexedVerts));
            /*
            NOTE: globalVertexBuffer 最终如下 = {
                // ── [0..3] indexed 原始：quad 的唯一顶点 ──
                Vertex{{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f}},  // 0 q0
                Vertex{{ 0.5f, -0.5f, 0.0f}, {1.0f, 0.0f}},  // 1 q1
                Vertex{{ 0.5f,  0.5f, 0.0f}, {1.0f, 1.0f}},  // 2 q2
                Vertex{{-0.5f,  0.5f, 0.0f}, {0.0f, 1.0f}},  // 3 q3
                // ── [4..7] quad 副本，给 vertexOffset=4 用（case [11]）──
                Vertex{{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f}},  // 4 q0
                Vertex{{ 0.5f, -0.5f, 0.0f}, {1.0f, 0.0f}},  // 5 q1
                Vertex{{ 0.5f,  0.5f, 0.0f}, {1.0f, 1.0f}},  // 6 q2
                Vertex{{-0.5f,  0.5f, 0.0f}, {0.0f, 1.0f}},  // 7 q3
                // ── [8..13] non-indexed 展开：[0,1,2,0,2,3]，给后面的 non-indexed 用 ──
                Vertex{{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f}},  // 8  = q0
                Vertex{{ 0.5f, -0.5f, 0.0f}, {1.0f, 0.0f}},  // 9  = q1
                Vertex{{ 0.5f,  0.5f, 0.0f}, {1.0f, 1.0f}},  // 10 = q2
                Vertex{{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f}},  // 11 = q0
                Vertex{{ 0.5f,  0.5f, 0.0f}, {1.0f, 1.0f}},  // 12 = q2
                Vertex{{-0.5f,  0.5f, 0.0f}, {0.0f, 1.0f}},  // 13 = q3
            };
            */

            /*
            constexpr void write(std::size_t offset, const void *src,
                                std::size_t size) noexcept
            {
                assert(offset + size <= capacity); // 不需要 vkFlushMappedMemoryRanges：HOST_COHERENT 保证自动可见
                std::memcpy(static_cast<char *>(buffer.mapPtr()) + offset, src, size);
            }
            */
            sb.globalIndexBuffer.write(0, quadIdx.data(),
                                       sizeof(quadIdx)); // 偏移 0，24 字节
            sb.globalIndexBuffer.write(kTriIdxOffset, triIdx.data(),
                                       sizeof(triIdx)); // 偏移 24，12 字节
            sb.globalIndexBuffer.write(kIndex16Offset, idx16.data(),
                                       sizeof(idx16)); // 偏移 72，12 字节

            // UBO 两边都写：descriptor set 切换 case 会用到另一帧的 set。
            // cameraInfo[1] 故意带一点位移，给“partial push cameraIndex”case 看出差别。
            UniformBufferObject ubo{};
            ubo.cameraInfo[0].view = glm::mat4(1.0F);
            ubo.cameraInfo[0].proj = glm::mat4(1.0F);
            ubo.cameraInfo[1] = ubo.cameraInfo[0];
            ubo.cameraInfo[1].view =
                glm::translate(glm::mat4(1.0F), glm::vec3(0.0F, 0.08F, 0.0F));
            for (auto &ub : uniformBuffers)
                std::memcpy(ub.mapPtr(), &ubo, sizeof(ubo));

            const bool hasStencil = mainCtx.depthResourcesBuild.hasStencilComponent();
            const VkImageAspectFlags depthAspect =
                hasStencil ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
                           : VK_IMAGE_ASPECT_DEPTH_BIT;

            // ── 共享 layout transition：全部从 UNDEFINED 进入 attachment layout ──
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
                    .image = pickingImage,
                    .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .src = {.layout = VK_IMAGE_LAYOUT_UNDEFINED,
                            .access_mask = VK_ACCESS_2_NONE,
                            .stage_mask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT},
                    .dst = {.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            .access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            .stage_mask =
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT}},
                my_render::image_info{
                    .image = resolveImage,
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
                                          VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT}});

            VkRenderingAttachmentInfo color0_clear{
                .sType = sType<VkRenderingAttachmentInfo>(),
                .imageView = mainCtx.msaaResource.imageView(),
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT,
                .resolveImageView = swapchain.imageView(imageIndex),
                .resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = {.color = {.float32 = {0.05F, 0.06F, 0.08F, 1.0F}}}};
            VkRenderingAttachmentInfo color1_clear{
                .sType = sType<VkRenderingAttachmentInfo>(),
                .imageView = pickCtx.pickResource.imageView(),
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT,
                .resolveImageView = pickCtx.resolveResource.imageView(),
                .resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = {.color = {.uint32 = {0xFFFFFFFFu, 0xFFFFFFFFu, 0u, 0u}}}};
            VkRenderingAttachmentInfo depth_clear{
                .sType = sType<VkRenderingAttachmentInfo>(),
                .imageView = mainCtx.depthResource.imageView(),
                .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = {.depthStencil = {.depth = 1.0F, .stencil = 0u}}};

            const std::array attachments_clear = {color0_clear, color1_clear};
            VkRenderingAttachmentInfo color0_load = color0_clear;
            color0_load.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            VkRenderingAttachmentInfo color1_load = color1_clear;
            color1_load.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            VkRenderingAttachmentInfo depth_load = depth_clear;
            depth_load.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            const std::array attachments_load = {color0_load, color1_load};

            VkRenderingInfo info_clear{
                .sType = sType<VkRenderingInfo>(),
                .renderArea = {.offset = {.x = 0, .y = 0}, .extent = extent},
                .layerCount = 1,
                .viewMask = 0,
                .colorAttachmentCount =
                    static_cast<std::uint32_t>(attachments_clear.size()),
                .pColorAttachments = attachments_clear.data(),
                .pDepthAttachment = &depth_clear,
                .pStencilAttachment = hasStencil ? &depth_clear : nullptr};
            VkRenderingInfo info_load = info_clear;
            info_load.colorAttachmentCount =
                static_cast<std::uint32_t>(attachments_load.size());
            info_load.pColorAttachments = attachments_load.data();
            info_load.pDepthAttachment = &depth_load;
            info_load.pStencilAttachment = hasStencil ? &depth_load : nullptr;
            VkRenderingInfo info_suspend = info_load;
            info_suspend.flags = VK_RENDERING_SUSPENDING_BIT;
            VkRenderingInfo info_resume = info_load;
            info_resume.flags = VK_RENDERING_RESUMING_BIT;

            // 共享 memory barrier：只在 scope 之间建立 RAW/WAW；没有 layout 变化。
            // 同一个 VkDependencyInfo 也会被 case [14] 在 scope 内复用。
            VkMemoryBarrier2 scope_mb{
                .sType = sType<VkMemoryBarrier2>(),
                .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
                                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                                 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
                                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                                 VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                                 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
            VkDependencyInfo scope_dep{.sType = sType<VkDependencyInfo>(),
                                       .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
                                       .memoryBarrierCount = 1,
                                       .pMemoryBarriers = &scope_mb};

            constexpr std::uint32_t kCols = 6;
            constexpr std::uint32_t kRows = 4;
            // 语料安全区：这台机器的 GLFW client 是 1024×640，swapchain 仍是
            // 1280×800；把所有 tile 放在左上 1024×640，保证 24 个 case 同屏可见。
            // 不是工程布局，只是让素材在 DPI 缩放下不被裁掉。
            const std::uint32_t gridW = std::min<std::uint32_t>(extent.width, 1024u);
            const std::uint32_t gridH = std::min<std::uint32_t>(extent.height, 640u);
            const std::uint32_t tw = gridW / kCols;
            const std::uint32_t th = gridH / kRows;
            const auto tile = [&](std::uint32_t i) -> VkRect2D {
                return {{static_cast<std::int32_t>((i % kCols) * tw),
                         static_cast<std::int32_t>((i / kCols) * th)},
                        {tw, th}};
            };
            const auto inset = [](const VkRect2D &r, float m) -> ui_dao::rect_px {
                return {static_cast<float>(r.offset.x) + m,
                        static_cast<float>(r.offset.y) + m,
                        static_cast<float>(r.extent.width) - 2.0F * m,
                        static_cast<float>(r.extent.height) - 2.0F * m};
            };
            const auto half = [](const VkRect2D &r, bool right) -> ui_dao::rect_px {
                const float m = 12.0F;
                const float w = (static_cast<float>(r.extent.width) - 3.0F * m) * 0.5F;
                const float x =
                    static_cast<float>(r.offset.x) + m + (right ? w + m : 0.0F);
                return {x, static_cast<float>(r.offset.y) + 32.0F, w,
                        static_cast<float>(r.extent.height) - 64.0F};
            };

            // NOTE: A 组 证明 一个 [beginRendering,endRendering] 可以做下面很多事情。16个例子
            // ════════════════════════════════════════════════════════════════
            // A 组：cases [00]..[16], [22], [23] 共用一个 render scope。
            //       begin 在这里，end 在 [23] 之后。
            //       这样每个 case 的 {} 只展示“该 case 相对共享设置多出来的 C API”，
            //       不会把 begin/end 重复 20 遍；需要独立 scope 的 case 在 B 组。
            // ════════════════════════════════════════════════════════════════
            commandBuffer.beginRendering(info_clear);
            // 这里所有 Shape 的 model 都是按整张 1280×800 的 NDC 算的；
            // 因此常规 viewport 必须保持整屏，靠 scissor 把绘制裁到 tile。
            // 若把 viewport 也设成 tile，NDC 会被二次缩放，形状会缩成 tile 的小角落。
            const VkViewport full_vp{0.0F, 0.0F, W, H, 0.0F, 1.0F};

            // ────────────────────────────────────────────────────────────────
            // [00] 基线：一个 scope / 一条 pipeline / 一个 set / 一条 indexed draw
            // C API: BeginRendering(共享) → BindPipeline → BindDescriptorSets →
            //        SetViewport → SetScissor → BindIndexBuffer →
            //        PushConstants → DrawIndexed(6,1) → EndRendering(共享)
            // 排列组合：所有状态都只出现一次，单实例。
            // 分离：不需要；这是后面每个 case 的对照。
            // NOTE: [comments/ability3-0.png]
            // 大背景灰	quad 外（tile 内、inset 24px 外）	不执行（光栅化外）	保持 clear color (0.05, 0.06, 0.08) // #3F4550
            // sdf外部默认 vec4 result = vec4(0.0);
            // ────────────────────────────────────────────────────────────────
            {
                const VkRect2D area = tile(0);
                const VkViewport vp = full_vp;
                auto s = ui_dao::sdf_shape(inset(area, 24.0F), view, 18.0F,
                                           glm::vec4(0.95F, 0.45F, 0.15F, 1.0F));
                const std::uint32_t heap = add_shape(s);
                const std::uint32_t cst = add_const(0u, heap);
                const shader_data::PushData pc = pc_at(cst, 0u);

                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *mainCtx.pipelineOpaqueUI);
                commandBuffer.bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                 *pipelineLayout, 0, 1, &descriptorSet, 0,
                                                 nullptr);
                commandBuffer.setViewport(0, std::span{&vp, 1});
                commandBuffer.setScissor(0, std::span{&area, 1});

                // NOTE: 推送常量 + BDA 传递了 顶点基地址
                // NOTE: 顶点通过BDA: base_pc.vertexAddress = sb.globalVertexBuffer.address;
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(pc), &pc);

                // NOTE: 不用 BDA 而用 bindIndexBuffer
                // NOTE: sb.globalIndexBuffer.write 上面初始化过来
                commandBuffer.bindIndexBuffer(sb.globalIndexBuffer.buffer.buffer(), 0,
                                              VK_INDEX_TYPE_UINT32);
                //                               └─ VkBuffer handle ─┘           offset=0  type=UINT32

                /*
                隐变量	            值	        来源
                gl_DrawIDARB	    0	        直接 draw
                gl_InstanceIndex	0	        firstInstance=0, count=1
                gl_BaseVertexARB	0	        vertexOffset=0
                gl_VertexIndex	    [0,1,2,0,2,3]	index[firstIndex+i] + vertexOffset
                gl_PrimitiveID =    0（三角形 1）/ 1（三角形 2） //   gl_PrimitiveID  —— vertex 阶段不可读，仅 fragment 可用
                */
                //constexpr void drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                //    uint32_t firstIndex, int32_t vertexOffset,
                //    uint32_t firstInstance) const noexcept
                commandBuffer.drawIndexed(6, 1, 0, 0, 0);
                //                        │  │  │  │  │
                //                        │  │  │  │  └─ firstInstance = 0
                //                        │  │  │  └──── vertexOffset  = 0
                //                        │  │  └─────── firstIndex    = 0
                //                        │  └────────── instanceCount = 1
                //                        └───────────── indexCount    = 6

                // GPU 展开（Vulkan 规范）：
                // gl_VertexIndex   = indexBuffer[firstIndex + i] + vertexOffset
                //                  = quadIdx[0 + i] + 0
                //                  = [0, 1, 2, 0, 2, 3][0 + i] + 0

                /*
                // 顶点着色器：
                //NOTE: 全局输出部分： 
                layout(location = 0) out vec4 fragColor;//NOTE:没有flat ，自动生成插值数据
                layout(location = 1) out vec2 fragTexCoord;
                layout(location = 2) flat out uint program; // 硬要求：插值会让 switch 失效
                layout(location = 3) flat out uint64_t instancePtr;// 硬要求：插值 = 垃圾地址 → 崩
                layout(location = 4) out vec2 localPos;
                layout(location = 5) flat out uint instanceId;

                // NOTE: main 内部：
                void main()
                {
                    fragColor = vec4(1.0);// NOTE: 顶点色输出的默认值（white），case 内会被实例 colors 覆盖
                    fragTexCoord = vec2(0.0);
                    mat4 model = mat4(1.0);
                    vec3 pos = vec3(0.0);
                    localPos = vec2(0.0);

                    CommandConstBuffer cmdConsts = CommandConstBuffer(pc.commandConstantsAddress);
                    CommandConstant cc = cmdConsts.consts[gl_DrawIDARB];//NOTE: 取出这个命令关联的偏移量+类型
                    program = cc.program;
                    instanceId = gl_InstanceIndex;// NOTE: 实例索引（0..instanceCount-1），不是 index buffer 的索引
                    uint64_t heapBaseStart = pc.dataAddress + cc.address_offset;//NOTE: 解析出本次多实例的起始地址
                    CameraInfo cam = ubo.cameraInfo[pc.cameraIndex];

                    VertexBuffer vertBuf = VertexBuffer(pc.vertexAddress);
                    VertexData v = vertBuf.vertices[gl_VertexIndex];// NOTE: 取出顶点数据

                    switch (program)
                    {
                    case PROGRAM_SHAPE: {
                        instancePtr = heapBaseStart + gl_InstanceIndex * SHAPE_SIZE;// NOTE: 本实例行绝对地址
                        Shape inst = ShapeBuffer(instancePtr).shapes[0];
                        model = inst.model;
                        // NOTE: 永远等于 index[firstIndex + i]，不管 firstIndex/vertexOffset 是多少
                        // gl_VertexIndex    = indexBuffer[firstIndex + i] + vertexOffset      ← 规范
                        // gl_BaseVertexARB  = vertexOffset                                    ← 规范
                        // ─────────────────────────────────────────────────────────────────
                        // 相减：localVertexIndex = indexBuffer[firstIndex + i]                ← 与 vertexOffset 无关
                        uint localVertexIndex = gl_VertexIndex - gl_BaseVertexARB;
                        fragColor = inst.colors[localVertexIndex];// NOTE: 取出当前顶点的颜色。保证安全

                        bool hasShadow = (inst.effects & FX_SHADOW) != 0u;
                        vec2 expand = vec2(1.0);
                        if (hasShadow)
                        {
                            vec2 cardHalf = max(inst.px_size * 0.5, vec2(1.0));
                            float blur = max(inst.blur_px, 0.5);
                            vec2 shadowHalf = cardHalf * max(inst.spread, 0.0);
                            vec2 offset = inst.shadow_offset_px;
                            vec2 margin = max(shadowHalf + blur + abs(offset) - cardHalf, vec2(0.0));
                            margin += vec2(0.5); // AA
                            expand = (cardHalf + margin) / cardHalf;
                        }
                        localPos = v.pos.xy * expand;//NOTE: 本地位置
                        pos.xy = v.pos.xy * expand;// NOTE: 模型空间位置（待 model / view / proj 变换）
                    }
                    ...
                    }
                    mat4 mvp = cam.proj * cam.view * model;
                    gl_Position = mvp * vec4(pos, 1.0);
                }
                */

                /*
                // NOTE: 片段着色器。 SDF 绘制。直接使用顶点传递过来的即可，几乎没什么好说的。SDF外部和内部的颜色都能定义
                // ── 机制 0：Shape（SDF；fill / border / shadow 参数）────────────────
                case PROGRAM_SHAPE: {
                    Shape inst = ShapeBuffer(instancePtr).shapes[0];
                    vec2 pPx = (localPos + 0.5) * inst.px_size;
                    outColor = drawShape(inst, pPx, fragColor);
                    outPicking = uvec4(program, inst.entity_index, packed_z, inst.hover_fn);
                }
                */
            }

            // ────────────────────────────────────────────────────────────────
            // [01] 多实例：同一状态，instanceCount 1→4
            // C API: SetViewport → SetScissor → PushConstants →
            //        DrawIndexed(6,4,0,0,0)
            //        （pipeline/set/index 沿用 [00]；begin/end 是 A 组共享的）
            // 位置：tile(1) 顶行第二格；代码标记 const VkRect2D area = tile(1);
            //       和 for (i < 4) 的 2×2 排布。
            //
            // CPU 侧（数据怎么排）：
            //   add_shape() 连续写 4 行，每行 248 B：
            //     heap + 0*248 = Shape0
            //     heap + 1*248 = Shape1
            //     heap + 2*248 = Shape2
            //     heap + 3*248 = Shape3
            //   add_const(0u, heap) 只写 1 条
            //   CommandConstant{program=0, address_offset=heap}；
            //   pc_at(cst,1u) 把 commandConstantsAddress 指到这条常量，
            //   renderVersion=1 只影响拾取打包，不影响几何。
            //   注意 heap 是“首行字节偏移”，不是每实例一个常量；4 行共用同一条命令。
            //
            // 与 [00] 的差别：
            //   [00] instanceCount=1 → gl_InstanceIndex 只有 0，instancePtr 永远落在 Shape0；
            //   [01] instanceCount=4 → gl_InstanceIndex=0..3，instancePtr 按 +i*248 走完 4 行。
            //   pipeline / descriptor set / index buffer 都没变（沿用 [00]），所以这四份
            //   几何的差异只来自“实例行”，不是来自额外的 bind/push。
            //
            // NOTE: Draw 参数（只标变化）：
            //   drawIndexed(6, 4, 0, 0, 0);
            //                │  └ instanceCount 1→4
            //                └ indexCount 仍是 6
            //   firstIndex=0、vertexOffset=0、firstInstance=0 都没变。
            //
            // 隐变量 / 偏移：
            //   gl_DrawIDARB = 0                 仍然只有一条 CommandConstant，不按实例变。
            //   gl_InstanceIndex = 0,1,2,3       = firstInstance(0) + 实例序号；这是唯一
            //                                    随实例变的量；若 firstInstance≠0 就从
            //                                    该值起算，本 case 仍是 0。
            //   gl_BaseVertexARB = 0             vertexOffset=0。
            //   gl_VertexIndex = [0,1,2,0,2,3]   每个实例都一样（同一份 index buffer）。
            //   gl_PrimitiveID = 0/1, 0/1, ...   每个实例都从 0 重新数（规范：PrimitiveId
            //                                    reset between each instance），所以它不是
            //                                    跨 4 个实例唯一的 id；如果要逐实例拾取，
            //                                    拾取键还得带 entity_index/instanceId
            //                                    （本语料没做）。
            //
            // 着色器端（回顾 [00] 的 main）：
            //     cc = consts[gl_DrawIDARB];                    // 仍是 Shape 的那条常量
            //     heapBaseStart = pc.dataAddress + cc.address_offset; // = base + heap
            //     instancePtr = heapBaseStart + gl_InstanceIndex * SHAPE_SIZE; // + i*248
            //     Shape inst = ShapeBuffer(instancePtr).shapes[0];
            //   于是 model/colors/px_size 都从第 i 行取；4 个矩形的位置和颜色由
            //   CPU 的 i%2 / i/2 以及每行 model 决定，shader 只是按实例行读。
            //
            // ── 合并能力（这套判据后面 case 直接复用，不再每条重写）──────────
            // 一条 draw 能“吞掉”前面若干条 draw 的前提：
            //   1. pipeline/layout/descriptor set 相同：fixed-function（blend/depth/
            //      stencil/topology/rasterization）只能有一套；pipeline 是 batch key。
            //      set 不同时，除非把数据统一到同一个 set，否则也不能合并。
            //   2. dynamic state 相同：viewport/scissor/stencil ref/blend constants
            //      等一条 draw 只有一份；不同就必须先统一。
            //   3. index/vertex 输入相同：同一个 index buffer + offset + index type，
            //      同一组 vertex buffer；Draw 和 DrawIndexed 是两条路径，不能直接合并。
            //   4. draw 参数可表达：indexCount/firstIndex/vertexOffset 必须相同；
            //      只有 instance 范围/数据可以变。若这些也变，走 DrawIndexedIndirect
            //      （同一 pipeline/state，多条 VkDrawIndexedIndirectCommand）。
            //      => 同 state、同这三个参数的相邻 drawIndexed 可以合成一条
            //         instanced drawIndexed；不同 indexCount/firstIndex/vertexOffset
            //         不能合成一条 drawIndexed，只能分开 draw 或走 indirect。
            //   5. 实例数据可寻址：每个逻辑 draw 的数据必须能由
            //      base + (firstInstance + i) * stride 找到，行连续、struct 相同。
            //      我们的协议就是 address_offset + gl_InstanceIndex * SHAPE_SIZE。
            //   6. CommandConstant 相同：直接 instanced draw 的 gl_DrawIDARB 恒为 0，
            //      所有实例共用一条常量；要按 draw 换 program/address_offset，
            //      只能走 indirect（gl_DrawID）或把 program 搬进实例数据。
            //   7. 顺序合法：合并后的 instance 顺序必须等于原 draw 顺序。
            //      Vulkan primitive order = submission > instance > vertex/index；
            //      painter's order（透明/叠加）不能借合并重排；不透明可另说。
            //   8. gl_PrimitiveID 每个 instance 从 0 重置；不能把它当跨实例唯一键。
            //
            // 具体到 [00]+[01]：
            // NOTE:  [01] 内部 4 个实例**已经是合并后的形态**，不是 4 条 draw。
            //   [00] 与 [01] 在当前语料**不能直接合并**：
            //     - viewport/scissor 不同：tile(0) vs tile(1)，dynamic state 不统一；
            //     - push 的 CommandConstant 不同：cst0 vs cst1，renderVersion 0 vs 1；
            //     - 若硬合并，需要一条 draw 同时覆盖两个 tile，scissor 必须变大，
            //       这会破坏“一格一个 case”的排版。
            //   如果只做数据实验，理论上可以这样合并：
            //     - viewport 用整屏，scissor 用覆盖 tile0+tile1 的大矩形；
            //     - Shape0..Shape4 在 heap 里连续（本语料 [00] 写 1 行、[01] 写 4 行，
            //       中间没有别的 Shape，正好连续）；
            //     - 只保留一个 CommandConstant{program=0,address_offset=Shape0}，
            //       以及一个 push（renderVersion 统一；若拾取要区分版本，把它搬进实例行）；
            //     - 一条 drawIndexed(6,5,0,0,0)：instance 0 = [00]，1..4 = [01]，
            //       每个实例的 model 已经写好各自的 tile 位置。
            //   所以“能不能合并”不是 shader 能不能画，而是**状态 + 数据基址 + 顺序**
            //   三件事同时成立；当前测试只排 API，暂时故意不合并。
            // ────────────────────────────────────────────────────────────────
            {
                const VkRect2D area = tile(1);
                const VkViewport vp = full_vp;
                std::uint32_t heap = 0;
                for (std::uint32_t i = 0; i < 4; ++i)
                {
                    const float x = static_cast<float>(area.offset.x) + 10.0F +
                                    static_cast<float>(i % 2) * 78.0F;
                    const float y = static_cast<float>(area.offset.y) + 18.0F +
                                    static_cast<float>(i / 2) * 64.0F;
                    auto s = ui_dao::sdf_shape(
                        ui_dao::rect_px{x, y, 68.0F, 54.0F}, view, 12.0F,
                        glm::vec4(0.95F - 0.12F * static_cast<float>(i),
                                  0.35F + 0.12F * static_cast<float>(i), 0.20F, 1.0F));
                    const std::uint32_t off = add_shape(s);
                    if (i == 0)
                        heap = off;
                }
                const std::uint32_t cst = add_const(0u, heap);
                const shader_data::PushData pc = pc_at(cst, 1u);
                commandBuffer.setViewport(0, std::span{&vp, 1});
                commandBuffer.setScissor(0, std::span{&area, 1});
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(pc), &pc);

                // NOTE: instanceCount = 4 这是多实例绘制。一个命令draw执行 4次。顶点着色器程序
                commandBuffer.drawIndexed(6, 4, 0, 0, 0);
                //                        │  │  │  │  │
                //                        │  │  │  │  └─ firstInstance = 0
                //                        │  │  │  └──── vertexOffset  = 0
                //                        │  │  └─────── firstIndex    = 0
                //                        │  └────────── instanceCount = 4
                //                        └───────────── indexCount    = 6
            }

            // ────────────────────────────────────────────────────────────────
            // [02] 同状态，两条 draw，只改 firstInstance：0 → 1
            // C API: SetViewport → SetScissor → PushConstants(一次) →
            //        DrawIndexed(6,1,0,0,0) → DrawIndexed(6,1,0,0,1)
            //        （pipeline/set/index 沿用 [00]/[01]；begin/end 是 A 组共享的）
            // 位置：tile(2)，顶行第三格；左/右两个 Shape（half(area,false/true)）。
            //
            // 和 [01] 的对照：
            //   [01] 一条 draw，instanceCount=4，gl_InstanceIndex=0..3；
            //   [02] 两条 draw，各 instanceCount=1，gl_InstanceIndex=0 / 1。
            //   两条 draw 的 pipeline/set/viewport/scissor/index/push 全同，
            //   真正的差异只有 draw 的 firstInstance；这是“同一个状态、同一份
            //   CommandConstant，靠 draw 参数切实例行”的最小例子。
            //
            // 数据/偏移（这里的 h0 不是 0）：
            //   前面 [00] 1 行 + [01] 4 行已经占了 5*248；
            //   [02] 再连续 add_shape 两行：
            //     h0 = 5*248 = 1240（当前顺序下；前面 [00]/[01] 已写 5 行）
            //     h1 = h0 + 248 = 1488（没有自己的 CommandConstant）
            //   add_const(0u,h0) 只写一条 CommandConstant{program=0,address_offset=h0}；
            //   pc_at(cst,2u) 把它指给本次 draw，renderVersion=2 只进拾取。
            //   => cst 也不是 0；当前顺序下它是第 3 条常量（16 B）。
            //   => 与 [01] 的 cst/pc 数值都变了（cst 8→16、heap 248→1240、
            //      renderVersion 1→2），但结构没变，都是“本 case 自己造的数据”。
            //
            // 隐变量（两条 draw 各自重置）：
            //   gl_DrawIDARB = 0            两条都是直接 draw，没有换 CommandConstant。
            //   gl_InstanceIndex = 0 / 1    分别来自 firstInstance=0 / 1。
            //   gl_BaseVertexARB = 0        vertexOffset=0，两条都一样。
            //   gl_VertexIndex = [0,1,2,0,2,3]  两条都一样（同一份 index buffer）。
            //   gl_PrimitiveID = 0/1        每条 draw 都从 0 重置，不是跨 draw 的唯一 id。
            //
            // 合并能力（引用 [01] 的判据）：
            //   这两条 draw 完全满足合并条件：同 pipeline/set/viewport/scissor/index，
            //   同 indexCount/firstIndex/vertexOffset，h0/h1 连续，且共用同一
            //   CommandConstant。因此可以合成一条
            //     drawIndexed(6,2,0,0,0)
            //   gl_InstanceIndex=0/1 分别对应 h0/h1；这就是 [01] 的 2 实例版。
            //   注意：合并后不能再按 draw 换 pc/cst；本 case 恰好不需要换。
            //   与 [00]/[01] 合并仍不成立：viewport/scissor 是 tile(2)，cst/renderVersion
            //   也不同；要合并先统一这些状态（详见 [01] 的 [00]+[01] 段）。
            // ────────────────────────────────────────────────────────────────
            {
                const VkRect2D area = tile(2);
                const VkViewport vp = full_vp;
                // NOTE： half 分割了 area的空间。CPU分割的。两个SDF的原始空间等分相同
                const std::uint32_t h0 =
                    add_shape(ui_dao::sdf_shape(half(area, false), view, 16.0F,
                                                glm::vec4(0.25F, 0.85F, 0.95F, 1.0F)));
                const std::uint32_t h1 = add_shape(ui_dao::sdf_shape(
                    half(area, true), view, 16.0F, glm::vec4(0.95F, 0.75F, 0.25F, 1.0F)));
                // NOTE: h0/h1 是连续两行；h0 不是 0（当前顺序下 1240，h1=1488）。
                //       cst 是第 3 条 CommandConstant（16 B），不是复用 [01] 的 cst；
                //       这里只写一条常量，两条 draw 共用。
                //       pc.instanceAddress 仍是 heapBase；真正指向 h0 的是
                //       CommandConstant.address_offset=h0。
                // NOTE:注意内部 ++constantCursor;。也就是add_const总是返回新的命令常量起始地址。其他add_XXX也类似，没有重复使用的
                const std::uint32_t cst = add_const(0u, h0);
                const shader_data::PushData pc = pc_at(cst, 2u);
                commandBuffer.setViewport(0, std::span{&vp, 1});
                commandBuffer.setScissor(0, std::span{&area, 1});
                // NOTE: push 只发一次；下面两条 draw 共用 pc/cst，不换 CommandConstant。
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(pc), &pc);

                // NOTE: 两条 draw 只有 firstInstance 变；gl_InstanceIndex 分别为 0/1，
                //       于是 instancePtr 落到 h0/h1。pipeline/set/viewport/scissor/
                //       index/push 全部不变。
                commandBuffer.drawIndexed(6, 1, 0, 0, 0);
                //                        │  │  │  │  └─ firstInstance = 0
                //                        │  │  │  └──── vertexOffset  = 0
                //                        │  │  └─────── firstIndex    = 0
                //                        │  └────────── instanceCount = 1
                //                        └───────────── indexCount    = 6
                //        gl_InstanceIndex=0 → instancePtr = h0 + 0*248 = h0
                commandBuffer.drawIndexed(6, 1, 0, 0, 1);
                //                        │  │  │  │  └─ firstInstance = 1
                //                        │  │  │  └──── vertexOffset  = 0
                //                        │  │  └─────── firstIndex    = 0
                //                        │  └────────── instanceCount = 1
                //                        └───────────── indexCount    = 6
                //        gl_InstanceIndex=1 → instancePtr = h0 + 1*248 = h1

                // NOTE: 两个draw 只有 draw的参数变化,我们的vulkan 提供了 drawIndexedIndirect 就是专门合批这样的
            }

            // ────────────────────────────────────────────────────────────────
            // [03] 同状态，两条 draw，各推一次 push constants
            // C API: SetViewport → SetScissor →
            //        PushConstants(pc0) → DrawIndexed(6,1,0,0,0) →
            //        PushConstants(pc1) → DrawIndexed(6,1,0,0,0)
            //        （pipeline/set/index 沿用 [00]..[02]；begin/end A 组共享）
            // 位置：tile(3) 顶行第四格；half(area,false/true) 在 CPU 上把 tile 等分。
            //
            // NOTE: 和 [02] 的对照（最关键）：
            //   [02] 两条 draw 只改 firstInstance，push 只发一次；
            //   [03] 两条 draw 的 draw 参数完全相同，改的是 push 里的 CommandConstant。
            //   => [02]：同一条命令，换实例行；[03]：同一条 draw，换命令常量。
            //      这就是“per-draw push 是 draw 间硬边界”的最小例子。
            //
            // 数据/游标（[02] 已讲 add_* 每次 ++cursor，这里直接套）：
            //   当前顺序下 [00]1 + [01]4 + [02]2 = 7 行，所以
            //     h0 = 7*248 = 1736, h1 = h0 + 248 = 1984。
            //   常量同理：前面 cst0/cst1/cst2 占 0/8/16，
            //     c0 = 24, c1 = 32（c1 = c0 + 8）；两条都是新槽，不复用。
            //   pc_at(c0,3) / pc_at(c1,3) 分别指到 c0/c1；renderVersion=3 只进拾取。
            //
            // CPU 的 half（[02] 已讲“等分/SDF 本地空间”，这里只补差异）：
            //   两半都是 67×96，两个 Shape 的 px_size 相同；shader 里 localPos*px_size
            //   的本地参数空间相同，唯一不同的是 model 平移，把等大的 SDF 放到左右。
            //   所以两条 draw 的几何/着色器分支完全一样，只有 CommandConstant 不同。
            //
            // 隐变量（两条 draw 都是 0 基）：
            //   gl_DrawIDARB = 0        两条都是直接 draw；push 变了，DrawID 仍是 0。
            //   gl_InstanceIndex = 0    两条都 firstInstance=0，instanceCount=1。
            //   gl_BaseVertexARB = 0    vertexOffset=0。
            //   gl_VertexIndex = [0,1,2,0,2,3] 两条相同。
            //   gl_PrimitiveID = 0/1    每条 draw 从 0 重置。
            //   => 两条 instancePtr 都是 heapBaseStart + 0*248；让第二条落到 h1 的
            //      是 cc1.address_offset=h1，不是 gl_InstanceIndex。
            //
            // 合并能力（引用 [01] 判据）：
            //   两条 draw 的 pipeline/set/viewport/scissor/index/draw 参数全同，
            //   但 PushConstants 不同，所以不能直接合成一条 instanced drawIndexed：
            //   direct draw 的 gl_DrawIDARB 恒为 0，只有一套 push/CommandConstant。
            //   两条路：
            //     A. 改成 [02] 的 firstInstance 方案：只留
            //        CommandConstant{program=0,offset=h0}，用 firstInstance=0/1
            //        选 h0/h1，合成一条 drawIndexed(6,2,0,0,0)。
            //        这要求 h0/h1 连续、同 program —— 本 case 满足。
            //     B. 保持两条不同 CommandConstant：用一条
            //        vkCmdDrawIndexedIndirect(count=2)，push 指向 CommandConstant[]
            //        首地址；两条 VkDrawIndexedIndirectCommand 的 gl_DrawID=0/1
            //        选 c0/c1。它是“per-draw 参数不同”时 Vulkan 提供的合批入口；
            //        仍然是 2 条 indirect command，只是 1 条 C API。
            //   => [03] 不能变成 1 条 drawIndexed；能变成
            //      “1 条 drawIndexedIndirect(count=2)”或“退化后的 1 条 direct”。
            //   与 [00]..[02] 合并仍不成立：tile(3) 的 scissor、c0/c1/renderVersion
            //   都不同；跨 case 合并先统一这些（同 [01] 判据）。
            // ────────────────────────────────────────────────────────────────
            {
                const VkRect2D area = tile(3);
                const VkViewport vp = full_vp;
                // NOTE: 同 [02] 的 half：CPU 等分 tile(3)；两半都是 67×96，
                //       两个 Shape 的 px_size 相同，本地 SDF 空间相同，只有 model 平移不同。
                //       add_shape 每次 ++shapeCursor；h0/h1 连续（当前 1736/1984）。
                const std::uint32_t h0 =
                    add_shape(ui_dao::sdf_shape(half(area, false), view, 16.0F,
                                                glm::vec4(0.35F, 0.95F, 0.55F, 1.0F)));
                const std::uint32_t h1 = add_shape(ui_dao::sdf_shape(
                    half(area, true), view, 16.0F, glm::vec4(0.95F, 0.35F, 0.65F, 1.0F)));
                // NOTE: add_const 每次 ++constantCursor，c0/c1 是两条新常量
                //       （当前 24/32，c1=c0+8），不是复用同一条。
                const std::uint32_t c0 = add_const(0u, h0);
                const std::uint32_t c1 = add_const(0u, h1);
                // NOTE: pc0/pc1 只有 commandConstantsAddress 不同（c0 vs c1）；
                //       renderVersion 都是 3，instanceAddress/vertexAddress/cameraIndex 相同。
                const shader_data::PushData pc0 = pc_at(c0, 3u);
                const shader_data::PushData pc1 = pc_at(c1, 3u);
                commandBuffer.setViewport(0, std::span{&vp, 1});
                commandBuffer.setScissor(0, std::span{&area, 1});
                // NOTE: push0 指向 c0；draw 参数与 [02] 相同，firstInstance=0。
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(pc0), &pc0);
                commandBuffer.drawIndexed(6, 1, 0, 0, 0);
                // NOTE: 这里换的是 push（state），不是 draw 参数；所以第二条必须重新 push。
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(pc1), &pc1);
                commandBuffer.drawIndexed(6, 1, 0, 0, 0);
                // NOTE: 两条 draw 的 draw 参数完全一样，只是 push 不同；
                //       Vulkan 的合批入口是 vkCmdDrawIndexedIndirect(count=2)：
                //       push 一次指向 CommandConstant[2] 首地址，gl_DrawID 选 c0/c1。
            }

            // ────────────────────────────────────────────────────────────────
            // NOTE: 图像位于: [comments/ability3-1.png]
            // [04] 同 scope 换 pipeline：opaqueUI → transparentUI
            // C API: BindPipeline(opaque) → SetViewport → SetScissor → Push(pc0) →
            //        DrawIndexed(6,1,0,0,0) → BindPipeline(transparent) →
            //        Push(pc1) → DrawIndexed(6,1,0,0,0)
            //        （set/index 沿用 [00]..[03]；begin/end A 组共享）
            // 位置：tile(4) 顶行第五格；r0=(x+30,y+36,116,116)，
            //       r1=(x+52,y+58,116,116)，两者重叠，r1 相对 r0 偏移 (22,22)。
            //
            // NOTE: 和 [03] 的对照：
            //   [03] 两条 draw 的 pipeline 相同，只换 push/CommandConstant；
            //   [04] 两条 draw 的 push 也不同，但真正的硬边界是 pipeline：
            //       opaqueUI: blendEnable=OFF；transparentUI: blendEnable=ON。
            //       这两条 pipeline 的 depth/stencil 都是 OFF，差别只在
            //       colorBlendState.blendEnable；Vulkan 仍按 pipeline 状态区分，
            //       不能跨 pipeline 合并。
            //   => 这是“pipeline 是 batch key”的最小例子；不能靠一条 draw 或
            //      一条 drawIndexedIndirect 跨 pipeline。
            //
            // 数据/偏移（当前顺序下）：
            //   前面 [00]1+[01]4+[02]2+[03]2 = 9 行，
            //     h0 = 9*248 = 2232, h1 = h0 + 248 = 2480。
            //   常量前面已有 5 条（0/8/16/24/32），
            //     c0 = 40, c1 = 48（c1 = c0 + 8）；都是新槽，不复用。
            //   pc0/pc1 只有 commandConstantsAddress 不同；renderVersion=4 只进拾取。
            //
            // 重叠/painter's order：
            //   r0 先画（opaque，橙，alpha=1），r1 后画（transparent，蓝，alpha=.55），
            //   r1 的 model 向右下平移 (22,22)，所以重叠区是“蓝 0.55 over 橙”。
            //   若交换两条 draw 或把透明放前面，结果会变；UI 是 painter's algorithm，
            //   顺序不能为了合批而重排。
            //
            // 隐变量（两条 draw 都是 0 基）：
            //   gl_DrawIDARB = 0   两条都是直接 draw；DrawID 不会因为 bindPipeline 变。
            //   gl_InstanceIndex = 0  两条都 firstInstance=0，instanceCount=1。
            //   gl_BaseVertexARB = 0；gl_VertexIndex = [0,1,2,0,2,3] 两条相同。
            //   gl_PrimitiveID = 0/1  每条 draw 从 0 重置。
            //   => instancePtr 都是 heapBaseStart + 0*248；第二条落到 h1 靠
            //      cc1.address_offset=h1，不是 gl_InstanceIndex。
            //
            // 着色器/混合：
            //   Shape 程序输出预乘色 vec4(rgb*a, a)；opaqueUI 直接写 attachment；
            //   transparentUI 的 blend 是 (ONE, ONE_MINUS_SRC_ALPHA)，正好消费预乘色。
            //   r1 的 a=0.55，所以重叠区 ≈ 0.55*蓝 + 0.45*橙；r0 的 a=1 不透明。
            //   SDF 本地空间：r0/r1 的 px_size 都是 116x116，SDF 参数化相同，
            //   只有 model 平移不同；不是两个不同 shader/不同 SDF。
            //
            // NOTE: 为什么橙色圆角外有黑角，蓝色透明圆角外没有？
            //   drawShape() 在 SDF 外（fill/border/shadow 都没覆盖）返回 vec4(0,0,0,0)。
            //   r0 用 opaqueUI（blendEnable=OFF）：这个 0 被直接写进 color attachment，
            //     116×116 quad 的圆角外就是黑（alpha 被忽略/不参与混合）。
            //   r1 用 transparentUI（blendEnable=ON）：src=(0,0,0,0) 时
            //     dst*(ONE_MINUS_SRC_ALPHA) = dst*1，结果 = dst，
            //     等于“不写”；所以蓝色 SDF 外保留橙色/背景，没有黑框。
            //   两条 draw 的 program 都是 0（Shape），并不是 program id 不同；
            //   差别在 pipeline 的 blendEnable + 实例 alpha。这是预乘 alpha 的经典坑：
            //   “SDF 外输出 0” 在 opaque 下是黑色，在 blend 下才是“透明”。
            //
            // 合并能力（引用 [01] 判据）：
            //   不能合并成一条 drawIndexed / drawIndexedIndirect：
            //     pipeline 不同；indirect 的每条 command 仍用当前 pipeline，
            //     gl_DrawID 只能选常量/数据，不能选 pipeline。
            //   如果强行都用 transparentUI，r0 alpha=1 的颜色结果接近，但 SDF 边缘
            //   的 coverage alpha <1 会走混合，和 opaqueUI 直接写 alpha 不同；
            //   语义上不是同一个 pipeline，不能当作无损合并。
            //   同 pipeline 时才可能像 [02] 那样用一条 drawIndexed(6,2) 合并。
            //
            // NOTE: 能不能不用两条 pipeline 实现同一效果？可以，而且通常更好：
            //   1. 只用 transparentUI（预乘 alpha）：
            //      橙色 a=1，内部结果 = src（dst*(1-1)=0），看起来仍是不透明；
            //      蓝色 a=.55 正常 over；SDF 外 a=0，dst 不变 → 黑角自然消失。
            //      代价：opaque 形状的 AA 边缘会和背景混合，而不是把 coverage
            //      写进 alpha；若后面把 alpha 当遮罩，语义会不同。
            //   2. 一条 pipeline + 动态 blend 状态（VK_EXT_extended_dynamic_state3
            //      的 vkCmdSetColorBlendEnableEXT 等）：省掉 pipeline 对象，但仍是
            //      两条 draw/两次状态切换，不能把 draw 合并。
            //   3. 真正一条 draw：只用 transparentUI + 一个 CommandConstant + h0/h1
            //      连续，firstInstance=0/1，drawIndexed(6,2,0,0,0)（[02] 的合并方式）；
            //      橙色/蓝色的 alpha 由实例行决定。这是“无多 pipeline + 单 draw”答案。
            //   所以本 case 的两条 pipeline 是刻意展示 pipeline 边界和黑角 artifact；
            //   生产 UI 通常选 1 或 3，而不是为 blend enable 单独做两条 pipeline。
            //
            // 附加：bindPipeline(transparent) 后不用重绑 set/index：
            //   两条 pipeline 的 layout 相同，descriptor set 兼容；viewport/scissor
            //   由两条 pipeline 都声明为 dynamic，所以状态保持。若 pipeline 把
            //   viewport/scissor 声明为 static，bind 会覆盖/使旧值失效，必须重设。
            // ────────────────────────────────────────────────────────────────
            {
                const VkRect2D area = tile(4);
                const VkViewport vp = full_vp;
                // NOTE: r1 相对 r0 偏移 (22,22)，两个 116×116 重叠；
                //       r0 先 opaque，r1 后 transparent，重叠区才看得到 alpha=0.55。
                const ui_dao::rect_px r0{static_cast<float>(area.offset.x) + 30.0F,
                                         static_cast<float>(area.offset.y) + 36.0F,
                                         116.0F, 116.0F};
                const ui_dao::rect_px r1{static_cast<float>(area.offset.x) + 52.0F,
                                         static_cast<float>(area.offset.y) + 58.0F,
                                         116.0F, 116.0F};
                // NOTE: add_shape/add_const 都是 ++cursor；当前 h0/h1=2232/2480，
                //       c0/c1=40/48（前面 5 条常量），不是复用前面的槽。
                const std::uint32_t h0 = add_shape(ui_dao::sdf_shape(
                    r0, view, 20.0F, glm::vec4(0.95F, 0.55F, 0.15F, 1.0F)));
                const std::uint32_t h1 = add_shape(ui_dao::sdf_shape(
                    r1, view, 20.0F, glm::vec4(0.20F, 0.55F, 0.95F, 0.55F)));
                const std::uint32_t c0 = add_const(0u, h0);
                const std::uint32_t c1 = add_const(0u, h1);
                // NOTE: pc0/pc1 只差 commandConstantsAddress；renderVersion 都是 4。
                const shader_data::PushData pc0 = pc_at(c0, 4u);
                const shader_data::PushData pc1 = pc_at(c1, 4u);

                // NOTE: 第一条用 opaqueUI（blend OFF）；这是和 [03] 之前相同的 pipeline。
                //       这条 bind 理论上可省（[03] 末尾就是 opaque），保留让片段自包含。
                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *mainCtx.pipelineOpaqueUI);
                commandBuffer.setViewport(0, std::span{&vp, 1});
                commandBuffer.setScissor(0, std::span{&area, 1});
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(pc0), &pc0);
                commandBuffer.drawIndexed(6, 1, 0, 0, 0);

                // NOTE: pipeline 是 batch key：这里换成 transparentUI（blend ON）。
                //       后面这条不能和前面的 opaque draw 合成一条 draw/indirect。
                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *mainCtx.pipelineTransparentUI);
                // NOTE: set/index 不用重绑（layout 兼容、状态保持）；但 push 换了 c1。
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(pc1), &pc1);
                commandBuffer.drawIndexed(6, 1, 0, 0, 0);
                // NOTE: 两条 draw 参数相同，但 pipeline 不同；vkCmdDrawIndexedIndirect
                //       也不能跨 pipeline（它只复用当前 pipeline）。
            }

            // ────────────────────────────────────────────────────────────────
            // [05] 同 scope 换 descriptor set：frame N → frame N+1
            // C API: SetViewport → SetScissor → BindDescriptorSets(A) →
            //        PushConstants(一次) → DrawIndexed(6,1,0,0,0) →
            //        BindDescriptorSets(B) → DrawIndexed(6,1,0,0,1)
            //        （pipeline/index 沿用 [00]..[04]；begin/end A 组共享）
            // 位置：tile(5) 顶行第六格；左/右两个 Shape（half 等分）。
            // NOTE: 运行当前程序可见：tile(5) 左黄、右青两块；因为两个 UBO
            //       内容相同，set A→B 本身不改变像素，肉眼看到的是 h0/h1 两行。
            //
            // NOTE: 和 [02]/[03] 的对照：
            //   [02] 只换 firstInstance；[03] 换 push/CommandConstant；
            //   [05] 换 descriptor set（UBO 来源）。draw 参数仍只有
            //   firstInstance=0/1，push 也仍是一次；区别只在 set 句柄。
            //
            // 数据/偏移（当前顺序下）：
            //   前面 [00]1+[01]4+[02]2+[03]2+[04]2 = 11 行，
            //     h0 = 11*248 = 2728, h1 = h0 + 248 = 2976。
            //   常量前面已有 7 条（0..48），cst = 56；只写一条，两条 draw 共用。
            //   pc_at(cst,5u) 的 renderVersion=5；firstInstance=0/1 仍是唯一
            //   选择 h0/h1 的 draw 参数。
            //
            // descriptor set 细节：
            //   descriptorSet = descriptorSets[currentFrame];
            //   descriptorSetAlternate = descriptorSets[(currentFrame+1)%2];
            //   两个句柄的 layout 相同（同一个 pipelineLayout/set0），但绑定的是
            //   不同的 uniformBuffers[i]。本 case 把两个 UBO 都写成同一个 ubo
            //   （cameraInfo[0] identity），所以两条 draw 的像素应完全相同；
            //   这是“API 边界”用例，不是视觉差异用例。
            //   如果两个 UBO 真不同（cameraInfo 平移/缩放），第二条才会换相机；
            //   当前故意相同，便于只观察 set 的绑定顺序。
            //
            // 隐变量（两条 draw 都是 0 基）：
            //   gl_DrawIDARB = 0   两条；gl_InstanceIndex = 0/1（firstInstance）；
            //   gl_BaseVertexARB = 0；gl_VertexIndex = [0,1,2,0,2,3] 相同；
            //   gl_PrimitiveID = 0/1 每条重置。
            //   => instancePtr=h0/h1 仍由 firstInstance 选，set 不影响实例行；
            //      UBO 只影响 cameraInfo，不影响 CommandConstant/instancePtr。
            //
            // 合并能力（引用 [01] 判据）：
            //   两条 draw 的 pipeline/index/viewport/scissor/push/draw 参数全同，
            //   但 descriptor set 不同 -> 不能合并成一条 draw / drawIndexedIndirect；
            //   indirect 的每条 command 仍使用当前绑定的 set，gl_DrawID 不能选 set。
            //   如果两个 set 内容真相同，这次 bind B 本身是冗余的；生产代码应做
            //   set 去重，直接沿用 A。若内容不同，要么：
            //     - 用同一个 set + bindless/SSBO 数组，把 per-instance UBO 数据
            //       放进 buffer，shader 用 gl_InstanceIndex 索引；这样一条 draw
            //       可覆盖两个实例；
            //     - 或用同一个 set + dynamic offset，但 dynamic offset 是 draw state，
            //       仍需两条 draw，不能靠它合并。
            //   所以 [05] 的 API 排列不能合并成 1 条；能优化的是“不要 bind 冗余的 B”。
            //
            // 附加：bindDescriptorSets 之后，pipeline/viewport/scissor/index/push
            //   都不变；只有 set0 绑定变了。set 的寿命到被 disturb 为止；这里
            //   layout 兼容，不会 disturb 其他 set。
            // ────────────────────────────────────────────────────────────────
            {
                const VkRect2D area = tile(5);
                const VkViewport vp = full_vp;
                // NOTE: h0/h1 连续（当前 2728/2976）；cst=56；
                //       两个 Shape 共用同一条 CommandConstant 和同一次 push。
                const std::uint32_t h0 =
                    add_shape(ui_dao::sdf_shape(half(area, false), view, 16.0F,
                                                glm::vec4(0.95F, 0.85F, 0.35F, 1.0F)));
                const std::uint32_t h1 = add_shape(ui_dao::sdf_shape(
                    half(area, true), view, 16.0F, glm::vec4(0.40F, 0.95F, 0.95F, 1.0F)));
                const std::uint32_t cst = add_const(0u, h0);
                const shader_data::PushData pc = pc_at(cst, 5u);

                commandBuffer.setViewport(0, std::span{&vp, 1});
                commandBuffer.setScissor(0, std::span{&area, 1});
                // NOTE: set A = 当前帧 UBO；push 一次覆盖两条 draw。
                commandBuffer.bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                 *pipelineLayout, 0, 1, &descriptorSet, 0,
                                                 nullptr);
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(pc), &pc);
                // NOTE: firstInstance=0 → h0，使用 set A 的 frame N UBO。
                commandBuffer.drawIndexed(6, 1, 0, 0, 0);

                // NOTE: 只换 set0；pipeline/index/viewport/scissor/push 都不变。
                commandBuffer.bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                 *pipelineLayout, 0, 1,
                                                 &descriptorSetAlternate, 0, nullptr);
                // NOTE: firstInstance=1 → h1，使用 set B 的 frame N+1 UBO。
                //       但两个 UBO 内容相同，所以像素应相同；这是 API 边界用例。
                commandBuffer.drawIndexed(6, 1, 0, 0, 1);
            }

            // ────────────────────────────────────────────────────────────────
            // [06] 同 scope 换 viewport：整屏 viewport → 右移 70px 的 viewport
            // NOTE: 图像位于: [comments/ability3-2.png]
            // NOTE: [06] 的核心是 viewport 平移，不是换数据.两条 draw 的 draw 参数、CommandConstant、heap 行完全一样；唯一变化是 viewport
            // NOTE: [06]/[07] 使用同一个 Rect/渐变/边框：
            //       [06] 画两份完整的；[07] 用 scissor 只裁两个角窗。
            //       所以 [06] 是“完整”，[07] 是“裁剪”，两者一起看。
            //
            // NOTE: 只有一个 add_shape，为什么画面像“两个实例”？
            //   不是 instancing！这里发了 **两次 vkCmdDrawIndexed**，每次
            //   instanceCount=1、firstInstance=0，所以 gl_InstanceIndex 两次都是 0；
            //   instancePtr 两次都指向同一行 Shape。也就是说：
            //     “画两遍同一份数据”，不是“一份数据画两个实例”。
            //   第二次看起来移动了，是因为两次 draw 之间改了 viewport：
            //     NDC 顶点 gl_Position 完全没变；viewport 只改 NDC→像素的映射：
            //       x_px = viewport.x + (x_ndc + 1) * 0.5 * viewport.width
            //     viewport A x=0  → shape 在 x20..90
            //     viewport B x=70 → 同一 NDC 整体 +70px → x90..160
            //   shape 宽 70、tile(6) 宽 170，所以两份都完整落在 tile 内；
            //   这次故意不裁，方便和 [07] 的裁剪对比。
            //   若真是多实例：[01] 那样 instanceCount>1，一次 draw 内
            //   gl_InstanceIndex=0/1 去选不同 heap 行；这里没有那条路径。
            //
            // NOTE: 运行当前程序：tile(6) 是两个完整的渐变圆角矩形，
            //       左右相邻（粉红→青蓝，带亮边框）。pipeline 是 [04] 的
            //       transparentUI，所以 SDF 外没有 [04] 的黑角。
            // C API: SetScissor → SetViewport(A) → Push(pc) → DrawIndexed →
            //        SetViewport(B, x+70) → DrawIndexed
            //        （没有 bindPipeline/bindSet/bindIndex：pipeline 沿用 [04]，
            //          set0 沿用 [05] 的 B，index 沿用 [00]；begin/end A 共享）
            // 位置：tile(6) 第二行第一格；完整 Rect = (x+20,y+30,70x100)，radius=18。
            //
            // NOTE: 和前面 case 的对照：
            //   [02]/[05] 靠 draw 参数/descriptor set 选数据；[06] 靠 viewport 变换。
            //   viewport A 全屏 → shape 在 x20..90；
            //   viewport B x=70 → 同一 model 平移到 x90..160；
            //   两拷贝都完整，scissor 仍是 tile(6) 全区域，没有裁切。
            //   两条 draw 的 draw 参数、CommandConstant、heap 行完全一样，
            //   唯一变化是 viewport。
            //
            // 数据/偏移：
            //   前面 [00]1+[01]4+[02]2+[03]2+[04]2+[05]2 = 13 行，
            //     heap = 13*248 = 3224；本 case 只写 1 行。
            //   常量前面 8 条（0..56），cst = 64；pc_at(cst,6u) 的 renderVersion=6。
            //   只 push 一次；两条 draw 共用同一条 CommandConstant 和 heap 行。
            //
            // 隐变量：
            //   gl_DrawIDARB=0 两条；gl_InstanceIndex=0 两条；gl_BaseVertexARB=0；
            //   gl_VertexIndex=[0,1,2,0,2,3] 相同；gl_PrimitiveID=0/1 每条重置。
            //   => instancePtr 不变，都是 heap；变化只在 NDC→像素的 viewport 映射。
            //
            // 合并能力：
            //   不能直接合并：一条 draw 只有一个 viewport；indirect 也不能换 viewport。
            //   但本 case 的差别只是平移，可以把它从 viewport 搬到 model：
            //     - 写两行 Shape，model 分别平移到 x+20 和 x+90；
            //     - 只保留一个全屏 viewport + tile scissor；
            //     - 一个 CommandConstant + firstInstance=0/1；
            //     - 一条 drawIndexed(6,2,0,0,0) 就画两条。
            //   这是 UI 合批的常见做法：能进 model/instance 的变换，不要用 viewport。
            //   若 viewport 还带缩放/翻转/投影语义，model 不一定等价；要逐项确认。
            //
            // 状态继承：
            //   [06] 没有 bindPipeline / bindDescriptorSets / bindIndexBuffer；
            //   pipeline 沿用 [04] 的 transparentUI，set0 沿用 [05] 的 B，
            //   index 沿用 [00]；viewport/scissor 只在这里改。
            //   这也是“状态粘滞”的实测：只改 viewport，其他都还在。
            // ────────────────────────────────────────────────────────────────
            {
                const VkRect2D area = tile(6);
                // NOTE: 两个 viewport 的 width/height 都是整屏；只有 x 变 0→70，
                //       避免把 viewport 设成 tile 导致的 NDC 二次缩放。
                const VkViewport vp_a{0.0F, 0.0F, W, H, 0.0F, 1.0F};
                const VkViewport vp_b{70.0F, 0.0F, W, H, 0.0F, 1.0F};
                // NOTE: 当前 heap=3224, cst=64；只写一条 CommandConstant/一行 Shape。
                //       只写 1 行！两次 draw 都读这同一行，不是多实例。
                //       这个 Rect/渐变/边框和 [07] 完全一样，方便对照“完整 vs 裁剪”。
                auto s = ui_dao::sdf_linear(
                    ui_dao::rect_px{static_cast<float>(area.offset.x) + 20.0F,
                                    static_cast<float>(area.offset.y) + 30.0F, 70.0F,
                                    100.0F},
                    view, 18.0F, glm::vec4(0.95F, 0.35F, 0.55F, 1.0F),
                    glm::vec4(0.20F, 0.80F, 0.95F, 1.0F));
                s.effects |= shader_data::FX_BORDER;
                s.border_px = 4.0F;
                s.border_color = glm::vec4(1.0F, 0.95F, 0.60F, 1.0F);
                const std::uint32_t heap = add_shape(s);
                const std::uint32_t cst = add_const(0u, heap);
                const shader_data::PushData pc = pc_at(cst, 6u);

                // NOTE: 这里没有 bindPipeline/bindSet/bindIndex；它们沿用 [04]/[05]/[00]。
                // NOTE: scissor 只设一次；viewport 才是两条 draw 的差异。
                commandBuffer.setScissor(0, std::span{&area, 1});
                commandBuffer.setViewport(0, std::span{&vp_a, 1});
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(pc), &pc);
                // NOTE: 同一 pc/heap/index；instanceCount=1，gl_InstanceIndex=0；
                //       viewport A 把 shape 放到 x20..90（完整）。
                commandBuffer.drawIndexed(6, 1, 0, 0, 0);
                // NOTE: 第二次还是 instanceCount=1、gl_InstanceIndex=0、同一 heap 行；
                //       不是新实例。viewport B 的 x+70 把同一 shape 放到 x90..160；
                //       两份都在 tile(6) 的 x0..170 内，所以都完整、不裁。
                commandBuffer.setViewport(0, std::span{&vp_b, 1});
                commandBuffer.drawIndexed(6, 1, 0, 0, 0);
                // NOTE: 两条 draw 参数一样；能合并的前提是把平移搬进 model，
                //       见上面的“合并能力”。

                // NOTE: 这里**不是** MVP 变化，也没有改 cam.view/model。
                //       mat4 mvp = cam.proj * cam.view * model; 两条 draw 完全相同，
                //       顶点着色器输出的 gl_Position / NDC 完全相同。
                //       移动发生在顶点着色器之后的 **viewport transform**：
                //         Vulkan spec §33.9 "Controlling the Viewport":
                //           x_f = (p_x / 2) * x_d + o_x
                //           o_x = viewport.x + p_x / 2
                //         代入后等价于：
                //           x_f = viewport.x + (x_d + 1) * 0.5 * p_x
                //       （p_x = viewport.width，x_d = NDC x，x_f = framebuffer x）
                //       所以 viewport B 的 x=70 只是把同一 NDC 结果在 framebuffer
                //       里右移 70px；tile(6) 足够宽，两份都完整。
                //       "V" 是 camera 的 view matrix；viewport 是另一件事，别混。
                //       官方出处：vkspec.html#vertexpostproc-viewport
                //       https://docs.vulkan.org/spec/latest/chapters/vertexpostproc.html#vertexpostproc-viewport
            }

            // ────────────────────────────────────────────────────────────────
            // NOTE: 图像位于: [comments/ability3-3.png]
            // [07] 同 scope 换 scissor：两个独立的裁剪窗口
            // C API: SetViewport → SetScissor(A) → Push(pc) → DrawIndexed →
            //        SetScissor(B) → DrawIndexed
            //        （没有 bindPipeline/bindSet/bindIndex：沿用 [04]/[05]/[00]）
            // 位置：tile(7) 第二行第二格。
            //
            // 数据/偏移：
            //   前面 [00]1+[01]4+[02]2+[03]2+[04]2+[05]2+[06]1 = 14 行，
            //     heap = 14*248 = 3472；本 case 只写 1 行。
            //   常量前面 9 条（0..64），cst = 72；pc_at(cst,7u) 的 renderVersion=7。
            //   只 push 一次；两条 draw 共用同一条 CommandConstant/同一行数据。
            //
            // NOTE: 这里的 Rect/渐变/边框和 [06] 完全一样：
            //   Rect = (x+20, y+30, 70×100)，radius=18，横向 linear 渐变 + 4px 边框。
            //   [06] 画两份完整的；[07] 只裁同一 Rect 的两个角窗，直接对照。
            //
            // shape：原始 Rect = (x+20, y+30, 70×100)，radius=18，
            //   横向 linear 渐变 + 4px 边框；渐变给出左右颜色，边框给出原 Rect 的边。
            //   win A = (x+15, y+25, 50, 50)  → 原 Rect 的左上角
            //   win B = (x+45, y+85, 50, 50)  → 原 Rect 的右下角
            //   两扇窗都同时包含“5px 背景 + 圆角边框 + 内部渐变”，所以：
            //     - 圆角/边框告诉你窗口落在原 Rect 的哪个角；
            //     - 窗口内侧的直边是 scissor 切出来的（与圆角边框明显不同）；
            //     - 横向渐变让左上窗偏 c0、右下窗偏 c1，位置更直观。
            //   一个全 tile scissor 的 draw 会显示整个 70×100 圆角矩形，
            //   不会出现这两个分离的角窗——这才是能看出 scissor 的效果。
            //
            // 隐变量：
            //   gl_DrawIDARB=0 两条；gl_InstanceIndex=0 两条；gl_BaseVertexARB=0；
            //   gl_VertexIndex=[0,1,2,0,2,3] 相同；gl_PrimitiveID=0/1 每条重置。
            //   => instancePtr/heap 不变；scissor 只影响片元是否通过。
            //
            // 合并/分离：
            //   对“两个分离窗口”这个视觉，当前两条 draw 是必要的：
            //   一条 draw 只有一个 scissor，无法同时产生 win A 和 win B；
            //   vkCmdDrawIndexedIndirect 也不能换 scissor（scissor 是 state）。
            //   若要把窗口也变成数据：把 clip rect 放进实例行，shader 里 discard/
            //   coverage；那样一条 draw + 全 tile scissor 也能做，但这是另一个数据模型。
            //   旧版“左右互补”之所以能合成一条 draw，是因为 sc_l ∪ sc_r = tile，
            //   且两半不重叠；这次窗口不互补，不能那样合并。
            //
            // 附加：这里只改 scissor，viewport/pipeline/set/index/push 都不变；
            //   scissor 是 dynamic state，生命周期到下一次 SetScissor/绑定 static
            //   pipeline 为止。
            // ────────────────────────────────────────────────────────────────
            {
                const VkRect2D area = tile(7);
                const VkViewport vp = full_vp;
                // NOTE: 两扇窗不重叠，分别切原 Rect 的左上角和右下角。
                //       窗口里同时有 5px 背景 + 圆角边框 + 渐变，便于定位。
                const VkRect2D win_a{{area.offset.x + 15, area.offset.y + 25}, {50, 50}};
                const VkRect2D win_b{{area.offset.x + 45, area.offset.y + 85}, {50, 50}};
                // NOTE: 原 Rect = (x+20,y+30,70×100)，radius=18；
                //       和 [06] 的 Rect/渐变/边框完全相同；这里只改 scissor。
                auto s = ui_dao::sdf_linear(
                    ui_dao::rect_px{static_cast<float>(area.offset.x) + 20.0F,
                                    static_cast<float>(area.offset.y) + 30.0F, 70.0F,
                                    100.0F},
                    view, 18.0F, glm::vec4(0.95F, 0.35F, 0.55F, 1.0F),
                    glm::vec4(0.20F, 0.80F, 0.95F, 1.0F));
                s.effects |= shader_data::FX_BORDER;
                s.border_px = 4.0F;
                s.border_color = glm::vec4(1.0F, 0.95F, 0.60F, 1.0F);
                const std::uint32_t heap = add_shape(s);
                const std::uint32_t cst = add_const(0u, heap);
                const shader_data::PushData pc = pc_at(cst, 7u);

                commandBuffer.setViewport(0, std::span{&vp, 1});
                // NOTE: 只换 scissor；pipeline/set/index/viewport/push 都不变。
                commandBuffer.setScissor(0, std::span{&win_a, 1});
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(pc), &pc);
                // NOTE: win A：只露出原 Rect 的左上角 50×50（含圆角边框+背景）。
                commandBuffer.drawIndexed(6, 1, 0, 0, 0);
                // NOTE: win B：只露出原 Rect 的右下角 50×50；两窗不重叠。
                commandBuffer.setScissor(0, std::span{&win_b, 1});
                commandBuffer.drawIndexed(6, 1, 0, 0, 0);
            }

            // ────────────────────────────────────────────────────────────────
            // NOTE: 图像位于: [comments/ability3-4.png]
            // [08] 同 scope 换 index buffer 绑定：quad 的 6 索引 → 三角形的 3 索引
            // NOTE: 图像 ability3-4.png 里第二个圆角四边形“未完成”不是 bug：SDF需要足够的画布像素来分配具体值,画布不够就不完整
            // NOTE: 顶点+硬件图元决定像素范围,没有未定义. 三角形图元的顶点至少是3
            //       offset=24 处只有 3 个索引 {0,1,2}，第二条 draw 的 indexCount=3，
            //       所以只光栅化一个三角形；SDF 仍在完整 Shape 的局部坐标里计算，
            //       于是你看到的是“圆角矩形的三角形切片”。
            //       第一条 offset=0 有 6 个索引 [0,1,2,0,2,3]，所以完整。
            // C API: SetViewport → SetScissor → BindIndexBuffer(offset=0,UINT32) →
            //        PushConstants(一次) → DrawIndexed(6,firstInstance=0) →
            //        BindIndexBuffer(offset=24,UINT32) → DrawIndexed(3,firstInstance=1)
            // 位置：tile(8) 第二行第三格；左/右两个 Shape（half 等分）。
            //
            // NOTE: 为什么这里 SDF 外没有黑边？[08] 没有 bindPipeline，
            //       沿用的还是 [04] 的 transparentUI（blendEnable=ON）。
            //       同一个 Shape shader/SDF 在 SDF 外输出 vec4(0,0,0,0)：
            //         - transparentUI: src + dst*(1-src.a) = dst → 透明，
            //           保留背景/上一次绘制；
            //         - opaqueUI: blend OFF，0 直接写进 attachment → 黑
            //           （[04] 橙色圆角外的黑角就是它）。
            //       所以“同一个 shader、同一个 SDF 算法”可以有不同效果：
            //       shader 只算 fragColor/coverage；pipeline 的 colorBlendState
            //       决定 fragColor 如何和 dst 合成。这里第一条完整 quad 的 SDF 外、
            //       第二条三角形内但 SDF 外的像素，都走 transparent 路径；
            //       第二个看起来“只有圆角的一部分”，是 indexCount=3 的几何裁剪，
            //       不是黑边，也不是 SDF 算法变了。
            //
            // NOTE: src + dst*(1-src.a) 的来源（Vulkan blend equation, ADD）：
            //   官方逐通道公式（spec §Blending / Blend Operations）：
            // NOTE: RGB 用 srcColorBlendFactor，Alpha 用 srcAlphaBlendFactor——两套 factor 是分开的
            // R_out = R_src * srcColorBlendFactor + R_dst * dstColorBlendFactor
            // G_out = G_src * srcColorBlendFactor + G_dst * dstColorBlendFactor
            // B_out = B_src * srcColorBlendFactor + B_dst * dstColorBlendFactor
            // A_out = A_src * srcAlphaBlendFactor + A_dst * dstAlphaBlendFactor
            //   本 pipeline 的 VkPipelineColorBlendAttachmentState：
            //     srcColorBlendFactor = ONE              -> S_r = 1
            //     dstColorBlendFactor = ONE_MINUS_SRC_ALPHA -> D_r = 1 - A_src
            //     srcAlphaBlendFactor = ONE              -> S_a = 1
            //     dstAlphaBlendFactor = ONE_MINUS_SRC_ALPHA -> D_a = 1 - A_src
            //     colorBlendOp = alphaBlendOp = ADD
            //   代入： C = Color（RGB 三通道打包）;A = Alpha;src = source（源，即将写入的）;dst = destination（目标，画布上已有的）;out = output（输出，混合结果）
            //     C_out = C_src + C_dst * (1 - A_src)
            //     A_out = A_src + A_dst * (1 - A_src)
            //   只有 C_src 是预乘色 (rgb * a) 时，才能简写成
            //     C_out = src + dst * (1 - src.a)
            //   这里的 src 指“预乘后的颜色”，不是直色；
            //   若 shader 输出直色，颜色 factor 要用 SRC_ALPHA，公式才是
            //     C_out = src * a + dst * (1 - a)。
            //   colorWriteMask 只决定写哪些通道，不参与上面的合成公式。
            //
            // 数据/偏移（当前顺序下）：
            //   前面 [00]1+[01]4+[02]2+[03]2+[04]2+[05]2+[06]1+[07]1 = 15 行，
            //     h0=15*248=3720, h1=3968。
            //   常量前面 10 条（0..72），cst=80；pc_at(cst,8u) 的 renderVersion=8。
            //   index buffer 布局：
            //     offset 0  : [0,1,2, 0,2,3]  （6 个 UINT32，完整 quad）
            //     offset 24 : [0,1,2]          （3 个 UINT32，三角形）
            //   kTriIdxOffset = 24 bytes = 6 * sizeof(uint32_t)，满足 4 字节对齐。
            //   只 push 一次；两条 draw 共用同一条 CommandConstant。
            //
            // 两条 draw 的差异：
            //   first : BindIndexBuffer(0,UINT32)  + DrawIndexed(6,1,0,0,0)
            //   second: BindIndexBuffer(24,UINT32) + DrawIndexed(3,1,0,0,1)
            //   => offset 0→24、indexCount 6→3；firstIndex/vertexOffset 仍是 0；
            //      firstInstance 0→1，所以第二条读 h1。
            //   gl_VertexIndex: 第一条 [0,1,2,0,2,3]，第二条 [0,1,2]；
            //   gl_PrimitiveID: 第一条 0/1（两个三角形），第二条只有 0。
            //   gl_DrawIDARB=0、gl_BaseVertexARB=0 两条相同。
            //
            // NOTE: 如果只想验证“index buffer 绑定 offset”，第二条应该也在
            //       offset24 放完整 6 索引并 draw(6)；当前故意放 3 索引，
            //       让“换绑定”带来的几何差异肉眼可见，代价是第二个 Shape 只剩三角形。
            //       这不是渲染错误，是测试数据的选择。
            //
            // 合并能力（引用 [01] 判据）：
            //   不能合成一条 direct drawIndexed：一条 draw 只有一个 index buffer
            //   绑定/offset 和 indexCount，6 与 3、0 与 24 不能同时表达。
            //   但两条用的是同一个 VkBuffer，只是 offset 不同；所以可以 bind 一次 0，
            //   用一条 vkCmdDrawIndexedIndirect(count=2)：
            //     cmd0 {indexCount=6, firstIndex=0, firstInstance=0}
            //     cmd1 {indexCount=3, firstIndex=6, firstInstance=1}  // 24B/4=6
            //   仍然只画一个三角形（cmd1 的 indexCount=3），但 C API 合成了一条。
            //   如果两条来自不同的 VkBuffer，indirect 也不能换 buffer，必须分开。
            //   （若要让第二条完整：offset24 放 6 索引 + cmd1 indexCount=6，
            //     这样才是只比较“绑定 offset”的干净用例。）
            // ────────────────────────────────────────────────────────────────
            {
                const VkRect2D area = tile(8);
                const VkViewport vp = full_vp;
                const std::uint32_t h0 =
                    add_shape(ui_dao::sdf_shape(half(area, false), view, 16.0F,
                                                glm::vec4(0.45F, 0.95F, 0.85F, 1.0F)));
                const std::uint32_t h1 = add_shape(ui_dao::sdf_shape(
                    half(area, true), view, 16.0F, glm::vec4(0.95F, 0.45F, 0.85F, 1.0F)));
                // NOTE: 当前 h0/h1=3720/3968；cst=80；只写一条 CommandConstant。
                const std::uint32_t cst = add_const(0u, h0);
                const shader_data::PushData pc = pc_at(cst, 8u);

                commandBuffer.setViewport(0, std::span{&vp, 1});
                commandBuffer.setScissor(0, std::span{&area, 1});
                // NOTE: 这里没有 bindPipeline；pipeline 是 [04] 的 transparentUI
                //       （blend ON），所以 SDF 外的 vec4(0) 不会写成黑。
                // NOTE: offset=0 → 6 个索引的完整 quad [0,1,2,0,2,3]。
                commandBuffer.bindIndexBuffer(sb.globalIndexBuffer.buffer.buffer(), 0,
                                              VK_INDEX_TYPE_UINT32);
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(pc), &pc);
                // NOTE: 6 索引 → 画完整 quad；firstInstance=0 → h0。
                commandBuffer.drawIndexed(6, 1, 0, 0, 0);
                // NOTE: offset=24 → 只有 3 个索引 {0,1,2}，所以下面只能画三角形；
                //       这不是漏画，是 indexCount=3 的自然结果。
                commandBuffer.bindIndexBuffer(sb.globalIndexBuffer.buffer.buffer(),
                                              kTriIdxOffset, VK_INDEX_TYPE_UINT32);
                // NOTE: firstInstance=1 → h1；indexCount=3 → 只光栅化一个三角形，
                //       SDF 仍按 h1 的完整 px_size 计算，所以看起来是“半个圆角矩形”。
                //       三角形内、圆角外的像素 SDF=0；transparentUI 下保留 dst，
                //       所以不是黑，而是背景/上一次绘制。若换成 opaqueUI 就会黑。
                commandBuffer.drawIndexed(3, 1, 0, 0, 1);

                // NOTE: 混合在 shader 输出之后，且不在 shader 里
            }

            // ════════════════════════════════════════════════════════════════
            // [08-1..8] fixed-function 颜色链：同一份 Shape 数据 + 同一 fragColor
            //
            //   fragment shader → fragColor(src)
            //     → ① depth test / stencil test
            //     → ② color blend（读 dst）
            //     → ③ colorWriteMask
            //     → framebuffer
            //
            // NOTE: 为了控制单一变量，整个系列只准备一份共享数据：
            //   Shape0 = base：teal (0.20,0.60,1.00)、inset(ref,20)、z=0.75
            //   Shape1 = srcNear：红色 a=0.5、inset(ref,34)、z=0.25
            //   Shape2 = srcFar ：红色 a=0.5、inset(ref,34)、z=0.75
            //   三条 Shape 都在同一个 ref tile(9) 里；换 tile 不重新造数据，
            //   只改 viewport 的 x/y 偏移（[06] 的平移效果），再用 scissor 裁到目标 tile。
            //   => 所有 case 的 fragColor、几何、颜色、常量都相同；唯一的变量是
            //      depth/stencil 的 pass/fail、blend on/off、mask 全写/RG。
            //
            // NOTE: ①/②/③ 到底做什么（只看固定管线，不看 shader 算法）：
            //   ① depth/stencil test
            //      - depth: z_f 与 depth buffer 比较（本系列 LESS）；
            //        通过才继续，失败直接丢弃。
            //      - stencil: ref 与 stencil buffer 比较（EQUAL）；
            //        通过才继续，失败直接丢弃。
            //      - 这一阶段**不混色**；通过后 fragColor 原样进入 ②/③。
            //   ② color blend
            //      - blendEnable=ON 才执行；读 dst，按 factor 合成：
            //          C_out = C_src * S + C_dst * D
            //      - blendEnable=OFF：不读 dst，C_src 直接覆盖（之后再由 ③ 过滤）。
            //   ③ colorWriteMask
            //      - 按位决定最终写哪些通道；没写的通道保留 dst。
            //      - 它在 blend 之后；不是“混合”，只是写通道过滤。
            //   顺序固定 ①→②→③；① 失败时 ②/③ 不会发生。
            //
            // NOTE: 本系列涉及的 pipeline 状态（只切这几个）：
            //   pipelineOpaque3D    : depth ON/WRITE(LESS), blend OFF, mask ALL
            //   pipes[4]            : stencil setup, blend OFF, mask 0
            //   pipes[11]           : stencil EQUAL, blend OFF, mask ALL  ← [08-3/4]
            //   pipelineOpaqueUI    : depth/stencil OFF, blend OFF, mask ALL
            //   pipelineTransparentUI: blend ON, mask ALL
            //   pipes[10]           : blend ON, mask R|G
            //   pipes[9]            : blend OFF, mask R|G
            //
            // NOTE: ①→②→③ 的顺序是 fixed-function 固定的，不能重排；
            //   ① 失败时 ②/③ 不会发生，所以不机械复制视觉重复的 2^3 组合。
            //   每个 case 的 C API diff 在注释里单独标出。
            //
            // NOTE: [08-1..8] 暂时占用 tile(9..16)；原 [09]..[16] 仍在 #if0，
            //       启用时把它们顺延/换 tile 即可，其他 case 代码不改。
            // NOTE: setViewport 来实现偏移分开的效果,但是不改变输入,从头到尾不变
            // ════════════════════════════════════════════════════════════════

            const VkRect2D chainRef = tile(9);
            auto chainBase = ui_dao::sdf_shape(inset(chainRef, 20.0F), view, 24.0F,
                                               glm::vec4(0.20F, 0.60F, 1.00F, 1.0F));
            chainBase.model[3].z = 0.75F;
            auto chainNear = ui_dao::sdf_shape(inset(chainRef, 34.0F), view, 20.0F,
                                               glm::vec4(1.00F, 0.00F, 0.00F, 0.50F));
            chainNear.model[3].z = 0.25F;
            auto chainFar = chainNear; //NOTE: chainFar 和 chainFar 就 z 不同
            chainFar.model[3].z = 0.75F;

            const std::uint32_t chainH0 = add_shape(chainBase);
            const std::uint32_t chainH1 = add_shape(chainNear);
            const std::uint32_t chainH2 = add_shape(chainFar);
            const std::uint32_t chainCst = add_const(0u, chainH0);
            const shader_data::PushData chainPc = pc_at(chainCst, 80u);

            // 同一份数据从 ref tile(9) 平移到目标 tile；width/height 保持整屏。
            const auto chain_vp = [&](const VkRect2D &area) -> VkViewport {
                return {static_cast<float>(area.offset.x - chainRef.offset.x),
                        static_cast<float>(area.offset.y - chainRef.offset.y),
                        W,
                        H,
                        0.0F,
                        1.0F};
            };

            // NOTE: [08-1],[08-2] 测试 z 远近; 管线: pipelineOpaque3D	    ON (LESS)	    ON	        OFF	            不透明3D物体
            // ────────────────────────────────────────────────────────────────
            // [08-1] ① depth PASS：base z=.75，srcNear z=.25
            // C API: BindPipeline(opaque3D) → SetViewport(chain_vp) → SetScissor(area) →
            //        BindIndexBuffer(0) → Push(chainPc) →
            //        DrawIndexed(Shape0) → DrawIndexed(Shape1)
            // 位置：tile(9)；viewport 偏移 (0,0)。
            // 共享数据：h0=4216,h1=4464,h2=4712；cst=88,rv=80。
            // 链：base 写 color+depth(.75)；srcNear(.25) depth 通过。
            // NOTE: pipelineOpaque3D 的 blendEnable=OFF，source 通过 depth 后是
            //       直接写红色 (.5,0,0)，不是 blend；所以中心暗红，不是紫。
            // ────────────────────────────────────────────────────────────────
            {
                const VkRect2D area = tile(9);
                const VkViewport vp = chain_vp(area);
                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *mainCtx.pipelineOpaque3D);
                commandBuffer.setViewport(0, std::span{&vp, 1});
                commandBuffer.setScissor(0, std::span{&area, 1});
                commandBuffer.bindIndexBuffer(sb.globalIndexBuffer.buffer.buffer(), 0,
                                              VK_INDEX_TYPE_UINT32);
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(chainPc), &chainPc);
                commandBuffer.drawIndexed(6, 1, 0, 0, 0); // Shape0 base
                commandBuffer.drawIndexed(6, 1, 0, 0, 1); // Shape1 near
            }

            // ────────────────────────────────────────────────────────────────
            // [08-2] ① depth FAIL：只把 source 从 Shape1 换成 Shape2（z=.75）
            // C API diff vs [08-1]: 第二条 DrawIndexed 的 firstInstance 1→2；
            //                       viewport/scissor 只从 tile9 换到 tile10（布局，不是实验变量）。
            // 位置：tile(10)；viewport 偏移 (+170,0)。
            // 链：base depth=.75；Shape2 也是 .75，LESS 失败，②/③ 不执行；只剩 teal base。
            // ────────────────────────────────────────────────────────────────
            {
                const VkRect2D area = tile(10);
                const VkViewport vp = chain_vp(area);
                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *mainCtx.pipelineOpaque3D);
                commandBuffer.setViewport(0, std::span{&vp, 1});
                commandBuffer.setScissor(0, std::span{&area, 1});
                commandBuffer.bindIndexBuffer(sb.globalIndexBuffer.buffer.buffer(), 0,
                                              VK_INDEX_TYPE_UINT32);
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(chainPc), &chainPc);
                commandBuffer.drawIndexed(6, 1, 0, 0, 0); // Shape0 base
                commandBuffer.drawIndexed(6, 1, 0, 0, 2); // Shape2 far
            }

            // NOTE: depth 和 stencil “一个水平”，是因为它们共用同一块 depth-stencil 内存、同属 fragment tests 阶段、都是不产生颜色的“留/丢”门禁；
            // NOTE: ① 测试门禁 → ② 颜色合成 → ③ 写通道过滤
            // ────────────────────────────────────────────────────────────────
            // [08-3] ① stencil PASS：pipe4 先把 0→1，pipe11 ref=1
            // C API: SetViewport(chain_vp) → SetScissor(area) → BindIndexBuffer(0) →
            //        Push(chainPc) →
            //        BindPipeline(pipe4) → SetStencilReference(0) →
            //        DrawIndexed(Shape0, 只写 stencil) →
            //        BindPipeline(opaqueUI) → DrawIndexed(Shape0, 写 color) →
            //        BindPipeline(pipe11) → SetStencilReference(1) →
            //        DrawIndexed(Shape1)
            // 位置：tile(11)；viewport 偏移 (+340,0)。
            //
            // NOTE: 先把 stencil 的三个值分开（后面 [08-4] 只改②）：
            //   ① stencil buffer 值：depth-stencil image 每像素 8 bit；
            //      GPU 在 draw 执行时按 passOp/failOp/depthFailOp 逐片元改。
            //   ② reference：per-draw 的 dynamic state；
            //      CPU 用 vkCmdSetStencilReference 在 draw 前设，后续 draw 沿用。
            //   ③ compareMask / writeMask / compareOp / passOp…：
            //      pipeline 创建时写死，draw 期间不变。
            //
            //   每个片元：
            //     ref_m     = reference     & compareMask
            //     stencil_m = stencil_buffer& compareMask
            //     pass      = compare(stencil_m, ref_m)       // 本 case EQUAL
            //     if (!pass)          stencil = op(failOp,      stencil) & writeMask
            //     else if (!depthPass) stencil = op(depthFailOp, stencil) & writeMask
            //     else                stencil = op(passOp,      stencil) & writeMask
            //   stencilTestEnable=FALSE 时整段跳过；
            //   writeMask=0 时测试照做，但 stencil buffer 不更新。
            //
            // NOTE: 本 case 三条 draw 用的 pipeline：
            //   pipes[4]        : stencil ON, compare=ALWAYS, passOp=INCREMENT,
            //                     writeMask=0xFF, colorWriteMask=0
            //   pipelineOpaqueUI: stencil OFF, depth OFF, blend OFF, color ALL
            //   pipes[11]       : stencil ON, compare=EQUAL, passOp=KEEP,
            //                     writeMask=0x00, blend OFF, color ALL
            //
            // 初始：tile(11) 的 stencil 全 0（A scope 的 stencil clear=0）。
            // Draw 1（pipes[4]，Shape0，ref=0）：
            //   compare=ALWAYS 通过；passOp=INCREMENT → stencil 0→1；
            //   colorWriteMask=0 → 不写 color。结果：stencil=1，color 仍是 clear。
            // Draw 2（opaqueUI，Shape0）：
            //   stencil OFF → stencil buffer 完全不动，仍是 1；
            //   写 teal 到 color。
            // Draw 3（pipes[11]，Shape1，ref=1）：
            //   stencil_m=1&0xFF=1，ref_m=1&0xFF=1 → EQUAL 通过；
            //   passOp=KEEP、writeMask=0 → stencil 仍为 1；
            //   blendEnable=OFF → 不读 dst；
            //   colorWriteMask=ALL → 直接写 Shape1 的预乘红色
            //   (.5,0,0,.5)。结果：暗红直接覆盖 teal，不是混合。
            //
            // NOTE: stencil PASS 本身**不是混合**！混合只由 ② blendEnable 决定；
            //       pipes[11] 的 blendEnable=OFF，所以 PASS 后是直接覆盖。
            //       （之前用 pipe6 会 blendOver，那是 stencil + blend 两件事混在一起，
            //         已改成 pipes[11] 隔离 ①。）
            // ────────────────────────────────────────────────────────────────
            {
                const VkRect2D area = tile(11);
                const VkViewport vp = chain_vp(area);
                commandBuffer.setViewport(0, std::span{&vp, 1});
                commandBuffer.setScissor(0, std::span{&area, 1});
                commandBuffer.bindIndexBuffer(sb.globalIndexBuffer.buffer.buffer(), 0,
                                              VK_INDEX_TYPE_UINT32);
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(chainPc), &chainPc);

                // NOTE: “draw 不一定生成像素”
                // pipeline	            stencil	                            colorWriteMask	    blend
                // pipes[4]	            ALWAYS + INCREMENT，writeMask=0xFF	0（不写 color）	    OFF
                // pipelineOpaqueUI	    OFF（不碰 stencil）	                ALL	                OFF
                // pipes[11]	        EQUAL + KEEP，writeMask=0	        ALL	                OFF

                /*
                Draw 1：pipes[4]，Shape0（base），ref=0
                光栅化：base 大矩形 ∩ scissor(tile11) → 每个片元
                对每个片元：
                    stencil_buffer[px] = 0        （初始 clear 值）
                    ref                = 0        （setStencilReference(0)）
                    compareMask        = 0xFF
                    compareOp          = ALWAYS   → (0 & 0xFF) ALWAYS (0 & 0xFF) → pass
                    passOp             = INCREMENT_AND_CLAMP, writeMask = 0xFF
                                    → stencil_buffer[px] = 0 + 1 = 1
                    colorWriteMask     = 0        → 不写 color（clear 值保留）
                结果：base 覆盖的每个像素 stencil=1，color 仍是 clear。
                注意：base 之外没有片元（scissor 内但几何外），谈不上 pass/fail。
                */
                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *(*uiPipelines().pipes[4]));
                commandBuffer.setStencilReference(VK_STENCIL_FACE_FRONT_AND_BACK, 0);
                commandBuffer.drawIndexed(6, 1, 0, 0, 0);

                /*
                Draw 2：pipelineOpaqueUI，Shape0（base）
                光栅化：base 大矩形 ∩ scissor → 每个片元
                对每个片元：
                    stencilTestEnable = VK_FALSE  → 没有 stencil test，也就没有 pass/fail
                                                → stencil_buffer 不读、不写、保持 1
                    blendEnable       = OFF       → 不读 dst
                    colorWriteMask    = ALL       → 直接写 teal
                结果：base 覆盖的每个像素 color=teal，stencil 仍为 1。
                NOTE: 关键：这条 draw 没有“成功/失败”概念，因为 stencilTest=OFF。它只写颜色
                */
                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *mainCtx.pipelineOpaqueUI);
                commandBuffer.drawIndexed(6, 1, 0, 0, 0); // base teal

                /*
                Draw 3：pipes[11]，Shape1（srcNear），ref=1
                光栅化：srcNear 小矩形 ∩ scissor → 每个片元
                前提：srcNear ⊂ base（inset 34 > inset 20），
                        所以这些片元所在像素在 Draw 1 里都被写成 stencil=1。
                对每个片元：
                    stencil_buffer[px] = 1
                    ref                = 1        （setStencilReference(1)）
                    compareMask        = 0xFF
                    compareOp          = EQUAL    → (1 & 0xFF) EQUAL (1 & 0xFF) → pass
                    passOp             = KEEP, writeMask = 0x00
                                    → stencil_buffer[px] 仍为 1
                    blendEnable        = OFF      → 不读 dst
                    colorWriteMask     = ALL      → 直接写 red (1,0,0,0.5)
                结果：srcNear 覆盖的每个像素 color=red（直接覆盖 teal），stencil 仍为 1。
                失败条件（本 case 不触发）：若某个片元所在像素 stencil≠1，
                    EQUAL 失败 → 该片元被丢弃，不写 color、不改 stencil。
                NOTE: 关键：这条 draw 的 pass/fail 取决于“片元所在像素的 stencil 值”。
                */
                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *(*uiPipelines().pipes[11]));
                commandBuffer.setStencilReference(VK_STENCIL_FACE_FRONT_AND_BACK, 1);
                commandBuffer.drawIndexed(6, 1, 0, 0,
                                          1); // Shape1, stencil pass, blend OFF

                // NOTE: 视觉：teal 框 + 红中心——和 [08-1] 完全一样。
                /*
                NOTE: 为什么 [08-1] 和 [08-3] 视觉一样
                因为它们都是“base 先画 teal，srcNear 后画 red 覆盖”，只是中间的“门禁”不同：
                            [08-1]	                [08-3]
                门禁	    depth test（LESS）	    stencil test（EQUAL）
                门禁数据	depth buffer 的 0.75	stencil buffer 的 1
                门禁通过后	写 red	                写 red
                门禁失败时	丢弃	                丢弃

                NOTE: depth test 和 stencil test 都是“门禁”：通过就放行片元去写颜色，失败就丢弃。
                */

                /*
                NOTE: 为什么 [08-3] 要 3 次 draw，而 [08-1] 只要 2 次
                因为代码故意把“stencil setup”和“color write”拆开，让每一步职责单一：
                pipes[4]：colorWriteMask=0 → 只写 stencil，不写 color
                pipelineOpaqueUI：stencilTest=OFF → 只写 color，不碰 stencil
                pipes[11]：blendEnable=OFF + mask=ALL → 只做 stencil 测试 + 直接写 color，不混合

                如果把 pipes[4] 的 colorWriteMask 改成 ALL，它就能同时写 stencil 和 teal，[08-3] 可以压缩成 2 次 draw（和 [08-1] 对称）。
                但那样就把“stencil 写”和“color 写”混在一次 draw 里，不容易单独观察 ① 阶段。这是测试代码的设计选择，不是 Vulkan 的硬性要求。
                */
            }

            // ────────────────────────────────────────────────────────────────
            // [08-4] ① stencil FAIL：//NOTE: 只把 pipe11 的 ref 1→0,让 EQUAL 失败,然后丢弃片元
            // C API diff vs [08-3]: SetStencilReference(1)→(0)；数据/viewport 完全不变
            //                      （viewport 只从 tile11 换到 tile12）。
            // 位置：tile(12)；viewport 偏移 (-510,+160)。
            //
            // 三条 draw 和 [08-3] 完全一样：
            //   Draw1 pipe4: stencil 0→1，不写 color；
            //   Draw2 opaqueUI: 写 teal，不碰 stencil；
            //   Draw3 pipe11: stencil_m=1，ref_m=0 → EQUAL 失败。
            // 失败分支：
            //   failOp=KEEP、writeMask=0 → stencil 仍为 1（不被改）；
            //   片元被丢弃 → ② blend / ③ colorWriteMask 根本不执行；
            //   framebuffer 只有 Draw2 的 teal。
            //
            // NOTE: 这不是“blend 失败”，是片元在 ① 就被丢弃了；
            //       即使 pipes[11] 的 blendEnable 改成 ON，也不会发生混合。
            //       ① 通过后才轮到 ②/③，这是固定顺序。
            // ────────────────────────────────────────────────────────────────
            {
                const VkRect2D area = tile(12);
                const VkViewport vp = chain_vp(area);
                commandBuffer.setViewport(0, std::span{&vp, 1});
                commandBuffer.setScissor(0, std::span{&area, 1});
                commandBuffer.bindIndexBuffer(sb.globalIndexBuffer.buffer.buffer(), 0,
                                              VK_INDEX_TYPE_UINT32);
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(chainPc), &chainPc);

                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *(*uiPipelines().pipes[4]));
                commandBuffer.setStencilReference(VK_STENCIL_FACE_FRONT_AND_BACK, 0);
                commandBuffer.drawIndexed(6, 1, 0, 0, 0);

                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *mainCtx.pipelineOpaqueUI);
                commandBuffer.drawIndexed(6, 1, 0, 0, 0);

                /*
                Draw 3：pipes[11]，Shape1（srcNear），ref=0   ← 与 [08-3] 唯一差别
                对每个片元：
                    stencil_buffer[px] = 1        （Draw 1 写的，Draw 2 没碰）
                    ref                = 0
                    compareOp          = EQUAL    → (1 & 0xFF) EQUAL (0 & 0xFF) → 1==0 → fail
                    failOp             = KEEP, writeMask = 0x00
                                    → stencil_buffer[px] 仍为 1
                    片元被丢弃 → blend / colorWriteMask 不执行
                结果：srcNear 区域保留 Draw 2 写的 teal，没有红色。
                */
                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *(*uiPipelines().pipes[11]));
                commandBuffer.setStencilReference(VK_STENCIL_FACE_FRONT_AND_BACK, 0);
                commandBuffer.drawIndexed(6, 1, 0, 0,
                                          1); // Shape1, stencil fail, blend OFF

                /*
                NOTE: 可以同时打开 深度和模板测试: fragment shader → scissor → stencil test → depth test → blend → colorWriteMask
                注意：stencil test 先于 depth test。这个顺序决定了 VkStencilOpState 为什么有三个 op：
                    .failOp        // stencil test 失败
                    .passOp        // stencil test 通过，且 depth test 也通过
                    .depthFailOp   // stencil test 通过，但 depth test 失败
                */
            }

            // ────────────────────────────────────────────────────────────────
            // [08-5..8] ② blend × ③ colorWriteMask 的 2×2 矩阵
            // NOTE: 就shape1 效果不同
            //                mask all          mask R|G
            //   blend ON     [08-5]            [08-7]
            //   blend OFF    [08-6]            [08-8]
            //
            //   四格用同一份 base/source、同一 viewport 尺寸、同一 fragColor：
            //     base   = (0.20,0.60,1.00,1.0)  // teal
            //     source = red a=0.5
            //   只切 pipeline 的 blendEnable / colorWriteMask；tile 只变 viewport 偏移。
            //   5↔6、7↔8 只差 blend；5↔7、6↔8 只差 mask。
            // ────────────────────────────────────────────────────────────────

            // NOTE: 8-5;8-6 测试混合;全部 mask ALL
            // ────────────────────────────────────────────────────────────────
            // [08-5] ② blend ON + ③ mask ALL
            // NOTE: 视觉：
            //   Shape1 中心 = 预乘红(0.5,0,0) + teal*0.5 = (0.6,0.3,0.5) 紫粉；
            //   Shape1 的 SDF 外 = src=(0,0,0,0) 走 blend → dst*1 = teal，不黑；
            //   Shape0 的 SDF 外 = Draw 1 用 opaqueUI，blend OFF，0 直接写 → 仍然黑。
            //   => “红色变粉”对；“没有圆角外的黑色”只对 Shape1 成立，Shape0 圆角外仍黑。
            // ────────────────────────────────────────────────────────────────
            {
                const VkRect2D area = tile(13);
                const VkViewport vp = chain_vp(area);
                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *mainCtx.pipelineOpaqueUI);
                commandBuffer.setViewport(0, std::span{&vp, 1});
                commandBuffer.setScissor(0, std::span{&area, 1});
                commandBuffer.bindIndexBuffer(sb.globalIndexBuffer.buffer.buffer(), 0,
                                              VK_INDEX_TYPE_UINT32);
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(chainPc), &chainPc);
                commandBuffer.drawIndexed(6, 1, 0, 0, 0); // Shape0 base

                // NOTE: pipelineTransparentUI 混合让圆角不黑
                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *mainCtx.pipelineTransparentUI);
                commandBuffer.drawIndexed(6, 1, 0, 0, 1); // Shape1, blend ON, mask all
            }

            // ────────────────────────────────────────────────────────────────
            // [08-6] ② blend OFF + ③ mask ALL
            // NOTE: 视觉：
            //   Shape1 红色直接覆盖 teal（不混合）→ 暗红；
            //   Shape1 的 SDF 外 = vec4(0) 直接写 → 也黑；
            //   Shape0 的 SDF 外 = 同样黑。
            //   => 和 [08-1] 视觉一样（blend OFF 直接贴 + 黑角），
            //      但 [08-1] 用 pipelineOpaque3D（depth ON），这里用 pipelineOpaqueUI（depth OFF）；
            //      因为 Shape1 ⊂ Shape0，depth test 不改变结果，所以视觉才一样。
            // ────────────────────────────────────────────────────────────────
            {
                const VkRect2D area = tile(14);
                const VkViewport vp = chain_vp(area);
                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *mainCtx.pipelineOpaqueUI);
                commandBuffer.setViewport(0, std::span{&vp, 1});
                commandBuffer.setScissor(0, std::span{&area, 1});
                commandBuffer.bindIndexBuffer(sb.globalIndexBuffer.buffer.buffer(), 0,
                                              VK_INDEX_TYPE_UINT32);
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(chainPc), &chainPc);
                commandBuffer.drawIndexed(6, 1, 0, 0, 0); // Shape0 base

                // NOTE: 都是 pipelineOpaqueUI .都有黑圆角外部
                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *mainCtx.pipelineOpaqueUI);
                commandBuffer.drawIndexed(6, 1, 0, 0, 1); // Shape1, blend OFF, mask all
            }

            /*
            // NOTE: 5,6,7,8差异都在管线上. blend 和 mask 决定的是颜色怎么写，不是像素在哪
            case	blend	mask	Shape1 SDF 内	        Shape1 SDF 外
            [08-5]	ON	    ALL	    (0.6, 0.3, 0.5) 紫粉	teal（看不见）
            [08-6]	OFF	    ALL	    (0.5, 0, 0) 暗红	    黑（0 直接写）
            [08-7]	ON	    R|G	    (0.6, 0.3, 1.0) 偏蓝紫	teal（看不见）
            [08-8]	OFF	    R|G	    (0.5, 0, 1.0) 偏蓝	    纯蓝（R/G=0，B 保留）
            */
            // ────────────────────────────────────────────────────────────────
            // [08-7] ② blend ON + ③ mask R|G
            // C API diff vs [08-5]: 只把 transparentUI 换成 maskRGBlend（pipes[10]，
            //                       blend ON + colorWriteMask=R|G）；数据/viewport 不变。
            // 位置：tile(15)；viewport 偏移 (0,+160)。
            // 链：先 blend 得 (.6,.3,.5)，再 mask R|G：
            //       R/G 写成 blend 结果，B/A 保留 base → (.6,.3,1.0)。
            //     对比 [08-5] 的 (.6,.3,.5)，B 没被写；对比 [08-6] 的暗红，
            //     mask 后 B 保留 base。
            // ────────────────────────────────────────────────────────────────
            {
                const VkRect2D area = tile(15);
                const VkViewport vp = chain_vp(area);
                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *mainCtx.pipelineOpaqueUI);
                commandBuffer.setViewport(0, std::span{&vp, 1});
                commandBuffer.setScissor(0, std::span{&area, 1});
                commandBuffer.bindIndexBuffer(sb.globalIndexBuffer.buffer.buffer(), 0,
                                              VK_INDEX_TYPE_UINT32);
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(chainPc), &chainPc);
                commandBuffer.drawIndexed(6, 1, 0, 0, 0); // Shape0 base

                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *(*uiPipelines().pipes[10]));
                commandBuffer.drawIndexed(6, 1, 0, 0, 1); // Shape1, blend ON, mask RG
            }

            // ────────────────────────────────────────────────────────────────
            // [08-8] ② blend OFF + ③ mask R|G
            // C API diff vs [08-7]: 只把 maskRGBlend 换成 maskRG（pipes[9]，
            //                       blend OFF + mask R|G）；数据/viewport 不变。
            // 位置：tile(16)；viewport 偏移 (+170,+160)。
            // 链：blend OFF 时 source (.5,0,0)；mask R|G：
            //       R/G 写成 .5/0，B/A 保留 base → (.5,0,1.0)。
            //     对比 [08-7] 的 (.6,.3,1.0)，R/G 不同；对比 [08-6] 的暗红，
            //     B 保留 base。
            // ────────────────────────────────────────────────────────────────
            {
                const VkRect2D area = tile(16);
                const VkViewport vp = chain_vp(area);
                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *mainCtx.pipelineOpaqueUI);
                commandBuffer.setViewport(0, std::span{&vp, 1});
                commandBuffer.setScissor(0, std::span{&area, 1});
                commandBuffer.bindIndexBuffer(sb.globalIndexBuffer.buffer.buffer(), 0,
                                              VK_INDEX_TYPE_UINT32);
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(chainPc), &chainPc);
                commandBuffer.drawIndexed(6, 1, 0, 0, 0); // Shape0 base

                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *(*uiPipelines().pipes[9]));
                commandBuffer.drawIndexed(6, 1, 0, 0, 1); // Shape1, blend OFF, mask RG
            }

            // NOTE: 没什么好讲. index type：UINT32 → UINT16 概率很低
            // ────────────────────────────────────────────────────────────────
            // [09] 同 scope 换 index type：UINT32 → UINT16
            // C API: BindIndexBuffer(UINT32,0) → DrawIndexed → BindIndexBuffer(UINT16,72) →
            //        DrawIndexed
            // 排列组合：indexType 变一次；两条 draw 的索引宽度不同。
            // 分离：必要。indexType 是 vkCmdBindIndexBuffer 的一部分，不能按实例变。
            // ────────────────────────────────────────────────────────────────
            {
                const VkRect2D area = tile(17);
                const VkViewport vp = full_vp;
                const std::uint32_t h0 =
                    add_shape(ui_dao::sdf_shape(half(area, false), view, 16.0F,
                                                glm::vec4(0.75F, 0.55F, 0.95F, 1.0F)));
                const std::uint32_t h1 = add_shape(ui_dao::sdf_shape(
                    half(area, true), view, 16.0F, glm::vec4(0.35F, 0.95F, 0.45F, 1.0F)));
                const std::uint32_t cst = add_const(0u, h0);
                const shader_data::PushData pc = pc_at(cst, 9u);

                commandBuffer.setViewport(0, std::span{&vp, 1});
                commandBuffer.setScissor(0, std::span{&area, 1});
                commandBuffer.bindIndexBuffer(sb.globalIndexBuffer.buffer.buffer(), 0,
                                              VK_INDEX_TYPE_UINT32);
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(pc), &pc);
                commandBuffer.drawIndexed(6, 1, 0, 0, 0);
                commandBuffer.bindIndexBuffer(sb.globalIndexBuffer.buffer.buffer(),
                                              kIndex16Offset, VK_INDEX_TYPE_UINT16);
                commandBuffer.drawIndexed(6, 1, 0, 0, 1);
            }

            // ────────────────────────────────────────────────────────────────
            // [10] indexed → non-indexed
            // C API: BindPipeline(transparent) → BindDescriptorSets → SetViewport →
            //        SetScissor → BindIndexBuffer(24) → PushConstants →
            //        DrawIndexed(3,firstIndex=0,firstInstance=0) →
            //        Draw(3,firstVertex=0,firstInstance=1)
            // 位置：tile(18)（[09] 在 tile17）。
            //
            // NOTE: PathFill 的“顶点数”要分两层看：
            //   1) path 点：PathFill.points[]，point_count 被 shader clamp 到
            //      3..PATH_MAX_POINTS(16)；也就是 path 至少是三角形。
            //   2) draw 的 carrier 顶点：shader 没有硬编码 3/4/6；
            //      它只从 VertexBuffer 里按 gl_VertexIndex 取顶点。
            //      carrier 可以是三角形/quad/任意三角化几何，只要覆盖 path。
            //   所以 3、4、6 个顶点本身都不“非法”；区别是 carrier 形状和覆盖范围。
            //
            // NOTE: 这次用 3 顶点 carrier，是因为 path 本身就是三角形，且
            //   carrier 的 3 个点和 path 的 3 个点完全重合。
            //   但注意：PathFill 的 AA 在 path 边界外侧需要片元；
            //   carrier 边界和 path 边界完全重合时，外侧半像素没有片元，
            //   AA 边缘会被裁掉。这个测试只验证命令族差异，边缘是硬边；
            //   要严格 AA，应把 carrier 稍微放大（仍可 3 顶点三角形），
            //   或恢复成覆盖 path + margin 的 quad。
            //
            // NOTE: 这条 case 和前面 06/07/08/09 不一样的地方：
            //   前面变的是 viewport/scissor/indexType/draw 参数，仍然都是
            //   vkCmdDrawIndexed；这里换的是“命令族”：
            //     DrawIndexed：先读 index buffer，再用 index 值取 vertex；
            //     Draw       ：直接从 firstVertex 顺序取 vertex，不读 index buffer。
            //   index buffer 仍然绑着，但对下面的 vkCmdDraw 是 dead state（合法）。
            //
            // NOTE: 顶点/索引数据：
            //   vertices[0] = (-.5,-.5) → local (0,0)
            //   vertices[1] = ( .5,-.5) → local (1,0)
            //   vertices[2] = ( .5, .5) → local (1,1)
            //   path tri   = (0,0), (1,0), (1,1)   // 与 vertices[0..2] 完全重合
            //   index buffer offset24: [0,1,2]     // 3 个 UINT32
            //   所以 indexed 和 non-indexed 都只提交 3 个顶点/索引，
            //   最终每条 draw 是一个三角形；两条 draw 才有左右两个三角形。
            //
            // NOTE: 两条 draw 的隐变量对比：
            //   DrawIndexed(3,1,0,0,0)：
            //     indexCount=3, instanceCount=1, firstIndex=0, vertexOffset=0,
            //     firstInstance=0
            //     index buffer [0,1,2] → gl_VertexIndex = 0,1,2
            //     gl_BaseVertexARB = vertexOffset = 0
            //     gl_InstanceIndex = 0 → 读 f0
            //   Draw(3,1,0,1)：
            //     vertexCount=3, instanceCount=1, firstVertex=0, firstInstance=1
            //     gl_VertexIndex = firstVertex + ordinal = 0,1,2
            //     gl_BaseVertexARB = firstVertex = 0
            //     gl_InstanceIndex = 1 → 读 f1
            //   注意：Draw 没有 firstIndex/vertexOffset 参数；DrawIndexed 没有 firstVertex。
            //   PathFill 的 vertex shader 只用 v.pos（raw gl_VertexIndex 取 vertex），
            //   不读 gl_BaseVertexARB；如果换成 Shape，BaseVertex 就会参与 local index。
            //
            // NOTE: 为什么不是 6 个顶点：
            //   6 个顶点是“quad carrier”版本的实现（carrier 覆盖任意 path）；
            //   这里 path 本身就是三角形，所以 carrier 也可以用 3 个顶点。
            //   之前看到的 6 顶点/两个三角形是 carrier 的两个三角形，
            //   不是最终可见的两个三角形；现在把 carrier 缩成 3 顶点，问题消失。
            //
            // NOTE: 合并能力：不能合并。
            //   indexed / non-indexed 是 vkCmdDrawIndexed / vkCmdDraw 两个不同命令族；
            //   vkCmdDrawIndexedIndirect 和 vkCmdDrawIndirect 也不能混在一条里。
            //   即使 f0/f1 连续、pipeline 相同，也不能用一条 draw 同时走两种路径。
            // ────────────────────────────────────────────────────────────────
            {
                const VkRect2D area = tile(18);
                const VkViewport vp = full_vp;
                // 与 vertices[0..2] 的 local 坐标完全重合；carrier 也是三角形。
                const std::array<glm::vec2, 3> tri = {
                    glm::vec2{0.0F, 0.0F}, glm::vec2{1.0F, 0.0F}, glm::vec2{1.0F, 1.0F}};
                const std::uint32_t f0 =
                    add_fill(ui_dao::path_fill(half(area, false), view, std::span{tri},
                                               glm::vec4(0.20F, 0.75F, 0.95F, 1.0F)));
                const std::uint32_t f1 =
                    add_fill(ui_dao::path_fill(half(area, true), view, std::span{tri},
                                               glm::vec4(0.95F, 0.55F, 0.20F, 1.0F)));
                const std::uint32_t cst = add_const(1u, f0);
                const shader_data::PushData pc = pc_at(cst, 10u);
                // 数据/偏移：f0=15872，f1=16124（PathFill stride=252）；
                //   cst=104，renderVersion=10，program=1（PATH_FILL）。
                //   f0/f1 连续；两条 draw 都只 push 一次 cst，靠 firstInstance 选 f0/f1。
                //   PathFill 的 shader 用 gl_InstanceIndex 选实例，所以 0→f0，1→f1。

                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *mainCtx.pipelineTransparentUI);
                commandBuffer.bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                 *pipelineLayout, 0, 1, &descriptorSet, 0,
                                                 nullptr);
                commandBuffer.setViewport(0, std::span{&vp, 1});
                commandBuffer.setScissor(0, std::span{&area, 1});
                // index buffer 只服务下面的 DrawIndexed；随后的 Draw 不读它。
                // 绑定保持是合法状态，不需要为了 non-indexed 解绑。
                commandBuffer.bindIndexBuffer(sb.globalIndexBuffer.buffer.buffer(),
                                              kTriIdxOffset, VK_INDEX_TYPE_UINT32);
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(pc), &pc);
                // indexed：3 个索引 [0,1,2]，firstIndex=0, vertexOffset=0, firstInstance=0
                //   gl_VertexIndex = 0,1,2；读 f0。
                commandBuffer.drawIndexed(3, 1, 0, 0, 0);
                // non-indexed：3 个顶点，firstVertex=0, firstInstance=1
                //   直接顺序取 vertices[0..2]；gl_VertexIndex = 0,1,2；
                //   index buffer 完全不参与；读 f1。
                commandBuffer.draw(3, 1, 0, 1);
                //                  │  │  │ │
                //                  │  │  │ └──────────────────── firstInstance = 1
                //                  │  │  └────────────────────── firstVertex   = 0
                //                  │  └───────────────────────── instanceCount = 1
                //                  └──────────────────────────── vertexCount   = 3
                /*
                firstVertex=8 的意思是：
                第 0 个顶点 → vertices[8]
                第 1 个顶点 → vertices[9]
                …
                第 5 个顶点 → vertices[13]
                字节偏移是 8 * sizeof(Vertex)，但 API 收的是序号，不是字节。Vulkan 会根据 VkVertexInputBindingDescription 的 stride 自动算字节偏移
                */

                /*
                NOTE: 非索引 draw 和 indexed draw 的区别
                                    drawIndexed	                                                    draw
                读 index buffer	    是	                                                            否
                gl_VertexIndex	    indexBuffer[firstIndex + i] + vertexOffset	                    firstVertex + i
                gl_BaseVertexARB	vertexOffset	                                                firstVertex
                顶点从哪取	        先查索引，再按索引取 vertex	                                    直接按序号取 vertex
                
                本例	            drawIndexed(6,1,0,0,0) → 索引 [0,1,2,0,2,3] → 顶点 0,1,2,0,2,3	draw(6,1,8,1) → 顶点 8,9,10,11,12,13
                
                */
            }

            // NOTE: 多个drawIndexed 可以通过inderct解决
            // ────────────────────────────────────────────────────────────────
            // [11] 同一绑定，只改 draw 参数：indexCount 6→3，firstIndex 0→1，
            //      vertexOffset 0→4，firstInstance 0→1
            // C API: BindPipeline(opaque) → BindIndexBuffer → SetViewport → SetScissor →
            //        PushConstants(一次) → DrawIndexed(6,firstInstance=0) →
            //        DrawIndexed(3,firstIndex=1,vertexOffset=4,firstInstance=1)
            // 排列组合：状态完全不变，只有 draw 参数变；第二条是三角形，
            //           并走顶点数组的 4..7 副本。
            // 分离：必要。indexCount/firstIndex/vertexOffset 不能按 instance 变；
            //       两条 draw 必须分开，且不能用一条 instanced draw 合并。
            // ────────────────────────────────────────────────────────────────
            {
                const VkRect2D area = tile(19);
                const VkViewport vp = full_vp;
                const std::uint32_t h0 =
                    add_shape(ui_dao::sdf_shape(half(area, false), view, 16.0F,
                                                glm::vec4(0.95F, 0.95F, 0.35F, 1.0F)));
                const std::uint32_t h1 = add_shape(ui_dao::sdf_shape(
                    half(area, true), view, 16.0F, glm::vec4(0.35F, 0.75F, 0.95F, 1.0F)));
                const std::uint32_t cst = add_const(0u, h0);
                const shader_data::PushData pc = pc_at(cst, 11u);

                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *mainCtx.pipelineOpaqueUI);
                commandBuffer.bindIndexBuffer(sb.globalIndexBuffer.buffer.buffer(), 0,
                                              VK_INDEX_TYPE_UINT32);
                commandBuffer.setViewport(0, std::span{&vp, 1});
                commandBuffer.setScissor(0, std::span{&area, 1});
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(pc), &pc);
                // 第 1 条：完整矩形
                //   (indexCount, instanceCount, firstIndex, vertexOffset, firstInstance)
                //   = (6, 1, 0, 0, 0)
                //   index buffer 从 0 开始取 6 个：
                //     [0,1,2, 0,2,3]
                //   两个三角形：
                //     (0,1,2) + (0,2,3)
                //   vertexOffset=0 → 顶点 0..3（quad 的四个唯一顶点）；
                //   firstInstance=0 → h0。
                //   所以这个 quad 被两个三角形完整覆盖，SDF 的四个圆角都能画出来。
                commandBuffer.drawIndexed(6, 1, 0, 0, 0);

                // 第 2 条：不完整矩形
                //   = (3, 1, 1, 4, 1)
                //   firstIndex=1 → 从 index[1] 开始取 3 个：
                //     [1,2,0]
                //   只够一个三角形，不是两个；第二个三角形 (0,2,3) 没有被提交。
                //   vertexOffset=4 → 实际顶点 = index + 4：
                //     1+4=5, 2+4=6, 0+4=4 → (5,6,4)
                //     4..7 是 0..3 的副本，所以几何上仍是 (1,2,0) 这个三角形；
                //     vertexOffset 只换顶点块，不增加三角形。
                //   firstInstance=1 → 选 h1（右半边实例），只改变实例数据/颜色/位置，
                //     也不会补上缺失的第二个三角形。
                //   因此“不完整”的根本原因是 indexCount=3：
                //     只光栅化 quad 的一个三角形；firstIndex/vertexOffset/firstInstance
                //     都不会改变“一个三角形”这个拓扑事实。
                //   SDF 仍按 h1 的完整 px_size 计算，但三角形外的片元不存在，
                //   所以看到的是被三角形裁掉一部分的圆角矩形，而不是完整矩形。
                commandBuffer.drawIndexed(3, 1, 1, 4, 1);
            }

            // ────────────────────────────────────────────────────────────────
            // [12] 第一次出现 VkDrawIndexedIndirectCommand：indexed indirect
            // C API: BindPipeline(transparent) → BindDescriptorSets → SetViewport →
            //        SetScissor → BindIndexBuffer → PushConstants(program=2) →
            //        DrawIndexedIndirect(count=1)
            //
            // NOTE: 和 [11] 的 API 对比，重点不是“画面像不像”，而是参数放在哪里：
            //   [11] vkCmdDrawIndexed(6,1,0,0,0)
            //        indexCount/firstIndex/vertexOffset/firstInstance 是
            //        **命令流里的立即参数**，每条 draw 都要在 CPU 侧发一次调用。
            //   [12] vkCmdDrawIndexedIndirect(buffer, offset, 1, stride)
            //        上面这 5 个数被写进 VkDrawIndexedIndirectCommand，
            //        作为 **buffer data** 存在；命令流里只有 buffer/offset/count/stride。
            //        这是第一次把“draw 参数”从 API 参数变成 GPU 可读数据。
            //
            // NOTE: indirect 的最大特点/最优场景：
            //   - 一次 vkCmdDrawIndexedIndirect 可以执行 drawCount 条 draw；
            //   - 每条 draw 的参数在 buffer 里连续排布，GPU 按 stride 读取；
            //   - CPU 可以只更新 buffer，不必重新录命令；也允许 GPU/compute 生成参数；
            //   - 适合批量、剔除、LOD、多实例；对单条 draw 反而多一次间接读取，
            //     所以 [12] 的 drawCount=1 只是 API 演示，不是性能最优形态。
            //   [13] 才会出现 count=2 / 多 program 的批量形态。
            //
            // NOTE: [12] 的画面和 [11] 很不一样，但不能全算到 indirect 头上：
            //   [12] 的 program=2，数据是 PathStroke（5 点 zigzag、宽 12、渐变），
            //       软件图元是“折线描边”；
            //   [11] 的 program=0，数据是 Shape（SDF 圆角矩形）。
            //   即使把 [12] 改成同参数的直接 DrawIndexed，path stroke 还是 zigzag；
            //   indirect 改变的是“参数从哪里来”，不是片元算法。
            //   所以这里提 [11] 只是为了对照 API 参数位置，不是拿两个不同图元比画质。
            // ────────────────────────────────────────────────────────────────
            {
                const VkRect2D area = tile(20);
                const VkViewport vp = full_vp;
                // NOTE: 恰好5个拐点,图像位于: [comments/ability3-5.png]
                const std::array<glm::vec2, 5> zig = {
                    glm::vec2{0.10F, 0.20F}, glm::vec2{0.50F, 0.82F},
                    glm::vec2{0.82F, 0.20F}, glm::vec2{0.22F, 0.62F},
                    glm::vec2{0.90F, 0.72F}};
                const std::uint32_t heap = add_stroke(
                    ui_dao::path_stroke(inset(area, 24.0F), view, std::span{zig}, 12.0F,
                                        glm::vec4(0.95F, 0.35F, 0.75F, 1.0F),
                                        glm::vec4(0.20F, 0.95F, 0.95F, 1.0F)));
                const std::uint32_t cst = add_const(2u, heap);

                // NOTE: 第一次出现 VkDrawIndexedIndirectCommand（20 字节，5×uint32）：
                //   indexCount    = 6
                //   instanceCount = 1
                //   firstIndex    = 0
                //   vertexOffset  = 0
                //   firstInstance = 0
                // 字段和 [11] 的 drawIndexed(6,1,0,0,0) 一一对应；
                // 区别是它们不再出现在 vkCmdDrawIndexed 的参数表里，而是写进
                // indirectDrawBuffer，成为 data。struct 之间按 stride 排列；
                // 这里只有 1 条，所以 stride = sizeof(VkDrawIndexedIndirectCommand)=20。
                const VkDrawIndexedIndirectCommand cmd{.indexCount = 6,
                                                       .instanceCount = 1,
                                                       .firstIndex = 0,
                                                       .vertexOffset = 0,
                                                       .firstInstance = 0};
                // add_indirect_indexed 把 cmd 写到 buffer 的 cursor*20 位置；
                // [12] 是第一次调用，所以 ind=0；下一个 command 会在 20。
                const VkDeviceSize ind = add_indirect_indexed(cmd);
                const shader_data::PushData pc = pc_at(cst, 12u);

                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *mainCtx.pipelineTransparentUI);
                commandBuffer.bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                 *pipelineLayout, 0, 1, &descriptorSet, 0,
                                                 nullptr);
                commandBuffer.setViewport(0, std::span{&vp, 1});
                commandBuffer.setScissor(0, std::span{&area, 1});
                commandBuffer.bindIndexBuffer(sb.globalIndexBuffer.buffer.buffer(), 0,
                                              VK_INDEX_TYPE_UINT32);
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(pc), &pc);
                // API: vkCmdDrawIndexedIndirect(commandBuffer, buffer, offset,
                //                               drawCount, stride)
                //   buffer    = indirectDrawBuffer
                //   offset    = ind = 0        （第一条 command 的字节偏移）
                //   drawCount = 1              （只执行 1 条）
                //   stride    = 20             （sizeof VkDrawIndexedIndirectCommand）
                // GPU 从 offset 读一条 command，按其中 5 个字段执行一次 indexed draw；
                // CPU 这里没有逐个传 indexCount/firstIndex/...。
                // 本 case 只有一条 command，gl_DrawIDARB=0；若 drawCount>1，
                // gl_DrawIDARB 会按 command 序号变化，[13] 会用到。
                commandBuffer.drawIndexedIndirect(sb.indirectDrawBuffer.buffer.buffer(),
                                                  ind, 1,
                                                  sizeof(VkDrawIndexedIndirectCommand));
            }

            // ────────────────────────────────────────────────────────────────
            // [13] 第一次 count>1：一次 vkCmdDrawIndexedIndirect 携带两个 mesh
            // C API: BindPipeline(transparent) → BindDescriptorSets → SetViewport →
            //        SetScissor → BindIndexBuffer →
            //        Push(cS) → DrawIndexedIndirect(count=2, stride=20)
            //
            // NOTE: 这条 case 只有 **1 次** drawIndexedIndirect，不是多次：
            //   drawCount=2，stride=20，读 ind 和 ind+20 两条
            //   VkDrawIndexedIndirectCommand。
            //   之前为了兼容“只执行第一条”的驱动，额外发过两条 count=1；
            //   实测本机 count=2 两条都执行，那个回退是重复绘制，已删除。
            //
            // NOTE: 两个 command 的 5 个 draw 参数完全一样：
            //   indexCount=6, instanceCount=1, firstIndex=0,
            //   vertexOffset=0, firstInstance=0
            //   隐变量：
            //     命令0（ind）    DrawID=0, Instance=0, BaseVertex=0,
            //                     Vertex=0,1,2,0,2,3, PrimitiveID=0/1
            //     命令1（ind+20） DrawID=1, Instance=0, BaseVertex=0,
            //                     Vertex=0,1,2,0,2,3, PrimitiveID=0/1
            //   区别只在 shader 用 gl_DrawIDARB 取的 CommandConstant：
            //     DrawID=0 → cS → program=0 Shape
            //     DrawID=1 → cF → program=1 PathFill
            //   所以“不同 mesh”不是 command 字段，而是 constant buffer 数据。
            //
            // NOTE: push constants 不是 per indirect command：
            //   push(pcS) 只设置一次当前 draw state，pcS 是 CommandConstant[] 的
            //   基地址；不能 push(pcS)+push(pcF) 两次再 count=2，否则两条都会用
            //   最后的 pcF，cmd0 的 Shape 就丢了。
            //
            // NOTE: 不同 mesh 可以压进一条 indirect，前提是共享：
            //   - 同一个 pipeline/pipelineLayout/descriptorSet；
            //   - 同一个 index buffer binding/type，所有索引都在里面；
            //   - 同一个 vertex buffer binding/format；
            //   - 每 command 的 program/address 放在 shader 可读 buffer 里，
            //     用 gl_DrawIDARB 选。
            //   VkDrawIndexedIndirectCommand 只带 5 个 draw 参数，
            //   不带 program/pipeline/descriptor/push constants。
            //   如果不同 mesh 需要换 pipeline/blend/depth/descriptor，
            //   就不能压进这一条，必须分开 bind。
            //
            // NOTE: 最终画面：黄色矩形 = Shape(hS,cS)；蓝色三角形 = PathFill(hF,cF)。
            //   TRIANGLE_LIST 的 primitive 数 = floor(indexCount/3)，indexCount=6
            //   表示两个 carrier 三角形；PathFill 的 path 只有一个三角形，
            //   fragment shader 把 path 外的片元丢掉，所以最终只看到 1 个三角形。
            //   可见三角形 ≠ draw 只有 3 个顶点；indexCount 仍可为 6。
            // ────────────────────────────────────────────────────────────────
            {
                const VkRect2D area = tile(21);
                const VkViewport vp = full_vp;
                const std::uint32_t hS =
                    add_shape(ui_dao::sdf_shape(half(area, false), view, 16.0F,
                                                glm::vec4(0.85F, 0.65F, 0.20F, 1.0F)));
                // 注意：hS 是 Shape 数据；矩形 carrier 是 quad，4 个唯一顶点，
                // 但 indexed draw 仍要 6 个索引（两个三角形）才能画完整矩形。
                // cS 不是顶点，也不是顶点数组；它是 8 字节 CommandConstant
                // {program=0, address_offset=hS}，只告诉 shader 去哪里取 Shape。
                const std::array<glm::vec2, 3> tri = {
                    glm::vec2{0.5F, 0.0F}, glm::vec2{0.0F, 1.0F}, glm::vec2{1.0F, 1.0F}};
                const std::uint32_t hF =
                    add_fill(ui_dao::path_fill(half(area, true), view, std::span{tri},
                                               glm::vec4(0.20F, 0.65F, 0.95F, 1.0F)));
                const std::uint32_t cS = add_const(0u, hS);
                const std::uint32_t cF = add_const(1u, hF);
                // NOTE: cmd0/cmd1 的 5 个 draw 参数故意完全一样：
                //   indexCount=6, instanceCount=1, firstIndex=0,
                //   vertexOffset=0, firstInstance=0
                // 它们不携带 program/mesh；区别只在 cS/cF 两个 CommandConstant，
                // 由 shader 的 gl_DrawIDARB 选择。这样一条 indirect 才能携带
                // “不同 mesh”，而不是靠改 VkDrawIndexedIndirectCommand。
                const VkDrawIndexedIndirectCommand cmd0{.indexCount = 6,
                                                        .instanceCount = 1,
                                                        .firstIndex = 0,
                                                        .vertexOffset = 0,
                                                        .firstInstance = 0};
                const VkDrawIndexedIndirectCommand cmd1{.indexCount = 6,
                                                        .instanceCount = 1,
                                                        .firstIndex = 0,
                                                        .vertexOffset = 0,
                                                        .firstInstance = 0};
                // [12] 已经用了 offset0；[13] 的 ind 是 20，cmd1 在 40。
                // 一条 count=2 + stride=20 会读 ind、ind+20 两条。
                const VkDeviceSize ind = add_indirect_indexed(cmd0);
                add_indirect_indexed(cmd1);
                const shader_data::PushData pcS = pc_at(cS, 13u);

                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *mainCtx.pipelineTransparentUI);
                commandBuffer.bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                 *pipelineLayout, 0, 1, &descriptorSet, 0,
                                                 nullptr);
                commandBuffer.setViewport(0, std::span{&vp, 1});
                commandBuffer.setScissor(0, std::span{&area, 1});
                commandBuffer.bindIndexBuffer(sb.globalIndexBuffer.buffer.buffer(), 0,
                                              VK_INDEX_TYPE_UINT32);

                // 唯一形态：一条 C API 承载两条 command，gl_DrawID 选 program。
                // 隐变量：
                //   命令0（ind）   DrawID=0, Instance=0, BaseVertex=0,
                //                  Vertex=0,1,2,0,2,3, PrimitiveID=0/1
                //   命令1（ind+20）DrawID=1, Instance=0, BaseVertex=0,
                //                  Vertex=0,1,2,0,2,3, PrimitiveID=0/1
                // 两条 command 的 draw 参数相同，唯一区别是 shader 用 DrawID
                // 取的 CommandConstant（cS/cF）。
                // NOTE: 这里只 push 一次 pcS；pcS 是 CommandConstant[] 的基地址。
                //   不能 push 两个不同的 CommandConstant 再 count=2，因为 push
                //   是 draw state，不是 per-command；第二次 push 会覆盖第一条。
                //   两条 command 的 program 由 shader 的 gl_DrawIDARB 选：
                //     draw 0 → cS → Shape，draw 1 → cF → PathFill。
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(pcS), &pcS);

                // NOTE: 一个drawIndexedIndirect.绘制多个图元,图像位于: [comments/ability3-6.png]
                commandBuffer.drawIndexedIndirect(sb.indirectDrawBuffer.buffer.buffer(),
                                                  ind, 2,
                                                  sizeof(VkDrawIndexedIndirectCommand));

                /*
                NOTE: “const 更新”靠的是 gl_DrawIDARB，不是 gl_InstanceIndex。
                CommandConstant cc = cmdConsts.consts[gl_DrawIDARB];   // ← 用 DrawID 选常量
                uint64_t heapBaseStart = pc.dataAddress + cc.address_offset;
                instancePtr = heapBaseStart + gl_InstanceIndex * SHAPE_SIZE; // ← 用 Instance 选行

                command	gl_DrawIDARB	gl_InstanceIndex	读到的常量	    program	        heap 基址
                cmd0	0	            0	                consts[0] = cS	0 (Shape)	    hS
                cmd1	1	            0	                consts[1] = cF	1 (PathFill)	hF

                NOTE: 为什么这里必须用 DrawID，不能用 Instance: 因为 Shape 和 PathFill 是两种不同的 mesh，stride 不同、heap 段也不同
                */
            }

            // ────────────────────────────────────────────────────────────────
            // [14]-[16] 保留的是 API 特例，不是“多画几个图元”：
            //   [14] firstInstance=1 + instanceCount=3 的实例范围；
            //   [15] BindVertexBuffers 在无 vertex input pipeline 上的合法 no-op；
            //   [16] dynamic state/descriptor/index 在 BindPipeline 之前的顺序。
            // [17]/[18] 已删：depth/stencil pass/fail 已由 [08-1..4] 覆盖。
            // ────────────────────────────────────────────────────────────────

            // NOTE: drawIndexed 实现多实例绘制. 同一个mesh 哦.不能修改偏移量的.更新的是gl_InstanceIndex
            // ────────────────────────────────────────────────────────────────
            // [14] 实例范围：firstInstance=1, instanceCount=3
            // C API: SetViewport → SetScissor → PushConstants(一次) →
            //        DrawIndexed(6,3,0,0,1)
            // 排列组合：一条 draw，实例范围 [1,3]；和 [01]（instanceCount=4,
            //           firstInstance=0）、[02]（两条 draw, firstInstance=0/1）不同。
            // 分离：不需要。firstInstance 是实例数组的起始行，instanceCount 是
            //       覆盖的连续行数；一条 draw 就能画“从第 1 行起的 3 个实例”。
            //
            // NOTE: 这里故意不放 vkCmdPipelineBarrier2。未启用
            //       dynamicRenderingLocalRead / shaderTileImage* 时，
            //       VUID-vkCmdPipelineBarrier2-None-09553 禁止在 render scope 内
            //       调用 vkCmdPipelineBarrier2；所以本语料的 barrier 全部放在
            //       scope 之间（见 [17] / [20] / [21]）。
            // ────────────────────────────────────────────────────────────────
            {
                const VkRect2D area = tile(22);
                const VkViewport vp = full_vp;
                std::uint32_t heap = 0;
                for (std::uint32_t i = 0; i < 4; ++i)
                {
                    auto s = ui_dao::sdf_shape(
                        ui_dao::rect_px{static_cast<float>(area.offset.x) + 10.0F +
                                            static_cast<float>(i) * 40.0F,
                                        static_cast<float>(area.offset.y) + 30.0F, 36.0F,
                                        100.0F},
                        view, 10.0F,
                        glm::vec4(0.15F + 0.20F * static_cast<float>(i), 0.45F,
                                  0.95F - 0.18F * static_cast<float>(i), 1.0F));
                    const std::uint32_t off = add_shape(s);
                    if (i == 0)
                        heap = off;
                }
                const std::uint32_t cst = add_const(0u, heap);
                const shader_data::PushData pc = pc_at(cst, 14u);

                commandBuffer.setViewport(0, std::span{&vp, 1});
                commandBuffer.setScissor(0, std::span{&area, 1});
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(pc), &pc);

                // ────────────────────────────────────────────────────────────────
                // [14] 一条 draw 的实例范围：firstInstance=1, instanceCount=3
                // C API: SetViewport → SetScissor → PushConstants(一次) →
                //        DrawIndexed(6, 3, 0, 0, 1)
                // 排列组合：一条 draw，实例范围 [1,3]；和 [01]（instanceCount=4,
                //           firstInstance=0）、[02]（两条 draw, firstInstance=0/1）不同。
                // 分离：不需要。firstInstance 是实例数组的起始行，instanceCount 是
                //       覆盖的连续行数；一条 draw 就能画“从第 1 行起的 3 个实例”。
                //
                // NOTE: 隐变量 vs [02] 的两条 draw：
                //   gl_DrawIDARB   = 0                    ← 不变，都是直接 draw
                //   gl_InstanceIndex = 1, 2, 3            ← 变了：firstInstance=1，
                //                                            [02] 是 0 / 1
                //   gl_BaseVertexARB = 0                  ← 不变，vertexOffset=0
                //   gl_VertexIndex  = [0,1,2,0,2,3] × 3   ← 每个实例都一样，
                //                                            只是实例数 2→3
                //   gl_PrimitiveID  = 0/1, 0/1, 0/1      ← 每个实例从 0 重置，
                //                                            不是跨实例唯一 id
                //
                // NOTE: 关键后果——第 1 个 Shape 被跳过：
                //   heap 是 Shape[0] 的偏移（i==0 时记录）；
                //   shader 用 instancePtr = heap + gl_InstanceIndex * SHAPE_SIZE，
                //   gl_InstanceIndex = 1/2/3 →
                //     实例 1 → heap+1*248 = Shape[1]
                //     实例 2 → heap+2*248 = Shape[2]
                //     实例 3 → heap+3*248 = Shape[3]
                //   → Shape[0] 不画；视觉上只有 3 个矩形，从 x+50 起，
                //     第 1 个（x+10）不出现。
                //   firstInstance 不是“跳过前 1 个再画 3 个”，而是“gl_InstanceIndex
                //   从 1 起算，所以读 heap 的第 2 行起”。
                //
                // NOTE: 这里故意不放 vkCmdPipelineBarrier2。未启用
                //       dynamicRenderingLocalRead / shaderTileImage* 时，
                //       VUID-vkCmdPipelineBarrier2-None-09553 禁止在 render scope 内
                //       调用 vkCmdPipelineBarrier2；所以本语料的 barrier 全部放在
                //       scope 之间（见 [20] / [21]）。
                // ────────────────────────────────────────────────────────────────
                // NOTE: 图像位于: [comments/ability3-7.png]
                commandBuffer.drawIndexed(6, 3, 0, 0, 1);
                //                        │  │  │  │  │
                //                        │  │  │  │  └─ firstInstance = 1
                //                        │  │  │  └──── vertexOffset  = 0
                //                        │  │  └─────── firstIndex    = 0
                //                        │  └────────── instanceCount = 3
                //                        └───────────── indexCount    = 6

                // NOTE: instanceCount 让 gl_InstanceIndex 从[0,instanceCount)变化. heap 也要保证不被变化越界
            }

            // NOTE: bindVertexBuffers 合法,但是可以不读.也就是 BDA 和 传统绑定数据 是不冲突的,可以对 const来分支;可编程
            // ────────────────────────────────────────────────────────────────
            // [15] 正交 state：BindVertexBuffers + Noise program
            // C API: BindVertexBuffers → BindPipeline(transparent) →
            //        BindDescriptorSets → SetViewport → SetScissor →
            //        BindIndexBuffer → PushConstants → DrawIndexed
            //
            // NOTE: 本 case 只有两个新点，其余（pipeline/set/viewport/scissor/
            //       index/push/draw）都是前面重复的，不再展开。
            //
            // ① BindVertexBuffers 是 [15] 唯一新增的 C API，但它是 dead state：
            //    pipeline 的 vertexInputState = {}，vertex shader 没有
            //    layout(location=N) in ... 声明；顶点永远从 BDA 读：
            //        VertexBuffer vertBuf = VertexBuffer(pc.vertexAddress);
            //        VertexData v = vertBuf.vertices[gl_VertexIndex];
            //    所以 vkCmdBindVertexBuffers 绑定合法，但 shader 不读；
            //    它只是状态流里的一条命令，不需要为它单独发 draw。
            //
            // ② program = 6（NoiseQuad）前面没出现过：
            //    switch(program) 在 VS/FS 都进入 PROGRAM_NOISE 分支；
            //    VS 读 NoiseQuadBuffer(instancePtr).quads[0]，
            //       fragColor = inst.color_a，pos = v.pos（v 仍是 BDA 顶点）；
            //    FS 用 inst.frequency / seed / octaves / color_a / color_b 算噪声。
            //    carrier 顶点仍是同一个 quad 的 6 个索引 [0,1,2,0,2,3]；
            //    几何和 program 解耦：顶点路径不变，变的是实例行 + 片元算法。
            //
            // NOTE: “着色器走不同分支”只对实例数据和片元算法成立；
            //       顶点数据路径没有分支，所有 program 共用 BDA 顶点缓冲。
            //       这正是 BindVertexBuffers 在这里是 dead state 的原因。
            // ────────────────────────────────────────────────────────────────
            {
                const VkRect2D area = tile(23);
                const VkViewport vp = full_vp;
                const std::uint32_t heap = add_noise(ui_dao::noise(
                    inset(area, 26.0F), view, glm::vec4(0.15F, 0.20F, 0.35F, 1.0F),
                    glm::vec4(0.95F, 0.55F, 0.20F, 1.0F), 6.0F, 0.37F, 3, 14.0F));
                const std::uint32_t cst = add_const(6u, heap);
                const shader_data::PushData pc = pc_at(cst, 15u);
                const VkBuffer vertexBuffer = sb.globalVertexBuffer.buffer.buffer();
                const VkDeviceSize vertexOffset = 0;

                commandBuffer.bindVertexBuffers(0, 1, vertexBuffer, vertexOffset);
                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *mainCtx.pipelineTransparentUI);
                commandBuffer.bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                 *pipelineLayout, 0, 1, &descriptorSet, 0,
                                                 nullptr);
                commandBuffer.setViewport(0, std::span{&vp, 1});
                commandBuffer.setScissor(0, std::span{&area, 1});
                commandBuffer.bindIndexBuffer(sb.globalIndexBuffer.buffer.buffer(), 0,
                                              VK_INDEX_TYPE_UINT32);
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(pc), &pc);
                // NOTE: 图像位于: [comments/ability3-8.png]
                commandBuffer.drawIndexed(6, 1, 0, 0, 0); //还是走 heap + 索引+图元类型
            }

            // ────────────────────────────────────────────────────────────────
            // [16] 顺序变体：BindPipeline 从第一个位置挪到倒数第三
            // C API: SetViewport → SetScissor → BindDescriptorSets →
            //        BindIndexBuffer → BindPipeline(opaqueUI) →
            //        PushConstants → DrawIndexed
            //
            // NOTE: 和 [00] 的 API 种类完全相同，唯一差别是 BindPipeline 的位置：
            //   [00]: BindPipeline → BindDescriptorSets → SetViewport → SetScissor →
            //         BindIndexBuffer → PushConstants → DrawIndexed
            //   [16]: SetViewport → SetScissor → BindDescriptorSets → BindIndexBuffer →
            //         BindPipeline → PushConstants → DrawIndexed
            //   这里要证明：dynamic state / descriptor set / index buffer
            //   不依赖 bindPipeline 的顺序；先设状态、后绑 pipeline 是合法的。
            //
            // NOTE: 合法性的三个前提（缺一不可）：
            //   ① 前后 pipeline 共用同一个 *pipelineLayout：
            //      [15] 结束时绑的是 pipelineTransparentUI，[16] 最后绑的是
            //      pipelineOpaqueUI，两者都是 makePipeline(..., *pipelineLayout)。
            //      layout 兼容，bindDescriptorSets 绑的 set 不会被 bindPipeline
            //      invalidate。若 layout 不同，先 bind set 再 bind pipeline
            //      会让 set 失效（VUID 层面错误）。
            //   ② 新 pipeline 声明了 viewport/scissor 为 dynamic：
            //      makePipeline 的 dynamicState 里有 VK_DYNAMIC_STATE_VIEWPORT /
            //      VK_DYNAMIC_STATE_SCISSOR，所以 bindPipeline 不会覆盖之前
            //      setViewport/setScissor 的值。若声明为 static，bindPipeline
            //      会用 pipeline 创建时的静态值，之前的设置失效。
            //   ③ index buffer 绑定独立于 pipeline：
            //      vkCmdBindIndexBuffer 是独立 state，不受 bindPipeline 影响，
            //      不需要在 pipeline 后重绑。
            //
            // NOTE: 和 [06] 的对照（两个方向相反的实验）：
            //   [06] 省略 bindPipeline/bindSet/bindIndex，只改 viewport
            //        → 证明状态“粘滞”：不重绑，旧状态仍有效；
            //   [16] 把 bindPipeline 挪到最后，其他状态先设
            //        → 证明状态“可重排”：先设后绑，只要兼容就有效。
            //   两者合起来：pipeline 绑定与 dynamic state / set / index
            //   是松耦合的，顺序可换，前提是 layout 兼容 + dynamic state
            //   声明正确。
            //
            // NOTE: 对 command 层的意义：这是“重排/去重”的合法性依据。
            //   如果为了合批把 bindPipeline 从“每条 draw 前”挪到“一段 draw 前”，
            //   或者把 bindDescriptorSets 提前，必须保证上面 ①②③ 都成立；
            //   [16] 是“这些条件满足时顺序可以任意”的最小证明。
            // ────────────────────────────────────────────────────────────────
            {
                const VkRect2D area = tile(24);
                const VkViewport vp = full_vp;
                const VkRect2D sc = area;
                const std::uint32_t heap =
                    add_shape(ui_dao::sdf_shape(inset(area, 24.0F), view, 18.0F,
                                                glm::vec4(0.55F, 0.95F, 0.55F, 1.0F)));
                const std::uint32_t cst = add_const(0u, heap);
                const shader_data::PushData pc = pc_at(cst, 16u);

                commandBuffer.setViewport(0, std::span{&vp, 1});
                commandBuffer.setScissor(0, std::span{&sc, 1});
                commandBuffer.bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                 *pipelineLayout, 0, 1, &descriptorSet, 0,
                                                 nullptr);
                commandBuffer.bindIndexBuffer(sb.globalIndexBuffer.buffer.buffer(), 0,
                                              VK_INDEX_TYPE_UINT32);
                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *mainCtx.pipelineOpaqueUI);
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(pc), &pc);
                commandBuffer.drawIndexed(6, 1, 0, 0, 0);
            }

            // [17]/[18] removed: covered by [08-1..4];
            // B-group [17]/[18] are scope/renderArea cases and stay.

            //NOTE: A 组结束：这是第一个 endRendering，但绝不是唯一一个。
            commandBuffer.endRendering();

            // ════════════════════════════════════════════════════════════════
            // B 组：scope 边界 / 多次 endRendering / suspend-resume / copy
            //   每个 case 自己带 begin/end；scope 之间没有隐式顺序，所以先发
            //   memory barrier（case [20] 的 copy 用 image barrier 承担同一职责）。
            //   NOTE: 这些 barrier 必须放在 scope 外。未启用
            //   dynamicRenderingLocalRead / shaderTileImage* 时，
            //   VUID-vkCmdPipelineBarrier2-None-09553 禁止在 render scope 内调用
            //   vkCmdPipelineBarrier2；scope 内只允许 draw/state/query 等。
            // ════════════════════════════════════════════════════════════════

            // ────────────────────────────────────────────────────────────────
            // [17] 两次 begin/end，同一个 target：multiple endRendering
            // C API: PipelineBarrier2 → BeginRendering(LOAD) → BindPipeline →
            //        BindDescriptorSets → SetViewport → SetScissor → BindIndexBuffer →
            //        Push(A) → DrawIndexed → EndRendering → PipelineBarrier2 →
            //        BeginRendering(LOAD) → Push(B) → DrawIndexed → EndRendering
            // 排列组合：两个 scope；第二次 begin 不再重绑 pipeline/set/index/viewport/
            //           scissor，证明 beginRendering 不是状态重置。
            // 分离：必要。两个 render pass instance 之间没有隐式顺序，必须显式 barrier；
            //       这是用户要求的“多个 endRendering”用例。
            // ────────────────────────────────────────────────────────────────
            {
                const VkRect2D area = tile(17);
                const VkViewport vp = full_vp;
                const std::uint32_t h0 =
                    add_shape(ui_dao::sdf_shape(half(area, false), view, 16.0F,
                                                glm::vec4(0.95F, 0.45F, 0.15F, 1.0F)));
                const std::uint32_t h1 = add_shape(ui_dao::sdf_shape(
                    half(area, true), view, 16.0F, glm::vec4(0.20F, 0.75F, 0.95F, 1.0F)));
                const std::uint32_t c0 = add_const(0u, h0);
                const std::uint32_t c1 = add_const(0u, h1);
                const shader_data::PushData pc0 = pc_at(c0, 17u);
                const shader_data::PushData pc1 = pc_at(c1, 17u);

                commandBuffer.pipelineBarrier2(scope_dep);
                commandBuffer.beginRendering(info_load);
                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *mainCtx.pipelineOpaqueUI);
                commandBuffer.bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                 *pipelineLayout, 0, 1, &descriptorSet, 0,
                                                 nullptr);
                commandBuffer.setViewport(0, std::span{&vp, 1});
                commandBuffer.setScissor(0, std::span{&area, 1});
                commandBuffer.bindIndexBuffer(sb.globalIndexBuffer.buffer.buffer(), 0,
                                              VK_INDEX_TYPE_UINT32);
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(pc0), &pc0);
                commandBuffer.drawIndexed(6, 1, 0, 0, 0);
                commandBuffer.endRendering();

                commandBuffer.pipelineBarrier2(scope_dep);
                commandBuffer.beginRendering(info_load);
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(pc1), &pc1);
                commandBuffer.drawIndexed(6, 1, 0, 0, 0);
                commandBuffer.endRendering();
            }
#if 0
            // ────────────────────────────────────────────────────────────────
            // [18] scope 的 renderArea 变体：只对 tile 做 CLEAR，再画
            // C API: PipelineBarrier2 → BeginRendering(renderArea=tile, loadOp=CLEAR) →
            //        BindPipeline → BindDescriptorSets → SetViewport → SetScissor →
            //        BindIndexBuffer → PushConstants → DrawIndexed → EndRendering
            // 排列组合：VkRenderingInfo.renderArea 从全屏变成 tile；clear 只作用于
            //           这个 scope 的 renderArea，不是 scissor。
            // 分离：这是 scope 级变体，不是 draw 间分离；它决定 load/store 的作用域。
            // ────────────────────────────────────────────────────────────────
            {
                const VkRect2D area = tile(18);
                const VkViewport vp = full_vp;
                const std::uint32_t heap = add_shape(ui_dao::sdf_shape(
                    inset(area, 24.0F), view, 18.0F,
                    glm::vec4(0.65F, 0.45F, 0.95F, 1.0F)));
                const std::uint32_t cst = add_const(0u, heap);
                const shader_data::PushData pc = pc_at(cst, 18u);
                VkRenderingInfo info_area = info_clear;
                info_area.renderArea = area;

                commandBuffer.pipelineBarrier2(scope_dep);
                commandBuffer.beginRendering(info_area);
                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *mainCtx.pipelineOpaqueUI);
                commandBuffer.bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                 *pipelineLayout, 0, 1, &descriptorSet,
                                                 0, nullptr);
                commandBuffer.setViewport(0, std::span{&vp, 1});
                commandBuffer.setScissor(0, std::span{&area, 1});
                commandBuffer.bindIndexBuffer(sb.globalIndexBuffer.buffer.buffer(), 0,
                                              VK_INDEX_TYPE_UINT32);
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(pc), &pc);
                commandBuffer.drawIndexed(6, 1, 0, 0, 0);
                commandBuffer.endRendering();
            }

            // ────────────────────────────────────────────────────────────────
            // [19] suspend → resume：同一个 scope 被拆成两次 begin/end
            // C API: PipelineBarrier2 → BeginRendering(SUSPENDING) → BindPipeline →
            //        BindDescriptorSets → SetViewport → SetScissor → BindIndexBuffer →
            //        Push(A) → DrawIndexed → EndRendering → BeginRendering(RESUMING) →
            //        BindPipeline → BindDescriptorSets → SetViewport → SetScissor →
            //        BindIndexBuffer → Push(B) → DrawIndexed → EndRendering
            // 排列组合：第一次 begin 带 SUSPENDING，第二次带 RESUMING；两个
            //           VkRenderingInfo 除 flags 外必须一致。
            // 分离：必要。suspend 与 resume 之间不能插入任何 action/sync 命令，
            //       也不能有别的 scope；这是动态渲染的高级拓扑。
            // ────────────────────────────────────────────────────────────────
            {
                const VkRect2D area = tile(19);
                const VkViewport vp = full_vp;
                const std::uint32_t h0 = add_shape(ui_dao::sdf_shape(
                    half(area, false), view, 16.0F, glm::vec4(0.95F, 0.85F, 0.55F, 1.0F)));
                const std::uint32_t h1 = add_shape(ui_dao::sdf_shape(
                    half(area, true), view, 16.0F, glm::vec4(0.35F, 0.85F, 0.45F, 1.0F)));
                const std::uint32_t c0 = add_const(0u, h0);
                const std::uint32_t c1 = add_const(0u, h1);
                const shader_data::PushData pc0 = pc_at(c0, 19u);
                const shader_data::PushData pc1 = pc_at(c1, 19u);

                commandBuffer.pipelineBarrier2(scope_dep);
                commandBuffer.beginRendering(info_suspend);
                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *mainCtx.pipelineOpaqueUI);
                commandBuffer.bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                 *pipelineLayout, 0, 1, &descriptorSet,
                                                 0, nullptr);
                commandBuffer.setViewport(0, std::span{&vp, 1});
                commandBuffer.setScissor(0, std::span{&area, 1});
                commandBuffer.bindIndexBuffer(sb.globalIndexBuffer.buffer.buffer(), 0,
                                              VK_INDEX_TYPE_UINT32);
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(pc0), &pc0);
                commandBuffer.drawIndexed(6, 1, 0, 0, 0);
                commandBuffer.endRendering();

                // 这里不能有 barrier / copy / 任何别的命令，只能 begin(RESUMING)。
                commandBuffer.beginRendering(info_resume);
                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *mainCtx.pipelineOpaqueUI);
                commandBuffer.bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                 *pipelineLayout, 0, 1, &descriptorSet,
                                                 0, nullptr);
                commandBuffer.setViewport(0, std::span{&vp, 1});
                commandBuffer.setScissor(0, std::span{&area, 1});
                commandBuffer.bindIndexBuffer(sb.globalIndexBuffer.buffer.buffer(), 0,
                                              VK_INDEX_TYPE_UINT32);
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(pc1), &pc1);
                commandBuffer.drawIndexed(6, 1, 0, 0, 0);
                commandBuffer.endRendering();
            }

            // ────────────────────────────────────────────────────────────────
            // [20] scope 外 copy：COLOR_ATTACHMENT → TRANSFER_SRC → copy → 回来
            // C API: Transition(resolve, COLOR→TRANSFER_SRC) →
            //        CopyImageToBuffer(1×1) →
            //        Transition(resolve, TRANSFER_SRC→COLOR) → PipelineBarrier2 →
            //        BeginRendering(LOAD) → BindPipeline → BindDescriptorSets →
            //        SetViewport → SetScissor → BindIndexBuffer → PushConstants →
            //        DrawIndexed → EndRendering
            // 排列组合：copy 是 render scope 外的工作；两条 layout transition 夹住它。
            // 分离：必要。copy / layout transition 都只能 Outside render scope；
            //       这是 picking/readback 的典型边界。
            // ────────────────────────────────────────────────────────────────
            {
                const VkRect2D area = tile(20);
                const VkViewport vp = full_vp;
                const std::uint32_t heap = add_shape(ui_dao::sdf_shape(
                    inset(area, 24.0F), view, 18.0F,
                    glm::vec4(0.55F, 0.95F, 0.95F, 1.0F)));
                const std::uint32_t cst = add_const(0u, heap);
                const shader_data::PushData pc = pc_at(cst, 20u);

                my_render::transition_image_layout(
                    commandBuffer,
                    my_render::image_info{
                        .image = resolveImage,
                        .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .src = {.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                .access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                .stage_mask =
                                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT},
                        .dst = {.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                .access_mask = VK_ACCESS_2_TRANSFER_READ_BIT,
                                .stage_mask = VK_PIPELINE_STAGE_2_TRANSFER_BIT}});

                const VkBufferImageCopy region{
                    .bufferOffset = 0,
                    .bufferRowLength = 0,
                    .bufferImageHeight = 0,
                    .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                    .imageOffset =
                        {area.offset.x +
                             static_cast<std::int32_t>(area.extent.width / 2),
                         area.offset.y +
                             static_cast<std::int32_t>(area.extent.height / 2),
                         0},
                    .imageExtent = {1, 1, 1}};
                commandBuffer.copyImageToBuffer(
                    resolveImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    pickCtx.frames[currentFrame].buffer(), std::span{&region, 1});

                my_render::transition_image_layout(
                    commandBuffer,
                    my_render::image_info{
                        .image = resolveImage,
                        .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .src = {.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                .access_mask = VK_ACCESS_2_TRANSFER_READ_BIT,
                                .stage_mask = VK_PIPELINE_STAGE_2_TRANSFER_BIT},
                        .dst = {.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                .access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                .stage_mask =
                                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT}});

                commandBuffer.pipelineBarrier2(scope_dep);
                commandBuffer.beginRendering(info_load);
                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *mainCtx.pipelineOpaqueUI);
                commandBuffer.bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                 *pipelineLayout, 0, 1, &descriptorSet,
                                                 0, nullptr);
                commandBuffer.setViewport(0, std::span{&vp, 1});
                commandBuffer.setScissor(0, std::span{&area, 1});
                commandBuffer.bindIndexBuffer(sb.globalIndexBuffer.buffer.buffer(), 0,
                                              VK_INDEX_TYPE_UINT32);
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(pc), &pc);
                commandBuffer.drawIndexed(6, 1, 0, 0, 0);
                commandBuffer.endRendering();
            }

            // ────────────────────────────────────────────────────────────────
            // [21] 非索引 indirect：vkCmdDrawIndirect，PathFill program
            // C API: PipelineBarrier2 → BeginRendering(LOAD) →
            //        BindPipeline(transparent) → BindDescriptorSets → SetViewport →
            //        SetScissor → PushConstants → DrawIndirect(count=1) → EndRendering
            // 排列组合：非索引 indirect；没有 BindIndexBuffer 参与这条 draw；
            //           firstVertex=8 指向 non-indexed 顶点数组。
            // 分离：C API 层不需要为“非索引”再发 BindIndexBuffer；它和 [12] 的
            //       indexed indirect 是两条不同的提交路径。
            // ────────────────────────────────────────────────────────────────
            {
                const VkRect2D area = tile(21);
                const VkViewport vp = full_vp;
                const std::array<glm::vec2, 3> tri = {
                    glm::vec2{0.5F, 0.0F}, glm::vec2{0.0F, 1.0F},
                    glm::vec2{1.0F, 1.0F}};
                const std::uint32_t heap = add_fill(ui_dao::path_fill(
                    inset(area, 26.0F), view, std::span{tri},
                    glm::vec4(0.35F, 0.95F, 0.45F, 1.0F)));
                const std::uint32_t cst = add_const(1u, heap);
                const VkDrawIndirectCommand cmd{
                    .vertexCount = 6,
                    .instanceCount = 1,
                    .firstVertex = kNonIndexedVertex,
                    .firstInstance = 0};
                const VkDeviceSize ind = add_indirect(cmd);
                const shader_data::PushData pc = pc_at(cst, 21u);

                commandBuffer.pipelineBarrier2(scope_dep);
                commandBuffer.beginRendering(info_load);
                commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           *mainCtx.pipelineTransparentUI);
                commandBuffer.bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                 *pipelineLayout, 0, 1, &descriptorSet,
                                                 0, nullptr);
                commandBuffer.setViewport(0, std::span{&vp, 1});
                commandBuffer.setScissor(0, std::span{&area, 1});
                commandBuffer.pushConstants(*pipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT |
                                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                            0, sizeof(pc), &pc);
                commandBuffer.functions().vkCmdDrawIndirect(
                    *commandBuffer, sb.indirectDrawBuffer.buffer.buffer(), ind, 1,
                    sizeof(VkDrawIndirectCommand));
                commandBuffer.endRendering();
            }
#endif
            // 最后：swapchain image 从 COLOR_ATTACHMENT_OPTIMAL → PRESENT_SRC。
            my_render::transition_image_layout(
                commandBuffer,
                my_render::image_info{
                    .image = image,
                    .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .src = {.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            .access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            .stage_mask =
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT},
                    .dst = {.layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                            .access_mask = VK_ACCESS_2_NONE,
                            .stage_mask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT}});
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
