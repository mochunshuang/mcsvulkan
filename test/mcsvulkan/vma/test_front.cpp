#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <iostream>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <unordered_map>

// ---------------------------- 常量 ----------------------------
constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;
constexpr int MAX_FRAMES_IN_FLIGHT = 2;

// ---------------------------- 顶点结构 ----------------------------
struct Vertex
{
    glm::vec2 pos; // 屏幕坐标 (x,y)
    glm::vec2 uv;  // 纹理坐标
};

// ---------------------------- 字体字符信息 ----------------------------
struct FontChar
{
    float u0, v0, u1, v1;                               // 归一化 UV
    float planeLeft, planeBottom, planeRight, planeTop; // 字形平面坐标 (EM)
    float advance;                                      // 步进 (EM)
};

// ---------------------------- 全局变量 ----------------------------
GLFWwindow *window = nullptr;

VkInstance instance = VK_NULL_HANDLE;
VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
VkSurfaceKHR surface = VK_NULL_HANDLE;
VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
VkDevice device = VK_NULL_HANDLE;
VkQueue graphicsQueue = VK_NULL_HANDLE;
VkQueue presentQueue = VK_NULL_HANDLE;
uint32_t graphicsFamilyIndex = 0;

VkSwapchainKHR swapchain = VK_NULL_HANDLE;
std::vector<VkImage> swapchainImages;
std::vector<VkImageView> swapchainImageViews;
VkFormat swapchainImageFormat;
VkExtent2D swapchainExtent;

VkRenderPass renderPass = VK_NULL_HANDLE;
VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
VkPipeline pipeline = VK_NULL_HANDLE;

VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

VmaAllocator allocator = VK_NULL_HANDLE;
VkImage fontImage = VK_NULL_HANDLE;
VkImageView fontImageView = VK_NULL_HANDLE;
VkSampler fontSampler = VK_NULL_HANDLE;

VkBuffer vertexBuffer = VK_NULL_HANDLE;
VmaAllocation vertexAlloc = VK_NULL_HANDLE;
VkBuffer indexBuffer = VK_NULL_HANDLE;
VmaAllocation indexAlloc = VK_NULL_HANDLE;
uint32_t indexCount = 0;

std::vector<VkFramebuffer> swapchainFramebuffers;
VkCommandPool commandPool = VK_NULL_HANDLE;
std::vector<VkCommandBuffer> commandBuffers(MAX_FRAMES_IN_FLIGHT);
std::vector<VkSemaphore> imageAvailableSemaphores(MAX_FRAMES_IN_FLIGHT);
std::vector<VkSemaphore> renderFinishedSemaphores(MAX_FRAMES_IN_FLIGHT);
std::vector<VkFence> inFlightFences(MAX_FRAMES_IN_FLIGHT);
int currentFrame = 0;

// ---------------------------- 函数声明 ----------------------------
static void checkVkResult(VkResult err);
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(...);
void createInstance();
void setupDebugMessenger();
void createSurface();
void pickPhysicalDevice();
void createDevice();
void createSwapchain();
void createImageViews();
void createDescriptorSetLayout();
void createRenderPass();
void createPipeline();
void createDescriptorPool();
void createDescriptorSet();
void createFontResources();
void createVertexBuffer();
void createCommandPool();
void createFramebuffers();
void createCommandBuffers();
void createSyncObjects();
void drawFrame();
void cleanup();

// ---------------------------- 工具函数 ----------------------------
static std::vector<char> readFile(const std::string &filename)
{
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open())
        throw std::runtime_error("failed to open file: " + filename);
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    return buffer;
}

// ---------------------------- 主函数 ----------------------------
int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window = glfwCreateWindow(WIDTH, HEIGHT, "MSDF Text Demo", nullptr, nullptr);

    try
    {
        createInstance();
        setupDebugMessenger();
        createSurface();
        pickPhysicalDevice();
        createDevice();
        createSwapchain();
        createImageViews();
        createDescriptorSetLayout();
        createRenderPass();
        createPipeline();
        createFontResources();
        createDescriptorPool();
        createDescriptorSet();
        // createVertexBuffer();
        createCommandPool();
        createFramebuffers();
        createCommandBuffers();
        createSyncObjects();

        while (!glfwWindowShouldClose(window))
        {
            glfwPollEvents();
            drawFrame();
        }
        vkDeviceWaitIdle(device);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    cleanup();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

// ---------------------------- 实现 ----------------------------
static void checkVkResult(VkResult err)
{
    if (err != VK_SUCCESS)
        throw std::runtime_error("Vulkan error: " + std::to_string(err));
}

static VKAPI_ATTR VkBool32 VKAPI_CALL
debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
              VkDebugUtilsMessageTypeFlagsEXT messageType,
              const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void *pUserData)
{
    std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}

