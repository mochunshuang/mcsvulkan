macro(mcs_vulkan_env_init TEST_SUB_DIR)
    set(DIR_NAME "${TEST_SUB_DIR}")
    set(EXE_DIR "${CMAKE_SOURCE_DIR}/test/${DIR_NAME}")
    set(OUTPUT_DIRECTORY ${TEST_EXECUTABLE_OUTPUT_PATH}/${DIR_NAME})
    set(BASE_LIBS ${MCSVULKAN_LIBS})
endmacro()

macro(mcs_vulkan_target fileName)
    string(REPLACE "/" "-" PREFIX_NAME ${DIR_NAME})
    set(TARGET_NAME "${PREFIX_NAME}-${fileName}")
    add_executable(${TARGET_NAME} "${EXE_DIR}/${fileName}.cpp")
    target_link_libraries(${TARGET_NAME} PRIVATE ${BASE_LIBS})

    set_target_properties(${TARGET_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIRECTORY}
        OUTPUT_NAME ${fileName}
    )
    message(STATUS "mcs_vulkan_target: ${TARGET_NAME}")
endmacro()

macro(add_mcs_vulkan_target fileName)
    mcs_vulkan_target(${fileName})

    # 添加测试
    add_test(NAME "${TARGET_NAME}" COMMAND $<TARGET_FILE:${TARGET_NAME}>)
endmacro()

macro(mcs_vulkan_env_destroy)
    unset(BASE_LIBS)
    unset(OUTPUT_DIRECTORY)
    unset(EXE_DIR)
    unset(DIR_NAME)
endmacro()
