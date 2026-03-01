set(DIR_NAME font)
set(EXE_DIR ${CMAKE_SOURCE_DIR}/test/font)
set(OUTPUT_DIRECTORY ${TEST_EXECUTABLE_OUTPUT_PATH}/${DIR_NAME})

macro(add_font_target NAME)
    set(TARGET_NAME "${DIR_NAME}-${NAME}")
    add_executable(${TARGET_NAME} "${EXE_DIR}/${NAME}.cpp")
    target_link_libraries(${TARGET_NAME} PRIVATE freetype harfbuzz msdfgen::msdfgen)
    set_target_properties(${TARGET_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIRECTORY}
        OUTPUT_NAME ${NAME}
    )
endmacro()

add_font_target("freetype_load")
add_font_target("freetype_part1")
add_font_target("freetype_part2")
add_font_target("freetype_part3")
add_font_target("freetype_part4")
add_font_target("freetype_part5")
add_font_target("canvas")