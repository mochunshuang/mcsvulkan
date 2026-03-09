set(DIR_NAME tool)
set(EXE_DIR ${CMAKE_SOURCE_DIR}/test/tool)

set(TOOL_EXE_DIR ${CMAKE_SOURCE_DIR}/tool CACHE STRING "TOOL EXE DIR" FORCE)

set(OUTPUT_DIRECTORY ${TOOL_EXE_DIR})

set(LIBS "")

macro(add_tool_target NAME)
    set(TARGET_NAME "${DIR_NAME}-${NAME}")
    add_executable(${TARGET_NAME} "${EXE_DIR}/${NAME}.cpp")
    target_link_libraries(${TARGET_NAME} PRIVATE ${LIBS})
    set_target_properties(${TARGET_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIRECTORY}
        OUTPUT_NAME ${NAME}
    )
endmacro()

# gen msdf config
set(MSDF_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/msdf" CACHE STRING "MSDF_ATLAS_GEN_EXE NAME" FORCE)
set(FONT_INPUT_DIR "${CMAKE_SOURCE_DIR}/assets/font" CACHE STRING "MSDF_ATLAS_GEN_EXE NAME" FORCE)
set(CHASET_INPUT_DIR "${CMAKE_SOURCE_DIR}/assets/charset" CACHE STRING "MSDF_ATLAS_GEN_EXE NAME" FORCE)

function(ADD_MSDF_DEF TARGET_NAME)
    target_compile_definitions(${TARGET_NAME} PRIVATE MSDF_OUTPUT_DIR="${MSDF_OUTPUT_DIR}" FONT_INPUT_DIR="${FONT_INPUT_DIR}" CHASET_INPUT_DIR="${CHASET_INPUT_DIR}")
endfunction()

if(TARGET msdf-atlas-gen-standalone)
    message(STATUS "gen target: msdf-atlas-gen-standalone")
    set(EXE_NAME "msdf-atlas-gen")
    set(MSDF_ATLAS_GEN_OUTPUT_DIR "${TOOL_EXE_DIR}")

    message(STATUS "${EXE_NAME} out dir: ${MSDF_ATLAS_GEN_OUTPUT_DIR}")
    add_custom_command(
        OUTPUT ${MSDF_ATLAS_GEN_OUTPUT_DIR}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${MSDF_ATLAS_GEN_OUTPUT_DIR}
    )
    set_target_properties(msdf-atlas-gen-standalone PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${MSDF_ATLAS_GEN_OUTPUT_DIR}
        OUTPUT_NAME ${EXE_NAME}
    )

    add_custom_command(
        OUTPUT ${MSDF_OUTPUT_DIR}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${MSDF_OUTPUT_DIR}
    )

    # 获取可执行文件的完整路径（生成器表达式）
    # set(MSDF_ATLAS_GEN_EXE $<TARGET_FILE:msdf-atlas-gen-standalone>) # 得不到的
    set(MSDF_ATLAS_GEN_EXE "${MSDF_ATLAS_GEN_OUTPUT_DIR}/${EXE_NAME}${CMAKE_EXECUTABLE_SUFFIX}" CACHE STRING "MSDF_ATLAS_GEN_EXE NAME" FORCE)
    message(STATUS "MSDF_ATLAS_GEN_EXE: ${MSDF_ATLAS_GEN_EXE}")

    # 定义模板函数，用于生成不同字体的图集
    function(generate_msdf_atlas)
        set(oneValueArgs TARGET_NAME FONT_PATH OUTPUT_NAME TYPE SIZE PX_RANGE CHARSET OUTPUT_DIR)
        cmake_parse_arguments(GEN "" "${oneValueArgs}" "" ${ARGN})

        # 设置默认值
        if(NOT GEN_FONT_PATH)
            message(FATAL_ERROR "generate_msdf_atlas: FONT_PATH is required")
        endif()

        if(NOT GEN_OUTPUT_NAME)
            message(FATAL_ERROR "generate_msdf_atlas: OUTPUT_NAME is required")
        endif()

        if(NOT GEN_OUTPUT_DIR)
            set(GEN_OUTPUT_DIR "${MSDF_OUTPUT_DIR}")
        endif()

        if(NOT GEN_TYPE)
            set(GEN_TYPE "msdf")
        endif()

        if(NOT GEN_SIZE)
            set(GEN_SIZE "32")
        endif()

        if(NOT GEN_PX_RANGE)
            set(GEN_PX_RANGE "8")
        endif()

        # 定义输出文件
        set(ATLAS_PNG "${GEN_OUTPUT_DIR}/${GEN_OUTPUT_NAME}.png")
        set(ATLAS_JSON "${GEN_OUTPUT_DIR}/${GEN_OUTPUT_NAME}.json")

        # 获取字体扩展名，构造复制后的文件名
        get_filename_component(FONT_EXT "${GEN_FONT_PATH}" LAST_EXT)
        set(FONT_COPY "${GEN_OUTPUT_DIR}/${GEN_OUTPUT_NAME}${FONT_EXT}")

        # 构建命令参数列表
        set(CMD_ARGS
            ${MSDF_ATLAS_GEN_EXE}
            -font "${GEN_FONT_PATH}"
            -type ${GEN_TYPE}
            -size ${GEN_SIZE}
            -pxrange ${GEN_PX_RANGE}
            -imageout ${ATLAS_PNG}
            -json ${ATLAS_JSON}
        )

        # 处理字符集参数
        if(GEN_CHARSET)
            if(EXISTS "${GEN_CHARSET}")
                list(APPEND CMD_ARGS -charset "${GEN_CHARSET}")
            elseif(NOT GEN_CHARSET STREQUAL "ascii")
                # 非 ascii 且不是文件，则作为内联字符串传递
                list(APPEND CMD_ARGS -chars "\"${GEN_CHARSET}\"")
            endif()

            # 如果 GEN_CHARSET 是 "ascii"，不添加任何参数（默认）
        endif()

        add_custom_command(
            OUTPUT ${ATLAS_PNG} ${ATLAS_JSON}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_OUTPUT_DIR}
            COMMAND ${CMD_ARGS}

            # 继续添加一个命令
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GEN_FONT_PATH}" "${FONT_COPY}"
            DEPENDS msdf-atlas-gen-standalone "${GEN_FONT_PATH}"
            COMMENT "Generating MSDF atlas for ${GEN_OUTPUT_NAME} and copying font as ${GEN_OUTPUT_NAME}${FONT_EXT}"
        )

        if(GEN_TARGET_NAME)
            add_custom_target(${GEN_TARGET_NAME} DEPENDS ${ATLAS_PNG} ${ATLAS_JSON})
        endif()
    endfunction()

    # 使用示例 1：英文（默认 ASCII）
    generate_msdf_atlas(
        TARGET_NAME generate_english_atlas
        FONT_PATH "${FONT_INPUT_DIR}/TiroBangla-Regular.ttf"
        OUTPUT_NAME "tirobangla_ascii"
        PX_RANGE 2 # 用官方默认值

        # CHARSET 不传或传 "ascii" 都不添加参数，使用默认 ASCII
    )

    if(WIN32)
        # # 使用示例 2：中文（假设有中文字体 simhei.ttf 和常用汉字列表 chinese_common.txt）
        generate_msdf_atlas(
            TARGET_NAME generate_chinese_atlas
            FONT_PATH "C:/Windows/Fonts/msyh.ttc" # 中文字体
            OUTPUT_NAME "msyh_chinese"
            PX_RANGE 2 # 必须用官方默认值，不要乱改
            SIZE 96 # 建议提高精度
            CHARSET "${CHASET_INPUT_DIR}/small_chinese_common.txt"
        )

        # # # 使用示例 3：表情符号（假设有表情字体 seguiemj.ttf 和表情列表 emoji.txt）
        # generate_msdf_atlas(
        # TARGET_NAME generate_emoji_atlas
        # FONT_PATH "C:/Windows/Fonts/seguiemj.ttf"
        # OUTPUT_NAME "emoji"
        # PX_RANGE 2 # 必须用官方默认值，不要乱改
        # SIZE 96 # 建议提高精度
        # CHARSET "${CHASET_INPUT_DIR}/small_emoji.txt"
        # )
    endif(WIN32)
