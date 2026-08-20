
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

#include "../head.hpp"
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtx/string_cast.hpp>
#include <glm/fwd.hpp>

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
};
namespace shader_data
{
    //  C++是静态类型语言。传递指针，必须能用指定的结构体，解析指针
    struct Glyph
    {
        static constexpr auto type_id = 2;
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
    static_assert(sizeof(Glyph) == 124);

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
    // NOTE: 负责生成命令。 必须使用顶点绘制才能驱动GPU
    struct ShaderDataRecorder
    {
        BufferResourceWithAddress globalVertexBuffer{};
        BufferResource globalIndexBuffer{};

        BufferResourceWithAddress globalHeapBuffer{};
        BufferResource indirectDrawBuffer{};
        BufferResourceWithAddress commandConstantsBuffer{};

        // NOTE: 最佳或新增写入，压缩.要求必须连续
        VkDrawIndexedIndirectCommand currentCommand{};
        CommandConstant currentCommandConstant{};

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

        constexpr ShaderDataRecorder(const LogicalDevice &device,
                                     VkDeviceSize vertexCapacity,
                                     VkDeviceSize indexCapacity,
                                     VkDeviceSize heapCapacity,
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

        // write data help
        std::optional<VkDrawIndexedIndirectCommand> pending_cmd;
        std::optional<shader_data::CommandConstant> pending_constant;
        size_t heapOffsetStart = 0;
        size_t indirectDrawOffsetStart = 0;
        size_t commandConstantsOffsetStart = 0;
        static constexpr size_t appendIndirectDrawOffset =
            sizeof(VkDrawIndexedIndirectCommand);
        static constexpr size_t appendCommandConstantsOffset =
            sizeof(shader_data::CommandConstant);
        constexpr auto write_shader_data(const auto &data, const mesh_data &meta)
        {
            using T = std::remove_cvref_t<decltype(data[0])>;
            constexpr auto type_id = T::type_id;

            const size_t append_offset = data.size() * sizeof(T);
            this->globalHeapBuffer.write(heapOffsetStart, data.data(), append_offset);

            VkDrawIndexedIndirectCommand cmd =
                meta.getDrawCommand(static_cast<uint32_t>(data.size()));
            shader_data::CommandConstant constant = {
                .type_id = type_id,
                .adddress_offset = static_cast<uint32_t>(heapOffsetStart)};

            heapOffsetStart += append_offset;

            // NOTE: 决定是否生成命令
            if (not pending_cmd) [[unlikely]]
            {
                pending_cmd = cmd;
                pending_constant = constant;
            }
            else [[likely]]
            {
                VkDrawIndexedIndirectCommand &pre_cmd = *pending_cmd;
                shader_data::CommandConstant &pre_constant = *pending_constant;
                const auto can_cmd_merge = [&] {
                    // NOTE: 就 instanceCount 不同，就能合并cmd。简化处理。目的是使用的是同一种mesh
                    return pre_cmd.indexCount == cmd.indexCount &&
                           pre_cmd.firstIndex == cmd.firstIndex &&
                           pre_cmd.vertexOffset == cmd.vertexOffset &&
                           pre_cmd.firstInstance == cmd.firstInstance;
                };
                const auto can_constant_merge = [&] {
                    // NOTE: 目的是使用的是同一类的  shader_data
                    return pre_constant.type_id == constant.type_id;
                };
                if (can_cmd_merge() && can_constant_merge())
                {
                    // 不写入 indirectDrawBuffer、commandConstantsBuffer
                    pre_cmd.instanceCount += cmd.instanceCount;
                    // adddress_offset 不变，adddress_offset是起始地址无需改变
                }
                else
                {
                    // 写入 indirectDrawBuffer、commandConstantsBuffer。真实生成一条实例命令
                    this->indirectDrawBuffer.write(indirectDrawOffsetStart, &pre_cmd,
                                                   appendIndirectDrawOffset);
                    this->commandConstantsBuffer.write(commandConstantsOffsetStart,
                                                       &pre_constant,
                                                       appendCommandConstantsOffset);

                    pending_cmd = cmd;
                    pending_constant = constant;
                    indirectDrawOffsetStart += appendIndirectDrawOffset;
                    commandConstantsOffsetStart += appendCommandConstantsOffset;
                }
            }
        };
        constexpr auto flush_pending_cmd()
        {
            if (pending_cmd)
            {
                VkDrawIndexedIndirectCommand &pre_cmd = *pending_cmd;
                shader_data::CommandConstant &pre_constant = *pending_constant;
                this->indirectDrawBuffer.write(indirectDrawOffsetStart, &pre_cmd,
                                               appendIndirectDrawOffset);
                this->commandConstantsBuffer.write(commandConstantsOffsetStart,
                                                   &pre_constant,
                                                   appendCommandConstantsOffset);

                pending_cmd = {};
                pending_constant = {};
                heapOffsetStart = 0;
                indirectDrawOffsetStart = 0;
                commandConstantsOffsetStart = 0;
            }
        };
    };

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
        uint32_t entity_index; // 布局节点索引（拾取外键）
        uint32_t hover_fn;     // hover 池下标（0xFFFFFFFF = 未绑定）
        glm::vec4 center_size; // center.xy + size.xy（NDC）
        glm::vec4 color;       // 边框色
        float border;          // 边框半宽（NDC）
    };
    static_assert(sizeof(UiRect) == 44);

    // ===================== 圆角阴影矩形（照抄 test_sdf Rectangle；TYPE_ROUND_RECT=5）=====================
    // 与 GLSL Rectangle（RECTANGLE_SIZE=264）严格一致；HTML box-shadow 风格
    struct VertexTransform
    {
        glm::mat4 matrix;
    };
    struct Rectangle
    {
        static constexpr auto type_id = 5;
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
    static_assert(sizeof(Rectangle) == 264);
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
constexpr auto initShaderDataRecorder(const LogicalDevice &device)
{
    constexpr auto vertexCapacity = sizeof(Vertex) * 1000;
    constexpr auto indexCapacity = sizeof(uint32_t) * 1000;
    constexpr auto heapCapacity = sizeof(shader_data::Glyph) * 2000;
    // NOTE: 目前是不超过100条命令的
    constexpr auto indirectDrawCapacity = sizeof(VkDrawIndexedIndirectCommand) * 100;
    constexpr auto commandConstantsCapacity = sizeof(shader_data::CommandConstant) * 100;

    std::array<shader_data::ShaderDataRecorder, MAX_FRAMES_IN_FLIGHT> shaderDataRecorder{
        shader_data::ShaderDataRecorder(device, vertexCapacity, indexCapacity,
                                        heapCapacity, indirectDrawCapacity,
                                        commandConstantsCapacity),
        shader_data::ShaderDataRecorder(device, vertexCapacity, indexCapacity,
                                        heapCapacity, indirectDrawCapacity,
                                        commandConstantsCapacity)};
    return shaderDataRecorder;
}

struct PushData
{
    // 字段顺序必须与 test_dod19.vert 的 PushConsts 完全一致
    // （GLSL: vertex/data/commandConstants/cameraIndex，Glyph 不需要 attributeAddress）
    uint64_t vertexAddress;           // 全局顶点缓冲区地址
    uint64_t instanceAddress;         // 全局实例堆地址（shader: dataAddress）
    uint64_t commandConstantsAddress; // 命令常量缓冲区地址
    uint32_t cameraIndex;             // 0: 3D, 1: UI
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

using mcs::vulkan::ecs::static_string;
//diff: [test_dod17] start
// ====================== 全局基础类型：Entity 与 RefAny ======================
struct Entity
{
    using release_type = std::move_only_function<void(uint32_t) noexcept>;
    static constexpr auto invalid = ~0u;
    uint32_t type_id = invalid;
    uint32_t entity_id = invalid;
    release_type release;

    constexpr bool valid() const noexcept
    {
        return type_id != invalid;
    }
    constexpr Entity(uint32_t type_id, uint32_t entity_id, release_type release)
        : type_id{type_id}, entity_id{entity_id}, release{std::move(release)}
    {
    }
    Entity(const Entity &) = delete;
    Entity &operator=(const Entity &) = delete;
    constexpr Entity(Entity &&o) noexcept
        : type_id(std::exchange(o.type_id, invalid)),
          entity_id(std::exchange(o.entity_id, invalid)), release(std::move(o.release))
    {
    }
    constexpr Entity &operator=(Entity &&o) noexcept
    {
        if (this != &o)
        {
            if (type_id != invalid)
                release(entity_id);
            type_id = std::exchange(o.type_id, invalid);
            entity_id = std::exchange(o.entity_id, invalid);
            release = std::move(o.release);
        }
        return *this;
    }
    constexpr ~Entity() noexcept
    {
        if (type_id != invalid)
        {
            release(entity_id);
            type_id = invalid;
            entity_id = invalid;
        }
    }
};

class RefAny
{
    void *ptr_ = nullptr;
    std::type_index type_ = typeid(void);

  public:
    constexpr RefAny() = default;
    template <typename T>
    constexpr RefAny(T &value) noexcept
        : ptr_(static_cast<void *>(std::addressof(value))), type_(typeid(T))
    {
    }

    constexpr bool has_value() const noexcept
    {
        return ptr_ != nullptr;
    }
    constexpr void *raw_address() const noexcept
    {
        return ptr_;
    }

