#include <array>
#include <cassert>
#include <cstdint>
#include <exception>
#include <iostream>
#include <print>
#include <random>
#include <vector>

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

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;
constexpr auto TITLE = "test_indirect_instanced";

// 简单的 Host 可见缓冲区创建辅助函数
template <typename T>
auto createHostBuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size,
                      VkBufferUsageFlags usage, const T *data = nullptr)
{
    VkBuffer buffer;
    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    vkCreateBuffer(device, &bufferInfo, nullptr, &buffer);

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, buffer, &memReqs);

    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    uint32_t memTypeIndex = 0;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
    {
        if ((memReqs.memoryTypeBits & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags &
             (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
        {
            memTypeIndex = i;
            break;
        }
    }

    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memReqs.size,
        .memoryTypeIndex = memTypeIndex,
    };
    VkDeviceMemory memory;
    vkAllocateMemory(device, &allocInfo, nullptr, &memory);
    vkBindBufferMemory(device, buffer, memory, 0);

    void *mapped;
    vkMapMemory(device, memory, 0, size, 0, &mapped);

    if (data)
    {
        memcpy(mapped, data, size);
    }

    return std::make_tuple(buffer, memory, mapped);
}

// 每个实例的数据布局（与着色器中的 per-instance 输入对应）
struct InstanceData
{
    float offsetX, offsetY; // 位置偏移 (location = 1)
    float r, g, b;          // 颜色 (location = 2)
};

// 图像布局转换（原框架提供）
struct my_render
{
    constexpr static void transition_image_layout(
        const CommandBufferView &commandBuffer, VkImage image,
        VkImageAspectFlags aspectMask, VkImageLayout oldLayout, VkImageLayout newLayout,
        VkAccessFlags2 srcAccessMask, VkAccessFlags2 dstAccessMask,
        VkPipelineStageFlags2 srcStageMask, VkPipelineStageFlags2 dstStageMask) noexcept
    {
        VkImageMemoryBarrier2 barrier = {.sType = sType<VkImageMemoryBarrier2>(),
                                         .srcStageMask = srcStageMask,
                                         .srcAccessMask = srcAccessMask,
                                         .dstStageMask = dstStageMask,
                                         .dstAccessMask = dstAccessMask,
                                         .oldLayout = oldLayout,
                                         .newLayout = newLayout,
                                         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                         .image = image,
                                         .subresourceRange = {.aspectMask = aspectMask,
                                                              .baseMipLevel = 0,
                                                              .levelCount = 1,
                                                              .baseArrayLayer = 0,
                                                              .layerCount = 1}};
        VkDependencyInfo dep = {.sType = sType<VkDependencyInfo>(),
                                .imageMemoryBarrierCount = 1,
                                .pImageMemoryBarriers = &barrier};
        commandBuffer.pipelineBarrier2(dep);
    }
};

/*
struct VkDrawIndexedIndirectCommand {
    uint32_t indexCount;    // 要绘制的索引数量（3 表示一个三角形）
    uint32_t instanceCount; // 实例数量（≥1）
    uint32_t firstIndex;    // 从索引缓冲区读取的起始偏移（以索引为单位）
    int32_t  vertexOffset;  // 加到每个索引值上的数值（用来选择不同顶点）
    uint32_t firstInstance; // 着色器中 gl_InstanceIndex 的起始值
};
所有参数都在这一个结构体里，完全由 GPU 从缓冲区读取，CPU 只需准备好 buffer。

》》》》缓冲区准备
顶点缓冲区：存放所有顶点数据（例如两个三角形的 6 个顶点）。

索引缓冲区：存放索引（局部索引 0,1,2），可以合并多个网格，也可以分开。

实例数据缓冲区 (SSBO)：存放每个实例的属性（偏移、颜色等），通过描述符集绑定。

间接命令缓冲区：包含一个或多个 VkDrawIndexedIndirectCommand，由 vkCmdDrawIndexedIndirect 读取。

》》》》绘制调用
void vkCmdDrawIndexedIndirect(
    VkCommandBuffer commandBuffer,
    VkBuffer        buffer,    // 存放间接命令的缓冲区
    VkDeviceSize    offset,    // 从缓冲区何处开始读取第一条命令
    uint32_t        drawCount, // 执行多少条命令（>1 需要 multiDrawIndirect 特性）
    uint32_t        stride     // 每条命令的字节步长（通常为 sizeof(VkDrawIndexedIndirectCommand)）
);
单命令：drawCount = 1，直接读取一条命令。

多命令（需特性）：drawCount > 1，一次性执行多条命令（场景 3、4 的 drawCount=2）。

循环调用：不需要特性，循环多次调用 drawIndexedIndirect，每次 drawCount = 1（场景 1、2）。

》》》》 测试用例及其效果
场景	                                关键参数设置	                                                                                    实现的视觉效果
0 基线	                                indexCount=3, instanceCount=1, firstInstance=0	                                                    一个红色三角形（实例0）
1 单 buffer 两次调用                    两条命令：firstInstance=1 和 firstInstance=2，每次 drawCount=1	                                        绿色 + 蓝色三角形，分别使用实例1和实例2
2 两个独立 buffer	                    两个间接缓冲区，分别存放 firstInstance=1 和 firstInstance=2 的命令	                                    同上，但 buffer 分开
3 单次 multiDraw	                    drawCount=2，命令连续存放，启用 multiDrawIndirect	一次调用完成两实例绘制
4 vertexOffset / firstIndex	          合并索引{0,1,2, 0,1,2}，cmds[0]: firstIndex=0, vertexOffset=0；cmds[1]: firstIndex=3, vertexOffset=3	左侧三角形（顶点 0~2） + 右侧三角形（顶点 3~5），颜色通过 firstInstance 区分
5 混合分离索引缓冲	                    两条命令使用不同的索引缓冲区 (indexBuffer0 和 indexBuffer1)，firstInstance 不同	                        左边红色三角形、右边绿色三角形
6 多实例一次性绘制	                    instanceCount=4, firstInstance=0，一条命令	                                                        屏幕四角同时出现红、绿、蓝、黄四个三角形


》》》》 从 API的参数理解数据
间接命令  →  firstInstance / instanceCount
                ↓
         gl_InstanceIndex = firstInstance + i   (i = 0 .. instanceCount-1)
                ↓
         从 SSBO 读取 instances[gl_InstanceIndex]
                ↓
         获得 offset 和 color
                ↓
         顶点着色器: gl_Position = inPos + offset
                    fragColor = color
*/
int main()
try
{
    // 着色器路径（由构建系统定义）
#ifdef VERT_SHADER_PATH
    std::cout << "VERT_SHADER_PATH: " << VERT_SHADER_PATH << '\n';
#endif
#ifdef FRAG_SHADER_PATH
    std::cout << "FRAG_SHADER_PATH: " << FRAG_SHADER_PATH << '\n';
#endif

    raii_vulkan ctx{};
    surface window{};
    window.setup({.width = WIDTH, .height = HEIGHT}, TITLE);

    constexpr auto APIVERSION = VK_API_VERSION_1_4;
    auto enables = enable_intance_build{}
                       .enableDebugExtension()
                       .enableValidationLayer()
                       .enableSurfaceExtension<surface>();
    enables.check();
    enables.print();

    Instance instance =
        create_instance{}
            .setCreateInfo(
                {.applicationInfo = {.pApplicationName = "Indirect Instanced",
                                     .applicationVersion = vkMakeVersion(1, 0, 0),
                                     .pEngineName = "No Engine",
                                     .engineVersion = vkMakeVersion(1, 0, 0),
                                     .apiVersion = APIVERSION},
                 .enabledLayers = enables.enabledLayers(),
                 .enabledExtensions = enables.enabledExtensions()})
            .build();
    volkLoadInstance(*instance);

    auto debuger = create_debugger{}
                       .setCreateInfo(create_debugger::defaultCreateInfo())
                       .build(instance);

    std::vector<const char *> requiredDeviceExt = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_SPIRV_1_4_EXTENSION_NAME,
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
        VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME,
    };

    structure_chain<VkPhysicalDeviceFeatures2, VkPhysicalDeviceVulkan12Features,
                    VkPhysicalDeviceVulkan13Features,
                    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT>
        featureChain = {{.features = {.multiDrawIndirect = VK_TRUE}},
                        {
                            .scalarBlockLayout = VK_TRUE,
                        },
                        {.synchronization2 = VK_TRUE, .dynamicRendering = VK_TRUE},
                        {.extendedDynamicState = VK_TRUE}};

    auto [id, physical_device] =
        physical_device_selector{instance}
            .requiredDeviceExtension(requiredDeviceExt)
            .requiredProperties([](const VkPhysicalDeviceProperties &props) {
                return props.apiVersion >= VK_API_VERSION_1_3;
            })
            .requiredQueueFamily([](const VkQueueFamilyProperties &qfp) {
                return !!(qfp.queueFlags & VK_QUEUE_GRAPHICS_BIT);
            })
            .requiredFeatures([](const PhysicalDevice &phys) { return true; })
            .select()[0];

    mcs::vulkan::surface auto surface = surface_impl(physical_device, window);
    const uint32_t GRAPHICS_QUEUE_FAMILY_IDX =
        queue_family_index_selector{physical_device}
            .requiredQueueFamily([&](const VkQueueFamilyProperties &qfp, uint32_t idx) {
                return (qfp.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                       physical_device.getSurfaceSupportKHR(idx, *surface);
            })
            .select()[0];

    LogicalDevice device =
        create_logical_device{}
            .setCreateInfo(
                {.pNext = make_pNext(featureChain),
                 .queueCreateInfos = create_logical_device::makeQueueCreateInfos(
                     create_logical_device::queue_create_info{
                         .queueFamilyIndex = GRAPHICS_QUEUE_FAMILY_IDX,
                         .queueCount = 1,
                         .queuePrioritie = 1.0}),
                 .enabledExtensions = requiredDeviceExt})
            .build(physical_device);
    volkLoadDevice(*device);

    requiredDeviceExt.clear();

    const auto GRAPHICS_AND_PRESENT = Queue(
        device, {.queue_family_index = GRAPHICS_QUEUE_FAMILY_IDX, .queue_index = 0});

    // ==================== 场景选择 ====================
    // 0: 基线 - 单命令，左上角红色大三角形
    // 1: 单 buffer + 两次调用（offset 切换） - 右下绿 + 左上蓝
    // 2: 两个独立 buffer - 同上
    // 3: 单次 multiDrawIndirect（drawCount=2） - 同上（需 multiDrawIndirect 特性）
    // 4: vertexOffset / firstIndex 测试 - 左边白色大三角形 + 右边白色大三角形
    // 5: 混合分离索引缓冲 - 左红（indexBuffer0） + 右绿（indexBuffer1）
    // 6: 【新】多实例一次性绘制 - 屏幕四角红、绿、蓝、黄四个三角形
#define TEST_SCENARIO 6

    // ==================== 几何体定义（NDC 大三角形） ====================
    const std::vector<float> tri0_vertices = {
        0.0f,  -0.5f, // 尖顶（NDC y 越小越靠上，-0.5 是上方）
        0.5f,  0.5f,  // 右下（y 大靠下）
        -0.5f, 0.5f,  // 左下
    };
    const std::vector<uint32_t> tri0_indices = {
        0, 1, 2}; // 顺时针（配合管线 frontFace = CLOCKWISE）

    const std::vector<float> tri1_vertices = {
        0.8f, -0.5f, // 尖顶 (上)
        1.3f, 0.5f,  // 右下
        0.3f, 0.5f,  // 左下
    };
    const std::vector<uint32_t> tri1_indices = {0, 1, 2};

    std::vector<float> combined_vertices;
    combined_vertices.insert(combined_vertices.end(), tri0_vertices.begin(),
                             tri0_vertices.end());
    combined_vertices.insert(combined_vertices.end(), tri1_vertices.begin(),
                             tri1_vertices.end());

    // 合并索引（不加偏移，保持局部索引 0,1,2）
    std::vector<uint32_t> combined_indices;
    combined_indices.insert(combined_indices.end(), tri0_indices.begin(),
                            tri0_indices.end());
    combined_indices.insert(combined_indices.end(), tri1_indices.begin(),
                            tri1_indices.end());
    // 现在 combined_indices = {0,1,2, 0,1,2}

    // 4 个预定义实例数据（NDC 偏移 + 颜色），将存入 SSBO
    struct InstanceData
    {
        float offsetX, offsetY;
        float r, g, b;
    };
    const std::vector<InstanceData> instanceData = {
        {.offsetX = -0.5f, .offsetY = 0.5f, .r = 1.0f, .g = 0.0f, .b = 0.0f},  // 0 左下红
        {.offsetX = 0.5f, .offsetY = 0.5f, .r = 0.0f, .g = 1.0f, .b = 0.0f},   // 1 右下绿
        {.offsetX = -0.5f, .offsetY = -0.5f, .r = 0.0f, .g = 0.0f, .b = 1.0f}, // 2 左上蓝
        {.offsetX = 0.5f, .offsetY = -0.5f, .r = 1.0f, .g = 1.0f, .b = 0.0f}   // 3 右上黄
    };

    // ==================== 创建顶点/索引缓冲区 ====================
    VkBuffer vertexBuffer, indexBuffer0, indexBuffer1;
    VkDeviceMemory vertexMemory, indexMemory0, indexMemory1;

    {
        const auto &verts = (TEST_SCENARIO == 4 || TEST_SCENARIO == 5) ? combined_vertices
                                                                       : tri0_vertices;
        VkDeviceSize size = verts.size() * sizeof(float);
        std::tie(vertexBuffer, vertexMemory, std::ignore) =
            createHostBuffer<float>(*device, *physical_device, size,
                                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, verts.data());
    }
    {
        VkDeviceSize size = tri0_indices.size() * sizeof(uint32_t);
        std::tie(indexBuffer0, indexMemory0, std::ignore) = createHostBuffer<uint32_t>(
            *device, *physical_device, size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            tri0_indices.data());
    }
    {
        VkDeviceSize size = tri1_indices.size() * sizeof(uint32_t);
        std::tie(indexBuffer1, indexMemory1, std::ignore) = createHostBuffer<uint32_t>(
            *device, *physical_device, size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            tri1_indices.data());
    }

    VkBuffer combinedIndexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory combinedIndexMemory = VK_NULL_HANDLE;
    if (TEST_SCENARIO == 4)
    {
        VkDeviceSize size = combined_indices.size() * sizeof(uint32_t);
        std::tie(combinedIndexBuffer, combinedIndexMemory, std::ignore) =
            createHostBuffer<uint32_t>(*device, *physical_device, size,
                                       VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                       combined_indices.data());
    }

    // ==================== SSBO（实例数据） ====================
    VkBuffer ssboBuffer;
    VkDeviceMemory ssboMemory;
    {
        VkDeviceSize size = instanceData.size() * sizeof(InstanceData);
        std::tie(ssboBuffer, ssboMemory, std::ignore) = createHostBuffer<InstanceData>(
            *device, *physical_device, size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            instanceData.data());
    }

    // ==================== 描述符集布局和管线布局 ====================
    VkDescriptorSetLayout setLayout;
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        VkDescriptorSetLayoutCreateInfo layoutInfo = {
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;
        vkCreateDescriptorSetLayout(*device, &layoutInfo, nullptr, &setLayout);
    }

    VkPipelineLayout pipelineLayout;
    {
        VkPipelineLayoutCreateInfo layoutInfo = {
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &setLayout;
        // 无推送常量
        layoutInfo.pushConstantRangeCount = 0;
        vkCreatePipelineLayout(*device, &layoutInfo, nullptr, &pipelineLayout);
    }

    // ==================== 描述符池和描述符集 ====================
    VkDescriptorPool descriptorPool;
    {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSize.descriptorCount = 1;
        VkDescriptorPoolCreateInfo poolInfo = {
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        vkCreateDescriptorPool(*device, &poolInfo, nullptr, &descriptorPool);
    }

    VkDescriptorSet descriptorSet;
    {
        VkDescriptorSetAllocateInfo allocInfo = {
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &setLayout;
        vkAllocateDescriptorSets(*device, &allocInfo, &descriptorSet);

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = ssboBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = VK_WHOLE_SIZE;
        VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = descriptorSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(*device, 1, &write, 0, nullptr);
    }

    // ==================== 间接命令缓冲区 ====================
    VkBuffer indirectBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indirectMemory = VK_NULL_HANDLE;
    std::vector<VkBuffer> indirectBuffers;
    std::vector<VkDeviceMemory> indirectMemories;

#if TEST_SCENARIO == 0
    {
        VkDrawIndexedIndirectCommand cmd{};
        cmd.indexCount = 3;
        cmd.instanceCount = 1;
        cmd.firstIndex = 0;
        cmd.vertexOffset = 0;
        cmd.firstInstance = 0;
        std::tie(indirectBuffer, indirectMemory, std::ignore) =
            createHostBuffer<VkDrawIndexedIndirectCommand>(
                *device, *physical_device, sizeof(cmd),
                VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, &cmd);
    }
#elif TEST_SCENARIO == 1
    {
        // 命令0 (offset=0):                                    { indexCount=3, instanceCount=1, firstIndex=0, vertexOffset=0, firstInstance=1 }
        // 命令1 (offset=sizeof(VkDrawIndexedIndirectCommand)): { indexCount=3, instanceCount=1, firstIndex=0, vertexOffset=0, firstInstance=2 }
        VkDrawIndexedIndirectCommand cmds[2] = {};
        cmds[0] = {.indexCount = 3,
                   .instanceCount = 1,
                   .firstIndex = 0,
                   .vertexOffset = 0,
                   .firstInstance = 1}; // 右下绿
        cmds[1] = {.indexCount = 3,
                   .instanceCount = 1,
                   .firstIndex = 0,
                   .vertexOffset = 0,
                   .firstInstance = 2}; // 左上蓝
        std::tie(indirectBuffer, indirectMemory, std::ignore) =
            createHostBuffer<VkDrawIndexedIndirectCommand>(
                *device, *physical_device, sizeof(cmds),
                VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, cmds);
        /*
着色器执行 gl_Position = vec4(inPos + data.offset, 0.0, 1.0)。

命令0（绿色，右下角）：

尖顶 (0, -0.5) + (0.5, 0.5) = (0.5, 0.0) → 屏幕中心偏右

右下 (0.5, 0.5) + (0.5, 0.5) = (1.0, 1.0) → 屏幕右下角

左下 (-0.5, 0.5) + (0.5, 0.5) = (0.0, 1.0) → 屏幕底边中心

整体位于屏幕右下区域，尖顶朝上，绿色。

命令1（蓝色，左上角）：

尖顶 (0, -0.5) + (-0.5, -0.5) = (-0.5, -1.0) → 屏幕左上边缘（Y=-1 是顶部）

右下 (0.5, 0.5) + (-0.5, -0.5) = (0.0, 0.0) → 屏幕中心

左下 (-0.5, 0.5) + (-0.5, -0.5) = (-1.0, 0.0) → 屏幕左边缘中心

整体位于屏幕左上区域，尖顶朝上，蓝色。

总结：
CPU 设置 → indirectBuffer [命令0 | 命令1]
                  ↓ (按 stride 偏移读取)
GPU 命令处理器:
   firstInstance=1 → SSBO[1] (绿, offset=+0.5,+0.5)
   firstInstance=2 → SSBO[2] (蓝, offset=-0.5,-0.5)
   firstIndex=0, vertexOffset=0 → indexBuffer 取 {0,1,2} → vertexBuffer 取 3 个顶点
   顶点着色器: inPos + offset → NDC 坐标 + 输出颜色
*/
    }
#elif TEST_SCENARIO == 2
    {
        indirectBuffers.resize(2);
        indirectMemories.resize(2);
        VkDrawIndexedIndirectCommand cmd = {3, 1, 0, 0, 1};
        std::tie(indirectBuffers[0], indirectMemories[0], std::ignore) =
            createHostBuffer<VkDrawIndexedIndirectCommand>(
                *device, *physical_device, sizeof(cmd),
                VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, &cmd);
        cmd.firstInstance = 2;
        std::tie(indirectBuffers[1], indirectMemories[1], std::ignore) =
            createHostBuffer<VkDrawIndexedIndirectCommand>(
                *device, *physical_device, sizeof(cmd),
                VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, &cmd);
    }
#elif TEST_SCENARIO == 3
    {
        VkDrawIndexedIndirectCommand cmds[2] = {};
        cmds[0] = {3, 1, 0, 0, 1};
        cmds[1] = {3, 1, 0, 0, 2};
        std::tie(indirectBuffer, indirectMemory, std::ignore) =
            createHostBuffer<VkDrawIndexedIndirectCommand>(
                *device, *physical_device, sizeof(cmds),
                VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, cmds);
    }
#elif TEST_SCENARIO == 4
    {
        // 间接命令
        VkDrawIndexedIndirectCommand cmds[2] = {};
        cmds[0] = {.indexCount = 3,
                   .instanceCount = 1,
                   .firstIndex = 0,
                   .vertexOffset = 0,
                   .firstInstance = 0}; // 第一个三角形：索引段0，顶点偏移0
        cmds[1] = {.indexCount = 3,
                   .instanceCount = 1,
                   .firstIndex = 3,
                   .vertexOffset = 3,
                   .firstInstance = 0}; // 第二个三角形：索引段3，顶点偏移3
        cmds[1] = {
            .indexCount = 3,
            .instanceCount = 1,
            .firstIndex =
                0, //NOTE: 可以使用前面那一份的索引。因为值是一样的。我们的顶点不一样
            .vertexOffset = 3,
            .firstInstance = 0}; // 第二个三角形：索引段3，顶点偏移3
        std::tie(indirectBuffer, indirectMemory, std::ignore) =
            createHostBuffer<VkDrawIndexedIndirectCommand>(
                *device, *physical_device, sizeof(cmds),
                VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, cmds);
    }
#elif TEST_SCENARIO == 5
    {
        indirectBuffers.resize(2);
        indirectMemories.resize(2);
        VkDrawIndexedIndirectCommand cmd = {.indexCount = 3,
                                            .instanceCount = 1,
                                            .firstIndex = 0,
                                            .vertexOffset = 0,
                                            .firstInstance = 0};
        std::tie(indirectBuffers[0], indirectMemories[0], std::ignore) =
            createHostBuffer<VkDrawIndexedIndirectCommand>(
                *device, *physical_device, sizeof(cmd),
                VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, &cmd);
        //NOTE:设置 firstInstance = 1 会使三角形变成绿色，这是因为间接命令中的 firstInstance 直接决定了着色器中 gl_InstanceIndex 的起始值，
        // 而着色器通过这个索引从 SSBO 中读取对应的实例数据（包含颜色和偏移）。
        cmd.firstInstance = 1; // 绿色
        std::tie(indirectBuffers[1], indirectMemories[1], std::ignore) =
            createHostBuffer<VkDrawIndexedIndirectCommand>(
                *device, *physical_device, sizeof(cmd),
                VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, &cmd);
    }
#elif TEST_SCENARIO == 6
    {
        // 场景 1 演示的是多条命令选择不同实例，场景 6 演示的是单条命令绘制多个连续实例。
        // 一条命令，绘制 4 个实例，起始实例索引为 0
        VkDrawIndexedIndirectCommand cmd{};
        cmd.indexCount = 3;    // 三角形有3个顶点
        cmd.instanceCount = 4; // 绘制4个实例（红、绿、蓝、黄）
        cmd.firstIndex = 0;
        cmd.vertexOffset = 0;
        cmd.firstInstance = 0; // 从实例数组的第0个开始
        std::tie(indirectBuffer, indirectMemory, std::ignore) =
            createHostBuffer<VkDrawIndexedIndirectCommand>(
                *device, *physical_device, sizeof(cmd),
                VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, &cmd);
    }
#endif

    // ==================== 交换链/管线 ====================
    auto swapchainBuild =
        create_swap_chain{surface, device}
            .setCreateInfo(
                {.changeMinImageCount = [](uint32_t min) noexcept { return min + 1; },
                 .candidateSurfaceFormats = {{.format = VK_FORMAT_B8G8R8A8_SRGB,
                                              .colorSpace =
                                                  VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}},
                 .imageArrayLayers = 1,
                 .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                 .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
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

    using stage_info = create_graphics_pipeline::stage_info;
    auto graphicsPipeline =
        create_graphics_pipeline{}
            .setCreateInfo({
                .pNext = make_pNext(structure_chain<VkPipelineRenderingCreateInfo>{
                    {.colorAttachmentCount = 1,
                     .pColorAttachmentFormats = &swapchainBuild.refImageFormat()}}),
                .stages = create_graphics_pipeline::makeStages(
                    stage_info{.stage = VK_SHADER_STAGE_VERTEX_BIT,
                               .filePath = VERT_SHADER_PATH,
                               .pName = "main"},
                    stage_info{.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                               .filePath = FRAG_SHADER_PATH,
                               .pName = "main"}),
                // 顶点输入：只有 binding=0 的位置
                .vertexInputState =
                    {.vertexBindingDescriptions = {{{0, 2 * sizeof(float),
                                                     VK_VERTEX_INPUT_RATE_VERTEX}}},
                     .vertexAttributeDescriptions = {{{0, 0, VK_FORMAT_R32G32_SFLOAT,
                                                       0}}}},
                .inputAssemblyState = {.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST},
                .viewportState = {.viewports = {VkViewport{}}, .scissors = {VkRect2D{}}},
                .rasterizationState = {.polygonMode = VK_POLYGON_MODE_FILL,
                                       .cullMode = VK_CULL_MODE_NONE,
                                       .frontFace =
                                           VK_FRONT_FACE_COUNTER_CLOCKWISE, //顺时针为正
                                       .lineWidth = 1.0f},
                .multisampleState = {.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT},
                .colorBlendState = {.logicOpEnable = VK_FALSE,
                                    .attachments = {{.blendEnable = VK_FALSE,
                                                     .colorWriteMask =
                                                         VK_COLOR_COMPONENT_R_BIT |
                                                         VK_COLOR_COMPONENT_G_BIT |
                                                         VK_COLOR_COMPONENT_B_BIT |
                                                         VK_COLOR_COMPONENT_A_BIT}}},
                .dynamicState = {.dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                                                   VK_DYNAMIC_STATE_SCISSOR}},
                .layout = pipelineLayout,
            })
            .build(device);

    // 命令池/帧上下文（保持原样）
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

    // 录制命令回调
    const auto recordCommandBuffer = [&](const CommandBufferView &cmdBuffer,
                                         uint32_t imageIndex) {
        VkImage image = swapchain.image(imageIndex);
        VkImageView imageView = swapchain.imageView(imageIndex);
        auto extent = swapchain.imageExtent();

        cmdBuffer.begin({.sType = sType<VkCommandBufferBeginInfo>()});
        my_render::transition_image_layout(
            cmdBuffer, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, {},
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

        VkClearValue clearValue = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 1.0f}}};
        VkRenderingAttachmentInfo colorAttachment = {
            .sType = sType<VkRenderingAttachmentInfo>(),
            .imageView = imageView,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = clearValue};
        cmdBuffer.beginRendering({
            .sType = sType<VkRenderingInfo>(),
            .renderArea = {.offset = {0, 0}, .extent = extent},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachment,
        });

        cmdBuffer.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, *graphicsPipeline);
        cmdBuffer.setViewport(0, std::array<VkViewport, 1>{VkViewport{
                                     .x = 0.0F,
                                     .y = 0.0F,
                                     .width = static_cast<float>(extent.width),
                                     .height = static_cast<float>(extent.height),
                                     .minDepth = 0.0F,
                                     .maxDepth = 1.0F}});
        cmdBuffer.setScissor(
            0, std::array<VkRect2D, 1>{VkRect2D{.offset = {0, 0}, .extent = extent}});

        // 绑定描述符集（SSBO）
        cmdBuffer.bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0,
                                     1, &descriptorSet, 0, nullptr);
        // 绑定顶点缓冲区
        cmdBuffer.bindVertexBuffers(0, 1, vertexBuffer, 0);

        // 根据场景绘制
#if TEST_SCENARIO == 0
        cmdBuffer.bindIndexBuffer(indexBuffer0, 0, VK_INDEX_TYPE_UINT32);
        cmdBuffer.drawIndexedIndirect(indirectBuffer, 0, 1,
                                      sizeof(VkDrawIndexedIndirectCommand));
#elif TEST_SCENARIO == 1
        cmdBuffer.bindIndexBuffer(indexBuffer0, 0, VK_INDEX_TYPE_UINT32);
        for (int i = 0; i < 2; ++i)
            cmdBuffer.drawIndexedIndirect(
                indirectBuffer,
                i * sizeof(VkDrawIndexedIndirectCommand), // 偏移到对应命令
                1,                                        // 每次只取1条命令
                sizeof(VkDrawIndexedIndirectCommand));
#elif TEST_SCENARIO == 2
        cmdBuffer.bindIndexBuffer(indexBuffer0, 0, VK_INDEX_TYPE_UINT32);
        for (int i = 0; i < 2; ++i)
            cmdBuffer.drawIndexedIndirect(indirectBuffers[i], 0, 1,
                                          sizeof(VkDrawIndexedIndirectCommand));
#elif TEST_SCENARIO == 3
        cmdBuffer.bindIndexBuffer(indexBuffer0, 0, VK_INDEX_TYPE_UINT32);
        cmdBuffer.drawIndexedIndirect(indirectBuffer, 0, 2,
                                      sizeof(VkDrawIndexedIndirectCommand));
#elif TEST_SCENARIO == 4
        cmdBuffer.bindIndexBuffer(combinedIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
#if 0
        for (int i = 0; i < 2; ++i)
            cmdBuffer.drawIndexedIndirect(indirectBuffer,
                                          i * sizeof(VkDrawIndexedIndirectCommand), 1,
                                          sizeof(VkDrawIndexedIndirectCommand));
#else
        //NOTE: 2挑指令是连续的。并且启动了扩展
        cmdBuffer.drawIndexedIndirect(indirectBuffer, 0, 2,
                                      sizeof(VkDrawIndexedIndirectCommand));
#endif

#elif TEST_SCENARIO == 5
        cmdBuffer.bindIndexBuffer(indexBuffer0, 0, VK_INDEX_TYPE_UINT32);
        cmdBuffer.drawIndexedIndirect(indirectBuffers[0], 0, 1,
                                      sizeof(VkDrawIndexedIndirectCommand));
        cmdBuffer.bindIndexBuffer(indexBuffer1, 0, VK_INDEX_TYPE_UINT32);
        cmdBuffer.drawIndexedIndirect(indirectBuffers[1], 0, 1,
                                      sizeof(VkDrawIndexedIndirectCommand));
#elif TEST_SCENARIO == 6
        // 使用三角形0的几何体，一次性绘制4个实例
        cmdBuffer.bindIndexBuffer(indexBuffer0, 0, VK_INDEX_TYPE_UINT32);
        cmdBuffer.drawIndexedIndirect(indirectBuffer, 0, 1,
                                      sizeof(VkDrawIndexedIndirectCommand));
#endif

        cmdBuffer.endRendering();
        my_render::transition_image_layout(
            cmdBuffer, image, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, 0,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
        cmdBuffer.end();
    };

    // ==================== 主循环 ====================
    const auto recreateSwapChain = [&]() {
        surface.waitGoodFramebufferSize();
        device.waitIdle();
        swapchain.destroy();
        swapchain = swapchainBuild.rebuild();
    };

    const auto drawFrame = [&]() {
        auto &inFlightFences = frameContext.inFlightFences;
        auto &currentFrame = frameContext.currentFrame;
        auto &presentSemaphore = frameContext.presentCompleteSemaphore;
        auto &semIdx = frameContext.semaphoreIndex;
        auto &renderSemaphore = frameContext.renderFinishedSemaphore;

        while (device.waitForFences(1, inFlightFences[currentFrame], VK_TRUE,
                                    UINT64_MAX) == VK_TIMEOUT)
            ;

        auto [result, imageIndex] =
            swapchain.acquireNextImage(UINT64_MAX, presentSemaphore[semIdx], nullptr);
        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            recreateSwapChain();
            return;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
            throw std::runtime_error("failed to acquire swap chain image!");

        device.resetFences(1, inFlightFences[currentFrame]);

        const auto &cmdBuffer = commandBuffers[currentFrame];
        cmdBuffer.reset({});
        recordCommandBuffer(cmdBuffer, imageIndex);

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        GRAPHICS_AND_PRESENT.submit(1,
                                    {.sType = sType<VkSubmitInfo>(),
                                     .waitSemaphoreCount = 1,
                                     .pWaitSemaphores = &presentSemaphore[semIdx],
                                     .pWaitDstStageMask = &waitStage,
                                     .commandBufferCount = 1,
                                     .pCommandBuffers = &*cmdBuffer,
                                     .signalSemaphoreCount = 1,
                                     .pSignalSemaphores = &renderSemaphore[imageIndex]},
                                    inFlightFences[currentFrame]);

        result = GRAPHICS_AND_PRESENT.presentKHR({
            .sType = sType<VkPresentInfoKHR>(),
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &renderSemaphore[imageIndex],
            .swapchainCount = 1,
            .pSwapchains = &(*swapchain),
            .pImageIndices = &imageIndex,
        });
        if (window.refFramebufferResized() || result == VK_ERROR_OUT_OF_DATE_KHR ||
            result == VK_SUBOPTIMAL_KHR)
        {
            window.refFramebufferResized() = false;
            recreateSwapChain();
        }
        semIdx = (semIdx + 1) % presentSemaphore.size();
        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    };

    // 主循环
    while (window.shouldClose() == 0)
    {
        surface::pollEvents();
        drawFrame();
        {
            // ---------- 增强型帧数统计 ----------
            static auto lastTime = std::chrono::high_resolution_clock::now();
            static int frameCount = 0;
            static int totalFrames = 0;         // 总帧数
            static float accumulatedFPS = 0.0f; // 累计FPS用于平均
            static int avgSamples = 0;          // 平均样本数

            frameCount++;
            totalFrames++;

            auto now = std::chrono::high_resolution_clock::now();
            float elapsed = std::chrono::duration<float>(now - lastTime).count();

            // 每秒更新一次统计
            if (elapsed >= 1.0F)
            {
                float fps = frameCount / elapsed;
                accumulatedFPS += fps;
                avgSamples++;

                // 平均FPS（平滑显示）
                float avgFPS = accumulatedFPS / avgSamples;

                // 帧时间（毫秒）
                float frameTimeMs = (elapsed / frameCount) * 1000.0f;

                std::println(
                    "FPS: {:.1f} (avg: {:.1f}) | FrameTime: {:.2f}ms | Total: {}", fps,
                    avgFPS, frameTimeMs, totalFrames);

                // 重置秒级计数器
                frameCount = 0;
                lastTime = now;
            }
        }
    }
    device.waitIdle();

    // 清理资源
    for (size_t i = 0; i < indirectBuffers.size(); ++i)
    {
        vkDestroyBuffer(*device, indirectBuffers[i], nullptr);
        vkFreeMemory(*device, indirectMemories[i], nullptr);
    }
    if (indirectBuffer)
    {
        vkDestroyBuffer(*device, indirectBuffer, nullptr);
        vkFreeMemory(*device, indirectMemory, nullptr);
    }
    vkDestroyBuffer(*device, vertexBuffer, nullptr);
    vkFreeMemory(*device, vertexMemory, nullptr);
    vkDestroyBuffer(*device, indexBuffer0, nullptr);
    vkFreeMemory(*device, indexMemory0, nullptr);
    vkDestroyBuffer(*device, indexBuffer1, nullptr);
    vkFreeMemory(*device, indexMemory1, nullptr);
    if (combinedIndexBuffer)
    {
        vkDestroyBuffer(*device, combinedIndexBuffer, nullptr);
        vkFreeMemory(*device, combinedIndexMemory, nullptr);
    }
    vkDestroyBuffer(*device, ssboBuffer, nullptr);
    vkFreeMemory(*device, ssboMemory, nullptr);
    vkDestroyDescriptorPool(*device, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(*device, setLayout, nullptr);
    vkDestroyPipelineLayout(*device, pipelineLayout, nullptr);

    std::cout << "main done\n";
    return 0;
}
catch (std::exception &e)
{
    std::println("main catch exception: {}", e.what());
}