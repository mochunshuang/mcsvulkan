# 收集要传递的工具链变量（使用 CMake 当前缓存的值）
set(TOOLCHAIN_VARS
    CMAKE_C_COMPILER
    CMAKE_CXX_COMPILER
    CMAKE_TOOLCHAIN_FILE
    CMAKE_MAKE_PROGRAM
    CMAKE_PREFIX_PATH
)

# 构建参数字符串
set(EXTERNAL_CMAKE_ARGS "")

foreach(VAR ${TOOLCHAIN_VARS})
    if(DEFINED ${VAR})
        # 将变量值加入参数，处理路径中的空格
        list(APPEND EXTERNAL_CMAKE_ARGS "-D${VAR}:STRING=${${VAR}}")
    endif()
endforeach()

# 工具输出路径（与工具项目中的 OUTPUT_DIRECTORY 保持一致）
set(TOOL_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/tool")
set(HB_SUBSET_TOOL_NAME hb_subset_tool)
set(HB_SUBSET_TOOL_EXE "${TOOL_OUTPUT_DIR}/${HB_SUBSET_TOOL_NAME}${CMAKE_EXECUTABLE_SUFFIX}")

set(EMOJI_ATLAS_NAME gen_emoji_atlas)
set(EMOJI_ATLAS_EXE "${TOOL_OUTPUT_DIR}/${EMOJI_ATLAS_NAME}${CMAKE_EXECUTABLE_SUFFIX}")

set(MSDF_ATLAS_NAME msdf-atlas-gen)
set(MSDF_ATLAS_EXE "${TOOL_OUTPUT_DIR}/${MSDF_ATLAS_NAME}${CMAKE_EXECUTABLE_SUFFIX}")

list(APPEND EXTERNAL_CMAKE_ARGS "-DHB_SUBSET_TOOL_NAME=${HB_SUBSET_TOOL_NAME}")
list(APPEND EXTERNAL_CMAKE_ARGS "-DEMOJI_ATLAS_NAME=${EMOJI_ATLAS_NAME}")
list(APPEND EXTERNAL_CMAKE_ARGS "-DMSDF_ATLAS_NAME=${MSDF_ATLAS_NAME}")

include(ExternalProject)
ExternalProject_Add(my_tool_external
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/test/tool
    BINARY_DIR ${CMAKE_SOURCE_DIR}/test/tool/build
    CMAKE_ARGS
    ${EXTERNAL_CMAKE_ARGS}
    INSTALL_COMMAND ""
    BUILD_ALWAYS YES
    BUILD_BYPRODUCTS # 声明构建产物
    ${HB_SUBSET_TOOL_EXE}
    ${EMOJI_ATLAS_EXE}
    ${MSDF_ATLAS_EXE}
)

# # 现在可以在自定义命令或自定义目标中依赖这些文件
# add_custom_command(
# OUTPUT some_output
# COMMAND ${HB_SUBSET_TOOL_EXE} ... # 使用工具
# DEPENDS ${HB_SUBSET_TOOL_EXE} # 依赖该文件，CMake 会自动触发 my_tool_external 的构建
# # ... 其他参数
# )
# # 或创建一个聚合目标
# add_custom_target(use_tools ALL
# DEPENDS ${HB_SUBSET_TOOL_EXE} ${EMOJI_ATLAS_EXE} ${MSDF_ATLAS_EXE}
# )
# # 无需显式 add_dependencies，因为 DEPENDS 已包含文件，CMake 会自动处理

# gen msdf config
set(MSDF_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/msdf" CACHE STRING "MSDF_OUTPUT_DIR NAME" FORCE)
set(FONT_INPUT_DIR "${CMAKE_SOURCE_DIR}/assets/font" CACHE STRING "FONT_INPUT_DIR NAME" FORCE)
set(CHASET_INPUT_DIR "${CMAKE_SOURCE_DIR}/assets/charset" CACHE STRING "CHASET_INPUT_DIR NAME" FORCE)
add_custom_command(
    OUTPUT ${MSDF_OUTPUT_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MSDF_OUTPUT_DIR}
)

function(ADD_MSDF_DEF TARGET_NAME)
    target_compile_definitions(${TARGET_NAME} PRIVATE MSDF_OUTPUT_DIR="${MSDF_OUTPUT_DIR}" FONT_INPUT_DIR="${FONT_INPUT_DIR}" CHASET_INPUT_DIR="${CHASET_INPUT_DIR}")
endfunction()

function(add_msdf_atlas_target TARGET_NAME)
    # 解析命名参数
    set(prefix ARG)
    set(noValues "")
    set(singleValues FONT_PATH OUTPUT_NAME TYPE SIZE PX_RANGE CHARSET CHARS GLYPHS FORMAT OUTPUT_DIR)
    set(multiValues EXTRA_ARGS)
    cmake_parse_arguments(${prefix} "${noValues}" "${singleValues}" "${multiValues}" ${ARGN})

    # message(STATUS "DEBUG: GLYPHS = '${ARG_GLYPHS}'") # 添加调试输出

    # 检查必需参数
    if(NOT ARG_FONT_PATH OR NOT ARG_OUTPUT_NAME)
        message(FATAL_ERROR "add_msdf_atlas_target: FONT_PATH and OUTPUT_NAME are required")
    endif()

    # 不能同时指定多个标识符类型
    set(id_count 0)

    if(ARG_CHARSET)
        math(EXPR id_count "${id_count}+1")
    endif()

    if(ARG_CHARS)
        math(EXPR id_count "${id_count}+1")
    endif()

    if(NOT "${ARG_GLYPHS}" STREQUAL "")
        math(EXPR id_count "${id_count}+1")
    endif()

    if(id_count GREATER 1)
        message(FATAL_ERROR "add_msdf_atlas_target: Cannot specify multiple of CHARSET, CHARS, or GLYPHS")
    endif()

    # 设置默认值
    if(NOT ARG_OUTPUT_DIR)
        set(ARG_OUTPUT_DIR "${MSDF_OUTPUT_DIR}")
    endif()

    if(NOT ARG_TYPE)
        set(ARG_TYPE "msdf")
    endif()

    if(NOT ARG_SIZE)
        set(ARG_SIZE "32")
    endif()

    if(NOT ARG_PX_RANGE)
        set(ARG_PX_RANGE "2")
    endif()

    if(NOT ARG_FORMAT)
        set(ARG_FORMAT "png")
    endif()

    # 确保输出目录存在
    add_custom_command(
        OUTPUT ${ARG_OUTPUT_DIR}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${ARG_OUTPUT_DIR}
        COMMENT "Creating output directory ${ARG_OUTPUT_DIR}"
    )

    # ----- 图集生成命令 -----
    set(PNG_FILE "${ARG_OUTPUT_DIR}/${ARG_OUTPUT_NAME}.${ARG_FORMAT}")
    set(JSON_FILE "${ARG_OUTPUT_DIR}/${ARG_OUTPUT_NAME}.json")

    set(ATLAS_CMD_ARGS
        ${MSDF_ATLAS_EXE}
        -font "${ARG_FONT_PATH}"
        -type "${ARG_TYPE}"
        -size "${ARG_SIZE}"
        -pxrange "${ARG_PX_RANGE}"
        -imageout "${PNG_FILE}"
        -format "${ARG_FORMAT}"
        -json "${JSON_FILE}"
    )

    # 添加字符/字形集参数（优先级：GLYPHS > CHARS > CHARSET）
    if(NOT "${ARG_GLYPHS}" STREQUAL "")
        list(APPEND ATLAS_CMD_ARGS -glyphs "${ARG_GLYPHS}")
    elseif(NOT "${ARG_CHARS}" STREQUAL "")
        list(APPEND ATLAS_CMD_ARGS -chars "${ARG_CHARS}")
    elseif(NOT "${ARG_CHARSET}" STREQUAL "")
        list(APPEND ATLAS_CMD_ARGS -charset "${ARG_CHARSET}")
    endif()

    # 附加其他额外参数
    if(ARG_EXTRA_ARGS)
        list(APPEND ATLAS_CMD_ARGS ${ARG_EXTRA_ARGS})
    endif()

    string(JOIN " " ATLAS_CMD_DISPLAY ${ATLAS_CMD_ARGS})

    add_custom_command(
        OUTPUT ${PNG_FILE} ${JSON_FILE}
        COMMAND ${CMAKE_COMMAND} -E echo "====== Starting MSDF atlas generation for ${ARG_OUTPUT_NAME} ======"
        COMMAND ${CMAKE_COMMAND} -E echo "cmd: ${ATLAS_CMD_DISPLAY}"
        COMMAND ${ATLAS_CMD_ARGS}
        COMMAND ${CMAKE_COMMAND} -E echo "====== Finished MSDF atlas generation ======"
        DEPENDS
        ${MSDF_ATLAS_EXE}
        "${ARG_FONT_PATH}"
        COMMENT "Generating MSDF atlas: ${ARG_OUTPUT_NAME}"
        VERBATIM
        COMMAND_EXPAND_LISTS
    )

    set(ALL_OUTPUTS ${PNG_FILE} ${JSON_FILE})

    # ----- 字体子集化（仅当提供了字符集且没有 -allglyphs 时）-----
    set(SUBSET_FONT_PATH "")
    set(SHOULD_SUBSET FALSE)

    # 只有使用 CHARS 或 CHARSET 时才进行子集化（GLYPHS 时跳过，因为工具不支持）
    if(NOT "${ARG_GLYPHS}" STREQUAL "" OR NOT "${ARG_CHARSET}" STREQUAL "" OR NOT "${ARG_GLYPHS}" STREQUAL "")
        set(SHOULD_SUBSET TRUE)
    endif()

    # 检查是否有 -allglyphs
    list(FIND ARG_EXTRA_ARGS "-allglyphs" allglyphs_idx)

    if(allglyphs_idx GREATER -1)
        set(SHOULD_SUBSET FALSE)
    endif()

    message(NOTICE "${TARGET_NAME}: SHOULD_SUBSET: ${SHOULD_SUBSET}")

    # ---------------------------手动复制font 到指定目录------------------------
    get_filename_component(FONT_EXT "${ARG_FONT_PATH}" LAST_EXT)
    set(SUBSET_FONT_PATH "${ARG_OUTPUT_DIR}/${ARG_OUTPUT_NAME}${FONT_EXT}")

    if(NOT SHOULD_SUBSET)
        message(STATUS "COPY ${ARG_FONT_PATH} TO ${SUBSET_FONT_PATH}")
        add_custom_command(
            OUTPUT "${SUBSET_FONT_PATH}"
            COMMAND ${CMAKE_COMMAND} -E copy ${ARG_FONT_PATH} ${SUBSET_FONT_PATH}
            DEPENDS ${ARG_FONT_PATH}
            COMMENT "Copying font_file"
        )
        list(APPEND ALL_OUTPUTS ${SUBSET_FONT_PATH})
    endif(NOT SHOULD_SUBSET)

    # ---------------------------裁剪并复制font 到指定目录------------------------
    if(SHOULD_SUBSET EQUAL TRUE)
        if(NOT HB_SUBSET_TOOL_EXE)
            message(FATAL_ERROR "add_msdf_atlas_target: HB_SUBSET_TOOL_EXE is not defined")
        endif()

        set(SUBSET_CMD_ARGS ${HB_SUBSET_TOOL_EXE} -font "${ARG_FONT_PATH}" -out "${SUBSET_FONT_PATH}")

        if(NOT "${ARG_GLYPHS}" STREQUAL "")
            list(APPEND SUBSET_CMD_ARGS -glyphs "${ARG_GLYPHS}")
        elseif(NOT "${ARG_CHARS}" STREQUAL "")
            list(APPEND SUBSET_CMD_ARGS -chars "${ARG_CHARS}")
        elseif(NOT "${ARG_CHARSET}" STREQUAL "")
            list(APPEND SUBSET_CMD_ARGS -charset "${ARG_CHARSET}")
        endif()

        string(JOIN " " SUBSET_CMD_DISPLAY ${SUBSET_CMD_ARGS})

        add_custom_command(
            OUTPUT ${SUBSET_FONT_PATH}
            COMMAND ${CMAKE_COMMAND} -E echo "====== Subsetting font for ${ARG_OUTPUT_NAME} ======"
            COMMAND ${CMAKE_COMMAND} -E echo "cmd: ${SUBSET_CMD_DISPLAY}"
            COMMAND ${SUBSET_CMD_ARGS}
            COMMAND ${CMAKE_COMMAND} -E echo "====== Subsetting done ======"
            DEPENDS
            ${PNG_FILE}
            ${HB_SUBSET_TOOL_EXE}
            "${ARG_FONT_PATH}"
            ${ARG_CHARSET}
            COMMENT "Subsetting font for ${ARG_OUTPUT_NAME}"
            VERBATIM
            COMMAND_EXPAND_LISTS
        )

        list(APPEND ALL_OUTPUTS ${SUBSET_FONT_PATH})
    elseif(NOT "${ARG_GLYPHS}" STREQUAL "")
        # 当使用 GLYPHS 时，提示用户不进行子集化
        message(WARNING "Note: GLYPHS specified for ${TARGET_NAME}, skipping font subsetting (subset tool only supports Unicode).")
    endif()

    # ----- 最终目标 -----
    add_custom_target(${TARGET_NAME} ALL DEPENDS ${ALL_OUTPUTS})

    if(TARGET msdf-atlas-gen-standalone)
        add_dependencies(${TARGET_NAME} msdf-atlas-gen-standalone)
    endif()

    if(SUBSET_FONT_PATH AND TARGET ${HB_SUBSET_TOOL_NAME})
        add_dependencies(${TARGET_NAME} ${HB_SUBSET_TOOL_NAME})
    endif()
endfunction()

# E:/0_github_project/mcsvulkan/tool/msdf-atlas-gen.exe -font "E:/0_github_project/mcsvulkan/msdf/tirobangla_ascii.ttf" -type msdf -size 32 -pxrange 2 -imageout "E:/0_github_project/mcsvulkan/msdf/tirobangla_ascii.png" -json "E:/0_github_project/mcsvulkan/msdf/tirobangla_ascii.json"

# 生成英文图集（使用默认参数）
add_msdf_atlas_target(
    generate_english_atlas
    FONT_PATH "${FONT_INPUT_DIR}/TiroBangla-Regular.ttf"
    OUTPUT_NAME "english_atlas"
    EXTRA_ARGS -allglyphs # 传递额外参数
)

# 生成中文图集，指定尺寸和字符集
add_msdf_atlas_target(
    generate_chinese_atlas
    FONT_PATH "C:/Windows/Fonts/msyh.ttc"
    OUTPUT_NAME "msyh_chinese"
    SIZE 64
    PX_RANGE 2
    CHARSET "${CHASET_INPUT_DIR}/small_chinese_common.txt"
)

# 阿拉伯字
# E:/0_github_project/mcsvulkan/tool/msdf-atlas-gen.exe -font "C:\Windows\Fonts\arial.ttf" -allglyphs -type msdf -format png -imageout arial.png -json arial.json -size 64 -pxrange 2
add_msdf_atlas_target(
    generate_arial_atlas
    FONT_PATH "C:/Windows/Fonts/arial.ttf"
    OUTPUT_NAME "arial_all"
    SIZE 64
    PX_RANGE 2
    EXTRA_ARGS -allglyphs # 传递额外参数
)
add_msdf_atlas_target(
    generate_segoeui_atlas
    FONT_PATH "C:/Windows/Fonts/segoeui.ttf" # 中文字体
    OUTPUT_NAME "segoe_arabic"
    PX_RANGE 2 # 默认值
    SIZE 64 # 建议提高精度
    CHARSET "${CHASET_INPUT_DIR}/arabic_charset.txt"
)

# ------------------------------------------ bitmap ----------------------------------------------------------
function(add_emoji_atlas_target TARGET_NAME)
    # 解析命名参数
    set(options "")
    set(oneValueArgs EXECUTABLE FONT_PATH CHARSET OUTPUT_NAME SIZE PADDING MAX_SIZE OUTPUT_DIR)
    set(multiValueArgs DIMENSIONS EXTRA_ARGS) # 增加 EXTRA_ARGS 以支持类似 -allglyphs 等选项
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # 检查必需参数
    if(NOT ARG_FONT_PATH)
        message(FATAL_ERROR "add_emoji_atlas_target: FONT_PATH is required")
    endif()

    if(NOT ARG_CHARSET)
        message(FATAL_ERROR "add_emoji_atlas_target: CHARSET is required")
    endif()

    if(NOT ARG_OUTPUT_NAME)
        message(FATAL_ERROR "add_emoji_atlas_target: OUTPUT_NAME is required")
    endif()

    # 可执行文件：优先使用传入的 EXECUTABLE，否则从父作用域获取
    if(NOT ARG_EXECUTABLE)
        if(NOT EMOJI_ATLAS_EXE)
            message(FATAL_ERROR "add_emoji_atlas_target: No executable provided and EMOJI_ATLAS_EXE is not defined")
        endif()

        set(ARG_EXECUTABLE "${EMOJI_ATLAS_EXE}")
    endif()

    # 输出目录：优先使用传入的 OUTPUT_DIR，否则使用 MSDF_OUTPUT_DIR
    if(NOT ARG_OUTPUT_DIR)
        set(ARG_OUTPUT_DIR "${MSDF_OUTPUT_DIR}")
    endif()

    # 设置默认参数
    if(NOT ARG_SIZE)
        set(ARG_SIZE "64")
    endif()

    if(NOT ARG_PADDING)
        set(ARG_PADDING "1")
    endif()

    if(NOT ARG_MAX_SIZE)
        set(ARG_MAX_SIZE "4096")
    endif()

    # ----- 子集化判断（与 add_msdf_atlas_target 逻辑一致） -----
    set(SUBSET_FONT_PATH "")
    set(SHOULD_SUBSET FALSE)

    # 如果提供了 CHARSET 且没有 -allglyphs，则进行子集化
    if(ARG_CHARSET)
        set(SHOULD_SUBSET TRUE)

        # 检查 EXTRA_ARGS 中是否有 -allglyphs（模拟 msdf-atlas-gen 行为）
        list(FIND ARG_EXTRA_ARGS "-allglyphs" allglyphs_idx)

        if(allglyphs_idx GREATER -1)
            set(SHOULD_SUBSET FALSE)
        endif()
    endif()

    # ---------------------------手动复制font 到指定目录------------------------
    get_filename_component(FONT_EXT "${ARG_FONT_PATH}" LAST_EXT)
    set(SUBSET_FONT_PATH "${ARG_OUTPUT_DIR}/${ARG_OUTPUT_NAME}${FONT_EXT}")

    if(NOT SHOULD_SUBSET)
        message(STATUS "COPY ${ARG_FONT_PATH} TO ${SUBSET_FONT_PATH}")
        add_custom_command(
            OUTPUT "${SUBSET_FONT_PATH}"
            COMMAND ${CMAKE_COMMAND} -E copy ${ARG_FONT_PATH} ${SUBSET_FONT_PATH}
            DEPENDS ${ARG_FONT_PATH}
            COMMENT "Copying font_file"
        )
        list(APPEND ALL_OUTPUTS ${SUBSET_FONT_PATH})
    endif(NOT SHOULD_SUBSET)

    if(SHOULD_SUBSET)
        if(NOT HB_SUBSET_TOOL_EXE)
            message(FATAL_ERROR "add_emoji_atlas_target: CHARSET provided but HB_SUBSET_TOOL_EXE is not defined")
        endif()

        set(SUBSET_CMD_ARGS
            ${HB_SUBSET_TOOL_EXE}
            -font "${ARG_FONT_PATH}"
            -charset "${ARG_CHARSET}"
            -out "${SUBSET_FONT_PATH}"
        )

        # 如果 EXTRA_ARGS 中包含其他子集化参数（如 -glyphs），可在此扩展，但目前仅支持 -charset
        add_custom_command(
            OUTPUT ${SUBSET_FONT_PATH}
            COMMAND ${CMAKE_COMMAND} -E echo "====== Subsetting emoji font for ${ARG_OUTPUT_NAME} ======"
            COMMAND ${CMAKE_COMMAND} -E echo "cmd: ${SUBSET_CMD_ARGS}"
            COMMAND ${SUBSET_CMD_ARGS}
            COMMAND ${CMAKE_COMMAND} -E echo "====== Subsetting done ======"
            DEPENDS
            ${HB_SUBSET_TOOL_EXE}
            "${ARG_FONT_PATH}"
            "${ARG_CHARSET}"
            COMMENT "Subsetting font for ${ARG_OUTPUT_NAME}"
            VERBATIM
            COMMAND_EXPAND_LISTS
        )

        # 图集生成时使用的字体改为子集字体
        set(USED_FONT_PATH "${SUBSET_FONT_PATH}")
    else()
        set(USED_FONT_PATH "${ARG_FONT_PATH}")
    endif()

    # ----- 图集生成命令（始终生成 PNG 和 JSON） -----
    set(PNG_FILE "${ARG_OUTPUT_DIR}/${ARG_OUTPUT_NAME}.png")
    set(JSON_FILE "${ARG_OUTPUT_DIR}/${ARG_OUTPUT_NAME}.json")

    set(ATLAS_CMD_ARGS
        "${ARG_EXECUTABLE}"
        -font "${USED_FONT_PATH}"
        -charset "${ARG_CHARSET}"
        -size "${ARG_SIZE}"
        -padding "${ARG_PADDING}"
        -file_name "${ARG_OUTPUT_DIR}/${ARG_OUTPUT_NAME}"
    )

    if(ARG_DIMENSIONS)
        list(LENGTH ARG_DIMENSIONS DIM_LEN)

        if(NOT DIM_LEN EQUAL 2)
            message(FATAL_ERROR "add_emoji_atlas_target: DIMENSIONS requires exactly two values (width height)")
        endif()

        list(APPEND ATLAS_CMD_ARGS -dimensions ${ARG_DIMENSIONS})
    endif()

    list(APPEND ATLAS_CMD_ARGS -max-size "${ARG_MAX_SIZE}")

    # 如果有 EXTRA_ARGS，附加到命令末尾（允许用户传递额外参数）
    if(ARG_EXTRA_ARGS)
        list(APPEND ATLAS_CMD_ARGS ${ARG_EXTRA_ARGS})
    endif()

    # 图集生成依赖
    set(ATLAS_DEPENDS
        "${ARG_EXECUTABLE}"
        "${USED_FONT_PATH}"
        "${ARG_CHARSET}"
    )

    add_custom_command(
        OUTPUT ${PNG_FILE} ${JSON_FILE}
        COMMAND ${CMAKE_COMMAND} -E echo "====== Starting emoji atlas generation for ${ARG_OUTPUT_NAME} ======"
        COMMAND ${CMAKE_COMMAND} -E echo "cmd: ${ATLAS_CMD_ARGS}"
        COMMAND ${ATLAS_CMD_ARGS}
        COMMAND ${CMAKE_COMMAND} -E echo "====== Finished emoji atlas generation ======"
        DEPENDS ${ATLAS_DEPENDS}
        COMMENT "Generating emoji atlas: ${ARG_OUTPUT_NAME}"
        VERBATIM
        COMMAND_EXPAND_LISTS
    )

    # 收集所有输出文件
    set(ALL_OUTPUTS ${PNG_FILE} ${JSON_FILE})

    if(SHOULD_SUBSET)
        list(APPEND ALL_OUTPUTS ${SUBSET_FONT_PATH})
    endif()

    # 创建最终目标
    add_custom_target(${TARGET_NAME} ALL DEPENDS ${ALL_OUTPUTS})

    # 添加依赖关系
    if(TARGET ${EMOJI_ATLAS_NAME})
        add_dependencies(${TARGET_NAME} ${EMOJI_ATLAS_NAME})
    endif()

    if(SHOULD_SUBSET AND TARGET ${HB_SUBSET_TOOL_NAME})
        add_dependencies(${TARGET_NAME} ${HB_SUBSET_TOOL_NAME})
    endif()
endfunction()

add_emoji_atlas_target(
    generate_emoji_atlas_custom
    FONT_PATH "C:/Windows/Fonts/seguiemj.ttf"
    CHARSET "${CHASET_INPUT_DIR}/small_emoji.txt"
    OUTPUT_NAME "emoji"
    SIZE 96
    PADDING 2

    # DIMENSIONS 4096 4096 # 可选，固定尺寸

    # MAX_SIZE 4096        # 可选，默认已设
)

# -----------------------------------测试-----------------------------------
# 测试单个字符格式（三种表示法）
add_msdf_atlas_target(
    generate_test_single_char
    FONT_PATH "${FONT_INPUT_DIR}/TiroBangla-Regular.ttf"
    OUTPUT_NAME "test_single_char"
    CHARS "'A',65,0x41"
)

add_msdf_atlas_target(
    generate_test_missing_char
    FONT_PATH "C:/Windows/Fonts/msyh.ttc"
    OUTPUT_NAME "missing_char"
    GLYPHS "0" # 指定字形索引 0

    # 可选其他参数：SIZE, PX_RANGE 等
)

# 测试字符范围格式（三种表示法）
add_msdf_atlas_target(
    generate_test_range
    FONT_PATH "${FONT_INPUT_DIR}/TiroBangla-Regular.ttf"
    OUTPUT_NAME "test_range"
    CHARS "['A','Z'],[65,90],[0x41,0x5a]"
)

# 测试字符串格式
add_msdf_atlas_target(
    generate_test_string
    FONT_PATH "${FONT_INPUT_DIR}/TiroBangla-Regular.ttf"
    OUTPUT_NAME "test_string"
    CHARS "\"ABCDEFGHIJKLMNOPQRSTUVWXYZ\"" # 注意转义的双引号
)

# 测试混合格式（同时包含单字符、范围、字符串）
add_msdf_atlas_target(
    generate_test_mixed
    FONT_PATH "${FONT_INPUT_DIR}/TiroBangla-Regular.ttf"
    OUTPUT_NAME "test_mixed"
    CHARS "'A',65,0x41,['B','D'],\"EFG\",[0x48,0x4a]"
)

# 测试带转义字符的字符串（包含引号和反斜杠）
add_msdf_atlas_target(
    generate_test_escape
    FONT_PATH "${FONT_INPUT_DIR}/TiroBangla-Regular.ttf"
    OUTPUT_NAME "test_escape"
    CHARS "\"'A' \\\"B\\\" C\\\\\""
)

# 测试空白和逗号分隔
add_msdf_atlas_target(
    generate_test_whitespace
    FONT_PATH "${FONT_INPUT_DIR}/TiroBangla-Regular.ttf"
    OUTPUT_NAME "test_whitespace"
    CHARS " 'A'  65  0x42 , ['C','E']  \"FG\" "
)