    template <typename T>
    constexpr friend T &any_cast(RefAny &any)
    {
        if (any.type_ != typeid(T))
            throw std::bad_cast();
        return *static_cast<T *>(any.ptr_);
    }
    template <typename T>
    constexpr friend const T &any_cast(const RefAny &any)
    {
        if (any.type_ != typeid(T))
            throw std::bad_cast();
        return *static_cast<const T *>(any.ptr_);
    }

    template <typename T>
    constexpr auto &cast_to(this auto &&self)
    {
        return any_cast<T>(self);
    }

    template <typename T>
    constexpr auto value_or(this auto &&self, T value) noexcept
        requires(requires { *(any_cast<T>(self)); })
    {
        try
        {
            auto &ref = any_cast<T>(self);
            return ref ? *ref : value;
        }
        catch (...)
        {
        }
        return value;
    }

    template <typename T>
    constexpr T miss_return(this auto &&self, T defaultVal) noexcept
    {
        if (!self.has_value())
            return defaultVal;
        return any_cast<T>(self);
    }
};

// ====================== 全局辅助模板 ======================
template <static_string name>
static constexpr auto do_get_member(auto &soaCtx, const Entity &ref)
{
    return soaCtx.get_member.template operator()<name>(soaCtx, ref);
}

// ====================== Trait 基类（全局）======================
template <static_string Name>
struct TraitBase
{
    static constexpr auto name = Name;
    template <typename SoaCtx>
    static consteval uint32_t type_id(SoaCtx &)
    {
        return std::remove_cvref_t<SoaCtx>::find_name(name);
    }
    template <typename SoaCtx>
    constexpr static auto release(SoaCtx &soaCtx)
        -> std::move_only_function<void(uint32_t) noexcept>
    {
        return [&soaCtx](uint32_t entity_id) noexcept {
            constexpr auto members = std::remove_cvref_t<decltype(soaCtx)>::members;
            constexpr auto I = type_id(soaCtx);
            return soaCtx.[:members[I]:].release_entity(entity_id);
        };
    }
};

// ====================== UI 布局核心（命名空间）======================
namespace ui
{

    // 基础几何类型
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

    // 方向枚举
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

    // [NEW] FlexFit 枚举，对应 Flutter 的 FlexFit
    enum class FlexFit
    {
        tight, // 强制填满分配空间（Expanded 默认行为）
        loose  // 允许子组件小于分配空间（Flexible 行为）
    };

    struct BoxGeometry
    {
        float x = 0, y = 0, w = 0, h = 0;
    };

    // 扁平树节点（使用全局 Entity）：布局只负责产出几何（位置坐标），不承载事件
    struct FlatNode
    {
        Entity ref; // 全局 Entity，ui 内可直接访问
        int parent = -1;
        int firstChild = -1;
        int nextSibling = -1;
        BoxGeometry geometry;
        std::string name;
        float baseline = 0;
    };

    // 树管理器
    class FlatLayoutTree
    {
      public:
        std::vector<FlatNode> nodes;

        static constexpr auto printLayoutTree = [](this auto &&self, auto &soaCtx,
                                                   const auto &tree, int nodeIdx = 0,
                                                   int depth = 0, float parentAbsX = 0.0f,
                                                   float parentAbsY = 0.0f) {
            if (nodeIdx < 0 || nodeIdx >= static_cast<int>(tree.nodes.size()))
                return;

            const FlatNode &node = tree.nodes[nodeIdx];
            std::string indent(depth * 2, ' ');

            // 计算绝对坐标（累积父节点偏移）
            float absX = parentAbsX + node.geometry.x;
            float absY = parentAbsY + node.geometry.y;
            float absRight = absX + node.geometry.w;
            float absBottom = absY + node.geometry.h;

            // 从 SOA 上下文中提取样式（若不存在则返回默认值）
            EdgeInsets pad =
                do_get_member<"padding">(soaCtx, node.ref).miss_return(EdgeInsets{});
            EdgeInsets margin =
                do_get_member<"margin">(soaCtx, node.ref).miss_return(EdgeInsets{});
            EdgeInsets border =
                do_get_member<"border">(soaCtx, node.ref).miss_return(EdgeInsets{});

            // 打印节点基本信息
            std::println("{}Node Layout [{}]:", indent, node.name);
            // 相对坐标（相对于父节点）
            std::println("{}  position (relative): left={}, top={}, right={}, bottom={}",
                         indent, node.geometry.x, node.geometry.y,
                         node.geometry.x + node.geometry.w,
                         node.geometry.y + node.geometry.h);
            // 绝对坐标（相对于根节点）
            std::println("{}  position (absolute): left={}, top={}, right={}, bottom={}",
                         indent, absX, absY, absRight, absBottom);
            std::println("{}  size: width={}, height={}", indent, node.geometry.w,
                         node.geometry.h);

            // 样式信息
            std::println("{}  padding: top={}, left={}, bottom={}, right={}", indent,
                         pad.top, pad.left, pad.bottom, pad.right);
            std::println("{}  margin: top={}, left={}, bottom={}, right={}", indent,
                         margin.top, margin.left, margin.bottom, margin.right);
            std::println("{}  border: top={}, left={}, bottom={}, right={}", indent,
                         border.top, border.left, border.bottom, border.right);

            // 额外信息：如果有文本节点可打印基线（可选）
            std::println("{}  baseline: {}", indent, node.baseline);

            // 递归打印子节点（传入当前绝对坐标作为父绝对坐标）
            for (int c = node.firstChild; c != -1; c = tree.nodes[c].nextSibling)
            {
                self(soaCtx, tree, c, depth + 1, absX, absY);
            }
        };

        void reserve(size_t n)
        {
            nodes.reserve(n);
        }
        const auto &getNode(size_t index) noexcept
        {
            return nodes[index];
        }

        int addNode(std::string name, Entity ref, int parentIdx)
        {
            int idx = static_cast<int>(nodes.size());
            nodes.push_back({std::move(ref), parentIdx, -1, -1, {}, std::move(name), 0});
            if (parentIdx != -1)
            {
                FlatNode &p = nodes[parentIdx];
                if (p.firstChild == -1)
                    p.firstChild = idx;
                else
                {
                    int sib = p.firstChild;
                    while (nodes[sib].nextSibling != -1)
                        sib = nodes[sib].nextSibling;
                    nodes[sib].nextSibling = idx;
                }
            }
            return idx;
        }

        // 将 childIdx 从其当前父节点移动到 newParentIdx 下，并插入到 beforeSibling 之前。
        // 若 beforeSibling == -1，则追加到末尾。
        constexpr void moveNode(int childIdx, int newParentIdx,
                                int beforeSibling = -1) noexcept
        {
            if (childIdx == -1 || newParentIdx == -1 || childIdx == newParentIdx)
                return;
            FlatNode &child = nodes[childIdx];
            int oldParent = child.parent;
            if (oldParent == newParentIdx && beforeSibling == -1)
                return; // 已在末尾无需移动

            // 1. 从旧父节点摘除
            if (oldParent != -1)
            {
                FlatNode &oldP = nodes[oldParent];
                if (oldP.firstChild == childIdx)
                {
                    oldP.firstChild = child.nextSibling;
                }
                else
                {
                    int prev = oldP.firstChild;
                    while (prev != -1 && nodes[prev].nextSibling != childIdx)
                        prev = nodes[prev].nextSibling;
                    if (prev != -1)
                        nodes[prev].nextSibling = child.nextSibling;
                }
            }

            // 2. 插入到新父节点的兄弟链中，位于 beforeSibling 之前
            child.parent = newParentIdx;
            FlatNode &newP = nodes[newParentIdx];

            if (beforeSibling == -1 || beforeSibling == newP.firstChild)
            {
                // 插入头部
                child.nextSibling = newP.firstChild;
                newP.firstChild = childIdx;
            }
            else
            {
                // 查找 beforeSibling 的前驱
                int prev = newP.firstChild;
                while (prev != -1 && nodes[prev].nextSibling != beforeSibling)
                    prev = nodes[prev].nextSibling;
                if (prev != -1)
                {
                    child.nextSibling = beforeSibling;
                    nodes[prev].nextSibling = childIdx;
                }
                else
                {
                    // 未找到 beforeSibling，则追加到末尾
                    int last = newP.firstChild;
                    while (last != -1 && nodes[last].nextSibling != -1)
                        last = nodes[last].nextSibling;
                    if (last != -1)
                    {
                        nodes[last].nextSibling = childIdx;
                        child.nextSibling = -1;
                    }
                    else
                    {
                        newP.firstChild = childIdx;
                        child.nextSibling = -1;
                    }
                }
            }
        }
        // 在 FlatLayoutTree 类内部添加
        constexpr std::optional<int> findNodeByName(
            const std::string &name) const noexcept
        {
            for (int i = 0; i < static_cast<int>(nodes.size()); ++i)
                if (nodes[i].name == name)
                    return i;
            return std::nullopt;
        }

        // 便捷移动封装
        constexpr void moveNodeByName(const std::string &childName,
                                      const std::string &newParentName) noexcept
        {
            auto c = findNodeByName(childName);
            auto p = findNodeByName(newParentName);
            if (c && p)
                moveNode(*c, *p);
        }

