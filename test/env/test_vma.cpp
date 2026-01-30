
#define VOLK_IMPLEMENTATION
#include <volk.h>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <iostream>
#include <vector>
#include <cstring>

int main()
{
    std::cout << "=== 开始 VMA 集成测试 ===" << std::endl;

    // 1. 初始化 Volk
    if (volkInitialize() != VK_SUCCESS)
    {
        std::cerr << "失败: Volk 初始化失败" << std::endl;
        return -1;
    }
    std::cout << "✓ Volk 初始化成功" << std::endl;

    // 2. 创建 Vulkan 实例和设备 (最小化设置)
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo instInfo = {};
    instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instInfo.pApplicationInfo = &appInfo;

    VkInstance instance;
    if (vkCreateInstance(&instInfo, nullptr, &instance) != VK_SUCCESS)
    {
        std::cerr << "失败: 无法创建 Vulkan 实例" << std::endl;
        return -1;
    }
    volkLoadInstance(instance);
    std::cout << "✓ Vulkan 实例创建成功" << std::endl;

    // 获取物理设备
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices.data());
    VkPhysicalDevice physicalDevice = physicalDevices[0];

    // 创建设备
    VkDeviceQueueCreateInfo queueInfo = {};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = 0; // 假设第一个队列族可用
    queueInfo.queueCount = 1;
    float queuePriority = 1.0f;
    queueInfo.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo deviceInfo = {};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;

    VkDevice device;
    if (vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device) != VK_SUCCESS)
    {
        std::cerr << "失败: 无法创建设备" << std::endl;
        vkDestroyInstance(instance, nullptr);
        return -1;
    }
    volkLoadDevice(device);
    std::cout << "✓ 逻辑设备创建成功" << std::endl;

    // 3. 初始化 VMA
    VmaAllocator allocator = VK_NULL_HANDLE;
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = physicalDevice;
    allocatorInfo.device = device;
    allocatorInfo.instance = instance;
    // 关键：告诉 VMA 使用我们通过 Volk 加载的函数
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_0;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT; // 可选常用标志

    // 设置 Vulkan 函数指针
    VmaVulkanFunctions vulkanFunctions = {};
    vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    vulkanFunctions.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
    vulkanFunctions.vkGetPhysicalDeviceMemoryProperties =
        vkGetPhysicalDeviceMemoryProperties;
    vulkanFunctions.vkAllocateMemory = vkAllocateMemory;
    vulkanFunctions.vkFreeMemory = vkFreeMemory;
    vulkanFunctions.vkMapMemory = vkMapMemory;
    vulkanFunctions.vkUnmapMemory = vkUnmapMemory;
    vulkanFunctions.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges;
    vulkanFunctions.vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges;
    vulkanFunctions.vkBindBufferMemory = vkBindBufferMemory;
    vulkanFunctions.vkBindImageMemory = vkBindImageMemory;
    vulkanFunctions.vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements;
    vulkanFunctions.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements;
    vulkanFunctions.vkCreateBuffer = vkCreateBuffer;
    vulkanFunctions.vkDestroyBuffer = vkDestroyBuffer;
    vulkanFunctions.vkCreateImage = vkCreateImage;
    vulkanFunctions.vkDestroyImage = vkDestroyImage;
    vulkanFunctions.vkCmdCopyBuffer = vkCmdCopyBuffer;

    allocatorInfo.pVulkanFunctions = &vulkanFunctions;

    VkResult result = vmaCreateAllocator(&allocatorInfo, &allocator);
    if (result != VK_SUCCESS || allocator == VK_NULL_HANDLE)
    {
        std::cerr << "失败: 无法创建 VMA 分配器 (错误码: " << result << ")" << std::endl;
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
        return -1;
    }
    std::cout << "✓ VMA 分配器创建成功" << std::endl;

    // 4. 测试 VMA 分配功能 (分配一个小的缓冲区)
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = 1024; // 1KB
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocCreateInfo = {};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU; // 常见用途：从CPU写入，GPU读取

    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo allocInfo = {};

    result = vmaCreateBuffer(allocator, &bufferInfo, &allocCreateInfo, &buffer,
                             &allocation, &allocInfo);
    if (result != VK_SUCCESS)
    {
        std::cerr << "失败: VMA 无法分配缓冲区 (错误码: " << result << ")" << std::endl;
    }
    else
    {
        std::cout << "✓ VMA 缓冲区分配成功" << std::endl;
        std::cout << "  分配大小: " << allocInfo.size << " 字节" << std::endl;
        std::cout << "  内存类型: " << allocInfo.memoryType << std::endl;

        // 可选：映射内存并写入数据以进一步测试
        void *mappedData = nullptr;
        if (vmaMapMemory(allocator, allocation, &mappedData) == VK_SUCCESS)
        {
            memset(mappedData, 0xAB, 64); // 写入一些测试数据
            vmaUnmapMemory(allocator, allocation);
            std::cout << "✓ VMA 内存映射/写入成功" << std::endl;
        }
    }

    // 5. 清理所有资源
    std::cout << "\n=== 开始清理 ===" << std::endl;
    if (buffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(allocator, buffer, allocation);
        std::cout << "✓ 测试缓冲区已销毁" << std::endl;
    }
    if (allocator != VK_NULL_HANDLE)
    {
        vmaDestroyAllocator(allocator);
        std::cout << "✓ VMA 分配器已销毁" << std::endl;
    }
    if (device != VK_NULL_HANDLE)
    {
        vkDestroyDevice(device, nullptr);
        std::cout << "✓ 逻辑设备已销毁" << std::endl;
    }
    if (instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(instance, nullptr);
        std::cout << "✓ Vulkan 实例已销毁" << std::endl;
    }

    std::cout << "\n=== 测试结果 ===" << std::endl;
    if (result == VK_SUCCESS)
    {
        std::cout << "✅ 成功: VMA 已完全集成并工作正常！" << std::endl;
        return 0;
    }
    else
    {
        std::cerr << "❌ 失败: VMA 集成或功能测试未通过。" << std::endl;
        return -1;
    }
}