# 1. 添加模块
# git submodule add https://gh-proxy.org/https://github.com/elalish/manifold.git third_party/manifold

# 2. 进入 manifold 目录
# cd third_party/manifold

# 查看可以checkout的：
# git tag -l | sort -V

# 3. 切换到指定 tag
# git checkout v3.4.1

# 验证确实在标签上
# git describe --tags

# 回到项目根目录
# cd ../..

# 提交子模块状态的更改
# git add third_party/manifold

# git commit -m "Pin manifold to v3.4.1"

# NOTE: 查看项目的所有子模块
# git submodule status

# Primary user facing options
# NOTE: 官方强烈推荐
set(MANIFOLD_PAR ON CACHE BOOL "" FORCE)
set(MANIFOLD_CROSS_SECTION ON CACHE BOOL "" FORCE)

# option(MANIFOLD_DEBUG "Enable debug tracing/timing" OFF)
# option(MANIFOLD_ASSERT "Enable assertions - requires MANIFOLD_DEBUG" OFF)
# option(MANIFOLD_STRICT "Treat compile warnings as fatal build errors" OFF

# option(
# MANIFOLD_DOWNLOADS
# "Allow Manifold build to download missing dependencies"
# ON
# )
# 指定本地依赖源码路径（请根据实际目录调整绝对路径或相对路径）

# 确保 MANIFOLD_DOWNLOADS 为 ON（默认即为 ON）
set(MANIFOLD_DOWNLOADS OFF CACHE BOOL "保证使用本地库" FORCE)

# option(MANIFOLD_PAR "Enable Parallel backend" OFF)
# option(
# MANIFOLD_OPTIMIZED
# "Force optimized build, even with debugging enabled"
# OFF
# )
set(MANIFOLD_TEST OFF CACHE BOOL "" FORCE)
set(MANIFOLD_PYBIND OFF CACHE BOOL "" FORCE)

# These three options can force the build to avoid using the system version of
# the dependency
# This will either use the provided source directory via
# FETCHCONTENT_SOURCE_DIR_XXX, or fetch the source from GitHub.
# Note that the dependency will be built as static dependency to avoid dynamic
# library conflict.
# When the system package is unavailable, the option will be automatically set
# to true.
# option(MANIFOLD_USE_BUILTIN_TBB "Use builtin tbb" OFF)
# option(MANIFOLD_USE_BUILTIN_CLIPPER2 "Use builtin clipper2" OFF)
# option(MANIFOLD_USE_BUILTIN_NANOBIND "Use builtin nanobind" OFF)
set(MANIFOLD_USE_BUILTIN_TBB OFF CACHE BOOL "" FORCE)
set(MANIFOLD_USE_BUILTIN_CLIPPER2 OFF CACHE BOOL "" FORCE)
set(MANIFOLD_USE_BUILTIN_NANOBIND OFF CACHE BOOL "" FORCE)

# default to Release build
# option(CMAKE_BUILD_TYPE "Build type" Release)

# default to building shared library
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# Set some option values in the CMake cache
# set(MANIFOLD_FLAGS "" CACHE STRING "Manifold compiler flags")

# Development options
# option(TRACY_ENABLE "Use tracy profiling" OFF)
# option(TRACY_MEMORY_USAGE "Track memory allocation with tracy (expensive)" OFF)
# option(MANIFOLD_FUZZ "Enable fuzzing tests" OFF)
# option(ASSIMP_ENABLE "Enable ASSIMP for utilities in extras" OFF)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

set(MANIFOLD_USE_BUILTIN_TBB OFF CACHE BOOL "" FORCE)

set(MANIFOLD_PYBIND OFF CACHE BOOL "" FORCE)
set(MANIFOLD_JSBIND OFF CACHE BOOL "" FORCE)
set(MANIFOLD_TEST OFF CACHE BOOL "" FORCE)
set(ASSIMP_ENABLE OFF CACHE BOOL "" FORCE)
set(TRACY_ENABLE OFF CACHE BOOL "" FORCE)

add_subdirectory(
    ${CMAKE_CURRENT_LIST_DIR}/manifold
    ${CMAKE_CURRENT_BINARY_DIR}/manifold
)
