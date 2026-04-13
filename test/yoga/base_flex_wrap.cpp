#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <print>

#include "../mcsvulkan/head.hpp"

#include <random>
#include <utility>
#include <variant>
#include <vector>
#include <yoga/Yoga.h>

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

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;
constexpr auto TITLE = "test_my_triangle";
using mcs::vulkan::MCS_ASSERT;

static constexpr auto MAX_FRAMES_IN_FLIGHT = 2;

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

namespace mcs::vulkan
{
    struct buffer_base
    {
        buffer_base() = default;
        constexpr buffer_base(VkBuffer buffer, VmaAllocator allocator,
                              VmaAllocation allocation) noexcept
            : buffer_{buffer}, allocator_{allocator}, allocation_{allocation}
        {
        }
        constexpr ~buffer_base() noexcept
        {
            destroy();
        }
        buffer_base(const buffer_base &) = delete;
        buffer_base &operator=(const buffer_base &) = delete;

        constexpr buffer_base(buffer_base &&other) noexcept
            : buffer_{std::exchange(other.buffer_, {})},
              allocator_{std::exchange(other.allocator_, {})},
              allocation_{std::exchange(other.allocation_, {})}
        {
        }

        constexpr buffer_base &operator=(buffer_base &&other) noexcept
        {
            if (&other != this)
            {
                this->destroy();
                buffer_ = std::exchange(other.buffer_, {});
                allocator_ = std::exchange(other.allocator_, {});
                allocation_ = std::exchange(other.allocation_, {});
            }
            return *this;
        }

        [[nodiscard]] constexpr VkBuffer buffer() const noexcept
        {
            return buffer_;
        }

        [[nodiscard]] constexpr VmaAllocation allocation() const noexcept
        {
            return allocation_;
        }

        [[nodiscard]] constexpr bool valid() const noexcept
        {
            return buffer_ != nullptr;
        }

        [[nodiscard]] void *map() const
        {
            void *data; // NOLINT
            ::vmaMapMemory(allocator_, allocation_, &data);
            return data;
        }

        void unmap() const noexcept
        {
            ::vmaUnmapMemory(allocator_, allocation_);
        }

        void copyDataToBuffer(const void *src, size_t size) const
        {
            void *data = map();
            ::memcpy(data, src, size);
            unmap();
        }

        void clear()
        {
            destroy();
        }

      private:
        VkBuffer buffer_{};
        VmaAllocator allocator_{};
        VmaAllocation allocation_{};

        constexpr void destroy() noexcept
        {
            if (buffer_ != nullptr)
            {
                ::vmaDestroyBuffer(allocator_, buffer_, allocation_);
                buffer_ = {};
                allocator_ = {};
                allocation_ = {};
            }
        }
    };

    // 修改 create_buffer 函数，移除 properties 参数
    [[nodiscard]] static constexpr buffer_base create_buffer(
        VmaAllocator allocator, const VkBufferCreateInfo &bufferInfo,
        const VmaAllocationCreateInfo &allocCreateInfo)
    {
        VkBuffer buffer = nullptr;
        VmaAllocation allocation = nullptr;
        VmaAllocationInfo allocInfo;

        check_vkresult(::vmaCreateBuffer(allocator, &bufferInfo, &allocCreateInfo,
                                         &buffer, &allocation, &allocInfo));

        buffer_base result{buffer, allocator, allocation};

        return result;
    }

