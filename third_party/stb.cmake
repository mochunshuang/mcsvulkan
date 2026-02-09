# NOTE: 只要master 分支
# git submodule add https://gh-proxy.com/https://github.com/nothings/stb third_party/stb
# add_library(stb INTERFACE)
# target_include_directories(stb SYSTEM INTERFACE "${CMAKE_SOURCE_DIR}/third_party/stb")

# define _LIBCPP_MATH_H // NOTE: 禁止导入<math.h>. 以避免类型重定义，很奇怪
# target_compile_definitions(stb INTERFACE
# _LIBCPP_MATH_H
# )

# define STB_IMAGE_IMPLEMENTATION
# include "stb_image.h"
set(STB_DIR "${CMAKE_SOURCE_DIR}/third_party/stb")

add_library(stb STATIC "${STB_DIR}/stb_image.h")
target_include_directories(stb SYSTEM PUBLIC "${STB_DIR}")

# private 则不会传递到 app 的源文件。不太好
# target_compile_definitions(stb PRIVATE STB_IMAGE_IMPLEMENTATION)
set_target_properties(stb PROPERTIES LINKER_LANGUAGE C)
target_compile_features(stb PRIVATE c_std_17)