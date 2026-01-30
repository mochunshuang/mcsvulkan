# NOTE: 去掉子项目没必要的target
# config glfw-3.3-stable
option(GLFW_BUILD_DOCS OFF)
option(GLFW_BUILD_TESTS OFF)
option(GLFW_BUILD_EXAMPLES OFF)
option(GLFW_BUILD_TESTS OFF)
option(GLFW_INSTALL OFF)
option(BUILD_SHARED_LIBS ON)

add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/glfw-3.3-stable)

# rm -r -Force third_party/glfw-3.3 就能添加代码
# 子模块依赖添加
# NOTE: 3.3-stable 是tag。得实现确定的
# git submodule add -b 3.3-stable https://github.com/glfw/glfw.git third_party/glfw-3.3-stable
# NOTE: 代理的更好
# git submodule add -b 3.3-stable https://gh-proxy.com/https://github.com/glfw/glfw.git third_party/glfw-3.3-stable

# NOTE: 删除
# 1. 删除本地目录
# rm-r -Force third_party/glfw-3.3-stable

# 2. 从 .gitmodules 中删除配置（如果存在）
# git config -f .gitmodules --remove-section submodule.third_party/glfw-3.3-stable

# 3. 从主仓库配置中删除
# git config --remove-section submodule.third_party/glfw-3.3-stable

# 4. 删除 .git/modules 中的缓存
# rm -r -Force .git/modules/third_party/glfw-3.3-stable
# 5. 从索引中删除（如果存在）
# git rm --cached third_party/glfw-3.3-stable