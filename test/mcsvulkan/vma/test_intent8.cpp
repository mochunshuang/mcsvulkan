#include <any>
#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <fstream>
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

#include <memory_resource> // pmr

#include "../head.hpp"
#include "test_intent8_draw_pipeline.hpp"

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

using mcs::vulkan::task::make_task;
using mcs::vulkan::task::init_task;
using mcs::vulkan::task::schedulable_task;

using mcs::vulkan::match;

using mcs::vulkan::fatal_error;

constexpr uint32_t WIDTH = 1280;
constexpr uint32_t HEIGHT = 1040;
// The UI ability scene is a 4x MSAA showcase.  Capping the sample count also
// keeps the MRT pick attachment (128 bit/texel) inside the device-local heap
// at 4K/fullscreen extents.
constexpr VkSampleCountFlagBits SAMPLE_COUNT = VK_SAMPLE_COUNT_4_BIT;
constexpr auto TITLE = "test_my_triangle";

static constexpr auto MAX_FRAMES_IN_FLIGHT = 2;
static constexpr VkDeviceSize TRANSFER_SCRATCH_SIZE = 256;
// Readback target for the verification capture (1920x1080 RGBA upper bound).
static constexpr VkDeviceSize CAPTURE_BYTES = 1920ULL * 1080ULL * 4ULL;

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
    using Clock = std::chrono::steady_clock;    //  ?
    Clock::time_point startTime = Clock::now(); //  ?
    Clock::time_point lastTime = startTime;
    float deltaTime = 0.016f;

    //  ?startTime  loat ?
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

    template <typename Frame, std::same_as<image_info>... T>
    static void transition_image_layout(Frame &frame, T... info) noexcept
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
        frame.barrier(VkDependencyInfo{
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
    glm::vec2 texCoord; // diff: [texture]
};
struct VertexAttribute
{
    glm::vec3 color; //  ?
};

// diff: [test_dod14] start:  ?D I ?
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

    //  ?
    constexpr static UvTransform identity() noexcept
    {
        return {};
    }

    //  ?
    static UvTransform from_target_verts(std::span<const glm::vec2> target)
    {
        //  ?
        float l = std::min({target[0].x, target[1].x, target[2].x, target[3].x});
        float r = std::max({target[0].x, target[1].x, target[2].x, target[3].x});
        float b = std::min({target[0].y, target[1].y, target[2].y, target[3].y});
        float t = std::max({target[0].y, target[1].y, target[2].y, target[3].y});
        return {.scale = {r - l, t - b}, .offset = {l, b}};
    }

    //  ?origin  [0,1] ?
    static UvTransform from_verts(std::span<const glm::vec2> origin,
                                  std::span<const glm::vec2> target)
    {
        //  ?from_target_verts   origin
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
    //    ?uint
    //   Bits <=  8  ?uint8_t
    //   Bits <= 16  ?uint16_t
    //   Bits <= 32  ?uint32_t
    template <std::size_t B>
    using best_uint_t =
        std::conditional_t<(B <= 8), std::uint8_t,
                           std::conditional_t<(B <= 16), std::uint16_t, std::uint32_t>>;

    using underlying_t = best_uint_t<Bits>;

    static constexpr std::size_t bit_width = Bits;

    //     1
    //    ?lambda + if constexpr   `1u << 32`  ?UB
    static constexpr underlying_t mask = [] {
        if constexpr (Bits >= 32)
            return static_cast<underlying_t>(~std::uint32_t{0});
        else
            return static_cast<underlying_t>((std::uint32_t{1} << Bits) - 1u);
    }();

    //    ?
    constexpr render_version() noexcept = default;

    //  ?mask
    constexpr explicit render_version(std::uint32_t v) noexcept
        : value_{static_cast<underlying_t>(v & mask)}
    {
    }

    //     mask   0 ?
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

    //    ?uint
    [[nodiscard]] constexpr operator underlying_t() const noexcept
    {
        return value_;
    }

    //  ?uint32 ?
    [[nodiscard]] constexpr std::uint32_t to_u32() const noexcept
    {
        return static_cast<std::uint32_t>(value_);
    }

    [[nodiscard]] constexpr bool operator==(const render_version &) const noexcept =
        default;

  private:
    underlying_t value_ = 0;
};
using render_version_t = render_version<16>; // 16
// picking_result  R32G32B32A32_UINT   + frag  ?uvec4  ?6B ?
// xy = object_key(type_id, entity_index)   = primitive_id  = hover_fn ?xFFFFFFFF =
struct picking_result
{
    //    ?version_t  ?bit_width  ?primitive
    using version_t = render_version_t; //   render_version<16>
    static constexpr std::size_t version_bits = version_t::bit_width;
    static constexpr std::size_t total_bits = 32;
    static constexpr std::size_t primitive_bits = total_bits - version_bits;

    static_assert(primitive_bits >= 1 && primitive_bits < total_bits,
                  "picking_result: version_t leaves no room for primitive_id");

    //   primitive_id  ?
    using primitive_t = version_t::template best_uint_t<primitive_bits>;

    //
    static constexpr std::uint32_t primitive_mask =
        (std::uint32_t{1} << primitive_bits) - 1u;

    //
    object_key key; // outPicking.xy
    std::uint32_t
        packed; // outPicking.z  version_bits=  primitive_bits=primitive_id
    std::uint32_t hover_fn; // outPicking.w

    //
    [[nodiscard]] constexpr primitive_t primitive_id() const noexcept
    {
        return static_cast<primitive_t>(packed & primitive_mask);
    }
    [[nodiscard]] constexpr version_t::underlying_t render_version() const noexcept
    {
        return version_t{packed >> version_bits}; //   mask  ?
    }

    //    hader  ?pack
    static constexpr std::uint32_t pack(version_t v, primitive_t p) noexcept
    {
        return (v.to_u32() << version_bits) |
               (static_cast<std::uint32_t>(p) & primitive_mask);
    }
};
static_assert(sizeof(picking_result) == 16);
//diff: [test_dod16] end
// test_intent5   test_intent4  ype 6 = Cell ?
#include "test_intent8_frame_context.hpp"

// 8  /7  ?DAO   dao  ?shader
#include "test_intent8_ui_dao.hpp"

#include <nlohmann/json.hpp>
constexpr auto inputInit(float aspect)
{
    auto camera = [&]() {
        using namespace camera; //   camera
        //   eye/center/up   ViewMatrixObject
        glm::vec3 eye(0.0f, 0.0f, 2.0f);
        glm::vec3 center(0.0f, 0.0f, 0.0f);
        glm::vec3 up(0.0f, 1.0f, 0.0f);
        glm::vec3 forward = glm::normalize(center - eye);
        RightHandedView view;
        view.setPosition(eye).setOrientation(glm::quatLookAt(forward, up)); //  ?

        //   fovy  ?
        float fovDeg = glm::degrees(glm::radians(45.0f)); //   45.0f
        VulkanPerspectiveProjection proj(fovDeg, aspect, 0.1f, 10.0f);
        return GenCamera(RightHandedView::lookAt(glm::vec3(0, 0, 2), glm::vec3(0, 0, 0),
                                                 glm::vec3(0, 1, 0)),
                         std::move(proj));
    }();
    //  ?camera  ?uiCamera.
    auto uiCamera = []() {
        using namespace camera;
        //  ? ?  = I
        auto uiView = camera::RightHandedView{
            glm::vec3(0.0f, 0.0f, 0.0f), //  ?
            glm::identity<glm::quat>()   //  ?
        };
        //  ?[-1,1]  ?near=0, far=1  ?  = I //NOTE: -10.0f, 10.0f  ?Y X  ?
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
    struct ScreenWidget; //NOTE:  ?
}
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
            assert(entity < hover_fns.size());
            hover_fns[entity](r, enter, inputCtx, screen);
        }
    };

    //  ?cur ?cur   pre  leave ?
    struct hover_manager
    {
        static constexpr auto hover_leave = false;
        static constexpr auto hover_enter = true;
        ui_new::ScreenWidget *screen = nullptr;
        picking_result cur{object_key{0xFFFFFFFF, 0}, 0,
                           0}; //  ey   =

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

//diff: [test_dod24.cpp] start [main
//diff: [test_dod24.cpp] end

constexpr auto initDrawRecorder(const LogicalDevice &device)
{
    constexpr auto vertexCapacity = sizeof(Vertex) * 1000;
    constexpr auto indexCapacity = sizeof(uint32_t) * 1000;
    constexpr auto heapCapacity = sizeof(shader_data::Shape) * 2000;
    // NOTE:  100
    constexpr auto indirectDrawCapacity = sizeof(VkDrawIndexedIndirectCommand) * 100;
    constexpr auto commandConstantsCapacity = sizeof(shader_data::CommandConstant) * 100;

    std::array<shader_data::FrameContext, MAX_FRAMES_IN_FLIGHT> drawRecorder{
        shader_data::FrameContext(device, vertexCapacity, indexCapacity, heapCapacity,
                                  indirectDrawCapacity, commandConstantsCapacity),
        shader_data::FrameContext(device, vertexCapacity, indexCapacity, heapCapacity,
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
        //
        uint8_t color1[4] = {255, 0, 0, 255}; //
        uint8_t color2[4] = {0, 0, 255, 255}; //
        generateCheckerboardTexture(pixels.get(), texWidth_, texHeight_, color1, color2);
    }
    else if (textureType == 2)
    {
        //  ?
        uint8_t topColor[4] = {0, 255, 0, 255};      //
        uint8_t bottomColor[4] = {255, 255, 0, 255}; //
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

//  ?UI  Container/Row/Column/Expanded/Text Trait, UIBuilder, FlatLayoutTree

constexpr auto init()
{
    //NOTE: make_unique
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
                      colorWriteMask  blendEnable   VK_FALSE  independentBlend  ?
                      colorWriteMask  ?A ?R|G ?
                    */
                     //diff: [test_indirectdraw_no_pick] start
                     .independentBlend = VK_TRUE, //
                     //diff: [test_indirectdraw_no_pick] end
                     // SPIR-V  ?gl_PrimitiveID ragment  ?Geometry
                     // capability ulkan   geometryShader  ?
                     .geometryShader = VK_TRUE,
                     //NOTE: gcc  ?multiDrawIndirect
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
                       features2.features.geometryShader &&
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
    auto secondaryCommandBuffers =
        std::make_unique<CommandBuffers>(commandPool->allocateCommandBuffers(
            {.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY,
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
                 // TRANSFER_SRC is used only by the verification readback path.
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
    const std::uint32_t graphicsQueueFamilyIndex = GRAPHICS_QUEUE_FAMILY_IDX;
    // hardware
    return make_aggregate<"hardwareCtx", "vulkan", "window", "instance", "debuger",
                          "physicalDevice", "surface", "device", "queue", "commandPool",
                          "commandBuffers", "secondaryCommandBuffers",
                          "swapchainBuild", "swapchain", "frameContext",
                          "graphicsQueueFamilyIndex">(
        std::move(ctx), std::move(window), std::move(instance), std::move(debuger),
        std::move(physical), std::move(surface), std::move(device),
        std::move(GRAPHICS_AND_PRESENT), std::move(commandPool),
        std::move(commandBuffers), std::move(secondaryCommandBuffers),
        std::move(swapchainBuild), std::move(swapchain),
        std::move(frameContext), graphicsQueueFamilyIndex);
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

    constexpr int MAX_TEXTURES = 64; //  ?
    constexpr int SAMPLER_COUNT = 4; //  2 ?

    std::array<VkDescriptorBindingFlags, 4> bindingFlags = {
        //  0 niform Buffer -
        0,
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT, //  1 ? ? ?3 ?
        0,
        0, //   binding 3 torage Buffer ?
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

    // diff: [test_dod7] start:  ?vma  ?
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
                                     .mipLevels = 1})); //
        upload_keys.emplace_back("white");

        upload.push_back(generateGradientTexture(device, commandPool,
                                                 GRAPHICS_AND_PRESENT, 2)); //
        upload_keys.emplace_back("gradient");
        upload.push_back(generateGradientTexture(device, commandPool,
                                                 GRAPHICS_AND_PRESENT, 1)); //
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
                .build(device)); //
        upload_keys.emplace_back("linear_sampler");
        samplers.emplace_back(create_sampler{}
                                  .setCreateInfo(create_sampler::templateNearest())
                                  .build(device)); //  ?
        upload_keys.emplace_back("nearest_neighbor_sampler");

        //  ? ?
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
                     .anisotropyEnable = VK_TRUE, // NOTE:  ?
                     .maxAnisotropy =
                         physical_device.getProperties().limits.maxSamplerAnisotropy,
                     .compareEnable = VK_FALSE,
                     .compareOp = VK_COMPARE_OP_ALWAYS,
                     .minLod = 0,
                     .maxLod =
                         VK_LOD_CLAMP_NONE, // NOTE: vulkan  ip
                     .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
                     .unnormalizedCoordinates = VK_FALSE})
                .build(device));
        upload_keys.emplace_back(font_linear_sampler_key);

        //  ?
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
                                .anisotropyEnable = VK_FALSE, // NOTE:
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
                            &imageInfos[slot], //  mageInfos  ?
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
                        // NOTE:  ?shader ?
                        .descriptorCount = 1,
                        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                        .pImageInfo =
                            &samplerInfos[slot], //  mageInfos  ?
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

    //diff: [test_dod12] start:
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

    //
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
                            VK_FORMAT_R8G8B8A8_UNORM, //diff: [test_dod14] msdf ?
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

// NOTE:
constexpr std::array<Vertex, 4> quadVerts = {
    Vertex{{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f}}, //
    Vertex{{0.5f, -0.5f, 0.0f}, {1.0f, 0.0f}},  //
    Vertex{{0.5f, 0.5f, 0.0f}, {1.0f, 1.0f}},   //
    Vertex{{-0.5f, 0.5f, 0.0f}, {0.0f, 1.0f}}   //
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
auto initPipeline(auto &hardwareCtx, auto &descriptorCtx)
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
         .samples = SAMPLE_COUNT,
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
         .samples = SAMPLE_COUNT,
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

    // ==========   =========
    auto pickResourcesBuild = mcs::vulkan::memory::build_simple_resource(
        {.imageType = VK_IMAGE_TYPE_2D,
         .format = VK_FORMAT_R32G32B32A32_UINT, // 128  + primitive_id + hover_fn
         .extent = {.width = WIDTH, .height = HEIGHT, .depth = 1},
         .mipLevels = 1,
         .arrayLayers = 1,
         .samples = SAMPLE_COUNT, //NOTE:  ?
         .tiling = VK_IMAGE_TILING_OPTIMAL,
         .usage =
             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT //diff: [test_indirectdraw_no_pick]  ?pickingImage   TRANSFER_SRC_BIT
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
         .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |   //   resolve
                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT |   //
                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT //  ? ?
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

    // Offscreen layer resources: MSAA color + sampled resolve texture.
    auto offscreenMsaaBuild = mcs::vulkan::memory::build_simple_resource(
        {.imageType = VK_IMAGE_TYPE_2D,
         .format = swapchainBuild.refImageFormat(),
         .extent = {.width = swapchain.refImageExtent().width,
                    .height = swapchain.refImageExtent().height,
                    .depth = 1},
         .mipLevels = 1,
         .arrayLayers = 1,
         .samples = SAMPLE_COUNT,
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
    auto offscreenMsaaResource = offscreenMsaaBuild.build(device);

    auto offscreenResolveBuild = mcs::vulkan::memory::build_simple_resource(
        {.imageType = VK_IMAGE_TYPE_2D,
         .format = swapchainBuild.refImageFormat(),
         .extent = {.width = swapchain.refImageExtent().width,
                    .height = swapchain.refImageExtent().height,
                    .depth = 1},
         .mipLevels = 1,
         .arrayLayers = 1,
         .samples = VK_SAMPLE_COUNT_1_BIT,
         .tiling = VK_IMAGE_TILING_OPTIMAL,
         .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                  VK_IMAGE_USAGE_SAMPLED_BIT |
                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
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
    auto offscreenResolveResource = offscreenResolveBuild.build(device);
    const VkImage offscreenResolveImage = offscreenResolveResource.image();
    const VkImageView offscreenResolveView = offscreenResolveResource.imageView();
    std::uint32_t offscreenTextureIndex = 0;
    {
        auto &textureManager = *descriptorCtx.textureManager.get();
        auto freeSlots = textureManager.view_free_indexes();
        auto it = freeSlots.begin();
        assert(it != freeSlots.end());
        offscreenTextureIndex = *it;
        textureManager.use_slot(offscreenTextureIndex,
                                std::move(offscreenResolveResource),
                                "offscreen_layer");
    }

    // Dedicated stencil attachment for stencil/path clipping.
    constexpr VkFormat stencilFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
    auto stencilBuild = mcs::vulkan::memory::build_simple_resource(
        {.imageType = VK_IMAGE_TYPE_2D,
         .format = stencilFormat,
         .extent = {.width = swapchain.refImageExtent().width,
                    .height = swapchain.refImageExtent().height,
                    .depth = 1},
         .mipLevels = 1,
         .arrayLayers = 1,
         .samples = SAMPLE_COUNT,
         .tiling = VK_IMAGE_TILING_OPTIMAL,
         .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
         .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED},
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        [](VkImageCreateInfo imageCreateInfo, VkImage image) noexcept
            -> mcs::vulkan::memory::create_image::view_create_info {
            return {.image = image,
                    .viewType = VK_IMAGE_VIEW_TYPE_2D,
                    .format = imageCreateInfo.format,
                    .subresourceRange = {
                        .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT |
                                      VK_IMAGE_ASPECT_STENCIL_BIT,
                        .baseMipLevel = 0,
                                         .levelCount = 1,
                                         .baseArrayLayer = 0,
                                         .layerCount = 1}};
        });
    auto stencilResource = stencilBuild.build(device);
    const VkImageView stencilView = stencilResource.imageView();
    const VkImage stencilImage = stencilResource.image();

    // Derive the pass target metadata from the actual attachment resources,
    // not from the pipeline create-info constants.
    intent8::RenderTarget passTargetTemplate{};
    passTargetTemplate.config.colorCount = 2;
    passTargetTemplate.config.colorFormats[0] =
        msaaResourcesBuild.refCreateInfo().format;
    passTargetTemplate.config.colorFormats[1] =
        pickResourcesBuild.refCreateInfo().format;
    passTargetTemplate.config.depthFormat =
        depthResourcesBuild.refCreateInfo().format;
    passTargetTemplate.config.stencilFormat = VK_FORMAT_UNDEFINED;
    passTargetTemplate.config.samples =
        msaaResourcesBuild.refCreateInfo().samples;
    passTargetTemplate.config.viewMask = 0;
    std::array<mcs::vulkan::memory::auto_map_buffer, MAX_FRAMES_IN_FLIGHT> pickingFrames;
    for (auto &pf : pickingFrames)
    {
        constexpr auto BUFFER_SIZE = sizeof(picking_result); // 16B =  ?4 u32 texel
        pf = mcs::vulkan::memory::auto_map_buffer(
            mcs::vulkan::memory::create_simple_buffer(
                device,
                {.size = BUFFER_SIZE,
                 .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
            BUFFER_SIZE);
        //  ?GPU  ?0xFF..FF ?
        //   render_version = 0xFFFF  0  ?
        //  ?
        std::memset(pf.mapPtr(), 0xFF, BUFFER_SIZE);
    }

    // Dedicated transfer scratch: lets the frame exercise FillBuffer +
    // CopyBuffer with real resources and no effect on rendering.
    std::array<std::array<mcs::vulkan::memory::auto_map_buffer, 2>,
               MAX_FRAMES_IN_FLIGHT>
        transferScratch;
    for (auto &framePair : transferScratch)
    {
        for (auto &buffer : framePair)
        {
            buffer = mcs::vulkan::memory::auto_map_buffer(
                mcs::vulkan::memory::create_simple_buffer(
                    device,
                    {.size = TRANSFER_SCRATCH_SIZE,
                     .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                              VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
                TRANSFER_SCRATCH_SIZE);
            std::memset(buffer.mapPtr(), 0, TRANSFER_SCRATCH_SIZE);
        }
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
                 // diff: [test_dod8]  ?
                 .pushConstantRanges = {{.stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                                                       VK_SHADER_STAGE_FRAGMENT_BIT,
                                         .offset = 0,
                                         .size = sizeof(PushData)}}})
            .build(device);

    using stage_info = create_graphics_pipeline::stage_info;
    //
    std::array<VkFormat, 2> mainColorFormats = {
        swapchainBuild.refImageFormat(), // location 0 (swapchain)
        VK_FORMAT_R32G32B32A32_UINT      // location 1 (picking)
    };

    intent8::PipelineRenderingConfig pipelineRenderingInfo{};
    pipelineRenderingInfo.colorCount =
        static_cast<std::uint32_t>(mainColorFormats.size());
    for (std::size_t i = 0; i < mainColorFormats.size(); ++i)
        pipelineRenderingInfo.colorFormats[i] = mainColorFormats[i];
    pipelineRenderingInfo.depthFormat = depthFormat_ref;
    pipelineRenderingInfo.samples = SAMPLE_COUNT;

    intent8::DynamicStateConfig stateOwnership{};
    stateOwnership.add(VK_DYNAMIC_STATE_VIEWPORT);
    stateOwnership.add(VK_DYNAMIC_STATE_SCISSOR);

    intent8::FeatureSet features{};

    /*
     ?                 	     	 (att0)	         ?
    pipelineOpaque3D	    ON (LESS)	    ON	        OFF	             3D
    pipelineTransparent3D	ON (LESS)	    OFF	        ON	             3D
    pipelineOpaqueUI	    OFF	            OFF	        OFF	             UI
    pipelineTransparentUI	OFF	            OFF	        ON	             UI
    */
    auto makePipeline = [&](const VkBool32 depthTest, const VkBool32 depthWrite,
                            VkBool32 blendEnable,
                            VkBlendFactor srcColor = VK_BLEND_FACTOR_ONE,
                            VkBlendFactor dstColor =
                                VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                            VkBlendFactor srcAlpha = VK_BLEND_FACTOR_ONE,
                            VkBlendFactor dstAlpha =
                                VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA) {
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
                             SAMPLE_COUNT,
                         .sampleShadingEnable = VK_FALSE,
                         .minSampleShading = 1.0F,
                         .sampleMask = {},
                         .alphaToCoverageEnable = VK_FALSE,
                         .alphaToOneEnable = VK_FALSE,
                     },
                 //  / ?  ?
                 .depthStencilState = {.depthTestEnable = depthTest,
                                       .depthWriteEnable = depthWrite,
                                       .depthCompareOp = VK_COMPARE_OP_LESS,
                                       .depthBoundsTestEnable = VK_FALSE,
                                       .stencilTestEnable = VK_FALSE,
                                       .front = {},
                                       .back = {},
                                       .minDepthBounds = 0.0F,
                                       .maxDepthBounds = 1.0F},
                 //  :  ?
                 .colorBlendState =
                     {.logicOpEnable = VK_FALSE,
                      .logicOp = VkLogicOp::VK_LOGIC_OP_COPY,
                      .attachments =
                          {
                              // 0
                              {
                                  //diff: [test_dod12] start:   0  ?  MSDF  ?
                                  .blendEnable = blendEnable,
                                  .srcColorBlendFactor = srcColor,
                                  .dstColorBlendFactor = dstColor,
                                  .colorBlendOp = VK_BLEND_OP_ADD,
                                  .srcAlphaBlendFactor = srcAlpha,
                                  .dstAlphaBlendFactor = dstAlpha,
                                  .alphaBlendOp = VK_BLEND_OP_ADD,
                                  .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                                    VK_COLOR_COMPONENT_G_BIT |
                                                    VK_COLOR_COMPONENT_B_BIT |
                                                    VK_COLOR_COMPONENT_A_BIT,
                                  //diff: [test_dod12] end
                              },
                              //diff: [test_indirectdraw_no_pick] start
                              {
                                  // location 1 ( ?  w = primitive_id + hover_fn)
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
                                          //NOTE:
                                          //   VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
                                          //   VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
                                      }},
                 .layout = *pipelineLayout})
            .build(device);
    };
    //  4 ?
    auto makeStencilPipeline = [&](bool mask) {
        const VkStencilOpState stencilOp{
            .failOp = VK_STENCIL_OP_KEEP,
            .passOp = mask ? VK_STENCIL_OP_REPLACE : VK_STENCIL_OP_KEEP,
            .depthFailOp = VK_STENCIL_OP_KEEP,
            .compareOp = mask ? VK_COMPARE_OP_ALWAYS : VK_COMPARE_OP_EQUAL,
            .compareMask = 0xFFU,
            .writeMask = static_cast<std::uint32_t>(mask ? 0xFF : 0),
            .reference = 1U};
        return create_graphics_pipeline{}
            .setCreateInfo(
                {.pNext = make_pNext(structure_chain<VkPipelineRenderingCreateInfo>{
                     {.colorAttachmentCount = 2,
                      .pColorAttachmentFormats = mainColorFormats.data(),
                      .depthAttachmentFormat = stencilFormat,
                      .stencilAttachmentFormat = stencilFormat}}),
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
                     {.rasterizationSamples =
                          SAMPLE_COUNT,
                      .sampleShadingEnable = VK_FALSE,
                      .minSampleShading = 1.0F,
                      .sampleMask = {},
                      .alphaToCoverageEnable = mask ? VK_TRUE : VK_FALSE,
                      .alphaToOneEnable = VK_FALSE},
                 .depthStencilState = {.depthTestEnable = VK_FALSE,
                                       .depthWriteEnable = VK_FALSE,
                                       .depthCompareOp = VK_COMPARE_OP_ALWAYS,
                                       .depthBoundsTestEnable = VK_FALSE,
                                       .stencilTestEnable = VK_TRUE,
                                       .front = stencilOp,
                                       .back = stencilOp,
                                       .minDepthBounds = 0.0F,
                                       .maxDepthBounds = 1.0F},
                 .colorBlendState =
                     {.logicOpEnable = VK_FALSE,
                      .logicOp = VK_LOGIC_OP_COPY,
                      .attachments = {{.blendEnable = VK_FALSE,
                                       .colorWriteMask =
                                           mask ? 0U
                                                : (VK_COLOR_COMPONENT_R_BIT |
                                                   VK_COLOR_COMPONENT_G_BIT |
                                                   VK_COLOR_COMPONENT_B_BIT |
                                                   VK_COLOR_COMPONENT_A_BIT)},
                                      {.blendEnable = VK_FALSE,
                                       .colorWriteMask = 0U}}},
                 .dynamicState = {.dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                                                    VK_DYNAMIC_STATE_SCISSOR}},
                 .layout = *pipelineLayout})
            .build(device);
    };
    auto pipelineStencilMask = makeStencilPipeline(true);
    auto pipelineStencilContent = makeStencilPipeline(false);

    auto pipelineOpaque3D = makePipeline(VK_TRUE, VK_TRUE, VK_FALSE);
    auto pipelineTransparent3D = makePipeline(VK_TRUE, VK_FALSE, VK_TRUE);
    auto pipelineOpaqueUI = makePipeline(VK_FALSE, VK_FALSE, VK_FALSE);
    auto pipelineTransparentUI = makePipeline(VK_FALSE, VK_FALSE, VK_TRUE);
    auto pipelineAdditiveUI =
        makePipeline(VK_FALSE, VK_FALSE, VK_TRUE, VK_BLEND_FACTOR_ONE,
                     VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
                     VK_BLEND_FACTOR_ONE);
    auto pipelineMultiplyUI =
        makePipeline(VK_FALSE, VK_FALSE, VK_TRUE, VK_BLEND_FACTOR_DST_COLOR,
                     VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_DST_ALPHA,
                     VK_BLEND_FACTOR_ZERO);

    return make_aggregate<"mainPipelineCtx", "pipelineLayout", "pipelineOpaque3D",
                          "pipelineTransparent3D", "pipelineOpaqueUI",
                          "pipelineTransparentUI", "pipelineAdditiveUI",
                          "pipelineMultiplyUI", "pipelineStencilMask",
                          "pipelineStencilContent", "swapchainAttachments",
                          "pickingAttachments", "pipelineRenderingInfo",
                          "stateOwnership", "features", "passTargetTemplate",
                          "transferScratch", "offscreenMsaaBuild",
                          "offscreenResolveBuild", "stencilBuild",
                          "offscreenMsaaResource", "offscreenResolveView",
                          "offscreenResolveImage", "offscreenTextureIndex",
                          "stencilResource", "stencilView", "stencilImage">(
        std::move(pipelineLayout), std::move(pipelineOpaque3D),
        std::move(pipelineTransparent3D), std::move(pipelineOpaqueUI),
        std::move(pipelineTransparentUI), std::move(pipelineAdditiveUI),
        std::move(pipelineMultiplyUI), std::move(pipelineStencilMask),
        std::move(pipelineStencilContent), std::move(swapchainAttachments),
        std::move(pickingAttachments), pipelineRenderingInfo, stateOwnership,
        features, passTargetTemplate, std::move(transferScratch),
        std::move(offscreenMsaaBuild), std::move(offscreenResolveBuild),
        std::move(stencilBuild), std::move(offscreenMsaaResource),
        offscreenResolveView, offscreenResolveImage, offscreenTextureIndex,
        std::move(stencilResource), stencilView, stencilImage);
}

//diff: [test_dod22] start
constexpr auto &hoverPool() noexcept
{
    static shader_data::hover_pool hoverPool{};
    return hoverPool;
}
namespace intent8 { class PassManager; struct RenderTarget; }
struct render_context
{
    //   GPU
    picking_result last_pick{};

    shader_data::CommandBuilder &drawRecorder;
    glfw_input &input;
    FrameClock &clock;
    FontSelect &fontSelect;
    intent8::PassManager *passManager{};
    // app main render target (config + attachment resources)
    const intent8::RenderTarget *mainPassDesc{};
    // Non-owning view of pipeline configs, descriptor sets and resources.
    intent8::DrawContext *draw{};
    // Live GLFW framebuffer size: the UI uses this for NDC layout even before
    // the swapchain/attachment resources finish rebuilding on a resize.
    VkExtent2D framebufferExtent{};
    VkDescriptorSet mainSet{};
    std::span<const Vertex> globalVertices{};
    std::span<const uint32_t> globalIndices{};
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

        //   API
        static constexpr auto tight(Size size) noexcept
        { return BoxConstraints{size.width,size.width,size.height,size.height}; }
        struct Optional {std::optional<double> width,height;};
        // null  ? ; ?null  ?->  ?
        static constexpr auto tightFor(Optional op) noexcept
        {
            auto [width,height] = op;
            return BoxConstraints{width.value_or(0.0),width.value_or(inf),height.value_or(0.0),height.value_or(inf)};
        }
        struct Finite {double width = inf, height = inf;};
        // infinity  ? ;  ? ? ?
        static constexpr auto tightForFinite(Finite op) noexcept
        {
            auto [width,height] = op;
            return BoxConstraints{width != inf ? width : 0.0,width != inf ? width : inf,height != inf ? height : 0.0,height != inf ? height : inf};
        }

        static constexpr auto loose(Size size) noexcept
        { return BoxConstraints{0,size.width,0,size.height}; }
        // expand  ?  ?
        static constexpr auto expand(Optional op = {}) noexcept
        {
            auto [width,height] = op;
            return BoxConstraints{width.value_or(inf),width.value_or(inf),height.value_or(inf),height.value_or(inf)};
        }

        //  API
        struct OptionalConstraints {std::optional<double> minWidth,maxWidth,minHeight,maxHeight;};
        //  ?
        constexpr auto copyWith(OptionalConstraints c) const noexcept
        {
            auto [minWidth,maxWidth,minHeight,maxHeight] =c;
            return BoxConstraints{minWidth.value_or(minWidth_),maxWidth.value_or(maxWidth_),minHeight.value_or(minHeight_),maxHeight.value_or(maxHeight_)};
        }
        //  ?
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


    //  ?Flutter  , h x, y
    struct ScreenWidget;
    struct Widget;
    struct RenderObject
    {
    virtual constexpr void render(ScreenWidget*screen,Widget* owner,render_context &context) = 0;
        virtual constexpr void tick(double) {}
        virtual ~RenderObject() = default;
    };

    //  Constraints ?
    //  ize ?
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


        //
        template <typename Fn>
        constexpr void visit(Fn &&fn)
        {
            fn(*this);
            for (auto &child : children())
                if (child) child->visit(std::forward<Fn>(fn));
        }

        //   nullptr
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

        //  ?key
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

        constexpr void tick(double now) noexcept
        {
            if (auto *p = root_.get())
                p->visit([now](Widget &w) {
                    if (w.renderObject)
                        w.renderObject->tick(now);
                });
        }

        constexpr void layout()
        {
            if(auto *ptr = root_.get())
            {
                ptr->layout(BoxConstraints{0,size_.width,0,size_.height});
                ptr->updateOffset({0,0});
            }
        }
        [[nodiscard]] constexpr Size size() const noexcept { return size_; }
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

                //  " "
                if (node->renderObject)
                    node->renderObject->render(screen, node, context);

                //  ?renderObject ?
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
                render(context); //NOTE:
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

        // root   const  ?
        [[nodiscard]] constexpr Widget *root() noexcept { return root_.get(); }
        //  ?
        template <typename Fn>
        constexpr void visit(Fn &&fn) { if (root_) root_->visit(std::forward<Fn>(fn)); }

        template <typename Pred>
        constexpr Widget *findIf(Pred &&pred)
        { return root_ ? root_->findIf(std::forward<Pred>(pred)) : nullptr; }

        [[nodiscard]] constexpr Widget *findByKey(std::string_view k)
        { return root_ ? root_->findByKey(k) : nullptr; }

    private:
        std::array<bool, MAX_FRAMES_IN_FLIGHT> layoutDirty_;   //  ?false ?true
        std::array<bool, MAX_FRAMES_IN_FLIGHT> renderDirty_;
        Size size_;
        std::unique_ptr<Widget> root_;
        render_version_t renderVersion_{};   //   0
        bool versionDirty_ = false;
        constexpr void makeVersionDirty() noexcept { versionDirty_ = true; }
        constexpr void clearVersionDirty() noexcept { versionDirty_ = false; }
        constexpr void toNextRenderVersion() noexcept { renderVersion_ = render_version_t{renderVersion()};clearVersionDirty(); }
    };

    // clang-format on
    // NOLINTEND

    /*
     ?Flutter  ?

     ?Flutter

     onstraints ize ?



    1.  ?
     ?0  ?w  ? ?

     ?

     ?
     ?200  ?
      size = 200 ?
      200  200  ?
     ontainer  ?Container(width: 200) ?200  ?

    2.  ?
      100 ?

     ?

     ?100  ?
      200 ?100 ?size = 100 ?
      SizedBox(width: 100)  Container(width: 200)  100 ?

    3.  ?


     ?0- ?

     ?child  ?size ?

     ?size  size ?

     ?


     onstraints ize ?

    ===============================================================================================
     ?
     ?flex ?

     Container ?

     Row

     ?RenderFlex

      Expanded  ayout  ?
     ?Expanded  ?flex   layout  ?


    ===============================================================================================
    //  ?
    child.layout(constraints);
    size = child.size;  //  ?  =  ?

    ----------------------------------------------------------------------
    //
    double totalWidth = 0;
    double maxHeight = 0;

    //  ?
    for (child in children) {
    child.layout(constraints, parentUsesSize: true);
    totalWidth += child.size.width;    //
    maxHeight = max(maxHeight, child.size.height); //  ?
    }

    //  ?  =  ? ? ? ?
    size = Size(totalWidth, maxHeight);

    ----------------------------------------------------------------------
    //  enderPadding.performLayout ( ?
    @override
    void performLayout() {
    // 1.  ?margin  child
    final BoxConstraints innerConstraints = constraints.deflate(_padding);
    child.layout(innerConstraints, parentUsesSize: true);

    // 2.  ? ?=  ?+ margin  ?
    size = constraints.constrain(Size(
        child.size.width + _padding.horizontal,   //  ?=  ?+  argin +  argin
        child.size.height + _padding.vertical,    //  ?=  ?+  argin +  argin
    ));
    }

    -------------------------------------------------------------------------------------
    //  ?performLayout
    void performLayout() {
    // 1.  ?
    child.layout(innerConstraints);

    // 2.   +  ?
    size = ...;

    // 3.  ?  x,y
    child.parentData.offset = Offset(padding.left, padding.top);
    }
    -------------------------------------------------------------------------------------
    //  Row  ?performLayout
    double currentX = 0;
    for (child in children) {
    //  ?
    //   MainAxisAlignment paceBetween ?x
    //  ?spaceBetween

    child.parentData.offset = Offset(currentX, 0); //   Y
    currentX += child.size.width; //
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

            //   1.   margin ?ConstrainedBox
            BoxConstraints afterMargin = c.deflate(margin);

            //   2.   constraints_
            BoxConstraints inner = constraints_.has_value()
                                       ? (*constraints_).enforce(afterMargin)
                                       : afterMargin;

            //   3.   padding + border ?child
            const EdgeInsetsGeometry inset = EdgeInsetsGeometry::fromLTRB(
                border.left + padding.left, border.top + padding.top,
                border.right + padding.right, border.bottom + padding.bottom);
            BoxConstraints childConstraints = inner.deflate(inset);

            if (child_)
            {
                Widget &childRef = *child_;

                //   4.  ?alignment  ? ?
                //       ?alignment  ? ?childConstraints  ?
                if (alignment_.has_value())
                {
                    childRef.layout(BoxConstraints{0.0, childConstraints.maxWidth(), 0.0,
                                                   childConstraints.maxHeight()});
                }
                else
                {
                    childRef.layout(childConstraints);
                }

                //   5.
                //       ?alignment lign  ?max ight   tight
                //       ?alignment ?
                Size contentSize =
                    alignment_.has_value() ? childConstraints.biggest() : childRef.size;

                // Padding(padding).size = inner.constrain(child.size + padding + border)
                Size paddingBoxSize = inner.constrain(Size{
                    .width = contentSize.width + inset.horizontal(),
                    .height = contentSize.height + inset.vertical(),
                });

                //   6.  ?=   + padding + border + margin
                // Padding(margin).size = c.constrain(paddingBox.size + margin)
                Self().size = c.constrain(Size{
                    .width = paddingBoxSize.width + margin.horizontal(),
                    .height = paddingBoxSize.height + margin.vertical(),
                });
            }
            else
            {
                //  ?child ?margin  adding/border
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

            //  ?=  ?+ margin + border + padding
            double dx = offset.x + margin.left + border.left + padding.left;
            double dy = offset.y + margin.top + border.top + padding.top;

            //  ?alignment ?
            if (alignment_.has_value())
            {
                //  ?=  ?- padding - border - margin
                //  ?layout  ?
                //  ?free = contentSize - child.size
                //  ?contentSize = Self().size - inset - margin
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

                // Align   0 ?
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
            // (-1, -1) =>
            // ( 0,  0) =>
            // ( 1,  1) =>
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
                //   =  ?
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
                // SizedBox  ?margin/padding ?
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

                // 1.  .deflate(padding)  ?
                childRef.layout(c.deflate(padding_));

                // 2.   =   + padding
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
                // padding  TR  ?start=left, end=right ?
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
                //   =  ?
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
                //  ?margin/padding ?
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
        max,          //   =  ?
        deferToChild, //   =   child.size
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
            // 1)  ?+   child  ?
            const BoxConstraints inner = innerConstraints(c);

            if (child_)
            {
                Widget &childRef = *child_;

                // 2) child  ?inner   OverflowBox
                childRef.layout(inner);

                // 3)  eferToChild  ?  child.size ax  ? ?
                Self().size = (fit_ == OverflowBoxFit::deferToChild)
                                  ? c.constrain(childRef.size)
                                  : c.biggest();
            }
            else
            {
                //  ?child ?fit
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

            // child   /  ?freeW / freeH  ?
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
            //    ? ? ?
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
    //  ?Flutter   ConstraintsTransformBox
    //   constrainedAxis == null
    //           ?child   = [0, inf] x [0, inf]
    //   constrainedAxis == horizontal
    //          ?child   = [c.minW, c.maxW] x [0, inf]
    //   constrainedAxis == vertical
    //          ?child   = [0, inf] x [c.minH, c.maxH]
    //
    //   =  ?child.size hild  ?alignment  ?
    //  ?child  ?free  ?
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

                // child
                childRef.layout(inner);

                //   =  ?child.size ?child  ?
                Self().size = c.constrain(childRef.size);
            }
            else
            {
                //  ?child ?
                Self().size = c.smallest();
            }
        }

        constexpr void updateOffset(Offset offset) noexcept override
        {
            Self().offset = offset;
            if (!child_)
                return;

            Widget &childRef = *child_;

            // child  reeW / freeH
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
        //  ?Flutter  ?BoxConstraintsTransform ?
        [[nodiscard]] constexpr BoxConstraints transform(BoxConstraints c) const noexcept
        {
            constexpr auto inf = std::numeric_limits<double>::infinity();

            if (!constrainedAxis_.has_value())
            {
                // unconstrained:
                return BoxConstraints{0.0, inf, 0.0, inf};
            }

            if (*constrainedAxis_ == Axis::horizontal)
            {
                // heightUnconstrained:
                return BoxConstraints{c.minWidth(), c.maxWidth(), 0.0, inf};
            }

            // Axis::vertical  ?widthUnconstrained:
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
    //  ?Flutter RenderAspectRatio
    //   aspectRatio = width / height ?> 0 ?
    //
    //   1.   maxWidth ?height * aspectRatio
    //       eight = maxHeight ?0 ?
    //   2.  ?width   height = width / aspectRatio ?
    //   3.  ?4   height  ?[minH, maxH]  ?width  ?[minW, maxW] ?
    //          height > maxHeight   ? ?maxHeight   width
    //          width  < minWidth    ? ?minWidth    height
    //          height < minHeight   ? ?minHeight   width
    //   4. size = c.constrain(Size(width, height))
    //   5. child  ?BoxConstraints::tight(size)
    //
    //    ?child  size = c.smallest() ?
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

            //   1.
            double width = c.maxWidth();
            if (!c.hasBoundedWidth())
            {
                width = c.hasBoundedHeight() ? c.maxHeight() * aspectRatio_ : 0.0;
            }

            //   2.  ?
            double height = width / aspectRatio_;

            //   3.   Flutter
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

            //   4.   =   (w, h)
            Self().size = c.constrain(Size{width, height});

            //   5. child  ?tight  ?child
            child_->layout(BoxConstraints::tight(Self().size));
        }

        constexpr void updateOffset(Offset offset) noexcept override
        {
            Self().offset = offset;
            if (child_)
            {
                // AspectRatio  ?margin/padding hild
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
    //  ?Flutter RenderFractionallySizedOverflowBox
    //   widthFactor  != null  ?child   = maxWidth * widthFactor
    //   widthFactor  == null  ?child
    //   heightFactor != null  ?child   = maxHeight * heightFactor
    //   heightFactor == null  ?child
    //
    //     = c.constrain(child.size)  child
    //   child  ?alignment
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

                // 1. child
                childRef.layout(inner);

                // 2.   =  ?child.size
                //      widthFactor > 1 hild   maxWidth ?
                Self().size = c.constrain(childRef.size);
            }
            else
            {
                // 3.  ?child ?Flutter
                Self().size = c.constrain(inner.smallest());
            }
        }

        constexpr void updateOffset(Offset offset) noexcept override
        {
            Self().offset = offset;
            if (!child_)
                return;

            Widget &childRef = *child_;

            // free  ? ?child   alignment   Flutter
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
        //   Flutter  ?RenderFractionallySizedOverflowBox._getInnerConstraints
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
    // flex   [start]
    // ===============================================================================================
    // =========================================================================
    // FlexInfo  ?Flex  ?
    //  ?children_  ?Flex  ?Widget
    // =========================================================================
    struct FlexInfo
    {
        int flex = 0;                 // 0  ?
        FlexFit fit = FlexFit::tight; // flex > 0  ?
    };

    //NOTE: Spacer   takeChild ? flex  PP art
    // =========================================================================
    // FlexChildBuilder concept ?Flex   (child, flex, fit)
    // =========================================================================
    template <typename B>
    concept FlexChildBuilder = requires(B &b) {
        { b.flexValue() } -> std::convertible_to<int>;
        { b.fitValue() } -> std::convertible_to<FlexFit>;
        { b.takeChild() } -> std::convertible_to<std::unique_ptr<Widget>>;
    };

    // =========================================================================
    // FlexibleBuild  ? ?DSL  Widget
    //   FlexBuild::addChild(FlexibleBuild&)   (child, flex, fit)
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

        //  ?FlexBuild
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
        FlexFit fit_ = FlexFit::loose; // Flutter   loose
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

            //    ?children
            if (children_.empty())
            {
                const double main =
                    (mainAxisSize_ == MainAxisSize::max && canFlex) ? maxMain : 0.0;
                Self().size = c.constrain(h ? Size{main, 0.0} : Size{0.0, main});
                return;
            }

            //    ?1  ?layout
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

            //    ?2   flex  ight/loose   min
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

            //
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
        std::vector<FlexInfo> flexes_; //  ?children_
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
        //   FlexBuild::addChild
        // (1)   Widget lex = 0
        constexpr auto &addChild(std::unique_ptr<Widget> child) noexcept
        {
            children_.push_back(std::move(child));
            flexes_.push_back(FlexInfo{.flex = 0, .fit = FlexFit::tight});
            return *this;
        }
        // (2) WidgetBuild   SizedBoxBuild / PaddingBuild  ?Widget  ?(1)
        constexpr auto &addChild(WidgetBuild auto &&b) noexcept
        {
            return addChild(
                static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
        }
        // (3) FlexibleBuild / ExpandedBuild / SpacerBuild  (child, flex, fit)
        template <FlexChildBuilder B>
        constexpr auto &addChild(B &&fb) noexcept
        {
            children_.push_back(fb.takeChild());
            flexes_.push_back(FlexInfo{fb.flexValue(), fb.fitValue()});
            return *this;
        }
        // =====================================================================

        //   Flexible DSL  (child, flex, fit)
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
    // Row / Column  ?Flex
    //
    //  ?Dart  ?Row / Column
    //   Row    = Flex(direction: Axis::horizontal)
    //   Column = Flex(direction: Axis::vertical)
    //
    //  ainAxisAlignment / mainAxisSize /
    // crossAxisAlignment / textDirection / verticalDirection / textBaseline / children ?
    //  ?FlexBuild ?
    // ===============================================================================================
    struct RowBuild : FlexBuild
    {
        constexpr RowBuild(std::string key) noexcept : FlexBuild(std::move(key))
        {
            FlexBuild::direction(Axis::horizontal);
        }
        // Row  ?horizontal ?direction()  ?
        FlexBuild &direction(Axis) noexcept = delete;
    };
    struct ColumnBuild : FlexBuild
    {
        constexpr ColumnBuild(std::string key) noexcept : FlexBuild(std::move(key))
        {
            FlexBuild::direction(Axis::vertical);
        }
        // Column  ?vertical ?direction()  ?
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

    //NOTE: ParentDataWidget<FlexParentData>  ?FlexibleBuild   flex.  lutter ?FlexInfo  ?
    static constexpr auto Flexible(std::string key = {}) noexcept // NOLINT
    {
        return FlexibleBuild{std::move(key)};
    }

    // =========================================================================
    // ExpandedBuild  ?Flexible(fit: FlexFit.tight)
    //   FlexibleBuild  ?fit  ?tight
    // =========================================================================
    struct ExpandedBuild : FlexibleBuild
    {
        constexpr explicit ExpandedBuild(std::string key = {}) noexcept
            : FlexibleBuild(std::move(key))
        {
            FlexibleBuild::fit(FlexFit::tight);
        }

        //   fit xpanded  ?tight
        FlexibleBuild &fit(FlexFit) noexcept = delete;
    };
    static constexpr auto Expanded(std::string key = {}) noexcept // NOLINT
    {
        return ExpandedBuild{std::move(key)};
    }

    // NOTE: Spacer  ?key  ?0 0 SizedBoxWidget  (flex, tight)   Flex  ?
    // NOTE:  ?FlexBuild::addChild(FlexibleBuild&)  ?concept   Spacer
    /*
class Spacer extends StatelessWidget {
  const Spacer({super.key, this.flex = 1}) : assert(flex > 0);
  final int flex;
  Widget build(BuildContext context) => Expanded(
    flex: flex,
    child: const SizedBox.shrink(),   // 0 0
  );
}

    Spacer  ?StatelessWidget  key   Expanded(tight)  ?SizedBox.shrink() ?

    find.byKey('s')   Spacer enderObject   RenderConstrainedBox  SizedBox.shrink()  ?renderObject ?

     ?Spacer  ?Widget  Flexible  find ?getSize  ?

      = SizedBox(0,0)  ?tight   ( ?  0  ? ?
*/
    // =========================================================================
    // SpacerBuild  ?Spacer
    //  ?key  ?0 0 SizedBoxWidget ?
    //  ?(flex, FlexFit::tight)   Flex  ?
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

        //    ?FlexBuild   FlexibleBuild
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
            // 0 0  key  ?Spacer
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

            //    ?children ?constraints.smallest
            if (children_.empty())
            {
                Self().size = c.smallest();
                runs_.clear();
                return;
            }

            //   flip   RenderWrap.performLayout
            flipMainAxis_ =
                h ? (textDirection_.value_or(TextDirection::ltr) == TextDirection::rtl)
                  : (verticalDirection_ == VerticalDirection::up);
            flipCrossAxis_ =
                h ? (verticalDirection_ == VerticalDirection::up)
                  : (textDirection_.value_or(TextDirection::ltr) == TextDirection::rtl);

            //    ?max ?
            const BoxConstraints childConstraints =
                h ? BoxConstraints{0.0, maxMain, 0.0, inf}
                  : BoxConstraints{0.0, inf, 0.0, maxMain};

            //     1  runs
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

                //  unMain + spacing + childMain > mainAxisLimit
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
            // flush  ?run
            if (childCount > 0)
            {
                mainAxisExtent = std::max(mainAxisExtent, runMain);
                crossAxisExtent += runCross;
                if (!runs_.empty())
                    crossAxisExtent += runSpacing_;
                runs_.push_back(RunInfo{runMain, runCross, runStart,
                                        static_cast<size_t>(childCount)});
            }

            //     2 ?
            Self().size = c.constrain(h ? Size{mainAxisExtent, crossAxisExtent}
                                        : Size{crossAxisExtent, mainAxisExtent});

            //    ?updateOffset  ?
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

            //   runs
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
            runBetweenSpace += runSpacing_; //  ?  runSpacing

            double crossAxisOffset =
                flipCrossAxis_ ? containerCrossExt_ - runLeadingSpace : runLeadingSpace;

            for (int i = 0; i < runCount; ++i)
            {
                const auto &run = runs_[i];
                const double runMain = run.mainSize;
                const double runCross = run.crossSize;
                const int cnt = static_cast<int>(run.childCount);

                //   run  ?
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
                childBetweenSpace += spacing_; //  ?  spacing

                double childMainPosition = flipMainAxis_
                                               ? containerMainExt_ - childLeadingSpace
                                               : childLeadingSpace;

                // flipCrossAxis  run  ? ?runCross
                if (flipCrossAxis_)
                    crossAxisOffset -= runCross;

                for (size_t k = 0; k < run.childCount; ++k)
                {
                    auto &child = children_[run.startIdx + k];
                    const double childMain = h ? child->size.width : child->size.height;
                    const double childCross = h ? child->size.height : child->size.width;

                    //   run   flipCrossAxis
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

                    // flipMainAxis  ?childMain
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

                //    ?run
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
            double mainSize;   //  ?run  ?run  ?spacing ?
            double crossSize;  //  ?run
            size_t startIdx;   // children_
            size_t childCount; //  ?run  ?
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

        // layout  pdateOffset
        std::vector<RunInfo> runs_;
        bool flipMainAxis_ = false;
        bool flipCrossAxis_ = false;
        double containerMainExt_ = 0.0;  //  onstrain
        double containerCrossExt_ = 0.0; //  constrain
        double totalMainExt_ = 0.0;      //  ?run
        double totalCrossExt_ = 0.0;     //  ?run  ?runSpacing ?
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
 ?
    layout        ?  widget  ?(W, H) + Offset
      update   ? ?Model   shader
    render        ? ?
Flow
    layout        ? ?(W, H) ffset
    paint      ?  delegate  Matrix4
    render        ? ?
NOTE:  renderobject  odel ?
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
    // StackChildInfo  ?Stack  ?children_  ?
    //  ?=  ?null =
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
    // PositionedBuild  ?Stack  ?DSL  Widget ?
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

        //  ?0 ?Positioned.fill ?
        auto &fill() noexcept
        {
            left_ = 0;
            top_ = 0;
            right_ = 0;
            bottom_ = 0;
            return *this;
        }

        //  romRect(left, top, width, height)
        auto &fromRect(double l, double t, double w, double h) noexcept
        {
            left_ = l;
            top_ = t;
            width_ = w;
            height_ = h;
            return *this;
        }

        //  romRelativeRect(left, top, right, bottom)
        auto &fromLTRB(double l, double t, double r, double b) noexcept
        {
            left_ = l;
            top_ = t;
            right_ = r;
            bottom_ = b;
            return *this;
        }

        //  irectional  start/end  ?textDirection  ?left/right ?
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
            //    ?children
            if (children_.empty())
            {
                const Size big = c.biggest();
                const bool finite = std::isfinite(big.width) && std::isfinite(big.height);
                Self().size = finite ? big : c.smallest();
                return;
            }

            //    ?fit
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

            //    ?
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

            //   Stack
            Self().size = hasNonPositioned ? Size{width, height} : c.biggest();

            //    ?
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
                    //
                    if (info.left)
                        x = *info.left;
                    else if (info.right)
                        x = Self().size.width - *info.right - child->size.width;
                    else
                    {
                        const double freeW = Self().size.width - child->size.width;
                        x = freeW * (alignment_.x + 1.0) / 2.0;
                    }
                    //
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
        //   RenderStack.layoutPositionedChild
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

        //
        auto &addChild(std::unique_ptr<Widget> child) noexcept
        {
            children_.push_back(std::move(child));
            infos_.push_back({}); //  ? ? ?
            return *this;
        }
        auto &addChild(WidgetBuild auto &&b) noexcept
        {
            return addChild(
                static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
        }

        //  ?
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

    ///NORE: Baseline  ?

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
            // child   offstage  ?layout
            if (child_)
                child_->layout(c);

            //   size ?
            //   offstage=true   ?constraints.smallest
            //   offstage=false  ?child.size  child  ?smallest ?
            if (offstage_ || !child_)
                Self().size = c.smallest();
            else
                Self().size = child_->size;
        }

        constexpr void updateOffset(Offset offset) noexcept override
        {
            Self().offset = offset;
            if (child_)
                child_->updateOffset(offset); //   Offstage   (0,0)
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

    // NOTE: CustomSingleChildLayout   widget
    // =========================================================================
    // SingleChildLayoutDelegate  ? ?
    //  ?layout  ?W/H + Offset
    // =========================================================================
    struct SingleChildLayoutDelegate
    {
        virtual ~SingleChildLayoutDelegate() = default;

        ///  ?constrain
        virtual Size getSize(BoxConstraints c) const
        {
            return c.biggest();
        }

        ///   child  elegate
        virtual BoxConstraints getConstraintsForChild(BoxConstraints c) const
        {
            return c;
        }

        ///   child  ?
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
            // 1.   = constrain(delegate.getSize(c))
            Self().size = c.constrain(delegate_->getSize(c));

            // 2. child  ?delegate  ?layout
            if (child_)
                child_->layout(delegate_->getConstraintsForChild(c));
        }

        void updateOffset(Offset offset) noexcept override
        {
            Self().offset = offset;
            if (!child_)
                return;

            // 3.   = delegate.getPositionForChild(  size, child size)
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
    // MultiChildLayoutContext  ?delegate.performLayout  ?children
    // hasChild / layoutChild / positionChild   Flutter  ?API
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

        ///   constrain
        virtual Size getSize(BoxConstraints c) const
        {
            return c.biggest();
        }

        ///  ?layout   ctx.layoutChild / ctx.positionChild   children
        virtual void performLayout(Size size, MultiChildLayoutContext &ctx) const = 0;
    };
    // =========================================================================
    // LayoutIdBuild  ? ?(id, child)  ?delegate
    //  ?Widget  CustomMultiChildLayoutBuild
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
            // 1.   = constrain(delegate.getSize(c))
            Self().size = c.constrain(delegate_->getSize(c));

            // 2.  ?child  ?(0, 0)
            std::fill(positions_.begin(), positions_.end(), Offset{0.0, 0.0});

            // 3.  ?context   delegate
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

            //   quarterTurns ?
            const BoxConstraints childC =
                odd ? BoxConstraints{c.minHeight(), c.maxHeight(), c.minWidth(),
                                     c.maxWidth()}
                    : c;

            child_->layout(childC);

            //   size = constrain( ?childSize)
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
            const Size cs = child_->size; //   child

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
    // flex   [end]
    // ===============================================================================================
}; // namespace ui_new

//diff: [test_dod24.cpp] end [

//diff: [test_dod22] end

//  ?
//  ?
//  ?
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
            // NOTE:  ?
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

    //  ?bad_alloc lots_ realloc ?
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

    //  swap-remove active_list_ +  ?free_head_ ?occupied ?
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

    //  ?attach   sink  obj + handler
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

    //  ? Obj  ?(void*, proxy_class_type)
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

    //  ?
    //    ?*  /   /  ** ?* ?* inds.size() ?
    //     extent  std::size_t  ? " ? ? ? ?
    //    std::uint32_t / std::uint16_t
    //   std::size_t  "   ? " ?
    using index_t = std::size_t;

    //  ?
    // 0.
    //  ?

    //
    //
    //   idle   ? ?claim
    //   hold   ? ?claim ?
    //   ready  ? ?claim ?
    //
    // ready   claim
    //  rag  hold/ready  ?ready  ?
    //
    //   done / aborted / reset ?claim  ?
    //   idle ?
    enum class claim_state : std::uint8_t
    {
        idle,
        hold,
        ready
    };

    enum class verdict : std::uint8_t
    {
        accepted,  //  ?
        superseded //
    };

    //  ?
    // 0.1  ? ? ?
    //
    //    ? ?session ?feed / tick  ?
    //      ?idle / hold / ready  ? ?
    //      ?              ?  feed  aw_of  ?accepted-frame
    //
    //   arbiter  ? ?
    //      ?win()   ? ?claim
    //      ?lose()  ?  claim
    //
    //     report[]  ?
    //     win/lose   session ?
    //
    //     reset session  ?
    //    ? ? in/lose  ?
    //
    //      /
    //   R1  ?kind  ?claim ?
    //   R2  eady  ?claim  ?win/lose   idle/hold ?
    //   R3   win/lose  ?claim  ?
    //   R4 feed/tick  oexcept ick  ?
    //   R5   claim old ?
    //       ?
    //   R6  in  ?ready ?ready
    //        claim   ready ?
    //  ?
    namespace recognizer
    {
        //  ? ?
        //  ?claim_state   Frame ayload pan ?
        template <class M, class Self, class Frame>
        concept can_feed = requires(M &m, Self &self, const Frame &f, time_point t) {
            { m.feed(self, f, t) } noexcept -> std::same_as<claim_state>;
        };
    } // namespace recognizer

    //  ?
    struct observation
    {
        std::string_view name{};
        verdict v{};
        time_point at{}; //
    };

    //    ?
    //    trace ?
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

    //   if constexpr  ?
    struct null_trace
    {
    };

    //  ?
    // 1. plan 1  ?
    //      kinds  :  ?/  ?
    //      awaits : (i,j)  ?i  ?j
    //      excl   : (i,j)  ?i  ?j
    //  ?
    struct kind_desc
    {
        static_string name{};    //  /
        static_string fn_name{}; //  invoke<fn_name> ?kind  ?

        friend constexpr bool operator==(const kind_desc &, const kind_desc &) = default;
    };
    struct plan_data
    {
        std::vector<kind_desc> kinds;
        //  ?index_t ?kinds.size()
        //  " " ? ? ?-1  ?
        std::vector<std::pair<index_t, index_t>> awaits;
        std::vector<std::pair<index_t, index_t>> excl;

        //    , b  ?+ base
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

    //   =   +  ?
    //     ui  ?
    //
    //    ?
    //     Name    ?kind  ? ? ?
    //     FnName  ? ink  ?invoke<FnName>(payload) ?
    //                 kind  ?
    //    ?feed  ?law_of
    //     channel  ?
    template <class Fsm>
    struct leaf
    {
        using protocol = Fsm; //
        static constexpr auto name = Fsm::name;       // kind  ?
        static constexpr auto fn_name = Fsm::fn_name; //  invoke<fn_name> ?
        using consume_t = Fsm::consume_t;

        static consteval plan_data plan()
        {
            return plan_data{.kinds = {{name, fn_name}}, .awaits = {}, .excl = {}};
        }
    };

    //  ?
    template <class... Rs>
    struct all_of
    {
        using children_t = std::tuple<Rs...>; //

        static consteval plan_data plan()
        {
            plan_data p;
            ((p = p + Rs::plan()), ...);
            return p;
        }
    };

    //  ?excl
    template <class... Rs>
    struct one_of
    {
        using children_t = std::tuple<Rs...>;

        static consteval plan_data plan()
        {
            constexpr std::size_t K = sizeof...(Rs);
            std::array<plan_data, K> parts{}; // ?plan_data
            index_t k = 0;
            ((parts[k++] = Rs::plan()), ...);

            //  ?off  ?p
            plan_data p;
            std::array<index_t, K + 1> off{};
            for (std::size_t i = 0; i < K; ++i)
            {
                off[i + 1] = off[i] + static_cast<index_t>(parts[i].kinds.size());
                p = p + parts[i];
            }
            /*
              (a, b)  a < b a  ?
              (x, y)    a  y   b  ?
             ?(x, y)   excl
            */
            // NOTE:  ?
            for (index_t a = 0; a < K; ++a)
                for (index_t b = a + 1; b < K; ++b)
                    for (index_t x = off[a]; x < off[a + 1]; ++x)
                        for (index_t y = off[b]; y < off[b + 1]; ++y)
                            p.excl.push_back({x, y});
            return p;
        }
    };

    //    ?B
    template <class A, class B>
    struct refines
    {
        using children_t = std::tuple<A, B>;

        static consteval plan_data plan()
        {
            auto pa = A::plan();
            auto pb = B::plan();
            const index_t na = static_cast<index_t>(pa.kinds.size());
            auto p = pa + pb; // NOTE:  ?
            // NOTE: A  ?    ?B  ?
            for (index_t i = 0; i < na; ++i)
                for (index_t j = na; j < static_cast<index_t>(p.kinds.size()); ++j)
                    p.awaits.push_back({i, j});
            return p;
        }
    };

    //  ?
    // 2. validate ?0-goal  ?3
    //     ? ? ? ? ?awaits
    //
    //     ?feed
    //    law_of   accepted-frame  ?
    //    name  / ?* ** ?
    //    fn_name  ? ?  kind  ?
    //     ?FSM
    //     ?shape  ?
    //  ?
    consteval void validate(const plan_data &p)
    {
        const std::size_t n = p.kinds.size(); //  ?kind ?

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

        // awaits   / Kahn ?
        //
        //  waits   (i, j)     j ?
        //    ? ? ?throw ?
        //
        //
        //   1. indeg[i] =    ?
        //   2.  ?indeg == 0    ?
        //       --indeg ?
        //   3.    ?   ?
        //
        //  ?O(n    |awaits|)  awaits  ?
        // max_kinds = 64   validate  ?Kahn ?
        std::vector<std::size_t> indeg(n, 0); // indeg[i] = i  " ?
        for (auto [i, j] : p.awaits)
            ++indeg[i]; // i  ?j   ?

        std::vector<bool> done(n, false);     // done[i]  = i  ?
        for (std::size_t left = n; left > 0;) // left =
        {
            bool prog = false; //  ?
            for (index_t i = 0; i < n; ++i)
                if (!done[i] && indeg[i] == 0) //    ?
                {
                    done[i] = true;
                    --left;
                    prog = true;
                    //   i  ?  i" ?x  ?
                    // (x, y)  ?y == i     i ?
                    for (auto [x, y] : p.awaits)
                        if (y == i)
                            --indeg[x];
                    //  ?for  ?break ?
                }
            if (!prog) //  ? ?   ?
                throw "refines cycle";
        }
    }

    //  ?
    // 3. bset ? "
    //
    //       ?i  ** **
    //      law::beats[i]    ?j  ?= "i  ?j"        =
    //      live           ?i  ?= " ?i  ?kind  ?claim"
    //      ready          ?i  ?= " ?
    //      advance        ?i  ?= " "
    //
    //
    //      blocked =  ? ? ?         ? for j: blocked |= beats[j]
    //      win     = ready  ?blocked      ? ready & ~blocked
    //      lose    =  ? ? )  ?live    ? for i: lose |= beats[i]; lose &= live
    //    "  i" (j)  " "
    //     ?blocked  ?
    //
    //        64    64
    //        =  /64 ? ?4  S=70  S=6  ?
    //       eats  ?S /64 ?  ?blocked  ?S /64 ?
    //       ?closure  ?S /64 ?validate  ?Kahn
    //       ?awaits  =200   GCC  ?constexpr  ?
    //
    //
    //          set/clr   [0,S)
    //        ~   [0,S)     x & ~y  ?
    //         ?none()  !has(0)
    //        |  ?   x & ~y ?x  ?y ?
    //  ?
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
        // "  i  ? ?
        //
        //   law::beats[i].has(j)   ?i  ?j  ?
        //   live.has(i)            ? ?i  ?kind  ?claim  ?
        //   advance.has(i)         ?
        constexpr bool has(index_t i) const noexcept
        {
            return (w[i / 64] >> (i % 64)) & 1;
        }
        //  O(popcount) ?O(S)
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
        //  ? " ? " ? ?  ?
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
            //   [0,S)
            //  " ?
            if constexpr (S % 64 != 0)
                a.w[W - 1] &= (std::uint64_t{1} << (S % 64)) - 1;
            return a;
        }
        constexpr bool operator==(const bset &) const noexcept = default;
    };
    //  ?
    // 4.  hape ? ?
    //
    //
    //      leaf_count     :
    //      recognizers    :  ?
    //      consume_t      :  ?leaf consume_t  ?
    //      consume_of<I>  :  ?I   consume_t ?
    //      unique_machines:  ?FSM
    //
    //    "  F" ?advance_mask<Tree,F>()  ?
    //     ? ?F"  4.3 ?
    //  ?
    namespace detail
    {
        //  ?=  ?children_t  ?
        //  ?= children_t td::tuple<Rs...> ?
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

        //  ?=  ?protocol
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

            //  ?FSM    ? ?claim ?
            // name  ?
            static constexpr bool unique_machines = []() consteval {
                constexpr std::size_t N = sizeof...(Ls);
                std::array<std::meta::info, N> m{^^typename Ls::protocol...};
                for (std::size_t i = 0; i < N; ++i)
                    for (std::size_t j = i + 1; j < N; ++j)
                        if (m[i] == m[j])
                            return false;
                return true;
            }();
            static_assert(unique_machines, "duplicate FSM type in the same forest");

            //  ?leaf  ?consume_t  ?
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
            // consume_t   change
            //  ?feed(frame<T>)  ?

            //  ?I  ?leaf  ?consume_t  I  ?
            template <std::size_t I>
            using consume_of = typename std::tuple_element_t<I, recognizers>::consume_t;
        };

        template <class Tree>
        using shape_of = typename[:std::meta::substitute(^^shape, leaf_infos(^^Tree)):];

        // T  ?Tuple
        template <class T, class Tuple>
        static consteval bool in_tuple() noexcept
        {
            for (auto a : template_args<Tuple>())
                if (std::meta::dealias(a) == std::meta::dealias(^^T))
                    return true;
            return false;
        }

    } // namespace detail

    //  ?
    // 4.3 advance_mask<Tree, Frame>() ** ** ?
    //  ?Frame  ree ?
    //  ?
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

    //  ?
    // 4.2   64
    //
    //   64  " ?   /64 ? ?S  ?
    //    ** ** ?
    //       refines   ?S/2   B  ?S/2 ?= S /4 ?
    //       S=70  ? ?
    //       S=96  ?'constexpr' operation count exceeds limit of 33554432
    //      losure   S /64  validate  ?Kahn ?
    //      ?awaits  ?O(n E) ?
    //
    //    ?64 ?uint64
    //    ?validate  ?Kahn   O(n+E) ?
    //    ?
    //  ?
    inline constexpr std::size_t max_kinds = 64;

    //  ?
    // 4.5   Warshall i   ?i
    //       test_matrix_series.cpp B4 ?
    //      ?law_of::beats   3  ?
    //  ?
    template <std::size_t S>
    consteval std::array<bset<S>, S> closure(std::array<bset<S>, S> m)
    {
        for (std::size_t k = 0; k < S; ++k)
            for (std::size_t i = 0; i < S; ++i)
                if (m[i].has(k))
                    m[i] |= m[k];
        return m;
    }

    //  ?
    // 5.  ?
    //
    //    beats[i] =  ?i  ?= "i  " ?
    //      refines<A,B>    ?A
    //      one_of<R...>  ?
    //     ? ? ?
    //
    //     ? ? ?* **  ?
    //     ? " " ?
    //  ?
    template <class Tree>
    struct law_of
    {
        using shape = detail::shape_of<Tree>;
        //  ?leaf  ?consume_t   beats ?
        using change_types = typename shape::consume_t;
        static constexpr std::size_t S = shape::leaf_count;
        static constexpr std::size_t C = std::tuple_size_v<change_types>;

        //     max_kinds  ?max_kinds
        static_assert(S >= 1, " ");
        static_assert(S <= max_kinds, "too many leaves for the intent bitmap");
        static_assert(C >= 1, "change_types must not be empty");

        //  i
        static constexpr std::array<bset<S>, S> beats = []() consteval {
            std::array<bset<S>, S> m{};
            const auto p = Tree::plan();
            validate(p);                 //  ?/   / awaits
            for (auto [a, b] : p.awaits) // a  ?b  ?b  ?a
                m[b].set(a);
            for (auto [a, b] : p.excl) //
            {
                if (b < a)
                    std::swap(a, b); //  ?a  ? ?
                m[a].set(b);         //  ?
            }
            m = closure<S>(m); //   Warshall ?
            for (index_t i = 0; i < S; ++i)
                if (m[i].has(i))
                    throw "beats  efines / one_of  ";

            //   kind   change ?consume_t ?
            // consumes
            // feed(frame<T>)  ?change ?
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
                    throw "kind  ?input_frame change";
            }

            //  / ?
            for (index_t i = 0; i < S; ++i)
                for (index_t j = 0; j < S; ++j)
                    if (m[i].has(j) && (consumes[i] & consumes[j]).none())
                        throw "beats relation endpoint has no common change type";
            return m;
        }();

        //  ? ?
        //   static_string  ?const char* ? B string_view  ?16 B ?
        //    view()  ?strlen  ?
        static constexpr std::array<static_string, S> names = []() consteval {
            std::array<static_string, S> r{};
            const auto p = Tree::plan();
            for (index_t i = 0; i < S; ++i)
                r[i] = p.kinds[i].name;
            return r;
        }();
    };

    //  ?
    // 6.
    //
    //     eport[N]  claim_state ?
    //          beats[N] ?
    //     in / lose
    //
    //      Frame rena_event ayload pan ?
    //     ?win/lose  ?arbiter  ?
    //  ?
    template <std::size_t S>
    struct decision
    {
        bset<S> win{}, lose{}; //  win  ?
    };

    //
    template <std::size_t S>
    constexpr decision<S> arbitrate(const std::array<claim_state, S> &report,
                                    const std::array<bset<S>, S> &beats) noexcept
    {
        //  ?  live / ready ?
        bset<S> live{}, ready{};
        for (index_t i = 0; i < S; ++i)
        {
            if (report[i] != claim_state::idle)
                live.set(i);
            if (report[i] == claim_state::ready)
                ready.set(i);
        }
        if (ready.none())
            return {}; //  ?ready   win  win   lose

        //  ? ?live  ?
        bset<S> blocked{};
        live.for_each([&](index_t j) noexcept { blocked |= beats[j]; });

        //  ?ready  ?
        const bset<S> win = ready & ~blocked;

        //  ? ?win  ?
        bset<S> lose{};
        win.for_each([&](index_t i) noexcept { lose |= beats[i]; });
        lose &= live & ~win;

        return {win, lose};
    }

    //  ?
    // 9. arbiter ?
    //  ?
    template <class Tree>
    struct arbiter
    {
        using shape = detail::shape_of<Tree>;
        using law = law_of<Tree>;
        static constexpr std::size_t S = shape::leaf_count;
        using Recognizers = typename shape::recognizers;

        Recognizers recognizers{};
        std::array<claim_state, S> reports{}; //

        //  ?
        template <class Self, class Frame>
        constexpr void feed(Self &self, const Frame &f, time_point now) noexcept
        {
            null_trace t{};
            feed<Self, Frame, null_trace>(self, f, now, t);
        }

        //  ?Trace  ?debug_arbiter
        template <class Self, class Frame, class Trace>
        constexpr void feed(Self &self, const Frame &f, time_point now, Trace &t) noexcept
        {
            constexpr bset<S> advance = advance_mask<Tree, Self, Frame>();
            if constexpr (advance.none())
                return; //  ?Frame

            template for (constexpr auto I : std::ranges::views::indices(S))
            {
                if constexpr (advance.has(static_cast<index_t>(I)))
                    reports[I] = std::get<I>(recognizers).feed(self, f, now);
            }
            resolve(self, now, t);
        }

        //  ?
        template <class Self>
        constexpr void tick(Self &self, time_point now) noexcept
        {
            null_trace t{};
            tick<Self, null_trace>(self, now, t);
        }

        //  ?Trace  ?debug_arbiter
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
        // report -> verdict ->  ?
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

        //  in/lose
        //   report  ready  ?
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

    //  ?
    // 10. debug_arbiter:  arbiter +
    //
    //    ?feed/tick   trace  ?
    //    ?arbiter   trace  ?
    //  ?
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

//  ?
//  arena_event LFW  ?
//    event_context keys / buttons / modifier / pointer ?
//    frame<T> ontext +   T    change  ?
//  FSM   input_frame::frame<T> ?GLFW  ?
//  ?
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

    // NOTE:
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
                      "consume_t  ?input_frame  ?change  ");
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
        //   return uto  ?void ?
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
        // scroll / char  ctx
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

    //  ?base ame / fn_name / policy / payload_t
    template <class Payload, static_string Name, static_string Fn, class Policy>
    struct trait_base
    {
        using payload_t = Payload;
        static constexpr auto name = Name;
        static constexpr auto fn_name = Fn;
        using policy = Policy;
    };

    //   data one_payload  ?
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

    //
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

    //   click_template
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

    //   multi_click_template
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

    //   drag_template
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

    //   hover_template one ?
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

    //   wheel_template
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

    //   key_template one ?
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

    //   key_observer_template
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

    //   chord_template
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

//  ?
//  mcs::app  ?  + Trait + tree + arena
//  ?
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

    //   arena ?Trait ?payload + collect
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

        // name / fn_name / policy / payload_t   base ?payload + collect

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
                    std::println("  >>>      (x={:.1f}, y={:.1f})", e.x, e.y);
            }),
            method<"on_double_click">(
                [](auto &self, const Trait_double_click::payload_t &e) noexcept {
                    ++self.counters.double_click;
                    self.double_click = e;
                    self.counters.x = e.x;
                    self.counters.y = e.y;
                    if (self.verbose)
                        std::println("  >>>      (x={:.1f}, y={:.1f}, count={})", e.x,
                                     e.y, e.count);
                }),
            method<"on_triple_click">(
                [](auto &self, const Trait_triple_click::payload_t &e) noexcept {
                    ++self.counters.triple_click;
                    self.triple_click = e;
                    self.counters.x = e.x;
                    self.counters.y = e.y;
                    if (self.verbose)
                        std::println("  >>>      (x={:.1f}, y={:.1f}, count={})", e.x,
                                     e.y, e.count);
                }),
            method<"on_drag">([](auto &self, const Trait_drag::payload_t &e) noexcept {
                self.drag = e;
                switch (e.phase)
                {
                case gesture::drag_phase::begin:
                    ++self.counters.drag_begin;
                    if (self.verbose)
                        std::println("  >>>  ?(  {:.1f},{:.1f})", e.x, e.y);
                    break;
                case gesture::drag_phase::move:
                    ++self.counters.drag_move;
                    self.counters.move_x = e.x;
                    self.counters.move_y = e.y;
                    self.counters.move_dx = e.dx;
                    self.counters.move_dy = e.dy;
                    if (self.verbose)
                        std::println(
                            "  >>>   ( ?{:.1f},{:.1f}; d={:.1f},{:.1f})", e.x,
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
                            "  >>>   ( ?{:.1f},{:.1f};  ?{:.1f},{:.1f})",
                            e.x, e.y, e.total_dx, e.total_dy);
                    break;
                }
            }),
            method<"on_hover">([](auto &self) noexcept {
                ++self.counters.hover;
                if (self.verbose)
                    std::println("  >>>       ");
            }),
            method<"on_wheel">([](auto &self, const Trait_wheel::payload_t &e) noexcept {
                ++self.counters.wheel;
                self.wheel = e;
                self.counters.delta = e.delta;
                if (self.verbose)
                    std::println("  >>>      (delta={})", e.delta);
            }),
            method<"on_key_a">([](auto &self) noexcept {
                ++self.counters.key_a;
                if (self.verbose)
                    std::println("  >>>   A");
            }),
            method<"on_key_c">([](auto &self) noexcept {
                ++self.counters.key_c;
                if (self.verbose)
                    std::println("  >>>   C");
            }),
            method<"on_shortcut">(
                [](auto &self, const Trait_shortcut::payload_t &e) noexcept {
                    ++self.counters.shortcut;
                    self.shortcut = e;
                    self.counters.commands += e.n;
                    if (self.verbose)
                    {
                        std::print("  >>> shortcut:");
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

//
#include "test_intent8_draw_pass.hpp"
#include "test_intent8_ability_test.hpp"
#include "test_intent8_frame_scheduler.hpp"
#include "test_intent8_resource_state.hpp"


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
    auto &secondaryCommandBuffers = *hardwareCtx.secondaryCommandBuffers.get();

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
        recorder.buffers.globalVertexBuffer.write(
            0, allVertices.data(), allVertices.size() * sizeof(allVertices[0]));
        recorder.buffers.globalIndexBuffer.write(
            0, allIndices.data(), allIndices.size() * sizeof(allIndices[0]));
    }

    auto mainPipelineCtx = initPipeline(hardwareCtx, descriptorCtx);
    auto &[pipelineLayout, pipelineOpaque3D, pipelineTransparent3D, pipelineOpaqueUI,
           pipelineTransparentUI, pipelineAdditiveUI, pipelineMultiplyUI,
           pipelineStencilMask, pipelineStencilContent,
           swapchainAttachments, pickingAttachments,
           pipelineRenderingInfo, stateOwnership, features,
           passTargetTemplate, transferScratch, offscreenMsaaBuild,
           offscreenResolveBuild, stencilBuild, offscreenMsaaResource,
           offscreenResolveView, offscreenResolveImage, offscreenTextureIndex,
           stencilResource, stencilView, stencilImage] = mainPipelineCtx;
    auto &[depthResourcesBuild, depthResource, msaaResourcesBuild, msaaResource] =
        swapchainAttachments;
    auto &[pickResourcesBuild, pickResource, resolveResourcesBuild, resolveResource,
           pickingFrames, pickMouse] = pickingAttachments;

    // =====================================================================
    // intent8 draw layer: collect non-owning views of the init-created
    // config + resources. Nothing here creates or destroys a Vulkan object
    // except the ShaderLibrary modules that describe the programmable stages.
    // =====================================================================
    intent8::GlobalConfig drawConfig{};
    intent8::GlobalResources drawResources{};
    intent8::DrawContext drawCtx{};

    {
        const auto properties = physical_device.getProperties();
        drawConfig.capabilities.apiVersion = properties.apiVersion;
        drawConfig.capabilities.properties = properties;
        drawConfig.capabilities.dynamicRendering = true;
        drawConfig.capabilities.synchronization2 = true;
        drawConfig.capabilities.bufferDeviceAddress = true;
        drawConfig.capabilities.descriptorIndexing = true;
        drawConfig.capabilities.extendedDynamicState = true;
        drawConfig.capabilities.multiDrawIndirect = true;
        drawConfig.capabilities.independentBlend = true;
    }

    intent8::ShaderProgram drawProgram{};
    {
        auto *vert =
            drawConfig.shaders.load(device, VERT_SHADER_PATH, VK_SHADER_STAGE_VERTEX_BIT);
        auto *frag = drawConfig.shaders.load(device, FRAG_SHADER_PATH,
                                             VK_SHADER_STAGE_FRAGMENT_BIT);
        assert(vert != nullptr && frag != nullptr);
        drawProgram.addStage({.stage = VK_SHADER_STAGE_VERTEX_BIT,
                              .module = vert->module,
                              .entryPoint = "main"});
        drawProgram.addStage({.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                              .module = frag->module,
                              .entryPoint = "main"});
        drawProgram.pushConstantStages =
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        drawProgram.pushConstantOffset = 0;
        drawProgram.pushConstantSize = sizeof(PushData);
    }

    const std::array<VkDescriptorSetLayoutBinding, 4> drawBindings{{
        {.binding = 0,
         .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_VERTEX_BIT},
        {.binding = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
         .descriptorCount = 64,
         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
        {.binding = 2,
         .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
         .descriptorCount = 4,
         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
        {.binding = 3,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_VERTEX_BIT},
    }};
    const intent8::DescriptorLayout *drawDescriptorLayout =
        drawConfig.descriptorLayouts.add(
            *descriptorSetLayout, drawBindings,
            VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT);
    assert(drawDescriptorLayout != nullptr);

    const VkPipelineLayout drawPipelineLayout = *pipelineLayout;
    const auto addPipeline = [&](const char *name, VkPipeline handle,
                                 const intent8::PipelineRenderingConfig &rendering,
                                 bool depthTest, bool depthWrite, bool stencil)
        -> const intent8::DrawPipeline * {
        intent8::PipelineConfig config = intent8::make_pipeline_config(
            drawPipelineLayout, drawProgram, rendering, stateOwnership);
        config.depthTestEnable = depthTest;
        config.depthWriteEnable = depthWrite;
        config.stencilTestEnable = stencil;
        config.colorAttachmentCount = rendering.colorCount;
        intent8::PipelineResource resource{.pipeline = handle,
                                           .layout = drawPipelineLayout};
        return drawConfig.pipelines.add(name, std::move(config), resource,
                                        drawDescriptorLayout);
    };

    auto stencilRendering = pipelineRenderingInfo;
    stencilRendering.depthFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
    stencilRendering.stencilFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;

    const auto *drawOpaque3D = addPipeline("opaque_3d", *pipelineOpaque3D,
                                           pipelineRenderingInfo, true, true, false);
    const auto *drawTransparent3D = addPipeline(
        "transparent_3d", *pipelineTransparent3D, pipelineRenderingInfo, true, false,
        false);
    const auto *drawOpaqueUI = addPipeline("opaque_ui", *pipelineOpaqueUI,
                                           pipelineRenderingInfo, false, false, false);
    const auto *drawTransparentUI = addPipeline(
        "transparent_ui", *pipelineTransparentUI, pipelineRenderingInfo, false, false,
        false);
    const auto *drawAdditiveUI = addPipeline("additive_ui", *pipelineAdditiveUI,
                                             pipelineRenderingInfo, false, false,
                                             false);
    const auto *drawMultiplyUI = addPipeline("multiply_ui", *pipelineMultiplyUI,
                                             pipelineRenderingInfo, false, false,
                                             false);
    const auto *drawStencilMask = addPipeline(
        "stencil_mask", *pipelineStencilMask, stencilRendering, false, false, true);
    const auto *drawStencilContent = addPipeline("stencil_content",
                                                 *pipelineStencilContent,
                                                 stencilRendering, false, false, true);

    drawResources.device = *device;
    drawResources.queue = *GRAPHICS_AND_PRESENT;
    drawResources.queueFamily = hardwareCtx.graphicsQueueFamilyIndex;
    drawResources.frameCount = MAX_FRAMES_IN_FLIGHT;
    for (std::size_t f = 0; f < MAX_FRAMES_IN_FLIGHT; ++f)
        drawResources.frameSets[f] = {.set = descriptorSets[f],
                                      .layout = drawDescriptorLayout};

    // Stable resource IDs for the real objects used by the draws.
    constexpr std::uint64_t RID_SWAPCHAIN = 1;
    constexpr std::uint64_t RID_MSAA = 10;
    constexpr std::uint64_t RID_DEPTH = 11;
    constexpr std::uint64_t RID_PICK = 12;
    constexpr std::uint64_t RID_PICK_RESOLVE = 13;
    constexpr std::uint64_t RID_OFFSCREEN_MSAA = 14;
    constexpr std::uint64_t RID_OFFSCREEN_RESOLVE = 15;
    constexpr std::uint64_t RID_STENCIL = 16;
    constexpr std::uint64_t RID_SWAP_IMAGE = 100;
    constexpr std::uint64_t RID_SWAP_VIEW = 200;
    constexpr std::uint64_t RID_VERTEX = 1000;
    constexpr std::uint64_t RID_INDEX = 1001;
    constexpr std::uint64_t RID_HEAP = 1002;
    constexpr std::uint64_t RID_INDIRECT = 1003;
    constexpr std::uint64_t RID_CONSTANTS = 1004;
    constexpr std::uint64_t RID_TEXTURE_VIEW = 3000;
    constexpr std::uint64_t RID_SAMPLER = 4000;

    auto &resourceTable = drawResources.resources;
    {
        const intent8::DrawPipeline *drawPipelines[] = {
            drawOpaque3D,    drawTransparent3D,  drawOpaqueUI,
            drawTransparentUI, drawAdditiveUI,   drawMultiplyUI,
            drawStencilMask, drawStencilContent};
        for (std::size_t i = 0; i < std::size(drawPipelines); ++i)
            resourceTable.addPipeline(6000 + i, drawPipelines[i]->pipeline(),
                                      drawPipelines[i]->config);
    }
    resourceTable.addSwapchain(RID_SWAPCHAIN, *swapchain);
    for (std::uint32_t i = 0; i < swapchain.imagesSize(); ++i)
    {
        const auto extent = swapchain.refImageExtent();
        resourceTable.addImage(
            RID_SWAP_IMAGE + i, swapchain.image(i),
            {.format = swapchainBuild.refImageFormat(),
             .extent = {.width = extent.width,
                        .height = extent.height,
                        .depth = 1},
             .samples = VK_SAMPLE_COUNT_1_BIT,
             .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
             .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
             .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
             .name = "swapchain"});
        resourceTable.addImageView(RID_SWAP_VIEW + i, swapchain.imageView(i));
    }

    const auto makeImageConfig = [](VkFormat format, VkExtent2D extent,
                                    VkSampleCountFlagBits samples,
                                    VkImageUsageFlags usage,
                                    const char *name) {
        return intent8::ImageConfig{
            .format = format,
            .extent = {.width = extent.width, .height = extent.height, .depth = 1},
            .samples = samples,
            .usage = usage,
            .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .name = name};
    };
    {
        const auto extent = swapchain.refImageExtent();
        resourceTable.addImage(
            RID_MSAA, msaaResource.image(),
            makeImageConfig(swapchainBuild.refImageFormat(), extent, SAMPLE_COUNT,
                            VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
                                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                            "main_msaa"));
        resourceTable.addImage(
            RID_DEPTH, depthResource.image(),
            {.format = depthResourcesBuild.refCreateInfo().format,
             .extent = {.width = extent.width, .height = extent.height, .depth = 1},
             .samples = SAMPLE_COUNT,
             .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
             .aspect = VK_IMAGE_ASPECT_DEPTH_BIT,
             .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
             .name = "main_depth"});
        resourceTable.addImage(
            RID_PICK, pickResource.image(),
            makeImageConfig(VK_FORMAT_R32G32B32A32_UINT, extent, SAMPLE_COUNT,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, "pick_mrt"));
        resourceTable.addImage(
            RID_PICK_RESOLVE, resolveResource.image(),
            makeImageConfig(VK_FORMAT_R32G32B32A32_UINT, extent,
                            VK_SAMPLE_COUNT_1_BIT,
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                            "pick_resolve"));
        resourceTable.addImage(
            RID_OFFSCREEN_MSAA, offscreenMsaaResource.image(),
            makeImageConfig(swapchainBuild.refImageFormat(), extent, SAMPLE_COUNT,
                            VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
                                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                            "offscreen_msaa"));
        resourceTable.addImage(
            RID_OFFSCREEN_RESOLVE, offscreenResolveImage,
            makeImageConfig(swapchainBuild.refImageFormat(), extent,
                            VK_SAMPLE_COUNT_1_BIT,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                VK_IMAGE_USAGE_SAMPLED_BIT |
                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                            "offscreen_resolve"));
        resourceTable.addImage(
            RID_STENCIL, stencilImage,
            {.format = VK_FORMAT_D32_SFLOAT_S8_UINT,
             .extent = {.width = extent.width, .height = extent.height, .depth = 1},
             .samples = SAMPLE_COUNT,
             .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
             .aspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
             .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
             .name = "stencil"});
        resourceTable.addImageView(RID_MSAA + 100, msaaResource.imageView());
        resourceTable.addImageView(RID_DEPTH + 100, depthResource.imageView());
        resourceTable.addImageView(RID_PICK + 100, pickResource.imageView());
        resourceTable.addImageView(RID_PICK_RESOLVE + 100,
                                   resolveResource.imageView());
        resourceTable.addImageView(RID_OFFSCREEN_MSAA + 100,
                                   offscreenMsaaResource.imageView());
        resourceTable.addImageView(RID_OFFSCREEN_RESOLVE + 100,
                                   offscreenResolveView);
        resourceTable.addImageView(RID_STENCIL + 100, stencilView);
    }

    for (std::uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        resourceTable.addDescriptorSet(5000 + i, descriptorSets[i],
                                       drawDescriptorLayout);
        const auto &buffers = drawRecorder[i].buffers;
        resourceTable.addBuffer(RID_VERTEX + i * 10,
                                buffers.globalVertexBuffer.buffer.buffer(),
                                {.size = buffers.globalVertexBuffer.capacity,
                                 .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
        resourceTable.addBuffer(RID_INDEX + i * 10,
                                buffers.globalIndexBuffer.buffer.buffer(),
                                {.size = buffers.globalIndexBuffer.capacity,
                                 .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT});
        resourceTable.addBuffer(RID_HEAP + i * 10,
                                buffers.globalHeapBuffer.buffer.buffer(),
                                {.size = buffers.globalHeapBuffer.capacity,
                                 .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
        resourceTable.addBuffer(RID_INDIRECT + i * 10,
                                buffers.indirectDrawBuffer.buffer.buffer(),
                                {.size = buffers.indirectDrawBuffer.capacity,
                                 .usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT});
        resourceTable.addBuffer(RID_CONSTANTS + i * 10,
                                buffers.commandConstantsBuffer.buffer.buffer(),
                                {.size = buffers.commandConstantsBuffer.capacity,
                                 .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT});
    }

    for (const auto index : textureManager.view_used_indexes())
    {
        auto &image = textureManager.resources[index];
        resourceTable.addImageView(RID_TEXTURE_VIEW + index, image.imageView());
    }
    for (const auto index : samplerManager.view_used_indexes())
        resourceTable.addSampler(RID_SAMPLER + index,
                                 samplerManager.resources[index].data());

    drawCtx.config = &drawConfig;
    drawCtx.resources = &drawResources;
    drawCtx.extent = swapchain.refImageExtent();

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
    // intent8: PassManager owns the per-pass CPU-side buffer slots; the
    // drawRecorder owns the main pass buffers. N+1 CPU slots let the GPU still
    // read the previous frame while the next one is being built.
    std::array<intent8::PassManager, MAX_FRAMES_IN_FLIGHT> passManagers{
        intent8::PassManager{device, sizeof(Vertex) * 1000, sizeof(uint32_t) * 1000,
                               sizeof(shader_data::Shape) * 2000,
                               sizeof(VkDrawIndexedIndirectCommand) * 100,
                               sizeof(shader_data::CommandConstant) * 100},
        intent8::PassManager{device, sizeof(Vertex) * 1000, sizeof(uint32_t) * 1000,
                               sizeof(shader_data::Shape) * 2000,
                               sizeof(VkDrawIndexedIndirectCommand) * 100,
                               sizeof(shader_data::CommandConstant) * 100}};

    intent8::FrameScheduler frameScheduler{
        {.role = intent8::QueueRole::Graphics,
         .familyIndex = hardwareCtx.graphicsQueueFamilyIndex,
         .queueIndex = 0,
         .queue = &GRAPHICS_AND_PRESENT},
        {.role = intent8::QueueRole::Present,
         .familyIndex = hardwareCtx.graphicsQueueFamilyIndex,
         .queueIndex = 0,
         .queue = &GRAPHICS_AND_PRESENT}};
    std::println("[frame-scheduler] graphics_family={} present_family={}",
                 frameScheduler.graphicsBinding().familyIndex,
                 frameScheduler.presentBinding().familyIndex);

    // Verification-only readback target. The draw layer treats it as another
    // transfer resource and records CopyImageToBuffer explicitly.
    auto captureBuffer = mcs::vulkan::memory::auto_map_buffer(
        mcs::vulkan::memory::create_simple_buffer(
            device,
            {.size = CAPTURE_BYTES,
             .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
             .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
        CAPTURE_BYTES);
    static bool capturePending = false;
    static bool captureDone = false;

    auto globalCtx =
        make_aggregate_ref<"globalCtx", "device", "window", "surface", "swapchainBuild",
                           "swapchain", "frameContext", "commandPool", "commandBuffers",
                           "secondaryCommandBuffers", "queue", "frameScheduler",
                           "meshMap", "passManagers", "globalVertices",
                           "globalIndices", "drawCtx", "captureBuffer">(
            device, window, surface, swapchainBuild, swapchain, frameContext, commandPool,
            commandBuffers, secondaryCommandBuffers, GRAPHICS_AND_PRESENT,
            frameScheduler, meshMap, passManagers, allVertices, allIndices, drawCtx,
            captureBuffer);

    auto mainCtx =
        make_aggregate_ref<"mainCtx", "pipelineOpaque3D", "pipelineTransparent3D",
                           "pipelineOpaqueUI", "pipelineTransparentUI",
                           "pipelineAdditiveUI", "pipelineMultiplyUI",
                           "pipelineStencilMask", "pipelineStencilContent",
                           "pipelineLayout",
                           "depthResourcesBuild", "depthResource", "msaaResourcesBuild",
                           "msaaResource", "pipelineRenderingInfo", "stateOwnership",
                           "features", "passTargetTemplate", "transferScratch",
                           "offscreenMsaaBuild", "offscreenResolveBuild",
                           "stencilBuild", "offscreenMsaaResource",
                           "offscreenResolveView", "offscreenResolveImage",
                           "offscreenTextureIndex", "stencilResource",
                           "stencilView", "stencilImage">(
            pipelineOpaque3D, pipelineTransparent3D, pipelineOpaqueUI,
            pipelineTransparentUI, pipelineAdditiveUI, pipelineMultiplyUI,
            pipelineStencilMask, pipelineStencilContent,
            pipelineLayout, depthResourcesBuild, depthResource,
            msaaResourcesBuild, msaaResource, pipelineRenderingInfo, stateOwnership,
            features, passTargetTemplate, transferScratch, offscreenMsaaBuild,
            offscreenResolveBuild, stencilBuild, offscreenMsaaResource,
            offscreenResolveView, offscreenResolveImage, offscreenTextureIndex,
            stencilResource, stencilView, stencilImage);

    auto mainShaderCtx = make_aggregate_ref<"mainShaderCtx", "drawRecorder",
                                            "uniformBuffers", "descriptorSets">(
        drawRecorder, uniformBuffers, descriptorSets);

    // Pick up the offscreen layer texture added by initPipeline.
    descriptorSetManager.update_texture(textureManager.view_used_indexes());

    auto scene = std::make_unique<ui_new::ability_test_scene>(
        drawOpaqueUI, drawTransparentUI, drawAdditiveUI, drawMultiplyUI,
        globalCtx.meshMap["quad"], swapchain.refImageExtent(), VK_SAMPLE_COUNT_4_BIT,
        offscreenMsaaResource.imageView(), offscreenResolveView,
        offscreenMsaaResource.image(), offscreenResolveImage, offscreenTextureIndex,
        drawStencilMask, drawStencilContent, stencilView, stencilImage);
    // Stable address: the widget owns the scene, so resize can refresh the
    // attachment handles the scene captured at construction time.
    ui_new::ability_test_scene *abilityScene = scene.get();

    ui_new::ScreenWidget screen{
        ui_new::Size{WIDTH, HEIGHT},
        ui_new::Container("panel").child(
            ui_new::Container("rectBox").renderObject(std::move(scene)))};

    shader_data::hover_manager hoverManager{.screen = &screen};
    // soaCtx  ?screen ?screen  ?render_object
    auto soaCtx =
        make_aggregate_ref<"soaCtx", "screen", "textureManager",
                           "descriptorSetManager", "abilityScene">(
            screen, textureManager, descriptorSetManager, abilityScene);

    // record_info
    auto recordCtx = make_aggregate<"recordCtx", "info", "frameRecorder">(
        record_info{}, static_cast<intent8::FrameRecorder *>(nullptr));
    auto world =
        make_aggregate_ref<"world", "globalCtx", "mainCtx", "mainShaderCtx", "pickCtx",
                           "recordCtx", "hoverPool", "hoverManager", "fontCtx",
                           "window">(
            globalCtx, mainCtx, mainShaderCtx, pickCtx, recordCtx, hoverPool(),
            hoverManager, fontCtx, window);
    using world_type = decltype(world);

    using data_type = decltype(soaCtx);

    // diff: [test_dod2] start:  world     API

    // init data

    //

    //diff: [test_dod3] end

    // diff: [test_dod2] end

    static constexpr auto views_matrix_update = [](world_type &world,
                                                   const input_type &inputCtx,
                                                   data_type &soaCtx) {
        const auto &input = inputCtx.input;
        auto &camera = inputCtx.camera; //  ?GenCamera<...>

        using mcs::vulkan::event::Key;
        const float step = 0.1f;

        //  ?ViewMatrixObject  ?moveForward/moveRight  ?
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

        // ========== Ctrl+  ==========
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

            constexpr float stepDeg = 1.0f; //
            constexpr float aspectStep = 0.1f;
            auto &proj = camera.refProjection();

            // FOV   179 ?
            if (input.isKeyPressedOrRepeat(Key::eR))
                proj.adjustFovSafe(stepDeg);

            // FOV  ?0 ?
            if (input.isKeyPressedOrRepeat(Key::eF))
                proj.adjustFovSafe(-stepDeg);

            //  ?
            if (input.isKeyPressedOrRepeat(Key::eT))
                proj.adjustAspect(aspectStep);
            //   0 ?
            if (input.isKeyPressedOrRepeat(Key::eG))
                proj.adjustAspectSafe(-aspectStep);

            //   0 ?
            if (input.isKeyPressedOrRepeat(Key::eH))
                proj.adjustNearSafe(-stepDeg);
            //  ?
            if (input.isKeyPressedOrRepeat(Key::eY))
                proj.adjustNearSafe(stepDeg);
            //  ?
            if (input.isKeyPressedOrRepeat(Key::eU))
                proj.adjustFarSafe(stepDeg);
            //   near ?
            if (input.isKeyPressedOrRepeat(Key::eJ))
                proj.adjustFarSafe(-stepDeg);

            //  ?
            // std::print("fov: {} deg, aspect: {}, near: {}, far: {}\n", proj.getFov(),
            //            proj.getAspect(), proj.getNear(), proj.getFar());
        };

    // diff: [test_model_matrix3]

    static constexpr auto views_ui_camera_update = [](world_type &world,
                                                      const input_type &inputCtx,
                                                      data_type &soaCtx) noexcept {
        const auto &input = inputCtx.input;
        auto &uiCam = inputCtx.uiCamera;
        using mcs::vulkan::event::Key;
        using mcs::vulkan::event::MouseButtons;

        //  ?
        auto &swapchain = world.globalCtx.swapchain;
        auto extent = swapchain.refImageExtent();
        float w = static_cast<float>(extent.width);
        float h = static_cast<float>(extent.height);

        // ---------- Alt  ?----------
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

        //  ?match ?
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

        // ----------  lt + WASD ?----------
        constexpr float keyStep = 0.02f;
        if (input.isKeyPressedOrRepeat(Key::eW))
            exec(UiTranslateKey{0.0f, keyStep}); //
        if (input.isKeyPressedOrRepeat(Key::eS))
            exec(UiTranslateKey{0.0f, -keyStep}); //
        if (input.isKeyPressedOrRepeat(Key::eA))
            exec(UiTranslateKey{-keyStep, 0.0f}); //
        if (input.isKeyPressedOrRepeat(Key::eD))
            exec(UiTranslateKey{keyStep, 0.0f}); //

        // ----------  lt + E/Q ?----------
        constexpr float zoomFactor = 1.05f;
        if (input.isKeyPressedOrRepeat(Key::eE))
            exec(UiZoom{1.0f / zoomFactor}); //
        if (input.isKeyPressedOrRepeat(Key::eQ))
            exec(UiZoom{zoomFactor}); //

        // ----------   ----------
        //  lt + R/F  ?Z lt + T/G  ?Y lt + Y/H  ?X ?
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

        // ----------  lt +  ?----------
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
                    //  dy<0 ?=> UI   =>
                    //   UI
                    exec(UiDrag{-ndcDx, -ndcDy}); //   y  ?x  ?
                    lastCursor = {cur.xpos, cur.ypos};
                }
            }
            else
            {
                wasDragging = false;
            }
        }

        // ----------  lt + Z ?----------
        if (input.isKeyPressedOrRepeat(Key::eZ))
            exec(UiReset{});
    };

    // diff: [test_model_matrix2] end
    static constexpr auto updateVertexData = [](world_type &world, input_type &inputCtx,
                                                data_type &soaCtx,
                                                uint32_t currentFrame) noexcept {

    };

    // diff: [test_indirectdraw] start prepareBatch  // NOLINTNEXTLINE
    static constexpr auto prepareBatch = [](world_type &world, input_type &inputCtx,
                                            data_type &soaCtx, uint32_t currentFrame) {
        auto &globalCtx = world.globalCtx;
        auto &mainShaderCtx = world.mainShaderCtx;

        auto &meshMap = globalCtx.meshMap;
    };

    // diff: [test_indirectdraw] end
    // diff: [test_dod5] start
    // Main render target: config + attachment resources. The config values are
    // copied from the init-built template; the views come from the live
    // swapchain/attachment resources.
    static auto makeMainTarget = [](VkImageView color, VkImageView colorResolve,
                                    VkImageView depth, VkImageView pick,
                                    VkImageView pickResolve, VkExtent2D extent,
                                    VkAttachmentLoadOp load,
                                    VkAttachmentStoreOp store,
                                    const intent8::RenderTarget &targetTemplate) {
        intent8::RenderTarget target{};
        target.config = targetTemplate.config;
        target.config.kind = intent8::TargetKind::DynamicRendering;
        target.config.renderArea = {.offset = {.x = 0, .y = 0}, .extent = extent};
        target.config.layerCount = 1;
        target.setColor(0, intent8::color_attachment(
                               color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, load,
                               store, {.color = {.float32 = {0.0F, 0.0F, 0.0F, 1.0F}}},
                               VK_RESOLVE_MODE_AVERAGE_BIT, colorResolve,
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));
        target.setColor(1, intent8::color_attachment(
                               pick, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, load,
                               store,
                               {.color = {.uint32 = {0xFFFFFFFF, 0xFFFFFFFF, 0, 0}}},
                               VK_RESOLVE_MODE_SAMPLE_ZERO_BIT, pickResolve,
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));
        target.setDepth(intent8::depth_attachment(
            depth, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, load, store,
            {.depthStencil = {.depth = 1.0F}}));
        return target;
    };

    static constexpr auto mainPipeline = make_aggregate<"mainPipeline">(
        method<"transitionImageLayout">([](world_type &world, input_type &inputCtx,
                                           data_type &soaCtx) {
            auto &globalCtx = world.globalCtx;
            auto &mainCtx = world.mainCtx;
            auto &pickCtx = world.pickCtx;
            auto &recordCtx = world.recordCtx;

            auto &swapchain = globalCtx.swapchain;
            auto &commandBuffers = globalCtx.commandBuffers;

            const auto &[currentFrame, imageIndex] = recordCtx.info;
            const auto &commandBuffer = commandBuffers[currentFrame];
            auto &recorder = *recordCtx.frameRecorder;

            auto &depthResource = mainCtx.depthResource;
            auto &msaaResource = mainCtx.msaaResource;

            VkImage image = swapchain.image(imageIndex);
            VkImage depthImage = depthResource.image();
            VkImage msaaImage = msaaResource.image();
            VkImage pickingImage = pickCtx.pickResource.image();
            VkImage resolveImage = pickCtx.resolveResource.image();

            my_render::transition_image_layout(
                recorder,
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
            (void)soaCtx;
        }),
        method<"draw">([](world_type &world, input_type &inputCtx, data_type &soaCtx) {
            auto &globalCtx = world.globalCtx;
            auto &mainCtx = world.mainCtx;
            auto &mainShaderCtx = world.mainShaderCtx;
            auto &recordCtx = world.recordCtx;
            auto &pickCtx = world.pickCtx;
            auto &fontCtx = world.fontCtx;
            auto &drawCtx = globalCtx.drawCtx;

            auto &swapchain = globalCtx.swapchain;
            auto &secondaryCommandBuffers = globalCtx.secondaryCommandBuffers;

            const auto &[currentFrame, imageIndex] = recordCtx.info;
            auto &passManager = globalCtx.passManagers[currentFrame];

            auto &frame = mainShaderCtx.drawRecorder[currentFrame];
            auto &builder = frame.builder;
            auto &screen = soaCtx.screen;

            const intent8::DrawPipeline *basePipeline = drawCtx.pipeline("transparent_ui");
            assert(basePipeline != nullptr && basePipeline->valid());

            // uniforms are host-visible per frame
            {
                UniformBufferObject ubo;
                ubo.cameraInfo[0].view = inputCtx.camera.getViewMatrix();
                ubo.cameraInfo[0].proj = inputCtx.camera.getProjMatrix();
                ubo.cameraInfo[1].view = inputCtx.uiCamera.getViewMatrix();
                ubo.cameraInfo[1].proj = inputCtx.uiCamera.getProjMatrix();
                memcpy(mainShaderCtx.uniformBuffers[currentFrame].mapPtr(), &ubo,
                       sizeof(ubo));
            }

            // The main pass is rebuilt every frame; its UI stream and camera
            // data change per frame and this also refreshes the swapchain
            // resolve view.
            screen.makeRenderDirty(currentFrame);
            const auto extent = swapchain.refImageExtent();
            builder.begin({
                .pipeline = basePipeline,
                .pushData = {.vertexAddress = frame.buffers.globalVertexBuffer.address,
                             .instanceAddress = frame.buffers.globalHeapBuffer.address,
                             .commandConstantsAddress =
                                 frame.buffers.commandConstantsBuffer.address,
                             .cameraIndex = 1,
                             .renderVersion = screen.renderVersion()},
                .mesh = globalCtx.meshMap["quad"],
                .viewport = {.x = 0.0F,
                             .y = 0.0F,
                             .width = static_cast<float>(extent.width),
                             .height = static_cast<float>(extent.height),
                             .minDepth = 0.0F,
                             .maxDepth = 1.0F},
                .scissor = {.offset = {.x = 0, .y = 0}, .extent = extent},
            });

            intent8::RenderTarget mainTarget = makeMainTarget(
                mainCtx.msaaResource.imageView(), swapchain.imageView(imageIndex),
                mainCtx.depthResource.imageView(), pickCtx.pickResource.imageView(),
                pickCtx.resolveResource.imageView(), extent,
                VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
                mainCtx.passTargetTemplate);
            drawCtx.mainTarget = &mainTarget;

            render_context context{
                .last_pick = world.hoverManager.cur,
                .drawRecorder = builder,
                .input = inputCtx.input,
                .clock = inputCtx.clock,
                .fontSelect = fontCtx.fontSelect,
                .passManager = &passManager,
                .mainPassDesc = &mainTarget,
                .draw = &drawCtx,
                .framebufferExtent = world.window.getFramebufferSize(),
                .mainSet = mainShaderCtx.descriptorSets[currentFrame],
                .globalVertices = globalCtx.globalVertices,
                .globalIndices = globalCtx.globalIndices};
            if (std::getenv("TABLE_SELFTEST") != nullptr)
                context.last_pick = {};
            screen.recorderCmd(currentFrame, context);
            builder.end();

            // Passes are grouping only: the main target + the extra passes.
            std::vector<intent8::PassRecord> passes;
            passes.reserve(1 + passManager.size());
            passes.push_back({.target = &mainTarget,
                              .buffers = &frame.buffers,
                              .stream = builder.stream(),
                              .descriptorSet =
                                  mainShaderCtx.descriptorSets[currentFrame]});
            for (const auto &pass : passManager.passes())
            {
                passes.push_back(
                    {.target = &pass.target,
                     .buffers = &pass.buffers(),
                     .stream = pass.stream(),
                     .descriptorSet = mainShaderCtx.descriptorSets[currentFrame],
                     .preBarriers = pass.preBarriers.view(),
                     .postBarriers = pass.postBarriers.view()});
            }

            // Record the pass before the final text overlay into a secondary
            // command buffer. In the ability scene this is the offscreen
            // composite pass.
            if (passes.size() >= 2)
            {
                auto &secondaryPass = passes[passes.size() - 2];
                const auto &target = *secondaryPass.target;
                const auto &cfg = target.config;

                VkCommandBufferInheritanceRenderingInfo inheritance{
                    .sType =
                        VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO,
                    .pNext = nullptr,
                    .flags = cfg.flags &
                             ~VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT,
                    .viewMask = cfg.viewMask,
                    .colorAttachmentCount = cfg.colorCount,
                    .pColorAttachmentFormats = cfg.colorFormats.data(),
                    .depthAttachmentFormat = cfg.depthFormat,
                    .stencilAttachmentFormat = cfg.stencilFormat,
                    .rasterizationSamples = cfg.samples,
                };
                VkCommandBufferInheritanceInfo inheritanceInfo{
                    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
                    .pNext = &inheritance,
                    .renderPass = VK_NULL_HANDLE,
                    .subpass = 0,
                    .framebuffer = VK_NULL_HANDLE,
                    .occlusionQueryEnable = VK_FALSE,
                    .queryFlags = 0,
                    .pipelineStatistics = 0,
                };

                auto secondary = secondaryCommandBuffers[currentFrame];
                secondary.begin(
                    {.sType = sType<VkCommandBufferBeginInfo>(),
                     .pNext = nullptr,
                     .flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT |
                              VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                     .pInheritanceInfo = &inheritanceInfo});

                intent8::FrameRecorder secondaryRecorder{secondary, drawCtx};
                secondaryRecorder.beginInheritedPass(target);
                shader_data::emit(secondaryRecorder, *secondaryPass.buffers,
                                  secondaryPass.stream,
                                  secondaryPass.descriptorSet);
                secondaryRecorder.endPass();
                assert(secondaryRecorder.valid());
                static bool secondaryDumped = false;
                if (!secondaryDumped)
                {
                    secondaryDumped = true;
                    // The secondary body uses the same op stream and the same
                    // emitter as the primary; c_api_list is its projection.
                    const auto secondaryCalls = secondaryRecorder.c_api_list();
                    std::println("[intent8-secondary-c-api] calls={} draws={}",
                                 secondaryCalls.size(),
                                 secondaryRecorder.drawCount());
                }
                secondary.end();

                secondaryPass.executeSecondary = true;
                secondaryPass.secondary = *secondary;
            }

            auto &recorder = *recordCtx.frameRecorder;
            intent8::recordPasses(recorder, passes);
        }),
        method<"postProcess">([](world_type &world, input_type &inputCtx,
                                  data_type &soaCtx) {
            auto &globalCtx = world.globalCtx;
            auto &recordCtx = world.recordCtx;
            auto &pickCtx = world.pickCtx;
            auto &mainCtx = world.mainCtx;

            const auto &[currentFrame, imageIndex] = recordCtx.info;
            auto &recorder = *recordCtx.frameRecorder;

            auto resolveImage = pickCtx.resolveResource.image();
            auto &pickingFrames = pickCtx.frames;
            auto &pickMouse = pickCtx.mouse;
            auto &mousePos = pickMouse.pos;

            // Resource-state layer: seed the actual post-graphics layout, ask
            // the tracker for one explicit barrier, then flush it.
            intent8::ResourceStateTracker resourceState;
            resourceState.registerImage(
                {.image = resolveImage,
                 .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                 .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                 .access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                 .stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT});
            intent8::BarrierPlanner postPassBarriers;
            postPassBarriers.add(resourceState.transitionImage(
                resolveImage, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT));
            postPassBarriers.flush(recorder);

            const auto pickExtent = globalCtx.swapchain.refImageExtent();
            const int32_t pickX =
                std::clamp(static_cast<int32_t>(mousePos.x), 0,
                           std::max(0, static_cast<int32_t>(pickExtent.width) - 1));
            const int32_t pickY =
                std::clamp(static_cast<int32_t>(mousePos.y), 0,
                           std::max(0, static_cast<int32_t>(pickExtent.height) - 1));
            const std::array<VkBufferImageCopy, 1> pickRegion{VkBufferImageCopy{
                .bufferOffset = 0,
                .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                .imageOffset = {pickX, pickY, 0},
                .imageExtent = {1, 1, 1}}};
            // Dedicated transfer ops: FillBuffer -> barrier -> CopyBuffer.
            // The scratch buffers are never sampled or displayed.
            auto &scratch = mainCtx.transferScratch[currentFrame];
            recorder.fillBuffer(scratch[0].buffer(), 0, TRANSFER_SCRATCH_SIZE,
                                0xA5A5A5A5U);
            recorder.barrier(VkMemoryBarrier2{
                .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT |
                                 VK_ACCESS_2_TRANSFER_WRITE_BIT,
            });
            const std::array<VkBufferCopy, 1> scratchCopy{VkBufferCopy{
                .srcOffset = 0, .dstOffset = 0, .size = TRANSFER_SCRATCH_SIZE}};
            recorder.copyBuffer(scratch[0].buffer(), scratch[1].buffer(),
                                scratchCopy);

            // Exercise the typed transfer path. The following
            // copyImageToBuffer overwrites these bytes with the picked texel.
            recorder.fillBuffer(pickingFrames[currentFrame].buffer(), 0,
                                sizeof(picking_result), 0xFFFFFFFFU);
            recorder.copyImageToBuffer(
                resolveImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                pickingFrames[currentFrame].buffer(), pickRegion);
            (void)inputCtx;
            (void)soaCtx;
        }));

    static constexpr auto recordCommandBuffer = [](world_type &world,
                                                   input_type &inputCtx,
                                                   data_type &soaCtx) {
        auto &globalCtx = world.globalCtx;
        auto &recordCtx = world.recordCtx;
        auto &drawCtx = globalCtx.drawCtx;

        auto &swapchain = globalCtx.swapchain;
        auto &commandBuffers = globalCtx.commandBuffers;

        auto [currentFrame, imageIndex] = recordCtx.info;
        const auto &commandBuffer = commandBuffers[currentFrame];
        VkImage image = swapchain.image(imageIndex);

        drawCtx.frameIndex = currentFrame;
        drawCtx.imageIndex = imageIndex;
        drawCtx.descriptorSet = drawCtx.resources->frameSets[currentFrame].set;

        commandBuffer.begin({.sType = sType<VkCommandBufferBeginInfo>()});
        intent8::FrameRecorder recorder{commandBuffer, drawCtx};
        recordCtx.frameRecorder = &recorder;

        mainPipeline.transitionImageLayout(world, inputCtx, soaCtx);
        mainPipeline.draw(world, inputCtx, soaCtx);
        mainPipeline.postProcess(world, inputCtx, soaCtx);

        // After rendering, either copy the swapchain image back for the
        // verification capture or transition it to PRESENT_SRC. Both paths are
        // explicit ops in the same recorder.
        static int captureAttempt = 0;
        ++captureAttempt;
        static const int captureAt = [] {
            const char *value = std::getenv("INTENT8_CAPTURE_FRAME");
            return value == nullptr ? 3 : std::atoi(value);
        }();
        const char *capturePath = std::getenv("INTENT8_CAPTURE");
        const auto captureExtent = swapchain.refImageExtent();
        const bool captureFits =
            static_cast<std::uint64_t>(captureExtent.width) *
                captureExtent.height * 4U <=
            CAPTURE_BYTES;
        if (capturePath != nullptr && !captureDone && captureAttempt >= captureAt &&
            captureFits)
        {
            const VkImageMemoryBarrier2 toTransfer{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = image,
                .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                     .levelCount = 1,
                                     .layerCount = 1}};
            recorder.barrier(toTransfer);
            const auto extent = swapchain.refImageExtent();
            const std::array<VkBufferImageCopy, 1> region{VkBufferImageCopy{
                .bufferOffset = 0,
                .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                .imageOffset = {0, 0, 0},
                .imageExtent = {extent.width, extent.height, 1}}};
            recorder.copyImageToBuffer(image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                       globalCtx.captureBuffer.buffer(), region);
            const VkImageMemoryBarrier2 toPresent{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                .dstAccessMask = VK_ACCESS_2_NONE,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = image,
                .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                     .levelCount = 1,
                                     .layerCount = 1}};
            recorder.barrier(toPresent);
            capturePending = true;
        }
        else
        {
            intent8::ResourceStateTracker presentState;
            presentState.registerImage(
                {.image = image,
                 .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                 .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                 .access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                 .stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT});
            intent8::BarrierPlanner presentBarriers;
            presentBarriers.add(presentState.transitionImage(
                image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT));
            presentBarriers.flush(recorder);
        }

        // The bottom-layer contract: the recorded op list lowers to an
        // explicit ordered list of Vulkan C entry points.
        static bool dumped = false;
        if (!dumped)
        {
            dumped = true;
            assert(intent8::c_api_coverage_ok());
            assert(recorder.valid());
            std::println("[intent8-dump]\n{}", recorder.dump());

            const auto c_api_calls = recorder.c_api_list();
            assert(recorder.emittedCalls() == c_api_calls.size());
            std::println("[intent8-c-api] opcodes={} calls={} passes={} draws={}",
                         intent8::OPCODE_COUNT, c_api_calls.size(),
                         recorder.passCount(), recorder.drawCount());
            for (std::size_t i = 0; i < c_api_calls.size(); ++i)
                std::println("  #{:<3} {}", i, c_api_calls[i]);
        }

        recordCtx.frameRecorder = nullptr;
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
        auto &textureManager = soaCtx.textureManager;
        auto &descriptorSetManager = soaCtx.descriptorSetManager;

        surface.waitGoodFramebufferSize();
        device.waitIdle();

        // Release the old extent-dependent resources before allocating the new
        // ones.  At fullscreen (1920x1080) the 128-bit pick attachment is large
        // enough that a new+old overlap can exceed the device-local heap.
        const auto offscreenSlot =
            static_cast<std::uint32_t>(mainCtx.offscreenTextureIndex);
        if (textureManager.is_used(offscreenSlot))
            textureManager.free_slot(offscreenSlot);
        msaaResource = {};
        depthResource = {};
        pickResource = {};
        resolveResource = {};
        mainCtx.offscreenMsaaResource = {};
        mainCtx.stencilResource = {};
        swapchain.destroy();

        swapchain = swapchainBuild.rebuild(device, surface);
        auto newExtent = swapchain.refImageExtent();
        std::println("[resize] new extent={}x{} images={}", newExtent.width,
                     newExtent.height, swapchain.imagesSize());
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

        // The ability scene owns two resources that follow the framebuffer:
        // the offscreen MSAA color + sampled resolve and the stencil attachment.
        // They must be rebuilt with the same extent as the swapchain, otherwise
        // the offscreen/stencil pass renderArea no longer matches the views.
        mainCtx.offscreenMsaaResource =
            mainCtx.offscreenMsaaBuild.setCreateInfoExtent(imageExtent).build(device);

        auto newOffscreenResolve =
            mainCtx.offscreenResolveBuild.setCreateInfoExtent(imageExtent).build(device);
        mainCtx.offscreenResolveView = newOffscreenResolve.imageView();
        mainCtx.offscreenResolveImage = newOffscreenResolve.image();

        // Replace the sampled offscreen texture in the same descriptor slot so
        // every pass keeps the same texture index.
        textureManager.use_slot(offscreenSlot, std::move(newOffscreenResolve),
                                "offscreen_layer");
        descriptorSetManager.update_texture(textureManager.view_used_indexes());

        mainCtx.stencilResource =
            mainCtx.stencilBuild.setCreateInfoExtent(imageExtent).build(device);

        soaCtx.abilityScene->updateResources(
            mainCtx.offscreenMsaaResource.imageView(), mainCtx.offscreenResolveView,
            mainCtx.offscreenMsaaResource.image(), mainCtx.offscreenResolveImage,
            mainCtx.offscreenTextureIndex, mainCtx.stencilResource.imageView(),
            mainCtx.stencilResource.image());

        // Refresh the non-owning draw-layer views: the stable IDs stay, the
        // real handles behind them are replaced by the rebuilt resources.
        {
            auto &drawCtx = globalCtx.drawCtx;
            // The resize owner refreshes the real handles behind stable IDs;
            // the recording path itself only ever sees a const view.
            auto &table =
                const_cast<intent8::GlobalResources *>(drawCtx.resources)->resources;
            drawCtx.extent = newExtent;
            const auto swapFormat = swapchainBuild.refImageFormat();
            const auto imageConfigFor = [&](VkFormat format,
                                            VkSampleCountFlagBits samples,
                                            VkImageUsageFlags usage,
                                            const char *name) {
                return intent8::ImageConfig{
                    .format = format,
                    .extent = {.width = newExtent.width,
                               .height = newExtent.height,
                               .depth = 1},
                    .samples = samples,
                    .usage = usage,
                    .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                    .name = name};
            };
            for (std::uint32_t i = 0; i < swapchain.imagesSize(); ++i)
            {
                table.upsertImage(100 + i, swapchain.image(i),
                                  imageConfigFor(swapFormat, VK_SAMPLE_COUNT_1_BIT,
                                                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                                 "swapchain"));
                table.upsertImageView(200 + i, swapchain.imageView(i));
            }
            table.upsertImage(
                10, msaaResource.image(),
                imageConfigFor(swapFormat, SAMPLE_COUNT,
                               VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
                                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                               "main_msaa"));
            table.upsertImage(
                12, pickCtx.pickResource.image(),
                imageConfigFor(VK_FORMAT_R32G32B32A32_UINT, SAMPLE_COUNT,
                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, "pick_mrt"));
            table.upsertImage(
                13, pickCtx.resolveResource.image(),
                imageConfigFor(VK_FORMAT_R32G32B32A32_UINT, VK_SAMPLE_COUNT_1_BIT,
                               VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                               "pick_resolve"));
            table.upsertImage(
                14, mainCtx.offscreenMsaaResource.image(),
                imageConfigFor(swapFormat, SAMPLE_COUNT,
                               VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
                                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                               "offscreen_msaa"));
            table.upsertImage(
                15, mainCtx.offscreenResolveImage,
                imageConfigFor(swapFormat, VK_SAMPLE_COUNT_1_BIT,
                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                   VK_IMAGE_USAGE_SAMPLED_BIT |
                                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                               "offscreen_resolve"));
            table.upsertImage(
                16, mainCtx.stencilResource.image(),
                {.format = VK_FORMAT_D32_SFLOAT_S8_UINT,
                 .extent = {.width = newExtent.width,
                            .height = newExtent.height,
                            .depth = 1},
                 .samples = SAMPLE_COUNT,
                 .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                 .aspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
                 .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                 .name = "stencil"});
            table.upsertImage(
                11, depthResource.image(),
                {.format = depthResourcesBuild.refCreateInfo().format,
                 .extent = {.width = newExtent.width,
                            .height = newExtent.height,
                            .depth = 1},
                 .samples = SAMPLE_COUNT,
                 .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                 .aspect = VK_IMAGE_ASPECT_DEPTH_BIT,
                 .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                 .name = "main_depth"});
            table.upsertImageView(110, msaaResource.imageView());
            table.upsertImageView(111, depthResource.imageView());
            table.upsertImageView(112, pickCtx.pickResource.imageView());
            table.upsertImageView(113, pickCtx.resolveResource.imageView());
            table.upsertImageView(114, mainCtx.offscreenMsaaResource.imageView());
            table.upsertImageView(115, mainCtx.offscreenResolveView);
            table.upsertImageView(116, mainCtx.stencilResource.imageView());
            table.upsertImageView(3000 + mainCtx.offscreenTextureIndex,
                                  mainCtx.offscreenResolveView);
        }

        pickMouse.valid = false;
        frameContext =
            std::decay_t<decltype(frameContext)>(device, swapchain.imagesSize());

        // Swapchain/attachment image views changed.  The UI scene owns the
        // extra-pass PassDesc (which stores VkImageView handles), so force a
        // rebuild on both frame slots even if the logical UI size happens to
        // be unchanged.
        for (std::size_t f = 0; f < MAX_FRAMES_IN_FLIGHT; ++f)
        {
            screen.makeLayoutDirty(static_cast<std::uint32_t>(f));
            screen.makeRenderDirty(static_cast<std::uint32_t>(f));
        }
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

        //
        auto nextFrameTime =
            clk.lastTime + duration_cast<Clock::duration>(targetFrameDuration);

        //  ?
        if (now < nextFrameTime)
        {
            std::this_thread::sleep_until(nextFrameTime);
            // sleep   lastTime ?
            clk.lastTime = Clock::now();
        }
        else
        {
            //  ?lastTime  ?
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
            FrameClock::Clock::now(); //  ?static
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

        //  ?
        //   hover /  ?
        if (std::getenv("TABLE_SELFTEST") == nullptr)
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

        //  ?UI
        // inputController(world, inputCtx, soaCtx); //
    };

    // F11 fullscreen for this target.  The shared WSI reserves F1, and its
    // handler toggles on both key press and release, so the test owns a
    // single edge-triggered fullscreen switch here.
    static constexpr auto FullscreenController = [](world_type &world,
                                                    input_type &inputCtx,
                                                    data_type &) {
        using namespace mcs::vulkan::event;
        auto &input = inputCtx.input;
        static bool wasPressed = false;
        static bool fullscreen = false;
        static int windowX = 0;
        static int windowY = 0;
        static int windowW = static_cast<int>(WIDTH);
        static int windowH = static_cast<int>(HEIGHT);

        const bool pressed = input.isKeyPressed(Key::eF11);
        if (pressed && !wasPressed)
        {
            auto *handle = world.window.data();
            if (!fullscreen)
            {
                ::glfwGetWindowPos(handle, &windowX, &windowY);
                ::glfwGetWindowSize(handle, &windowW, &windowH);
                auto *monitor = ::glfwGetPrimaryMonitor();
                const GLFWvidmode *mode = ::glfwGetVideoMode(monitor);
                ::glfwSetWindowMonitor(handle, monitor, 0, 0, mode->width,
                                       mode->height, mode->refreshRate);
                fullscreen = true;
            }
            else
            {
                ::glfwSetWindowMonitor(handle, nullptr, windowX, windowY, windowW,
                                       windowH, GLFW_DONT_CARE);
                fullscreen = false;
            }
        }
        wasPressed = pressed;
    };

    //NOTE:  ?
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
                         .function = ^^decltype([](world_type &world,
                                                   input_type &inputCtx,
                                                   data_type &soaCtx) {
                             tickClock(inputCtx); //  ? ?
                             limit_frame_rate(inputCtx,
                                              60.0f); //  ?
                             InputController(world, inputCtx, soaCtx);
                             FullscreenController(world, inputCtx, soaCtx);

                             //
                             //     PI  ?
                             static int selftest_frame = 0;
                             if (std::getenv("TABLE_SELFTEST") != nullptr &&
                                 ++selftest_frame == 6)
                             {
                                 using namespace mcs::vulkan::event;
                                 auto move = [](double x, double y) {
                                     position2d_event_dispatcher::instance().distribute(
                                         {.xpos = x, .ypos = y});
                                 };
                                 auto click = [](ModifierKey mod) {
                                     mousebutton_event_dispatcher::instance().distribute(
                                         {.button = MouseButtons::eMOUSE_BUTTON_LEFT,
                                          .action = Action::ePRESS,
                                          .modifier_key = mod});
                                     mousebutton_event_dispatcher::instance().distribute(
                                         {.button = MouseButtons::eMOUSE_BUTTON_LEFT,
                                          .action = Action::eRELEASE,
                                          .modifier_key = mod});
                                 };
                                 move(82, 187);
                                 click(ModifierKey::None()); //  ?1
                                 move(82, 315);
                                 click(ModifierKey::Shift()); // 1..5
                                 move(82, 251);
                                 click(ModifierKey::Control()); //   3
                                 keyboard_event_dispatcher::instance().distribute(
                                     {.key = Key::eC,
                                      .action = Action::ePRESS,
                                      .modifier_key = ModifierKey::Control()});
                                 keyboard_event_dispatcher::instance().distribute(
                                     {.key = Key::eA,
                                      .action = Action::ePRESS,
                                      .modifier_key = ModifierKey::Control()});
                                 keyboard_event_dispatcher::instance().distribute(
                                     {.key = Key::eC,
                                      .action = Action::ePRESS,
                                      .modifier_key = ModifierKey::Control()});
                                 //  nter    ?Shift+ ?  ?X    ?Enter
                                 keyboard_event_dispatcher::instance().distribute(
                                     {.key = Key::eENTER,
                                      .action = Action::ePRESS,
                                      .modifier_key = ModifierKey::None()});
                                 keyboard_event_dispatcher::instance().distribute(
                                     {.key = Key::eLEFT_SHIFT,
                                      .action = Action::ePRESS,
                                      .modifier_key = ModifierKey::Shift()});
                                 for (int k = 0; k < 3; ++k)
                                     keyboard_event_dispatcher::instance().distribute(
                                         {.key = Key::eLEFT,
                                          .action = Action::ePRESS,
                                          .modifier_key = ModifierKey::Shift()});
                                 char_event_dispatcher::instance().distribute(
                                     {.codepoint = static_cast<std::uint32_t>('X')});
                                 keyboard_event_dispatcher::instance().distribute(
                                     {.key = Key::eENTER,
                                      .action = Action::ePRESS,
                                      .modifier_key = ModifierKey::None()});
                             }

                             if (std::getenv("TABLE_SELFTEST") != nullptr &&
                                 selftest_frame == 70)
                             {
                                 using namespace mcs::vulkan::event;
                                 keyboard_event_dispatcher::instance().distribute(
                                     {.key = Key::eF10,
                                      .action = Action::ePRESS,
                                      .modifier_key = ModifierKey::None()});
                                 keyboard_event_dispatcher::instance().distribute(
                                     {.key = Key::eLEFT_CONTROL,
                                      .action = Action::ePRESS,
                                      .modifier_key = ModifierKey::Control()});
                                 keyboard_event_dispatcher::instance().distribute(
                                     {.key = Key::eC,
                                      .action = Action::ePRESS,
                                      .modifier_key = ModifierKey::Control()});
                             }

                             //    ?100 tooltip
                             if (std::getenv("TABLE_SELFTEST") != nullptr &&
                                 selftest_frame == 100)
                             {
                                 using namespace mcs::vulkan::event;
                                 position2d_event_dispatcher::instance().distribute(
                                     {.xpos = 5.0, .ypos = 5.0});
                                 mousebutton_event_dispatcher::instance().distribute(
                                     {.button = MouseButtons::eMOUSE_BUTTON_RIGHT,
                                      .action = Action::ePRESS,
                                      .modifier_key = ModifierKey::None()});
                             }

                             if (std::getenv("TABLE_SELFTEST") != nullptr &&
                                 selftest_frame == 120)
                             {
                                 using namespace mcs::vulkan::event;
                                 position2d_event_dispatcher::instance().distribute(
                                     {.xpos = 100.0, .ypos = 120.0});
                                 mousebutton_event_dispatcher::instance().distribute(
                                     {.button = MouseButtons::eMOUSE_BUTTON_RIGHT,
                                      .action = Action::ePRESS,
                                      .modifier_key = ModifierKey::None()});
                             }

                             if (std::getenv("TABLE_SELFTEST") != nullptr &&
                                 selftest_frame == 122)
                             {
                                 using namespace mcs::vulkan::event;
                                 position2d_event_dispatcher::instance().distribute(
                                     {.xpos = 5.0, .ypos = 5.0});
                                 mousebutton_event_dispatcher::instance().distribute(
                                     {.button = MouseButtons::eMOUSE_BUTTON_LEFT,
                                      .action = Action::ePRESS,
                                      .modifier_key = ModifierKey::None()});
                                 mousebutton_event_dispatcher::instance().distribute(
                                     {.button = MouseButtons::eMOUSE_BUTTON_LEFT,
                                      .action = Action::eRELEASE,
                                      .modifier_key = ModifierKey::None()});
                             }

                             //    ?8  1  ?278 ?378
                             if (std::getenv("TABLE_SELFTEST") != nullptr &&
                                 selftest_frame == 8)
                             {
                                 using namespace mcs::vulkan::event;
                                 auto move = [](double x, double y) {
                                     position2d_event_dispatcher::instance().distribute(
                                         {.xpos = x, .ypos = y});
                                 };
                                 auto btn = [](Action a) {
                                     mousebutton_event_dispatcher::instance().distribute(
                                         {.button = MouseButtons::eMOUSE_BUTTON_LEFT,
                                          .action = a,
                                          .modifier_key = ModifierKey::None()});
                                 };
                                 move(278, 120);
                                 btn(Action::ePRESS);
                                 move(328, 120);
                                 move(378, 120);
                                 btn(Action::eRELEASE);
                             }

                             //    ?10 tatus
                             if (std::getenv("TABLE_SELFTEST") != nullptr &&
                                 selftest_frame == 10)
                             {
                                 using namespace mcs::vulkan::event;
                                 auto move = [](double x, double y) {
                                     position2d_event_dispatcher::instance().distribute(
                                         {.xpos = x, .ypos = y});
                                 };
                                 auto btn = [](Action a) {
                                     mousebutton_event_dispatcher::instance().distribute(
                                         {.button = MouseButtons::eMOUSE_BUTTON_LEFT,
                                          .action = a,
                                          .modifier_key = ModifierKey::None()});
                                 };
                                 move(424, 120);
                                 btn(Action::ePRESS);
                                 move(220, 120);
                                 move(120, 120);
                                 btn(Action::eRELEASE);
                             }

                             if (std::getenv("TABLE_SELFTEST") != nullptr &&
                                 selftest_frame == 12)
                             {
                                 using namespace mcs::vulkan::event;
                                 position2d_event_dispatcher::instance().distribute(
                                     {.xpos = 100.0, .ypos = 120.0});
                                 mousebutton_event_dispatcher::instance().distribute(
                                     {.button = MouseButtons::eMOUSE_BUTTON_RIGHT,
                                      .action = Action::ePRESS,
                                      .modifier_key = ModifierKey::None()});
                                 position2d_event_dispatcher::instance().distribute(
                                     {.xpos = 140.0, .ypos = 143.0});
                                 mousebutton_event_dispatcher::instance().distribute(
                                     {.button = MouseButtons::eMOUSE_BUTTON_LEFT,
                                      .action = Action::ePRESS,
                                      .modifier_key = ModifierKey::None()});
                                 mousebutton_event_dispatcher::instance().distribute(
                                     {.button = MouseButtons::eMOUSE_BUTTON_LEFT,
                                      .action = Action::eRELEASE,
                                      .modifier_key = ModifierKey::None()});
                             }

                             if (std::getenv("TABLE_SELFTEST") != nullptr &&
                                 selftest_frame == 14)
                             {
                                 using namespace mcs::vulkan::event;
                                 keyboard_event_dispatcher::instance().distribute(
                                     {.key = Key::eLEFT_SHIFT,
                                      .action = Action::ePRESS,
                                      .modifier_key = ModifierKey::Shift()});
                                 scroll_event_dispatcher::instance().distribute(
                                     {.xoffset = 0.0, .yoffset = -5.0});
                             }

                             //    ?16  Reset sort
                             if (std::getenv("TABLE_SELFTEST") != nullptr &&
                                 selftest_frame == 16)
                             {
                                 using namespace mcs::vulkan::event;
                                 position2d_event_dispatcher::instance().distribute(
                                     {.xpos = 100.0, .ypos = 120.0});
                                 mousebutton_event_dispatcher::instance().distribute(
                                     {.button = MouseButtons::eMOUSE_BUTTON_RIGHT,
                                      .action = Action::ePRESS,
                                      .modifier_key = ModifierKey::None()});
                                 position2d_event_dispatcher::instance().distribute(
                                     {.xpos = 140.0, .ypos = 169.0});
                                 mousebutton_event_dispatcher::instance().distribute(
                                     {.button = MouseButtons::eMOUSE_BUTTON_LEFT,
                                      .action = Action::ePRESS,
                                      .modifier_key = ModifierKey::None()});
                                 mousebutton_event_dispatcher::instance().distribute(
                                     {.button = MouseButtons::eMOUSE_BUTTON_LEFT,
                                      .action = Action::eRELEASE,
                                      .modifier_key = ModifierKey::None()});
                             }

                             if (std::getenv("TABLE_SELFTEST") != nullptr &&
                                 selftest_frame == 18)
                             {
                                 using namespace mcs::vulkan::event;
                                 position2d_event_dispatcher::instance().distribute(
                                     {.xpos = 100.0, .ypos = 120.0});
                                 mousebutton_event_dispatcher::instance().distribute(
                                     {.button = MouseButtons::eMOUSE_BUTTON_RIGHT,
                                      .action = Action::ePRESS,
                                      .modifier_key = ModifierKey::None()});
                                 position2d_event_dispatcher::instance().distribute(
                                     {.xpos = 140.0, .ypos = 191.0});
                                 mousebutton_event_dispatcher::instance().distribute(
                                     {.button = MouseButtons::eMOUSE_BUTTON_LEFT,
                                      .action = Action::ePRESS,
                                      .modifier_key = ModifierKey::None()});
                                 mousebutton_event_dispatcher::instance().distribute(
                                     {.button = MouseButtons::eMOUSE_BUTTON_LEFT,
                                      .action = Action::eRELEASE,
                                      .modifier_key = ModifierKey::None()});
                             }

                             if (std::getenv("TABLE_SELFTEST") != nullptr &&
                                 selftest_frame == 20)
                             {
                                 using namespace mcs::vulkan::event;
                                 position2d_event_dispatcher::instance().distribute(
                                     {.xpos = 100.0, .ypos = 120.0});
                                 mousebutton_event_dispatcher::instance().distribute(
                                     {.button = MouseButtons::eMOUSE_BUTTON_RIGHT,
                                      .action = Action::ePRESS,
                                      .modifier_key = ModifierKey::None()});
                                 position2d_event_dispatcher::instance().distribute(
                                     {.xpos = 140.0, .ypos = 221.0});
                                 mousebutton_event_dispatcher::instance().distribute(
                                     {.button = MouseButtons::eMOUSE_BUTTON_LEFT,
                                      .action = Action::ePRESS,
                                      .modifier_key = ModifierKey::None()});
                                 mousebutton_event_dispatcher::instance().distribute(
                                     {.button = MouseButtons::eMOUSE_BUTTON_LEFT,
                                      .action = Action::eRELEASE,
                                      .modifier_key = ModifierKey::None()});
                             }

                             if (std::getenv("TABLE_SELFTEST") != nullptr &&
                                 selftest_frame == 22)
                             {
                                 using namespace mcs::vulkan::event;
                                 position2d_event_dispatcher::instance().distribute(
                                     {.xpos = 354.0, .ypos = 219.0});
                             }

                             if (std::getenv("TABLE_SELFTEST") != nullptr &&
                                 selftest_frame == 60)
                             {
                                 using namespace mcs::vulkan::event;
                                 position2d_event_dispatcher::instance().distribute(
                                     {.xpos = 500.0, .ypos = 300.0});
                             }

                             if (std::getenv("TABLE_SELFTEST") != nullptr &&
                                 selftest_frame == 62)
                             {
                                 using namespace mcs::vulkan::event;
                                 keyboard_event_dispatcher::instance().distribute(
                                     {.key = Key::eLEFT_CONTROL,
                                      .action = Action::ePRESS,
                                      .modifier_key = ModifierKey::Control()});
                                 keyboard_event_dispatcher::instance().distribute(
                                     {.key = Key::eEND,
                                      .action = Action::ePRESS,
                                      .modifier_key = ModifierKey::Control()});
                             }
                             views_matrix_update(world, inputCtx, soaCtx);
                             soaCtx.screen.tick(
                                 static_cast<double>(app::now_ms().count()));
                             views_perspective_update(world, inputCtx, soaCtx);
                             views_ui_camera_update(world, inputCtx, soaCtx);
                             // model_update
                             FrameRateMonitor(world, inputCtx,
                                              soaCtx);     //  ? ?
                             inputCtx.input.scroll() = {}; //  ?
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

                         //   inFlightFences[currentFrame]  N-2
                         //   pickingFrames[currentFrame]
                         //  ?N-1  PU   DMA ?
                         //  ?GPU   0xFF..FF ?
                         //   0xFFFF  ?
                         {
                             uint32_t readIdx = currentFrame;
                             //diff: [test_dod19] start:   hover_manager ?
                             auto *data = static_cast<picking_result *>(
                                 pickingFrames[readIdx].mapPtr());
                             picking_result r = *data;
                             //  ? ? ?  picking
                             if (r.render_version() != soaCtx.screen.renderVersion())
                             {
                                 world.hoverManager.resetPicking();
                                 return;
                             }

                             if (!pickMouse.valid)
                                 r.key.object_type =
                                     0xFFFFFFFF; //   =   leave ?

                             // [PICKDBG]   GPU  ?
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
                                 // update  ?
                                 // updateVertexData  ?
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
        auto &frameScheduler = globalCtx.frameScheduler;

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

        std::uint32_t imageIndex = 0;
        auto result = frameScheduler.acquire(
            device, *swapchain, presentCompleteSemaphore[semaphoreIndex],
            imageIndex);
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
        frameScheduler.submitGraphics(
            {.sType = sType<VkSubmitInfo>(),
             .waitSemaphoreCount = 1,
             .pWaitSemaphores = &presentCompleteSemaphore[semaphoreIndex],
             .pWaitDstStageMask = waitDestinationStageMask,
             .commandBufferCount = 1,
             .pCommandBuffers = &*commandBuffer,
             .signalSemaphoreCount = 1,
             .pSignalSemaphores = &renderFinishedSemaphore[imageIndex]},
            inFlightFences[currentFrame]);

        result = frameScheduler.present(
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

        if (capturePending)
        {
            const char *path = std::getenv("INTENT8_CAPTURE");
            if (path != nullptr)
            {
                device.waitIdle();
                const auto extent = swapchain.refImageExtent();
                const auto *src =
                    static_cast<const std::uint8_t *>(globalCtx.captureBuffer.mapPtr());
                const std::size_t pixelCount =
                    static_cast<std::size_t>(extent.width) * extent.height;
                std::vector<std::uint8_t> rgb(pixelCount * 3);
                for (std::size_t i = 0; i < pixelCount; ++i)
                {
                    // Swapchain format is B8G8R8A8_SRGB.
                    rgb[i * 3 + 0] = src[i * 4 + 2];
                    rgb[i * 3 + 1] = src[i * 4 + 1];
                    rgb[i * 3 + 2] = src[i * 4 + 0];
                }
                std::ofstream out(path, std::ios::binary);
                out << "P6\n" << extent.width << " " << extent.height << "\n255\n";
                out.write(reinterpret_cast<const char *>(rgb.data()),
                          static_cast<std::streamsize>(rgb.size()));
                out.close();
                std::println("[intent8-capture] wrote {} {}x{}", path,
                             extent.width, extent.height);
                captureDone = true;
                std::exit(0);
            }
            capturePending = false;
        }

        semaphoreIndex = (semaphoreIndex + 1) % presentCompleteSemaphore.size();
        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
        frameScheduler.advanceFrame();

        static std::uint32_t frameCount = 0;
        ++frameCount;
        if (const char *limit = std::getenv("INTENT8_FRAMES");
            limit != nullptr && frameCount >= static_cast<std::uint32_t>(std::atoi(limit)))
            std::exit(0);
    };
    while (globalCtx.window.shouldClose() == 0)
    {
        task_graph.invoke_ranges<"input_start", "input_end">(world, inputCtx, soaCtx);
        drawFrame(world, inputCtx, soaCtx);
    }
    device.waitIdle();
    // The draw layer owns the shader modules it created for the config view.
    drawConfig.shaders.destroy(device);

    std::cout << "main done\n";
    return 0;
}
catch (std::exception &e)
{
    std::println("main catch exception: {}", e.what());
}
