
function(add_glsl_shader_dependencies TARGET_NAME VERT_NAME FRAG_NAME)
    add_dependencies("${PREFIX_NAME}-${TARGET_NAME}"
        "${GLSL_SHADERS_NAME}-${VERT_NAME}"
        "${GLSL_SHADERS_NAME}-${FRAG_NAME}"
    )
endfunction(add_glsl_shader_dependencies TARGET_NAME VERT_NAME FRAG_NAME)
