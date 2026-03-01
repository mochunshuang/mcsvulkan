# 1. 添加模块
# git submodule add  https://gh-proxy.org/https://github.com/ghaerr/agg-2.6.git third_party/agg

# 2. 进入 freetype 目录
# cd third_party/agg

# 查看可以checkout的：
# git tag -l | sort -V

# NOTE: 查看项目的所有子模块
# git submodule status
set(antigrain_SOURCE_DIR "${CMAKE_SOURCE_DIR}/third_party/agg/agg-src" CACHE BOOL "" FORCE)
add_subdirectory(${antigrain_SOURCE_DIR}/src)

# antigrain
if(TARGET antigrain)
    message(STATUS "Target 'antigrain' exists. Linking...")

    target_include_directories(antigrain SYSTEM PUBLIC ${antigrain_SOURCE_DIR}/include)

    # controls platform
    target_include_directories(controls SYSTEM PUBLIC ${antigrain_SOURCE_DIR}/include)
    target_include_directories(platform SYSTEM PUBLIC ${antigrain_SOURCE_DIR}/include)

    target_compile_definitions(antigrain PUBLIC NOMINMAX)
    target_compile_definitions(controls PUBLIC NOMINMAX)
    target_compile_definitions(platform PUBLIC NOMINMAX)

else()
    message(WARNING "Target 'antigrain' not found. Check the subdirectory.")

    # 可以在这里添加 fallback 逻辑，比如直接使用源文件
endif()