    // 修改 staging_buffer 函数，直接使用 VMA 的便利标志
    static constexpr auto staging_buffer(VmaAllocator allocator, size_t buffer_size)
    {
        // https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/memory_mapping.html#:~:text=Persistently%20mapped%20memory
        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        return create_buffer(allocator,
                             {.sType = sType<VkBufferCreateInfo>(),
                              .size = buffer_size,
                              .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
                             allocCreateInfo);
    }

    static constexpr auto simple_copy_buffer(const CommandPool &commandpool,
                                             const Queue &queue, VkBuffer srcBuffer,
                                             VkBuffer dstBuffer,
                                             const VkBufferCopy &regions)
    {
        const auto *logicalDevice = commandpool.device();
        CommandBuffer commandCopyBuffer = commandpool.allocateOneCommandBuffer(
            {.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY});

        commandCopyBuffer.begin({.sType = sType<VkCommandBufferBeginInfo>(),
                                 .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT});
        commandCopyBuffer.copyBuffer(srcBuffer, dstBuffer, {&regions, 1});
        commandCopyBuffer.end();

        Fence fence{*logicalDevice, {.sType = sType<VkFenceCreateInfo>(), .flags = 0}};
        queue.submit(1,
                     {.sType = sType<VkSubmitInfo>(),
                      .commandBufferCount = 1,
                      .pCommandBuffers = &*commandCopyBuffer},
                     *fence);
        check_vkresult(logicalDevice->waitForFences(1, *fence, VK_TRUE, UINT64_MAX));
    }

}; // namespace mcs::vulkan

struct PushConstants
{
    uint64_t vertexBufferAddress; // 使用uint64_t而不是VkDeviceAddress
};
struct Vertex // NOLINT
{
    glm::vec2 pos;
    glm::vec3 color;
};
static_assert(alignof(Vertex) == 4, "check alignof error");
static_assert(sizeof(Vertex) == 20, "check sizeof error");

struct mesh_base
{
    using index_type = uint32_t;
    // 双缓冲顶点数据：每个飞行帧都有自己的缓冲区
    struct FrameBuffers
    {
        mcs::vulkan::buffer_base vertexBuffer;
        mcs::vulkan::buffer_base indexBuffer;
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
    // NOTE: 不再需要重复设置下面的内存配置
    //  static constexpr auto REQUIRE_MEMORY_FLAG = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    constexpr void createVertexBufferForFrame(VmaAllocator allocator, FrameBuffers &fb)
    {
        auto &vertices = queue_data.vertices;
        const VkDeviceSize BUFFER_SIZE = sizeof(vertices[0]) * vertices.size();
        auto &destBuffer = fb.vertexBuffer;
        constexpr auto USAGE = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

        // 直接使用重构后的 create_buffer 函数
        destBuffer = mcs::vulkan::create_buffer(
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
        const auto STAGING_BUFFER = mcs::vulkan::staging_buffer(allocator, BUFFER_SIZE);
        STAGING_BUFFER.copyDataToBuffer(vertices.data(), BUFFER_SIZE);

        mcs::vulkan::simple_copy_buffer(*commandpool, *queue, STAGING_BUFFER.buffer(),
                                        destBuffer.buffer(),
                                        VkBufferCopy{.size = BUFFER_SIZE});
    }

    void createIndexBufferForFrame(VmaAllocator allocator, FrameBuffers &fb)
    {
        auto &indices = queue_data.indices;
        const VkDeviceSize BUFFER_SIZE = sizeof(indices[0]) * indices.size();
        auto &destBuffer = fb.indexBuffer;
        constexpr auto USAGE = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

        // 直接使用重构后的 create_buffer 函数
        destBuffer = mcs::vulkan::create_buffer(
            allocator,
            {.sType = sType<VkBufferCreateInfo>(),
             .size = BUFFER_SIZE,
             .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | USAGE | REQUIRE_BUFFER_USAGE,
             .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
            {.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
             .usage = VMA_MEMORY_USAGE_AUTO});

        // 创建暂存缓冲区并复制数据
        const auto STAGING_BUFFER = mcs::vulkan::staging_buffer(allocator, BUFFER_SIZE);
        STAGING_BUFFER.copyDataToBuffer(indices.data(), BUFFER_SIZE);

        mcs::vulkan::simple_copy_buffer(*commandpool, *queue, STAGING_BUFFER.buffer(),
                                        destBuffer.buffer(),
                                        {VkBufferCopy{.size = BUFFER_SIZE}});
    }

    void getBufferDeviceAddresses(FrameBuffers &fb)
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

namespace yoga
{

    template <typename Type, std::same_as<Type>... T>
    constexpr static auto make_vector(T &&...t) -> std::vector<Type>
    {
        std::vector<Type> nodes;
        nodes.reserve(sizeof...(T));
        (nodes.push_back(std::forward<T>(t)), ...);
        return nodes;
    }

    namespace property
    {
        struct Auto // NOLINT
        {
        };
        struct Pixels // NOLINT
        {
            float points{};
        };
        struct Percentage // NOLINT
        {
            float percent{};
        };

        constexpr auto zerofloat = 0.0F;               // NOLINT
        constexpr auto zeropixels = Pixels{zerofloat}; // NOLINT
        constexpr auto undefined = std::monostate{};   // NOLINT

        struct value_property
        {
            using value_type = std::variant<Auto, Pixels, Percentage>;
            value_type value_{}; // NOLINT

            // NOLINTBEGIN // 隐式转换构造函数（不加 explicit）
            value_property() = default;
            constexpr value_property(Auto v) noexcept : value_(v) {}
            constexpr value_property(Pixels v) noexcept : value_(v) {}
            constexpr value_property(Percentage v) noexcept : value_(v) {}
            constexpr value_property(value_type v) noexcept : value_(v) {}
            // NOLINTEND

            constexpr auto &operator*() noexcept
            {
                return value_;
            }
            constexpr const auto &operator*() const noexcept
            {
                return value_;
            }
            constexpr auto &operator=(Auto value) noexcept
            {
                value_ = value;
                return *this;
            }
            constexpr auto &operator=(Pixels value) noexcept
            {
                value_ = value;
                return *this;
            }
            constexpr auto &operator=(Percentage value) noexcept
            {
                value_ = value;
                return *this;
            }
        };

        struct value_property_noauto
        {
            using value_type = std::variant<std::monostate, Pixels, Percentage>;
            value_type value_{}; // NOLINT

            // NOLINTBEGIN // 隐式转换构造函数（不加 explicit）
            value_property_noauto() = default;
            constexpr value_property_noauto(Pixels v) noexcept : value_(v) {}
            constexpr value_property_noauto(Percentage v) noexcept : value_(v) {}
            constexpr value_property_noauto(value_type v) noexcept : value_(v) {}
            // NOLINTEND

            constexpr auto &operator*() noexcept
            {
                return value_;
            }
            constexpr const auto &operator*() const noexcept
            {
                return value_;
            }
            constexpr auto &operator=(std::monostate value) noexcept
            {
                value_ = value;
                return *this;
            }
            constexpr auto &operator=(Pixels value) noexcept
            {
                value_ = value;
                return *this;
            }
            constexpr auto &operator=(Percentage value) noexcept
            {
                value_ = value;
                return *this;
            }
        };

        template <typename value_type, auto applyImpl>
        struct property_box
        {
            value_type top{};    // NOLINT
            value_type right{};  // NOLINT
            value_type bottom{}; // NOLINT
            value_type left{};   // NOLINT

            // 1 个值：四边相同
            template <typename T>
                requires(requires(T v) { value_type{v}; })
            property_box(T value) noexcept // NOLINT
                : top{value}, right{value}, bottom{value}, left{value}
            {
            }
            // 2 个值：上下 | 左右
            template <typename T>
                requires(requires(T v) { value_type{v}; })
            property_box(T vertical, T horizontal) noexcept // NOLINT
                : top{vertical}, right{horizontal}, bottom{vertical}, left{horizontal}
            {
            }
            // 3 个值：上 | 左右 | 下
            template <typename T>
                requires(requires(T v) { value_type{v}; })
            property_box(T top, T horizontal, T bottom) noexcept // NOLINT
                : top{top}, right{horizontal}, bottom{bottom}, left{horizontal}
            {
            }
            // 4 个值：上 | 右 | 下 | 左
            template <typename T>
                requires(requires(T v) { value_type{v}; })
            property_box(T top, T right, T bottom, T left) noexcept // NOLINT
                : top{top}, right{right}, bottom{bottom}, left{left}
            {
            }
            void apply(YGNodeRef node) const noexcept
            {
                applyImpl(node, YGEdge::YGEdgeTop, top);
                applyImpl(node, YGEdge::YGEdgeRight, right);
                applyImpl(node, YGEdge::YGEdgeBottom, bottom);
                applyImpl(node, YGEdge::YGEdgeLeft, left);
            }
        };

        template <typename value_type, auto applyImpl>
        struct single_value
        {
            value_type value_{}; // NOLINT

            constexpr auto &operator*() noexcept
            {
                return value_;
            }
            constexpr const auto &operator*() const noexcept
            {
                return value_;
            }

            single_value() = default;
            template <typename T>
                requires(requires(T v) { value_type{v}; })
            single_value(T value) noexcept // NOLINT
                : value_{value}
            {
            }
            template <typename T>
                requires(requires(T v) { value_type{v}; })
            constexpr auto &operator=(T value) noexcept
            {
                this->value_ = value;
                return *this;
            }

            void apply(YGNodeRef node) const noexcept
            {
                applyImpl(node, value_);
            }
        };

        template <auto AutoFn, auto PixelsFn, auto PercentFn>
        struct common_property
        {
            using value_type = value_property;
            value_type value_{}; // NOLINT

            constexpr auto &operator*() noexcept
            {
                return value_;
            }
            constexpr const auto &operator*() const noexcept
            {
                return value_;
            }

            common_property() = default;
            template <typename T>
                requires(requires(T v) { value_type{v}; })
            common_property(T value) noexcept // NOLINT
                : value_{value}
            {
            }
            template <typename T>
                requires(requires(T v) { value_type{v}; })
            constexpr auto &operator=(T value) noexcept
            {
                this->value_ = value;
                return *this;
            }

            void apply(YGNodeRef node) const noexcept
            {
                std::visit(
                    [=](auto &&arg) {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, property::Auto>)
                            AutoFn(node);
                        else if constexpr (std::is_same_v<T, property::Pixels>)
                            PixelsFn(node, arg.points);
                        else if constexpr (std::is_same_v<T, property::Percentage>)
                            PercentFn(node, arg.percent);
                        else
                            static_assert(false, "non-exhaustive visitor!");
                    },
                    *value_);
            }
        };

        template <auto PixelsFn, auto PercentFn>
        struct noauto_property
        {
            using value_type = value_property_noauto;
            value_type value_{}; // NOLINT

            constexpr auto &operator*() noexcept
            {
                return value_;
            }
            constexpr const auto &operator*() const noexcept
            {
                return value_;
            }

            noauto_property() = default;
            template <typename T>
                requires(requires(T v) { value_type{v}; })
            noauto_property(T value) noexcept // NOLINT
                : value_{value}
            {
            }
            template <typename T>
                requires(requires(T v) { value_type{v}; })
            constexpr auto &operator=(T value) noexcept
            {
                this->value_ = value;
                return *this;
            }

            void apply(YGNodeRef node) const noexcept
            {
                std::visit(
                    [=](auto &&arg) {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, std::monostate>)
                            ;
                        else if constexpr (std::is_same_v<T, property::Pixels>)
                            PixelsFn(node, arg.points);
                        else if constexpr (std::is_same_v<T, property::Percentage>)
                            PercentFn(node, arg.percent);
                        else
                            static_assert(false, "non-exhaustive visitor!");
                    },
                    *value_);
            }
        };

    }; // namespace property

    using property::Auto;
    using property::Pixels;
    using property::Percentage;

    // https://www.yogalayout.dev/docs/styling/position
    struct property_position
        : property::single_value<YGPositionType, YGNodeStyleSetPositionType>
    {
        using single_value::operator=;
    };

    // https://www.yogalayout.dev/docs/styling/insets
    template <YGEdge edge>
    struct insets
    {
        using value_type = property::value_property;
        value_type value{}; // NOLINT

        constexpr auto &operator*() noexcept
        {
            return value;
        }
        constexpr const auto &operator*() const noexcept
        {
            return value;
        }

        insets() = default;
        template <typename T>
            requires(requires(T v) { value_type{v}; })
        insets(T value) noexcept // NOLINT
            : value{value}
        {
        }
        template <typename T>
            requires(requires(T v) { value_type{v}; })
        constexpr auto &operator=(T value) noexcept
        {
            this->value = value;
            return *this;
        }

        void apply(YGNodeRef node) const noexcept
        {
            std::visit(
                [=](auto &&arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, property::Auto>)
                        YGNodeStyleSetPositionAuto(node, edge);
                    else if constexpr (std::is_same_v<T, property::Pixels>)
                        YGNodeStyleSetPosition(node, edge, arg.points);
                    else if constexpr (std::is_same_v<T, property::Percentage>)
                        YGNodeStyleSetPositionPercent(node, edge, arg.percent);
                    else
                        static_assert(false, "non-exhaustive visitor!");
                },
                *value);
        }
    };
    struct property_top : insets<YGEdge::YGEdgeTop>
    {
        using insets::operator=;
    };
    struct property_right : insets<YGEdge::YGEdgeRight>
    {
        using insets::operator=;
    };
    struct property_bottom : insets<YGEdge::YGEdgeBottom>
    {
        using insets::operator=;
    };
    struct property_left : insets<YGEdge::YGEdgeLeft>
    {
        using insets::operator=;
    };

