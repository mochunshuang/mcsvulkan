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
        Slice<T> push_span(std::span<const T> src) noexcept
        {
            const uint32_t start = count;
            if (count + src.size() > pool.size())
                pool.resize(std::max<size_t>(pool.size() * 2, count + src.size()));
            std::memcpy(pool.data() + start, src.data(), src.size() * sizeof(T));
            count += static_cast<uint32_t>(src.size());
            return {start, static_cast<uint32_t>(src.size())};
        }

        // 追加单个元素，返回它的引用（便于就地进行进一步赋值）
        T &push_one(const T &v) noexcept
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

                    cmd.pushConstants(unit.staticKey.layout, VK_SHADER_STAGE_VERTEX_BIT,
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
    };

    template <typename B>
    concept WidgetBuild = requires(B &&b) {
        static_cast<std::unique_ptr<Widget>>(std::forward<B>(b));
    };
    struct ScreenWidget
    {

        constexpr void makeLayoutDirty(uint32_t frame) noexcept
        { layoutDirty_[frame] = true; }
        constexpr void clearLayoutDirty(uint32_t frame) noexcept
        { layoutDirty_[frame] = false; }
        constexpr bool isLayoutDirty(uint32_t frame) const noexcept
        { return layoutDirty_[frame]; }

        constexpr void makeRenderDirty(uint32_t frame) noexcept
        { renderDirty_[frame] = true; }
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
        constexpr void updateSize(double width,double height)
        {
            size_ = {width,height};
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
                return;
            }
            if(isRenderDirty(frame))
            {
                render(context); //NOTE: 布局不变，仅仅是数据渲染更新
                clearRenderDirty(frame);
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
        constexpr ContainerWidget(std::string key,
                                  std::optional<BoxConstraints> constraints,
                                  std::optional<Alignment> alignment,
                                  std::optional<EdgeInsetsGeometry> margin,
                                  std::optional<EdgeInsetsGeometry> border,
                                  std::optional<EdgeInsetsGeometry> padding,
                                  std::unique_ptr<Widget> child) noexcept
            : constraints_{constraints}, alignment_{alignment}, margin_{margin},
              border_{border}, padding_{padding}, child_{std::move(child)} // NOLINTEND
        {
            Self().key = std::move(key);
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
                std::move(key_),
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
        AlignWidget(std::string key, Alignment alignment,
                    std::optional<double> widthFactor, std::optional<double> heightFactor,
                    std::unique_ptr<Widget> child) noexcept // NOLINTEND
            : alignment_{alignment}, widthFactor_{widthFactor},
              heightFactor_{heightFactor}, child_{std::move(child)}
        {
            Self().key = std::move(key);
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
            return std::make_unique<AlignWidget>(std::move(key_), alignment_,
                                                 widthFactor_, heightFactor_,
                                                 std::move(child_));
        }
        constexpr auto &key(std::string key) noexcept
        {
            key_ = std::move(key);
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
        constexpr SizedBoxWidget(std::string key, std::optional<double> width,
                                 std::optional<double> height,
                                 std::unique_ptr<Widget> child) noexcept
            : width_{width}, height_{height}, child_{std::move(child)} // NOLINTEND
        {
            Self().key = std::move(key);
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
            return std::make_unique<SizedBoxWidget>(std::move(key_), width_, height_,
                                                    std::move(child_));
        }

        constexpr auto &key(std::string key) noexcept
        {
            key_ = std::move(key);
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
        constexpr PaddingWidget(std::string key, EdgeInsetsGeometry padding,
                                std::unique_ptr<Widget> child) noexcept
            : padding_{padding}, child_{std::move(child)}
        {
            Self().key = std::move(key);
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
            return std::make_unique<PaddingWidget>(std::move(key_), *padding_,
                                                   std::move(child_));
        }

        constexpr auto &key(std::string key) noexcept
        {
            key_ = std::move(key);
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
        std::optional<EdgeInsetsGeometry> padding_;
        std::unique_ptr<Widget> child_;
    };
    static constexpr auto Padding(std::string key = {}) noexcept // NOLINT
    {
        return PaddingBuild{std::move(key)};
    }

    struct ConstrainedBoxWidget : Widget
    {
        constexpr ConstrainedBoxWidget(std::string key, BoxConstraints constraints,
                                       std::unique_ptr<Widget> child) noexcept
            : constraints_{constraints}, child_{std::move(child)}
        {
            Self().key = std::move(key);
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
            return std::make_unique<ConstrainedBoxWidget>(std::move(key_), *constraints_,
                                                          std::move(child_));
        }

        constexpr auto &key(std::string key) noexcept
        {
            key_ = std::move(key);
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
        constexpr OverflowBoxWidget(std::string key, Alignment alignment,
                                    std::optional<double> minWidth,
                                    std::optional<double> maxWidth,
                                    std::optional<double> minHeight,
                                    std::optional<double> maxHeight, OverflowBoxFit fit,
                                    std::unique_ptr<Widget> child) noexcept // NOLINTEND
            : alignment_{alignment}, minWidth_{minWidth}, maxWidth_{maxWidth},
              minHeight_{minHeight}, maxHeight_{maxHeight}, fit_{fit},
              child_{std::move(child)}
        {
            Self().key = std::move(key);
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
                std::move(key_), alignment_, minWidth_, maxWidth_, minHeight_, maxHeight_,
                fit_, std::move(child_));
        }

        constexpr auto &key(std::string key) noexcept
        {
            key_ = std::move(key);
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
            std::string key, Alignment alignment, std::optional<Axis> constrainedAxis,
            std::unique_ptr<Widget> child) noexcept // NOLINTEND
            : alignment_{alignment}, constrainedAxis_{constrainedAxis},
              child_{std::move(child)}
        {
            Self().key = std::move(key);
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
                std::move(key_), alignment_, constrainedAxis_, std::move(child_));
        }

        constexpr auto &key(std::string key) noexcept
        {
            key_ = std::move(key);
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
        constexpr LimitedBoxWidget(std::string key, double maxWidth, double maxHeight,
                                   std::unique_ptr<Widget> child) noexcept // NOLINTEND
            : maxWidth_{maxWidth}, maxHeight_{maxHeight}, child_{std::move(child)}
        {
            Self().key = std::move(key);
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
            return std::make_unique<LimitedBoxWidget>(std::move(key_), maxWidth_,
                                                      maxHeight_, std::move(child_));
        }

        constexpr auto &key(std::string key) noexcept
        {
            key_ = std::move(key);
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
        constexpr AspectRatioWidget(std::string key, double aspectRatio,
                                    std::unique_ptr<Widget> child) noexcept // NOLINTEND
            : aspectRatio_{aspectRatio}, child_{std::move(child)}
        {
            Self().key = std::move(key);
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
            return std::make_unique<AspectRatioWidget>(std::move(key_), aspectRatio_,
                                                       std::move(child_));
        }

        constexpr auto &key(std::string key) noexcept
        {
            key_ = std::move(key);
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
            std::string key, Alignment alignment, std::optional<double> widthFactor,
            std::optional<double> heightFactor,
            std::unique_ptr<Widget> child) noexcept // NOLINTEND
            : alignment_{alignment}, widthFactor_{widthFactor},
              heightFactor_{heightFactor}, child_{std::move(child)}
        {
            Self().key = std::move(key);
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
                std::move(key_), alignment_, widthFactor_, heightFactor_,
                std::move(child_));
        }

        constexpr auto &key(std::string key) noexcept
        {
            key_ = std::move(key);
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
        constexpr FlexWidget(std::string key, Axis direction,
                             MainAxisAlignment mainAxisAlignment,
                             MainAxisSize mainAxisSize,
                             CrossAxisAlignment crossAxisAlignment,
                             std::optional<TextDirection> textDirection,
                             VerticalDirection verticalDirection,
                             std::optional<TextBaseline> textBaseline,
                             std::vector<std::unique_ptr<Widget>> children,
                             std::vector<FlexInfo> flexes) noexcept
            : direction_{direction}, mainAxisAlignment_{mainAxisAlignment},
              mainAxisSize_{mainAxisSize}, crossAxisAlignment_{crossAxisAlignment},
              textDirection_{textDirection}, verticalDirection_{verticalDirection},
              textBaseline_{textBaseline}, children_{std::move(children)},
              flexes_{std::move(flexes)}
        {
            Self().key = std::move(key);
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
                std::move(key_), *direction_, mainAxisAlignment_, mainAxisSize_,
                crossAxisAlignment_, textDirection_, verticalDirection_, textBaseline_,
                std::move(children_), std::move(flexes_));
        }

        constexpr auto &key(std::string key) noexcept
        {
            key_ = std::move(key);
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
            return std::make_unique<SizedBoxWidget>(std::move(key_), std::nullopt,
                                                    std::nullopt, nullptr);
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
        constexpr WrapWidget(std::string key, Axis direction, WrapAlignment alignment,
                             double spacing, WrapAlignment runAlignment,
                             double runSpacing, WrapCrossAlignment crossAxisAlignment,
                             std::optional<TextDirection> textDirection,
                             VerticalDirection verticalDirection,
                             std::vector<std::unique_ptr<Widget>> children) noexcept
            : direction_{direction}, alignment_{alignment}, spacing_{spacing},
              runAlignment_{runAlignment}, runSpacing_{runSpacing},
              crossAxisAlignment_{crossAxisAlignment}, textDirection_{textDirection},
              verticalDirection_{verticalDirection},
              children_{std::move(children)} // NOLINTEND
        {
            Self().key = std::move(key);
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
            return std::make_unique<WrapWidget>(std::move(key_), direction_, alignment_,
                                                spacing_, runAlignment_, runSpacing_,
                                                crossAxisAlignment_, textDirection_,
                                                verticalDirection_, std::move(children_));
        }

      private:
        std::string key_;
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
        StackWidget(std::string key, Alignment alignment, StackFit fit,
                    std::vector<std::unique_ptr<Widget>> children,
                    std::vector<StackChildInfo> infos) noexcept
            : alignment_{alignment}, fit_{fit}, children_{std::move(children)},
              infos_{std::move(infos)}
        {
            Self().key = std::move(key);
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
            return std::make_unique<StackWidget>(std::move(key_), alignment_, fit_,
                                                 std::move(children_), std::move(infos_));
        }

      private:
        std::string key_;
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
        OffstageWidget(std::string key, bool offstage,
                       std::unique_ptr<Widget> child) noexcept
            : offstage_{offstage}, child_{std::move(child)}
        {
            Self().key = std::move(key);
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
            return std::make_unique<OffstageWidget>(std::move(key_), offstage_,
                                                    std::move(child_));
        }

        auto &key(std::string k) noexcept
        {
            key_ = std::move(k);
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
        CustomSingleChildLayoutWidget(std::string key,
                                      std::unique_ptr<SingleChildLayoutDelegate> delegate,
                                      std::unique_ptr<Widget> child) noexcept
            : delegate_{std::move(delegate)}, child_{std::move(child)}
        {
            Self().key = std::move(key);
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
                std::move(key_), std::move(delegate_), std::move(child_));
        }

      private:
        std::string key_;
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
        CustomMultiChildLayoutWidget(std::string key,
                                     std::unique_ptr<MultiChildLayoutDelegate> delegate,
                                     std::vector<std::unique_ptr<Widget>> children,
                                     std::vector<std::string> ids) noexcept
            : delegate_{std::move(delegate)}, children_{std::move(children)},
              ids_{std::move(ids)}
        {
            Self().key = std::move(key);
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
                std::move(key_), std::move(delegate_), std::move(children_),
                std::move(ids_));
        }

      private:
        std::string key_;
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
        RotatedBoxWidget(std::string key, int quarterTurns,
                         std::unique_ptr<Widget> child) noexcept
            : quarterTurns_{quarterTurns}, child_{std::move(child)}
        {
            Self().key = std::move(key);
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
            return std::make_unique<RotatedBoxWidget>(std::move(key_), quarterTurns_,
                                                      std::move(child_));
        }

      private:
        std::string key_;
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

namespace my_ui
{
    using GlyphPool = std::remove_reference_t<decltype(glyphPool())>;

    struct TextBoxWidget;

    struct TextRenderObject : ui_new::RenderObject
    {
        std::string text;
        std::vector<proxy_value<GlyphPool>> textGlyphProxies;

        // ---------- 缓存：最终提交用的 Glyph 数组 ----------
        // cachedGlyphs 里的 model 矩阵已经含 origin 偏移 + NDC 转换，
        // 因此只要 (owner->offset, viewport size) 不变，可以逐帧复用。
        std::vector<shader_data::Glyph> cachedGlyphs;
        ui_new::Offset cachedOwnerOffset{-1e30, -1e30};
        glm::vec2 cachedViewport{-1.0f, -1.0f};

        explicit TextRenderObject(std::string s) : text(std::move(s)) {}

        static uint32_t hover_fn();

        void render(ui_new::ScreenWidget *screen, ui_new::Widget *owner,
                    render_context &context) override
        {
            (void)screen;

            const auto &viewports = context.drawRecorder.currentDynamic.viewports;
            if (viewports.empty())
                return;

            const float windowWidth = viewports[0].width;
            const float windowHeight = viewports[0].height;
            const ui_new::Offset off = owner->offset;

            const bool cacheValid =
                !cachedGlyphs.empty() && cachedOwnerOffset.x == off.x &&
                cachedOwnerOffset.y == off.y && cachedViewport.x == windowWidth &&
                cachedViewport.y == windowHeight;

            if (!cacheValid)
            {
                rebuildCache(owner, windowWidth, windowHeight, context);
                cachedOwnerOffset = off;
                cachedViewport = {windowWidth, windowHeight};
            }

            // 每帧只走这一条：memcpy cachedGlyphs 到 mapped GPU buffer，
            // 并追加一条 indirect draw command（recorder 内部完成）
            if (!cachedGlyphs.empty())
                context.drawRecorder.addInstances(
                    std::span<const shader_data::Glyph>(cachedGlyphs));
        }

      private:
        // 只在 cache 失效时调用：重新 shape、重新分配 SoA 实体、生成 cachedGlyphs
        void rebuildCache(ui_new::Widget *owner, float windowWidth, float windowHeight,
                          render_context &context)
        {
            textGlyphProxies.clear();
            cachedGlyphs.clear();

            // ---------- 状态无关的字形生成 Lambda ----------
            auto make_glyph = [](const auto &g, float W, float H, float fontSizePx,
                                 float cursorX, float baselineY, uint64_t data,
                                 uint32_t entity_index, uint32_t hover_fn_id,
                                 glm::vec4 color,
                                 uint32_t modulateFlag) -> shader_data::Glyph {
                float leftPx = cursorX + g.plane_bounds.left * fontSizePx;
                float rightPx = cursorX + g.plane_bounds.right * fontSizePx;
                float topPx = baselineY - g.plane_bounds.top * fontSizePx;
                float bottomPx = baselineY - g.plane_bounds.bottom * fontSizePx;
                float w = rightPx - leftPx;
                float h = bottomPx - topPx;

                // 像素 → NDC（Vulkan Y 向下，无翻转）
                float cx = ((leftPx + w * 0.5f) / W) * 2.0f - 1.0f;
                float cy = ((topPx + h * 0.5f) / H) * 2.0f - 1.0f;

                // UV 变换（翻转 V）
                UvTransform uv;
                uv.scale =
                    glm::vec2(static_cast<float>(g.uv_bounds.right - g.uv_bounds.left),
                              static_cast<float>(g.uv_bounds.top - g.uv_bounds.bottom));
                uv.offset = glm::vec2(static_cast<float>(g.uv_bounds.left),
                                      static_cast<float>(g.uv_bounds.bottom));

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
                glyph.model = glm::translate(glm::mat4(1.0f), glm::vec3(cx, cy, 0.0f)) *
                              glm::scale(glm::mat4(1.0f),
                                         glm::vec3(w / W * 2.0f, h / H * 2.0f, 1.0f));
                glyph.uvTransform = uv;
                glyph.hover_fn = hover_fn_id;
                return glyph;
            };

            const uint64_t data_ptr = reinterpret_cast<uint64_t>(owner);
            const uint32_t hfn = hover_fn();

            // ================= 第一块：五位置单字符 =================
            {
                const std::string testStr = "ABCDE";
                auto textResult =
                    run_text_pipeline(context.fontSelect, testStr.data(), "zh-CN");
                const auto &shapeResult = textResult.shape_result;
                if (!shapeResult.empty() && !shapeResult[0].empty())
                {
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
                            shader_data::Glyph glyph =
                                make_glyph(g, windowWidth, windowHeight, fontSizePx,
                                           cursorX, baselineY, data_ptr, entity_index,
                                           hfn, glm::vec4(1.0f), 1);
                            auto proxy = glyphPool().make_soa_value(entity_index, glyph);
                            textGlyphProxies.push_back(std::move(proxy));
                            cachedGlyphs.push_back(std::move(glyph));
                        }
                    }
                }
            }

            // ================= 第二块：正式文本（顶部居中） =================
            if (!text.empty())
            {
                auto textResult =
                    run_text_pipeline(context.fontSelect, text.data(), "zh-CN");
                const auto &shapeResult = textResult.shape_result;
                if (!shapeResult.empty() && !shapeResult[0].empty())
                {
                    const float fontSizePx = 24.0f;
                    const float topMargin = 20.0f;
                    const float baselineY = topMargin + fontSizePx;

                    float totalWidth = 0.0f;
                    for (const auto &run : shapeResult)
                        for (const auto &g : run)
                            totalWidth += g.advance_x * fontSizePx;

                    float cursorX = (windowWidth - totalWidth) * 0.5f;

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
                            shader_data::Glyph glyph =
                                make_glyph(g, windowWidth, windowHeight, fontSizePx,
                                           cursorX, baselineY, data_ptr, entity_index,
                                           hfn, glm::vec4(1.0f), 1);
                            auto proxy = glyphPool().make_soa_value(entity_index, glyph);
                            textGlyphProxies.push_back(std::move(proxy));
                            cachedGlyphs.push_back(std::move(glyph));
                            cursorX += g.advance_x * fontSizePx;
                        }
                    }
                }
            }
        }
    };

    struct TextBoxWidget : ui_new::Widget
    {
        TextBoxWidget(std::string key, std::string text)
        {
            this->key = std::move(key);
            this->renderObject = std::make_unique<TextRenderObject>(std::move(text));
        }
        void layout(ui_new::BoxConstraints c) override
        {
            this->size = c.smallest();
        }
        void updateOffset(ui_new::Offset o) noexcept override
        {
            this->offset = o;
        }
        [[nodiscard]] constexpr std::span<const std::unique_ptr<Widget>> children()
            const noexcept override
        {
            return {};
        }
    };

    inline uint32_t TextRenderObject::hover_fn()
    {
        static uint32_t fn = hoverPool().bind([](picking_result r, bool enter) noexcept {
            uint64_t ptr = glyphPool().template get<"data">(r.key.entity_index);
            std::println("[Text-HOVER] type={} entity={} primitive={} {} (hover_fn={})",
                         r.key.object_type, r.key.entity_index, r.primitive_id,
                         enter ? "ENTER" : "LEAVE", r.hover_fn);

            auto *widget = reinterpret_cast<ui_new::Widget *>(ptr);
            if (!widget)
                return;
            auto *box = dynamic_cast<TextBoxWidget *>(widget);
            if (!box)
                return;
            auto *render = dynamic_cast<TextRenderObject *>(box->renderObject.get());
            if (!render)
                return;
            std::println("text: {}", render->text);
        });
        return fn;
    }
} // namespace my_ui

//diff: [test_dod24.cpp] end [替换旧的布局，并更新录制的算法]

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

    ui_new::ScreenWidget screen{
        ui_new::Size{WIDTH, HEIGHT},
        ui_new::Container("panel")
            .child(ui_new::Container("rectBox").width(0.0).height(0.0))
            .child(std::make_unique<my_ui::TextBoxWidget>("text", "CD"))};

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
            auto &screen = soaCtx.screen;

            if (screen.hasDirty(currentFrame))
            {
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
                recorder.setViewports(std::span{&fullVP, 1});
                recorder.setScissors(std::span{&fullSC, 1});
                //-------------------------------------
            }

            render_context context{.drawRecorder = recorder,
                                   .input = inputCtx.input,
                                   .clock = inputCtx.clock,
                                   .fontSelect = fontCtx.fontSelect};
            screen.recorderCmd(currentFrame, context);

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