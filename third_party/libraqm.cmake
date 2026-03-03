# 1. 添加模块
# git submodule add https://gh-proxy.org/https://github.com/HOST-Oman/libraqm.git third_party/libraqm

# 2. 进入 目录
# cd third_party/libraqm

# 查看可以checkout的：
# git tag -l | sort -V

# 3. 切换到指定 tag
# git checkout v0.10.4

# 验证确实在标签上
# git describe --tags

# 回到项目根目录
# cd ../..

# 提交子模块状态的更改
# git add third_party/libraqm

# git commit -m "Pin libraqm to v0.10.4"

# NOTE: 查看项目的所有子模块
# git submodule status

# -----------------------------------------------------------------
# libraqm 集成（超简版）
# -----------------------------------------------------------------
set(RAQM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libraqm")

# 定义版本变量. raqm-version.h.in 需要
set(RAQM_VERSION_MAJOR 0)
set(RAQM_VERSION_MINOR 10)
set(RAQM_VERSION_MICRO 4)
set(RAQM_VERSION "0.10.4")

# 2. 生成 raqm-version.h（从 .in 文件）
configure_file(
    "${RAQM_ROOT}/src/raqm-version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/raqm-version.h"
    @ONLY
)

# 3. 列出源文件（根据你的实际目录调整）
set(RAQM_SOURCES
    "${RAQM_ROOT}/src/raqm.c"
)

# 4. 创建静态库
set(libraqm_name "libraqm")
add_library(${libraqm_name} STATIC ${RAQM_SOURCES})

# 5. 包含头文件路径（包括生成的版本头文件目录）
target_include_directories(${libraqm_name}
    PUBLIC
    "${RAQM_ROOT}/src"
    "${CMAKE_CURRENT_BINARY_DIR}" # 让 raqm-version.h 可以被找到
)

# 看源码。 我们使用的是。RAQM_SHEENBIDI_GT_2_9 要配置如下
target_compile_definitions(${libraqm_name} PRIVATE RAQM_SHEENBIDI RAQM_SHEENBIDI_GT_2_9)
target_link_libraries(${libraqm_name} PRIVATE freetype harfbuzz SheenBidi)
