set(DIR_NAME "mcsvulkan/vma")
set(EXE_DIR "${CMAKE_SOURCE_DIR}/test/${DIR_NAME}")

# 头文件默认依赖
set(BASE_LIBS ${MCSVULKAN_LIBS})

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

# 头文件默认依赖
# set(BASE_LIBS volk vma glfw glm stb ktx nlohmann_json
# freetype harfbuzz SheenBidi libunibreak utf8proc
# )
set(BASE_LIBS ${MCSVULKAN_LIBS} msdfgen::msdfgen)
add_std_glsl_target(test_buffer_base test_bindless_vertext.vert test_triangle.frag)
add_std_glsl_target(test_uniform test_uniform.vert test_triangle.frag)
add_std_glsl_target(test_depth test_depth.vert test_triangle.frag)
add_std_glsl_target(test_msaa test_depth.vert test_triangle.frag)

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
add_std_glsl_target(camera_model2 test_model_matrix2.vert test_texture.frag)
add_std_glsl_target(camera_model3 test_model_matrix2.vert test_texture.frag)
add_std_glsl_target(test_ray test_model_matrix2.vert test_texture.frag)
add_std_glsl_target(test_ray2 test_model_matrix2.vert test_texture.frag)
add_std_glsl_target(test_picking test_model_matrix2.vert test_texture.frag)
add_std_glsl_target(test_picking2 test_model_matrix2.vert test_texture.frag)
add_std_glsl_target(test_picking3 test_model_matrix2.vert test_texture.frag)

add_std_glsl_target(test_texture2 test_texture.vert test_texture2.frag)
add_std_glsl_target(test_texture3 test_texture.vert test_texture2.frag)
ADD_MSDF_DEF(${TARGET_NAME})

add_std_glsl_target(create_texture_image test_texture.vert test_texture.frag)
add_std_glsl_target(test_msdf_atlas_gen test_texture.vert test_texture2.frag)
ADD_MSDF_DEF(${TARGET_NAME})

add_std_glsl_target(test_msdf_atlas_gen1 test_msdf_atlas_gen.vert test_msdf_atlas_gen.frag)
ADD_MSDF_DEF(${TARGET_NAME})
add_std_glsl_target(test_msdf_atlas_gen2 test_msdf_atlas_gen.vert test_msdf_atlas_gen.frag)
ADD_MSDF_DEF(${TARGET_NAME})

# set(BASE_LIBS volk vma glfw glm stb ktx nlohmann_json msdfgen::msdfgen
# freetype harfbuzz SheenBidi libraqm libunibreak utf8proc
# )
add_std_glsl_target(test_msdf_atlas_gen3 test_msdf_atlas_gen.vert test_msdf_atlas_gen.frag)
ADD_MSDF_DEF(${TARGET_NAME})

add_std_glsl_target(test_emoji test_emoji.vert test_emoji.frag)
ADD_MSDF_DEF(${TARGET_NAME})
add_std_glsl_target(test_bidi test_emoji.vert test_emoji.frag)
ADD_MSDF_DEF(${TARGET_NAME})
add_std_glsl_target(test_bidi2 test_emoji.vert test_emoji.frag)
ADD_MSDF_DEF(${TARGET_NAME})

add_std_glsl_target(test_harfbuzz test_emoji.vert test_emoji.frag)
ADD_MSDF_DEF(${TARGET_NAME})

add_std_glsl_target(test_harfbuzz2 test_emoji.vert test_emoji.frag)
ADD_MSDF_DEF(${TARGET_NAME})
add_std_glsl_target(test_harfbuzz3 test_emoji.vert test_emoji.frag)
ADD_MSDF_DEF(${TARGET_NAME})
add_std_glsl_target(test_libunibreak test_emoji.vert test_emoji.frag)
ADD_MSDF_DEF(${TARGET_NAME})
add_std_glsl_target(test_libunibreak2 test_emoji.vert test_emoji.frag)
ADD_MSDF_DEF(${TARGET_NAME})
add_std_glsl_target(test_libunibreak3 test_emoji.vert test_emoji.frag)
ADD_MSDF_DEF(${TARGET_NAME})

add_std_glsl_target(test_libunibreak4 test_emoji.vert test_emoji.frag)
ADD_MSDF_DEF(${TARGET_NAME})