void createInstance()
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "MSDF Demo";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    uint32_t glfwExtensionCount = 0;
    const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    std::vector<const char *> extensions(glfwExtensions,
                                         glfwExtensions + glfwExtensionCount);
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    const char *validationLayers[] = {"VK_LAYER_KHRONOS_validation"};
    createInfo.enabledLayerCount = 1;
    createInfo.ppEnabledLayerNames = validationLayers;

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugCreateInfo.pfnUserCallback = debugCallback;
    createInfo.pNext = &debugCreateInfo;

    checkVkResult(vkCreateInstance(&createInfo, nullptr, &instance));
}

void setupDebugMessenger()
{
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance, "vkCreateDebugUtilsMessengerEXT");
    if (func)
    {
        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = debugCallback;
        func(instance, &debugCreateInfo, nullptr, &debugMessenger);
    }
}

void createSurface()
{
    glfwCreateWindowSurface(instance, window, nullptr, &surface);
}

void pickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0)
        throw std::runtime_error("No GPU with Vulkan support");

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    for (const auto &d : devices)
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(d, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
            props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
        {
            uint32_t qfCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(d, &qfCount, nullptr);
            std::vector<VkQueueFamilyProperties> qfProps(qfCount);
            vkGetPhysicalDeviceQueueFamilyProperties(d, &qfCount, qfProps.data());
            for (uint32_t i = 0; i < qfCount; i++)
            {
                VkBool32 presentSupport = false;
                vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface, &presentSupport);
                if (qfProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT && presentSupport)
                {
                    physicalDevice = d;
                    graphicsFamilyIndex = i;
                    break;
                }
            }
        }
        if (physicalDevice != VK_NULL_HANDLE)
            break;
    }
    if (physicalDevice == VK_NULL_HANDLE)
        throw std::runtime_error("No suitable GPU");
}

void createDevice()
{
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = graphicsFamilyIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkPhysicalDeviceFeatures deviceFeatures{};
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    const char *extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &features13;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = extensions;

    checkVkResult(vkCreateDevice(physicalDevice, &createInfo, nullptr, &device));
    vkGetDeviceQueue(device, graphicsFamilyIndex, 0, &graphicsQueue);
    vkGetDeviceQueue(device, graphicsFamilyIndex, 0, &presentQueue); // same queue
}

void createSwapchain()
{
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &caps);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount,
                                         formats.data());
    VkSurfaceFormatKHR surfaceFormat = formats[0];
    for (const auto &f : formats)
    {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            surfaceFormat = f;
            break;
        }
    }

    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount,
                                              nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount,
                                              presentModes.data());
    for (auto m : presentModes)
    {
        if (m == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            presentMode = m;
            break;
        }
    }

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX)
    {
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
    }
    swapchainExtent = extent;

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = caps.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    checkVkResult(vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain));
    swapchainImageFormat = surfaceFormat.format;

    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
    swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());
}

void createImageViews()
{
    swapchainImageViews.resize(swapchainImages.size());
    for (size_t i = 0; i < swapchainImages.size(); i++)
    {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapchainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = swapchainImageFormat;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;
        checkVkResult(
            vkCreateImageView(device, &createInfo, nullptr, &swapchainImageViews[i]));
    }
}

void createDescriptorSetLayout()
{
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    createInfo.bindingCount = 1;
    createInfo.pBindings = &binding;
    checkVkResult(
        vkCreateDescriptorSetLayout(device, &createInfo, nullptr, &descriptorSetLayout));
}

void createRenderPass()
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = 1;
    createInfo.pAttachments = &colorAttachment;
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = 1;
    createInfo.pDependencies = &dependency;
    checkVkResult(vkCreateRenderPass(device, &createInfo, nullptr, &renderPass));
}

