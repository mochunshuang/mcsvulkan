#include <array>
#include <cassert>
#include <codecvt>
#include <optional>
#include <set>
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
#include <msdfgen.h>
#include <msdfgen-ext.h>
// diff: [test_texture2.cpp] end

// diff: [test_texture3.cpp] start
#include <nlohmann/json.hpp>
using json = nlohmann::json;
// diff: [test_texture3.cpp] end

// NOTE: 布局添加的头文件
// diff: [test_msdf_atlas_gen3] start
#include <ft2build.h>
#include FT_FREETYPE_H

#include <hb.h>
#include <hb-ft.h>
#include <raqm.h>                //NOTE: 不需要,因为和换行不搭
#include <SheenBidi/SheenBidi.h> //diff: [test_bidi] bidi
#include <linebreak.h>           // libunibreak 核心头文件
#include <unibreakdef.h>         // 基础定义
// diff: [test_msdf_atlas_gen3] end

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
    struct freetype_version
    {
        int major;
        int minor;
        int patch;
    };

    struct freetype_loader
    {
        constexpr operator bool() const noexcept // NOLINT
        {
            return value_ != nullptr;
        }
        constexpr auto &operator*() noexcept
        {
            return value_;
        }
        constexpr const auto &operator*() const noexcept
        {
            return value_;
        }
        [[nodiscard]] auto version() const noexcept
        {
            freetype_version ver{};
            ::FT_Library_Version(value_, &ver.major, &ver.minor, &ver.patch);
            return ver;
        }
        constexpr freetype_loader()
        {
            // https://freetype.org/freetype2/docs/reference/ft2-library_setup.html#ft_init_freetype
            auto error = FT_Init_FreeType(&value_);
            if (error != FT_Err_Ok)
                throw std::runtime_error{
                    "an error occurred during FT_Library initialization"};
            auto ver = version();
            std::println("freetype_loader: init library: {} freetype_version: {}.{}.{}",
                         static_cast<void *>(value_), ver.major, ver.minor, ver.patch);
        }
        freetype_loader(const freetype_loader &) = delete;
        constexpr freetype_loader(freetype_loader &&o) noexcept
            : value_{std::exchange(o.value_, {})}
        {
        }
        freetype_loader &operator=(const freetype_loader &) = delete;
        freetype_loader &operator=(freetype_loader &&o) noexcept
        {
            if (&o != this)
            {
                destroy();
                value_ = std::exchange(o.value_, {});
            }
            return *this;
        }
        constexpr void destroy() noexcept
        {
            if (value_ != nullptr)
            {
                FT_Done_FreeType(value_);
                value_ = nullptr;
            }
        }
        constexpr ~freetype_loader() noexcept
        {
            destroy();
        }

      private:
        FT_Library value_{};
    };
    // 3. Loading a Font Face
    struct freetype_face
    {
        constexpr operator bool() const noexcept // NOLINT
        {
            return value_ != nullptr;
        }
        constexpr auto &operator*() noexcept
        {
            return value_;
        }
        constexpr const auto &operator*() const noexcept
        {
            return value_;
        }
        freetype_face(const freetype_face &) = delete;
        freetype_face(freetype_face &&o) noexcept : value_{std::exchange(o.value_, {})} {}
        freetype_face &operator=(const freetype_face &) = delete;
        freetype_face &operator=(freetype_face &&o) noexcept
        {
            if (&o != this)
            {
                destroy();
                value_ = std::exchange(o.value_, {});
            }
            return *this;
        }
        // a. From a Font File
        freetype_face(FT_Library library, const std::string &fontPath,
                      FT_Long face_index = 0)
        {
            /*
            face_index:
            某些字体格式允许在单个文件中嵌入多个字体。此索引告诉您要加载哪个面。
                         如果其值太大，则返回错误。不过，索引0总是有效的。
            face_: 指向设置为描述新face对象的句柄的指针。如果出错，它被设置为NULL
            */
            auto error = FT_New_Face(library, fontPath.data(), face_index, &value_);
            if (error == FT_Err_Unknown_File_Format)
                throw std::runtime_error{"the font file could be opened and read, but it "
                                         "appears that its font format is unsupported"};
            if (error != FT_Err_Ok)
                throw std::runtime_error{
                    "another error code means that the font file could "
                    "not be opened or read, or that it is broken"};
        }
        // b. From Memory
        // NOTE: 请注意，在调用FT_Done_Face之前，您不能释放字体文件缓冲区。
        freetype_face(FT_Library library, const std::span<const FT_Byte> &bufferView,
                      FT_Long face_index = 0)
        {
            auto error = FT_New_Memory_Face(
                library, bufferView.data(),              /* first byte in memory */
                static_cast<FT_Long>(bufferView.size()), /* size in bytes */
                face_index,                              /* face_index           */
                &value_);
            if (error != FT_Err_Ok)
                throw std::runtime_error{"init FT_Face From Memory error."};
        }
        // c. From Other Sources (Compressed Files, Network, etc.)

        constexpr void destroy() noexcept
        {
            if (value_ != nullptr)
            {
                FT_Done_Face(value_);
                value_ = nullptr;
            }
        }
        constexpr ~freetype_face() noexcept
        {
            destroy();
        }

      private:
        FT_Face value_{}; /* handle to face object */
    };

    /*
     glyph.getQuadAtlasBounds(l, b, r, t);
                if (l || b || r || t) {
                    switch (metrics.yDirection) {
                        case msdfgen::Y_UPWARD:
                            fprintf(f,
     ",\"atlasBounds\":{\"left\":%.17g,\"bottom\":%.17g,\"right\":%.17g,\"top\":%.17g}",
     l, b, r, t); break; case msdfgen::Y_DOWNWARD: fprintf(f,
     ",\"atlasBounds\":{\"left\":%.17g,\"top\":%.17g,\"right\":%.17g,\"bottom\":%.17g}",
     l, metrics.height-t, r, metrics.height-b); break;
                    }
                }
    */
    // msdf-atlas-gen\json-export.cpp
    class Font
    {
        using json = nlohmann::json;

        class Atlas // NOLINTBEGIN
        {
          public:
            // "\"type\":\"%s\","
            std::string type;

            // "\"distanceRange\":%.17g," (only present for SDF/PSDF/MSDF/MTSDF)
            std::optional<double> distanceRange;

            // "\"distanceRangeMiddle\":%.17g," (only present for SDF/PSDF/MSDF/MTSDF)
            std::optional<double> distanceRangeMiddle;

            // "\"size\":%.17g,"
            double size = 0.0;

            // "\"width\":%d,"
            int width = 0;

            // "\"height\":%d,"
            int height = 0;

            // "\"yOrigin\":\"%s\""
            std::string yOrigin;

            // Optional grid object
            struct Grid
            {
                // "\"cellWidth\":%d,"
                int cellWidth = 0;

                // "\"cellHeight\":%d,"
                int cellHeight = 0;

                // "\"columns\":%d,"
                int columns = 0;

                // "\"rows\":%d"
                int rows = 0;

                // Optional "\"originX\":%.17g"
                std::optional<double> originX;

                // Optional "\"originY\":%.17g"
                std::optional<double> originY;
            };
            std::optional<Grid> grid;

            static Atlas make(const json &obj)
            {
                Atlas ret{};
                const auto &atlas = obj["atlas"];
                ret.type = atlas["type"];

                constexpr auto hasDistance =
                    [](std::string_view type) constexpr noexcept {
                        return type == "sdf" || type == "psdf" || type == "msdf" ||
                               type == "mtsdf";
                    };
                if (hasDistance(ret.type))
                {
                    ret.distanceRange = atlas["distanceRange"];
                    ret.distanceRangeMiddle = atlas["distanceRangeMiddle"];
                }
                ret.size = atlas["size"];
                ret.width = atlas["width"];
                ret.height = atlas["height"];
                ret.yOrigin = atlas["yOrigin"];
                if (atlas.contains("grid"))
                {
                    const auto &obj = atlas["grid"];
                    Grid grid{};
                    grid.cellWidth = obj["cellWidth"];
                    grid.cellHeight = obj["cellHeight"];
                    grid.columns = obj["columns"];
                    grid.rows = obj["rows"];
                    if (obj.contains("originX"))
                        grid.originX = obj["originX"];
                    if (obj.contains("originY"))
                        grid.originX = obj["originY"];
                }
                return ret;
            }
        };
        class Metrics
        {
          public:
            // "\"emSize\":%.17g,"
            double emSize = 0.0;

            // "\"lineHeight\":%.17g,"
            double lineHeight = 0.0;

            // "\"ascender\":%.17g,"
            double ascender = 0.0;

            // "\"descender\":%.17g,"
            double descender = 0.0;

            // "\"underlineY\":%.17g,"
            double underlineY = 0.0;

            // "\"underlineThickness\":%.17g"
            double underlineThickness = 0.0;

            static Metrics make(const json &obj)
            {
                const auto &metrics = obj["metrics"];
                return {.emSize = metrics["emSize"],
                        .lineHeight = metrics["lineHeight"],
                        .ascender = metrics["ascender"],
                        .descender = metrics["descender"],
                        .underlineY = metrics["underlineY"],
                        .underlineThickness = metrics["underlineThickness"]};
            }
        };

        // NOTE: 只考虑unicode码点的类型
        class Glyphs
        {
          public:
            class Bounds
            {
              public:
                //%.17g 使用double
                double left, bottom, right, top;
            };
            using PlaneBounds = Bounds;
            using AtlasBounds = Bounds;
            /*
             switch (font.getPreferredIdentifierType()) {
                            case GlyphIdentifierType::GLYPH_INDEX:
                                fprintf(f, "\"index\":%d,", glyph.getIndex());
                                break;
                            case GlyphIdentifierType::UNICODE_CODEPOINT:
                                fprintf(f, "\"unicode\":%u,", glyph.getCodepoint());
                                break;
                        }
            */
            using GLYPH_INDEX = signed int;
            using UNICODE_CODEPOINT = unsigned int;
            using IdentifierType = std::variant<GLYPH_INDEX, UNICODE_CODEPOINT>;
            // "\"unicode\":%u,"
            IdentifierType index_or_unicode;
            static_assert(!std::is_same_v<char32_t, uint32_t>);
            static_assert(sizeof(char32_t) == sizeof(uint32_t));

            double advance;
            std::optional<PlaneBounds> planeBounds;
            std::optional<AtlasBounds> atlasBounds;

            static std::vector<Glyphs> make(const json &obj)
            {
                std::vector<Glyphs> ret{};
                const auto &glyphs = obj["glyphs"];
                for (const auto &glyph : glyphs)
                {
                    // NOTE: BUG BUG
                    //  NOTE: 拆分就没事太离谱l
                    //   IdentifierType id = glyph.contains("index")
                    //                           ? GLYPH_INDEX{glyph["index"]}
                    //                           : UNICODE_CODEPOINT{glyph["unicode"]};
                    IdentifierType id;
                    if (glyph.contains("index"))
                        id = GLYPH_INDEX{glyph["index"]};
                    else
                        id = UNICODE_CODEPOINT{glyph["unicode"]};

                    constexpr auto makebounds = [](const auto &bound) -> Bounds {
                        return {.left = bound["left"],
                                .bottom = bound["bottom"],
                                .right = bound["right"],
                                .top = bound["top"]};
                    };
                    // NOTE: 比如 32 即 空格字符，就没有Bounds
                    const auto parseBounds =
                        [&](const char *key) noexcept -> std::optional<Bounds> {
                        const auto it = glyph.find(key);
                        if (it == glyph.end() || it->is_null())
                            return std::nullopt;
                        const auto &b = *it;
                        return Bounds{.left = b["left"],
                                      .bottom = b["bottom"],
                                      .right = b["right"],
                                      .top = b["top"]};
                    };
                    ret.emplace_back(Glyphs{.index_or_unicode = std::move(id),
                                            .advance = glyph["advance"],
                                            .planeBounds = parseBounds("planeBounds"),
                                            .atlasBounds = parseBounds("atlasBounds")});
                }
                ret.shrink_to_fit();
                return ret;
            }
        };

        // NOTE: 仅仅考虑unicode ，不做运行时检查
        class Kerning
        {
          public:
            /*
            fprintf(f, "\"index1\":%d,", kernPair.first.first);
                                    fprintf(f, "\"index2\":%d,", kernPair.first.second);
                                    fprintf(f, "\"advance\":%.17g", kernPair.second);
            */
            struct GLYPH_INDEX
            {
                signed int index1;
                signed int index2;
                double advance;
            };
            //"\"unicode2\":%u,"
            struct UNICODE_CODEPOINT
            {
                char32_t unicode1;
                char32_t unicode2;
                double advance;
            };

            // using GLYPH_INDEX = signed int;
            // using UNICODE_CODEPOINT = unsigned int;

            using kerning_data = std::variant<GLYPH_INDEX, UNICODE_CODEPOINT>;
            kerning_data value;

            static std::vector<Kerning> make(const json &obj)
            {
                std::vector<Kerning> ret{};
                const auto it = obj.find("kerning");
                if (it == obj.end() || it->is_null())
                    return ret;
                const auto &kernings = *it;
                for (const auto &kerning : kernings)
                {
                    if (kerning.contains("index1"))
                        ret.emplace_back(
                            Kerning{.value = GLYPH_INDEX{.index1 = kerning["index1"],
                                                         .index2 = kerning["index2"],
                                                         .advance = kerning["advance"]}});
                    else
                        ret.emplace_back(Kerning{
                            .value = UNICODE_CODEPOINT{.unicode1 = kerning["unicode1"],
                                                       .unicode2 = kerning["unicode2"],
                                                       .advance = kerning["advance"]}});
                }
                ret.shrink_to_fit();
                return ret;
            }
        };

      public:
        using Bounds = Glyphs::Bounds;
        using atlas_type = Atlas;
        using metrics_type = Metrics;
        using glyphs_type = Glyphs;
        using kerning_type = Kerning;
        Atlas atlas{};
        Metrics metrics{};
        std::vector<Glyphs> glyphs;
        std::vector<Kerning> kerning;

        static Font make(const std::string &jsonPath)
        {
            json data = json::parse(std::ifstream(jsonPath));
            if (data.contains("variants"))
                throw std::runtime_error{".......[TODO] unsuported now."};
            return {.atlas = Atlas::make(data),
                    .metrics = Metrics::make(data),
                    .glyphs = Glyphs::make(data),
                    .kerning = Kerning::make(data)};
        }
    }; // NOLINTEND

    class FontTexture
    {
        texture_image_base texture_;

      public:
        struct msdf_info // NOLINTBEGIN
        {
            raw_stbi_image png;
            VmaAllocator allocator;
            const LogicalDevice &device;
            const CommandPool &pool;
            const Queue &queue;
        }; // NOLINTEND
        explicit FontTexture(const msdf_info &info)
        {
            const auto &[raw_data, allocator, device, pool, queue] = info;
            auto width = raw_data.width();
            auto height = raw_data.height();
            uint32_t mipLevels = 1; // 字体无需 mipmap
            auto create_texture =
                create_texture_image{allocator, pool, queue}.setEnableMipmapping(false);
            create_image build =
                create_image{device, allocator}
                    .setCreateInfo(
                        {.imageType = VK_IMAGE_TYPE_2D,
                         .format = VK_FORMAT_R8G8B8A8_UNORM,
                         .extent = {static_cast<uint32_t>(width),
                                    static_cast<uint32_t>(height), 1},
                         .mipLevels = mipLevels,
                         .arrayLayers = 1,
                         .samples = VK_SAMPLE_COUNT_1_BIT,
                         .tiling = VK_IMAGE_TILING_OPTIMAL,
                         .usage =
                             VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                         .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                         .allocationCreateInfo =
                             {.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
                              .usage = VMA_MEMORY_USAGE_AUTO}})
                    .setViewCreateInfo(
                        {.viewType = VK_IMAGE_VIEW_TYPE_2D,
                         .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                              .baseMipLevel = 0,
                                              .levelCount = mipLevels,
                                              .baseArrayLayer = 0,
                                              .layerCount = 1}});
            texture_ = create_texture.build(
                build, std::span<const unsigned char>{raw_data.data(), raw_data.size()});
        }
        [[nodiscard]] const texture_image_base &view() const noexcept
        {
            return texture_;
        }
    };

    enum class FontType : uint8_t
    {
        eHARD_MASK,
        eSOFT_MASK,
        eSDF,
        ePSDF,
        eMSDF,
        eMTSDF,
        eBITMAP,
    };

    // diff: [test_harfbuzz] start
    struct harfbuzz_font
    {
        constexpr operator bool() const noexcept // NOLINT
        {
            return value_ != nullptr;
        }
        constexpr auto &operator*() noexcept
        {
            return value_;
        }
        constexpr const auto &operator*() const noexcept
        {
            return value_;
        }

        constexpr harfbuzz_font(const harfbuzz_font &) = delete;
        constexpr harfbuzz_font &operator=(const harfbuzz_font &) = delete;

        harfbuzz_font() = default;
        // 从 FT_Face 创建 hb_font_t
        constexpr explicit harfbuzz_font(FT_Face ft_face)
            : value_(hb_ft_font_create(ft_face, nullptr))
        {
            if (value_ == nullptr)
                throw std::runtime_error("hb_ft_font_create failed");
        }

        // 移动构造函数
        constexpr harfbuzz_font(harfbuzz_font &&o) noexcept
            : value_{std::exchange(o.value_, {})}
        {
        }

        // 移动赋值
        constexpr harfbuzz_font &operator=(harfbuzz_font &&o) noexcept
        {
            if (this != &o)
            {
                destroy();
                value_ = std::exchange(o.value_, {});
            }
            return *this;
        }

        constexpr void destroy() noexcept
        {
            if (value_ != nullptr)
            {
                hb_font_destroy(value_);
                value_ = nullptr;
            }
        }
        constexpr ~harfbuzz_font() noexcept
        {
            destroy();
        }

      private:
        hb_font_t *value_{};
    };
    // diff: [test_harfbuzz] end

    struct texture_bind_sampler
    {
        uint32_t texture_index;
        uint32_t sampler_index;
    };

    class FontAtlas
    {
      public:
        std::string name;
        Font font;
        FontTexture texture;
        freetype_face face;

        FontType type;
        uint32_t texture_index;
        uint32_t sampler_index;

        // diff: [test_bidi] start
        std::unordered_map<FT_UInt, const Font::glyphs_type *>
            glyph_index_to_glyphs; // 加速 HarfBuzz
        std::unordered_map<uint32_t, const Font::glyphs_type *>
            unicode_default_glyphs; // 字体回退
        // diff: [test_bidi] end

        harfbuzz_font hb_font; // diff: [test_harfbuzz]

        FontAtlas(const std::string &jsonPath, const FontTexture::msdf_info &info,
                  freetype_face &&face, FontType type, texture_bind_sampler bind)
            : name(jsonPath), font(Font::make(jsonPath)), texture(info),
              face(std::move(face)), type(type), texture_index(bind.texture_index),
              sampler_index(bind.sampler_index)
        {
            FT_Face raw_face = *this->face; // 获取原始 FT_Face
            FT_Set_Pixel_Sizes(raw_face, 0,
                               static_cast<FT_UInt>(font.atlas.size)); // 设置像素大小

            hb_font = harfbuzz_font{raw_face}; // 创建 HarfBuzz 字体
            hb_ft_font_changed(*hb_font);

            // 1. 填充 glyph_index_to_info
            for (const auto &glyph : font.glyphs)
            {
                if (glyph.index_or_unicode.index() == 0)
                {
                    auto glyph_index =
                        static_cast<FT_UInt>(std::get<0>(glyph.index_or_unicode));
                    assert(!glyph_index_to_glyphs.contains(glyph_index));
                    glyph_index_to_glyphs[glyph_index] = &glyph;
                }
                else
                {
                    char32_t codepoint = std::get<1>(glyph.index_or_unicode);
                    FT_UInt glyph_index = FT_Get_Char_Index(raw_face, codepoint);
                    if (glyph_index != 0)
                    {
                        assert(!glyph_index_to_glyphs.contains(glyph_index));
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
                auto it = glyph_index_to_glyphs.find(static_cast<uint32_t>(gindex));
                if (it != glyph_index_to_glyphs.end())
                {
                    assert(!unicode_default_glyphs.contains(
                        static_cast<char32_t>(charcode)));
                    unicode_default_glyphs[static_cast<char32_t>(charcode)] = it->second;
                }
                charcode = FT_Get_Next_Char(raw_face, charcode, &gindex);
            }
        }
        double getEmUnits() const
        {
            return static_cast<double>((*face)->units_per_EM);
        }
    };
    struct glyph_info // NOLINTBEGIN
    {
        using Bounds = Font::Bounds;
        Bounds uv_bounds; // 归一化 UV
        const FontAtlas *font_ctx;
        const Font::glyphs_type *glyph;

        auto &type() const noexcept
        {
            return font_ctx->type;
        }
        auto &texture_index() const noexcept
        {
            return font_ctx->texture_index;
        }
        auto &sampler_index() const noexcept
        {
            return font_ctx->sampler_index;
        }
        auto &font() const noexcept
        {
            return font_ctx->font;
        }
        Bounds plane_bounds() const noexcept
        {
            return glyph->planeBounds.value_or(Bounds{0, 0, 0, 0});
        }
        double advance() const noexcept
        {
            return glyph->advance;
        }

        static glyph_info make(const Font::glyphs_type &glyph,
                               const FontAtlas &atlas) noexcept
        {
            auto atlasBounds = glyph.atlasBounds;
            if (!atlasBounds)
                // 空格等
                return {.uv_bounds = {.left = 0, .bottom = 0, .right = 0, .top = 0},
                        .font_ctx = &atlas,
                        .glyph = &glyph};

            auto &font = atlas.font;
            double w = font.atlas.width;
            double h = font.atlas.height;
            if (atlas.font.atlas.yOrigin == "bottom")
            {
                // Y_UPWARD: 需要翻转
                auto left = atlasBounds->left / w;
                auto bottom = (h - atlasBounds->bottom) / h;
                auto right = atlasBounds->right / w;
                auto top = (h - atlasBounds->top) / h;
                return {.uv_bounds = {.left = left,
                                      .bottom = bottom,
                                      .right = right,
                                      .top = top},
                        .font_ctx = &atlas,
                        .glyph = &glyph};
            }

            // "top"
            // Y_DOWNWARD: 无需翻转，直接使用原始 bounds（已交换）
            auto left = atlasBounds->left / w;
            auto bottom = atlasBounds->bottom / h;
            auto right = atlasBounds->right / w;
            auto top = atlasBounds->top / h;
            return {
                .uv_bounds = {.left = left, .bottom = bottom, .right = right, .top = top},
                .font_ctx = &atlas,
                .glyph = &glyph};
        }

    }; // NOLINTEND

    class FontLibrary
    {

        std::vector<FontAtlas> fonts_;
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
                  freetype_face face)
        {
            // 先构造一个完整的 FontAtlas 对象（但此时 hb_font 还未初始化）
            fonts_.emplace_back(jsonPath, info, std::move(face), type, bind);

            auto &newFont = fonts_.back();

            // 计算当前字体的分数：分数越大优先级越高。这里使用索引的逆序，先加载的字体分数高。
            int score = static_cast<int>(
                fonts_.size() -
                1); // 当前字体分数为 1（最小），之前字体分数大于 1，因此先加载优先。

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
            return fonts_ |
                   std::views::transform(
                       [](const FontAtlas &atlas) noexcept -> const texture_image_base & {
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

    // diff: [Uniform] start
    // diff: [texture] start
    // diff: [test_bidi] 改成4.
    constexpr int TEXTURE_COUNT = 4; // diff: [test_texture2] msdf：测试纹理+着色器
    constexpr int SAMPLER_COUNT = 2; // 创建2个不同的采样器
    // diff: [texture] end
    auto descriptorSetLayout =
        create_descriptor_set_layout{}
            .setCreateInfo(
                {.bindings =
                     {
                         {.binding = 0,
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
    const std::string TEXTURE_PATH_0 = pre + "/tirobangla_ascii.png";
    const std::string JSON_PATH_0 = pre + "/tirobangla_ascii.json";
    const std::string FONT_PATH_0 = pre + "/tirobangla_ascii.ttf";
    // constexpr auto CHAR = U'g'; // NOTE: 注意有参数耦合着色器
    // constexpr auto CHAR = U' '; //NOTE: 空格就是什么都看不到

    const std::string TEXTURE_PATH_1 = pre + "/msyh_chinese.png";
    const std::string JSON_PATH_1 = pre + "/msyh_chinese.json";
    const std::string FONT_PATH_1 = pre + "/msyh_chinese.ttc";
    // constexpr auto CHAR = U'是'; // NOTE: 注意有参数耦合着色器

    // diff: [test_emoji] start
    const std::string TEXTURE_PATH_2 = pre + "/emoji.png";
    const std::string JSON_PATH_2 = pre + "/emoji.json";
    const std::string FONT_PATH_2 = pre + "/emoji.ttf";
    // diff: [test_emoji] end

    // diff: [test_bidi] start
    // const std::string TEXTURE_PATH_3 = pre + "/segoe_arabic.png";
    // const std::string JSON_PATH_3 = pre + "/segoe_arabic.json";
    // const std::string FONT_PATH_3 = pre + "/segoe_arabic.ttf";
    // const std::string FONT_PATH_3 = "C:/Windows/Fonts/segoeui.ttf";

    // diff: [test_harfbuzz] BUG BUG. 会导致无法加载font
    // NOTE: 要想解决
    // const std::string TEXTURE_PATH_3 = pre + "/segoe_arabic_new.png";
    // const std::string JSON_PATH_3 = pre + "/segoe_arabic_new.json";
    const std::string TEXTURE_PATH_3 = pre + "/segoe_arabic_all.png";
    const std::string JSON_PATH_3 = pre + "/segoe_arabic_all.json";
    const std::string FONT_PATH_3 = "C:/Windows/Fonts/segoeui.ttf";
    /*
NOTE: segoe_arabic_new 并没有解决问题。
PS E:\mysoftware\msdf-atlas-gen> .\msdf-atlas-gen.exe -font "C:\Windows\Fonts\segoeui.ttf"
-chars "[0x0000,0x9FFF]" -type msdf -format png -imageout segoe_arabic_new.png -json
segoe_arabic_new.json -size 64 -pxrange 2

U+0048 U+0065 U+006C U+006C U+006F U+0020 U+0633
U+0644 U+0627 U+0645 U+7684 U+1F605 Warning: glyph index 159 not found in font atlas,
using default glyph for character U+627 Warning: glyph index 169 not found in font atlas,
using default glyph for character U+644 Warning: glyph index 15 not found in font atlas,
using default glyph for character U+633

NOTE: 引入新警告。。。
Warning: glyph index 3941 not found in font atlas, using default glyph for character U+627
Warning: glyph index 3981 not found in font atlas, using default glyph for character U+644
Warning: glyph index 2260 not found in font atlas, using default glyph for character U+633

NOTE: 新的API。 也无济于事。纹理还变化了. 结果 ？： 变成 if-else 就没问题。离谱
./msdf-atlas-gen.exe -font "C:\Windows\Fonts\segoeui.ttf" -allglyphs -type msdf -format
png -imageout segoe_arabic_all.png -json segoe_arabic_all.json -size 64 -pxrange 2

U+0048 U+0065 U+006C U+006C U+006F U+0020 U+0633 U+0644 U+0627 U+0645 U+7684 U+1F605
Warning: glyph index 3941 not found in font atlas, using default glyph for character U+627
Warning: glyph index 3981 not found in font atlas, using default glyph for character U+644
Warning: glyph index 2260 not found in font atlas, using default glyph for character U+633
*/

    // diff: [test_bidi] end

    // 添加字体
    font::freetype_loader loader{};
    {
        FT_Face test_face;
        FT_New_Face(*loader, "C:/Windows/Fonts/segoeui.ttf", 0, &test_face);
        // 查三个字符：U+0627 (ا), U+0644 (ل), U+0633 (س)
        FT_UInt idx1 = FT_Get_Char_Index(test_face, 0x0627);
        FT_UInt idx2 = FT_Get_Char_Index(test_face, 0x0644);
        FT_UInt idx3 = FT_Get_Char_Index(test_face, 0x0633);
        std::cout << "glyph index for U+0627: " << idx1 << std::endl;
        std::cout << "glyph index for U+0644: " << idx2 << std::endl;
        std::cout << "glyph index for U+0633: " << idx3 << std::endl;
        FT_Done_Face(test_face);
    }
    font::FontLibrary font_lib{};
    font_lib.load(
        font::FontType::eMSDF,
        {.texture_index = 0, .sampler_index = 0}, // textureIndex=0, samplerIndex=0
        {.png = raw_stbi_image{TEXTURE_PATH_0.data(), STBI_rgb_alpha},
         .allocator = allocator,
         .device = device,
         .pool = commandPool,
         .queue = GRAPHICS_AND_PRESENT},
        JSON_PATH_0, font::freetype_face{*loader, FONT_PATH_0});

    font_lib.load(
        font::FontType::eMSDF,
        {.texture_index = 1, .sampler_index = 0}, // textureIndex=2, samplerIndex=0
        {.png = raw_stbi_image{TEXTURE_PATH_1.data(), STBI_rgb_alpha},
         .allocator = allocator,
         .device = device,
         .pool = commandPool,
         .queue = GRAPHICS_AND_PRESENT},
        JSON_PATH_1, font::freetype_face{*loader, FONT_PATH_1});

    // diff: [test_emoji] 使用 eBITMAP
    font_lib.load(
        font::FontType::eBITMAP,
        {.texture_index = 2, .sampler_index = 0}, // textureIndex=2, samplerIndex=0
        {.png = raw_stbi_image{TEXTURE_PATH_2.data(), STBI_rgb_alpha},
         .allocator = allocator,
         .device = device,
         .pool = commandPool,
         .queue = GRAPHICS_AND_PRESENT},
        JSON_PATH_2, font::freetype_face{*loader, FONT_PATH_2});

    // diff: [test_bidi] 添加阿拉伯字体
    auto image = raw_stbi_image{TEXTURE_PATH_3.data(), STBI_rgb_alpha};
    font_lib.load(font::FontType::eMSDF, {.texture_index = 3, .sampler_index = 0},
                  {.png = std::move(image),
                   .allocator = allocator,
                   .device = device,
                   .pool = commandPool,
                   .queue = GRAPHICS_AND_PRESENT},
                  JSON_PATH_3, font::freetype_face{*loader, FONT_PATH_3});

    auto textures = font_lib.getTextureImageBases();
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

    /*
    NOTE: 简单说：双向处理和整形要在换行之前，但最后一步的视觉重排要在换行之后
    数据流大致如下：理论正确流程：双向分析 → 整形（逻辑顺序）→ 换行 → 重排 → 渲染。
    [Unicode 文本段落]
        ↓
    1️⃣ 双向分析 (BiDi Analysis) ← SheenBidi/FriBiDi
    • 分析整段文本的书写方向
    • 为每个字符计算嵌入层级 (embedding levels)
    • 输出：带层级信息的逻辑文本
        ↓
    2️⃣ 整形 (Shaping) ← HarfBuzz
    • 将字符转换为字形 (glyphs)
    • 应用连字、位置调整等规则
    • 输出：字形序列（仍按逻辑顺序）
        ↓
    3️⃣ 测量与换行 (Line Breaking) ← libunibreak
    • 按逻辑顺序累加每个字形的宽度
    • 根据宽度限制确定断点位置
    • 输出：分行后的字形序列（逻辑顺序）
        ↓
    4️⃣ 行内重排 (Line Reordering) ← SheenBidi
    • 对每一行应用规则 L1-L4
    • 将逻辑顺序转换为视觉顺序
    • 输出：每行已重排的字形（准备渲染）
        ↓
    5️⃣ 渲染 (Rendering) ← Vulkan + 你的代码

    关键点：
        换行是在 逻辑顺序 下用 塑形后 的字形宽度来决定的
        如果先换行再做双向处理，混合方向文本的宽度计算会完全错误
        最后的重排（视觉顺序）必须在换行之后，因为重排是逐行进行的

    所以你在代码里的正确做法是：
        用 SheenBidi 分析整段文本 → 得到每个字符的嵌入层级
        用 HarfBuzz 对整段整形 → 得到字形和宽度
        用 libunibreak 确定断点 → 分行（用逻辑顺序的宽度）
        对每一行调用 SheenBidi 的 reorder_line 进行视觉重排
        逐行生成顶点数据 → Vulkan 渲染

    */
    /*
    SheenBidi
    并不直接修改你的顶点数据，而是通过输出一个关键的中间产物——嵌入层级（embedding level）
    来影响整个后续流程 。
        偶数值层级：表示该字符是从左到右（LTR）的，例如英文和数字。
        奇数值层级：表示该字符是从右到左（RTL）的，例如阿拉伯语和希伯来语字母
    NOTE: BIDI: BIDI（双向算法）的任务是：决定一段混合了 LTR 和 RTL
    文字的文本，每个字符在屏幕上应该从左到右还是从右到左显示。
    它输出的是视觉顺序，即字符渲染时的前后次序。
    */
    // Vectex
    auto printu32string = [](const std::u32string &text) {
        for (char32_t ch : text)
        {
            std::cout << "U+" << std::hex << std::uppercase << std::setw(4)
                      << std::setfill('0') << static_cast<uint32_t>(ch) << " ";
            // optionally print the character itself if convertible
        }
        std::cout << '\n';
    };
    {
        // NOTE: 从表情向左删除。第一个删除的是"م"。说明"م"就是在数组的尾部
        //  std::u32string text = U"Hello سلام的😅";
        // U+0048 U+0065 U+006C U+006C U+006F U+0020 U+0633 U+0644 U+0627 U+0645 U+7684
        // U+1F605
        // NOTE: 下面能证明 逻辑输入顺序。 阿拉伯字符确实视觉和输入相反的
        static_assert(U'😅' == 0x1F605, "U+1F605 expected for '😅'");
        static_assert(U'的' == 0x7684, "U+7684 expected for '的'");
        static_assert(U'م' == 0x0645, "U+0645 expected for 'م'");
        static_assert(U'م' == 0x0645U, "U+0645 expected for 'م'"); // 或加上 U 后缀

        static_assert(U'س' == 0x0633, "U+0645 expected for 'س'");

        static_assert(U' ' == 0x0020, "U+0020 expected for ' '");
    }
    std::u32string text = U"Hello سلام的😅"; // diff: [test_bidi]
    printu32string(text);

    /*
    整形（Shaping） 是文本渲染中的一个关键步骤，它的核心任务是：将一串
    Unicode字符（逻辑顺序）转换为字体中对应的具体字形，
    并计算出每个字形应该如何摆放，以便最终正确呈现文字的视觉形态。
    HarfBuzz 本身只负责文本整形——将 Unicode
    字符序列转换为字体字形（glyph）序列，并计算出每个字形的位置偏移（offset）和前进量（advance）。它输出的结果是一系列逻辑顺序的字形
    ID 和位置信息，完全不涉及图形 API 的顶点缓冲区或渲染命令。
    */
#if 0
    // diff: test_bidi2 start
    // NOTE: 应该和控制台渲染一样才算OK
    std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> conv;
    std::string utf8_text = conv.to_bytes(text);
    std::cout << "UTF-8 text: " << utf8_text << " (bytes: " << utf8_text.size() << ")\n";

    // ----- 步骤1：UTF-32 双向分析（SheenBidi）-----
    // 使用 UTF-32 编码，长度传递代码单元个数（即 text.size()）
    SBCodepointSequence codepointSequence = {
        SBStringEncodingUTF32, (void *)text.data(),
        static_cast<SBUInteger>(text.size()) // 注意：这里是 text.size()，不是乘以 sizeof
    };

    SBAlgorithmRef bidiAlgorithm = SBAlgorithmCreate(&codepointSequence);
    SBParagraphRef firstParagraph =
        SBAlgorithmCreateParagraph(bidiAlgorithm, 0, INT32_MAX, SBLevelDefaultLTR);
    SBUInteger paragraphLength = SBParagraphGetLength(firstParagraph);

    SBLineRef paragraphLine = SBParagraphCreateLine(firstParagraph, 0, paragraphLength);
    SBUInteger runCount = SBLineGetRunCount(paragraphLine);
    const SBRun *runArray = SBLineGetRunsPtr(paragraphLine);

    // 保存 runs 信息（视觉顺序），offset/length 已是码点索引/个数
    std::vector<SBRun> runs(runArray, runArray + runCount);

    // 打印 runs 验证（应该与 UTF-8 分析结果一致）
    for (const auto &run : runs)
    {
        std::println("Run Offset: {}", (long)run.offset);
        std::println("Run Length: {}", (long)run.length);
        std::println("Run Level: {}\n", (long)run.level);
    }

    // 释放 SheenBidi 对象
    SBLineRelease(paragraphLine);
    SBParagraphRelease(firstParagraph);
    SBAlgorithmRelease(bidiAlgorithm);
    // ----- 步骤2：预计算每个逻辑字符的图形信息 -----
    struct CharInfo
    {
        font::glyph_info glyph;
        double distanceRange;
        uint32_t modulateFlag;
    };
    std::vector<CharInfo> charInfos;
    charInfos.reserve(text.size());

    bool modulateFlag = false;
    for (char32_t ch : text)
    {
        modulateFlag = !modulateFlag;
        auto font = font_lib.getFont(ch);
        assert(font.has_value());
        auto info = (*font).getDefaultGlyph(ch, font_lib);
        double distanceRange = info.font().atlas.distanceRange.value_or(0);
        charInfos.push_back({info, distanceRange, static_cast<uint32_t>(modulateFlag)});
    }

    // ----- 步骤3：按视觉顺序生成顶点数据 -----
    const float fontSize = 0.2f;
    float cursorX = 0.0f, cursorY = 0.0f; // 当前绘制位置（原点）
    std::vector<mesh_base> objects;       // 新的对象容器

    auto createMeshForChar = [&](const CharInfo &ci, float originX, float originY) {
        std::vector<Vertex> tmp_vertices;
        std::vector<uint32_t> tmp_idxs;
        tmp_vertices.reserve(4);
        tmp_idxs.reserve(6);

        const auto &info = ci.glyph;
        const auto pos = info.plane_bounds();

        float left = originX + static_cast<float>(pos.left) * fontSize;
        float bottom = originY + static_cast<float>(pos.bottom) * fontSize;
        float right = originX + static_cast<float>(pos.right) * fontSize;
        float top = originY + static_cast<float>(pos.top) * fontSize;

        glm::vec3 pos_left_bottom(left, bottom, 0.0f);
        glm::vec3 pos_right_bottom(right, bottom, 0.0f);
        glm::vec3 pos_right_top(right, top, 0.0f);
        glm::vec3 pos_left_top(left, top, 0.0f);

        glm::vec2 uv_left_bottom(info.uv_bounds.left, info.uv_bounds.bottom);
        glm::vec2 uv_right_bottom(info.uv_bounds.right, info.uv_bounds.bottom);
        glm::vec2 uv_right_top(info.uv_bounds.right, info.uv_bounds.top);
        glm::vec2 uv_left_top(info.uv_bounds.left, info.uv_bounds.top);

        tmp_vertices.push_back(Vertex{.pos = pos_left_bottom,
                                      .color = {1.0f, 0.0f, 0.0f},
                                      .texCoord = uv_left_bottom,
                                      .textureIndex = info.texture_index(),
                                      .samplerIndex = info.sampler_index(),
                                      .fontType = static_cast<uint32_t>(info.type()),
                                      .pxRange = static_cast<float>(ci.distanceRange),
                                      .modulateFlag = ci.modulateFlag});
        tmp_vertices.push_back(Vertex{.pos = pos_right_bottom,
                                      .color = {0.0f, 1.0f, 0.0f},
                                      .texCoord = uv_right_bottom,
                                      .textureIndex = info.texture_index(),
                                      .samplerIndex = info.sampler_index(),
                                      .fontType = static_cast<uint32_t>(info.type()),
                                      .pxRange = static_cast<float>(ci.distanceRange),
                                      .modulateFlag = ci.modulateFlag});
        tmp_vertices.push_back(Vertex{.pos = pos_right_top,
                                      .color = {0.0f, 0.0f, 1.0f},
                                      .texCoord = uv_right_top,
                                      .textureIndex = info.texture_index(),
                                      .samplerIndex = info.sampler_index(),
                                      .fontType = static_cast<uint32_t>(info.type()),
                                      .pxRange = static_cast<float>(ci.distanceRange),
                                      .modulateFlag = ci.modulateFlag});
        tmp_vertices.push_back(Vertex{.pos = pos_left_top,
                                      .color = {1.0f, 0.0f, 0.0f},
                                      .texCoord = uv_left_top,
                                      .textureIndex = info.texture_index(),
                                      .samplerIndex = info.sampler_index(),
                                      .fontType = static_cast<uint32_t>(info.type()),
                                      .pxRange = static_cast<float>(ci.distanceRange),
                                      .modulateFlag = ci.modulateFlag});

        uint32_t base = 0;
        tmp_idxs.insert(tmp_idxs.end(),
                        {base, base + 1, base + 2, base, base + 2, base + 3});

        objects.emplace_back(mesh_base{allocator,
                                       commandPool,
                                       GRAPHICS_AND_PRESENT,
                                       {std::move(tmp_vertices), std::move(tmp_idxs)}});
    };

    // 遍历视觉顺序的 runs
    for (const auto &run : runs)
    {
        uint32_t start = static_cast<uint32_t>(run.offset);
        uint32_t length = static_cast<uint32_t>(run.length);
        bool isRTL = (run.level % 2) == 1;

        if (isRTL)
        {
            // 预先计算每个字符的缩放后宽度，避免重复乘法
            std::vector<float> scaledAdvances(length);
            float runWidth = 0.0f;
            for (uint32_t i = 0; i < length; ++i)
            {
                float adv =
                    static_cast<float>(charInfos[start + i].glyph.advance()) * fontSize;
                scaledAdvances[i] = adv;
                runWidth += adv;
            }

            // run 的右边界 = 当前光标位置 + 总宽度
            float runRight = cursorX + runWidth;
            float runCursor = runRight; // 从右边界开始放置

            // 按逻辑顺序放置字符，原点依次向左移动
            for (uint32_t i = 0; i < length; ++i)
            {
                const auto &ci = charInfos[start + i];
                float originX = runCursor - scaledAdvances[i];
                createMeshForChar(ci, originX, cursorY);
                runCursor = originX; // 光标左移
            }

            // 更新全局光标到 run 的右边界，供后续 run 使用
            cursorX = runRight;
        }
        else // LTR run
        {
            // 从左向右放置字符
            for (uint32_t i = start; i < start + length; ++i)
            {
                const auto &ci = charInfos[i];
                createMeshForChar(ci, cursorX, cursorY);
                cursorX += static_cast<float>(ci.glyph.advance()) * fontSize;
            }
        }
    }
    // objects 已填充完毕，后续代码保持不变

    // diff: [test_bidi2] end
#else
    // diff: [test_harfbuzz] start
    // 步骤1 的输出：双向分析结果
    struct BidiResult
    {
        std::u32string text;           // 原始文本
        std::vector<SBLevel> levels;   // 每个字符的嵌入层级
        std::vector<SBRun> visualRuns; // 视觉顺序的 run（用于重排）
        // 可选：保存 SBLineRef 等，但为简化，这里只保存 runs
    };

    // 步骤2 的输出：逻辑顺序的字形
    struct ShapedGlyph
    {
        uint32_t glyphIndex;
        const font::FontAtlas *fontAtlas;
        double advance; // 像素单位（已除以64）
        double offsetX;
        double offsetY;
        size_t charIndex;                      // 对应字符在 text 中的索引
        font::glyph_info::Bounds uv_bounds;    // 归一化 UV
        font::glyph_info::Bounds plane_bounds; // 平面坐标边界
        bool isRTL;                            // 是否为从右向左运行
    };
    using LogicalGlyphs = std::vector<ShapedGlyph>;

    // 步骤3 的输出：视觉顺序的字形（只需指针，避免拷贝）
    using VisualGlyphs = std::vector<const ShapedGlyph *>;

    // 步骤4 的输出：最终生成的网格对象
    using RenderMeshes = std::vector<mesh_base>;

    // 步骤1：双向分析
    auto bidi_analyze = [](const std::u32string &text) -> BidiResult {
        SBCodepointSequence seq{SBStringEncodingUTF32, (void *)text.data(), text.size()};
        SBAlgorithmRef algo = SBAlgorithmCreate(&seq);
        SBParagraphRef para =
            SBAlgorithmCreateParagraph(algo, 0, INT32_MAX, SBLevelDefaultLTR);
        SBUInteger len = SBParagraphGetLength(para);

        // 保存每个字符的嵌入层级
        const SBLevel *levelsPtr = SBParagraphGetLevelsPtr(para);
        std::vector<SBLevel> levels(levelsPtr, levelsPtr + len);

        // 创建一行（整段作为一行）
        SBLineRef line = SBParagraphCreateLine(para, 0, len);
        SBUInteger runCount = SBLineGetRunCount(line);
        const SBRun *runsPtr = SBLineGetRunsPtr(line);
        std::vector<SBRun> visualRuns(runsPtr, runsPtr + runCount);

        // 释放 SheenBidi 对象（注意：line 需要稍后释放，但 runs 数据已经拷贝）
        SBLineRelease(line);
        SBParagraphRelease(para);
        SBAlgorithmRelease(algo);

        return {std::move(text), std::move(levels), std::move(visualRuns)};
    };

    // 步骤2：HarfBuzz 整形（逻辑顺序）
    auto shape_with_harfbuzz = [](const BidiResult &bidi,
                                  const font::FontLibrary &fontLib) -> LogicalGlyphs {
        LogicalGlyphs glyphs;
        const auto &text = bidi.text;
        const auto &levels = bidi.levels;
        const size_t n = text.size();

        // ----- 1. 预计算每个字符的默认字形信息（用于分组和回退）-----
        std::vector<font::glyph_info> charDefaultInfo;
        charDefaultInfo.reserve(n);
        for (size_t i = 0; i < n; ++i)
        {
            auto font = fontLib.getFont(text[i]);
            assert(font.has_value());
            charDefaultInfo.push_back((*font).getDefaultGlyph(text[i], fontLib));
        }

        // ----- 2. 分组：生成逻辑运行（run）区间 -----
        struct Run
        {
            size_t start, end;
            const font::FontAtlas *font;
            bool isRTL;
        };
        std::vector<Run> runs;
        size_t i = 0;
        while (i < n)
        {
            const auto *currentFont = charDefaultInfo[i].font_ctx;
            bool isRTL = (levels[i] % 2) == 1;
            size_t j = i + 1;
            while (j < n && charDefaultInfo[j].font_ctx == currentFont &&
                   (levels[j] % 2) == isRTL)
                ++j;
            runs.push_back({i, j, currentFont, isRTL});
            i = j;
        }

        // ----- 3. 对每个 run 进行 HarfBuzz 整形 -----
        for (const auto &run : runs)
        {
            hb_buffer_t *buf = hb_buffer_create();
            hb_buffer_add_utf32(
                buf, reinterpret_cast<const uint32_t *>(text.data()) + run.start,
                static_cast<int>(run.end - run.start), 0,
                static_cast<int>(run.end - run.start));
            hb_buffer_guess_segment_properties(buf);
            hb_buffer_set_direction(buf, run.isRTL ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
            hb_shape(*run.font->hb_font, buf, nullptr, 0);

            unsigned count;
            hb_glyph_info_t *info = hb_buffer_get_glyph_infos(buf, &count);
            hb_glyph_position_t *pos = hb_buffer_get_glyph_positions(buf, &count);

            // 🔁 关键：使用字体的真实设计单位大小（如 2048）
            double emUnits = run.font->getEmUnits(); // 通过 face->units_per_EM 获取
            std::print("emUnits: {}\n", emUnits);

            for (unsigned k = 0; k < count; ++k)
            {
                size_t charIdx = run.start + info[k].cluster;

                // HarfBuzz 输出 26.6 定点数，除以 64 得设计单位
                double designOffsetX = static_cast<double>(pos[k].x_offset) / 64.0;
                double designOffsetY = static_cast<double>(pos[k].y_offset) / 64.0;

                // 归一化：设计单位 → 相对于 1em 的比例
                double normalizedOffsetX = designOffsetX / emUnits;
                double normalizedOffsetY = designOffsetY / emUnits;

                // 查找字形元数据（与之前相同）
                auto it = run.font->glyph_index_to_glyphs.find(info[k].codepoint);
                // 在找到字形索引或回退后，advance 统一使用默认值：
                // 在找到字形索引或回退后，advance 统一使用默认值
                if (it == run.font->glyph_index_to_glyphs.end())
                {
                    const auto &glyph = charDefaultInfo[charIdx];
                    glyphs.push_back(
                        {.glyphIndex = info[k].codepoint,
                         .fontAtlas = run.font,
                         .advance = glyph.advance(), // 使用 JSON 中的 advance
                         .offsetX = normalizedOffsetX,
                         .offsetY = normalizedOffsetY,
                         .charIndex = charIdx,
                         .uv_bounds = glyph.uv_bounds,
                         .plane_bounds = glyph.plane_bounds(),
                         .isRTL = run.isRTL});
                }
                else
                {
                    const auto *raw_glyph = it->second;
                    auto glyph = font::glyph_info::make(*raw_glyph, *run.font);
                    glyphs.push_back(
                        {.glyphIndex = info[k].codepoint,
                         .fontAtlas = run.font,
                         .advance = glyph.advance(), // 使用 JSON 中的 advance
                         .offsetX = normalizedOffsetX,
                         .offsetY = normalizedOffsetY,
                         .charIndex = charIdx,
                         .uv_bounds = glyph.uv_bounds,
                         .plane_bounds = glyph.plane_bounds(),
                         .isRTL = run.isRTL});
                }
            }
            hb_buffer_destroy(buf);
        }
        return glyphs;
    };

    // 步骤3：视觉重排
    auto reorder_visually = [](const BidiResult &bidi,
                               LogicalGlyphs &logicalGlyphs) -> VisualGlyphs {
        std::unordered_map<size_t, std::vector<const ShapedGlyph *>> charToGlyphs;
        for (const auto &g : logicalGlyphs)
        {
            charToGlyphs[g.charIndex].push_back(&g);
        }

        VisualGlyphs visual;
        for (auto &run : bidi.visualRuns)
        {
            bool isRTL = (run.level % 2) == 1;
            if (isRTL)
            {
                // RTL 运行，视觉从左到右 = 逻辑逆序
                for (size_t offset = run.length; offset > 0; --offset)
                {
                    size_t ch = run.offset + offset - 1;
                    if (auto it = charToGlyphs.find(ch); it != charToGlyphs.end())
                    {
                        for (auto *g : it->second)
                        {
                            visual.push_back(g);
                        }
                    }
                }
            }
            else
            {
                // LTR 运行，视觉从左到右 = 逻辑顺序
                for (size_t ch = run.offset; ch < run.offset + run.length; ++ch)
                {
                    if (auto it = charToGlyphs.find(ch); it != charToGlyphs.end())
                    {
                        for (auto *g : it->second)
                        {
                            visual.push_back(g);
                        }
                    }
                }
            }
        }
        return visual;
    };
    // 步骤4：生成网格 - 修复偏移量问题
    auto generate_meshes = [&](const VisualGlyphs &visual, VmaAllocator allocator,
                               const CommandPool &pool, const Queue &queue,
                               float fontSize) -> RenderMeshes {
        RenderMeshes objects;
        float cursorX = 0.0f, cursorY = 0.0f; // 当前绘制位置（基线原点）

        struct Rect
        {
            float left, right, bottom, top;
        };
        std::optional<Rect> prevRect; // 上一个字形的矩形

        for (size_t idx = 0; idx < visual.size(); ++idx)
        {
            const auto *g = visual[idx];

            // 计算字形原点（基线 + HarfBuzz 偏移）
            float originX = cursorX + static_cast<float>(g->offsetX * fontSize);
            float originY = cursorY + static_cast<float>(g->offsetY * fontSize);

            // 根据平面边界计算四边形的四个角
            float left = originX + static_cast<float>(g->plane_bounds.left * fontSize);
            float bottom =
                originY + static_cast<float>(g->plane_bounds.bottom * fontSize);
            float right = originX + static_cast<float>(g->plane_bounds.right * fontSize);
            float top = originY + static_cast<float>(g->plane_bounds.top * fontSize);

            // 检查与上一个矩形的重叠
            if (prevRect.has_value())
            {
                float overlapX = std::max(0.0f, std::min(right, prevRect->right) -
                                                    std::max(left, prevRect->left));
                float overlapY = std::max(0.0f, std::min(top, prevRect->top) -
                                                    std::max(bottom, prevRect->bottom));
                if (overlapX > 0.0f && overlapY > 0.0f)
                {
                    float width = right - left;
                    float prevWidth = prevRect->right - prevRect->left;
                    float overlapRatio = overlapX / std::min(width, prevWidth);
                    if (overlapRatio > 0.03f)
                    { // 阈值可调
                        std::println("Warning: Excessive overlap detected between glyphs "
                                     "{} and {}: {:.2f}%",
                                     idx - 1, idx, overlapRatio * 100.0f);
                        // 可打印更多信息用于调试
                    }
                }
            }
            if (true)
            {
                std::println(
                    "Glyph {}: charIndex={}, advance={}, offsetX={}, offsetY={}, "
                    "planeBounds=[{},{},{},{}], uvBounds=[{},{},{},{}], width={}",
                    idx, g->charIndex, g->advance, g->offsetX, g->offsetY,
                    g->plane_bounds.left, g->plane_bounds.right, g->plane_bounds.bottom,
                    g->plane_bounds.top, //
                    g->uv_bounds.left, g->uv_bounds.right, g->uv_bounds.bottom,
                    g->uv_bounds.top, g->plane_bounds.right - g->plane_bounds.left);
            }

            // 记录当前矩形供下一次使用
            prevRect = Rect{left, right, bottom, top};

            // UV 坐标（已归一化）
            glm::vec2 uv_lb(g->uv_bounds.left, g->uv_bounds.bottom);
            glm::vec2 uv_rb(g->uv_bounds.right, g->uv_bounds.bottom);
            glm::vec2 uv_rt(g->uv_bounds.right, g->uv_bounds.top);
            glm::vec2 uv_lt(g->uv_bounds.left, g->uv_bounds.top);

            // 从字体图集中获取固定属性
            uint32_t textureIdx = g->fontAtlas->texture_index;
            uint32_t samplerIdx = g->fontAtlas->sampler_index;
            uint32_t fontType = static_cast<uint32_t>(g->fontAtlas->type);
            float pxRange =
                static_cast<float>(g->fontAtlas->font.atlas.distanceRange.value_or(0.0));

            // 创建四个顶点（颜色可任意指定，这里沿用原有的红/绿/蓝/红顺序）
            std::vector<Vertex> vertices(4);
            vertices[0] = {{left, bottom, 0.0f}, {1.0f, 0.0f, 0.0f}, uv_lb,   textureIdx,
                           samplerIdx,           fontType,           pxRange, 0u};
            vertices[1] = {{right, bottom, 0.0f},
                           {0.0f, 1.0f, 0.0f},
                           uv_rb,
                           textureIdx,
                           samplerIdx,
                           fontType,
                           pxRange,
                           0u};
            vertices[2] = {{right, top, 0.0f}, {0.0f, 0.0f, 1.0f}, uv_rt,   textureIdx,
                           samplerIdx,         fontType,           pxRange, 0u};
            vertices[3] = {{left, top, 0.0f}, {1.0f, 0.0f, 0.0f}, uv_lt,   textureIdx,
                           samplerIdx,        fontType,           pxRange, 0u};

            // 索引（两个三角形）
            std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};

            // 构造 mesh 对象并存入列表
            objects.emplace_back(
                allocator, pool, queue,
                mesh_base::mesh_data{std::move(vertices), std::move(indices)});

            // 更新光标：前进量（设计单位 * fontSize）
            cursorX += static_cast<float>(g->advance * fontSize);
        }

        return objects;
    };
    // NOTE: 生成结果
    RenderMeshes objects;
    BidiResult bidi = bidi_analyze(text);
    LogicalGlyphs logical = shape_with_harfbuzz(bidi, font_lib);
    VisualGlyphs visual = reorder_visually(bidi, logical);
    objects = generate_meshes(visual, allocator, commandPool, GRAPHICS_AND_PRESENT, 0.2f);

    // diff: [test_harfbuzz] end

#endif

    std::println("objects.size()==text.size(): {}", objects.size() == text.size());
    // createDescriptorSets
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
                                 .descriptorCount = TEXTURE_COUNT,
                                 .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                 .pImageInfo = textureInfos.data()},
            VkWriteDescriptorSet{.sType = sType<VkWriteDescriptorSet>(),
                                 .dstSet = descriptorSets[i],
                                 .dstBinding = 2,
                                 .dstArrayElement = 0,
                                 .descriptorCount = SAMPLER_COUNT,
                                 .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                                 .pImageInfo = samplerInfos.data()}};
        device.updateDescriptorSets(static_cast<uint32_t>(writes.size()), writes.data(),
                                    0, nullptr);
        // diff: [texture] end
    }
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
                                                    VK_DYNAMIC_STATE_SCISSOR}},
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
            // 绘制单个 字符
            for (auto &input_mesh : objects)
            {

                // 推送常量
                uint64_t vertexBufferAddress =
                    input_mesh.getVertexBufferAddress(currentFrame);
                commandBuffer.pushConstants(*pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                                            0, sizeof(PushConstants),
                                            &vertexBufferAddress);

                // 绑定索引缓冲区（全局）
                commandBuffer.bindIndexBuffer(
                    input_mesh.frameBuffers[currentFrame].indexBuffer.buffer(), 0,
                    mesh_base::indexType());

                // 绘制该批次
                constexpr auto index_size = 6;
                commandBuffer.drawIndexed(static_cast<uint32_t>(index_size), 1, 0, 0, 0);
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