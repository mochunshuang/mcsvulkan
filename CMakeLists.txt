cmake_minimum_required(VERSION 4.2.0)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# NOTE: volk 是 C 项目，有C的静态库的，必须这样做
project("mcsvulkan"
    LANGUAGES CXX C
)

# Set language version used
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED YES)
set(CMAKE_C_EXTENSIONS OFF)

include(cmake/out_config.cmake)
include(cmake/vulkan.cmake)

include(third_party/volk.cmake)
include(third_party/vma.cmake)
include(third_party/glfw.cmake)
include(third_party/glm.cmake)
include(third_party/stb.cmake)
include(third_party/nlohmann_json.cmake)
include(third_party/ktx.cmake)

include(third_party/freetype.cmake)
include(third_party/harfbuzz.cmake)

include(third_party/zlib.cmake)
include(third_party/libpng.cmake)
include(third_party/msdfgen.cmake)
include(third_party/msdf-atlas-gen.cmake)

include(third_party/rectpack2D.cmake)

include(third_party/agg.cmake)

# text layout
include(third_party/SheenBidi.cmake)
include(third_party/libraqm.cmake)
include(third_party/libunibreak.cmake)

set(ASSETS_DIR ${CMAKE_SOURCE_DIR}/assets)

# test add
include(test/all_test.cmake)