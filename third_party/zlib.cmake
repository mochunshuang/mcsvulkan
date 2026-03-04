# 1. 添加模块
# git submodule add  https://gh-proxy.org/https://github.com/madler/zlib.git third_party/zlib

# 2. 进入 目录
# cd third_party/zlib

# 查看可以checkout的：
# git tag -l | sort -V

# 3. 切换到指定 tag
# git checkout v1.3.2

# 验证确实在标签上
# git describe --tags

# 回到项目根目录
# cd ../..

# 提交子模块状态的更改
# git add third_party/zlib

# git commit -m "Pin zlib to v1.3.2"

# git submodule status

# 1. 配置 zlib 构建选项
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE) # 不安装

# 2. 添加 zlib 子目录
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib)

add_library(ZLIB::ZLIB ALIAS zlibstatic)

# ============欺骗 find_package 开始=====================
# 手动设置 find_package(ZLIB) 所需的所有变量，模拟查找成功
# 这些变量是 CMake 内置的 ZLIB 查找模块会检查的核心变量
set(ZLIB_FOUND TRUE) # 标记找到 ZLIB
set(ZLIB_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib) # zlib 头文件目录
set(ZLIB_LIBRARIES ZLIB::ZLIB) # zlib 库目标

set(ZLIB_VERSION_MAJOR 1)
set(ZLIB_VERSION_MINOR 3)
set(ZLIB_VERSION_PATCH 2)
set(ZLIB_VERSION_STRING "${ZLIB_VERSION_MAJOR}.${ZLIB_VERSION_MINOR}.${ZLIB_VERSION_PATCH}") # 可选：指定 zlib 版本

# ============欺骗 find_package 结束=====================

# 禁用 CMake 内置的 ZLIB 查找逻辑（避免冲突）
set(CMAKE_DISABLE_FIND_PACKAGE_ZLIB OFF)
find_package(ZLIB REQUIRED)