    // https://www.yogalayout.dev/docs/styling/margin-padding-border
    struct property_margin
        : property::property_box<
              property::value_property,
              [](YGNodeRef node, YGEdge edge, property::value_property value) noexcept {
                  std::visit(
                      [=](auto &&arg) {
                          using T = std::decay_t<decltype(arg)>;
                          if constexpr (std::is_same_v<T, property::Auto>)
                              YGNodeStyleSetMarginAuto(node, edge);
                          else if constexpr (std::is_same_v<T, property::Pixels>)
                              YGNodeStyleSetMargin(node, edge, arg.points);
                          else if constexpr (std::is_same_v<T, property::Percentage>)
                              YGNodeStyleSetMarginPercent(node, edge, arg.percent);
                          else
                              static_assert(false, "non-exhaustive visitor!");
                      },
                      *value);
              }>
    {
    };
    struct property_padding
        : property::property_box<
              property::value_property_noauto,
              [](YGNodeRef node, YGEdge edge,
                 property::value_property_noauto value) noexcept {
                  std::visit(
                      [=](auto &&arg) {
                          using T = std::decay_t<decltype(arg)>;
                          if constexpr (std::is_same_v<T, std::monostate>)
                              ;
                          else if constexpr (std::is_same_v<T, property::Pixels>)
                              YGNodeStyleSetPadding(node, edge, arg.points);
                          else if constexpr (std::is_same_v<T, property::Percentage>)
                              YGNodeStyleSetPaddingPercent(node, edge, arg.percent);
                          else
                              static_assert(false, "non-exhaustive visitor!");
                      },
                      *value);
              }>
    {
    };
    struct property_border
        : property::property_box<property::Pixels, [](YGNodeRef node, YGEdge edge,
                                                      property::Pixels value) noexcept {
            YGNodeStyleSetBorder(node, edge, value.points);
        }>
    {
    };

