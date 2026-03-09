# 1. 添加模块
# git submodule add  https://gh-proxy.org/https://github.com/Chlumsky/msdf-atlas-gen.git third_party/msdf-atlas-gen

# 2. 进入 msdf-atlas-gen 目录
# cd third_party/msdf-atlas-gen

# 查看可以checkout的：
# git tag -l | sort -V

# 3. 切换到指定 tag
# git checkout v1.3

# 验证确实在标签上
# git describe --tags

# 回到项目根目录
# cd ../..

# 提交子模块状态的更改
# git add third_party/msdf-atlas-gen

# git commit -m "Pin msdf-atlas-gen to v1.3"

# NOTE: 查看项目的所有子模块
# git submodule status

# NOTE: 和当前的 msdf-atlas-gen 不兼容
# NOTE: 查看依赖的版本：
# PS E:\0_github_project\mcsvulkan\third_party\msdf-atlas-gen> git submodule status
# -888674220216d1d326c6f29cf89165b545279c1f artery-font-format
# -85e8b3d47b3d1a42e4a5ebda0a24fb1cc2e669e0 msdfgen
# NOTE: 看起来要回退版本？ 当前是 v1.13
# PS E:\0_github_project\mcsvulkan\third_party\msdfgen> git describe --tags 85e8b3d
# v1.12

# NOTE: 切换到 master 分支并拉取最新代码，接口就兼容了
# git checkout master
# git pull origin master
# git describe --tags

# 1. config
set(MSDF_ATLAS_BUILD_STANDALONE ON CACHE BOOL "Build the msdf-atlas-gen standalone executable" FORCE)
set(MSDF_ATLAS_USE_VCPKG OFF CACHE BOOL "Use vcpkg package manager to link project dependencies" FORCE)
set(MSDF_ATLAS_USE_SKIA OFF CACHE BOOL "Build with the Skia library" FORCE)
set(MSDF_ATLAS_NO_ARTERY_FONT ON CACHE BOOL "Disable Artery Font export and do not require its submodule" FORCE)

# 已经下载因此不要子模块的
set(MSDF_ATLAS_MSDFGEN_EXTERNAL ON CACHE BOOL "Disable Artery Font export and do not require its submodule" FORCE)
set(MSDF_ATLAS_INSTALL OFF CACHE BOOL "Generate installation target" FORCE)

# NOTE: 但是输出目录只有一个 exe 没有 dll。 虽然我们的库都说静态的，但是c++的标准库不是静态库，还得运行时链接的
# freetype是动态库，导致的好像
# MSDF_ATLAS_DYNAMIC_RUNTIME 控制的是 C/C++ 运行时库的链接方式（静态 vs 动态）。如果设为 ON，则链接器会链接动态版本的运行时库（例如 msvcrt.lib 导入库，实际运行时需要 msvcp140.dll 等）。
# 如果设为 OFF，则会将运行时库静态链接进 exe，使得 exe 体积变大但不依赖外部运行时 DLL。
set(MSDF_ATLAS_DYNAMIC_RUNTIME ON CACHE BOOL "Link dynamic runtime library instead of static" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Generate dynamic library files instead of static" FORCE)

# 2. add_subdirectory
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/msdf-atlas-gen)

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
    set(MSDF_ATLAS_GEN_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/tool")

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

        # # 使用示例 3：表情符号（假设有表情字体 seguiemj.ttf 和表情列表 emoji.txt）
        generate_msdf_atlas(
            TARGET_NAME generate_emoji_atlas
            FONT_PATH "C:/Windows/Fonts/seguiemj.ttf"
            OUTPUT_NAME "emoji"
            PX_RANGE 2 # 必须用官方默认值，不要乱改
            SIZE 96 # 建议提高精度
            CHARSET "${CHASET_INPUT_DIR}/small_emoji.txt"
        )
    endif(WIN32)
endif(TARGET msdf-atlas-gen-standalone)
