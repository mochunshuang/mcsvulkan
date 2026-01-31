# 启用测试
enable_testing()

set(TEST_ROOT_DIR "${CMAKE_SOURCE_DIR}/test")
set(TEST_EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/output/test_program)

include(test/env.cmake)
include(test/base.cmake)
include(test/mcsvulkan.cmake)