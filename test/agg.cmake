set(DIR_NAME agg)
set(EXE_DIR "${CMAKE_SOURCE_DIR}/test/${DIR_NAME}")
set(OUTPUT_DIRECTORY "${TEST_EXECUTABLE_OUTPUT_PATH}/${DIR_NAME}")

# 将 assets/ 的全部内容复制到 OUTPUT_DIRECTORY
file(COPY "${EXE_DIR}/assets/" DESTINATION "${OUTPUT_DIRECTORY}")

set(agg_freetype_dir ${EXE_DIR}/font_freetype)
add_library(agg_freetype STATIC ${agg_freetype_dir}/agg_font_freetype.cpp)
target_include_directories(agg_freetype PUBLIC ${agg_freetype_dir} ${antigrain_SOURCE_DIR}/include ${antigrain_SOURCE_DIR}/font_win32_tt)
target_link_libraries(agg_freetype PUBLIC freetype)
target_compile_definitions(agg_freetype PUBLIC NOMINMAX)

macro(add_target NAME)
    set(TARGET_NAME "${DIR_NAME}-${NAME}")
    add_executable(${TARGET_NAME} "${EXE_DIR}/${NAME}.cpp")
    target_link_libraries(${TARGET_NAME} PRIVATE antigrain controls platform agg_freetype harfbuzz libraqm libunibreak msdfgen::msdfgen nlohmann_json stb rectpack2D)
    set_target_properties(${TARGET_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIRECTORY}
        OUTPUT_NAME ${NAME}
    )
    target_include_directories(${TARGET_NAME} PRIVATE ${antigrain_examples_dir})
    target_compile_definitions(${TARGET_NAME} PUBLIC AGG_USE_FREETYPE)
endmacro()

macro(add_agg_target NAME)
    set(TARGET_NAME "${DIR_NAME}-${NAME}")
    add_executable(${TARGET_NAME} "${EXE_DIR}/${NAME}.cpp")

    set(antigrain_examples_dir ${antigrain_SOURCE_DIR}/examples)

    # 如果传入了额外参数，假设它们是额外的源文件
    if(${ARGC} GREATER 1)
        set(all_args ${ARGV})
        list(REMOVE_AT all_args 0)

        foreach(arg IN LISTS all_args)
            target_sources(${TARGET_NAME} PRIVATE "${antigrain_examples_dir}/${arg}")
        endforeach()
    endif()

    target_link_libraries(${TARGET_NAME} PRIVATE antigrain controls platform agg_freetype)
    set_target_properties(${TARGET_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIRECTORY}
        OUTPUT_NAME ${NAME}
    )
    target_include_directories(${TARGET_NAME} PRIVATE ${antigrain_examples_dir})

    if(WIN32)
        set_target_properties(${TARGET_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()

    # -DAGG_USE_FREETYPE
    target_compile_definitions(${TARGET_NAME} PUBLIC AGG_USE_FREETYPE)
endmacro()

add_agg_target(t01_rendering_buffer)
add_agg_target(t02_pixel_formats)
add_agg_target(test_clip)
add_agg_target(t03visualize)

add_target(arrow_demo)
add_target(arrow_demo1)
add_target(arrow_demo2)
add_target(arrow_demo3)
add_target(arrow_demo4)
add_target(arrow_demo5)
add_target(arrow_demo6)
add_target(freeType)
add_target(harfbuzz)
add_target(harfbuzz2)
add_target(harfbuzz3)

add_target(raqm)
add_target(unibreak)
add_target(unibreak2)
add_target(unibreak3)
add_target(color)
add_target(unibreak4)
add_target(msdfgen)
add_target(msdf_atlas_gen)
ADD_MSDF_DEF(${TARGET_NAME})

add_target(gen_emoji_atlas)
add_target(gen_emoji_atlas2)
add_target(gen_emoji_atlas3)
ADD_MSDF_DEF(${TARGET_NAME})

add_target(gen_emoji_atlas4)
ADD_MSDF_DEF(${TARGET_NAME})

# --------------------- examples  ---------------------
add_agg_target(lion parse_lion.cpp)
add_agg_target(freetype_test
    make_arrows.cpp
    make_gb_poly.cpp)
add_agg_target(truetype_test)
