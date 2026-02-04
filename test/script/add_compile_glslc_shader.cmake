# E:/mysoftware/VulkanSDK/1.4.321.1/Bin/glslc.exe 27_shader_textures.vert -o 27_vert.spv
# E:/mysoftware/VulkanSDK/1.4.321.1/Bin/glslc.exe 27_shader_textures.frag -o 27_frag.spv
macro(add_compile_glslc_shader TARGET_NAME input_file out_file)
    add_custom_target(${TARGET_NAME}
        COMMAND ${Vulkan_glslc_EXECUTABLE}
        ${input_file}
        -o ${out_file}
        DEPENDS ${SHADER_OUTPUT_DIR}
        COMMENT "Compiling glslc shader: ${TARGET_NAME}"
        VERBATIM
    )
endmacro()