endif(TARGET msdf-atlas-gen-standalone)

set(LIBS freetype nlohmann_json stb rectpack2D)
add_tool_target(gen_emoji_atlas)

set(EXE_NAME "gen_emoji_atlas")
set(EMOJI_ATLAS_GEN_EXE "${TOOL_EXE_DIR}/${EXE_NAME}${CMAKE_EXECUTABLE_SUFFIX}" CACHE STRING "EMOJI_ATLAS_GEN_EXE NAME" FORCE)
message(STATUS "EMOJI_ATLAS_GEN_EXE: ${EMOJI_ATLAS_GEN_EXE}")

# 使用示例 3：表情符号 使用自定义生成
# 设置 emoji 图集输出目录
set(EMOJI_OUTPUT_DIR "${MSDF_OUTPUT_DIR}" CACHE STRING "Directory for emoji atlases")

# 定义生成 emoji 图集的函数
function(generate_emoji_atlas)
    set(oneValueArgs TARGET_NAME FONT_PATH OUTPUT_NAME CHARSET SIZE PADDING MAX_SIZE OUTPUT_DIR)
    cmake_parse_arguments(GEN "" "${oneValueArgs}" "" ${ARGN})

    # 必需参数检查
    if(NOT GEN_FONT_PATH)
        message(FATAL_ERROR "generate_emoji_atlas: FONT_PATH is required")
    endif()

    if(NOT GEN_OUTPUT_NAME)
        message(FATAL_ERROR "generate_emoji_atlas: OUTPUT_NAME is required")
    endif()

    if(NOT GEN_CHARSET)
        message(FATAL_ERROR "generate_emoji_atlas: CHARSET is required")
    endif()

    if(NOT EXISTS "${GEN_CHARSET}")
        message(FATAL_ERROR "generate_emoji_atlas: CHARSET file '${GEN_CHARSET}' does not exist")
    endif()

    # 设置默认值
    if(NOT GEN_OUTPUT_DIR)
        set(GEN_OUTPUT_DIR "${EMOJI_OUTPUT_DIR}")
    endif()

    if(NOT GEN_SIZE)
        set(GEN_SIZE "64")
    endif()

    if(NOT GEN_PADDING)
        set(GEN_PADDING "1")
    endif()

    if(NOT GEN_MAX_SIZE)
        set(GEN_MAX_SIZE "4096")
    endif()

    # 定义输出文件
    set(ATLAS_PNG "${GEN_OUTPUT_DIR}/${GEN_OUTPUT_NAME}.png")
    set(ATLAS_JSON "${GEN_OUTPUT_DIR}/${GEN_OUTPUT_NAME}.json")

    # 获取字体扩展名，构造复制后的文件名（可选）
    get_filename_component(FONT_EXT "${GEN_FONT_PATH}" LAST_EXT)
    set(FONT_COPY "${GEN_OUTPUT_DIR}/${GEN_OUTPUT_NAME}${FONT_EXT}")

    # 构建命令行参数
    set(CMD_ARGS
        ${EMOJI_ATLAS_GEN_EXE}
        -font "${GEN_FONT_PATH}"
        -charset "${GEN_CHARSET}"
        -size ${GEN_SIZE}
        -padding ${GEN_PADDING}
        -max-size ${GEN_MAX_SIZE}
        -file_name "${GEN_OUTPUT_DIR}/${GEN_OUTPUT_NAME}"
    )

    add_custom_command(
        OUTPUT ${ATLAS_PNG} ${ATLAS_JSON}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_OUTPUT_DIR}
        COMMAND ${CMD_ARGS}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GEN_FONT_PATH}" "${FONT_COPY}"
        DEPENDS ${EMOJI_ATLAS_GEN_EXE} "${GEN_FONT_PATH}"
        COMMENT "Generating emoji atlas for ${GEN_OUTPUT_NAME} and copying font"
    )

    if(GEN_TARGET_NAME)
        add_custom_target(${GEN_TARGET_NAME} DEPENDS ${ATLAS_PNG} ${ATLAS_JSON})
    endif()
endfunction()

# 使用示例：生成 emoji 图集
if(WIN32)
    generate_emoji_atlas(
        TARGET_NAME generate_emoji_atlas_custom
        FONT_PATH "C:/Windows/Fonts/seguiemj.ttf"
        OUTPUT_NAME "emoji"
        CHARSET "${CHASET_INPUT_DIR}/small_emoji.txt"
        SIZE 96
        PADDING 2
        MAX_SIZE 4096

        # OUTPUT_DIR 可选，默认使用 ${EMOJI_OUTPUT_DIR}
    )
endif()