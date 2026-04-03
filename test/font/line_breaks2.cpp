#include <cassert>
#include <iostream>
#include <exception>

#include <print>
#include <string_view>
#include <vector>

#include <cstdint>
#include <cstring>

#include <SheenBidi/SheenBidi.h>

#include <unicode/ubidi.h>   // ICU BIDI 核心
#include <unicode/ustring.h> // UTF-16 转换
#include <unicode/uchar.h>   // u_charMirror 等

#include <fstream>

// 简单的 UTF-32 到 UTF-8 转换器，用于打印文本片段
std::string utf32ToUtf8(const char32_t *u32str, size_t u32len)
{
    std::string out;
    for (size_t i = 0; i < u32len; ++i)
    {
        char32_t cp = u32str[i];
        if (cp < 0x80)
        {
            out.push_back(static_cast<char>(cp));
        }
        else if (cp < 0x800)
        {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else if (cp < 0x10000)
        {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

int main()
{
    try
    {
        auto write_file = [](std::string content, std::string file_name) {
            std::ofstream file(file_name);
            if (file.is_open())
            {
                file << content; // 写入字符串
                file.close();
            };
        };
        {
            std::println("===================base bidi start=============");
            constexpr auto bidiTextU32 = U"W3C (World) \nמעביר את שירותי- ERCIM.";
            size_t u32len = std::u32string_view{bidiTextU32}.size();

            // 1. 将 UTF-32 转为 UTF-8，并构建映射表：每个 UTF-32 字符的起始字节偏移
            std::string utf8Text;
            std::vector<size_t> u32ToByteOffset(u32len + 1, 0); // 多一个用于结尾
            for (size_t i = 0; i < u32len; ++i)
            {
                u32ToByteOffset[i] = utf8Text.size();
                char32_t cp = bidiTextU32[i];
                if (cp < 0x80)
                    utf8Text.push_back(static_cast<char>(cp));
                else if (cp < 0x800)
                {
                    utf8Text.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                    utf8Text.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
                else if (cp < 0x10000)
                {
                    utf8Text.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                    utf8Text.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    utf8Text.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
                else
                {
                    utf8Text.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                    utf8Text.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                    utf8Text.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    utf8Text.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
            }
            u32ToByteOffset[u32len] = utf8Text.size();

            // 2. SheenBidi 处理
            SBCodepointSequence codepointSequence = {SBStringEncodingUTF32,
                                                     (void *)bidiTextU32, u32len};

            SBAlgorithmRef algorithm = SBAlgorithmCreate(&codepointSequence);
            SBUInteger totalLength = u32len;
            SBUInteger offset = 0;

            while (offset < totalLength)
            {
                SBUInteger actualLength = 0, separatorLength = 0;
                SBAlgorithmGetParagraphBoundary(algorithm, offset, INT32_MAX,
                                                &actualLength, &separatorLength);
                if (actualLength == 0)
                    break;

                SBParagraphRef paragraph = SBAlgorithmCreateParagraph(
                    algorithm, offset, actualLength, SBLevelDefaultLTR);
                if (!paragraph)
                    break;

                // 使用绝对偏移 offset 作为 lineOffset
                SBLineRef line = SBParagraphCreateLine(paragraph, offset, actualLength);
                if (!line)
                {
                    SBParagraphRelease(paragraph);
                    break;
                }

                SBUInteger runCount = SBLineGetRunCount(line);
                const SBRun *runs = SBLineGetRunsPtr(line);

                std::println("--- Paragraph at UTF-32 offset {} (length {} chars) ---",
                             offset, actualLength);
                for (SBUInteger i = 0; i < runCount; ++i)
                {
                    // runs[i].offset 是绝对 UTF-32 索引
                    SBUInteger u32start = runs[i].offset;
                    SBUInteger u32end = u32start + runs[i].length;
                    size_t byteStart = u32ToByteOffset[u32start];
                    size_t byteEnd = u32ToByteOffset[u32end];
                    std::string_view textFragment(utf8Text.data() + byteStart,
                                                  byteEnd - byteStart);

                    std::println("Run Byte Offset: {}", byteStart);
                    std::println("Run Byte Length: {}", byteEnd - byteStart);
                    std::println("Run Level: {}\n", (long)runs[i].level);
                    std::println("[{},{}): {}", byteStart, byteEnd, textFragment);
                }

                // 镜像处理
                SBMirrorLocatorRef mirrorLocator = SBMirrorLocatorCreate();
                SBMirrorLocatorLoadLine(mirrorLocator, line, (void *)bidiTextU32);
                const SBMirrorAgent *agent = SBMirrorLocatorGetAgent(mirrorLocator);
                while (SBMirrorLocatorMoveNext(mirrorLocator))
                {
                    SBUInteger u32idx = agent->index;
                    size_t bytePos = u32ToByteOffset[u32idx];
                    char32_t original = agent->codepoint;
                    char32_t mirrored = agent->mirror;

                    // 将码点转为 UTF-8 用于打印（可选）
                    std::string origUtf8 = utf32ToUtf8(&original, 1);
                    std::string mirrUtf8 = utf32ToUtf8(&mirrored, 1);

                    std::println("Mirror Byte Index: {}", bytePos);
                    std::println("Actual Code Point: U+{:04X} ({})",
                                 (unsigned int)original, origUtf8);
                    std::println("Mirrored Code Point: U+{:04X} ({})\n",
                                 (unsigned int)mirrored, mirrUtf8);
                }
                SBMirrorLocatorRelease(mirrorLocator);

                SBLineRelease(line);
                SBParagraphRelease(paragraph);

                offset += actualLength + separatorLength;
            }

            SBAlgorithmRelease(algorithm);
            std::println("===================base bidi end=============");
        }

        {
            std::println("===================ICU bidi end=============");

            // ---------- 1. 原始 UTF-8 文本 ----------
            const char *bidiText = "W3C (World) \nמעביר את שירותי- ERCIM.";
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
                    cp = ((*p & 0x0F) << 12) | ((*(p + 1) & 0x3F) << 6) |
                         (*(p + 2) & 0x3F);
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
                std::println("Mirrored Code Point: {}\n",
                             static_cast<uint32_t>(mirrored));
            }

            // 释放 ICU 资源
            ubidi_close(bidi);

            std::println("===================ICU bidi end=============");
        }
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
}