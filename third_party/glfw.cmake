# NOTE: 去掉子项目没必要的target
# config glfw-3.3-stable
option(GLFW_BUILD_DOCS OFF)
option(GLFW_BUILD_TESTS OFF)
option(GLFW_BUILD_EXAMPLES OFF)
option(GLFW_BUILD_TESTS OFF)
option(GLFW_INSTALL OFF)
option(BUILD_SHARED_LIBS ON)
add_subdirectory(
    ${CMAKE_CURRENT_LIST_DIR}/glfw
    ${CMAKE_CURRENT_BINARY_DIR}/glfw
)

# rm -r -Force third_party/glfw-3.3 就能添加代码
# 子模块依赖添加
# NOTE: 3.3-stable 是tag。得实现确定的
# git submodule add -b 3.3-stable https://github.com/glfw/glfw.git third_party/glfw-3.3-stable
# NOTE: 代理的更好
# git submodule add -b 3.3-stable https://gh-proxy.com/https://github.com/glfw/glfw.git third_party/glfw-3.3-stable

# NOTE: 删除
# 1. 删除本地目录
# rm -r -Force third_party/glfw-3.3-stable

# 2. 从 .gitmodules 中删除配置（如果存在）
# git config -f .gitmodules --remove-section submodule.third_party/glfw-3.3-stable

# 3. 从主仓库配置中删除
# git config --remove-section submodule.third_party/glfw-3.3-stable

# 4. 删除 .git/modules 中的缓存
# rm -r -Force .git/modules/third_party/glfw-3.3-stable
# 5. 从索引中删除（如果存在）
# git rm --cached third_party/glfw-3.3-stable

# NOTE: 更新到 3.4
# 1. 添加模块
# git submodule add  https://gh-proxy.com/https://github.com/glfw/glfw.git third_party/glfw

# 2. 进入 目录
# cd third_party/glfw

# 查看可以checkout的：
# git tag -l | sort -V

# 3. 切换到指定 tag
# git checkout 3.4

# 验证确实在标签上
# git describe --tags

# 回到项目根目录
# cd ../..

# 提交子模块状态的更改
# git add third_party/glfw

# git commit -m "Pin glfw to 3.4"

# git submodule status