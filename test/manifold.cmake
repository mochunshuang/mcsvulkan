# 查看直接依赖的库接口传递的链接库
get_target_property(manifold_interface manifold INTERFACE_LINK_LIBRARIES)
message(STATUS "manifold INTERFACE_LINK_LIBRARIES: ${manifold_interface}")

# 如果 manifold 是导入目标，检查 IMPORTED_LINK_INTERFACE_LIBRARIES
get_target_property(manifold_imported_interface manifold IMPORTED_LINK_INTERFACE_LIBRARIES)
message(STATUS "manifold IMPORTED_LINK_INTERFACE_LIBRARIES: ${manifold_imported_interface}")

std_glsl_env_init("manifold")
set(BASE_LIBS volk vma glfw glm stb nlohmann_json
    freetype harfbuzz SheenBidi libunibreak utf8proc manifold
)
auto_compile_glsl_shaders(${GLSL_SHADERS_NAME} ${SHADER_DIR} ${SHADER_OUTPUT_DIR})

std_glsl_target(test_triangle test_bindless_vertext.vert test_triangle.frag)

# 获取 test_triangle 的 LINK_LIBRARIES 属性
get_target_property(test_link_libs manifold-test_triangle LINK_LIBRARIES)
message(STATUS "test_triangle LINK_LIBRARIES: ${test_link_libs}")

# 获取所有直接依赖（包括通过 INTERFACE 传递的）
get_target_property(test_link_deps manifold-test_triangle LINK_DEPENDENT_LIBRARIES)
message(STATUS "test_triangle LINK_DEPENDENT_LIBRARIES: ${test_link_deps}")

foreach(lib IN LISTS ${test_link_deps})
    if(TARGET ${lib})
        get_target_property(iface ${lib} INTERFACE_LINK_LIBRARIES)
        message(STATUS "Target ${lib} INTERFACE_LINK_LIBRARIES = ${iface}")
        get_target_property(loc ${lib} IMPORTED_LOCATION)

        if(loc)
            message(STATUS "  Imported location: ${loc}")
        endif()
    else()
        message(STATUS "${lib} is not a CMake target (plain library name)")
    endif()
endforeach()

std_glsl_target(test_manifold test_bindless_vertext.vert test_triangle.frag)
std_glsl_target(base_triangulation test_bindless_vertext.vert test_triangle.frag)

# end
std_glsl_env_destroy()