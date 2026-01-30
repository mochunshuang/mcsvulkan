// volk_device_table_test.cpp - 单文件测试 volkLoadDeviceTable
// 编译：cl volk_device_table_test.cpp /I"C:\VulkanSDK\1.3.275.0\Include"
// /Fe:device_test.exe

// 1. 启用 Volk 实现
#define VOLK_IMPLEMENTATION

// // 2. 关键：告诉 Volk 在哪里找 Vulkan 头文件
// #define VOLK_VULKAN_H_PATH <vulkan/vulkan.h>

// // 3. 包含 Volk
// #include "volk.h"

#define VOLK_IMPLEMENTATION
#include <volk.h>

// 4. Windows 相关
#include <stdio.h>
#include <vector>
#include <string>

// ==================== 自定义 Device 类 ====================
class MyVulkanDevice
{
  private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    uint32_t m_queueFamilyIndex = 0;

    // 关键：每个 Device 实例有独立的函数表
    VolkDeviceTable m_funcs = {};

  public:
    // 配置信息
    struct CreateInfo
    {
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        std::vector<const char *> extensions = {};
        std::vector<const char *> layers = {};
        bool enableGraphics = true;
        bool enableCompute = false;
        void *pNext = nullptr;
    };

    // 创建 Device
    bool create(const CreateInfo &info)
    {
        if (!info.physicalDevice)
        {
            printf("[错误] 物理设备为空\n");
            return false;
        }

        // 1. 查找队列族
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(info.physicalDevice, &queueFamilyCount,
                                                 nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(info.physicalDevice, &queueFamilyCount,
                                                 queueFamilies.data());

        // 寻找图形队列
        m_queueFamilyIndex = UINT32_MAX;
        for (uint32_t i = 0; i < queueFamilyCount; i++)
        {
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                m_queueFamilyIndex = i;
                break;
            }
        }

        if (m_queueFamilyIndex == UINT32_MAX)
        {
            printf("[错误] 没有找到图形队列\n");
            return false;
        }

        // 2. 创建设备
        float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueCreateInfo = {};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = m_queueFamilyIndex;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        VkPhysicalDeviceFeatures deviceFeatures = {};

        VkDeviceCreateInfo deviceCreateInfo = {};
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceCreateInfo.queueCreateInfoCount = 1;
        deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
        deviceCreateInfo.pEnabledFeatures = &deviceFeatures;

        // 设置扩展
        if (!info.extensions.empty())
        {
            deviceCreateInfo.enabledExtensionCount =
                static_cast<uint32_t>(info.extensions.size());
            deviceCreateInfo.ppEnabledExtensionNames = info.extensions.data();
        }

        // 设置层
        if (!info.layers.empty())
        {
            deviceCreateInfo.enabledLayerCount =
                static_cast<uint32_t>(info.layers.size());
            deviceCreateInfo.ppEnabledLayerNames = info.layers.data();
        }

        deviceCreateInfo.pNext = info.pNext;

        if (vkCreateDevice(info.physicalDevice, &deviceCreateInfo, nullptr, &m_device) !=
            VK_SUCCESS)
        {
            printf("[错误] 创建设备失败\n");
            return false;
        }

        // 3. 关键：加载设备专用函数表
        volkLoadDeviceTable(&m_funcs, m_device);

        // 4. 获取队列
        m_funcs.vkGetDeviceQueue(m_device, m_queueFamilyIndex, 0, &m_graphicsQueue);

        printf("[成功] 自定义 Device 创建完成\n");
        printf("       - 函数表地址: %p\n", &m_funcs);
        printf("       - 设备句柄: %p\n", m_device);
        printf("       - 队列族索引: %u\n", m_queueFamilyIndex);

        return true;
    }

    // 使用设备函数（通过函数表）
    void beginCommandBuffer(VkCommandBuffer cmdBuffer)
    {
        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = 0;

        // 使用设备专属的函数表调用
        m_funcs.vkBeginCommandBuffer(cmdBuffer, &beginInfo);
    }

    void cmdDraw(VkCommandBuffer cmdBuffer, uint32_t vertexCount,
                 uint32_t instanceCount = 1)
    {
        m_funcs.vkCmdDraw(cmdBuffer, vertexCount, instanceCount, 0, 0);
    }

    void endCommandBuffer(VkCommandBuffer cmdBuffer)
    {
        m_funcs.vkEndCommandBuffer(cmdBuffer);
    }

    // 创建命令池
    VkCommandPool createCommandPool(VkCommandPoolCreateFlags flags = 0)
    {
        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = m_queueFamilyIndex;
        poolInfo.flags = flags;

        VkCommandPool commandPool;
        if (m_funcs.vkCreateCommandPool(m_device, &poolInfo, nullptr, &commandPool) ==
            VK_SUCCESS)
        {
            return commandPool;
        }
        return VK_NULL_HANDLE;
    }

    // 获取函数表（供外部使用）
    VolkDeviceTable *getFunctionTable()
    {
        return &m_funcs;
    }

    // 获取设备句柄
    VkDevice getDevice() const
    {
        return m_device;
    }
    VkQueue getGraphicsQueue() const
    {
        return m_graphicsQueue;
    }

