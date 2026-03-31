set(DIR_NAME font)
set(EXE_DIR ${CMAKE_SOURCE_DIR}/test/font)
set(OUTPUT_DIRECTORY ${TEST_EXECUTABLE_OUTPUT_PATH}/${DIR_NAME})

set(LIB freetype harfbuzz msdfgen::msdfgen utf8proc SheenBidi libunibreak)

macro(add_font_target NAME)
    set(TARGET_NAME "${DIR_NAME}-${NAME}")
    add_executable(${TARGET_NAME} "${EXE_DIR}/${NAME}.cpp")
    target_link_libraries(${TARGET_NAME} PRIVATE ${LIB})
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

add_font_target("language")
add_font_target("font_info")
add_font_target("select_font")
add_font_target("select_font2")
add_font_target("select_font3")
add_font_target("select_font4")
add_font_target("unibreak")

set(LIB freetype harfbuzz utf8proc ICU::uc ICU::i18n ICU::dt)
add_font_target("icu")

set(LIB freetype harfbuzz utf8proc FriBiDi::FriBiDi)
add_font_target("fribidi")

set(LIB freetype harfbuzz utf8proc SheenBidi libunibreak)
add_font_target("unibreak2")

set(LIB freetype harfbuzz utf8proc ICU::uc ICU::i18n ICU::dt SheenBidi libunibreak)
add_font_target("unibreak3")
add_font_target("harfbuzz")
add_font_target("layout")
add_font_target("layout2")

add_font_target("icu2")
add_font_target("harfbuzz2")
add_font_target("ScriptLocatorTests")
add_font_target("bidi")
add_font_target("icu3")
add_font_target("bidi2")
add_font_target("bidi3")
add_font_target("bidi4")
add_font_target("bidi5")
add_font_target("harfbuzz3")