        // 从树中摘除节点（不释放向量槽位，索引稳定；修复父/兄弟链）
        constexpr void removeNode(int idx) noexcept
        {
            if (idx < 0 || idx >= static_cast<int>(nodes.size()))
                return;
            FlatNode &child = nodes[idx];
            int oldParent = child.parent;
            if (oldParent != -1)
            {
                FlatNode &oldP = nodes[oldParent];
                if (oldP.firstChild == idx)
                {
                    oldP.firstChild = child.nextSibling;
                }
                else
                {
                    int prev = oldP.firstChild;
                    while (prev != -1 && nodes[prev].nextSibling != idx)
                        prev = nodes[prev].nextSibling;
                    if (prev != -1)
                        nodes[prev].nextSibling = child.nextSibling;
                }
            }
            child.parent = -1;
            child.nextSibling = -1;
            child.firstChild = -1;
        }

        constexpr void clear()
        {
            nodes.clear();
        }

        template <typename F>
            requires(requires(F &f) { f(0); })
        constexpr void forEachChild(int idx, F &&f) const noexcept(noexcept(f(0)))
        {
            for (int c = nodes[idx].firstChild; c != -1; c = nodes[c].nextSibling)
                f(c);
        }

        constexpr void printLayout(auto &soaCtx, int nodeIdx = 0, int depth = 0,
                                   float parentAbsX = 0.0f, float parentAbsY = 0.0f) const
        {
            printLayoutTree(soaCtx, *this, nodeIdx, depth, parentAbsX, parentAbsY);
        }
    };

    // 主轴/交叉轴工具函数
    enum class AxisDirection
    {
        right,
        left,
        down,
        up
    };

    inline AxisDirection axisDirectionForFlex(bool isRow, TextDirection td,
                                              VerticalDirection vd)
    {
        if (isRow)
            return (td == TextDirection::ltr) ? AxisDirection::right
                                              : AxisDirection::left;
        else
            return (vd == VerticalDirection::down) ? AxisDirection::down
                                                   : AxisDirection::up;
    }

    inline bool isAxisForward(AxisDirection dir)
    {
        return dir == AxisDirection::right || dir == AxisDirection::down;
    }

    inline float mainAxisSizeFromConstraints(const Constraints &bc, bool isRow)
    {
        return isRow ? bc.maxW : bc.maxH;
    }
    inline float crossAxisSizeFromConstraints(const Constraints &bc, bool isRow)
    {
        return isRow ? bc.maxH : bc.maxW;
    }

    inline float mainAxisPaddingStart(const EdgeInsets &pad, AxisDirection dir)
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
    inline float mainAxisPaddingEnd(const EdgeInsets &pad, AxisDirection dir)
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
    inline float crossAxisPaddingStart(const EdgeInsets &pad, AxisDirection dir)
    {
        if (dir == AxisDirection::right || dir == AxisDirection::left)
            return pad.top;
        else
            return pad.left;
    }
    inline float crossAxisPaddingEnd(const EdgeInsets &pad, AxisDirection dir)
    {
        if (dir == AxisDirection::right || dir == AxisDirection::left)
            return pad.bottom;
        else
            return pad.right;
    }

    inline float childMainAxisLength(const FlatNode &child, bool isRow)
    {
        return isRow ? child.geometry.w : child.geometry.h;
    }
    inline float childCrossAxisLength(const FlatNode &child, bool isRow)
    {
        return isRow ? child.geometry.h : child.geometry.w;
    }

    inline void setChildMainAxisPosition(FlatNode &child, AxisDirection dir, float pos,
                                         float childMainLen, float parentMainSize,
                                         float padStart, float padEnd)
    {
        float physical = isAxisForward(dir)
                             ? padStart + pos
                             : parentMainSize - padEnd - pos - childMainLen;
        if (dir == AxisDirection::right || dir == AxisDirection::left)
            child.geometry.x = physical;
        else
            child.geometry.y = physical;
    }
    inline void setChildCrossAxisPosition(FlatNode &child, AxisDirection dir, float pos,
                                          float /*childCrossLen*/,
                                          float /*parentCrossSize*/, float padCrossStart,
                                          float /*padCrossEnd*/)
    {
        if (dir == AxisDirection::right || dir == AxisDirection::left)
            child.geometry.y = padCrossStart + pos;
        else
            child.geometry.x = padCrossStart + pos;
    }

    inline bool mainAxisIsBounded(const Constraints &bc, bool isRow)
    {
        return isRow ? bc.hasBoundedWidth() : bc.hasBoundedHeight();
    }
    inline bool crossAxisIsBounded(const Constraints &bc, bool isRow)
    {
        return isRow ? bc.hasBoundedHeight() : bc.hasBoundedWidth();
    }

    inline Constraints makeFlexAxisConstraints(bool isRow, float minMain, float maxMain,
                                               float minCross, float maxCross)
    {
        if (isRow)
            return {minMain, maxMain, minCross, maxCross};
        else
            return {minCross, maxCross, minMain, maxMain};
    }

    // 布局辅助函数
    inline Constraints makeLooseConstraints(const Constraints &c)
    {
        return {0.0f, c.maxW, 0.0f, c.maxH};
    }

    inline Size childFullSize(auto &soaCtx, const FlatNode &child)
    {
        EdgeInsets m =
            do_get_member<"margin">(soaCtx, child.ref).miss_return(EdgeInsets{});
        return {child.geometry.w + m.horizontal(), child.geometry.h + m.vertical()};
    }

    inline Offset alignmentOffset(const Alignment &align, float extraW, float extraH,
                                  const EdgeInsets &pad, const EdgeInsets &margin,
                                  const EdgeInsets &border)
    {
        return {border.left + pad.left + margin.left + extraW * (align.x + 1.0f) / 2.0f,
                border.top + pad.top + margin.top + extraH * (align.y + 1.0f) / 2.0f};
    }

    inline void positionChildByAlignment(auto &soaCtx, FlatNode &child,
                                         const Constraints &containerConstraints,
                                         const EdgeInsets &padding,
                                         const std::optional<Alignment> &align,
                                         const EdgeInsets &border)
    {
        EdgeInsets childMargin =
            do_get_member<"margin">(soaCtx, child.ref).miss_return(EdgeInsets{});
        Size childFull = childFullSize(soaCtx, child);
        float contentW =
            containerConstraints.maxW - border.horizontal() - padding.horizontal();
        float contentH =
            containerConstraints.maxH - border.vertical() - padding.vertical();
        float extraW = std::max(0.0f, contentW - childFull.width);
        float extraH = std::max(0.0f, contentH - childFull.height);
        Alignment al = align.value_or(Align::topLeft);
        Offset offset = alignmentOffset(al, extraW, extraH, padding, childMargin, border);
        child.geometry.x = offset.x;
        child.geometry.y = offset.y;
    }

    // ====================== FlexChildInfo ======================
    struct FlexChildInfo
    {
        int nodeIdx = -1;
        float flex = 0;
        FlexFit flexFit = FlexFit::tight; // [NEW] 默认为 tight
        EdgeInsets margin;

        float marginMain(bool isRow) const
        {
            return isRow ? margin.horizontal() : margin.vertical();
        }
        float marginCross(bool isRow) const
        {
            return isRow ? margin.vertical() : margin.horizontal();
        }
    };

    // ====================== collectFlexChildren ======================
    inline std::vector<FlexChildInfo> collectFlexChildren(auto &soaCtx,
                                                          FlatLayoutTree &tree,
                                                          int parentIdx, bool isRow,
                                                          float &outTotalFlex)
    {
        std::vector<FlexChildInfo> infos;
        outTotalFlex = 0.0f;
        constexpr auto expandedTypeId =
            std::decay_t<decltype(soaCtx)>::find_name("expandeds");
        static_assert(expandedTypeId != ~0u);

        for (int c = tree.nodes[parentIdx].firstChild; c != -1;
             c = tree.nodes[c].nextSibling)
        {
            FlatNode &child = tree.nodes[c];
            if (size_t(child.ref.type_id) == expandedTypeId)
            {
                if (child.firstChild == -1)
                    throw std::logic_error("Expanded/Flexible widget has no child.");
                int realIdx = child.firstChild;
                auto [flexVal, flexFitVal] = // [FIX] 读取 flexFit
                    soaCtx.expandeds.template view_entity<"flex", "flexFit">(
                        0, child.ref.entity_id);
                EdgeInsets m = do_get_member<"margin">(soaCtx, tree.nodes[realIdx].ref)
                                   .miss_return(EdgeInsets{});
                infos.push_back({realIdx, (float)flexVal, flexFitVal, m});
                outTotalFlex += flexVal;
            }
            else
            {
                EdgeInsets m =
                    do_get_member<"margin">(soaCtx, child.ref).miss_return(EdgeInsets{});
                infos.push_back({c, 0.0f, FlexFit::tight, m});
            }
        }
        return infos;
    }

