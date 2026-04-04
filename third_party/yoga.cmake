# 1. 添加模块
# git submodule add https://gh-proxy.org/https://github.com/facebook/yoga.git third_party/yoga

# 2. 进入 yoga 目录
# cd third_party/yoga

# 查看可以checkout的：
# git tag -l | sort -V

# 3. 切换到指定 tag
# git checkout v3.2.1

# 验证确实在标签上
# git describe --tags

# 回到项目根目录
# cd ../..

# 提交子模块状态的更改
# git add third_party/yoga

# git commit -m "Pin yoga to v3.2.1"

# NOTE: 查看项目的所有子模块
# git submodule status

# add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/yoga)
add_subdirectory(
    ${CMAKE_CURRENT_LIST_DIR}/yoga
    ${CMAKE_CURRENT_BINARY_DIR}/yoga
)
