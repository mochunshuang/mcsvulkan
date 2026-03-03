# 1. 添加模块
# git submodule add https://gh-proxy.org/https://github.com/adah1972/libunibreak.git third_party/libunibreak

# 2. 进入 目录
# cd third_party/libunibreak

# 查看可以checkout的：
# git tag -l | sort -V

# 3. 切换到指定 tag
# git checkout libunibreak_6_1

# 验证确实在标签上
# git describe --tags

# 回到项目根目录
# cd ../..

# 提交子模块状态的更改
# git add third_party/libunibreak

# git commit -m "Pin libunibreak to libunibreak_6_1"

# NOTE: 查看项目的所有子模块
# git submodule status

# -----------------------------------------------------------------
# libunibreak 集成（全功能版）
# -----------------------------------------------------------------
set(UNIBREAK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libunibreak")

# 收集所有 .c 源文件
file(GLOB UNIBREAK_SOURCES "${UNIBREAK_ROOT}/src/*.c")

# 排除 tests.c（它是测试程序，不是库的一部分）
list(REMOVE_ITEM UNIBREAK_SOURCES "${UNIBREAK_ROOT}/src/tests.c")

# 创建静态库
add_library(libunibreak STATIC ${UNIBREAK_SOURCES})

# 公开头文件路径（供外部 include）
target_include_directories(libunibreak PUBLIC "${UNIBREAK_ROOT}/src")