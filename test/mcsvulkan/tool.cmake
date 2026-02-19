std_glsl_env_init("mcsvulkan/tool")

auto_compile_glsl_shaders(${GLSL_SHADERS_NAME} ${SHADER_DIR} ${SHADER_OUTPUT_DIR})

macro(add_vulkan_tool_test fileName)
    string(REPLACE "/" "-" PREFIX_NAME ${DIR_NAME})
    set(TAGET_NAME "${PREFIX_NAME}-${fileName}")
    add_executable(${TAGET_NAME} "${EXE_DIR}/${fileName}.cpp")
    target_link_libraries(${TAGET_NAME} PRIVATE ${BASE_LIBS})

    set_target_properties(${TAGET_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIRECTORY}
        OUTPUT_NAME ${fileName}
    )

    # 添加测试
    add_test(NAME "${TAGET_NAME}" COMMAND $<TARGET_FILE:${TAGET_NAME}>)
endmacro()

add_vulkan_tool_test(test_pNext)
add_vulkan_tool_test(test_create_instance)
add_vulkan_tool_test(test_enable_intance_build)
add_vulkan_tool_test(test_create_debuger)
add_vulkan_tool_test(test_physical_device_selector)
add_vulkan_tool_test(test_queue_family_index_selector)
add_vulkan_tool_test(test_create_logical_device)
add_vulkan_tool_test(test_create_swap_chain)
add_vulkan_tool_test(test_create_command_pool)
add_vulkan_tool_test(test_frame_context)
add_vulkan_tool_test(test_format)

add_std_glsl_target(test_create_pipeline_layout test_triangle.vert test_triangle.frag)
add_std_glsl_target(test_create_graphics_pipeline test_triangle.vert test_triangle.frag)

# end
std_glsl_env_destroy()