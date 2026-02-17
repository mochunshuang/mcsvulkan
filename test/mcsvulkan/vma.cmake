set(DIR_NAME "mcsvulkan/vma")
set(EXE_DIR "${CMAKE_SOURCE_DIR}/test/${DIR_NAME}")

set(BASE_LIBS volk vma glfw glm)

macro(add_vulkan_vma_test fileName)
    string(REPLACE "/" "-" PREFIX_NAME ${DIR_NAME})
    set(TAGET_NAME "${PREFIX_NAME}-${fileName}")
    add_executable(${TAGET_NAME} "${EXE_DIR}/${fileName}.cpp")
    target_link_libraries(${TAGET_NAME} PRIVATE ${BASE_LIBS})

    # 添加测试
    add_test(NAME "${TAGET_NAME}" COMMAND $<TARGET_FILE:${TAGET_NAME}>)
endmacro()

add_vulkan_vma_test(test_raii_vma)

# end
unset(BASE_LIBS)
unset(EXE_DIR)
unset(DIR_NAME)

std_glsl_env_init("mcsvulkan/vma")
auto_compile_glsl_shaders(${GLSL_SHADERS_NAME} ${SHADER_DIR} ${SHADER_OUTPUT_DIR})
copy_dir_to_bindir("textures")

add_std_glsl_target(test_buffer_base test_bindless_vertext.vert test_triangle.frag)
add_std_glsl_target(test_uniform test_uniform.vert test_triangle.frag)
add_std_glsl_target(test_depth test_depth.vert test_triangle.frag)
add_std_glsl_target(test_msaa test_depth.vert test_triangle.frag)

set(BASE_LIBS volk vma glfw glm stb)
add_std_glsl_target(test_texture test_texture.vert test_texture.frag)
add_std_glsl_target(test_mipmapping test_texture.vert test_texture.frag)
add_std_glsl_target(test_update_texture test_texture.vert test_texture.frag)
add_std_glsl_target(test_update_texture2 test_texture.vert test_texture.frag)
add_std_glsl_target(test_model_matrix test_model_matrix.vert test_texture.frag)
add_std_glsl_target(test_model_matrix2 test_model_matrix2.vert test_texture.frag)
add_std_glsl_target(test_model_matrix3 test_model_matrix2.vert test_texture.frag)
add_std_glsl_target(test_views_matrix test_model_matrix2.vert test_texture.frag)
add_std_glsl_target(test_projection_matrix test_model_matrix2.vert test_texture.frag)
add_std_glsl_target(test_ortho_matrix test_model_matrix2.vert test_texture.frag)
add_std_glsl_target(camera_view test_model_matrix2.vert test_texture.frag)
add_std_glsl_target(camera_ortho test_model_matrix2.vert test_texture.frag)
add_std_glsl_target(camera_perspective test_model_matrix2.vert test_texture.frag)
add_std_glsl_target(camera_model test_model_matrix2.vert test_texture.frag)

# end
std_glsl_env_destroy()