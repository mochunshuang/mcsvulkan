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

add_library(vma INTERFACE)
target_include_directories(vma SYSTEM INTERFACE "${CMAKE_SOURCE_DIR}/third_party/vma/include")