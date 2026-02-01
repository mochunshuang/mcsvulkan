
message(STATUS ".................................vulakn exec check: [begin].................................")
find_program(Vulkan_slang_EXECUTABLE
    NAMES slangc
    HINTS
    "$ENV{VULKAN_SDK}/Bin"
    "$ENV{VULKAN_SDK}/bin"
)

if(Vulkan_slang_EXECUTABLE)
    message(STATUS "Found slang Shader Compiler under ${Vulkan_slang_EXECUTABLE}")
else()
    message(WARNING "Couldn't find slang Shader Compiler executable, make sure it is present in Vulkan SDK or add it manually via Vulkan_slang_EXECUTABLE cmake variable. Slang shaders won't be compiled.")
endif()

find_program(Vulkan_glslc_EXECUTABLE
    NAMES glslc
    HINTS
    "$ENV{VULKAN_SDK}/Bin"
    "$ENV{VULKAN_SDK}/bin"
)

if(Vulkan_glslc_EXECUTABLE)
    message(STATUS "Found glslc Shader Compiler under ${Vulkan_glslc_EXECUTABLE}")
else()
    message(STATUS "Couldn't find glslc Shader Compiler executable, make sure it is present in Vulkan SDK or add it manually via Vulkan_glslc_EXECUTABLE cmake variable. GLSL shaders won't be compiled.")
endif()

find_program(Vulkan_spirvas_EXECUTABLE
    NAMES spirv-as
    HINTS
    "$ENV{VULKAN_SDK}/Bin"
    "$ENV{VULKAN_SDK}/bin"
)

if(Vulkan_spirvas_EXECUTABLE)
    message(STATUS "Found spirv-as Shader Compiler under ${Vulkan_spirvas_EXECUTABLE}")
else()
    message(STATUS "Couldn't find spirv-as Shader Compiler executable, make sure it is present in Vulkan SDK or add it manually via Vulkan_spirvas_EXECUTABLE cmake variable. SPIR-V assembly shaders won't be compiled.")
endif()

set(Vulkan_Include_DIR "$ENV{VULKAN_SDK}/Include")
message(STATUS "Vulkan_Include_DIR: ${Vulkan_Include_DIR}")

message(STATUS ".................................vulakn exec check: [end].................................")

add_library(vulkan STATIC IMPORTED)
set_target_properties(vulkan PROPERTIES
    IMPORTED_LOCATION "$ENV{VULKAN_SDK}/Lib/vulkan-1.lib"
    INTERFACE_INCLUDE_DIRECTORIES "$ENV{VULKAN_SDK}/Include"
)
target_compile_definitions(vulkan INTERFACE
    VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
)
