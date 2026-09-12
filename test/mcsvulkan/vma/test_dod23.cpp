#include <any>
#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
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

using mcs::vulkan::ecs::gen_soa_aggregate;
using mcs::vulkan::ecs::gen_soa_struct;
using mcs::vulkan::ecs::soa_vector;
using mcs::vulkan::ecs::proxy_value;

using mcs::vulkan::task::make_task;
using mcs::vulkan::task::init_task;
using mcs::vulkan::task::schedulable_task;

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
// picking_result：与 R32G32B32A32_UINT 附件 + frag 的 uvec4 输出严格对齐（16B）
// xy = object_key(type_id, entity_index) 外键；z = primitive_id；w = hover_fn（池实体下标，0xFFFFFFFF = 未绑定）
struct picking_result
{
    object_key key;        // outPicking.xy
    uint32_t primitive_id; // outPicking.z
    uint32_t hover_fn;     // outPicking.w：hover 函数池实体下标（0xFFFFFFFF = 未绑定）
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
    // （GLSL: vertex/data/commandConstants/cameraIndex，Glyph 不需要 attributeAddress）
    uint64_t vertexAddress;           // 全局顶点缓冲区地址
    uint64_t instanceAddress;         // 全局实例堆地址（shader: dataAddress）
    uint64_t commandConstantsAddress; // 命令常量缓冲区地址
    uint32_t cameraIndex;             // 0: 3D, 1: UI
    bool operator==(const PushData &o) const noexcept = default;
    constexpr bool valid() const noexcept
    {
        return vertexAddress != 0 && instanceAddress != 0 && commandConstantsAddress != 0;
    }
};
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
        using hover_callback_t =
            std::move_only_function<void(picking_result, bool enter) noexcept>;
        std::vector<hover_callback_t> hover_fns;

        // 绑定一个可共享的 hover 函数，返回池实体下标（0 也是合法下标）
        uint32_t bind(
            std::move_only_function<void(picking_result, bool enter) noexcept> fn)
        {
            hover_fns.push_back(std::move(fn));
            return hover_fns.size() - 1;
        }
        // 按实体下标调用（0xFFFFFFFF = 未绑定；已释放 = 不调用）
        void call(uint32_t entity, const picking_result &r, bool enter) noexcept
        {
            if (entity == ~0U)
                return;
            assert(entity < hover_fns.size());
            auto &fn = hover_fns[entity];
            assert(fn);
            fn(r, enter);
        }
    };

    // 外键状态机：只存当前命中 cur（换新时旧 cur 就是 pre，用来发 leave）
    struct hover_manager
    {
        static constexpr auto hover_leave = false;
        static constexpr auto hover_enter = true;
        picking_result cur{object_key{0xFFFFFFFF, 0}, 0,
                           0}; // 当前命中（key 无效 = 未悬停）

        constexpr void hover(const picking_result &r, hover_pool &pool) noexcept
        {
            static_assert(0xFFFFFFFF == uint32_t{~0U});
            if (cur.key == r.key)
                return; // 同一外键：无动作
            if (cur.key.object_type != 0xFFFFFFFF && cur.hover_fn != ~0U)
                pool.call(cur.hover_fn, cur, hover_leave);
            cur = r;
            if (r.key.object_type != 0xFFFFFFFF && r.hover_fn != ~0U)
                pool.call(r.hover_fn, r, hover_enter);
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
    static constexpr bool equal_scissors(const std::vector<VkRect2D> &a,
                                         const std::vector<VkRect2D> &b) noexcept
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

    static constexpr bool equal_viewports(const std::vector<VkViewport> &a,
                                          const std::vector<VkViewport> &b) noexcept
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
    struct DynamicDrawState
    {
        std::vector<VkRect2D> scissors;
        std::vector<VkViewport> viewports;

        bool operator==(const DynamicDrawState &o) const noexcept
        {
            return equal_scissors(scissors, o.scissors) &&
                   equal_viewports(viewports, o.viewports);
        }
    };

    // ===================== 绘制段：一个动态状态对应一次实际的 drawIndexedIndirect =====================
    struct DrawSegment
    {
        DynamicDrawState dynamic;

        // 段首命令在 pendingDraw->cmds / consts 中的下标
        uint32_t commandStart{0};
        uint32_t commandCount{0};

        // 最终写入 GPU 缓冲后的字节偏移，由 finishDraw() 填充
        uint32_t indirectOffsetStart{0};
        uint32_t constantOffsetStart{0};
    };

    // ===================== 绘制单元：共享静态状态，包含多个段 =====================
    struct DrawUnit
    {
        StaticDrawKey staticKey;
        std::vector<DrawSegment> segments;
        uint32_t totalCommandCount{0};

        constexpr bool valid() const noexcept
        {
            return staticKey.valid() && !segments.empty() && totalCommandCount > 0;
        }
    };

    // ===================== 待提交的绘制数据 =====================
    struct PendingDraw
    {
        StaticDrawKey staticKey;

        // 所有命令（跨段连续存储）
        std::vector<VkDrawIndexedIndirectCommand> cmds;
        std::vector<CommandConstant> consts;

        // 已经固定下来的段
        std::vector<DrawSegment> segments;

        // 当前活动段的起始命令下标
        size_t currentSegmentCmdStart{0};

        // 当前活动段使用的动态状态
        DynamicDrawState activeDynamic;
    };

    // ===================== DrawRecorder（修正版） =====================
    class DrawRecorder
    {
      public:
        // 缓冲区
        BufferResourceWithAddress globalVertexBuffer{};
        BufferResource globalIndexBuffer{};
        BufferResourceWithAddress globalHeapBuffer;
        BufferResource indirectDrawBuffer;
        BufferResourceWithAddress commandConstantsBuffer;

        // 全局字节偏移
        size_t heapOffset = 0;
        size_t indirectOffset = 0;
        size_t constantOffset = 0;

        // 已完成的绘制单元
        std::vector<DrawUnit> draws;

        // 当前静态 / 动态状态
        StaticDrawKey currentStatic{};
        DynamicDrawState currentDynamic;

        // 当前待提交的绘制
        std::optional<PendingDraw> pendingDraw;

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
        }

        // ---------- 每帧重置 ----------
        void reset()
        {
            heapOffset = 0;
            indirectOffset = 0;
            constantOffset = 0;
            draws.clear();
            currentStatic = {};
            currentDynamic = {};
            pendingDraw.reset();
        }

        // ---------- 只结束当前活动段，绝不修改已经记录的段 ----------
        void pushCurrentSegment()
        {
            if (!pendingDraw)
                return;

            const size_t cmdCount =
                pendingDraw->cmds.size() - pendingDraw->currentSegmentCmdStart;

            if (cmdCount > 0)
            {
                DrawSegment seg{};
                seg.dynamic = pendingDraw->activeDynamic;
                seg.commandStart =
                    static_cast<uint32_t>(pendingDraw->currentSegmentCmdStart);
                seg.commandCount = static_cast<uint32_t>(cmdCount);

                pendingDraw->segments.push_back(seg);
            }

            // 下一次命令从当前 cmds.size() 开始
            pendingDraw->currentSegmentCmdStart = pendingDraw->cmds.size();
        }

        // ---------- 结束当前 DrawUnit ----------
        void finishDraw()
        {
            if (!pendingDraw)
                return;

            // 结束最后一个活动段
            pushCurrentSegment();

            if (pendingDraw->cmds.empty())
            {
                pendingDraw.reset();
                return;
            }

            assert(!pendingDraw->segments.empty());

            const size_t cmdBytes =
                pendingDraw->cmds.size() * sizeof(VkDrawIndexedIndirectCommand);
            const size_t constBytes =
                pendingDraw->consts.size() * sizeof(CommandConstant);

            const size_t baseIndirect = indirectOffset;
            const size_t baseConstant = constantOffset;

            // 连续写入所有命令和命令常量
            indirectDrawBuffer.write(baseIndirect, pendingDraw->cmds.data(), cmdBytes);
            commandConstantsBuffer.write(baseConstant, pendingDraw->consts.data(),
                                         constBytes);

            // 根据 commandStart 计算每个段实际字节偏移
            for (auto &seg : pendingDraw->segments)
            {
                seg.indirectOffsetStart = static_cast<uint32_t>(
                    baseIndirect +
                    seg.commandStart * sizeof(VkDrawIndexedIndirectCommand));
                seg.constantOffsetStart = static_cast<uint32_t>(
                    baseConstant + seg.commandStart * sizeof(CommandConstant));
            }

            DrawUnit unit{};
            unit.staticKey = pendingDraw->staticKey;
            unit.segments = std::move(pendingDraw->segments);
            unit.totalCommandCount = static_cast<uint32_t>(pendingDraw->cmds.size());
            draws.push_back(std::move(unit));

            indirectOffset += cmdBytes;
            constantOffset += constBytes;

            pendingDraw.reset();
        }

        // ---------- 静态状态设置 ----------
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

        // ---------- 动态状态设置：先结束旧段，再切换状态 ----------
        void setScissors(const std::vector<VkRect2D> &scissors)
        {
            if (!equal_scissors(currentDynamic.scissors, scissors))
            {
                if (pendingDraw)
                    pushCurrentSegment();

                currentDynamic.scissors = scissors;

                if (pendingDraw)
                    pendingDraw->activeDynamic = currentDynamic;
            }
        }

        void setViewports(const std::vector<VkViewport> &viewports)
        {
            if (!equal_viewports(currentDynamic.viewports, viewports))
            {
                if (pendingDraw)
                    pushCurrentSegment();

                currentDynamic.viewports = viewports;

                if (pendingDraw)
                    pendingDraw->activeDynamic = currentDynamic;
            }
        }

        // ---------- 添加实例数据 ----------
        template <typename T>
        void addInstances(std::span<const T> instances)
        {
            assert(currentStatic.valid());
            const auto &mesh = currentStatic.mesh;

            if (!pendingDraw)
            {
                pendingDraw = PendingDraw{currentStatic, {}, {}, {}, 0, currentDynamic};
            }

            // 写入堆
            const size_t dataOffset = heapOffset;
            globalHeapBuffer.write(heapOffset, instances.data(),
                                   instances.size() * sizeof(T));
            heapOffset += instances.size() * sizeof(T);

            VkDrawIndexedIndirectCommand cmd =
                mesh.getDrawCommand(static_cast<uint32_t>(instances.size()));
            CommandConstant constant{.type_id = T::type_id,
                                     .adddress_offset = static_cast<uint32_t>(dataOffset),
                                     .slot_count = 0};

            // 只与当前活动段的最后一条命令合并
            if (!pendingDraw->cmds.empty() &&
                pendingDraw->currentSegmentCmdStart < pendingDraw->cmds.size())
            {
                auto &lastCmd = pendingDraw->cmds.back();
                auto &lastConst = pendingDraw->consts.back();

                const bool canMerge = lastCmd.indexCount == cmd.indexCount &&
                                      lastCmd.firstIndex == cmd.firstIndex &&
                                      lastCmd.vertexOffset == cmd.vertexOffset &&
                                      lastCmd.firstInstance == cmd.firstInstance &&
                                      lastConst.type_id == constant.type_id;

                if (canMerge)
                {
                    lastCmd.instanceCount += cmd.instanceCount;
                    return; // 合并成功，不新增命令
                }
            }

            pendingDraw->cmds.push_back(cmd);
            pendingDraw->consts.push_back(constant);
        }

        // ---------- 结束录制 ----------
        void end()
        {
            finishDraw();
        }

        // ---------- 提交 Vulkan 命令 ----------
        void doDraw(CommandBufferView cmd)
        {
            assert(!pendingDraw);
            // NOTE: 顶点通过推送常量的设备地址传上了
            cmd.bindIndexBuffer(globalIndexBuffer.buffer.buffer(), 0,
                                VK_INDEX_TYPE_UINT32);

            VkPipeline lastPipeline = VK_NULL_HANDLE; // 缓存当前 CommandBuffer 的管线状态
            for (const auto &du : draws)
            {
                assert(du.valid());

                // 只有管线真正改变时才调用绑定
                if (du.staticKey.pipeline != lastPipeline)
                {
                    cmd.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                     du.staticKey.pipeline);
                    lastPipeline = du.staticKey.pipeline;
                }

                for (const auto &seg : du.segments)
                {
                    cmd.setViewport(0, seg.dynamic.viewports);
                    cmd.setScissor(0, seg.dynamic.scissors);

                    PushData pc = du.staticKey.pushData;
                    pc.commandConstantsAddress =
                        commandConstantsBuffer.address + seg.constantOffsetStart;

                    cmd.pushConstants(du.staticKey.layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                                      sizeof(PushData), &pc);

                    cmd.drawIndexedIndirect(indirectDrawBuffer.buffer.buffer(),
                                            seg.indirectOffsetStart, seg.commandCount,
                                            sizeof(VkDrawIndexedIndirectCommand));
                }
            }
        }
    };

}; // namespace shader_data

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

