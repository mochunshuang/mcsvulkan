# 1. 添加模块
# git submodule add https://gh-proxy.org/https://github.com/ocornut/imgui.git third_party/imgui

# 2. 进入 imgui 目录
# cd third_party/imgui

# 查看可以checkout的：
# git tag -l | sort -V

# 3. 切换到指定 tag
# git checkout v1.92.7

# 验证确实在标签上
# git describe --tags

# 回到项目根目录
# cd ../..

# 提交子模块状态的更改
# git add third_party/imgui

# git commit -m "Pin imgui to v1.92.7"

# NOTE: 查看项目的所有子模块
# git submodule status

set(IMGUI_DIR ${CMAKE_CURRENT_LIST_DIR}/imgui)
file(GLOB IMGUI_SRC ${IMGUI_DIR}/*.cpp)
message(STATUS "IMGUI_SRC: ${IMGUI_SRC}")

set(IMGUI_VULKAN_BACKENDS_DIR "${IMGUI_DIR}/backends")
set(IMGUI_BACKENDS
    "${IMGUI_VULKAN_BACKENDS_DIR}/imgui_impl_glfw.cpp"
    "${IMGUI_VULKAN_BACKENDS_DIR}/imgui_impl_vulkan.cpp"
)
message(STATUS "IMGUI_BACKENDS: ${IMGUI_BACKENDS}")

set(GLFW_DIR ${CMAKE_CURRENT_LIST_DIR}/glfw/include)

# ----------------------------------------
# 着色器编译（不变）
# ----------------------------------------
set(IMGUI_VULKAN_SHADER_DIR "${IMGUI_VULKAN_BACKENDS_DIR}/vulkan")
set(SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)

add_custom_command(
    OUTPUT ${SHADER_OUTPUT_DIR}/glsl_shader.vert.spv
    COMMAND ${Vulkan_glslc_EXECUTABLE}
    -fshader-stage=vert
    ${IMGUI_VULKAN_BACKENDS_DIR}/vulkan/glsl_shader.vert
    -o ${SHADER_OUTPUT_DIR}/glsl_shader.vert.spv
    --target-env=vulkan1.4
    DEPENDS ${IMGUI_VULKAN_BACKENDS_DIR}/vulkan/glsl_shader.vert
    COMMENT "Compiling vertex shader"
)

add_custom_command(
    OUTPUT ${SHADER_OUTPUT_DIR}/glsl_shader.frag.spv
    COMMAND ${Vulkan_glslc_EXECUTABLE}
    -fshader-stage=frag
    ${IMGUI_VULKAN_BACKENDS_DIR}/vulkan/glsl_shader.frag
    -o ${SHADER_OUTPUT_DIR}/glsl_shader.frag.spv
    --target-env=vulkan1.4
    DEPENDS ${IMGUI_VULKAN_BACKENDS_DIR}/vulkan/glsl_shader.frag
    COMMENT "Compiling fragment shader"
)

add_custom_target(imgui_shaders ALL
    DEPENDS
    ${SHADER_OUTPUT_DIR}/glsl_shader.vert.spv
    ${SHADER_OUTPUT_DIR}/glsl_shader.frag.spv
)

# -------------------------------
# 1. 标准版 imgui 库（无任何特殊宏）
# -------------------------------
add_library(imgui STATIC ${IMGUI_SRC} ${IMGUI_BACKENDS})
target_include_directories(imgui SYSTEM PUBLIC
    ${IMGUI_DIR} ${IMGUI_VULKAN_BACKENDS_DIR} ${Vulkan_Include_DIR} ${GLFW_DIR}
)
target_link_libraries(imgui PRIVATE glfw vulkan)
target_compile_definitions(imgui
    PUBLIC
    IMGUI_VULKAN_SHADER_DIR="${SHADER_OUTPUT_DIR}"
)
add_dependencies(imgui imgui_shaders)

# -------------------------------
# 2. volk 专用版 imgui 库（加 NO_PROTOTYPES）
# -------------------------------
add_library(imgui_volk STATIC ${IMGUI_SRC} ${IMGUI_BACKENDS})
target_include_directories(imgui_volk SYSTEM PUBLIC
    ${IMGUI_DIR} ${IMGUI_VULKAN_BACKENDS_DIR} ${Vulkan_Include_DIR} ${GLFW_DIR}
)
target_link_libraries(imgui_volk PRIVATE glfw vulkan)
target_compile_definitions(imgui_volk
    PUBLIC
    IMGUI_VULKAN_SHADER_DIR="${SHADER_OUTPUT_DIR}"
    PRIVATE
    IMGUI_IMPL_VULKAN_NO_PROTOTYPES
)
add_dependencies(imgui_volk imgui_shaders)

# -------------------------------
# 官方示例（链接标准版）
# -------------------------------
add_executable(example_glfw_vulkan ${IMGUI_DIR}/examples/example_glfw_vulkan/main.cpp)
target_link_libraries(example_glfw_vulkan PRIVATE imgui)