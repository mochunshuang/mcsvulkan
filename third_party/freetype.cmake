# 1. 添加模块
# git submodule add  https://gh-proxy.org/https://github.com/freetype/freetype.git third_party/freetype

# 2. 进入 freetype 目录
# cd third_party/freetype

# 查看可以checkout的：
# git tag -l | sort -V

# 3. 切换到指定 tag
# git checkout VER-2-14-1

# 验证确实在标签上
# git describe --tags

# 回到项目根目录
# cd ../..

# 提交子模块状态的更改
# git add third_party/freetype

# git commit -m "Pin freetype to VER-2-14-1"

# NOTE: 查看项目的所有子模块
# git submodule status

# -------------------- FreeType 配置 --------------------
# 彻底禁用外部依赖
set(FT_WITH_ZLIB OFF CACHE BOOL "" FORCE)
set(FT_WITH_PNG OFF CACHE BOOL "" FORCE)
set(FT_WITH_BZIP2 OFF CACHE BOOL "" FORCE)

# 允许 FreeType 启用 HarfBuzz 支持（动态加载，不影响链接）
set(FT_WITH_HARFBUZZ OFF CACHE BOOL "" FORCE) # 不链接 HarfBuzz

# 防止 CMake 查找外部包
set(CMAKE_DISABLE_FIND_PACKAGE_ZLIB ON)
set(CMAKE_DISABLE_FIND_PACKAGE_PNG ON)
set(CMAKE_DISABLE_FIND_PACKAGE_BZip2 ON)
set(CMAKE_DISABLE_FIND_PACKAGE_HarfBuzz ON) # 不让 FreeType 在构建时链接 HarfBuzz

# add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/freetype)
add_subdirectory(
    ${CMAKE_CURRENT_LIST_DIR}/freetype
    ${CMAKE_CURRENT_BINARY_DIR}/freetype
)