    // https://www.yogalayout.dev/docs/styling/width-height
    struct property_width
        : property::common_property<YGNodeStyleSetWidthAuto, YGNodeStyleSetWidth,
                                    YGNodeStyleSetWidthPercent>
    {
        using common_property::operator=;
    };
    struct property_height
        : property::common_property<YGNodeStyleSetHeightAuto, YGNodeStyleSetHeight,
                                    YGNodeStyleSetHeightPercent>
    {
        using common_property::operator=;
    };

    // https://www.yogalayout.dev/docs/styling/min-max-width-height
    struct property_min_width
        : property::noauto_property<YGNodeStyleSetMinWidth, YGNodeStyleSetMinWidthPercent>
    {
        using noauto_property::operator=;
    };
    struct property_max_width
        : property::noauto_property<YGNodeStyleSetMaxWidth, YGNodeStyleSetMaxWidthPercent>
    {
        using noauto_property::operator=;
    };
    struct property_min_height : property::noauto_property<YGNodeStyleSetMinHeight,
                                                           YGNodeStyleSetMinHeightPercent>
    {
        using noauto_property::operator=;
    };
    struct property_max_height : property::noauto_property<YGNodeStyleSetMaxHeight,
                                                           YGNodeStyleSetMaxHeightPercent>
    {
        using noauto_property::operator=;
    };

    // https://www.yogalayout.dev/docs/styling/layout-direction
    struct property_layout_direction
        : property::single_value<YGDirection, YGNodeStyleSetDirection>
    {
        using single_value::operator=;
    };

    // https://www.yogalayout.dev/docs/styling/flex-direction
    struct property_flex_direction
        : property::single_value<YGFlexDirection, YGNodeStyleSetFlexDirection>
    {
        using single_value::operator=;
    };

    // https://www.yogalayout.dev/docs/styling/justify-content
    struct property_justify_content
        : property::single_value<YGJustify, YGNodeStyleSetJustifyContent>
    {
        using single_value::operator=;
    };

    // https://www.yogalayout.dev/docs/styling/gap
    struct property_gap
    {
        using value_type = property::value_property_noauto;
        value_type row{};    // NOLINT
        value_type column{}; // NOLINT

        property_gap() = default;
        // 单个值简化
        template <typename T>
            requires(requires(T v) { value_type{v}; })
        property_gap(T value) noexcept // NOLINT
            : row{value}, column{value}
        {
        }
        // 明确两个值
        template <typename T>
            requires(requires(T v) { value_type{v}; })
        property_gap(T row, T column) noexcept // NOLINT
            : row{row}, column{column}
        {
        }
        void apply(YGNodeRef node) const noexcept
        {
            // NOLINTNEXTLINE
            constexpr auto vister = [](YGNodeRef node, YGGutter edge,
                                       value_type value) noexcept {
                std::visit(
                    [=](auto &&arg) {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, std::monostate>)
                            ;
                        else if constexpr (std::is_same_v<T, property::Pixels>)
                            YGNodeStyleSetGap(node, edge, arg.points);
                        else if constexpr (std::is_same_v<T, property::Percentage>)
                            YGNodeStyleSetGapPercent(node, edge, arg.percent);
                        else
                            static_assert(false, "non-exhaustive visitor!");
                    },
                    *value);
            };
            vister(node, YGGutterRow, row);
            vister(node, YGGutterColumn, column);
        }
    };

    // https://www.yogalayout.dev/docs/styling/flex-wrap
    struct property_flex_wrap : property::single_value<YGWrap, YGNodeStyleSetFlexWrap>
    {
        using single_value::operator=;
    };

    // https://www.yogalayout.dev/docs/styling/flex-basis-grow-shrink
    struct property_flex_basis
        : property::common_property<YGNodeStyleSetFlexBasisAuto, YGNodeStyleSetFlexBasis,
                                    YGNodeStyleSetFlexBasisPercent>
    {
        using common_property::operator=;
    };
    struct property_flex_grow : property::single_value<float, YGNodeStyleSetFlexGrow>
    {
        using single_value::operator=;
    };
    struct property_flex_shrink : property::single_value<float, YGNodeStyleSetFlexShrink>
    {
        using single_value::operator=;
    };

    // https://www.yogalayout.dev/docs/styling/display
    struct property_display : property::single_value<YGDisplay, YGNodeStyleSetDisplay>
    {
        using single_value::operator=;
    };

    // https://www.yogalayout.dev/docs/styling/aspect-ratio
    struct property_aspect_ratio
        : property::single_value<float, YGNodeStyleSetAspectRatio>
    {
        using single_value::operator=;
    };

    // https://www.yogalayout.dev/docs/styling/align-items-self
    struct property_align_items
        : property::single_value<YGAlign, YGNodeStyleSetAlignItems>
    {
        using single_value::operator=;
    };
    struct property_align_self : property::single_value<YGAlign, YGNodeStyleSetAlignSelf>
    {
        using single_value::operator=;
    };

    // https://www.yogalayout.dev/docs/styling/align-content
    struct property_align_content
        : property::single_value<YGAlign, YGNodeStyleSetAlignContent>
    {
        using single_value::operator=;
    };

    class Config
    {
        using value_type = YGConfigRef;
        value_type value_{};

