set(DIR_NAME tool)
set(EXE_DIR ${CMAKE_SOURCE_DIR}/test/tool)

set(TOOL_EXE_DIR ${CMAKE_SOURCE_DIR}/tool CACHE STRING "TOOL EXE DIR" FORCE)

set(OUTPUT_DIRECTORY ${TOOL_EXE_DIR})

macro(add_tool_target NAME)
    set(TARGET_NAME "${DIR_NAME}-${NAME}")
    add_executable(${TARGET_NAME} "${EXE_DIR}/${NAME}.cpp")
    target_link_libraries(${TARGET_NAME} PRIVATE ${LIBS})
    set_target_properties(${TARGET_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIRECTORY}
        OUTPUT_NAME ${NAME}
    )
endmacro()

set(LIBS freetype harfbuzz harfbuzz-subset)
add_tool_target(hb_subset_tool)
set(EXE_NAME "hb_subset_tool")
set(HB_SUBSET_TOOL_EXE "${TOOL_EXE_DIR}/${EXE_NAME}${CMAKE_EXECUTABLE_SUFFIX}" CACHE STRING "HB_SUBSET_TOOL_EXE NAME" FORCE)
message(STATUS "HB_SUBSET_TOOL_EXE: ${HB_SUBSET_TOOL_EXE}")

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
        get_filename_component(FONT_EXT "${GEN_FONT_PATH}" LAST_EXT)
        set(FONT_FILE "${GEN_OUTPUT_DIR}/${GEN_OUTPUT_NAME}${FONT_EXT}")

        # 判断是否需要裁剪：仅当 CHARSET 是存在的文件时
        set(SHOULD_SUBSET FALSE)

        if(GEN_CHARSET AND EXISTS "${GEN_CHARSET}")
            set(SHOULD_SUBSET TRUE)
        endif()

        if(SHOULD_SUBSET)
            # 第一步：裁剪字体
            add_custom_command(
                OUTPUT ${FONT_FILE}
                COMMAND ${HB_SUBSET_TOOL_EXE} "${GEN_FONT_PATH}" "${FONT_FILE}" "${GEN_CHARSET}"
                DEPENDS ${HB_SUBSET_TOOL_EXE} "${GEN_FONT_PATH}" "${GEN_CHARSET}"
                COMMENT "Subsetting font to ${FONT_FILE}"
            )
            set(FONT_DEP ${FONT_FILE})
        else()
            # 不裁剪，直接复制原字体
            add_custom_command(
                OUTPUT ${FONT_FILE}
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GEN_FONT_PATH}" "${FONT_FILE}"
                DEPENDS "${GEN_FONT_PATH}"
                COMMENT "Copying font to ${FONT_FILE}"
            )
            set(FONT_DEP ${FONT_FILE})
        endif()

        # 构建 msdf-atlas-gen 命令参数
        set(CMD_ARGS
            ${MSDF_ATLAS_GEN_EXE}
            -font "${FONT_FILE}" # 使用最终的字体文件
            -type ${GEN_TYPE}
            -size ${GEN_SIZE}
            -pxrange ${GEN_PX_RANGE}
            -imageout ${ATLAS_PNG}
            -json ${ATLAS_JSON}
        )

        # 处理字符集参数（原样传递给 msdf-atlas-gen）
        if(GEN_CHARSET)
            if(EXISTS "${GEN_CHARSET}")
                list(APPEND CMD_ARGS -charset "${GEN_CHARSET}")
            elseif(NOT GEN_CHARSET STREQUAL "ascii")
                list(APPEND CMD_ARGS -chars "\"${GEN_CHARSET}\"")
            endif()
        endif()

        add_custom_command(
            OUTPUT ${ATLAS_PNG} ${ATLAS_JSON}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_OUTPUT_DIR}
            COMMAND ${CMD_ARGS}
            DEPENDS ${MSDF_ATLAS_GEN_EXE} ${FONT_DEP} ${GEN_CHARSET}
            COMMENT "Generating MSDF atlas for ${GEN_OUTPUT_NAME}"
        )

        if(GEN_TARGET_NAME)
            add_custom_target(${GEN_TARGET_NAME} DEPENDS ${ATLAS_PNG} ${ATLAS_JSON} ${FONT_FILE})
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

        # 使用示例 4：阿拉伯测试BIDI
        generate_msdf_atlas(
            TARGET_NAME generate_segoeui_atlas
            FONT_PATH "C:/Windows/Fonts/segoeui.ttf" # 中文字体
            OUTPUT_NAME "segoe_arabic"
            PX_RANGE 2 # 默认值
            SIZE 96 # 建议提高精度
            CHARSET "${CHASET_INPUT_DIR}/arabic_charset.txt"
        )
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
    get_filename_component(FONT_EXT "${GEN_FONT_PATH}" LAST_EXT)
    set(FONT_FILE "${GEN_OUTPUT_DIR}/${GEN_OUTPUT_NAME}${FONT_EXT}")

    # 判断是否需要裁剪：仅当提供了有效的字符集文件时
    set(SHOULD_SUBSET FALSE)

    if(GEN_CHARSET AND EXISTS "${GEN_CHARSET}")
        set(SHOULD_SUBSET TRUE)
    endif()

    if(SHOULD_SUBSET)
        # 第一步：裁剪字体
        add_custom_command(
            OUTPUT ${FONT_FILE}
            COMMAND ${HB_SUBSET_TOOL_EXE} "${GEN_FONT_PATH}" "${FONT_FILE}" "${GEN_CHARSET}"
            DEPENDS ${HB_SUBSET_TOOL_EXE} "${GEN_FONT_PATH}" "${GEN_CHARSET}"
            COMMENT "Subsetting font to ${FONT_FILE}"
        )
        set(FONT_DEP ${FONT_FILE})
        set(CHARSET_ARG "-charset" "${GEN_CHARSET}")
    else()
        # 不裁剪，直接复制原字体
        add_custom_command(
            OUTPUT ${FONT_FILE}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GEN_FONT_PATH}" "${FONT_FILE}"
            DEPENDS "${GEN_FONT_PATH}"
            COMMENT "Copying font to ${FONT_FILE}"
        )
        set(FONT_DEP ${FONT_FILE})

        # 如果没有提供字符集文件，不传递 -charset 参数（gen_emoji_atlas 可能会默认处理所有字符？）
        # 注意：gen_emoji_atlas 可能需要字符集，若未提供可能出错，这里根据实际情况处理
        if(GEN_CHARSET)
            # 如果 GEN_CHARSET 是内联字符串，可能需要特殊处理，但这里简化处理
            message(WARNING "No valid charset file provided for ${GEN_OUTPUT_NAME}, skipping subset but passing -charset argument as is.")
            set(CHARSET_ARG "-charset" "${GEN_CHARSET}")
        else()
            set(CHARSET_ARG "")
        endif()
    endif()

    # 构建 gen_emoji_atlas 命令参数
    set(CMD_ARGS
        ${EMOJI_ATLAS_GEN_EXE}
        -font "${FONT_FILE}"
        -size ${GEN_SIZE}
        -padding ${GEN_PADDING}
        -max-size ${GEN_MAX_SIZE}
        -file_name "${GEN_OUTPUT_DIR}/${GEN_OUTPUT_NAME}"
    )

    if(CHARSET_ARG)
        list(APPEND CMD_ARGS ${CHARSET_ARG})
    endif()

    add_custom_command(
        OUTPUT ${ATLAS_PNG} ${ATLAS_JSON}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_OUTPUT_DIR}
        COMMAND ${CMD_ARGS}
        DEPENDS ${EMOJI_ATLAS_GEN_EXE} ${FONT_DEP} ${GEN_CHARSET}
        COMMENT "Generating emoji atlas for ${GEN_OUTPUT_NAME}"
    )

    if(GEN_TARGET_NAME)
        add_custom_target(${GEN_TARGET_NAME} DEPENDS ${ATLAS_PNG} ${ATLAS_JSON} ${FONT_FILE})
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