    // ====================== layoutChildrenNaturally ======================
    inline std::vector<float> layoutChildrenNaturally(
        auto &soaCtx, FlatLayoutTree &tree, const std::vector<FlexChildInfo> &infos,
        bool isRow, bool /*isBoundedMain*/, float /*innerMain*/, float innerCross,
        CrossAxisAlignment crossAlign)
    {
        std::vector<float> naturalMain;
        naturalMain.reserve(infos.size());
        for (const auto &info : infos)
        {
            // 弹性子组件用 0 主轴最大约束（为了测量交叉轴）
            float childMainMax = (info.flex > 0) ? 0.0f : Constraints::inf;
            float childCrossMax = innerCross;
            float childCrossMin =
                (crossAlign == CrossAxisAlignment::stretch) ? childCrossMax : 0.0f;
            Constraints childBC = makeFlexAxisConstraints(isRow, 0.0f, childMainMax,
                                                          childCrossMin, childCrossMax);
            // 非弹性子组件才 deflate margin（弹性子组件后续布局会处理）
            if (info.flex == 0)
                childBC = childBC.deflate(info.margin);
            soaCtx.layout(soaCtx, tree, info.nodeIdx, childBC);
            naturalMain.push_back(childMainAxisLength(tree.nodes[info.nodeIdx], isRow));
        }
        return naturalMain;
    }

    // ====================== distributeFlexSpace ======================
    inline void distributeFlexSpace(auto &soaCtx, FlatLayoutTree &tree,
                                    const std::vector<FlexChildInfo> &infos,
                                    std::vector<float> &naturalMain, bool isRow,
                                    float freeMain, float totalFlex, float innerCross,
                                    CrossAxisAlignment crossAlign)
    {
        if (totalFlex <= 0 || freeMain <= 0)
            return;
        float flexUnit = freeMain / totalFlex;
        for (size_t i = 0; i < infos.size(); ++i)
        {
            if (infos[i].flex > 0)
            {
                float allocated = infos[i].flex * flexUnit;
                float childCrossMax = innerCross;
                float childCrossMin =
                    (crossAlign == CrossAxisAlignment::stretch) ? childCrossMax : 0.0f;

                // [FIX] 根据 flexFit 选择约束类型
                float minMain = (infos[i].flexFit == FlexFit::loose) ? 0.0f : allocated;
                float maxMain = allocated;

                Constraints flexBC = makeFlexAxisConstraints(
                    isRow, minMain, maxMain, childCrossMin, childCrossMax);
                // 扣除 margin
                flexBC = flexBC.deflate(infos[i].margin);
                soaCtx.layout(soaCtx, tree, infos[i].nodeIdx, flexBC);
                naturalMain[i] = childMainAxisLength(tree.nodes[infos[i].nodeIdx], isRow);
            }
        }
    }

    // computeFinalMainSize 不变
    inline float computeFinalMainSize(bool isBoundedMain, float totalNatural,
                                      float innerMain, MainAxisSize axisSize)
    {
        if (!isBoundedMain)
            return totalNatural;
        if (axisSize == MainAxisSize::min)
            return std::min(totalNatural, innerMain);
        return innerMain;
    }

    // computeMainGapAndStart 不变
    inline void computeMainGapAndStart(size_t childCount, float extraMain,
                                       MainAxisAlignment mainAlign, float &outMainGap,
                                       float &outMainStartOff)
    {
        outMainGap = 0.0f;
        outMainStartOff = 0.0f;
        if (childCount == 1)
        {
            if (mainAlign == MainAxisAlignment::spaceAround ||
                mainAlign == MainAxisAlignment::spaceEvenly)
                outMainStartOff = extraMain / 2.0f;
        }
        else if (childCount > 1)
        {
            switch (mainAlign)
            {
            case MainAxisAlignment::start:
                outMainStartOff = 0.0f;
                break;
            case MainAxisAlignment::end:
                outMainStartOff = extraMain;
                break;
            case MainAxisAlignment::center:
                outMainStartOff = extraMain / 2.0f;
                break;
            case MainAxisAlignment::spaceBetween:
                outMainGap = extraMain / (childCount - 1);
                break;
            case MainAxisAlignment::spaceAround:
                outMainGap = extraMain / childCount;
                outMainStartOff = outMainGap / 2.0f;
                break;
            case MainAxisAlignment::spaceEvenly:
                outMainGap = extraMain / (childCount + 1);
                outMainStartOff = outMainGap;
                break;
            }
        }
    }

    // ====================== positionChildrenInFlex ======================
    inline void positionChildrenInFlex(
        auto &soaCtx, FlatLayoutTree &tree, const std::vector<FlexChildInfo> &infos,
        const std::vector<float> &naturalMain, AxisDirection dir, bool isRow,
        float /*innerMain*/, float innerCross, float padMainStart, float padMainEnd,
        float padCrossStart, float padCrossEnd, CrossAxisAlignment crossAlign,
        float mainGap, float mainStartOff, float parentMainSize, float parentCrossSize)
    {
        float mainPos = mainStartOff;
        float maxBaseline = 0;
        if (crossAlign == CrossAxisAlignment::baseline && isRow)
        {
            for (const auto &info : infos)
                maxBaseline = std::max(maxBaseline, tree.nodes[info.nodeIdx].baseline);
        }

        for (size_t i = 0; i < infos.size(); ++i)
        {
            FlatNode &child = tree.nodes[infos[i].nodeIdx];
            const EdgeInsets &childMargin = infos[i].margin;
            float childMainLen = naturalMain[i];
            float childCrossLen = childCrossAxisLength(child, isRow);

            float leadingMargin = isRow ? childMargin.left : childMargin.top;
            float crossLeadingMargin = isRow ? childMargin.top : childMargin.left;

            // 主轴位置 = 分配位置 + leading margin
            setChildMainAxisPosition(child, dir, mainPos + leadingMargin, childMainLen,
                                     parentMainSize, padMainStart, padMainEnd);

            float crossStart = 0;
            float crossExtra = std::max(
                0.0f, innerCross - (childCrossLen + infos[i].marginCross(isRow)));
            switch (crossAlign)
            {
            case CrossAxisAlignment::start:
                crossStart = 0;
                break;
            case CrossAxisAlignment::end:
                crossStart = crossExtra;
                break;
            case CrossAxisAlignment::center:
                crossStart = crossExtra / 2.0f;
                break;
            case CrossAxisAlignment::stretch:
                crossStart = 0;
                break;
            case CrossAxisAlignment::baseline:
                crossStart = isRow ? maxBaseline - child.baseline : 0;
                break;
            }
            // 交叉轴位置 = 对齐位置 + cross leading margin
            setChildCrossAxisPosition(child, dir, crossStart + crossLeadingMargin,
                                      childCrossLen, parentCrossSize, padCrossStart,
                                      padCrossEnd);

            mainPos += naturalMain[i] + infos[i].marginMain(isRow) + mainGap;
        }
    }

    // ====================== layoutFlexImpl (关键修正) ======================
    template <bool isRow>
    void layoutFlexImpl(auto &soaCtx, FlatLayoutTree &tree, int nodeIdx,
                        Constraints borderBC, const EdgeInsets &pad, AxisDirection dir,
                        CrossAxisAlignment crossAlign, MainAxisSize axisSize,
                        MainAxisAlignment mainAlign, std::optional<float> w,
                        std::optional<float> h)
    {
        FlatNode &node = tree.nodes[nodeIdx];
        bool isBoundedMain = mainAxisIsBounded(borderBC, isRow);
        bool isBoundedCross = crossAxisIsBounded(borderBC, isRow);

        // [FIX] 根据 Flutter 规范：交叉轴必须始终有界，否则直接报错
        if (!isBoundedCross)
            throw std::logic_error("Row/Column '" + node.name +
                                   "' has unbounded cross axis. Cross axis must be "
                                   "bounded in Flutter flex layout.");

        if (!isBoundedMain)
        {
            for (int c = node.firstChild; c != -1; c = tree.nodes[c].nextSibling)
                if (tree.nodes[c].ref.type_id ==
                    (uint32_t)std::remove_cvref_t<decltype(soaCtx)>::find_name(
                        "expandeds"))
                    throw std::logic_error(
                        "Row/Column '" + node.name +
                        "' has unbounded main axis and Expanded/Flexible child.");
        }

        float mainSize = mainAxisSizeFromConstraints(borderBC, isRow);
        float crossSize = crossAxisSizeFromConstraints(borderBC, isRow);
        float padMainStart = mainAxisPaddingStart(pad, dir);
        float padMainEnd = mainAxisPaddingEnd(pad, dir);
        float padCrossStart = crossAxisPaddingStart(pad, dir);
        float padCrossEnd = crossAxisPaddingEnd(pad, dir);
        float innerMain = mainSize - padMainStart - padMainEnd;
        if (!isBoundedMain)
            innerMain = Constraints::inf;
        float innerCross = crossSize - padCrossStart - padCrossEnd;

        float totalFlex = 0;
        auto infos = collectFlexChildren(soaCtx, tree, nodeIdx, isRow, totalFlex);
        auto naturalMain = layoutChildrenNaturally(
            soaCtx, tree, infos, isRow, isBoundedMain, innerMain, innerCross, crossAlign);

        float totalNatural = 0.0f;
        for (size_t i = 0; i < infos.size(); ++i)
        {
            if (infos[i].flex == 0)
                totalNatural += naturalMain[i] + infos[i].marginMain(isRow);
        }

        float finalInnerMain =
            computeFinalMainSize(isBoundedMain, totalNatural, innerMain, axisSize);
        float freeMain = std::max(0.0f, finalInnerMain - totalNatural);
        distributeFlexSpace(soaCtx, tree, infos, naturalMain, isRow, freeMain, totalFlex,
                            innerCross, crossAlign);

        float totalMainUsed = 0.0f;
        for (size_t i = 0; i < infos.size(); ++i)
            totalMainUsed += naturalMain[i] + infos[i].marginMain(isRow);

        float extraMain = std::max(0.0f, finalInnerMain - totalMainUsed);
        float mainGap = 0.0f, mainStartOff = 0.0f;
        computeMainGapAndStart(infos.size(), extraMain, mainAlign, mainGap, mainStartOff);

        positionChildrenInFlex(soaCtx, tree, infos, naturalMain, dir, isRow, innerMain,
                               innerCross, padMainStart, padMainEnd, padCrossStart,
                               padCrossEnd, crossAlign, mainGap, mainStartOff, mainSize,
                               crossSize);

        Size size;
        if (isRow)
        {
            size.width = finalInnerMain + pad.horizontal();
            size.height = innerCross + pad.vertical();
        }
        else
        {
            size.width = innerCross + pad.horizontal();
            size.height = finalInnerMain + pad.vertical();
        }
        if (w)
            size.width = *w;
        if (h)
            size.height = *h;
        size = borderBC.clamp(size);

        node.geometry = {0, 0, size.width, size.height};

        if (!infos.empty())
        {
            FlatNode &first = tree.nodes[infos[0].nodeIdx];
            node.baseline = isRow ? first.baseline + first.geometry.y
                                  : first.baseline + first.geometry.x;
        }
        else
        {
            node.baseline = isRow ? size.height : size.width;
        }
    }

} // namespace ui

using namespace ui;

// ====================== Container Trait（重写以符合 Flutter 规范）======================
struct ContainerStyleTrait : TraitBase<"containers">
{
    static constexpr bool has_single_child = true;