    // 销毁
    void destroy()
    {
        if (m_device)
        {
            // 使用函数表中的销毁函数
            m_funcs.vkDeviceWaitIdle(m_device);
            m_funcs.vkDestroyDevice(m_device, nullptr);
            m_device = VK_NULL_HANDLE;

            // 清空函数表
            memset(&m_funcs, 0, sizeof(VolkDeviceTable));
        }
    }
};

// ==================== 测试函数 ====================
void testSingleDevice()
{
    printf("\n=== 测试 1：单个 Device ===\n");

    // 初始化 Volk
    if (volkInitialize() != VK_SUCCESS)
    {
        printf("Volk 初始化失败\n");
        return;
    }

    // 创建实例
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Device Table Test";
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instInfo = {};
    instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instInfo.pApplicationInfo = &appInfo;

    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&instInfo, nullptr, &instance) != VK_SUCCESS)
    {
        printf("创建实例失败\n");
        return;
    }

    volkLoadInstance(instance);

    // 枚举物理设备
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    if (deviceCount == 0)
    {
        printf("没有找到 Vulkan 设备\n");
        vkDestroyInstance(instance, nullptr);
        return;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    // 使用第一个设备
    MyVulkanDevice myDevice;
    MyVulkanDevice::CreateInfo devInfo;
    devInfo.physicalDevice = devices[0];

    // 添加交换链扩展（如果有）
    devInfo.extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    if (myDevice.create(devInfo))
    {
        printf("✓ Device 创建成功\n");

        // 测试函数调用
        VkCommandPool pool = myDevice.createCommandPool();
        if (pool)
        {
            printf("✓ 命令池创建成功\n");

            // 可以使用函数表直接调用
            VolkDeviceTable *table = myDevice.getFunctionTable();
            table->vkDestroyCommandPool(myDevice.getDevice(), pool, nullptr);
            printf("✓ 命令池销毁成功\n");
        }

        myDevice.destroy();
        printf("✓ Device 销毁成功\n");
    }

    vkDestroyInstance(instance, nullptr);
}

void testMultipleDevices()
{
    printf("\n=== 测试 2：多个 Device（模拟多 GPU）===\n");

    // 初始化
    if (volkInitialize() != VK_SUCCESS)
        return;

    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instInfo = {};
    instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instInfo.pApplicationInfo = &appInfo;

    VkInstance instance;
    if (vkCreateInstance(&instInfo, nullptr, &instance) != VK_SUCCESS)
        return;

    volkLoadInstance(instance);

    // 获取所有 GPU
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    std::vector<VkPhysicalDevice> gpus(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, gpus.data());

    // 为每个 GPU 创建独立的 Device
    std::vector<MyVulkanDevice> devices(gpus.size());

    for (size_t i = 0; i < gpus.size(); i++)
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(gpus[i], &props);

        printf("\n创建 Device %zu: %s\n", i, props.deviceName);

        MyVulkanDevice::CreateInfo info;
        info.physicalDevice = gpus[i];

        if (devices[i].create(info))
        {
            printf("  ✓ 成功！函数表地址: %p\n", devices[i].getFunctionTable());

            // 验证每个 Device 的函数表是独立的
            VolkDeviceTable *table = devices[i].getFunctionTable();
            printf("  ✓ vkCmdDraw 指针: %p\n", table->vkCmdDraw);

            // 创建测试用的命令池
            VkCommandPool pool = devices[i].createCommandPool();
            if (pool)
            {
                table->vkDestroyCommandPool(devices[i].getDevice(), pool, nullptr);
            }
        }
    }

    // 同时使用多个 Device（关键优势）
    if (devices.size() > 1)
    {
        printf("\n=== 演示同时使用多个 Device ===\n");

        // 为每个设备创建命令池
        std::vector<VkCommandPool> pools;
        for (auto &dev : devices)
        {
            if (dev.getDevice())
            {
                VkCommandPool pool = dev.createCommandPool();
                if (pool)
                {
                    pools.push_back(pool);
                    printf("为设备 %p 创建命令池 %p\n", dev.getDevice(), pool);
                }
            }
        }

        // 清理
        for (size_t i = 0; i < devices.size(); i++)
        {
            if (i < pools.size())
            {
                VolkDeviceTable *table = devices[i].getFunctionTable();
                table->vkDestroyCommandPool(devices[i].getDevice(), pools[i], nullptr);
            }
            devices[i].destroy();
        }
    }

    vkDestroyInstance(instance, nullptr);
}

// ==================== 主函数 ====================
int main()
{
    printf("=== Volk Device Table 测试 ===\n");
    printf("编译时间: %s %s\n", __DATE__, __TIME__);

    // 测试 1：单个设备
    testSingleDevice();

    // 测试 2：多个设备
    testMultipleDevices();

    printf("\n=== 测试完成 ===\n");
    printf("\n总结:\n");
    printf("1. volkLoadDeviceTable 为每个 VkDevice 创建独立的函数表\n");
    printf("2. 函数调用通过 table->vkFunctionName() 进行\n");
    printf("3. 完美支持多 GPU 场景\n");
    printf("4. 性能最佳（直接调用驱动函数）\n");

    return 0;
}