using model_matrix = camera::transform;
using mcs::vulkan::match;

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
    constexpr int SAMPLER_COUNT = 4; // 创建2个不同的采样器

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
                             .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
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

constexpr auto inputInit(auto &swapchain)
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
        float aspect = swapchain.refImageExtent().width /
                       static_cast<float>(swapchain.refImageExtent().height);
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
                 .pushConstantRanges = {{.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
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
    // 创建4个管线
    auto pipelineOpaque3D = makePipeline(VK_TRUE, VK_TRUE, VK_FALSE);
    auto pipelineTransparent3D = makePipeline(VK_TRUE, VK_FALSE, VK_TRUE);
    auto pipelineOpaqueUI = makePipeline(VK_FALSE, VK_FALSE, VK_FALSE);
    auto pipelineTransparentUI = makePipeline(VK_FALSE, VK_FALSE, VK_TRUE);
    return make_aggregate<"mainPipelineCtx", "pipelineLayout", "pipelineOpaque3D",
                          "pipelineTransparent3D", "pipelineOpaqueUI",
                          "pipelineTransparentUI", "swapchainAttachments",
                          "pickingAttachments">(
        std::move(pipelineLayout), std::move(pipelineOpaque3D),
        std::move(pipelineTransparent3D), std::move(pipelineOpaqueUI),
        std::move(pipelineTransparentUI), std::move(swapchainAttachments),
        std::move(pickingAttachments));
}

// ================= id -> 对象：统一实体访问 =================
// 按 type_id 找到 store，把具体 store 类型交给 visitor（一次实现，取代各处成员遍历分派）
// 已删除 UI 相关的所有 Trait，因此以下函数不再需要，但为保持编译，保留空实现（或可直接删除）
static constexpr auto visit_store(auto &soaCtx, uint32_t type_id, auto &&visitor)
{
    // 无任何 store，直接抛出或返回空
    throw std::out_of_range{"visit_store: no stores available"};
}

static constexpr auto inputController(auto &world, auto &inputCtx, auto &soaCtx)
{
    // 无 UI 组件，无需处理
}
static constexpr auto update(auto &world, auto &inputCtx, auto &soaCtx,
                             uint32_t currentFrame) noexcept
{
    // 无 UI 组件更新
}
static constexpr auto model_update(auto &world, auto &inputCtx, auto &soaCtx) noexcept
{
    // 无 UI 组件更新
}

//diff: [test_dod22] start
constexpr auto &hoverPool() noexcept
{
    static shader_data::hover_pool hoverPool{};
    return hoverPool;
}
namespace ui
{
    // 引入到全局，方便使用
    using static_string = mcs::vulkan::meta::static_string;
    // ============================================================================
    // 哈希与相等函数对象（支持透明查找和指针快速路径）
    // ============================================================================

    struct StaticStringHash
    {
        using is_transparent = void; // 启用异构查找

        std::size_t operator()(const static_string &s) const noexcept
        {
            return std::hash<std::string_view>{}(s.view());
        }
        std::size_t operator()(std::string_view sv) const noexcept
        {
            return std::hash<std::string_view>{}(sv);
        }
    };

    struct StaticStringEqual
    {
        using is_transparent = void; // 启用异构查找

        bool operator()(const static_string &a, const static_string &b) const noexcept
        {
            if (a.value == b.value) // 指针相等快速路径
                return true;
            return a.view() == b.view();
        }
        bool operator()(const static_string &a, std::string_view b) const noexcept
        {
            return a.view() == b;
        }
        bool operator()(std::string_view a, const static_string &b) const noexcept
        {
            return a == b.view();
        }
    };

    // ============================================================================
    // 全局内存池资源（供所有 pmr 分配使用）
    // ============================================================================
    inline std::pmr::synchronized_pool_resource globalMemoryPool;
    inline std::pmr::memory_resource *nodeResource = &globalMemoryPool;

    // ============================================================================
    // pmr 唯一所有权指针封装（类型擦除删除器）
    // ============================================================================

    struct PmrDeleter
    {
        using destroy_fn = void (*)(void *, std::pmr::memory_resource *);
        destroy_fn destroy{nullptr};
        std::pmr::memory_resource *resource{nodeResource};

        PmrDeleter() = default;
        PmrDeleter(destroy_fn fn, std::pmr::memory_resource *r) : destroy(fn), resource(r)
        {
        }

        void operator()(void *p) const noexcept
        {
            if (p && destroy)
            {
                destroy(p, resource);
            }
        }
    };

    template <typename T>
    PmrDeleter make_pmr_deleter(std::pmr::memory_resource *r)
    {
        return PmrDeleter{
            [](void *p, std::pmr::memory_resource *r) {
                auto typed = static_cast<T *>(p);
                std::pmr::polymorphic_allocator<T> alloc{r};
                std::allocator_traits<std::pmr::polymorphic_allocator<T>>::destroy(alloc,
                                                                                   typed);
                alloc.deallocate(typed, 1);
            },
            r};
    }

    template <typename T>
    using pmr_unique_ptr = std::unique_ptr<T, PmrDeleter>;

    template <typename T, typename... Args>
    pmr_unique_ptr<T> make_pmr_unique(std::pmr::memory_resource *r, Args &&...args)
    {
        std::pmr::polymorphic_allocator<T> alloc{r};
        T *p = alloc.allocate(1);

        try
        {
            std::allocator_traits<std::pmr::polymorphic_allocator<T>>::construct(
                alloc, p, std::forward<Args>(args)...);
        }
        catch (...)
        {
            alloc.deallocate(p, 1);
            throw;
        }

        return pmr_unique_ptr<T>(p, make_pmr_deleter<T>(r));
    }

    // ============================================================================
    // 基础几何与布局类型
    // ============================================================================

    struct Size
    {
        float width, height;
    };

    struct Offset
    {
        float x, y;
    };

    struct EdgeInsets
    {
        float left = 0, top = 0, right = 0, bottom = 0;
        constexpr float horizontal() const
        {
            return left + right;
        }
        constexpr float vertical() const
        {
            return top + bottom;
        }
        static constexpr EdgeInsets all(float v)
        {
            return {v, v, v, v};
        }
    };

    struct Alignment
    {
        float x, y;
    };
    namespace Align
    {
        constexpr Alignment topLeft{-1, -1}, topCenter{0, -1}, topRight{1, -1};
        constexpr Alignment centerLeft{-1, 0}, center{0, 0}, centerRight{1, 0};
        constexpr Alignment bottomLeft{-1, 1}, bottomCenter{0, 1}, bottomRight{1, 1};
    } // namespace Align

    // ============================================================================
    // 约束（Constraints）
    // ============================================================================

    struct Constraints
    {
        static constexpr float inf = std::numeric_limits<float>::infinity();
        float minW = 0, maxW = inf, minH = 0, maxH = inf;

        constexpr bool hasBoundedWidth() const noexcept
        {
            return maxW < inf;
        }
        constexpr bool hasBoundedHeight() const noexcept
        {
            return maxH < inf;
        }
        constexpr bool hasUnboundedWidth() const noexcept
        {
            return maxW >= inf;
        }
        constexpr bool hasUnboundedHeight() const noexcept
        {
            return maxH >= inf;
        }

        constexpr Constraints deflate(const EdgeInsets &e) const noexcept
        {
            float h = e.horizontal(), v = e.vertical();
            return {std::max(0.0f, minW - h), std::max(0.0f, maxW - h),
                    std::max(0.0f, minH - v), std::max(0.0f, maxH - v)};
        }

        constexpr Constraints intersect(const Constraints &parent) const noexcept
        {
            float newMinW = std::max(parent.minW, minW);
            float newMaxW = std::min(parent.maxW, maxW);
            float newMinH = std::max(parent.minH, minH);
            float newMaxH = std::min(parent.maxH, maxH);
            if (newMinW > newMaxW)
                newMinW = newMaxW = std::clamp(minW, parent.minW, parent.maxW);
            if (newMinH > newMaxH)
                newMinH = newMaxH = std::clamp(minH, parent.minH, parent.maxH);
            return {newMinW, newMaxW, newMinH, newMaxH};
        }

        constexpr Constraints applyFixedWidth(float w) const noexcept
        {
            return Constraints{w, w, minH, maxH}.intersect(*this);
        }
        constexpr Constraints applyFixedHeight(float h) const noexcept
        {
            return Constraints{minW, maxW, h, h}.intersect(*this);
        }

        constexpr Size clamp(const Size &s) const noexcept
        {
            return {std::clamp(s.width, minW, maxW), std::clamp(s.height, minH, maxH)};
        }
    };

    // ============================================================================
    // 方向与枚举
    // ============================================================================

    enum class TextDirection
    {
        ltr,
        rtl
    };
    enum class VerticalDirection
    {
        down,
        up
    };
    enum class MainAxisAlignment
    {
        start,
        end,
        center,
        spaceBetween,
        spaceAround,
        spaceEvenly
    };
    enum class CrossAxisAlignment
    {
        start,
        end,
        center,
        stretch,
        baseline
    };
    enum class MainAxisSize
    {
        max,
        min
    };
    enum class AxisDirection
    {
        right,
        left,
        down,
        up
    };

    // ============================================================================
    // 数据挂载基类 IData
    // ============================================================================
    class Node;
    class IData
    {
      public:
        virtual ~IData() = default;
    };
    struct render_context
    {
        shader_data::DrawRecorder &drawRecorder;
        glfw_input &input;
        FrameClock &clock;
        FontSelect &fontSelect;
    };
    using render_callback = void(Node *self, render_context &context);
    class RenderData
    {
      public:
        virtual ~RenderData() = default;
        virtual void render(Node *self, render_context &context) = 0;
    };

    // ============================================================================
    // 前置声明 Node 并定义 Widget 别名
    // ============================================================================
    using Widget = pmr_unique_ptr<Node>;

    // ============================================================================
    // 抽象节点基类 Node
    // ============================================================================

    struct BoxGeometry
    {
        float x = 0, y = 0, w = 0, h = 0;
    };

    class Node
    {
      public:
        explicit Node(std::string name, EdgeInsets margin = {}, EdgeInsets padding = {},
                      std::optional<float> fixedWidth = std::nullopt,
                      std::optional<float> fixedHeight = std::nullopt)
            : name(std::move(name)), margin(margin), padding(padding),
              fixedWidth(fixedWidth), fixedHeight(fixedHeight)
        {
        }

        virtual ~Node() = default;

        BoxGeometry geometry;
        std::string name;
        float baseline = 0.0f;
        EdgeInsets margin;
        EdgeInsets padding;
        std::optional<float> fixedWidth;
        std::optional<float> fixedHeight;
        std::vector<Widget> children;
        Node *parent = nullptr; // 父节点裸指针，不拥有所有权
        bool dirty = false;     // 脏标记，用于测试遍历是否覆盖
        pmr_unique_ptr<RenderData> renderData;

        constexpr void markDirty() noexcept
        {
            Node *current = this;
            while (current)
            {
                if (current->dirty)
                    break; // 祖先已经脏了，无需继续
                current->dirty = true;
                current = current->parent;
            }
        }

        // ---------- 数据挂载 API（使用 static_string 优化） ----------
        // 添加数据，返回原始指针以便后续修改
        template <typename T, typename... Args>
        T *addData(static_string key, Args &&...args)
        {
            static_assert(std::is_base_of_v<IData, T>, "T must derive from IData");
            auto ptr = make_pmr_unique<T>(nodeResource, std::forward<Args>(args)...);
            T *raw = ptr.get();
            dataMap_[key].push_back(std::move(ptr));
            markDirty(); // 新增（若挂载数据不影响布局/渲染，可考虑不标，但保守起见标上）
            return raw;
        }
        template <typename T, typename... Args>
        T *setRenderData(Args &&...args)
        {
            static_assert(std::is_base_of_v<RenderData, T>,
                          "T must derive from RenderData");
            auto ptr = make_pmr_unique<T>(nodeResource, std::forward<Args>(args)...);
            T *raw = ptr.get();
            renderData = std::move(ptr);
            markDirty(); // 新增
            return raw;
        }

        // 获取数据，如果类型不匹配或不存在则返回 nullptr
        template <typename T>
        T *getData(static_string key, std::size_t index = 0)
        {
            static_assert(std::is_base_of_v<IData, T>, "T must derive from IData");
            auto it = dataMap_.find(key);
            if (it == dataMap_.end() || index >= it->second.size())
                return nullptr;
            return dynamic_cast<T *>(it->second[index].get());
        }

        // 获取指定键下数据的数量
        std::size_t getDataCount(static_string key) const
        {
            auto it = dataMap_.find(key);
            return (it == dataMap_.end()) ? 0 : it->second.size();
        }

        // 移除指定键的某个数据，若成功则返回 true
        bool removeData(static_string key, std::size_t index = 0)
        {
            auto it = dataMap_.find(key);
            if (it == dataMap_.end() || index >= it->second.size())
                return false;
            it->second.erase(it->second.begin() + index);
            if (it->second.empty())
                dataMap_.erase(it);
            markDirty(); // 新增
            return true;
        }

        // 布局入口
        void layout(Constraints constraints)
        {
            if (fixedWidth)
                constraints = constraints.applyFixedWidth(*fixedWidth);
            if (fixedHeight)
                constraints = constraints.applyFixedHeight(*fixedHeight);
            performLayout(constraints);
            Size finalSize{geometry.w, geometry.h};
            if (finalSize.width > constraints.maxW + 1e-6f ||
                finalSize.height > constraints.maxH + 1e-6f)
            {
                std::cerr << "WARNING: Widget '" << name << "' overflows parent by ("
                          << finalSize.width - constraints.maxW << ", "
                          << finalSize.height - constraints.maxH << ")\n";
            }
            dirty = false; // 布局完成，清除自身脏标志（子树已在递归中清除）
        }

        Size size() const
        {
            return {geometry.w, geometry.h};
        }
        Offset position() const
        {
            return {geometry.x, geometry.y};
        }
        BoxGeometry getGeometry() const
        {
            return geometry;
        }

        // ==========================================================================
        // 节点移动 API（安全操作，维护 parent 指针）
        // ==========================================================================

        // 移除指定子节点，返回其 unique_ptr（所有权转移给调用者），parent 置空
        Widget removeChild(Node *child)
        {
            auto it = std::ranges::find_if(
                children, [child](const Widget &w) { return w.get() == child; });
            if (it == children.end())
                return nullptr;

            auto detached = std::move(*it);
            children.erase(it);
            detached->parent = nullptr;
            markDirty(); // 新增（旧父节点脏）
            return detached;
        }

        // 添加子节点（接收 unique_ptr），设置 parent 并加入 children
        void addChild(Widget child)
        {
            if (!child)
                return;
            child->parent = this;
            children.push_back(std::move(child));
            markDirty(); // 新增
        }

        // 将已存在的子节点移动到新的父节点下
        static void moveNode(Node *child, Node *newParent)
        {
            if (!child || !newParent)
                return;
            Node *oldParent = child->parent;
            if (oldParent == newParent)
                return;

            Widget detached;
            if (oldParent)
            { // removeChild 内部已调用 markDirty()
                detached = oldParent->removeChild(child);
            }
            else
            {
                return; // 没有父节点，无法取得所有权
            }

            if (detached)
            { // addChild 内部已调用 markDirty()
                newParent->addChild(std::move(detached));
            }
        }

      private:
        std::unordered_map<static_string, std::vector<pmr_unique_ptr<IData>>,
                           StaticStringHash, StaticStringEqual>
            dataMap_;

      protected:
        virtual void performLayout(Constraints borderBC) = 0;
        Size childFullSize(const Node &child) const
        {
            return {child.geometry.w + child.margin.horizontal(),
                    child.geometry.h + child.margin.vertical()};
        }
        static Constraints makeLooseConstraints(const Constraints &c)
        {
            return {0.0f, c.maxW, 0.0f, c.maxH};
        }
    };

    // ============================================================================
    // Expanded 节点
    // ============================================================================

    class ExpandedNode : public Node
    {
      public:
        explicit ExpandedNode(int flex = 1, std::string name = "expanded")
            : Node(std::move(name)), flex(flex)
        {
        }
        int flex = 1;

      protected:
        void performLayout(Constraints) override
        {
            throw std::logic_error("Expanded must be direct child of Row/Column/Flex.");
        }
    };

    // ============================================================================
    // Container 节点
    // ============================================================================

    class ContainerNode : public Node
    {
      public:
        explicit ContainerNode(std::string name,
                               std::optional<float> width = std::nullopt,
                               std::optional<float> height = std::nullopt,
                               EdgeInsets margin = {}, EdgeInsets padding = {},
                               EdgeInsets border = {},
                               std::optional<Alignment> alignment = std::nullopt,
                               std::optional<Constraints> constraints = std::nullopt)
            : Node(std::move(name), margin, padding, width, height), alignment(alignment),
              ownConstraints(constraints), border(border)
        {
        }

        std::optional<Alignment> alignment;
        std::optional<Constraints> ownConstraints;
        EdgeInsets border;

      protected:
        void performLayout(Constraints borderBC) override
        {
            if (ownConstraints)
                borderBC = borderBC.intersect(*ownConstraints);
            if (children.empty())
            {
                float w = fixedWidth.value_or(0.0f);
                float h = fixedHeight.value_or(0.0f);
                Size size = borderBC.clamp({w, h});
                geometry = BoxGeometry{0, 0, size.width, size.height};
                baseline = size.height;
                return;
            }
            if (children.size() != 1)
                throw std::logic_error("Container '" + name +
                                       "' must have exactly one child.");
            Node &child = *children[0];
            Constraints innerBC = borderBC.deflate(border).deflate(padding);
            Constraints childBC =
                alignment ? Node::makeLooseConstraints(innerBC) : innerBC;
            childBC = childBC.deflate(child.margin);
            child.layout(childBC);
            Size childFull = childFullSize(child);
            float baseW = childFull.width + padding.horizontal() + border.horizontal();
            float baseH = childFull.height + padding.vertical() + border.vertical();
            float containerW = fixedWidth.value_or(baseW);
            float containerH = fixedHeight.value_or(baseH);
            Size containerSize = borderBC.clamp({containerW, containerH});
            Constraints containerAsConstraints{0, containerSize.width, 0,
                                               containerSize.height};
            positionChildByAlignment(child, containerAsConstraints, padding, alignment,
                                     border);
            geometry = BoxGeometry{0, 0, containerSize.width, containerSize.height};
            baseline = child.baseline + child.geometry.y;
        }

      private:
        void positionChildByAlignment(Node &child,
                                      const Constraints &containerConstraints,
                                      const EdgeInsets &padding,
                                      const std::optional<Alignment> &align,
                                      const EdgeInsets &border)
        {
            Size childFull = childFullSize(child);
            float contentW =
                containerConstraints.maxW - border.horizontal() - padding.horizontal();
            float contentH =
                containerConstraints.maxH - border.vertical() - padding.vertical();
            float extraW = std::max(0.0f, contentW - childFull.width);
            float extraH = std::max(0.0f, contentH - childFull.height);
            Alignment al = align.value_or(Align::topLeft);
            Offset offset = {border.left + padding.left + child.margin.left +
                                 extraW * (al.x + 1.0f) / 2.0f,
                             border.top + padding.top + child.margin.top +
                                 extraH * (al.y + 1.0f) / 2.0f};
            child.geometry.x = offset.x;
            child.geometry.y = offset.y;
        }
    };

    // ============================================================================
    // Flex 节点基类（Row 和 Column 共用）
    // ============================================================================

    template <bool isRow_>
    class FlexNode : public Node
    {
      public:
        MainAxisAlignment mainAlign = MainAxisAlignment::start;
        CrossAxisAlignment crossAlign = CrossAxisAlignment::start;
        MainAxisSize mainAxisSize = MainAxisSize::max;
        TextDirection textDirection = TextDirection::ltr;
        VerticalDirection verticalDirection = VerticalDirection::down;

      protected:
        FlexNode(std::string name, std::optional<float> fixedWidth,
                 std::optional<float> fixedHeight, EdgeInsets margin, EdgeInsets padding,
                 MainAxisAlignment mainAlign, CrossAxisAlignment crossAlign,
                 MainAxisSize mainAxisSize,
                 TextDirection textDirection = TextDirection::ltr,
                 VerticalDirection verticalDirection = VerticalDirection::down)
            : Node(std::move(name), margin, padding, fixedWidth, fixedHeight),
              mainAlign(mainAlign), crossAlign(crossAlign), mainAxisSize(mainAxisSize),
              textDirection(textDirection), verticalDirection(verticalDirection)
        {
        }

        void performLayout(Constraints borderBC) override
        {
            AxisDirection dir = axisDirection();
            bool isBoundedMain =
                isRow_ ? borderBC.hasBoundedWidth() : borderBC.hasBoundedHeight();
            bool isBoundedCross =
                isRow_ ? borderBC.hasBoundedHeight() : borderBC.hasBoundedWidth();
            if (!isBoundedCross)
                throw std::logic_error("Row/Column '" + name +
                                       "' has unbounded cross axis.");
            if (!isBoundedMain)
            {
                for (const auto &child : children)
                    if (dynamic_cast<ExpandedNode *>(child.get()) != nullptr)
                        throw std::logic_error(
                            "Row/Column '" + name +
                            "' has unbounded main axis and Expanded child.");
            }

            float mainSize = mainAxisSizeFromConstraints(borderBC);
            float crossSize = crossAxisSizeFromConstraints(borderBC);
            float padMainStart = mainAxisPaddingStart(padding, dir);
            float padMainEnd = mainAxisPaddingEnd(padding, dir);
            float padCrossStart = crossAxisPaddingStart(padding, dir);
            float padCrossEnd = crossAxisPaddingEnd(padding, dir);
            float innerMain = mainSize - padMainStart - padMainEnd;
            if (!isBoundedMain)
                innerMain = Constraints::inf;
            float innerCross = crossSize - padCrossStart - padCrossEnd;

            struct FlexChildInfo
            {
                Node *node;
                float flex;
                float marginMain;
                float marginCross;
            };
            std::vector<FlexChildInfo> infos;
            float totalFlex = 0;
            for (auto &childPtr : children)
            {
                Node *child = childPtr.get();
                if (auto expanded = dynamic_cast<ExpandedNode *>(child))
                {
                    if (expanded->children.empty())
                        throw std::logic_error("Expanded widget has no child.");
                    Node *real = expanded->children[0].get();
                    float flexVal = expanded->flex;
                    EdgeInsets m = real->margin;
                    float mM = isRow_ ? m.horizontal() : m.vertical();
                    float mC = isRow_ ? m.vertical() : m.horizontal();
                    infos.push_back({real, flexVal, mM, mC});
                    totalFlex += flexVal;
                }
                else
                {
                    EdgeInsets m = child->margin;
                    float mM = isRow_ ? m.horizontal() : m.vertical();
                    float mC = isRow_ ? m.vertical() : m.horizontal();
                    infos.push_back({child, 0.0f, mM, mC});
                }
            }

            std::vector<float> naturalMain;
            naturalMain.reserve(infos.size());
            for (const auto &info : infos)
            {
                float childMainMax = (info.flex > 0)
                                         ? 0.0f
                                         : (isBoundedMain ? innerMain : Constraints::inf);
                float childCrossMax = innerCross;
                float childCrossMin =
                    (crossAlign == CrossAxisAlignment::stretch) ? childCrossMax : 0.0f;
                Constraints childBC = makeFlexAxisConstraints(
                    isRow_, 0.0f, childMainMax, childCrossMin, childCrossMax);
                if (info.flex == 0)
                {
                    EdgeInsets deflateMargin =
                        isRow_ ? EdgeInsets{info.marginMain, info.marginCross, 0, 0}
                               : EdgeInsets{info.marginCross, info.marginMain, 0, 0};
                    childBC = childBC.deflate(deflateMargin);
                }
                info.node->layout(childBC);
                naturalMain.push_back(isRow_ ? info.node->geometry.w
                                             : info.node->geometry.h);
            }

            float totalNatural = 0;
            for (size_t i = 0; i < infos.size(); ++i)
                totalNatural += naturalMain[i] + infos[i].marginMain;

            float finalInnerMain;
            if (!isBoundedMain)
                finalInnerMain = totalNatural;
            else if (mainAxisSize == MainAxisSize::min)
                finalInnerMain = std::min(totalNatural, innerMain);
            else
                finalInnerMain = innerMain;

            float freeMain = std::max(0.0f, finalInnerMain - totalNatural);
            if (totalFlex > 0 && freeMain > 0)
            {
                float flexUnit = freeMain / totalFlex;
                for (size_t i = 0; i < infos.size(); ++i)
                {
                    if (infos[i].flex > 0)
                    {
                        float allocated = infos[i].flex * flexUnit;
                        float childMainSize =
                            std::max(0.0f, allocated - infos[i].marginMain);
                        float childCrossMax = innerCross;
                        float childCrossMin = (crossAlign == CrossAxisAlignment::stretch)
                                                  ? childCrossMax
                                                  : 0.0f;
                        Constraints tightBC =
                            makeFlexAxisConstraints(isRow_, childMainSize, childMainSize,
                                                    childCrossMin, childCrossMax);
                        infos[i].node->layout(tightBC);
                        naturalMain[i] = isRow_ ? infos[i].node->geometry.w
                                                : infos[i].node->geometry.h;
                    }
                }
            }

            float totalMainUsed = 0;
            for (size_t i = 0; i < infos.size(); ++i)
                totalMainUsed += naturalMain[i] + infos[i].marginMain;

            float extraMain = std::max(0.0f, finalInnerMain - totalMainUsed);
            float mainGap = 0, mainStartOff = 0;
            size_t childCount = infos.size();
            if (childCount == 1)
            {
                if (mainAlign == MainAxisAlignment::spaceAround ||
                    mainAlign == MainAxisAlignment::spaceEvenly)
                    mainStartOff = extraMain / 2;
            }
            else if (childCount > 1)
            {
                switch (mainAlign)
                {
                case MainAxisAlignment::start:
                    mainStartOff = 0;
                    break;
                case MainAxisAlignment::end:
                    mainStartOff = extraMain;
                    break;
                case MainAxisAlignment::center:
                    mainStartOff = extraMain / 2;
                    break;
                case MainAxisAlignment::spaceBetween:
                    mainGap = extraMain / (childCount - 1);
                    break;
                case MainAxisAlignment::spaceAround:
                    mainGap = extraMain / childCount;
                    mainStartOff = mainGap / 2;
                    break;
                case MainAxisAlignment::spaceEvenly:
                    mainGap = extraMain / (childCount + 1);
                    mainStartOff = mainGap;
                    break;
                }
            }

            float mainPos = mainStartOff;
            float maxBaseline = 0;
            if (crossAlign == CrossAxisAlignment::baseline && isRow_)
            {
                for (const auto &info : infos)
                    maxBaseline = std::max(maxBaseline, info.node->baseline);
            }

            for (size_t i = 0; i < infos.size(); ++i)
            {
                Node &child = *infos[i].node;
                float childMainLen = naturalMain[i];
                float childCrossLen = isRow_ ? child.geometry.h : child.geometry.w;
                float leadingMargin = isRow_ ? child.margin.left : child.margin.top;
                setChildMainAxisPosition(child, dir, mainPos + leadingMargin,
                                         childMainLen, mainSize, padMainStart,
                                         padMainEnd);
                float crossExtra =
                    std::max(0.0f, innerCross - (childCrossLen + infos[i].marginCross));
                float crossStart = 0;
                switch (crossAlign)
                {
                case CrossAxisAlignment::start:
                    crossStart = 0;
                    break;
                case CrossAxisAlignment::end:
                    crossStart = crossExtra;
                    break;
                case CrossAxisAlignment::center:
                    crossStart = crossExtra / 2;
                    break;
                case CrossAxisAlignment::stretch:
                    crossStart = 0;
                    break;
                case CrossAxisAlignment::baseline:
                    if (isRow_)
                        crossStart = maxBaseline - child.baseline;
                    break;
                }
                float crossLeadingMargin = isRow_ ? child.margin.top : child.margin.left;
                setChildCrossAxisPosition(child, dir, crossStart + crossLeadingMargin,
                                          childCrossLen, crossSize, padCrossStart,
                                          padCrossEnd);
                mainPos += childMainLen + infos[i].marginMain + mainGap;
            }

            Size size;
            if (isRow_)
            {
                size.width = finalInnerMain + padding.horizontal();
                if (crossAlign == CrossAxisAlignment::stretch)
                    size.height = innerCross + padding.vertical();
                else
                {
                    float maxChildHeight = 0;
                    for (const auto &info : infos)
                        maxChildHeight = std::max(maxChildHeight, info.node->geometry.h);
                    size.height = maxChildHeight + padding.vertical();
                }
            }
            else
            {
                if (crossAlign == CrossAxisAlignment::stretch)
                    size.width = innerCross + padding.horizontal();
                else
                {
                    float maxChildWidth = 0;
                    for (const auto &info : infos)
                        maxChildWidth = std::max(maxChildWidth, info.node->geometry.w);
                    size.width = maxChildWidth + padding.horizontal();
                }
                size.height = finalInnerMain + padding.vertical();
            }
            if (fixedWidth)
                size.width = *fixedWidth;
            if (fixedHeight)
                size.height = *fixedHeight;
            size = borderBC.clamp(size);
            geometry = BoxGeometry{0, 0, size.width, size.height};
            if (!infos.empty())
            {
                Node &first = *infos[0].node;
                baseline = isRow_ ? first.baseline + first.geometry.y
                                  : first.baseline + first.geometry.x;
            }
            else
            {
                baseline = isRow_ ? size.height : size.width;
            }
        }

      private:
        AxisDirection axisDirection() const
        {
            if (isRow_)
                return (textDirection == TextDirection::ltr) ? AxisDirection::right
                                                             : AxisDirection::left;
            else
                return (verticalDirection == VerticalDirection::down)
                           ? AxisDirection::down
                           : AxisDirection::up;
        }
        bool isAxisForward(AxisDirection dir) const
        {
            return dir == AxisDirection::right || dir == AxisDirection::down;
        }
        float mainAxisSizeFromConstraints(const Constraints &bc) const
        {
            return isRow_ ? bc.maxW : bc.maxH;
        }
        float crossAxisSizeFromConstraints(const Constraints &bc) const
        {
            return isRow_ ? bc.maxH : bc.maxW;
        }
        float mainAxisPaddingStart(const EdgeInsets &pad, AxisDirection dir) const
        {
            switch (dir)
            {
            case AxisDirection::right:
                return pad.left;
            case AxisDirection::left:
                return pad.right;
            case AxisDirection::down:
                return pad.top;
            case AxisDirection::up:
                return pad.bottom;
            }
            return 0;
        }
        float mainAxisPaddingEnd(const EdgeInsets &pad, AxisDirection dir) const
        {
            switch (dir)
            {
            case AxisDirection::right:
                return pad.right;
            case AxisDirection::left:
                return pad.left;
            case AxisDirection::down:
                return pad.bottom;
            case AxisDirection::up:
                return pad.top;
            }
            return 0;
        }
        float crossAxisPaddingStart(const EdgeInsets &pad, AxisDirection dir) const
        {
            return (dir == AxisDirection::right || dir == AxisDirection::left) ? pad.top
                                                                               : pad.left;
        }
        float crossAxisPaddingEnd(const EdgeInsets &pad, AxisDirection dir) const
        {
            return (dir == AxisDirection::right || dir == AxisDirection::left)
                       ? pad.bottom
                       : pad.right;
        }
        Constraints makeFlexAxisConstraints(bool isRow, float minMain, float maxMain,
                                            float minCross, float maxCross) const
        {
            return isRow ? Constraints{minMain, maxMain, minCross, maxCross}
                         : Constraints{minCross, maxCross, minMain, maxMain};
        }
        void setChildMainAxisPosition(Node &child, AxisDirection dir, float pos,
                                      float childMainLen, float parentMainSize,
                                      float padStart, float padEnd)
        {
            float physical;
            if (isAxisForward(dir))
                physical = padStart + pos;
            else
                physical = parentMainSize - padEnd - pos - childMainLen;
            if (dir == AxisDirection::right || dir == AxisDirection::left)
                child.geometry.x = physical;
            else
                child.geometry.y = physical;
        }
        void setChildCrossAxisPosition(Node &child, AxisDirection dir, float pos,
                                       float childCrossLen, float parentCrossSize,
                                       float padCrossStart, float padCrossEnd)
        {
            (void)childCrossLen;
            (void)parentCrossSize;
            (void)padCrossEnd;
            if (dir == AxisDirection::right || dir == AxisDirection::left)
                child.geometry.y = padCrossStart + pos;
            else
                child.geometry.x = padCrossStart + pos;
        }
    };

    // ============================================================================
    // Row 和 Column 具体节点
    // ============================================================================

    class RowNode : public FlexNode<true>
    {
      public:
        explicit RowNode(std::string name, std::optional<float> width = std::nullopt,
                         std::optional<float> height = std::nullopt,
                         MainAxisAlignment ma = MainAxisAlignment::start,
                         CrossAxisAlignment ca = CrossAxisAlignment::start,
                         MainAxisSize ms = MainAxisSize::max, EdgeInsets m = {},
                         EdgeInsets p = {}, TextDirection td = TextDirection::ltr)
            : FlexNode<true>(std::move(name), width, height, m, p, ma, ca, ms, td)
        {
        }
    };

    class ColumnNode : public FlexNode<false>
    {
      public:
        explicit ColumnNode(std::string name, std::optional<float> width = std::nullopt,
                            std::optional<float> height = std::nullopt,
                            MainAxisAlignment ma = MainAxisAlignment::start,
                            CrossAxisAlignment ca = CrossAxisAlignment::start,
                            MainAxisSize ms = MainAxisSize::max, EdgeInsets m = {},
                            EdgeInsets p = {},
                            VerticalDirection vd = VerticalDirection::down)
            : FlexNode<false>(std::move(name), width, height, m, p, ma, ca, ms,
                              TextDirection::ltr, vd)
        {
        }
    };

    // ============================================================================
    // Builder 类
    // ============================================================================

    // 辅助函数：将任意类型转换为 Widget（支持 Widget 和 Builder）
    template <typename T>
    Widget toWidget(T &&arg)
    {
        if constexpr (std::is_convertible_v<T, Widget>)
        {
            return std::forward<T>(arg);
        }
        else
        {
            return std::forward<T>(arg).build();
        }
    }

    // 通用数据挂载方法，所有 Builder 继承使用
    template <typename Derived>
    class DataMixin
    {
      public:
        template <typename T, typename... Args>
        Derived &data(static_string key, Args &&...args)
        {
            static_cast<Derived *>(this)->node->template addData<T>(
                key, std::forward<Args>(args)...);
            return *static_cast<Derived *>(this);
        }
        template <typename T, typename... Args>
        Derived &renderData(Args &&...args)
        {
            static_cast<Derived *>(this)->node->template setRenderData<T>(
                std::forward<Args>(args)...);
            return *static_cast<Derived *>(this);
        }
    };

    class ContainerBuilder : public DataMixin<ContainerBuilder>
    {
        pmr_unique_ptr<ContainerNode> node;

      public:
        explicit ContainerBuilder(std::string name)
            : node(make_pmr_unique<ContainerNode>(nodeResource, std::move(name)))
        {
        }
        ContainerBuilder &width(float w)
        {
            node->fixedWidth = w;
            return *this;
        }
        ContainerBuilder &height(float h)
        {
            node->fixedHeight = h;
            return *this;
        }
        ContainerBuilder &margin(EdgeInsets m)
        {
            node->margin = m;
            return *this;
        }
        ContainerBuilder &padding(EdgeInsets p)
        {
            node->padding = p;
            return *this;
        }
        ContainerBuilder &border(EdgeInsets b)
        {
            node->border = b;
            return *this;
        }
        ContainerBuilder &alignment(Alignment a)
        {
            node->alignment = a;
            return *this;
        }
        ContainerBuilder &constraints(Constraints c)
        {
            node->ownConstraints = c;
            return *this;
        }
        template <typename T>
        ContainerBuilder &child(T &&child)
        {
            Widget childWidget = toWidget(std::forward<T>(child));
            node->addChild(std::move(childWidget));
            return *this;
        }
        Widget build()
        {
            assert(node != nullptr);
            return std::move(node);
        }

        template <typename>
        friend class DataMixin; // 允许访问私有成员 node
    };

    class RowBuilder : public DataMixin<RowBuilder>
    {
        pmr_unique_ptr<RowNode> node;

      public:
        explicit RowBuilder(std::string name)
            : node(make_pmr_unique<RowNode>(nodeResource, std::move(name)))
        {
        }
        RowBuilder &width(float w)
        {
            node->fixedWidth = w;
            return *this;
        }
        RowBuilder &height(float h)
        {
            node->fixedHeight = h;
            return *this;
        }
        RowBuilder &margin(EdgeInsets m)
        {
            node->margin = m;
            return *this;
        }
        RowBuilder &padding(EdgeInsets p)
        {
            node->padding = p;
            return *this;
        }
        RowBuilder &mainAxisAlignment(MainAxisAlignment ma)
        {
            node->mainAlign = ma;
            return *this;
        }
        RowBuilder &crossAxisAlignment(CrossAxisAlignment ca)
        {
            node->crossAlign = ca;
            return *this;
        }
        RowBuilder &mainAxisSize(MainAxisSize ms)
        {
            node->mainAxisSize = ms;
            return *this;
        }
        RowBuilder &textDirection(TextDirection td)
        {
            node->textDirection = td;
            return *this;
        }

        template <typename... Children>
        RowBuilder &children(Children &&...children)
        {
            (
                [&] {
                    Widget childWidget = toWidget(std::forward<Children>(children));
                    node->addChild(std::move(childWidget));
                }(),
                ...);
            return *this;
        }

        Widget build()
        {
            assert(node != nullptr);
            return std::move(node);
        }

        template <typename>
        friend class DataMixin;
    };

    class ColumnBuilder : public DataMixin<ColumnBuilder>
    {
        pmr_unique_ptr<ColumnNode> node;

      public:
        explicit ColumnBuilder(std::string name)
            : node(make_pmr_unique<ColumnNode>(nodeResource, std::move(name)))
        {
        }
        ColumnBuilder &width(float w)
        {
            node->fixedWidth = w;
            return *this;
        }
        ColumnBuilder &height(float h)
        {
            node->fixedHeight = h;
            return *this;
        }
        ColumnBuilder &margin(EdgeInsets m)
        {
            node->margin = m;
            return *this;
        }
        ColumnBuilder &padding(EdgeInsets p)
        {
            node->padding = p;
            return *this;
        }
        ColumnBuilder &mainAxisAlignment(MainAxisAlignment ma)
        {
            node->mainAlign = ma;
            return *this;
        }
        ColumnBuilder &crossAxisAlignment(CrossAxisAlignment ca)
        {
            node->crossAlign = ca;
            return *this;
        }
        ColumnBuilder &mainAxisSize(MainAxisSize ms)
        {
            node->mainAxisSize = ms;
            return *this;
        }
        ColumnBuilder &verticalDirection(VerticalDirection vd)
        {
            node->verticalDirection = vd;
            return *this;
        }

        template <typename... Children>
        ColumnBuilder &children(Children &&...children)
        {
            (
                [&] {
                    Widget childWidget = toWidget(std::forward<Children>(children));
                    node->addChild(std::move(childWidget));
                }(),
                ...);
            return *this;
        }

        Widget build()
        {
            assert(node != nullptr);
            return std::move(node);
        }

        template <typename>
        friend class DataMixin;
    };

    class ExpandedBuilder : public DataMixin<ExpandedBuilder>
    {
        pmr_unique_ptr<ExpandedNode> node;

      public:
        explicit ExpandedBuilder(int flex = 1, std::string name = "expanded")
            : node(make_pmr_unique<ExpandedNode>(nodeResource, flex, std::move(name)))
        {
        }
        ExpandedBuilder &flex(int f)
        {
            node->flex = f;
            return *this;
        }
        template <typename T>
        ExpandedBuilder &child(T &&child)
        {
            Widget childWidget = toWidget(std::forward<T>(child));
            node->addChild(std::move(childWidget));
            return *this;
        }
        Widget build()
        {
            assert(node != nullptr);
            return std::move(node);
        }

        template <typename>
        friend class DataMixin;
    };

    // ============================================================================
    // 顶层入口函数
    // ============================================================================

    inline ContainerBuilder Container(std::string name)
    {
        return ContainerBuilder(std::move(name));
    }
    inline RowBuilder Row(std::string name)
    {
        return RowBuilder(std::move(name));
    }
    inline ColumnBuilder Column(std::string name)
    {
        return ColumnBuilder(std::move(name));
    }
    inline ExpandedBuilder Expanded(int flex = 1, std::string name = "expanded")
    {
        return ExpandedBuilder(flex, std::move(name));
    }
} // namespace ui

// shader_data::Glyph
auto &glyphPool()
{
    using Pool = soa_vector<shader_data::Glyph>;
    static Pool pool{};
    return pool;
}
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

//diff: [test_dod22] end

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

    auto inputDataCtx = inputInit(swapchain);
    auto &input = *inputDataCtx.input.get();
    auto &camera = inputDataCtx.camera;
    auto &uiCamera = inputDataCtx.uiCamera;
    auto &clock = inputDataCtx.clock;

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

    std::vector<shader_data::Glyph> simpleGlyphs;
    constexpr float FONT_SIZE = 0.2f;         // 1em 对应的 NDC 高度
    constexpr glm::vec2 ORIGIN{-0.1f, -0.1f}; // 字形块左上角（NDC，y 向下）
    const float baselineY = ORIGIN.y + FONT_SIZE;
    float cursorX = ORIGIN.x;
    for (const auto &run : shapeResult)
    {
        for (const auto &g : run)
        {
            using bound_type = decltype(g.plane_bounds);
            if (g.plane_bounds == bound_type{}) // 空格等无字形字符
            {
                cursorX += static_cast<float>(g.advance_x) * FONT_SIZE;
                continue;
            }
            // 字形矩形（NDC，y 向下）：顶边在上（数值更小）
            float left = cursorX + static_cast<float>(g.plane_bounds.left) * FONT_SIZE;
            float bottom =
                baselineY + static_cast<float>(g.plane_bounds.bottom) * FONT_SIZE;
            float right = cursorX + static_cast<float>(g.plane_bounds.right) * FONT_SIZE;
            float top = baselineY + static_cast<float>(g.plane_bounds.top) * FONT_SIZE;

            glm::vec2 p0{left, top};
            glm::vec2 p2{right, bottom};
            glm::vec2 center = (p0 + p2) * 0.5f;
            glm::vec2 full = p2 - p0;

            // NOTE: 图集 UV 为 y 向下（bottom > top），
            // 必须 offset=top、scale=bottom-top，否则字形上下颠倒。
            UvTransform uv;
            uv.scale = {static_cast<float>(g.uv_bounds.right - g.uv_bounds.left),
                        static_cast<float>(g.uv_bounds.bottom - g.uv_bounds.top)};
            uv.offset = {static_cast<float>(g.uv_bounds.left),
                         static_cast<float>(g.uv_bounds.top)};

            shader_data::Glyph tg{};
            tg.entity_index = 0;
            tg.textureIndex = g.font_ctx->bind.texture_index;
            tg.samplerIndex = g.font_ctx->bind.sampler_index;
            tg.fontType = static_cast<uint32_t>(g.font_ctx->type);
            tg.pxRange =
                static_cast<float>(g.font_ctx->font.atlas.distanceRange.value_or(0.0));
            tg.modulateFlag = 1;
            tg.color = glm::vec4(1.0f); // 白色字形（调制路径输出 fragColor.rgb*opacity）
            tg.model = glm::translate(glm::mat4(1.0f), glm::vec3(center, 0.0f)) *
                       glm::scale(glm::mat4(1.0f), glm::vec3(full, 1.0f));
            tg.uvTransform = uv;
            simpleGlyphs.push_back(tg);
            cursorX += static_cast<float>(g.advance_x) * FONT_SIZE;
        }
    }
    std::cout << "simpleGlyphs size: " << simpleGlyphs.size() << '\n';
    assert(not simpleGlyphs.empty()); // 必须至少 shape 出 1 个字形

    auto mainPipelineCtx = initPipeline(hardwareCtx, descriptorCtx);
    auto &[pipelineLayout, pipelineOpaque3D, pipelineTransparent3D, pipelineOpaqueUI,
           pipelineTransparentUI, swapchainAttachments, pickingAttachments] =
        mainPipelineCtx;
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
    auto globalCtx =
        make_aggregate_ref<"globalCtx", "device", "window", "surface", "swapchainBuild",
                           "swapchain", "frameContext", "commandPool", "commandBuffers",
                           "queue", "meshMap">(
            device, window, surface, swapchainBuild, swapchain, frameContext, commandPool,
            commandBuffers, GRAPHICS_AND_PRESENT, meshMap);

    auto mainCtx =
        make_aggregate_ref<"mainCtx", "pipelineOpaque3D", "pipelineTransparent3D",
                           "pipelineOpaqueUI", "pipelineTransparentUI", "pipelineLayout",
                           "depthResourcesBuild", "depthResource", "msaaResourcesBuild",
                           "msaaResource">(
            pipelineOpaque3D, pipelineTransparent3D, pipelineOpaqueUI,
            pipelineTransparentUI, pipelineLayout, depthResourcesBuild, depthResource,
            msaaResourcesBuild, msaaResource);

    auto mainShaderCtx = make_aggregate_ref<"mainShaderCtx", "drawRecorder",
                                            "uniformBuffers", "descriptorSets", "glyphs">(
        drawRecorder, uniformBuffers, descriptorSets, simpleGlyphs);
    auto inputCtx =
        make_aggregate_ref<"inputCtx", "input", "camera", "uiCamera", "clock">(
            input, camera, uiCamera, clock);

    // ===== hover：全局唯一一份 实体→函数 关联（外键 = 池实体下标）=====
    shader_data::hover_manager hoverManager{};

    // 测试绑定：所有字形共享同一个 hover 函数（演示"函数可被共享"）
    uint32_t testHover = hoverPool().bind([](picking_result r, bool enter) noexcept {
        std::println("[HOVER] type={} entity={} primitive={} {} (hover_fn={})",
                     r.key.object_type, r.key.entity_index, r.primitive_id,
                     enter ? "ENTER" : "LEAVE", r.hover_fn);
    });
    struct MyData : public ui::IData
    {
        int a;
        MyData(int val) : a(val) {} // 显式添加
    };

    struct Render : public ui::RenderData
    {
        using GlyphPool = std::remove_reference_t<decltype(glyphPool())>;

        static uint32_t hover_fn()
        {
            static uint32_t fn =
                hoverPool().bind([](picking_result r, bool enter) noexcept {
                    uint64_t ptr = glyphPool().template get<"data">(r.key.entity_index);
                    std::println(
                        "[Render-HOVER] type={} entity={} primitive={} {} (hover_fn={})",
                        r.key.object_type, r.key.entity_index, r.primitive_id,
                        enter ? "ENTER" : "LEAVE", r.hover_fn);

                    auto *node = reinterpret_cast<ui::Node *>(ptr);
                    auto *render = dynamic_cast<Render *>(node->renderData.get());
                    if (render)
                        std::println("text: {}", render->text);
                });
            return fn;
        }

        std::string text;
        std::vector<proxy_value<GlyphPool>> textGlyphProxies;

        Render(std::string s) : text(std::move(s))
        {
            (void)hover_fn();
        }

        void render(ui::Node *self, ui::render_context &context) override
        {
            auto *data_ptr = dynamic_cast<Render *>(self->renderData.get());
            if (!data_ptr)
                return;

            // ---- 1. 释放上一帧的代理 ----
            data_ptr->textGlyphProxies.clear();

            const auto &viewports = context.drawRecorder.currentDynamic.viewports;
            if (viewports.empty())
                return;
            float windowWidth = viewports[0].width;
            float windowHeight = viewports[0].height;
            // ---------- 状态无关的字形生成 Lambda（共享） ----------
            auto make_glyph_instance =
                [](const auto &g, float windowWidth, float windowHeight, float fontSizePx,
                   float cursorX, float baselineY, uint64_t data, uint32_t entity_index,
                   uint32_t hover_fn_id, glm::vec4 color,
                   uint32_t modulateFlag) -> shader_data::Glyph {
                // 1. 计算字形像素矩形
                float leftPx = cursorX + g.plane_bounds.left * fontSizePx;
                float rightPx = cursorX + g.plane_bounds.right * fontSizePx;
                float topPx = baselineY - g.plane_bounds.top * fontSizePx;
                float bottomPx = baselineY - g.plane_bounds.bottom * fontSizePx;
                float glyphWidthPx = rightPx - leftPx;
                float glyphHeightPx = bottomPx - topPx;

                // 2. 像素转 NDC（Vulkan Y 向下，无翻转）
                float ndcCenterX =
                    ((leftPx + glyphWidthPx * 0.5f) / windowWidth) * 2.0f - 1.0f;
                float ndcCenterY =
                    ((topPx + glyphHeightPx * 0.5f) / windowHeight) * 2.0f - 1.0f;
                float ndcW = (glyphWidthPx / windowWidth) * 2.0f;
                float ndcH = (glyphHeightPx / windowHeight) * 2.0f;

                // 3. UV 变换（翻转 V）
                UvTransform uv;
                uv.scale =
                    glm::vec2(static_cast<float>(g.uv_bounds.right - g.uv_bounds.left),
                              static_cast<float>(g.uv_bounds.top - g.uv_bounds.bottom));
                uv.offset = glm::vec2(static_cast<float>(g.uv_bounds.left),
                                      static_cast<float>(g.uv_bounds.bottom));

                // 4. 填充 Glyph
                shader_data::Glyph glyph{};
                glyph.data = data;
                glyph.entity_index = entity_index;
                glyph.textureIndex = g.font_ctx->bind.texture_index;
                glyph.samplerIndex = g.font_ctx->bind.sampler_index;
                glyph.fontType = static_cast<uint32_t>(g.font_ctx->type);
                glyph.pxRange = static_cast<float>(
                    g.font_ctx->font.atlas.distanceRange.value_or(0.0));
                glyph.modulateFlag = modulateFlag;
                glyph.color = color;
                glyph.model = glm::translate(glm::mat4(1.0f),
                                             glm::vec3(ndcCenterX, ndcCenterY, 0.0f)) *
                              glm::scale(glm::mat4(1.0f), glm::vec3(ndcW, ndcH, 1.0f));
                glyph.uvTransform = uv;
                glyph.hover_fn = hover_fn_id;
                return glyph;
            };

            // ==================== 第一块：测试五个位置的单字符 ====================
            {
                const std::string testStr = "ABCDE";
                auto textResult =
                    run_text_pipeline(context.fontSelect, testStr.data(), "zh-CN");
                const auto &shapeResult = textResult.shape_result;
                if (!shapeResult.empty() && !shapeResult[0].empty())
                {
                    // 收集字形指针（简化类型，直接使用 auto）
                    std::vector<const std::remove_cvref_t<decltype(shapeResult[0][0])> *>
                        glyphPtrs;
                    for (const auto &run : shapeResult)
                        for (const auto &g : run)
                            glyphPtrs.push_back(&g);

                    if (glyphPtrs.size() >= 5)
                    {
                        const float margin = 20.0f;
                        const float fontSizePx = 24.0f;
                        std::array<glm::vec2, 5> positions = {
                            glm::vec2(margin, margin),
                            glm::vec2(windowWidth - margin - fontSizePx, margin),
                            glm::vec2(windowWidth - margin - fontSizePx,
                                      windowHeight - margin - fontSizePx),
                            glm::vec2(margin, windowHeight - margin - fontSizePx),
                            glm::vec2(windowWidth * 0.5f, windowHeight * 0.5f)};

                        std::vector<shader_data::Glyph> instances;
                        for (int i = 0; i < 5; ++i)
                        {
                            const auto &g = *glyphPtrs[i];
                            if (g.plane_bounds == decltype(g.plane_bounds){})
                                continue;
                            float cursorX =
                                positions[i].x - g.plane_bounds.left * fontSizePx;
                            float baselineY =
                                positions[i].y + g.plane_bounds.top * fontSizePx;

                            auto entity_index = glyphPool().allocate();
                            shader_data::Glyph glyph = make_glyph_instance(
                                g, windowWidth, windowHeight, fontSizePx, cursorX,
                                baselineY, reinterpret_cast<uint64_t>(self), entity_index,
                                hover_fn(), glm::vec4(1.0f), 1);
                            auto proxy = glyphPool().make_soa_value(entity_index, glyph);
                            data_ptr->textGlyphProxies.push_back(std::move(proxy));
                            instances.push_back(std::move(glyph));
                        }
                        if (!instances.empty())
                            context.drawRecorder.addInstances(
                                std::span<const shader_data::Glyph>(instances));
                    }
                }
            }

            // ==================== 第二块：正式文本（顶部居中） ====================
            if (!data_ptr->text.empty())
            {
                auto textResult =
                    run_text_pipeline(context.fontSelect, data_ptr->text.data(), "zh-CN");
                const auto &shapeResult = textResult.shape_result;
                if (!shapeResult.empty() && !shapeResult[0].empty())
                {
                    const float fontSizePx = 24.0f;
                    const float topMargin = 20.0f;
                    const float baselineY = topMargin + fontSizePx;

                    // 计算文本总宽度（用于居中）
                    float totalWidth = 0.0f;
                    for (const auto &run : shapeResult)
                        for (const auto &g : run)
                            totalWidth += g.advance_x * fontSizePx;

                    float cursorX = (windowWidth - totalWidth) * 0.5f;

                    std::vector<shader_data::Glyph> instances;
                    for (const auto &run : shapeResult)
                    {
                        for (const auto &g : run)
                        {
                            if (g.plane_bounds == decltype(g.plane_bounds){})
                            {
                                cursorX += g.advance_x * fontSizePx;
                                continue;
                            }
                            auto entity_index = glyphPool().allocate();
                            shader_data::Glyph glyph = make_glyph_instance(
                                g, windowWidth, windowHeight, fontSizePx, cursorX,
                                baselineY, reinterpret_cast<uint64_t>(self), entity_index,
                                hover_fn(), glm::vec4(1.0f), 1);
                            auto proxy = glyphPool().make_soa_value(entity_index, glyph);
                            data_ptr->textGlyphProxies.push_back(std::move(proxy));
                            instances.push_back(std::move(glyph));
                            cursorX += g.advance_x * fontSizePx;
                        }
                    }

                    if (!instances.empty())
                        context.drawRecorder.addInstances(
                            std::span<const shader_data::Glyph>(instances));
                }
            }
        }
    };
    auto panel = ui::Container("panel")
                     .child(ui::Container("rectBox").data<MyData>("render", 1))
                     .renderData<Render>(std::string{"CD"})
                     .build();
    struct ui_tree
    {
        ui::Widget root;
        void layout(ui::Constraints c) noexcept
        {
            assert(root);
            if (!root->dirty)
                return; // 根节点不脏，整棵树无需布局
            try
            {
                c = c.deflate(root->margin);
                root->layout(c); // 递归布局，完成后整棵树 dirty 都会被清除
            }
            catch (const std::exception &e)
            {
                std::cerr << "FAIL: unexpected exception: " << e.what() << "\n";
            }
        }
        // NOTE: 写入GPU数据，就算启动了
        void render(ui::render_context &context)
        {
            const auto dfs = [&](this auto &self, ui::Node *node) {
                if (!node || !node->renderData)
                    return;
                node->renderData->render(node, context);
                for (auto &child : node->children)
                    self(child.get());
            };
            dfs(root.get());
        }
    };
    ui_tree uiTree{.root = std::move(panel)};
    uiTree.layout({WIDTH, WIDTH, HEIGHT, HEIGHT}); // 紧约束，强制根节点填满屏幕

    // 新的 soaCtx 仅包含 uiRects 和 uiWireRects
    auto soaCtx = make_aggregate_ref<"soaCtx", "uiTree">(uiTree);

    // record_info
    auto recordCtx = make_aggregate<"recordCtx", "info">(record_info{});
    auto world =
        make_aggregate_ref<"world", "globalCtx", "mainCtx", "mainShaderCtx", "pickCtx",
                           "recordCtx", "hoverPool", "hoverManager", "fontCtx">(
            globalCtx, mainCtx, mainShaderCtx, pickCtx, recordCtx, hoverPool(),
            hoverManager, fontCtx);
    using world_type = decltype(world);
    using input_type = decltype(inputCtx);
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
        auto &glyphs = mainShaderCtx.glyphs;
    };

    // diff: [test_indirectdraw] end
    // diff: [test_dod5] start
    static constexpr auto mainPipeline = make_aggregate<
        "mainPipeline", "transitionImageLayout", "beginRendering", "setPipelineState",
        "draw", "endRendering">(
        std::constant_wrapper<[](world_type &world, input_type &inputCtx,
                                 data_type &soaCtx) {
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
        std::constant_wrapper<[](world_type &world, input_type &inputCtx,
                                 data_type &soaCtx) {
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
        std::constant_wrapper<[](world_type &world, input_type &inputCtx,
                                 data_type &soaCtx) {
            auto &globalCtx = world.globalCtx;
            auto &mainCtx = world.mainCtx;
            auto &recordCtx = world.recordCtx;
            auto &mainShaderCtx = world.mainShaderCtx;

            auto &swapchain = globalCtx.swapchain;
            auto &commandBuffers = globalCtx.commandBuffers;

            const auto &[currentFrame, imageIndex] = recordCtx.info;
            const auto &commandBuffer = commandBuffers[currentFrame];

            auto imageExtent = swapchain.imageExtent();
            auto &pipelineLayout = mainCtx.pipelineLayout;

            auto &descriptorSets = mainShaderCtx.descriptorSets;
            auto descriptorSet = descriptorSets[currentFrame];

            auto &uniformBuffers = mainShaderCtx.uniformBuffers;

            // 绑定公共资源：索引缓冲、描述符集、推送常量公共部分（如缓冲地址）
            commandBuffer.bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                             *pipelineLayout, 0, 1, &(descriptorSet), 0,
                                             nullptr);

            auto uploadUniformBuffers = [&]() {
                UniformBufferObject ubo;
                // 3D 矩阵从 camera 获取
                ubo.cameraInfo[0].view = inputCtx.camera.getViewMatrix();
                ubo.cameraInfo[0].proj = inputCtx.camera.getProjMatrix();
                // UI 矩阵从 uiCamera 获取
                ubo.cameraInfo[1].view = inputCtx.uiCamera.getViewMatrix();
                ubo.cameraInfo[1].proj = inputCtx.uiCamera.getProjMatrix();

                memcpy(uniformBuffers[currentFrame].mapPtr(), &ubo, sizeof(ubo));
            };
            uploadUniformBuffers();
        }>{},
        std::constant_wrapper<[](world_type &world, input_type &inputCtx,
                                 data_type &soaCtx) {
            auto &mainShaderCtx = world.mainShaderCtx;
            auto &recordCtx = world.recordCtx;
            auto &globalCtx = world.globalCtx;
            auto &mainCtx = world.mainCtx;
            auto &fontCtx = world.fontCtx;

            auto &commandBuffers = globalCtx.commandBuffers;
            auto &pipelineLayout = mainCtx.pipelineLayout;

            const auto &[currentFrame, imageIndex] = recordCtx.info;
            const auto &commandBuffer = commandBuffers[currentFrame];

            // diff: [test_dod21] start: 区别在 直接往 GPU 写入数据.是 shaderDataRecorder 的增强版本
            // ============================================================
            // 使用 DrawRecorder 构建真实 UI 场景（含文字）
            // ============================================================
            auto &recorder = mainShaderCtx.drawRecorder[currentFrame];
            recorder.reset();

            auto &meshMap = globalCtx.meshMap;
            auto &quad_meta = meshMap["quad"];
            auto &swapchain = globalCtx.swapchain;
            auto extent = swapchain.refImageExtent();

            // ---------- 视口 / 裁剪 ----------
            VkViewport fullVP{.x = 0.0F,
                              .y = 0.0F,
                              .width = static_cast<float>(extent.width),
                              .height = static_cast<float>(extent.height),
                              .minDepth = 0.0F,
                              .maxDepth = 1.0F};
            VkRect2D fullSC{.offset = {.x = 0, .y = 0}, .extent = extent};

            PushData pcBase{.vertexAddress = recorder.globalVertexBuffer.address,
                            .instanceAddress = recorder.globalHeapBuffer.address,
                            .commandConstantsAddress =
                                recorder.commandConstantsBuffer.address,
                            .cameraIndex = 1};
            // ---------- 设置初始化状态 ----------
            recorder.setPipeline(*mainCtx.pipelineTransparentUI);
            recorder.setLayout(*mainCtx.pipelineLayout);
            recorder.setPushData(pcBase);
            recorder.setMesh(quad_meta);
            recorder.setViewports({fullVP});
            recorder.setScissors({fullSC});
        //-------------------------------------

#if 0
            const float elapsed = inputCtx.clock.getElapsed();
            uint32_t entityCounter = 0;
            auto nextId = [&]() {
                return entityCounter++;
            };

            auto makeRect = [&](glm::vec2 center, glm::vec2 size, glm::vec4 color,
                                uint32_t effects,
                                glm::vec4 radiusSoftness = glm::vec4(0.0f),
                                glm::vec2 shadowOffset = glm::vec2(0.0f),
                                glm::vec4 shadowColor = glm::vec4(0.0f)) {
                shader_data::Rectangle r{};
                r.entity_index = nextId();
                r.effects = effects;
                for (auto &c : r.colors)
                    c = color;
                r.model = glm::translate(glm::mat4(1.0f), glm::vec3(center, 0.0f));
                r.vertexTransform.matrix =
                    glm::scale(glm::mat4(1.0f), glm::vec3(size, 1.0f));
                r.uvTransform = UvTransform{glm::vec2(1.0f), glm::vec2(0.0f)};
                r.size = size;
                r.shadowOffset = shadowOffset;
                r.radiusSoftness = radiusSoftness;
                r.shadowColor = shadowColor;
                return r;
            };

            auto makeUiRect = [&](glm::vec2 center, glm::vec2 size, glm::vec4 color,
                                  float border) {
                shader_data::UiRect r{};
                r.entity_index = nextId();
                r.hover_fn = ~0U;
                r.center_size = glm::vec4(center, size);
                r.color = color;
                r.border = border;
                return r;
            };

            // ---------- 布局常量 ----------
            const float PANEL_W = 0.9f, PANEL_H = 0.7f;
            const float HEADER_Y = 0.18f, HEADER_H = 0.07f;
            const float TABLE_LEFT = -0.42f, TABLE_RIGHT = 0.42f;
            const float TABLE_TOP = 0.10f, TABLE_BOTTOM = -0.24f;
            const float TABLE_W = TABLE_RIGHT - TABLE_LEFT;
            const float TABLE_H = TABLE_TOP - TABLE_BOTTOM;
            const float TABLE_CENTER_Y = (TABLE_TOP + TABLE_BOTTOM) * 0.5f;
            const float ROW_H = 0.055f, ROW_GAP = 0.005f;
            const float BUTTON_W = 0.16f, BUTTON_H = 0.08f;
            const float BUTTON_Y = -0.29f;

            // ---------- 全屏数据 ----------
            std::vector<shader_data::Rectangle> fullRects;
            std::vector<shader_data::UiRect> fullUiRects;

            // 1. 背景面板（圆角 + 阴影 + 渐变）
            shader_data::Rectangle panel{};
            panel.entity_index = nextId();
            panel.effects =
                shader_data::FX_ROUNDED | shader_data::FX_SHADOW | shader_data::FX_FILL;
            panel.colors[0] = glm::vec4(0.18f, 0.20f, 0.25f, 1.0f);
            panel.colors[1] = glm::vec4(0.18f, 0.20f, 0.25f, 1.0f);
            panel.colors[2] = glm::vec4(0.10f, 0.11f, 0.14f, 1.0f);
            panel.colors[3] = glm::vec4(0.10f, 0.11f, 0.14f, 1.0f);
            panel.model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f));
            panel.vertexTransform.matrix =
                glm::scale(glm::mat4(1.0f), glm::vec3(PANEL_W, PANEL_H, 1.0f));
            panel.uvTransform = UvTransform{glm::vec2(1.0f), glm::vec2(0.0f)};
            panel.size = glm::vec2(PANEL_W, PANEL_H);
            panel.shadowOffset = glm::vec2(0.0f, -0.03f);
            panel.radiusSoftness = glm::vec4(0.03f, 0.005f, 0.02f, 1.0f);
            panel.shadowColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.35f);
            fullRects.push_back(panel);

            // 2. 表头背景
            fullRects.push_back(makeRect(glm::vec2(0.0f, HEADER_Y),
                                         glm::vec2(0.86f, HEADER_H),
                                         glm::vec4(0.28f, 0.30f, 0.36f, 1.0f),
                                         shader_data::FX_ROUNDED | shader_data::FX_FILL,
                                         glm::vec4(0.006f, 0.005f, 0.0f, 1.0f)));

            // 3. 两个按钮（带脉冲效果）
            float pulse = 0.5f + 0.5f * std::sin(elapsed * 3.0f);
            glm::vec4 okColor = glm::mix(glm::vec4(0.16f, 0.50f, 0.20f, 1.0f),
                                         glm::vec4(0.24f, 0.75f, 0.28f, 1.0f), pulse);
            glm::vec4 cancelColor = glm::mix(glm::vec4(0.50f, 0.16f, 0.16f, 1.0f),
                                             glm::vec4(0.75f, 0.24f, 0.24f, 1.0f), pulse);

            fullRects.push_back(makeRect(
                glm::vec2(0.25f, BUTTON_Y), glm::vec2(BUTTON_W, BUTTON_H), okColor,
                shader_data::FX_ROUNDED | shader_data::FX_SHADOW | shader_data::FX_FILL,
                glm::vec4(0.015f, 0.005f, 0.01f, 1.0f), glm::vec2(0.0f, -0.01f),
                glm::vec4(0.0f, 0.0f, 0.0f, 0.2f)));
            fullRects.push_back(makeRect(
                glm::vec2(-0.25f, BUTTON_Y), glm::vec2(BUTTON_W, BUTTON_H), cancelColor,
                shader_data::FX_ROUNDED | shader_data::FX_SHADOW | shader_data::FX_FILL,
                glm::vec4(0.015f, 0.005f, 0.01f, 1.0f), glm::vec2(0.0f, -0.01f),
                glm::vec4(0.0f, 0.0f, 0.0f, 0.2f)));

            // 4. 表格外框 + 列分隔线
            fullUiRects.push_back(makeUiRect(glm::vec2(0.0f, TABLE_CENTER_Y),
                                             glm::vec2(TABLE_W, TABLE_H),
                                             glm::vec4(1.0f, 1.0f, 1.0f, 0.25f), 0.002f));
            fullUiRects.push_back(makeUiRect(glm::vec2(-0.18f, TABLE_CENTER_Y),
                                             glm::vec2(0.002f, TABLE_H),
                                             glm::vec4(1.0f, 1.0f, 1.0f, 0.2f), 0.001f));
            fullUiRects.push_back(makeUiRect(glm::vec2(0.06f, TABLE_CENTER_Y),
                                             glm::vec2(0.002f, TABLE_H),
                                             glm::vec4(1.0f, 1.0f, 1.0f, 0.2f), 0.001f));
            fullUiRects.push_back(makeUiRect(glm::vec2(0.26f, TABLE_CENTER_Y),
                                             glm::vec2(0.002f, TABLE_H),
                                             glm::vec4(1.0f, 1.0f, 1.0f, 0.2f), 0.001f));

            // ---------- 滚动表格数据（仅矩形，无文字） ----------
            std::vector<shader_data::Rectangle> tableRects;
            const int totalRows = 12;
            float rowSpan = ROW_H + ROW_GAP;
            float scroll = std::fmod(elapsed * 0.2f, totalRows * rowSpan);
            int highlightRow = int(elapsed * 3.0f) % totalRows;

            for (int i = 0; i < totalRows; ++i)
            {
                float y = TABLE_TOP - ROW_H * 0.5f - i * rowSpan + scroll;
                glm::vec4 rowColor = (i % 2 == 0) ? glm::vec4(0.30f, 0.31f, 0.36f, 0.9f)
                                                  : glm::vec4(0.24f, 0.25f, 0.29f, 0.9f);
                if (i == highlightRow)
                    rowColor = glm::vec4(0.40f, 0.50f, 0.60f, 1.0f);

                tableRects.push_back(
                    makeRect(glm::vec2(0.0f, y), glm::vec2(TABLE_W, ROW_H), rowColor,
                             shader_data::FX_ROUNDED | shader_data::FX_FILL,
                             glm::vec4(0.004f, 0.003f, 0.0f, 1.0f)));
            }

            // 添加矩形（背景、表头、按钮）和线框
            recorder.addInstances(std::span<const shader_data::Rectangle>(fullRects));
            recorder.addInstances(std::span<const shader_data::UiRect>(fullUiRects));

            // ---------- 切换裁剪区域，绘制表格滚动内容 ----------
            int32_t scX = static_cast<int32_t>((TABLE_LEFT + 1.0f) * 0.5f * extent.width);
            int32_t scY = static_cast<int32_t>((1.0f - TABLE_TOP) * 0.5f * extent.height);
            uint32_t scW =
                static_cast<uint32_t>((TABLE_RIGHT - TABLE_LEFT) * 0.5f * extent.width);
            uint32_t scH =
                static_cast<uint32_t>((TABLE_TOP - TABLE_BOTTOM) * 0.5f * extent.height);
            VkRect2D tableSC{{scX, scY}, {scW, scH}};

            recorder.setScissors({tableSC}); // 状态改变 -> 结束上一个 DrawUnit
            recorder.addInstances(std::span<const shader_data::Rectangle>(tableRects));

            // ---------- 切回全屏状态，绘制文字（最上层） ----------
            recorder.setScissors({fullSC}); // 再次改变状态 -> 结束表格 DrawUnit
            recorder.addInstances(
                std::span<const shader_data::Glyph>(mainShaderCtx.glyphs));