add_std_glsl_target(test_shape test_emoji.vert test_emoji.frag)
ADD_MSDF_DEF(${TARGET_NAME})

add_std_glsl_target(test_line_breaks test_emoji.vert test_emoji.frag)
ADD_MSDF_DEF(${TARGET_NAME})

add_std_glsl_target(test_yoga test_emoji.vert test_emoji2.frag)
ADD_MSDF_DEF(${TARGET_NAME})
add_std_glsl_target(test_yoga2 test_emoji.vert test_emoji2.frag)
ADD_MSDF_DEF(${TARGET_NAME})
add_std_glsl_target(test_yoga3 test_emoji.vert test_emoji2.frag)
ADD_MSDF_DEF(${TARGET_NAME})
add_std_glsl_target(test_yoga4 test_yoga4.vert test_emoji2.frag)
ADD_MSDF_DEF(${TARGET_NAME})
add_std_glsl_target(test_yoga5 test_yoga4.vert test_emoji2.frag)
ADD_MSDF_DEF(${TARGET_NAME})
add_std_glsl_target(test_yoga6 test_yoga4.vert test_emoji2.frag)
ADD_MSDF_DEF(${TARGET_NAME})

add_std_glsl_target(test_picking4 test_model_matrix2.vert test_texture.frag)
add_std_glsl_target(test_picking5 test_model_matrix2.vert test_texture.frag)
add_std_glsl_target(test_picking6 test_model_matrix2.vert test_texture.frag)

add_std_glsl_target(test_indirectdraw test_indirectdraw.vert test_texture.frag)
add_std_glsl_target(test_indirectdraw2 test_indirectdraw.vert test_texture.frag)

set(BASE_LIBS ${BASE_LIBS} imgui_volk
)
add_std_glsl_target(test_imgui test_indirectdraw.vert test_texture.frag)

add_std_glsl_target(test_dod test_indirectdraw.vert test_texture.frag)
add_std_glsl_target(test_indirectdraw_no_pick test_indirectdraw_no_pick.vert test_indirectdraw_no_pick.frag)
add_std_glsl_target(test_dod2 test_indirectdraw_no_pick.vert test_indirectdraw_no_pick.frag)
add_std_glsl_target(test_dod3 test_indirectdraw_no_pick.vert test_indirectdraw_no_pick.frag)
add_std_glsl_target(test_dod4 test_indirectdraw_no_pick.vert test_indirectdraw_no_pick.frag)
add_std_glsl_target(test_dod5 test_indirectdraw_no_pick.vert test_indirectdraw_no_pick.frag)
add_std_glsl_target(test_dod6 test_indirectdraw_no_pick.vert test_indirectdraw_no_pick.frag)

add_std_glsl_target(test_dod7 test_indirectdraw_no_pick.vert test_indirectdraw_no_pick.frag)
add_std_glsl_target(test_dod8 test_dod8.vert test_indirectdraw_no_pick.frag)
add_std_glsl_target(test_dod9 test_dod9.vert test_indirectdraw_no_pick.frag)

# NOTE: 就顶点着色器不同： ubo.proj[1][1] *= -1; 是BUG的来源
add_std_glsl_target(test_dod8_debug test_dod8_debug.vert test_indirectdraw_no_pick.frag)
add_std_glsl_target(test_dod8_debug2 test_dod8.vert test_indirectdraw_no_pick.frag)

# NDC 的重要性。矩阵/相机矩阵，有一个有问题，就是全部有问题
add_std_glsl_target(test_dod10 test_dod9.vert test_indirectdraw_no_pick.frag)
add_std_glsl_target(test_dod11 test_dod9.vert test_indirectdraw_no_pick.frag)

add_std_glsl_target(test_dod12 test_dod12.vert test_dod12.frag)
ADD_MSDF_DEF(${TARGET_NAME})
add_std_glsl_target(test_dod13 test_dod13.vert test_dod12.frag)
ADD_MSDF_DEF(${TARGET_NAME})

add_std_glsl_target(test_dod14 test_dod14.vert test_dod12.frag)
ADD_MSDF_DEF(${TARGET_NAME})

add_std_glsl_target(test_dod15 test_dod14.vert test_dod12.frag)
ADD_MSDF_DEF(${TARGET_NAME})

# end
std_glsl_env_destroy()