void createPipeline()
{
    auto vertCode = readFile("vert.spv");
    auto fragCode = readFile("frag.spv");

    VkShaderModuleCreateInfo shaderInfo{};
    shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderInfo.codeSize = vertCode.size();
    shaderInfo.pCode = reinterpret_cast<const uint32_t *>(vertCode.data());
    VkShaderModule vertModule;
    checkVkResult(vkCreateShaderModule(device, &shaderInfo, nullptr, &vertModule));
    shaderInfo.codeSize = fragCode.size();
    shaderInfo.pCode = reinterpret_cast<const uint32_t *>(fragCode.data());
    VkShaderModule fragModule;
    checkVkResult(vkCreateShaderModule(device, &shaderInfo, nullptr, &fragModule));

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName = "main";

    VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

    // Vertex input
    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(Vertex);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrDesc[2]{};
    attrDesc[0].location = 0;
    attrDesc[0].binding = 0;
    attrDesc[0].format = VK_FORMAT_R32G32_SFLOAT;
    attrDesc[0].offset = offsetof(Vertex, pos);
    attrDesc[1].location = 1;
    attrDesc[1].binding = 0;
    attrDesc[1].format = VK_FORMAT_R32G32_SFLOAT;
    attrDesc[1].offset = offsetof(Vertex, uv);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = 2;
    vertexInput.pVertexAttributeDescriptions = attrDesc;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)swapchainExtent.width;
    viewport.height = (float)swapchainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchainExtent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &blend;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout;
    checkVkResult(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout));

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    checkVkResult(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                            nullptr, &pipeline));

    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);
}