      public:
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
        [[nodiscard]] constexpr auto get() const noexcept
        {
            return value_;
        }
        constexpr void release() noexcept
        {
            if (value_ != nullptr)
                YGConfigFree(value_);
        }
        Config() = default;
        constexpr explicit Config(value_type value) noexcept : value_{value} {}
        constexpr explicit Config(YGErrata errata) noexcept : value_{YGConfigNew()}
        {
            YGConfigSetErrata(value_, errata);
        }
        constexpr ~Config() noexcept
        {
            release();
        }
        Config(const Config &) = delete;
        constexpr Config(Config &&o) noexcept : value_{std::exchange(o.value_, {})} {}
        Config &operator=(const Config &) = delete;
        constexpr Config &operator=(Config &&o) noexcept
        {
            if (&o != this)
                value_ = std::exchange(o.value_, {});
            return *this;
        }

        auto &setErrata(YGErrata errata) & noexcept
        {
            YGConfigSetErrata(value_, errata);
            return *this;
        }
        auto &setPointScaleFactor(float pixelsInPoint) & noexcept
        {
            YGConfigSetPointScaleFactor(value_, pixelsInPoint);
            return *this;
        }
        auto &setUseWebDefaults(bool enabled) & noexcept
        {
            YGConfigSetUseWebDefaults(value_, enabled);
            return *this;
        }

        auto &&setErrata(YGErrata errata) && noexcept
        {
            YGConfigSetErrata(value_, errata);
            return std::move(*this);
        }
        auto &&setPointScaleFactor(float pixelsInPoint) && noexcept
        {
            YGConfigSetPointScaleFactor(value_, pixelsInPoint);
            return std::move(*this);
        }
        auto &&setUseWebDefaults(bool enabled) && noexcept
        {
            YGConfigSetUseWebDefaults(value_, enabled);
            return std::move(*this);
        }
    };
    // https://www.yogalayout.dev/docs/styling/
    class Style
    {
      public:
        // NOLINTBEGIN

        // https://www.yogalayout.dev/docs/styling/align-content
        property_align_content align_content{YGAlign::YGAlignFlexStart};

        // https://www.yogalayout.dev/docs/styling/align-items-self
        property_align_items align_items{YGAlign::YGAlignStretch};
        property_align_self align_self{YGAlign::YGAlignAuto};

        // https://www.yogalayout.dev/docs/styling/aspect-ratio
        property_aspect_ratio aspect_ratio{property::zerofloat};

        // https://www.yogalayout.dev/docs/styling/display
        property_display display{YGDisplay::YGDisplayFlex};

        // https://www.yogalayout.dev/docs/styling/flex-basis-grow-shrink
        property_flex_basis flex_basis{property::Auto{}};
        property_flex_grow flex_grow{property::zerofloat};
        property_flex_shrink flex_shrink{1.0F};

        // https://www.yogalayout.dev/docs/styling/flex-wrap
        property_flex_wrap flex_wrap{YGWrap::YGWrapNoWrap};
        // https://www.yogalayout.dev/docs/styling/flex-direction
        property_flex_direction flex_direction{YGFlexDirection::YGFlexDirectionColumn};

        // https://www.yogalayout.dev/docs/styling/gap
        property_gap gap{};

        // https://www.yogalayout.dev/docs/styling/position
        property_position position{YGPositionType::YGPositionTypeRelative};
        // https://www.yogalayout.dev/docs/styling/insets
        property_top top{property::Auto{}};
        property_right right{property::Auto{}};
        property_bottom bottom{property::Auto{}};
        property_left left{property::Auto{}};

        // https://www.yogalayout.dev/docs/styling/justify-content
        property_justify_content justify_content{YGJustify::YGJustifyFlexStart};

        // https://www.yogalayout.dev/docs/styling/layout-direction
        property_layout_direction direction{YGDirection::YGDirectionLTR};

        // https://www.yogalayout.dev/docs/styling/margin-padding-border
        property_margin margin{property::zeropixels};
        property_padding padding{property::zeropixels};
        property_border border{property::zeropixels};

        // https://www.yogalayout.dev/docs/styling/min-max-width-height
        // By default all these constraints are undefined.
        property_min_width min_width{property::undefined};
        property_max_width max_width{property::undefined};
        property_min_height min_height{property::undefined};
        property_max_height max_height{property::undefined};

        // https://www.yogalayout.dev/docs/styling/width-height
        property_width width{property::Auto{}};
        property_height height{property::Auto{}};

        // NOLINTEND

        void apply(YGNodeRef node) const noexcept
        {
            align_content.apply(node);

            align_items.apply(node);
            align_self.apply(node);

            // Accepts any floating point value > 0, the default is undefined.
            if (*aspect_ratio > property::zerofloat)
                aspect_ratio.apply(node);

            display.apply(node);

            flex_basis.apply(node);
            // Flex grow accepts any floating point value >= 0, with 0 being the default
            // value.
            if (*flex_grow >= property::zerofloat)
                flex_grow.apply(node);
            // Flex shrink accepts any floating point value >= 0, with 1 being the default
            // value.
            if (*flex_shrink >= property::zerofloat)
                flex_shrink.apply(node);

            flex_wrap.apply(node);
            flex_direction.apply(node); // NOTE: 依赖wrap值。调整wrap到前面

            gap.apply(node);

            position.apply(node); // NOTE: 耦合性强需要特别注意。事实上就是忽略
            if (*position != YGPositionType::YGPositionTypeStatic)
            {
                // 在线测试：https://www.yogalayout.dev/docs/styling/insets
                // 处理 top/bottom
                if (!std::holds_alternative<property::Auto>(**top))
                {
                    top.apply(node); // top 非 auto，使用 top，忽略 bottom
                }
                else
                {
                    bottom.apply(node); // top 为 auto，则使用 bottom
                }

                // 处理 left/right
                if (!std::holds_alternative<property::Auto>(**left))
                {
                    left.apply(node); // left 非 auto，使用 left，忽略 right
                }
                else
                {
                    right.apply(node); // left 为 auto，则使用 right
                }
            }

            // NOTE: 和其他属性耦合 ，至少依赖 flex direction
            justify_content.apply(node);

            // NOTE: Layout Direction 决定布局的起始边和结束边,有耦合
            direction.apply(node);

            margin.apply(node);
            padding.apply(node);
            border.apply(node);

            if ((**min_width).index() != 0)
                min_width.apply(node);
            if ((**max_width).index() != 0)
                max_width.apply(node);
            if ((**min_height).index() != 0)
                min_height.apply(node);
            if ((**max_height).index() != 0)
                max_height.apply(node);

            width.apply(node);
            height.apply(node);
        }
    };
    struct layout_size
    {
        float available_width{YGUndefined};
        float available_height{YGUndefined};
    };

