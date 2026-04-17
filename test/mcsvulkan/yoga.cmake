std_glsl_env_init("mcsvulkan/yoga")
auto_compile_glsl_shaders(${GLSL_SHADERS_NAME} ${SHADER_DIR} ${SHADER_OUTPUT_DIR})
copy_dir_to_bindir("textures")

std_glsl_target(base_children test_bindless_vertext.vert test_triangle.frag)

# end
std_glsl_env_destroy()