void createFontResources()
{
    // 1. 解析 JSON
    std::ifstream f("font/font.json");
    if (!f.is_open())
        throw std::runtime_error("Cannot open font.json");
    json jdata = json::parse(f);

    float atlasWidth = jdata["atlas"]["width"];
    float atlasHeight = jdata["atlas"]["height"];

    std::unordered_map<uint32_t, FontChar> fontChars;
    for (auto &glyph : jdata["glyphs"])
    {
        FontChar fc;
        float left = glyph["atlasBounds"]["left"];
        float bottom = glyph["atlasBounds"]["bottom"];
        float right = glyph["atlasBounds"]["right"];
        float top = glyph["atlasBounds"]["top"];

        fc.u0 = left / atlasWidth;
        fc.v0 = top / atlasHeight; // 纹理顶部（较小 y）
        fc.u1 = right / atlasWidth;
        fc.v1 = bottom / atlasHeight; // 纹理底部（较大 y）

        fc.planeLeft = glyph["planeBounds"]["left"];
        fc.planeBottom = glyph["planeBounds"]["bottom"];
        fc.planeRight = glyph["planeBounds"]["right"];
        fc.planeTop = glyph["planeBounds"]["top"];
        fc.advance = glyph["advance"];

        fontChars[glyph["unicode"]] = fc;
    }

    // 2. 生成顶点数据（硬编码 "你好"）
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    float posx = 0.0f;
    float baselineY = HEIGHT / 2.0f; // 基线垂直居中
    float targetHeight = 48.0f;      // 和生成图集时用的 size 一致

    std::vector<uint32_t> codepoints = {0x4F60, 0x597D}; // 你 好
    for (uint32_t cp : codepoints)
    {
        auto it = fontChars.find(cp);
        if (it == fontChars.end())
            continue;
        const FontChar &fc = it->second;

        float emHeight = fc.planeTop - fc.planeBottom;
        float scale = targetHeight / emHeight;

        float x0 = posx + fc.planeLeft * scale;
        float x1 = posx + fc.planeRight * scale;
        float y0 = baselineY - fc.planeTop * scale;    // 顶部（屏幕 Y 较小）
        float y1 = baselineY - fc.planeBottom * scale; // 底部（屏幕 Y 较大）

        vertices.push_back({{x1, y0}, {fc.u1, fc.v1}}); // 右上
        vertices.push_back({{x0, y0}, {fc.u0, fc.v1}}); // 左上
        vertices.push_back({{x0, y1}, {fc.u0, fc.v0}}); // 左下
        vertices.push_back({{x1, y1}, {fc.u1, fc.v0}}); // 右下

        uint32_t base = vertices.size() - 4;
        indices.insert(indices.end(),
                       {base, base + 1, base + 2, base + 2, base + 3, base});
        posx += fc.advance * scale;
    }

    // 水平居中
    float totalWidth = posx;
    for (auto &v : vertices)
    {
        v.pos.x += (WIDTH / 2.0f) - totalWidth / 2.0f;
    }

    indexCount = static_cast<uint32_t>(indices.size());

    // 3. 创建 VMA 分配器
    VmaAllocatorCreateInfo allocInfo{};
    allocInfo.physicalDevice = physicalDevice;
    allocInfo.device = device;
    allocInfo.instance = instance;
    allocInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    checkVkResult(vmaCreateAllocator(&allocInfo, &allocator));

    // 4. 上传顶点/索引缓冲
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = vertices.size() * sizeof(Vertex);
    bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocCreateInfo{};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    checkVkResult(vmaCreateBuffer(allocator, &bufInfo, &allocCreateInfo, &vertexBuffer,
                                  &vertexAlloc, nullptr));

    bufInfo.size = indices.size() * sizeof(uint32_t);
    bufInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    checkVkResult(vmaCreateBuffer(allocator, &bufInfo, &allocCreateInfo, &indexBuffer,
                                  &indexAlloc, nullptr));

    // 创建暂存缓冲并上传
    VkBuffer stagingBuffer;
    VmaAllocation stagingAlloc;
    bufInfo.size = vertices.size() * sizeof(Vertex);
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    checkVkResult(vmaCreateBuffer(allocator, &bufInfo, &allocCreateInfo, &stagingBuffer,
                                  &stagingAlloc, nullptr));

    void *data;
    vmaMapMemory(allocator, stagingAlloc, &data);
    memcpy(data, vertices.data(), vertices.size() * sizeof(Vertex));
    vmaUnmapMemory(allocator, stagingAlloc);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = graphicsFamilyIndex;
    VkCommandPool tempPool;
    vkCreateCommandPool(device, &poolInfo, nullptr, &tempPool);
    VkCommandBufferAllocateInfo allocCmd{};
    allocCmd.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocCmd.commandPool = tempPool;
    allocCmd.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocCmd.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &allocCmd, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkBufferCopy copy{};
    copy.size = vertices.size() * sizeof(Vertex);
    vkCmdCopyBuffer(cmd, stagingBuffer, vertexBuffer, 1, &copy);

    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkQueueSubmit(graphicsQueue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkDestroyCommandPool(device, tempPool, nullptr);
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);

    // 上传索引缓冲（类似，略）
    bufInfo.size = indices.size() * sizeof(uint32_t);
    vmaCreateBuffer(allocator, &bufInfo, &allocCreateInfo, &stagingBuffer, &stagingAlloc,
                    nullptr);
    vmaMapMemory(allocator, stagingAlloc, &data);
    memcpy(data, indices.data(), indices.size() * sizeof(uint32_t));
    vmaUnmapMemory(allocator, stagingAlloc);
    vkCreateCommandPool(device, &poolInfo, nullptr, &tempPool);
    vkAllocateCommandBuffers(device, &allocCmd, &cmd);
    vkBeginCommandBuffer(cmd, &beginInfo);
    copy.size = indices.size() * sizeof(uint32_t);
    vkCmdCopyBuffer(cmd, stagingBuffer, indexBuffer, 1, &copy);
    vkEndCommandBuffer(cmd);
    vkQueueSubmit(graphicsQueue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    vkDestroyCommandPool(device, tempPool, nullptr);
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);

    // 5. 加载纹理图片
    int texWidth, texHeight, channels;
    stbi_uc *pixels = stbi_load("font/font.png", &texWidth, &texHeight, &channels, 4);
    if (!pixels)
        throw std::runtime_error("Failed to load texture image");

    VkDeviceSize imageSize = texWidth * texHeight * 4;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {(uint32_t)texWidth, (uint32_t)texHeight, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo imgAllocInfo{};
    imgAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    vmaCreateImage(allocator, &imageInfo, &imgAllocInfo, &fontImage, nullptr, nullptr);

    // 创建暂存缓冲并上传图片
    vmaCreateBuffer(allocator, &bufInfo, &allocCreateInfo, &stagingBuffer, &stagingAlloc,
                    nullptr);
    vmaMapMemory(allocator, stagingAlloc, &data);
    memcpy(data, pixels, imageSize);
    vmaUnmapMemory(allocator, stagingAlloc);
    stbi_image_free(pixels);

    vkCreateCommandPool(device, &poolInfo, nullptr, &tempPool);
    vkAllocateCommandBuffers(device, &allocCmd, &cmd);
    vkBeginCommandBuffer(cmd, &beginInfo);

    // 布局转换 UNDEFINED -> TRANSFER_DST_OPTIMAL
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = fontImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &barrier);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {(uint32_t)texWidth, (uint32_t)texHeight, 1};
    vkCmdCopyBufferToImage(cmd, stagingBuffer, fontImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                         1, &barrier);

    vkEndCommandBuffer(cmd);
    vkQueueSubmit(graphicsQueue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    vkDestroyCommandPool(device, tempPool, nullptr);
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);

    // 创建 ImageView
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = fontImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    checkVkResult(vkCreateImageView(device, &viewInfo, nullptr, &fontImageView));

    // 创建采样器
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 1.0f;
    checkVkResult(vkCreateSampler(device, &samplerInfo, nullptr, &fontSampler));
}