    class Node
    {
        using value_type = YGNodeRef;

        std::string id_;
        Config config_;
        value_type value_;
        Style style_;
        std::vector<Node> childrens_;

      public:
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
        [[nodiscard]] constexpr auto get() const noexcept
        {
            return value_;
        }
        constexpr void release() noexcept
        {
            if (value_ != nullptr)
                YGNodeFree(std::exchange(value_, {}));
        }

        void applyStyle() const noexcept
        {
            style_.apply(value_);
        }
        void update() const noexcept
        {
            applyStyle();
        }

        auto append(const Node &child)
        {
            YGNodeInsertChild(value_, child.value_, YGNodeGetChildCount(value_));
        }

        Node(const Node &) = delete;
        Node(Node &&o) noexcept
            : id_{std::exchange(o.id_, {})}, config_{std::exchange(o.config_, {})},
              value_{std::exchange(o.value_, {})}, style_{std::exchange(o.style_, {})},
              childrens_{std::exchange(o.childrens_, {})}
        {
        }
        Node &operator=(const Node &) = delete;
        Node &operator=(Node &&o) noexcept
        {
            if (&o != this)
            {
                this->release();
                id_ = std::exchange(o.id_, {});
                config_ = std::exchange(o.config_, {});
                value_ = std::exchange(o.value_, {});
                style_ = std::exchange(o.style_, {});
                childrens_ = std::exchange(o.childrens_, {});
            }
            return *this;
        }
        template <std::same_as<Node>... Children>
        Node(std::string id, Config config, Style style, Children... children)
            : id_{std::move(id)}, config_{std::move(config)},
              value_(YGNodeNewWithConfig(*config_)), style_{std::move(style)}
        {
            // 1. 应用当前节点的样式
            applyStyle();

            // 2. 移动构造子节点列表
            childrens_.reserve(sizeof...(Children));
            (childrens_.emplace_back(std::forward<Children>(children)), ...);

            // 3. 将所有子节点插入 Yoga 树
            for (const auto &child : childrens_)
            {
                append(child);
            }
        }
        template <std::same_as<Node>... Children>
        Node(std::string id, Style style, Children... children)
            : id_{std::move(id)}, value_(YGNodeNew()), style_{std::move(style)}
        {
            // 1. 应用当前节点的样式
            applyStyle();

            // 2. 移动构造子节点列表
            childrens_.reserve(sizeof...(Children));
            (childrens_.emplace_back(std::forward<Children>(children)), ...);

            // 3. 将所有子节点插入 Yoga 树
            for (const auto &child : childrens_)
            {
                append(child);
            }
        }
        ~Node() noexcept
        {
            release();
        }

        [[nodiscard]] YGConfigConstRef config() const noexcept
        {
            return *config_;
        }
        [[nodiscard]] const std::string &id() const noexcept
        {
            return id_;
        }
        void calculateLayout(layout_size size)
        {
            YGNodeCalculateLayout(value_, size.available_width, size.available_height,
                                  *style_.direction);
        }

        [[nodiscard]] const std::vector<Node> &childrens() const noexcept
        {
            return childrens_;
        }

        Style &refStyle() noexcept
        {
            return style_;
        }
    };

    // 生成顶点
    class Layout // NOLINT
    {
        Node *root_;

        static void printNodeLayout(const Node &node, int depth = 0)
        {
            YGNodeRef ygNode = node.get();

            std::string indent(depth * 2, ' ');

            float left = YGNodeLayoutGetLeft(ygNode);
            float top = YGNodeLayoutGetTop(ygNode);
            float right = YGNodeLayoutGetRight(ygNode);
            float bottom = YGNodeLayoutGetBottom(ygNode);
            float width = YGNodeLayoutGetWidth(ygNode);
            float height = YGNodeLayoutGetHeight(ygNode);

            std::println("{}Node Layout [{}]:", indent, node.id());
            std::println("{}  position: left={}, top={}, right={}, bottom={}", indent,
                         left, top, right, bottom);
            std::println("{}  size: width={}, height={}", indent, width, height);

            YGDirection dir = YGNodeLayoutGetDirection(ygNode);
            bool overflow = YGNodeLayoutGetHadOverflow(ygNode);
            std::println("{}  direction: {}", indent,
                         (dir == YGDirectionLTR ? "LTR" : "RTL"));
            std::println("{}  hadOverflow: {}", indent, overflow ? "true" : "false");

            std::println("{}  margin: top={}, left={}, bottom={}, right={}", indent,
                         YGNodeLayoutGetMargin(ygNode, YGEdgeTop),
                         YGNodeLayoutGetMargin(ygNode, YGEdgeLeft),
                         YGNodeLayoutGetMargin(ygNode, YGEdgeBottom),
                         YGNodeLayoutGetMargin(ygNode, YGEdgeRight));

            std::println("{}  border: top={}, left={}, bottom={}, right={}", indent,
                         YGNodeLayoutGetBorder(ygNode, YGEdgeTop),
                         YGNodeLayoutGetBorder(ygNode, YGEdgeLeft),
                         YGNodeLayoutGetBorder(ygNode, YGEdgeBottom),
                         YGNodeLayoutGetBorder(ygNode, YGEdgeRight));

            std::println("{}  padding: top={}, left={}, bottom={}, right={}", indent,
                         YGNodeLayoutGetPadding(ygNode, YGEdgeTop),
                         YGNodeLayoutGetPadding(ygNode, YGEdgeLeft),
                         YGNodeLayoutGetPadding(ygNode, YGEdgeBottom),
                         YGNodeLayoutGetPadding(ygNode, YGEdgeRight));

            // 递归打印子节点
            for (const auto &child : node.childrens())
            {
                printNodeLayout(child, depth + 1);
            }
        }

