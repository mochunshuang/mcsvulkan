# 1. 添加模块
# git submodule add  https://gh-proxy.org/https://github.com/Chlumsky/msdfgen.git third_party/msdfgen

# 2. 进入 msdfgen 目录
# cd third_party/msdfgen

# 查看可以checkout的：
# git tag -l | sort -V

# 3. 切换到指定 tag
# git checkout v1.13

# 验证确实在标签上
# git describe --tags

# 回到项目根目录
# cd ../..

# 提交子模块状态的更改
# git add third_party/msdfgen

# git commit -m "Pin msdfgen to v1.13"

# NOTE: 查看项目的所有子模块
# git submodule status

set(MSDFGEN_USE_VCPKG OFF CACHE BOOL "" FORCE)
set(MSDFGEN_USE_SKIA OFF CACHE BOOL "" FORCE)
set(MSDFGEN_DISABLE_SVG ON CACHE BOOL "" FORCE)
set(MSDFGEN_DISABLE_PNG OFF CACHE BOOL "" FORCE)
set(MSDFGEN_BUILD_STANDALONE OFF CACHE BOOL "not build standalone executable" FORCE)

# harfbuzz 依赖运行时。自定义target也没有用 总之冲突
set(MSDFGEN_DYNAMIC_RUNTIME ON CACHE BOOL "" FORCE) # 关闭静态运行时

# Freetype::Freetype. msdfgen只认识这个
add_library(Freetype::Freetype ALIAS freetype)
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/msdfgen)
