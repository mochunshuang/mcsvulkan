cmake_minimum_required(VERSION 4.2.0)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# NOTE: volk 是 C 项目，有C的静态库的，必须这样做
project("mcsvulkan-tool"
    LANGUAGES CXX C
)

# Set language version used
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED YES)
set(CMAKE_C_EXTENSIONS OFF)
message("Main project source dir: ${MAIN_SOURCE_DIR}")

include(${MAIN_SOURCE_DIR}/third_party/zlib.cmake)
include(${MAIN_SOURCE_DIR}/third_party/libpng.cmake)
include(${MAIN_SOURCE_DIR}/third_party/stb.cmake)
include(${MAIN_SOURCE_DIR}/third_party/nlohmann_json.cmake)

include(${MAIN_SOURCE_DIR}/third_party/freetype.cmake)
include(${MAIN_SOURCE_DIR}/third_party/harfbuzz.cmake)

include(${MAIN_SOURCE_DIR}/third_party/msdfgen.cmake)
include(${MAIN_SOURCE_DIR}/third_party/msdf-atlas-gen.cmake)

include(${MAIN_SOURCE_DIR}/third_party/rectpack2D.cmake)

# ------------------------------------------------------
set(DIR_NAME ${PROJECT_NAME})
set(EXE_DIR ${CMAKE_SOURCE_DIR})

set(TOOL_EXE_DIR ${CMAKE_SOURCE_DIR}/tool CACHE STRING "TOOL EXE DIR" FORCE)

set(OUTPUT_DIRECTORY ${MAIN_SOURCE_DIR}/tool)

macro(add_tool_target NAME OUT_NAME)
    set(TARGET_NAME "${DIR_NAME}-${NAME}")
    add_executable(${TARGET_NAME} "${EXE_DIR}/${NAME}.cpp")
    target_link_libraries(${TARGET_NAME} PRIVATE ${LIBS})
    set_target_properties(${TARGET_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIRECTORY}
        OUTPUT_NAME ${OUT_NAME}
    )
endmacro()

set(LIBS freetype harfbuzz harfbuzz-subset)
add_tool_target(hb_subset_tool ${HB_SUBSET_TOOL_NAME})
set(EXE_NAME "${HB_SUBSET_TOOL_NAME}")
set(HB_SUBSET_TOOL_EXE "${OUTPUT_DIRECTORY}/${EXE_NAME}${CMAKE_EXECUTABLE_SUFFIX}" CACHE STRING "HB_SUBSET_TOOL_EXE NAME" FORCE)
message(STATUS "HB_SUBSET_TOOL_EXE: ${HB_SUBSET_TOOL_EXE}")

set(LIBS freetype nlohmann_json stb rectpack2D)
add_tool_target(gen_emoji_atlas ${EMOJI_ATLAS_NAME})
set(EXE_NAME "${EMOJI_ATLAS_NAME}")
set(EMOJI_ATLAS_GEN_EXE "${OUTPUT_DIRECTORY}/${EXE_NAME}${CMAKE_EXECUTABLE_SUFFIX}" CACHE STRING "EMOJI_ATLAS_GEN_EXE NAME" FORCE)
message(STATUS "EMOJI_ATLAS_GEN_EXE: ${EMOJI_ATLAS_GEN_EXE}")

# NOTE: 生成 msdf-atlas-gen
if(TARGET msdf-atlas-gen-standalone)
    message(STATUS "gen target: msdf-atlas-gen-standalone")
    set(EXE_NAME "${MSDF_ATLAS_NAME}")
    set(MSDF_ATLAS_GEN_OUTPUT_DIR "${OUTPUT_DIRECTORY}")

    message(STATUS "${EXE_NAME} out dir: ${MSDF_ATLAS_GEN_OUTPUT_DIR}")
    add_custom_command(
        OUTPUT ${MSDF_ATLAS_GEN_OUTPUT_DIR}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${MSDF_ATLAS_GEN_OUTPUT_DIR}
    )
    set_target_properties(msdf-atlas-gen-standalone PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${MSDF_ATLAS_GEN_OUTPUT_DIR}
        OUTPUT_NAME ${EXE_NAME}
    )

    # 获取可执行文件的完整路径（生成器表达式）
    # set(MSDF_ATLAS_GEN_EXE $<TARGET_FILE:msdf-atlas-gen-standalone>) # 得不到的
    set(MSDF_ATLAS_GEN_EXE "${MSDF_ATLAS_GEN_OUTPUT_DIR}/${EXE_NAME}${CMAKE_EXECUTABLE_SUFFIX}" CACHE STRING "MSDF_ATLAS_GEN_EXE NAME" FORCE)
    message(STATUS "MSDF_ATLAS_GEN_EXE: ${MSDF_ATLAS_GEN_EXE}")
endif()