      public:
        explicit Layout(Node &root) noexcept : root_{&root} {}
        auto calculate(layout_size size) const
        {
            root_->calculateLayout(size);
        }
        auto update()
        {
            root_->update();
        }
        void print()
        {
            printNodeLayout(*root_);
        }
        // TODO(mcs): 应该得到信息的顶点数据？
        auto applyLayout(YGNodeRef node)
        {
            if (!YGNodeGetHasNewLayout(node))
            {
                return;
            }

            // Reset the flag
            YGNodeSetHasNewLayout(node, false);

            // Do the real work
            // ...

            for (size_t i = 0; i < YGNodeGetChildCount(node); i++)
            {
                applyLayout(YGNodeGetChild(node, i));
            }
        }

        [[nodiscard]] Node *root() const noexcept
        {
            return root_;
        }
    };
}; // namespace yoga

namespace yoga::literals
{
    constexpr property::Pixels operator"" _px(unsigned long long v) noexcept
    {
        return property::Pixels{static_cast<float>(v)};
    }
    constexpr property::Pixels operator"" _px(long double v) noexcept
    {
        return property::Pixels{static_cast<float>(v)};
    }
    constexpr property::Percentage operator"" _pc(unsigned long long v) noexcept
    {
        return property::Percentage{static_cast<float>(v)};
    }
    constexpr property::Percentage operator"" _pc(long double v) noexcept
    {
        return property::Percentage{static_cast<float>(v)};
    }
} // namespace yoga::literals

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
            {.features = {.shaderInt64 = VK_TRUE}}, // diff: shader 扩展这里也要打开
            {.synchronization2 = VK_TRUE, .dynamicRendering = VK_TRUE},
            {
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
                return features2.features.shaderInt64 &&
                       query_vulkan13_features.dynamicRendering &&
                       query_vulkan13_features.synchronization2 &&

                       query_vulkan12_features
                           .bufferDeviceAddress && // diff: [new]
                                                   // 检查Vulkan 1.2中的bufferDeviceAddress
                       query_vulkan12_features
                           .scalarBlockLayout && // diff: [new]
                                                 // 检查scalarBlockLayout

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

    auto pipelineLayout =
        create_pipeline_layout{}
            .setCreateInfo(
                {.setLayouts = {}, // diff: [new] 布局定义推送常量范围
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
                      .pColorAttachmentFormats = &swapchainBuild.refImageFormat()}}),
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

