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

add_library(volk INTERFACE)
target_include_directories(volk SYSTEM INTERFACE "${CMAKE_SOURCE_DIR}/third_party/volk")