#endif

            // NOTE: 上面是现有的，现在是uiTree的
            auto &uiTree = soaCtx.uiTree;
            ui::render_context context{.drawRecorder = recorder,
                                       .input = inputCtx.input,
                                       .clock = inputCtx.clock,
                                       .fontSelect = fontCtx.fontSelect};
            uiTree.render(context);

            recorder.end();
            recorder.doDraw(commandBuffer);
            // diff: [test_dod21] end
        }>{},
        std::constant_wrapper<[](world_type &world, input_type &inputCtx,
                                 data_type &soaCtx) {
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

            auto &mousePos = pickMouse.pos;

            commandBuffer.endRendering();
            // 无条件录制拷贝：鼠标无效时用最后位置，保证回读缓冲总是被写入（避免读到全零初值）
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
    static constexpr auto recordCommandBuffer = [](world_type &world,
                                                   input_type &inputCtx,
                                                   data_type &soaCtx) {
        auto &globalCtx = world.globalCtx;
        auto &recordCtx = world.recordCtx;

        auto &swapchain = globalCtx.swapchain;
        auto &commandBuffers = globalCtx.commandBuffers;

        auto [currentFrame, imageIndex] = recordCtx.info;
        const auto &commandBuffer = commandBuffers[currentFrame];
        VkImage image = swapchain.image(imageIndex);

        commandBuffer.begin({.sType = sType<VkCommandBufferBeginInfo>()});
        mainPipeline.transitionImageLayout(world, inputCtx, soaCtx);
        mainPipeline.beginRendering(world, inputCtx, soaCtx);
        mainPipeline.setPipelineState(world, inputCtx, soaCtx);
        mainPipeline.draw(world, inputCtx, soaCtx);
        mainPipeline.endRendering(world, inputCtx, soaCtx);

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

        surface.waitGoodFramebufferSize();
        device.waitIdle();

        swapchain = swapchainBuild.rebuild(device, surface);
        auto newExtent = swapchain.refImageExtent();
        VkExtent3D imageExtent = {
            .width = newExtent.width, .height = newExtent.height, .depth = 1};

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
                             if (!pickMouse.valid)
                                 r.key.object_type =
                                     0xFFFFFFFF; // 光标不在窗口 = 无命中（自动 leave）
                             // [PICKDBG] 定位用：每帧打印 GPU 回读原始值（可删）
                             std::println(
                                 "[PICKDBG] frame={} valid={} type={:#x} entity={} "
                                 "primitive={} hover_fn={}",
                                 currentFrame, pickMouse.valid, r.key.object_type,
                                 r.key.entity_index, r.primitive_id, r.hover_fn);
                             world.hoverManager.hover(r, world.hoverPool);
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
    while (globalCtx.window.shouldClose() == 0)
    {
        task_graph.invoke_ranges<"input_start", "input_end">(world, inputCtx, soaCtx);
        drawFrame(world, inputCtx, soaCtx);
    }
    device.waitIdle();

    std::cout << "main done\n";
    return 0;
}
catch (std::exception &e)
{
    std::println("main catch exception: {}", e.what());
}