                      // diff: [test_triangle] start 逆时针为正面的约定，兼容性配置固定
                      .cullMode = VK_CULL_MODE_BACK_BIT,            // 恢复背面剔除
                      .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, // 逆时针为正面
                      // diff: [test_triangle] end
                      .depthBiasEnable = VK_FALSE,
                      .lineWidth = 1.0F},
                 .multisampleState =
                     {
                         // 没有硬件采样配置
                         .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
                         // NOTE: 9. 这里可以改进内部颜色质量
                         .sampleShadingEnable = VK_FALSE,
                     },
                 .depthStencilState = {},
                 .colorBlendState = {.logicOpEnable = VK_FALSE,
                                     .logicOp = VkLogicOp::VK_LOGIC_OP_COPY,
                                     .attachments = {{.blendEnable = VK_FALSE, // 关闭混合
                                                      .colorWriteMask =
                                                          VK_COLOR_COMPONENT_R_BIT |
                                                          VK_COLOR_COMPONENT_G_BIT |
                                                          VK_COLOR_COMPONENT_B_BIT |
                                                          VK_COLOR_COMPONENT_A_BIT}}},
                 .dynamicState = {.dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                                                    VK_DYNAMIC_STATE_SCISSOR}},
                 .layout = *pipelineLayout})
            .build(device);

    CommandPool commandPool =
        create_command_pool{}
            .setCreateInfo({.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                            .queueFamilyIndex = GRAPHICS_QUEUE_FAMILY_IDX})
            .build(device);

    constexpr auto MAX_FRAMES_IN_FLIGHT = 2;
    CommandBuffers commandBuffers =
        commandPool.allocateCommandBuffers({.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                            .commandBufferCount = MAX_FRAMES_IN_FLIGHT});
    frame_context<MAX_FRAMES_IN_FLIGHT> frameContext{device, swapchain.imagesSize()};

    // diff: 准备好顶点和顶点索引--------------- // NOLINTBEGIN
    // diff: [test_yoga_init3] start
    using namespace yoga::literals;
    using namespace yoga;
    // diff: [base_flex_wrap] start
    /*
<Layout config={{useWebDefaults: false}}>
<Node
  style={{
    width: 200,
    height: 250,
    padding: 10,
    flexWrap: 'wrap',
  }}>
  <Node style={{margin: 5, height: 50, width: 50}} />
  <Node style={{margin: 5, height: 50, width: 50}} />
  <Node style={{margin: 5, height: 50, width: 50}} />
  <Node style={{margin: 5, height: 50, width: 50}} />
</Node>
</Layout>
*/
    Node screen{
        "screen", Style{.width = {Pixels{WIDTH}}, .height = {Pixels{HEIGHT}}},
        Node{
            "root",
            Config{YGConfigNew()}.setUseWebDefaults(false),
            Style{
                .flex_wrap = {YGWrap::YGWrapWrap},
                .padding = {10_px},
                .width = {200_px},
                .height = {250_px},
            },
            Node{"child0", Style{.margin = {5_px}, .width = {50_px}, .height = {50_px}}},
            Node{"child1", Style{.margin = {5_px}, .width = {50_px}, .height = {50_px}}},
            Node{"child2", Style{.margin = {5_px}, .width = {50_px}, .height = {50_px}}},
            Node{"child3", Style{.margin = {5_px}, .width = {50_px}, .height = {50_px}}},
        }};
    // diff: [base_flex_wrap] end

    Layout layout{screen};
    auto on_resize = [](Layout &layout, layout_size size) {
        layout.root()->refStyle().width = Pixels{size.available_width};
        layout.root()->refStyle().height = Pixels{size.available_height};
        layout.update();
    };
    layout.calculate({.available_width = 400, .available_height = 800});
    std::println("layout.print [start]");
    layout.print();
    std::println("layout.print [end]");
    auto generateVerticesFromLayout = [](Layout &layout, layout_size size,
                                         std::vector<Vertex> &outVerts,
                                         std::vector<uint32_t> &outIndices) {
        // 先让 Yoga 计算布局（传入当前屏幕宽高）
        layout.calculate(size);
        auto [screenWidth, screenHeight] = size;

        // 屏幕坐标转 NDC（-1 到 1）
        auto screenToNDC = [=](float x, float y) -> glm::vec2 {
            return {(x / screenWidth) * 2.0f - 1.0f, (y / screenHeight) * 2.0f - 1.0f};
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
            return colors[index]; // diff: [base_display] 改成尾部控制
        };

        auto traverse = [&](this auto &self, const Node &cur, float parentLeft,
                            float parentTop) {
            auto node = *cur;
            float left = YGNodeLayoutGetLeft(node) + parentLeft;
            float top = YGNodeLayoutGetTop(node) + parentTop;
            float width = YGNodeLayoutGetWidth(node);
            float height = YGNodeLayoutGetHeight(node);

            if (width > 0.0f && height > 0.0f)
            {
                float right = left + width;
                float bottom = top + height;

                glm::vec2 p0 = screenToNDC(left, bottom);
                glm::vec2 p1 = screenToNDC(right, bottom);
                glm::vec2 p2 = screenToNDC(left, top);
                glm::vec2 p3 = screenToNDC(right, top);
                glm::vec3 color = getColor();

                uint32_t base = static_cast<uint32_t>(outVerts.size());
                outVerts.push_back({p0, color});
                outVerts.push_back({p1, color});
                outVerts.push_back({p2, color});
                outVerts.push_back({p3, color});

                outIndices.push_back(base + 0);
                outIndices.push_back(base + 1);
                outIndices.push_back(base + 2);
                outIndices.push_back(base + 1);
                outIndices.push_back(base + 3);
                outIndices.push_back(base + 2);
            }
            index++;

            // NOTE: 是否必须迁移到上面的 if条件内部。
            // 这样就，如果父组件不可见子组件也不可见了。反正 可能是错误布局
            if (cur.childrens().empty())
                return;
            for (const auto &child : cur.childrens())
                self(child, left, top);
        };
        traverse(*layout.root(), 0.0f, 0.0f);
    };
    std::vector<Vertex> yogaVertices;
    std::vector<uint32_t> yogaIndices;
    generateVerticesFromLayout(layout, {WIDTH, HEIGHT}, yogaVertices, yogaIndices);
    mesh_base input_mesh{
        allocator, commandPool, GRAPHICS_AND_PRESENT, {yogaVertices, yogaIndices}};
    // diff: [test_yoga_init3] end

    auto &indexs = input_mesh.queue_data.indices;

    // diff: end // NOLINTEND
    struct record_info
    {
        uint32_t current_frame;
        uint32_t image_index;
    };
    // NOLINTNEXTLINE
    const auto recordCommandBuffer = [&](const CommandBufferView &commandBuffer,
                                         record_info info) {
        auto [currentFrame, imageIndex] = info;
        VkImage image = swapchain.image(imageIndex);
        VkImageView imageView = swapchain.imageView(imageIndex);
        auto imageExtent = swapchain.imageExtent();

        commandBuffer.begin({.sType = sType<VkCommandBufferBeginInfo>()});
        my_render::transition_image_layout(
            commandBuffer, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            {}, // srcAccessMask (no need to wait for previous operations)
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

        VkRenderingAttachmentInfo colorAttachment = {
            .sType = sType<VkRenderingAttachmentInfo>(),
            .imageView = imageView,
            .imageLayout = VkImageLayout::VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.color = {.float32 = {0.0F, 0.0F, 0.0F, 1.0F}}}};

        commandBuffer.beginRendering(
            {.sType = sType<VkRenderingInfo>(),
             .renderArea = {.offset = {.x = 0, .y = 0}, .extent = imageExtent},
             .layerCount = 1,
             .colorAttachmentCount = 1,
             .pColorAttachments = &colorAttachment,
             .pDepthAttachment = nullptr});
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
            // diff: [new] 即使使用设备地址，Vulkan仍需要绑定索引缓冲区.来变量顶点数组
            uint64_t vertexBufferDeviceAddress =
                input_mesh.getVertexBufferAddress(currentFrame);
            PushConstants pushConstants = {.vertexBufferAddress =
                                               vertexBufferDeviceAddress};

            // NOTE: 当前仅仅推送顶点数据
            commandBuffer.pushConstants(*pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                                        sizeof(PushConstants), &pushConstants);

            commandBuffer.bindIndexBuffer(
                input_mesh.frameBuffers[currentFrame].indexBuffer.buffer(), 0,
                mesh_base::indexType());

            commandBuffer.drawIndexed(static_cast<uint32_t>(indexs.size()), 1, 0, 0, 0);
            // diff: end
        }
        commandBuffer.endRendering();

        my_render::transition_image_layout(
            commandBuffer, image, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, 0,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
        commandBuffer.end();
    };

    // NOLINTNEXTLINE
    const auto recreateSwapChain = [&]() {
        surface.waitGoodFramebufferSize();
        device.waitIdle();

        // diff: [test_yoga_init] start
        VkExtent2D newSize = window.getFramebufferSize();
        uint32_t newWidth = newSize.width;
        uint32_t newHeight = newSize.height;

        on_resize(layout, {.available_width = static_cast<float>(newWidth),
                           .available_height = static_cast<float>(newHeight)});
        std::vector<Vertex> newVerts;
        std::vector<uint32_t> newIndices;
        generateVerticesFromLayout(layout,
                                   {.available_width = static_cast<float>(newWidth),
                                    .available_height = static_cast<float>(newHeight)},
                                   newVerts, newIndices);
        input_mesh.queueUpdate(std::move(newVerts), std::move(newIndices));

        // diff: [test_yoga_init] end

        swapchain.destroy();
        swapchain = swapchainBuild.rebuild();
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

        // diff: vkResetFences 之后更新顶点. 仅仅更新飞行的帧
        input_mesh.requestUpdate(allocator, currentFrame);
        // diff: end
        recordCommandBuffer(commandBuffer,
                            {.current_frame = currentFrame, .image_index = imageIndex});

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

    std::cout << "main done\n";
    return 0;
}
catch (std::exception &e)
{
    std::println("main catch exception: {}", e.what());
}