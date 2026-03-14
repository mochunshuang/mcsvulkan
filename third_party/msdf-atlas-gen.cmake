# 1. 添加模块
# git submodule add  https://gh-proxy.org/https://github.com/Chlumsky/msdf-atlas-gen.git third_party/msdf-atlas-gen

# 2. 进入 msdf-atlas-gen 目录
# cd third_party/msdf-atlas-gen

# 查看可以checkout的：
# git tag -l | sort -V

# 3. 切换到指定 tag
# git checkout v1.3

# 验证确实在标签上
# git describe --tags

# 回到项目根目录
# cd ../..

# 提交子模块状态的更改
# git add third_party/msdf-atlas-gen

# git commit -m "Pin msdf-atlas-gen to v1.3"

# NOTE: 查看项目的所有子模块
# git submodule status

# NOTE: 和当前的 msdf-atlas-gen 不兼容
# NOTE: 查看依赖的版本：
# PS E:\0_github_project\mcsvulkan\third_party\msdf-atlas-gen> git submodule status
# -888674220216d1d326c6f29cf89165b545279c1f artery-font-format
# -85e8b3d47b3d1a42e4a5ebda0a24fb1cc2e669e0 msdfgen
# NOTE: 看起来要回退版本？ 当前是 v1.13
# PS E:\0_github_project\mcsvulkan\third_party\msdfgen> git describe --tags 85e8b3d
# v1.12

# NOTE: 切换到 master 分支并拉取最新代码，接口就兼容了
# git checkout master
# git pull origin master
# git describe --tags

# 1. config
set(MSDF_ATLAS_BUILD_STANDALONE ON CACHE BOOL "Build the msdf-atlas-gen standalone executable" FORCE)
set(MSDF_ATLAS_USE_VCPKG OFF CACHE BOOL "Use vcpkg package manager to link project dependencies" FORCE)
set(MSDF_ATLAS_USE_SKIA OFF CACHE BOOL "Build with the Skia library" FORCE)
set(MSDF_ATLAS_NO_ARTERY_FONT ON CACHE BOOL "Disable Artery Font export and do not require its submodule" FORCE)

# 已经下载因此不要子模块的
set(MSDF_ATLAS_MSDFGEN_EXTERNAL ON CACHE BOOL "Disable Artery Font export and do not require its submodule" FORCE)
set(MSDF_ATLAS_INSTALL OFF CACHE BOOL "Generate installation target" FORCE)

# NOTE: 但是输出目录只有一个 exe 没有 dll。 虽然我们的库都说静态的，但是c++的标准库不是静态库，还得运行时链接的
# freetype是动态库，导致的好像
# MSDF_ATLAS_DYNAMIC_RUNTIME 控制的是 C/C++ 运行时库的链接方式（静态 vs 动态）。如果设为 ON，则链接器会链接动态版本的运行时库（例如 msvcrt.lib 导入库，实际运行时需要 msvcp140.dll 等）。
# 如果设为 OFF，则会将运行时库静态链接进 exe，使得 exe 体积变大但不依赖外部运行时 DLL。
set(MSDF_ATLAS_DYNAMIC_RUNTIME ON CACHE BOOL "Link dynamic runtime library instead of static" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Generate dynamic library files instead of static" FORCE)

# 2. add_subdirectory
# add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/msdf-atlas-gen)
add_subdirectory(
    ${CMAKE_CURRENT_LIST_DIR}/msdf-atlas-gen
    ${CMAKE_CURRENT_BINARY_DIR}/msdf-atlas-gen
)
