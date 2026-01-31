set(DIR_NAME base)
set(EXE_DIR ${CMAKE_SOURCE_DIR}/test/${DIR_NAME})

set(BASE_LIBS volk vma glfw glm)

macro(add_base_test fileName)
    set(TAGET_NAME "${DIR_NAME}-${fileName}")
    add_executable(${TAGET_NAME} ${EXE_DIR}/${fileName}.cpp)
    target_link_libraries(${TAGET_NAME} PRIVATE ${BASE_LIBS})
    target_include_directories(${TAGET_NAME} PRIVATE ${Vulkan_Include_DIR})
endmacro()

add_base_test(test_layer)
add_base_test(test_instanceExtension)
add_base_test(test_Profiles)

# end
unset(BASE_LIBS)
unset(EXE_DIR)
unset(DIR_NAME)