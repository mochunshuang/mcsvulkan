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

include(cmake/out_config.cmake)
include(cmake/vulkan.cmake)

include(third_party/volk.cmake)
include(third_party/vma.cmake)
include(third_party/glfw.cmake)
include(third_party/glm.cmake)

# test add
include(test/all_test.cmake)