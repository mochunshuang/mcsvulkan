set(DIR_NAME env)
set(EXE_DIR ${CMAKE_SOURCE_DIR}/test/env)

# volk
set(TAGET_NAME "${DIR_NAME}-test_volk")
add_executable(${TAGET_NAME} ${EXE_DIR}/test_volk.cpp)
target_link_libraries(${TAGET_NAME} PRIVATE volk)
target_include_directories(${TAGET_NAME} PRIVATE ${Vulkan_Include_DIR})

set(TAGET_NAME "${DIR_NAME}-test_volk2")
add_executable(${TAGET_NAME} ${EXE_DIR}/test_volk2.cpp)
target_link_libraries(${TAGET_NAME} PRIVATE volk)
target_include_directories(${TAGET_NAME} PRIVATE ${Vulkan_Include_DIR})

set(TAGET_NAME "${DIR_NAME}-test_volk_device_table")
add_executable(${TAGET_NAME} ${EXE_DIR}/test_volk_device_table.cpp)
target_link_libraries(${TAGET_NAME} PRIVATE volk)
target_include_directories(${TAGET_NAME} PRIVATE ${Vulkan_Include_DIR})

set(TAGET_NAME "${DIR_NAME}-test_volk_with_validation")
add_executable(${TAGET_NAME} ${EXE_DIR}/test_volk_with_validation.cpp)
target_link_libraries(${TAGET_NAME} PRIVATE volk)
target_include_directories(${TAGET_NAME} PRIVATE ${Vulkan_Include_DIR})

# vma
set(TAGET_NAME "${DIR_NAME}-test_vma")
add_executable(${TAGET_NAME} ${EXE_DIR}/test_vma.cpp)
target_link_libraries(${TAGET_NAME} PRIVATE volk vma)
target_include_directories(${TAGET_NAME} PRIVATE ${Vulkan_Include_DIR})

# glfw3
set(TAGET_NAME "${DIR_NAME}-test_glfw")
add_executable(${TAGET_NAME} ${EXE_DIR}/test_glfw.cpp)
target_link_libraries(${TAGET_NAME} PRIVATE volk vma glfw glm)
target_include_directories(${TAGET_NAME} PRIVATE ${Vulkan_Include_DIR})

set(TAGET_NAME "${DIR_NAME}-test_glfw2")
add_executable(${TAGET_NAME} ${EXE_DIR}/test_glfw2.cpp)
target_link_libraries(${TAGET_NAME} PRIVATE volk vma glfw glm)
target_include_directories(${TAGET_NAME} PRIVATE ${Vulkan_Include_DIR})

if(WIN32)
    set(TAGET_NAME "${DIR_NAME}-test_vulkan_minimal")
    add_executable(${TAGET_NAME} ${EXE_DIR}/test_vulkan_minimal.cpp)
    target_include_directories(${TAGET_NAME} PRIVATE ${Vulkan_Include_DIR})
endif(WIN32)

# vulkan
set(TAGET_NAME "${DIR_NAME}-test_vulkan")
add_executable(${TAGET_NAME} ${EXE_DIR}/test_vulkan.cpp)
target_link_libraries(${TAGET_NAME} PRIVATE vulkan)
target_include_directories(${TAGET_NAME} PRIVATE ${Vulkan_Include_DIR})

set(TAGET_NAME "${DIR_NAME}-test_vulkan_hpp")
add_executable(${TAGET_NAME} ${EXE_DIR}/test_vulkan_hpp.cpp)
target_link_libraries(${TAGET_NAME} PRIVATE vulkan)
target_include_directories(${TAGET_NAME} PRIVATE ${Vulkan_Include_DIR})

# end
unset(EXE_DIR)
unset(DIR_NAME)