    template <typename SoaCtx>
    static Entity make(SoaCtx &soaCtx, std::optional<float> width = std::nullopt,
                       std::optional<float> height = std::nullopt, EdgeInsets margin = {},
                       EdgeInsets padding = {}, EdgeInsets border = {},
                       std::optional<Alignment> alignment = std::nullopt,
                       std::optional<Constraints> constraints = std::nullopt)
    {
        uint32_t idx = soaCtx.containers.new_entity(width, height, margin, padding,
                                                    border, alignment, constraints);
        return {type_id(soaCtx), idx, release(soaCtx)};
    }

    // [FIX] 完全按照 Flutter Container 布局算法重写
    static void layout(auto &soaCtx, auto &selfSoa, FlatLayoutTree &tree, int nodeIdx,
                       Constraints borderBC, const EdgeInsets &pad)
    {
        FlatNode &node = tree.nodes[nodeIdx];
        auto [border, ownConstraints, width, height, alignment] =
            selfSoa.template view_entity<"border", "constraints", "width", "height",
                                         "alignment">(0, node.ref.entity_id);

        if (ownConstraints)
            borderBC = borderBC.intersect(*ownConstraints);

        // 1. 无子组件：尽可能大（或显式尺寸）
        if (node.firstChild == -1)
        {
            float w = width.has_value()
                          ? *width
                          : (borderBC.hasBoundedWidth() ? borderBC.maxW : 0.0f);
            float h = height.has_value()
                          ? *height
                          : (borderBC.hasBoundedHeight() ? borderBC.maxH : 0.0f);
            Size size = borderBC.clamp({w, h});
            node.geometry = {0, 0, size.width, size.height};
            node.baseline = size.height;
            return;
        }

        // 2. 有子组件：必须唯一
        int childIdx = node.firstChild;
        if (tree.nodes[childIdx].nextSibling != -1)
            throw std::logic_error("Container '" + node.name +
                                   "' must have exactly one child.");

        FlatNode &child = tree.nodes[childIdx];
        EdgeInsets childMargin =
            do_get_member<"margin">(soaCtx, child.ref).miss_return(EdgeInsets{});

        // 计算内部约束（去除 border 和 container 自己的 padding）
        Constraints innerBC = borderBC.deflate(border).deflate(pad);

        if (alignment.has_value())
        {
            // --- 有 alignment：容器尺寸优先看显式宽高，否则尝试填满父约束（有界时），最后被子组件撑开 ---

            // 1. 先布局子组件，使用 loose 约束（内部扣除 margin）
            Constraints childBC = makeLooseConstraints(innerBC);
            childBC = childBC.deflate(childMargin);
            soaCtx.layout(soaCtx, tree, childIdx, childBC);

            Size childFull = childFullSize(soaCtx, child);

            // 2. 确定容器最终尺寸
            // 使用 innerBC（已扣除 padding 和 border），而非 borderBC
            float containerW =
                width.has_value()
                    ? *width
                    : (innerBC.hasBoundedWidth()
                           ? innerBC.maxW
                           : childFull.width + border.horizontal() + pad.horizontal());
            float containerH =
                height.has_value()
                    ? *height
                    : (innerBC.hasBoundedHeight()
                           ? innerBC.maxH
                           : childFull.height + border.vertical() + pad.vertical());

            Size containerSize = borderBC.clamp({containerW, containerH});

            // 3. 根据 alignment 定位子组件（使用容器的实际尺寸作为对齐参考空间）
            Constraints containerAsConstraints{0, containerSize.width, 0,
                                               containerSize.height};
            positionChildByAlignment(soaCtx, child, containerAsConstraints, pad,
                                     alignment, border);

            // 4. 设置当前节点几何
            node.geometry = {0, 0, containerSize.width, containerSize.height};
            node.baseline = child.baseline + child.geometry.y;
        }
        else
        {
            // --- 无 alignment：容器尺寸由子组件决定，子组件接收 loose 约束 ---
            Constraints childBC = makeLooseConstraints(innerBC).deflate(childMargin);
            soaCtx.layout(soaCtx, tree, childIdx, childBC);

            Size childFull = childFullSize(soaCtx, child);
            float containerW =
                width.value_or(childFull.width + border.horizontal() + pad.horizontal());
            float containerH =
                height.value_or(childFull.height + border.vertical() + pad.vertical());
            Size containerSize = borderBC.clamp({containerW, containerH});

            // 子组件定位到左上角（默认无 alignment 行为）
            child.geometry.x = border.left + pad.left + childMargin.left;
            child.geometry.y = border.top + pad.top + childMargin.top;

            node.geometry = {0, 0, containerSize.width, containerSize.height};
            node.baseline = child.baseline + child.geometry.y;
        }
    }
};
using ContainerStyleObject =
    gen_soa_struct<ContainerStyleTrait, {"width", ^^std::optional<float>},
                   {"height", ^^std::optional<float>}, {"margin", ^^EdgeInsets},
                   {"padding", ^^EdgeInsets}, {"border", ^^EdgeInsets},
                   {"alignment", ^^std::optional<Alignment>},
                   {"constraints", ^^std::optional<Constraints>}>;

// ====================== Row / Column / Expanded / Text Trait ======================
struct RowStyleTrait : TraitBase<"rows">
{
    static void layout(auto &soaCtx, auto &selfSoa, FlatLayoutTree &tree, int nodeIdx,
                       Constraints borderBC, const EdgeInsets &pad)
    {
        auto &ref = tree.nodes[nodeIdx].ref;
        auto [td, crossAlign, axisSize, mainAlign, w, h] =
            selfSoa.template view_entity<"textDirection", "crossAlign", "mainAxisSize",
                                         "mainAlign", "width", "height">(0,
                                                                         ref.entity_id);
        AxisDirection dir = axisDirectionForFlex(true, td, VerticalDirection::down);
        layoutFlexImpl<true>(soaCtx, tree, nodeIdx, borderBC, pad, dir, crossAlign,
                             axisSize, mainAlign, w, h);
    }

