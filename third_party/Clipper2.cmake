# 1. 添加模块
# git submodule add https://github.com/AngusJohnson/Clipper2.git third_party/Clipper2

# 2. 进入 Clipper2 目录
# cd third_party/Clipper2

# 查看可以checkout的：
# git tag -l | sort -V

# 3. 切换到指定 tag
# git checkout Clipper2_2.0.1

# 验证确实在标签上
# git describe --tags

# 回到项目根目录
# cd ../..

# 提交子模块状态的更改
# git add third_party/Clipper2

# git commit -m "Pin Clipper2 to Clipper2_2.0.1"

# NOTE: 查看项目的所有子模块
# git submodule status

set(CLIPPER2_HI_PRECISION OFF CACHE BOOL "" FORCE)
set(CLIPPER2_UTILS ON CACHE BOOL "" FORCE)
set(CLIPPER2_EXAMPLES OFF CACHE BOOL "" FORCE) # 如果您不需要构建示例
set(CLIPPER2_TESTS OFF CACHE BOOL "" FORCE) # 如果您不需要测试
set(USE_EXTERNAL_GTEST OFF CACHE BOOL "" FORCE)
set(USE_EXTERNAL_GBENCHMARK OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

add_subdirectory(
    ${CMAKE_CURRENT_LIST_DIR}/Clipper2/CPP
    ${CMAKE_CURRENT_BINARY_DIR}/Clipper2
)

# 对于 Windows 平台，移除 Clipper2 库中的 -lm 依赖（仅当目标存在时）
if(WIN32)
    foreach(lib IN ITEMS Clipper2 Clipper2Z)
        if(TARGET ${lib})
            get_target_property(lib_libs ${lib} INTERFACE_LINK_LIBRARIES)

            if(lib_libs)
                # 移除 "-lm" 项（注意可能是字符串或列表）
                list(REMOVE_ITEM lib_libs "-lm")
                set_target_properties(${lib} PROPERTIES INTERFACE_LINK_LIBRARIES "${lib_libs}")
            endif()
        endif()
    endforeach()
endif()

if(NOT TARGET Clipper2)
    message(FATAL_ERROR "NOT FIND Clipper2")
endif(NOT TARGET Clipper2)

# ============ 欺骗 find_package(Clipper2) [start]=============
# 1. 确保有别名目标供现代 CMake 链接
if(NOT TARGET Clipper2::clipper2)
    add_library(Clipper2::clipper2 ALIAS Clipper2)
endif()

# 2. 提供伪造的 Config 文件（以防依赖使用 find_package 配置模式）
set(FAKE_CLIPPER2_DIR ${CMAKE_CURRENT_BINARY_DIR}/fake_clipper2_config)
file(WRITE ${FAKE_CLIPPER2_DIR}/Clipper2Config.cmake
    "if(NOT TARGET Clipper2::clipper2)\n"
    "    add_library(Clipper2::clipper2 ALIAS Clipper2)\n"
    "endif()\n"
    "set(Clipper2_FOUND TRUE)\n"
    "set(Clipper2_VERSION \"2.0.1\")\n"
    "set(Clipper2_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}/Clipper2/CPP)\n"
    "set(Clipper2_LIBRARIES Clipper2::clipper2)\n"
)
file(WRITE ${FAKE_CLIPPER2_DIR}/Clipper2ConfigVersion.cmake
    "set(PACKAGE_VERSION \"2.0.1\")\n"
    "if(PACKAGE_VERSION VERSION_EQUAL PACKAGE_FIND_VERSION)\n"
    "    set(PACKAGE_VERSION_COMPATIBLE TRUE)\n"
    "    set(PACKAGE_VERSION_EXACT TRUE)\n"
    "endif()\n"
)
list(PREPEND CMAKE_PREFIX_PATH ${FAKE_CLIPPER2_DIR})

# 3. 假装已经找到，调用 find_package 会直接成功
find_package(Clipper2 REQUIRED)

# ============ 欺骗 find_package(Clipper2) [end]=============