set(DIR_NAME "mcsvulkan/tool")
set(EXE_DIR "${CMAKE_SOURCE_DIR}/test/${DIR_NAME}")

set(BASE_LIBS volk vma glfw glm)

macro(add_vulkan_tool_test fileName)
    string(REPLACE "/" "-" PREFIX_NAME ${DIR_NAME})
    set(TAGET_NAME "${PREFIX_NAME}-${fileName}")
    add_executable(${TAGET_NAME} "${EXE_DIR}/${fileName}.cpp")
    target_link_libraries(${TAGET_NAME} PRIVATE ${BASE_LIBS})

    # NOTE: 不再需要。因为 volk 暴露传递了
    # target_include_directories(${TAGET_NAME} PRIVATE ${Vulkan_Include_DIR})

    # 添加测试
    add_test(NAME "${TAGET_NAME}" COMMAND $<TARGET_FILE:${TAGET_NAME}>)
endmacro()

add_vulkan_tool_test(test_pNext)
add_vulkan_tool_test(test_create_instance)
add_vulkan_tool_test(test_enable_intance_bulid)
add_vulkan_tool_test(test_create_debuger)
add_vulkan_tool_test(test_physical_device_selector)
add_vulkan_tool_test(test_queue_family_index_selector)
add_vulkan_tool_test(test_create_logical_device)
add_vulkan_tool_test(test_create_swap_chain)

# end
unset(BASE_LIBS)
unset(EXE_DIR)
unset(DIR_NAME)