    static Entity make(auto &soaCtx, std::optional<float> width = std::nullopt,
                       std::optional<float> height = std::nullopt, EdgeInsets margin = {},
                       EdgeInsets padding = {},
                       MainAxisAlignment mainAlign = MainAxisAlignment::start,
                       CrossAxisAlignment crossAlign = CrossAxisAlignment::start,
                       MainAxisSize mainAxisSize = MainAxisSize::max,
                       TextDirection textDirection = TextDirection::ltr)
    {
        uint32_t idx = soaCtx.rows.new_entity(width, height, margin, padding, mainAlign,
                                              crossAlign, mainAxisSize, textDirection);
        return {type_id(soaCtx), idx, release(soaCtx)};
    }
};
using RowStyleObject =
    gen_soa_struct<RowStyleTrait, {"width", ^^std::optional<float>},
                   {"height", ^^std::optional<float>}, {"margin", ^^EdgeInsets},
                   {"padding", ^^EdgeInsets}, {"mainAlign", ^^MainAxisAlignment},
                   {"crossAlign", ^^CrossAxisAlignment}, {"mainAxisSize", ^^MainAxisSize},
                   {"textDirection", ^^TextDirection}>;

struct ColumnStyleTrait : TraitBase<"columns">
{
    static void layout(auto &soaCtx, auto &selfSoa, FlatLayoutTree &tree, int nodeIdx,
                       Constraints borderBC, const EdgeInsets &pad)
    {
        auto &ref = tree.nodes[nodeIdx].ref;
        auto [vd, crossAlign, axisSize, mainAlign, w, h] =
            selfSoa.template view_entity<"verticalDirection", "crossAlign",
                                         "mainAxisSize", "mainAlign", "width", "height">(
                0, ref.entity_id);
        AxisDirection dir = axisDirectionForFlex(false, TextDirection::ltr, vd);
        layoutFlexImpl<false>(soaCtx, tree, nodeIdx, borderBC, pad, dir, crossAlign,
                              axisSize, mainAlign, w, h);
    }
    static Entity make(auto &soaCtx, std::optional<float> width = std::nullopt,
                       std::optional<float> height = std::nullopt, EdgeInsets margin = {},
                       EdgeInsets padding = {},
                       MainAxisAlignment mainAlign = MainAxisAlignment::start,
                       CrossAxisAlignment crossAlign = CrossAxisAlignment::start,
                       MainAxisSize mainAxisSize = MainAxisSize::max,
                       VerticalDirection verticalDirection = VerticalDirection::down)
    {
        uint32_t idx =
            soaCtx.columns.new_entity(width, height, margin, padding, mainAlign,
                                      crossAlign, mainAxisSize, verticalDirection);
        return {type_id(soaCtx), idx, release(soaCtx)};
    }
};
using ColumnStyleObject =
    gen_soa_struct<ColumnStyleTrait, {"width", ^^std::optional<float>},
                   {"height", ^^std::optional<float>}, {"margin", ^^EdgeInsets},
                   {"padding", ^^EdgeInsets}, {"mainAlign", ^^MainAxisAlignment},
                   {"crossAlign", ^^CrossAxisAlignment}, {"mainAxisSize", ^^MainAxisSize},
                   {"verticalDirection", ^^VerticalDirection}>;

// [FIX] Expanded 扩展为支持 FlexFit（可表达 Expanded 和 Flexible）
struct ExpandedStyleTrait : TraitBase<"expandeds">
{
    static constexpr bool has_single_child = true; // 强制单子节点

    static Entity make(auto &soaCtx, int flex = 1,
                       FlexFit fit = FlexFit::tight) // [NEW] 增加 flexFit 参数
    {
        uint32_t idx = soaCtx.expandeds.new_entity(flex, fit);
        return {type_id(soaCtx), idx, release(soaCtx)};
    }
};
using ExpandedStyleObject =
    gen_soa_struct<ExpandedStyleTrait, {"flex", ^^int},
                   {"flexFit", ^^FlexFit}>; // [NEW] 添加 flexFit 字段

struct TextStyleTrait : TraitBase<"texts">
{
    static Entity make(auto &soaCtx, std::optional<float> w = std::nullopt,
                       std::optional<float> h = std::nullopt, EdgeInsets m = {},
                       EdgeInsets p = {})
    {
        uint32_t idx = soaCtx.texts.new_entity(w, h, m, p);
        return {type_id(soaCtx), idx, release(soaCtx)};
    }

    static void layout(auto &soaCtx, auto &selfSoa, FlatLayoutTree &tree, int nodeIdx,
                       Constraints borderBC, const EdgeInsets & /*pad*/)
    {
        FlatNode &node = tree.nodes[nodeIdx];
        if (node.firstChild != -1)
            throw std::logic_error("Text widget '" + node.name +
                                   "' cannot have children.");

        auto [width, height] =
            selfSoa.template view_entity<"width", "height">(0, node.ref.entity_id);
        float w = width.value_or(80.0f);
        float h = height.value_or(40.0f);
        Size size = borderBC.clamp({w, h});
        node.geometry = {0, 0, size.width, size.height};
        node.baseline = size.height * 0.8f; // 测试用，实际需真实字体度量
    }
};
using TextStyleObject =
    gen_soa_struct<TextStyleTrait, {"width", ^^std::optional<float>},
                   {"height", ^^std::optional<float>}, {"margin", ^^EdgeInsets},
                   {"padding", ^^EdgeInsets}>;

constexpr void pre_single_child_check(auto &soaCtx, const FlatNode &node)
{
    constexpr auto members = std::remove_cvref_t<decltype(soaCtx)>::members;
    template for (constexpr auto I : std::ranges::views::indices(members.size()))
    {
        using MemberType = std::remove_cvref_t<decltype(soaCtx.[:members[I]:])>;
        if constexpr (requires {
                          typename MemberType::trait_type;
                          MemberType::trait_type::has_single_child;
                      })
        {
            if constexpr (MemberType::trait_type::has_single_child)
            {
                if (node.ref.type_id == I && node.firstChild != -1)
                    throw std::logic_error("this type'" + node.name +
                                           "' can only have one child.");
            }
        }
    }
}
// ====================== 通用声明式 UIBuilder（新增） ======================
template <typename SoaCtx>
class UIBuilder
{
    SoaCtx &soaCtx_;
    FlatLayoutTree &tree_;
    int currentParent_ = -1;
    std::vector<int> parentStack_;

    void pushParent(int idx)
    {
        parentStack_.push_back(currentParent_);
        currentParent_ = idx;
    }
    void popParent()
    {
        currentParent_ = parentStack_.back();
        parentStack_.pop_back();
    }

    int addNode(const std::string &name, Entity entity)
    {
        if (currentParent_ != -1)
            pre_single_child_check(soaCtx_, tree_.getNode(currentParent_));
        return tree_.addNode(name, std::move(entity), currentParent_);
    }

  public:
    UIBuilder(SoaCtx &ctx, FlatLayoutTree &tree) : soaCtx_{ctx}, tree_{tree} {}

    // 获取最后添加的节点索引（辅助）
    int lastNodeIdx() const noexcept
    {
        return static_cast<int>(tree_.nodes.size() - 1);
    }

    // ========== 叶子版本（无子节点） ==========
    // 约束：没有额外参数，或者第一个参数不可调用（即不是 BuildFn）
    template <typename TraitType, typename... Args>
        requires(
            sizeof...(Args) == 0 ||
            !std::invocable<std::decay_t<std::tuple_element_t<0, std::tuple<Args...>>>,
                            UIBuilder &>)
    UIBuilder &Add(const std::string &name, Args &&...args)
    {
        auto entity = TraitType::make(soaCtx_, std::forward<Args>(args)...);
        addNode(name, std::move(entity));
        return *this;
    }

    // ========== 容器版本（有子节点） ==========
    // 约束：第一个额外参数必须可调用（以 UIBuilder& 为参数）
    template <typename TraitType, typename BuildFn, typename... Args>
        requires std::invocable<std::decay_t<BuildFn>, UIBuilder &>
    UIBuilder &Add(const std::string &name, BuildFn &&buildFn, Args &&...args)
    {
        // 先创建节点（使用叶子版本，传入 args...）
        int idx = Add<TraitType>(name, std::forward<Args>(args)...).lastNodeIdx();
        pushParent(idx);
        std::invoke(std::forward<BuildFn>(buildFn), *this);
        popParent();
        return *this;
    }

