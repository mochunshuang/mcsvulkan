std_glsl_env_init("mcsvulkan/integration")

auto_compile_glsl_shaders(${GLSL_SHADERS_NAME} ${SHADER_DIR} ${SHADER_OUTPUT_DIR})

std_glsl_target(test_triangle test_triangle.vert test_triangle.frag)

# end
std_glsl_env_destroy()