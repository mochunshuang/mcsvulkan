# 启用测试
enable_testing()

set(TEST_ROOT_DIR "${CMAKE_SOURCE_DIR}/test")
set(TEST_EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/output/test_program)
include(test/script/add_compile_glslc_shader.cmake)
include(test/script/auto_compile_glsl_shaders.cmake)
include(test/script/add_glsl_shader_dependencies.cmake)
include(test/script/std_glsl_env.cmake)
include(test/script/copy_dir_to_bindir.cmake)

include(test/env.cmake)
include(test/base.cmake)
include(test/mcsvulkan.cmake)