# 1. 添加模块
# git submodule add https://gh-proxy.org/https://github.com/Tehreer/SheenBidi.git third_party/SheenBidi

# 2. 进入 目录
# cd third_party/SheenBidi

# 查看可以checkout的：
# git tag -l | sort -V

# 3. 切换到指定 tag
# git checkout v3.0.0

# 验证确实在标签上
# git describe --tags

# 回到项目根目录
# cd ../..

# 提交子模块状态的更改
# git add third_party/SheenBidi

# git commit -m "Pin SheenBidi to v3.0.0"

# NOTE: 查看项目的所有子模块
# git submodule status

set(SB_CONFIG_UNITY OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/SheenBidi)