# 1. 添加模块
# git submodule add  https://gh-proxy.org/https://github.com/pnggroup/libpng.git third_party/libpng

# 2. 进入 目录
# cd third_party/libpng

# 查看可以checkout的：
# git tag -l | sort -V

# 3. 切换到指定 tag
# git checkout v1.6.55

# 验证确实在标签上
# git describe --tags

# 回到项目根目录
# cd ../..

# 提交子模块状态的更改
# git add third_party/libpng

# git commit -m "Pin libpng to v1.6.55"

# git submodule status

set(PNG_SHARED OFF CACHE BOOL "" FORCE)
set(PNG_STATIC ON CACHE BOOL "" FORCE)
set(PNG_TESTS OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_LIBRARIES ON CACHE BOOL "" FORCE)
set(PSKIP_INSTALL_ALLS ON CACHE BOOL "" FORCE)
set(SKIP_INSTALL_EXPORT ON CACHE BOOL "" FORCE)
set(SKIP_INSTALL_CONFIG_FILE ON CACHE BOOL "" FORCE)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/libpng)
add_library(PNG::PNG ALIAS png_static)
