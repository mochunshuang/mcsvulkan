# git submodule add https://gh-proxy.com/https://github.com/syoyo/tinygltf third_party/tinygltf

# tinygltf
set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF CACHE BOOL "控制 tinygltf 是否构建示例" FORCE)
set(TINYGLTF_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tinygltf)
