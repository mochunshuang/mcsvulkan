std_glsl_env_init("mcsvulkan/integration")

# set(BASE_LIBS volk vma glfw glm stb nlohmann_json
# freetype harfbuzz SheenBidi libunibreak utf8proc
# )
auto_compile_glsl_shaders(${GLSL_SHADERS_NAME} ${SHADER_DIR} ${SHADER_OUTPUT_DIR})

std_glsl_target(test_triangle test_triangle.vert test_triangle.frag)
std_glsl_target(test_bindless_vertext test_bindless_vertext.vert test_triangle.frag)
std_glsl_target(test_bindless_vertext2 test_bindless_vertext.vert test_triangle.frag)
std_glsl_target(test_triangle2 test_triangle2.vert test_triangle.frag)
std_glsl_target(test_bindless_vertext3 test_bindless_vertext3.vert test_triangle.frag)
std_glsl_target(test_bindless_vertext4 test_bindless_vertext.vert test_triangle.frag)
std_glsl_target(test_bindless_vertext5 test_bindless_vertext5.vert test_triangle.frag)
std_glsl_target(test_bindless_vertext6 test_bindless_vertext5.vert test_triangle.frag)

# end
std_glsl_env_destroy()