    // ========== Root 入口（保持不变） ==========
    template <typename TraitType = ContainerStyleTrait, typename BuildFn,
              typename... Args>
    void Root(const std::string &name, BuildFn &&buildFn, Args &&...args)
    {
        currentParent_ = -1;
        int idx = Add<TraitType>(name, std::forward<Args>(args)...).lastNodeIdx();
        pushParent(idx);
        std::invoke(std::forward<BuildFn>(buildFn), *this);
        popParent();
    }
};

//diff: [test_dod17] end

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

constexpr auto run_text_pipeline(auto &fontSelect, const char8_t *rawText,
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
static constexpr auto visit_store(auto &soaCtx, uint32_t type_id, auto &&visitor)
{
    constexpr auto members = std::remove_cvref_t<decltype(soaCtx)>::members;
    template for (constexpr auto I : std::ranges::views::indices(members.size()))
    {
        using MemberType = std::remove_cvref_t<decltype(soaCtx.[:members[I]:])>;
        if constexpr (requires { typename MemberType::trait_type; })
            if (I == type_id)
                return std::forward<decltype(visitor)>(visitor)(soaCtx.[:members[I]:]);
    }
    throw std::out_of_range{"visit_store: bad type_id"};
}

static constexpr auto inputController(auto &world, auto &inputCtx, auto &soaCtx)
{
    auto &currentFrame = world.globalCtx.frameContext.currentFrame;
    constexpr auto members = std::remove_cvref_t<decltype(soaCtx)>::members;
    template for (constexpr auto e : std::ranges::views::indices(members.size()))
    {
        if constexpr (requires {
                          typename std::remove_cvref_t<
                              decltype(soaCtx.[:members[e]:])>::trait_type;
                          std::remove_cvref_t<decltype(soaCtx.[:members[e]:])>::
                              trait_type::template inputController(world, inputCtx,
                                                                   soaCtx, currentFrame);
                      })
        {
            using trait_type =
                std::remove_cvref_t<decltype(soaCtx.[:members[e]:])>::trait_type;
            trait_type::template inputController(world, inputCtx, soaCtx, currentFrame);
        }
    }
}
static constexpr auto update(auto &world, auto &inputCtx, auto &soaCtx,
                             uint32_t currentFrame) noexcept
{
    constexpr auto members = std::remove_cvref_t<decltype(soaCtx)>::members;
    template for (constexpr auto e : std::ranges::views::indices(members.size()))
    {
        if constexpr (requires {
                          typename std::remove_cvref_t<
                              decltype(soaCtx.[:members[e]:])>::trait_type;
                          std::remove_cvref_t<decltype(soaCtx.[:members[e]:])>::
                              trait_type::template update(world, inputCtx, soaCtx,
                                                          currentFrame);
                      })
        {
            using trait_type =
                std::remove_cvref_t<decltype(soaCtx.[:members[e]:])>::trait_type;
            trait_type::template update(world, inputCtx, soaCtx, currentFrame);
        }
    }
}
// NOTE: 数据的分类 和 函数的个数息息相关。需要考虑好了，那些是 一份的数据，那些是多份的数据。 currentFrame 的重要性？顺序和先后？
static constexpr auto model_update(auto &world, auto &inputCtx, auto &soaCtx) noexcept
{
    auto &currentFrame = world.globalCtx.frameContext.currentFrame;
    constexpr auto members = std::remove_cvref_t<decltype(soaCtx)>::members;
    template for (constexpr auto e : std::ranges::views::indices(members.size()))
    {
        if constexpr (requires {
                          typename std::remove_cvref_t<
                              decltype(soaCtx.[:members[e]:])>::trait_type;
                          std::remove_cvref_t<decltype(soaCtx.[:members[e]:])>::
                              trait_type::template model_update(world, inputCtx, soaCtx,
                                                                currentFrame);
                      })
        {
            using trait_type =
                std::remove_cvref_t<decltype(soaCtx.[:members[e]:])>::trait_type;
            trait_type::template model_update(world, inputCtx, soaCtx, currentFrame);
        }
    }
}
constexpr auto initSoaData()
{

    ContainerStyleObject containers{100};
    RowStyleObject rows{100};
    ColumnStyleObject columns{100};
    ExpandedStyleObject expandeds{100};
    TextStyleObject texts{100};

    //diff: [test_dod17] start
    constexpr auto get_member =
        []<static_string member_name>(auto &soaCtx, const Entity &ref) -> RefAny {
        constexpr auto members = std::remove_cvref_t<decltype(soaCtx)>::members;
        template for (constexpr auto I : std::ranges::views::indices(members.size()))
        {
            using MemberType = std::remove_cvref_t<decltype(soaCtx.[:members[I]:])>;
            if constexpr (requires { typename MemberType::trait_type; })
            {
                if constexpr (MemberType::has_member(member_name))
                {
                    if (I == (size_t)ref.type_id)
                    {
                        auto [m] = soaCtx.[:members[I]:]
                            .template view_entity<member_name>(0, ref.entity_id);
                        using Ref = decltype(m);
                        static_assert(std::is_reference_v<Ref>, "should be reference");
                        return RefAny{m};
                    }
                }
            }
        }
        return RefAny{};
    };

    constexpr auto layout = [](auto &soaCtx, FlatLayoutTree &tree, int nodeIdx,
                               Constraints constraints) {
        FlatNode &node = tree.nodes[nodeIdx];
        const auto &ref = node.ref;
        if (ref.type_id == ExpandedStyleTrait::type_id(soaCtx))
            throw std::logic_error(
                "Expanded widget must be placed directly inside Row/Column/Flex.");

        Constraints borderBC = constraints;
        if (auto w = do_get_member<"width">(soaCtx, ref).value_or(std::optional<float>{}))
            borderBC = borderBC.applyFixedWidth(*w);
        if (auto h =
                do_get_member<"height">(soaCtx, ref).value_or(std::optional<float>{}))
            borderBC = borderBC.applyFixedHeight(*h);
        if (auto ownConstraints = do_get_member<"constraints">(soaCtx, ref)
                                      .value_or(std::optional<Constraints>{}))
            borderBC = borderBC.intersect(*ownConstraints);

        EdgeInsets padding =
            do_get_member<"padding">(soaCtx, ref).miss_return(EdgeInsets{});
        bool dispatched = false;
        constexpr auto members = std::remove_cvref_t<decltype(soaCtx)>::members;
        template for (constexpr auto I : std::ranges::views::indices(members.size()))
        {
            using MemberType = std::remove_cvref_t<decltype(soaCtx.[:members[I]:])>;
            if constexpr (requires {
                              typename MemberType::trait_type;
                              MemberType::trait_type::layout(
                                  soaCtx, soaCtx.[:members[I]:], tree, nodeIdx, borderBC,
                                                               padding);
                          })
            {
                if (size_t(ref.type_id) == I)
                {
                    MemberType::trait_type::layout(
                        soaCtx, soaCtx.[:members[I]:], tree, nodeIdx, borderBC, padding);
                    dispatched = true;
                }
            }
        }
        if (!dispatched)
            throw std::logic_error("Unhandled WidgetKind in layout");

        Size finalSize{node.geometry.w, node.geometry.h};
        if (finalSize.width > constraints.maxW + 1e-6f ||
            finalSize.height > constraints.maxH + 1e-6f)
        {
            std::cerr << "WARNING: Widget '" << node.name << "' overflows parent by ("
                      << finalSize.width - constraints.maxW << ", "
                      << finalSize.height - constraints.maxH << ")\n";
        }
    };

    return make_aggregate<"soaData", ContainerStyleObject::trait_type::name,
                          RowStyleObject::trait_type::name,
                          ColumnStyleObject::trait_type::name,
                          ExpandedStyleObject::trait_type::name,
                          TextStyleObject::trait_type::name, "get_member", "layout">(
        std::move(containers), std::move(rows), std::move(columns), std::move(expandeds),
        std::move(texts), std::move(get_member), std::move(layout));
    //diff: [test_dod17] end
}

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
    auto shaderDataRecorder = initShaderDataRecorder(device);
    for (auto &recorder : shaderDataRecorder)
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

    // diff: [test_dod2] start 定义全部数据

    // diff: [test_dod2] end

    //diff: [test_dod8.cpp] start

    auto [containers_, rows_, columns_, expandeds_, texts_, get_member_, layout_] =
        initSoaData();

    //diff: [test_dod17] end

    //diff: [test_dod10] end

    //diff: [test_dod14] 不再需要  topLeftLocal 字段，因为我们使用的公共的顶点我们是知道的
    // soaCtx：布局系统（stores + get_member + layout + uiRects）；事件走 shader_data::hover_manager
    std::vector<shader_data::Rectangle>
        uiRects; // 圆角卡片（照抄 test_sdf Rectangle）；线框另存 uiWireRects
    std::vector<shader_data::UiRect> uiWireRects; // 线框边框
    auto soaCtx =
        make_aggregate_ref<"soaCtx", "containers", "rows", "columns", "expandeds",
                           "texts", "get_member", "layout", "uiRects", "uiWireRects">(
            containers_, rows_, columns_, expandeds_, texts_, get_member_, layout_,
            uiRects, uiWireRects);

    //diff: [test_dod8.cpp] end

    // ===================== UI 布局树：构建 + 布局 + 打印 =====================
    // 画布 = NDC 0..2（y 向下）；桥输出 NDC 中心（画布中心 - 1）
    FlatLayoutTree uiTree;
    uiTree.reserve(16);
    {
        UIBuilder builder(soaCtx, uiTree);
        builder.Root<ContainerStyleTrait>(
            "root",
            [](auto &b) {
                b.template Add<RowStyleTrait>("row", [](auto &b) {
                    b.template Add<ContainerStyleTrait>("left", 0.8f, 1.6f);
                    b.template Add<ContainerStyleTrait>("right", 0.8f, 1.6f);
                });
            },
            2.0f, 2.0f); // root 填满画布
    }
    soaCtx.layout(soaCtx, uiTree, 0, Constraints{0.0f, 2.0f, 0.0f, 2.0f});
    uiTree.printLayout(soaCtx); // 相对 + 绝对坐标

    // 桥：前序遍历 → 圆角卡片（Rectangle）+ 线框（UiRect）
    constexpr glm::vec4 kLevelColors[] = {glm::vec4(1, 0, 0, 1), glm::vec4(0, 1, 0, 1),
                                          glm::vec4(0, 0, 1, 1), glm::vec4(1, 1, 0, 1),
                                          glm::vec4(0, 1, 1, 1), glm::vec4(1, 0, 1, 1)};
    {
        auto collectLayoutRects = [&](const FlatLayoutTree &tree) {
            uiRects.clear();
            uiWireRects.clear();
            // 矩形构建器（照抄 test_sdf makeRect：HTML box-shadow 卡片）
            struct RectStyle
            {
                glm::vec4 fillColor{1.0f, 1.0f, 1.0f, 1.0f};
                glm::vec4 shadowColor{0.0f, 0.0f, 0.0f, 0.0f};
                glm::vec2 shadowOffset{0.0f, 0.04f};
                float radius = 0.08f;
                float edgeSoftness = 0.006f;
                float shadowBlur = 0.08f;
                float shadowSpread = 1.0f;
                float rotation = 0.0f;
                uint32_t effects = 0u;
            };
            auto makeRect = [&](glm::vec2 center, glm::vec2 size, RectStyle s) {
                shader_data::Rectangle r{};
                r.colors[0] = s.fillColor;
                r.colors[1] = s.fillColor;
                r.colors[2] = s.fillColor;
                r.colors[3] = s.fillColor;
                r.model =
                    glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(center, 0.0f)),
                                glm::radians(s.rotation), glm::vec3(0.0f, 0.0f, 1.0f));
                r.vertexTransform.matrix =
                    glm::scale(glm::mat4(1.0f), glm::vec3(size, 1.0f));
                r.uvTransform = UvTransform{glm::vec2(1.0f), glm::vec2(0.0f)};
                r.size = size;
                r.shadowOffset = s.shadowOffset;
                r.radiusSoftness =
                    glm::vec4(s.radius, s.edgeSoftness, s.shadowBlur, s.shadowSpread);
                r.shadowColor = s.shadowColor;
                r.effects = s.effects;
                return r;
            };

            // [1] 底层背景：中灰不透明（让黑色投影可见）
            uiRects.push_back(makeRect({0.0f, 0.0f}, {2.0f, 2.0f},
                                       {.fillColor = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)}));
            // [2] 标准 HTML 卡片（照抄 test_sdf [2]）：白色圆角 + 黑色投影
            uiRects.push_back(makeRect({0.3f, 0.15f}, {0.5f, 0.35f},
                                       {.fillColor = glm::vec4(1.0f),
                                        .shadowColor = glm::vec4(0.0f, 0.0f, 1.0f, 0.45f),
                                        .shadowOffset = {0.0f, 0.05f},
                                        .radius = 0.08f,
                                        .shadowBlur = 0.08f}));
            // 布局线框（所有节点，结构可见）：细白线
            auto walk = [&](this auto &&self, int idx, float ax, float ay,
                            int depth) -> void {
                const auto &n = tree.nodes[idx];
                float x = ax + n.geometry.x, y = ay + n.geometry.y;
                shader_data::UiRect w{};
                w.entity_index = static_cast<uint32_t>(idx);
                w.hover_fn = ~0u;
                w.center_size = {x + n.geometry.w * 0.5f - 1.0f,
                                 y + n.geometry.h * 0.5f - 1.0f, n.geometry.w,
                                 n.geometry.h};
                w.color = {1.0f, 1.0f, 1.0f, 0.6f};
                w.border = 0.003f;
                uiWireRects.push_back(w);
                std::println("[UI] node={} name={} abs=({:.3f},{:.3f}) "
                             "size=({:.3f},{:.3f}) depth={}",
                             idx, n.name, x, y, n.geometry.w, n.geometry.h, depth);
                for (int c = n.firstChild; c != -1; c = tree.nodes[c].nextSibling)
                    self(c, x, y, depth + 1);
            };
            walk(0, 0.0f, 0.0f, 0);
            std::println("[UI] total cards: {} wire: {}", uiRects.size(),
                         uiWireRects.size());
        };
        collectLayoutRects(uiTree);
        // [DBG2] 收集后 C++ 向量里的值（桥是否正确）
        std::println(
            "[DBG2] uiRects[0]: entity={} fill.a={} shadow.a={} size=({:.2f},{:.2f})",
            uiRects[0].entity_index, uiRects[0].colors[0].a, uiRects[0].shadowColor.a,
            uiRects[0].size.x, uiRects[0].size.y);
    }

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