void createDescriptorPool()
{
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    checkVkResult(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool));
}

void createDescriptorSet()
{
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;
    checkVkResult(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = fontSampler;
    imageInfo.imageView = fontImageView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void createCommandPool()
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = graphicsFamilyIndex;
    checkVkResult(vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool));
}

void createFramebuffers()
{
    swapchainFramebuffers.resize(swapchainImageViews.size());
    for (size_t i = 0; i < swapchainImageViews.size(); i++)
    {
        VkFramebufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = renderPass;
        info.attachmentCount = 1;
        info.pAttachments = &swapchainImageViews[i];
        info.width = swapchainExtent.width;
        info.height = swapchainExtent.height;
        info.layers = 1;
        checkVkResult(
            vkCreateFramebuffer(device, &info, nullptr, &swapchainFramebuffers[i]));
    }
}

void createCommandBuffers()
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
    checkVkResult(vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()));
}

void createSyncObjects()
{
    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        checkVkResult(
            vkCreateSemaphore(device, &semInfo, nullptr, &imageAvailableSemaphores[i]));
        checkVkResult(
            vkCreateSemaphore(device, &semInfo, nullptr, &renderFinishedSemaphores[i]));
        checkVkResult(vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]));
    }
}

void drawFrame()
{
    vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &inFlightFences[currentFrame]);

    uint32_t imageIndex;
    vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                          imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE,
                          &imageIndex);

    vkResetCommandBuffer(commandBuffers[currentFrame], 0);
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(commandBuffers[currentFrame], &beginInfo);

    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapchainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapchainExtent;
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;
    vkCmdBeginRenderPass(commandBuffers[currentFrame], &renderPassInfo,
                         VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipeline);
    vkCmdBindDescriptorSets(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

    VkBuffer vertexBuffers[] = {vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffers[currentFrame], 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffers[currentFrame], indexBuffer, 0,
                         VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffers[currentFrame], indexCount, 1, 0, 0, 0);

    vkCmdEndRenderPass(commandBuffers[currentFrame]);
    vkEndCommandBuffer(commandBuffers[currentFrame]);

    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &imageAvailableSemaphores[currentFrame];
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[currentFrame];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinishedSemaphores[currentFrame];

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]);

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphores[currentFrame];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;
    vkQueuePresentKHR(presentQueue, &presentInfo);

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void cleanup()
{
    for (size_t i = 0; i < swapchainFramebuffers.size(); i++)
    {
        vkDestroyFramebuffer(device, swapchainFramebuffers[i], nullptr);
    }
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
        vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
        vkDestroyFence(device, inFlightFences[i], nullptr);
    }
    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyRenderPass(device, renderPass, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroySampler(device, fontSampler, nullptr);
    vkDestroyImageView(device, fontImageView, nullptr);
    vmaDestroyImage(allocator, fontImage, nullptr);
    vmaDestroyBuffer(allocator, vertexBuffer, vertexAlloc);
    vmaDestroyBuffer(allocator, indexBuffer, indexAlloc);
    vmaDestroyAllocator(allocator);
    for (auto iv : swapchainImageViews)
        vkDestroyImageView(device, iv, nullptr);
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroyDevice(device, nullptr);
    if (debugMessenger)
    {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func)
            func(instance, debugMessenger, nullptr);
    }
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
}