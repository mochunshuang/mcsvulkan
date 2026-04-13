set(DIR_NAME yoga)
set(EXE_DIR ${CMAKE_SOURCE_DIR}/test/${DIR_NAME})
set(OUTPUT_DIRECTORY ${TEST_EXECUTABLE_OUTPUT_PATH}/${DIR_NAME})

set(LIB yogacore glfw)

macro(add_yoga_target NAME)
    set(TARGET_NAME "${DIR_NAME}-${NAME}")
    add_executable(${TARGET_NAME} "${EXE_DIR}/${NAME}.cpp")
    target_link_libraries(${TARGET_NAME} PRIVATE ${LIB})
    set_target_properties(${TARGET_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIRECTORY}
        OUTPUT_NAME ${NAME}
    )
endmacro()

add_yoga_target("test_yoga")
add_yoga_target("test_base")
add_yoga_target("test_config")

std_glsl_env_init("yoga")
set(BASE_LIBS volk vma glfw glm stb nlohmann_json
    freetype harfbuzz SheenBidi libunibreak utf8proc yogacore
)
auto_compile_glsl_shaders(${GLSL_SHADERS_NAME} ${SHADER_DIR} ${SHADER_OUTPUT_DIR})

std_glsl_target(test_triangle test_bindless_vertext.vert test_triangle.frag)
std_glsl_target(test_yoga_init test_bindless_vertext.vert test_triangle.frag)
std_glsl_target(test_yoga_init2 test_bindless_vertext.vert test_triangle.frag)
std_glsl_target(test_yoga_init3 test_bindless_vertext.vert test_triangle.frag)

std_glsl_target(base_align_content test_bindless_vertext.vert test_triangle.frag)
std_glsl_target(base_align_items_self test_bindless_vertext.vert test_triangle.frag)
std_glsl_target(base_aspect_ratio test_bindless_vertext.vert test_triangle.frag)
std_glsl_target(base_display test_bindless_vertext.vert test_triangle.frag)
std_glsl_target(base_flex_basis_grow_shrink test_bindless_vertext.vert test_triangle.frag)
std_glsl_target(base_flex_direction test_bindless_vertext.vert test_triangle.frag)
std_glsl_target(base_flex_wrap test_bindless_vertext.vert test_triangle.frag)
std_glsl_target(base_gap test_bindless_vertext.vert test_triangle.frag)
std_glsl_target(base_insets test_bindless_vertext.vert test_triangle.frag)
std_glsl_target(base_justify_content test_bindless_vertext.vert test_triangle.frag)
std_glsl_target(base_layout_direction test_bindless_vertext.vert test_triangle.frag)
std_glsl_target(base_margin_padding_border test_bindless_vertext.vert test_triangle.frag)
std_glsl_target(base_position test_bindless_vertext.vert test_triangle.frag)
std_glsl_target(base_min_max_width_height test_bindless_vertext.vert test_triangle.frag)
std_glsl_target(base_width_height test_bindless_vertext.vert test_triangle.frag)

# end
std_glsl_env_destroy()