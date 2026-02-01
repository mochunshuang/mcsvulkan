set(DIR_NAME "mcsvulkan/wsi")
set(EXE_DIR "${CMAKE_SOURCE_DIR}/test/${DIR_NAME}")

set(BASE_LIBS volk vma glfw glm)

macro(add_vulkan_wsi_test fileName)
    string(REPLACE "/" "-" PREFIX_NAME ${DIR_NAME})
    set(TAGET_NAME "${PREFIX_NAME}-${fileName}")
    add_executable(${TAGET_NAME} "${EXE_DIR}/${fileName}.cpp")
    target_link_libraries(${TAGET_NAME} PRIVATE ${BASE_LIBS})
    target_include_directories(${TAGET_NAME} PRIVATE ${Vulkan_Include_DIR})

    # 添加测试
    add_test(NAME "${TAGET_NAME}" COMMAND $<TARGET_FILE:${TAGET_NAME}>)
endmacro()

add_vulkan_wsi_test(test_glfw)

# end
unset(BASE_LIBS)
unset(EXE_DIR)
unset(DIR_NAME)