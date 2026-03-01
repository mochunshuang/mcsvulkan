set(DIR_NAME agg)
set(EXE_DIR "${CMAKE_SOURCE_DIR}/test/${DIR_NAME}")
set(OUTPUT_DIRECTORY "${TEST_EXECUTABLE_OUTPUT_PATH}/${DIR_NAME}")

# 将 assets/ 的全部内容复制到 OUTPUT_DIRECTORY
file(COPY "${EXE_DIR}/assets/" DESTINATION "${OUTPUT_DIRECTORY}")

set(agg_freetype_dir ${EXE_DIR}/font_freetype)
add_library(agg_freetype STATIC ${agg_freetype_dir}/agg_font_freetype.cpp)
target_include_directories(agg_freetype PUBLIC ${agg_freetype_dir} ${antigrain_SOURCE_DIR}/include)
target_link_libraries(agg_freetype PUBLIC freetype)
target_compile_definitions(agg_freetype PUBLIC NOMINMAX)

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

# --------------------- examples  ---------------------
add_agg_target(lion parse_lion.cpp)
add_agg_target(freetype_test
    make_arrows.cpp
    make_gb_poly.cpp)