# 1. 添加模块
# git submodule add https://gh-proxy.org/https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git third_party/vma

# 2. 进入 volk 目录
# cd third_party/vma

# 查看可以checkout的：
# git tag -l | sort -V

# 3. 切换到指定 tag
# git checkout v3.3.0

# 验证确实在标签上
# git describe --tags

# 回到项目根目录
# cd ../..

# 提交子模块状态的更改
# git add third_party/vma

# git commit -m "Pin vma to v3.3.0"

# NOTE: 查看项目的所有子模块
# git submodule status

# add_library(vma INTERFACE)
# target_include_directories(vma SYSTEM INTERFACE "${CMAKE_SOURCE_DIR}/third_party/vma/include")

set(VMA_DIR "${CMAKE_SOURCE_DIR}/third_party/vma/include")
set(VMA_FILES
    "${VMA_DIR}/vk_mem_alloc.h")
add_library(vma STATIC ${VOLK_FILES})
target_compile_features(vma PRIVATE cxx_std_23)
target_include_directories(vma SYSTEM PUBLIC "${VMA_DIR}")
target_link_libraries(vma PRIVATE volk)

# NOTE: 宏的定义顺序 很重要。平台配置应该放到前面
if(WIN32)
    # VK_USE_PLATFORM_WIN32_KHR 会引入很多 windows 的头文件
    target_compile_definitions(vma PRIVATE VK_USE_PLATFORM_WIN32_KHR)
    target_compile_definitions(vma PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# Linux (X11)
elseif(UNIX AND NOT APPLE)
    target_compile_definitions(vma PRIVATE VK_USE_PLATFORM_XLIB_KHR)

# Linux (Wayland)
elseif(UNIX AND NOT APPLE AND USE_WAYLAND) # 如果需要 Wayland
    target_compile_definitions(vma PRIVATE VK_USE_PLATFORM_WAYLAND_KHR)

# macOS
elseif(APPLE)
    target_compile_definitions(vma PRIVATE VK_USE_PLATFORM_MACOS_MVK)

# Android
elseif(ANDROID)
    target_compile_definitions(vma PRIVATE VK_USE_PLATFORM_ANDROID_KHR)

# iOS
elseif(IOS)
    target_compile_definitions(vma PRIVATE VK_USE_PLATFORM_IOS_MVK)
endif()

target_compile_definitions(vma PUBLIC VMA_STATIC_VULKAN_FUNCTIONS=0 VMA_DYNAMIC_VULKAN_FUNCTIONS=0 VMA_IMPLEMENTATION)