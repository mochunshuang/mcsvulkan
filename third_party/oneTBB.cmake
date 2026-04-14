# 1. 添加模块
# git submodule add https://gh-proxy.org/https://github.com/uxlfoundation/oneTBB.git third_party/oneTBB

# 2. 进入 oneTBB 目录
# cd third_party/oneTBB

# 查看可以checkout的：
# git tag -l | sort -V

# 3. 切换到指定 tag
# git checkout v2022.3.0

# 验证确实在标签上
# git describe --tags

# 回到项目根目录
# cd ../..

# 提交子模块状态的更改
# git add third_party/oneTBB

# git commit -m "Pin oneTBB to v2022.3.0"

# NOTE: 查看项目的所有子模块
# git submodule status

# ========== 配置 oneTBB 选项 ==========
set(TBB_TEST OFF CACHE BOOL "Disable TBB tests" FORCE)
set(TBB_EXAMPLES OFF CACHE BOOL "Disable examples" FORCE)

# 默认开启的
# set(TBB_STRICT ON CACHE BOOL "Disable strict warnings" FORCE)
set(TBB_STRICT OFF CACHE BOOL "Disable -Werror for oneTBB" FORCE) # 编译才过，win和linux差太多

set(TBB_WINDOWS_DRIVER OFF CACHE BOOL "" FORCE)
set(TBB_NO_APPCONTAINER OFF CACHE BOOL "" FORCE)
set(TBB4PY_BUILD OFF CACHE BOOL "" FORCE)

set(TBB_BUILD ON CACHE BOOL "Enable tbb build" FORCE) # 构建 tbb 库
set(TBBMALLOC_BUILD ON CACHE BOOL "" FORCE) # 默认

set(TBB_CPF OFF CACHE BOOL "" FORCE)
set(TBB_FIND_PACKAGE OFF CACHE BOOL "" FORCE)

# option(TBB_DISABLE_HWLOC_AUTOMATIC_SEARCH "Disable HWLOC automatic search by pkg-config tool" ${CMAKE_CROSSCOMPILING})
set(TBB_DISABLE_HWLOC_AUTOMATIC_SEARCH ON CACHE BOOL "Disable search" FORCE)
set(TBB_ENABLE_IPO OFF CACHE BOOL "" FORCE) # 默认
set(TBB_CONTROL_FLOW_GUARD OFF CACHE BOOL "" FORCE) # 默认
set(TBB_FUZZ_TESTING OFF CACHE BOOL "" FORCE) # 默认

set(TBB_FILE_TRIM ON CACHE BOOL "" FORCE) # 默认

# set(TBB_INSTALL OFF CACHE BOOL "" FORCE)
set(TBB_LINUX_SEPARATE_DBG OFF CACHE BOOL "" FORCE) # 默认

set(TBB_FIND_PACKAGE OFF CACHE BOOL "" FORCE) # 避免 oneTBB 自己去找另一个 TBB

set(TBBMALLOC_PROXY_BUILD OFF CACHE BOOL "" FORCE)

# 注意：需在 add_subdirectory(oneTBB) 之前调用

# 1. 强制禁用 PIC
set(CMAKE_POSITION_INDEPENDENT_CODE OFF CACHE BOOL "" FORCE)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# 添加 oneTBB 子目录
add_subdirectory(
    ${CMAKE_CURRENT_LIST_DIR}/oneTBB
    ${CMAKE_CURRENT_BINARY_DIR}/oneTBB
)

if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # 移除 tbb 静态库目标的 -fPIC 标志
    if(TARGET tbb)
        get_target_property(tbb_opts tbb COMPILE_OPTIONS)

        if(tbb_opts)
            list(REMOVE_ITEM tbb_opts "-fPIC" "-fPIE")
            set_target_properties(tbb PROPERTIES COMPILE_OPTIONS "${tbb_opts}")
        endif()
    endif()

    # 如果有其他目标如 tbbmalloc 也需处理
    if(TARGET tbbmalloc)
        get_target_property(tbbmalloc_opts tbbmalloc COMPILE_OPTIONS)

        if(tbbmalloc_opts)
            list(REMOVE_ITEM tbbmalloc_opts "-fPIC" "-fPIE")
            set_target_properties(tbbmalloc PROPERTIES COMPILE_OPTIONS "${tbbmalloc_opts}")
        endif()
    endif()
endif()

if(NOT TARGET tbb)
    message(FATAL_ERROR "NOT FIND tbb")
endif(NOT TARGET tbb)

# ============ 欺骗 find_package(TBB) [start]=============
# 1. 伪造 Config 文件（只设变量，不动目标）
set(FAKE_TBB_DIR ${CMAKE_CURRENT_BINARY_DIR}/fake_tbb_config)
file(WRITE ${FAKE_TBB_DIR}/TBBConfig.cmake
    "set(TBB_FOUND TRUE)\n"
    "set(TBB_VERSION_MAJOR 2022)\n"
    "set(TBB_VERSION_MINOR 3)\n"
    "set(TBB_VERSION_PATCH 0)\n"
    "set(TBB_VERSION_STRING \"2022.3.0\")\n"
    "set(TBB_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}/oneTBB/include)\n"
    "set(TBB_LIBRARIES tbb)\n"
)
file(WRITE ${FAKE_TBB_DIR}/TBBConfigVersion.cmake
    "set(PACKAGE_VERSION \"2022.3.0\")\n"
    "if(PACKAGE_VERSION VERSION_EQUAL PACKAGE_FIND_VERSION)\n"
    "    set(PACKAGE_VERSION_COMPATIBLE TRUE)\n"
    "    set(PACKAGE_VERSION_EXACT TRUE)\n"
    "endif()\n"
)

# 2. 将伪造目录置于搜索路径最前
list(PREPEND CMAKE_PREFIX_PATH ${FAKE_TBB_DIR})

# 3. 现在 find_package 必定成功
find_package(TBB REQUIRED)

# ============ 欺骗 find_package(TBB) [end]=============