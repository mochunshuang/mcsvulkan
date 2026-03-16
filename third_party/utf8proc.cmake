# 1. 添加模块
# git submodule add  https://gh-proxy.org/https://github.com/JuliaStrings/utf8proc.git third_party/utf8proc

# 2. 进入 目录
# cd third_party/utf8proc

# 查看可以checkout的：
# git tag -l | sort -V

# 3. 切换到指定 tag
# git checkout v2.11.3

# 验证确实在标签上
# git describe --tags

# 回到项目根目录
# cd ../..

# 提交子模块状态的更改
# git add third_party/utf8proc

# git commit -m "Pin utf8proc to v2.11.3"

# git submodule status

set(UTF8PROC_INSTALL OFF CACHE BOOL "Enable installation of utf8proc" FORCE)
set(UTF8PROC_ENABLE_TESTING OFF CACHE BOOL "Enable testing of utf8proc" FORCE)
set(LIB_FUZZING_ENGINE OFF CACHE BOOL "Fuzzing engine to link against" FORCE) # 不安装
add_subdirectory(
    ${CMAKE_CURRENT_LIST_DIR}/utf8proc
    ${CMAKE_CURRENT_BINARY_DIR}/utf8proc
)
