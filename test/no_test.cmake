set(DIR_NAME no_test)
set(EXE_DIR ${CMAKE_SOURCE_DIR}/test/${DIR_NAME})
set(OUTPUT_DIRECTORY ${TEST_EXECUTABLE_OUTPUT_PATH}/${DIR_NAME})

# NOTE: 已经再主线加上了
# set(LIB stdc++exp)
# link_libraries(stdc++exp)
macro(add_no_test_target NAME)
    set(TARGET_NAME "${DIR_NAME}-${NAME}")
    add_executable(${TARGET_NAME} "${EXE_DIR}/${NAME}.cpp")
    target_link_libraries(${TARGET_NAME} PRIVATE ${LIB})
    set_target_properties(${TARGET_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIRECTORY}
        OUTPUT_NAME ${NAME}
    )
endmacro()

add_no_test_target(print)
add_no_test_target(reflection)

# target_compile_options(no_test-reflection PRIVATE -freflection) # 打开反射特性