
function(auto_compile_glsl_shaders NAME SHADER_INPUT_DIR SHADER_OUTPUT_DIR)
    message(STATUS "auto_compile_glsl_shaders: ${NAME}")
    message(STATUS "SHADER_INPUT_DIR: ${SHADER_INPUT_DIR}")
    message(STATUS "SHADER_OUTPUT_DIR: ${SHADER_OUTPUT_DIR}")

    # SHADER_OUTPUT_DIR taget
    add_custom_command(
        OUTPUT ${SHADER_OUTPUT_DIR}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
    )
    file(GLOB files "${SHADER_INPUT_DIR}/*.vert" "${SHADER_INPUT_DIR}/*.frag")

    foreach(cur_file ${files})
        # 获取文件名（包含扩展名）
        get_filename_component(file_name ${cur_file} NAME)

        set(target_name "${NAME}-${file_name}")
        set(output_file "${SHADER_OUTPUT_DIR}/${file_name}.spv")

        message(STATUS "shader_target_name: ${target_name}")
        message(STATUS "    input_file: ${cur_file}")
        message(STATUS "    output_file: ${output_file}")

        add_compile_glslc_shader(
            ${target_name}
            "${cur_file}"
            "${output_file}"
        )
    endforeach()
endfunction()