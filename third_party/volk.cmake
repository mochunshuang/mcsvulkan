# NOTE: 1. 千万别手动 修改.gitmodules
# git submodule add  https://gh-proxy.org/https://github.com/zeux/volk third_party/volk

# NOTE: ❌ 初始化子模块。 已经初始化了
# git submodule update --init --recursive

# NOTE: ❌ 下面是和checkout 冲突的
# git submodule update --remote

# 2. 进入 volk 目录
# cd third_party/volk

# NOTE:查看可以checkout的： git tag -l | sort -V
# 3. 切换到指定 tag
# git checkout vulkan-sdk-1.4.321.0

# 验证确实在标签上
# git describe --tags

# 回到项目根目录
# cd ../..

# 提交子模块状态的更改
# git add third_party/volk

# git commit -m "Pin volk to vulkan-sdk-1.4.321.0"

# NOTE: 查看项目的所有子模块
# git submodule status

# NOTE: 下面是有用的信息
# 查看当前状态
# git status

# NOTE: 查看当前所在的提交和标签.最近的提交
# git log --oneline -3

# NOTE: 优先静态库。 宏就不暴露了。 INTERFACE 的库会传递宏就恶心了
set(VOLK_DIR "${CMAKE_SOURCE_DIR}/third_party/volk")
set(VOLK_FILES
    "${VOLK_DIR}/volk.c"
    "${VOLK_DIR}/volk.h")
add_library(volk STATIC ${VOLK_FILES})

# c_std_17 需要注意LANGUAGES 得包含C。不然就玩玩。 project("mcsvulkan"
# LANGUAGES CXX C
# )
target_compile_features(volk PRIVATE c_std_17)
target_include_directories(volk SYSTEM PUBLIC "${VOLK_DIR}" ${Vulkan_Include_DIR})

if(WIN32)
    # VK_USE_PLATFORM_WIN32_KHR 会引入很多 windows 的头文件
    target_compile_definitions(volk PRIVATE VK_USE_PLATFORM_WIN32_KHR)

# Linux (X11)
elseif(UNIX AND NOT APPLE)
    target_compile_definitions(volk PRIVATE VK_USE_PLATFORM_XLIB_KHR)

# Linux (Wayland)
elseif(UNIX AND NOT APPLE AND USE_WAYLAND) # 如果需要 Wayland
    target_compile_definitions(volk PRIVATE VK_USE_PLATFORM_WAYLAND_KHR)

# macOS
elseif(APPLE)
    target_compile_definitions(volk PRIVATE VK_USE_PLATFORM_MACOS_MVK)

# Android
elseif(ANDROID)
    target_compile_definitions(volk PRIVATE VK_USE_PLATFORM_ANDROID_KHR)

# iOS
elseif(IOS)
    target_compile_definitions(volk PRIVATE VK_USE_PLATFORM_IOS_MVK)
endif()