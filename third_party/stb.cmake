# NOTE: 只要master 分支
# git submodule add https://gh-proxy.com/https://github.com/nothings/stb third_party/stb
add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE "${CMAKE_SOURCE_DIR}/third_party/stb")

# define _LIBCPP_MATH_H // NOTE: 禁止导入<math.h>. 以避免类型重定义，很奇怪
# target_compile_definitions(stb INTERFACE
# _LIBCPP_MATH_H
# )