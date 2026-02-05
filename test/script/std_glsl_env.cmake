macro(std_glsl_env_init TEST_SUB_DIR)
    set(DIR_NAME "${TEST_SUB_DIR}")
    set(EXE_DIR "${CMAKE_SOURCE_DIR}/test/${DIR_NAME}")
    string(REPLACE "/" "-" GLSL_SHADERS_NAME "${DIR_NAME}-glsl_shaders")
    set(SHADER_DIR "${EXE_DIR}/shaders")
    set(OUTPUT_DIRECTORY ${TEST_EXECUTABLE_OUTPUT_PATH}/${DIR_NAME})
    set(SHADER_OUTPUT_DIR "${OUTPUT_DIRECTORY}/shaders")
    set(BASE_LIBS volk vma glfw glm)
endmacro()

macro(std_glsl_target fileName vertName fragName)
    string(REPLACE "/" "-" PREFIX_NAME ${DIR_NAME})
    set(TARGET_NAME "${PREFIX_NAME}-${fileName}")
    set(VERT_TAGET "${GLSL_SHADERS_NAME}-${vertName}")
    set(FRAG_TAGET "${GLSL_SHADERS_NAME}-${fragName}")
    add_executable(${TARGET_NAME} "${EXE_DIR}/${fileName}.cpp")
    target_link_libraries(${TARGET_NAME} PRIVATE ${BASE_LIBS})

    set_target_properties(${TARGET_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIRECTORY}
        OUTPUT_NAME ${fileName}
    )

    add_dependencies(${TARGET_NAME}
        "${GLSL_SHADERS_NAME}-${vertName}"
        "${GLSL_SHADERS_NAME}-${fragName}"
    )

    message(STATUS "std_glsl_target: ${TARGET_NAME}")
    message(STATUS "    dependencies glsl info:")
    message(STATUS "    vert_taget: ${VERT_TAGET}")
    message(STATUS "    frag_taget: ${FRAG_TAGET}")
    message(STATUS "    SHADER_OUTPUT_DIR: ${SHADER_OUTPUT_DIR}/")
    message(STATUS "    vert_output_file: ${vertName}.spv")
    message(STATUS "    frag_output_file: ${fragName}.spv")
    target_compile_definitions(${TARGET_NAME} PRIVATE
        VERT_SHADER_PATH="${SHADER_OUTPUT_DIR}/${vertName}.spv"
        FRAG_SHADER_PATH="${SHADER_OUTPUT_DIR}/${fragName}.spv"
    )
endmacro()

macro(add_std_glsl_target fileName vertName fragName)
    std_glsl_target(${fileName} ${vertName} ${fragName})

    # 添加测试
    add_test(NAME "${TARGET_NAME}" COMMAND $<TARGET_FILE:${TARGET_NAME}>)
endmacro()

macro(std_glsl_env_destroy)
    unset(BASE_LIBS)
    unset(SHADER_OUTPUT_DIR)
    unset(OUTPUT_DIRECTORY)
    unset(SHADER_DIR)
    unset(GLSL_SHADERS_NAME)
    unset(EXE_DIR)
    unset(DIR_NAME)
endmacro()
