# git submodule add https://gh-proxy.org/https://github.com/tinyobjloader/tinyobjloader.git third_party/tinyobjloader

add_library(tinyobjloader INTERFACE)
target_include_directories(tinyobjloader SYSTEM INTERFACE "${CMAKE_SOURCE_DIR}/third_party/tinyobjloader")