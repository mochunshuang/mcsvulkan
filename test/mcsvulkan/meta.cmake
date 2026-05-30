set(DIR_NAME "mcsvulkan/meta")
set(EXE_DIR "${CMAKE_SOURCE_DIR}/test/${DIR_NAME}")

set(BASE_LIBS ${MCSVULKAN_LIBS})

macro(add_vulkan_meta_test fileName)
    string(REPLACE "/" "-" PREFIX_NAME ${DIR_NAME})
    set(TAGET_NAME "${PREFIX_NAME}-${fileName}")
    add_executable(${TAGET_NAME} "${EXE_DIR}/${fileName}.cpp")
    target_link_libraries(${TAGET_NAME} PRIVATE ${BASE_LIBS})

    # 添加测试
    add_test(NAME "${TAGET_NAME}" COMMAND $<TARGET_FILE:${TAGET_NAME}>)
endmacro()

add_vulkan_meta_test(test_make_aggregate)
add_vulkan_meta_test(test_identifier_view)
add_vulkan_meta_test(test_type)
add_vulkan_meta_test(test_value)
add_vulkan_meta_test(test_is_info)
add_vulkan_meta_test(test_has_all_annotations)
add_vulkan_meta_test(test_contains_nonstatic_data_members)

# end
unset(BASE_LIBS)
unset(EXE_DIR)
unset(DIR_NAME)