    auto mainShaderCtx = make_aggregate_ref<"mainShaderCtx", "shaderDataRecorder",
                                            "uniformBuffers", "descriptorSets", "glyphs">(
        shaderDataRecorder, uniformBuffers, descriptorSets, simpleGlyphs);
    auto inputCtx =
        make_aggregate_ref<"inputCtx", "input", "camera", "uiCamera", "clock">(
            input, camera, uiCamera, clock);

    // ===== hover：全局唯一一份 实体→函数 关联（外键 = 池实体下标）=====
    shader_data::hover_pool hoverPool{};
    shader_data::hover_manager hoverManager{};

    // 测试绑定：所有字形共享同一个 hover 函数（演示"函数可被共享"）
    uint32_t testHover = hoverPool.bind([](picking_result r, bool enter) noexcept {
        std::println("[HOVER] type={} entity={} primitive={} {} (hover_fn={})",
                     r.key.object_type, r.key.entity_index, r.primitive_id,
                     enter ? "ENTER" : "LEAVE", r.hover_fn);
    });
    for (auto &g : simpleGlyphs)
        g.hover_fn = testHover;

    // [DBG0] 绑定后立即检查：池实体下标 + 字形实际值
    std::println("[DBG0] testHover={} glyph0.hover_fn={} sizeofGlyph={}", testHover,
                 simpleGlyphs[0].hover_fn, sizeof(shader_data::Glyph));

    // record_info
    auto recordCtx = make_aggregate<"recordCtx", "info">(record_info{});
    auto world = make_aggregate_ref<"world", "globalCtx", "mainCtx", "mainShaderCtx",
                                    "pickCtx", "recordCtx", "hoverPool", "hoverManager">(
        globalCtx, mainCtx, mainShaderCtx, pickCtx, recordCtx, hoverPool, hoverManager);
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

        auto &shaderDataRecorder = mainShaderCtx.shaderDataRecorder;
        auto &glyphs = mainShaderCtx.glyphs;

        auto &batch = shaderDataRecorder[currentFrame];

        // ============ 简单 Glyph 测试：写 quad + Glyph 实例 + 间接命令 ============
        static mesh_data &quad_meta = meshMap["quad"];

        batch.write_shader_data(soaCtx.uiRects, quad_meta);
        batch.write_shader_data(soaCtx.uiWireRects, quad_meta);
        batch.write_shader_data(glyphs, quad_meta);
        batch.flush_pending_cmd();
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

            auto &shaderDataRecorder = mainShaderCtx.shaderDataRecorder;
            auto &batch = shaderDataRecorder[currentFrame];

            auto &descriptorSets = mainShaderCtx.descriptorSets;
            auto descriptorSet = descriptorSets[currentFrame];

            auto &uniformBuffers = mainShaderCtx.uniformBuffers;

            // 公共动态状态
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

            // 绑定公共资源：索引缓冲、描述符集、推送常量公共部分（如缓冲地址）
            commandBuffer.bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                             *pipelineLayout, 0, 1, &(descriptorSet), 0,
                                             nullptr);
            commandBuffer.bindIndexBuffer(batch.globalIndexBuffer.buffer.buffer(), 0,
                                          VK_INDEX_TYPE_UINT32);

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

            auto &commandBuffers = globalCtx.commandBuffers;
            auto &pipelineLayout = mainCtx.pipelineLayout;

            const auto &[currentFrame, imageIndex] = recordCtx.info;
            const auto &commandBuffer = commandBuffers[currentFrame];

            auto &shaderDataRecorder = mainShaderCtx.shaderDataRecorder;
            auto &batch = shaderDataRecorder[currentFrame];

            // ============ 简单 Glyph 测试：绑定管线 + 推送常量 + 间接绘制 ============
            // 字形走 UI 透明管线（深度关、混合开，适配 MSDF 半透明）
            commandBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       *mainCtx.pipelineTransparentUI);

            // 推送常量：顶点 / 实例堆 / 命令常量地址 + UI 相机（cameraIndex = 1）
            PushData pc{.vertexAddress = batch.globalVertexBuffer.address,
                        .instanceAddress = batch.globalHeapBuffer.address,
                        .commandConstantsAddress = batch.commandConstantsBuffer.address,
                        .cameraIndex = 1}; // UI 相机
            commandBuffer.pushConstants(*pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                                        sizeof(pc), &pc);

            // 3 条间接绘制命令（圆角卡片、线框、glyph；gl_DrawIDARB 0/1/2）
            commandBuffer.drawIndexedIndirect(batch.indirectDrawBuffer.buffer.buffer(), 0,
                                              3, sizeof(VkDrawIndexedIndirectCommand));
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

        // NOTE: 从 soaCtx 的成员找这个”autoSpinStore“的成员就好了
        inputController(world, inputCtx, soaCtx);
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
                                 model_update(world, inputCtx, soaCtx);
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
                                 update(world, inputCtx, soaCtx, currentFrame);
                                 updateVertexData(world, inputCtx, soaCtx, currentFrame);
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

        // ========== 新增：每 2 秒切换 autoSpinStore 第一个实体的纹理索引 ==========
        {
            // auto &autoSpinStore = soaCtx.autoSpinStore;
            // if (autoSpinStore.size() > 0) // 确保至少有一个实体
            // {
            //     static auto lastTexSwitch = std::chrono::steady_clock::now();
            //     auto now = std::chrono::steady_clock::now();
            //     if (std::chrono::duration_cast<std::chrono::seconds>(now - lastTexSwitch)
            //             .count() >= 2)
            //     {
            //         lastTexSwitch = now;

            //         // 直接通过 view_entity 获取第一个实体的 textureIndex 引用并修改
            //         // 注意：实体 ID 是 0（第一个创建的实体）
            //         auto [texRef] = autoSpinStore.view_entity<"instanceData">(0, 0);
            //         // 在 0 和 3 之间切换（这两个槽位已经绑定了纹理，确保有效）
            //         texRef.textureIndex = (texRef.textureIndex == 0) ? 3 : 0;
            //     }
            // }
        }
        // ======================================================================
        drawFrame(world, inputCtx, soaCtx);
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
