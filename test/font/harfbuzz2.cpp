#include <cassert>
#include <iostream>
#include <exception>

#include <print>
#include <unordered_map>
#include <vector>
#include <string_view>

#include <cstdint>
#include <cstring>

#include <SheenBidi/SheenBidi.h>

#include <unicode/ubidi.h>   // ICU BIDI 核心
#include <unicode/ustring.h> // UTF-16 转换
#include <unicode/uchar.h>   // u_charMirror 等

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>
#include <hb-ot.h>

#include <string>

int main()
try
{
    {
        std::println("===================Harfbuzz shaping start=============");

        // ---------- 1. 原始 UTF-8 文本 ----------
        const char *text = "یہ ایک )car( ہے۔";

        // ---------- 2. 创建 HarfBuzz 缓冲区并添加文本 ----------
        hb_buffer_t *buf = hb_buffer_create();
        hb_buffer_add_utf8(buf, text, -1, 0, -1);

        // ---------- 3. 设置方向、脚本、语言（使用自动猜测，也可显式设置）----------
        // 显式设置方向为 LTR（基准方向），脚本和语言让 HarfBuzz 自动检测
        hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
        hb_buffer_guess_segment_properties(buf); // 自动设置脚本和语言

        // ---------- 4. 从字体文件加载字体 ----------
        const char *font_path = "C:/Windows/Fonts/arial.ttf";
        hb_blob_t *blob = hb_blob_create_from_file(font_path);
        if (!blob)
        {
            std::cerr << "Failed to load font file: " << font_path << '\n';
            return 1;
        }
        hb_face_t *face = hb_face_create(blob, 0);
        hb_font_t *font = hb_font_create(face);

        // 设置字体大小（可选，方便观察 advance 值）
        hb_font_set_scale(font, 1024, 1024);

        // ---------- 5. 执行字形塑造 ----------
        hb_shape(font, buf, nullptr, 0);

        // ---------- 6. 获取塑造结果 ----------
        unsigned int glyph_count;
        hb_glyph_info_t *glyph_info = hb_buffer_get_glyph_infos(buf, &glyph_count);
        hb_glyph_position_t *glyph_pos = hb_buffer_get_glyph_positions(buf, &glyph_count);

        std::println("Glyph count: {}", glyph_count);

        // ---------- 7. 输出每个字形的信息 ----------
        for (unsigned int i = 0; i < glyph_count; ++i)
        {
            std::println(
                "Glyph {}: id={}, x_advance={}, y_advance={}, x_offset={}, y_offset={}",
                i, static_cast<uint32_t>(glyph_info[i].codepoint), glyph_pos[i].x_advance,
                glyph_pos[i].y_advance, glyph_pos[i].x_offset, glyph_pos[i].y_offset);
        }

        // ---------- 8. 释放资源 ----------
        hb_buffer_destroy(buf);
        hb_font_destroy(font);
        hb_face_destroy(face);
        hb_blob_destroy(blob);

        std::println("===================Harfbuzz shaping end=============");
    }
    {
        std::println("===================base bidi start=============");
        /* Create code point sequence for a sample bidirectional text. */
        constexpr auto bidiText = "یہ ایک )car( ہے۔";
        static_assert(std::string_view{bidiText} ==
                      std::string_view{"\333\214\333\201 \330\247\333\214\332\251 )car( "
                                       "\333\201\333\222\333\224"});
        {
            constexpr auto test_char = u8"یہ ایک )car( ہے۔";
            constexpr auto expected =
                u8"\u06CC\u06C1 \u0627\u06CC\u06A9 )car( \u06C1\u06D2\u06D4";
            static_assert(std::u8string_view(test_char) == std::u8string_view(expected));
            static_assert(
                std::u8string_view(test_char) ==
                std::u8string_view(u8"\333\214\333\201 \330\247\333\214\332\251 "
                                   u8")car( \333\201\333\222\333\224"));
        }

        SBCodepointSequence codepointSequence = {SBStringEncodingUTF8, (void *)bidiText,
                                                 strlen(bidiText)};

        /* Extract the first bidirectional paragraph. */
        SBAlgorithmRef bidiAlgorithm = SBAlgorithmCreate(&codepointSequence);
        SBParagraphRef firstParagraph =
            SBAlgorithmCreateParagraph(bidiAlgorithm, 0, INT32_MAX, SBLevelDefaultLTR);
        SBUInteger paragraphLength = SBParagraphGetLength(firstParagraph);

        /* Create a line consisting of the whole paragraph and get its runs. */
        SBLineRef paragraphLine =
            SBParagraphCreateLine(firstParagraph, 0, paragraphLength);
        SBUInteger runCount = SBLineGetRunCount(paragraphLine);
        const SBRun *runArray = SBLineGetRunsPtr(paragraphLine);

        /* Log the details of each run in the line. */
        for (SBUInteger i = 0; i < runCount; i++)
        {
            std::println("Run Offset: {}", (long)runArray[i].offset);
            std::println("Run Length: {}", (long)runArray[i].length);
            std::println("Run Level: {}\n", (long)runArray[i].level);

            std::println("[{},{}): {}", runArray[i].offset,
                         runArray[i].offset + runArray[i].length,
                         std::string_view{bidiText}.substr(runArray[i].offset,
                                                           runArray[i].length));
        }

        /* Create a mirror locator and load the line in it. */
        SBMirrorLocatorRef mirrorLocator = SBMirrorLocatorCreate();
        SBMirrorLocatorLoadLine(mirrorLocator, paragraphLine, (void *)bidiText);
        const SBMirrorAgent *mirrorAgent = SBMirrorLocatorGetAgent(mirrorLocator);

        /* Log the details of each mirror in the line. */
        while (SBMirrorLocatorMoveNext(mirrorLocator))
        {
            std::println("Mirror Index: {}", (long)mirrorAgent->index);
            std::println("Actual Code Point: {}", (long)mirrorAgent->codepoint);
            std::println("Mirrored Code Point: {}\n", (long)mirrorAgent->mirror);

            // 1. 确认镜像映射正确（括号互相映射）
            if (mirrorAgent->codepoint == '(')
                assert(mirrorAgent->mirror == ')');
            if (mirrorAgent->codepoint == ')')
                assert(mirrorAgent->mirror == '(');
            static_assert(U'(' == 40);
            static_assert(U')' == 41);
            static_assert(bidiText[12] == ')');
            static_assert(bidiText[13] == 'c');
            static_assert(bidiText[16] == '(');
        }

        /* Release all objects. */
        SBMirrorLocatorRelease(mirrorLocator);
        SBLineRelease(paragraphLine);
        SBParagraphRelease(firstParagraph);
        SBAlgorithmRelease(bidiAlgorithm);
        std::println("===================base bidi end=============");
    }
    {
        std::println("===================ICU bidi end=============");

        // ---------- 1. 原始 UTF-8 文本 ----------
        const char *bidiText = "یہ ایک )car( ہے۔";
        size_t textLen = strlen(bidiText);

        // ---------- 2. 将 UTF-8 转换为 UTF-16 (UChar)，并记录每个字符的字节偏移
        // ----------
        std::vector<UChar> utf16; // UTF-16 代码单元序列
        std::vector<size_t>
            byteOffsetOfUChar; // 每个 UChar 对应的原始字节偏移（每个字符起始）
        std::vector<size_t> byteLenOfUChar; // 每个字符在 UTF-8 中的字节长度

        const unsigned char *p = reinterpret_cast<const unsigned char *>(bidiText);
        const unsigned char *end = p + textLen;

        while (p < end)
        {
            size_t offset = p - reinterpret_cast<const unsigned char *>(bidiText);
            UChar32 cp;
            int32_t len;
            // 简单的 UTF-8 解码（假定没有过长或无效序列）
            if ((*p & 0x80) == 0)
            { // ASCII
                cp = *p;
                len = 1;
            }
            else if ((*p & 0xE0) == 0xC0)
            { // 2 字节
                cp = ((*p & 0x1F) << 6) | (*(p + 1) & 0x3F);
                len = 2;
            }
            else if ((*p & 0xF0) == 0xE0)
            { // 3 字节
                cp = ((*p & 0x0F) << 12) | ((*(p + 1) & 0x3F) << 6) | (*(p + 2) & 0x3F);
                len = 3;
            }
            else if ((*p & 0xF8) == 0xF0)
            { // 4 字节 (本字符串中不会出现)
                cp = ((*p & 0x07) << 18) | ((*(p + 1) & 0x3F) << 12) |
                     ((*(p + 2) & 0x3F) << 6) | (*(p + 3) & 0x3F);
                len = 4;
            }
            else
            {
                // 非法，跳过
                cp = 0xFFFD;
                len = 1;
            }

            // 假设所有字符都在 BMP (本字符串满足)，直接存入单个 UChar
            utf16.push_back(static_cast<UChar>(cp));
            byteOffsetOfUChar.push_back(offset);
            byteLenOfUChar.push_back(len);
            p += len;
        }
        size_t charCount = utf16.size(); // 逻辑字符数

        // ---------- 3. 使用 ICU Bidi 处理 ----------
        UErrorCode err = U_ZERO_ERROR;
        UBiDi *bidi = ubidi_open();
        ubidi_setPara(bidi, utf16.data(), static_cast<int32_t>(charCount),
                      UBIDI_DEFAULT_LTR, nullptr, &err);
        if (U_FAILURE(err))
        {
            std::cerr << "ICU ubidi_setPara failed: " << u_errorName(err) << '\n';
            return 1;
        }

        // ---------- 4. 获取视觉顺序的运行（与 SheenBidi 一致）----------
        int32_t runCount = ubidi_countRuns(bidi, &err);
        if (U_FAILURE(err))
        {
            std::cerr << "ICU ubidi_countRuns failed: " << u_errorName(err) << '\n';
            return 1;
        }

        // 为了方便，预先计算每个字符的累计字节偏移（用于快速得到 run 的字节长度）
        std::vector<size_t> byteEndOfChar(charCount);
        size_t curByte = 0;
        for (size_t i = 0; i < charCount; ++i)
        {
            curByte += byteLenOfUChar[i];
            byteEndOfChar[i] = curByte; // 字符结束的下一个字节位置
        }

        for (int32_t i = 0; i < runCount; ++i)
        {
            int32_t start, length;
            ubidi_getVisualRun(bidi, i, &start, &length);
            UBiDiLevel level = ubidi_getLevelAt(bidi, start);

            // 计算该 run 在原始 UTF-8 中的起始偏移和字节长度
            size_t byteStart = byteOffsetOfUChar[start];
            size_t byteLen = byteEndOfChar[start + length - 1] - byteStart;

            std::println("Run Offset: {}", byteStart);
            std::println("Run Length: {}", byteLen);
            std::println("Run Level: {}\n", static_cast<int>(level));

            // 提取文本片段（UTF-8 视图）
            std::string_view textFragment(bidiText + byteStart, byteLen);
            std::println("[{},{}): {}", byteStart, byteStart + byteLen, textFragment);
        }

        // ---------- 5. 镜像字符输出（模拟 SBMirrorLocator）----------
        // 遍历所有逻辑字符，如果字符可镜像且位于奇数级别（RTL）运行中，则输出镜像信息
        for (size_t idx = 0; idx < charCount; ++idx)
        {
            UChar32 ch = utf16[idx];
            if (!u_isMirrored(ch))
                continue; // 不需要镜像的字符
            UBiDiLevel level = ubidi_getLevelAt(bidi, static_cast<int32_t>(idx));
            if ((level & 1) == 0)
                continue; // 偶数级别（LTR）不镜像

            UChar32 mirrored = u_charMirror(ch);
            if (mirrored == ch)
                continue; // 实际镜像无变化

            size_t bytePos = byteOffsetOfUChar[idx]; // 字节索引
            std::println("Mirror Index: {}", bytePos);
            std::println("Actual Code Point: {}", static_cast<uint32_t>(ch));
            std::println("Mirrored Code Point: {}\n", static_cast<uint32_t>(mirrored));
        }

        // 释放 ICU 资源
        ubidi_close(bidi);

        std::println("===================ICU bidi end=============");
    }
    //

    std::cout << "main done\n";
    return 0;
}
catch (const std::exception &e)
{
    std::cerr << "Exception: " << e.what() << '\n';
    return 1;
}
catch (...)
{
    std::cerr << "Unknown exception\n";
    return 1;
}