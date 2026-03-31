#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <unicode/ubidi.h>
#include <unicode/ustring.h>
#include <unicode/uchar.h>

int main()
{
    // ---------- 1. 原始 UTF-8 文本 ----------
    const char *bidiText = "یہ ایک )car( ہے۔";
    size_t textLen = strlen(bidiText);

    // ---------- 2. 将 UTF-8 转换为 UTF-16 ----------
    std::vector<UChar> utf16;
    std::vector<size_t> byteOffsetOfUChar; // 每个字符在原始 UTF-8 中的字节偏移（起始）
    std::vector<size_t> byteLenOfUChar;    // 每个字符的 UTF-8 字节长度

    const unsigned char *p = reinterpret_cast<const unsigned char *>(bidiText);
    const unsigned char *end = p + textLen;
    while (p < end)
    {
        size_t offset = p - reinterpret_cast<const unsigned char *>(bidiText);
        UChar32 cp;
        int32_t len;

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
        { // 4 字节
            cp = ((*p & 0x07) << 18) | ((*(p + 1) & 0x3F) << 12) |
                 ((*(p + 2) & 0x3F) << 6) | (*(p + 3) & 0x3F);
            len = 4;
        }
        else
        {
            cp = 0xFFFD; // 非法字节
            len = 1;
        }

        utf16.push_back(static_cast<UChar>(cp)); // 本示例中所有字符都在 BMP
        byteOffsetOfUChar.push_back(offset);
        byteLenOfUChar.push_back(len);
        p += len;
    }
    size_t charCount = utf16.size();

    // ---------- 3. 使用 ICU Bidi，强制 LTR 段落 ----------
    UErrorCode err = U_ZERO_ERROR;
    UBiDi *bidi = ubidi_open();
    ubidi_setPara(bidi, utf16.data(), static_cast<int32_t>(charCount), UBIDI_LTR, nullptr,
                  &err);
    if (U_FAILURE(err))
    {
        std::cerr << "ubidi_setPara failed: " << u_errorName(err) << '\n';
        return 1;
    }

    // ---------- 4. 获取级别、映射 ----------
    const uint8_t *levels = ubidi_getLevels(bidi, &err);
    if (U_FAILURE(err))
    {
        std::cerr << "ubidi_getLevels failed: " << u_errorName(err) << '\n';
        return 1;
    }

    int32_t *logicalMap = new int32_t[charCount];
    ubidi_getLogicalMap(bidi, logicalMap, &err);
    if (U_FAILURE(err))
    {
        std::cerr << "ubidi_getLogicalMap failed: " << u_errorName(err) << '\n';
        delete[] logicalMap;
        ubidi_close(bidi);
        return 1;
    }

    // ---------- 5. 输出表格（逻辑顺序） ----------
    std::cout << std::left << std::setw(12) << "Memory pos." << std::setw(12)
              << "Character" << std::setw(12) << "Code point" << std::setw(8) << "Level"
              << std::setw(12) << "Visual pos."
              << "Mirrored?\n";

    for (size_t i = 0; i < charCount; ++i)
    {
        UChar ch = utf16[i];
        uint32_t cp = static_cast<uint32_t>(ch);

        // 将单个 UChar 转为 UTF-8 用于显示
        char utf8Buf[5] = {0};
        UErrorCode convErr = U_ZERO_ERROR;
        int32_t utf8Len = 0;
        u_strToUTF8(utf8Buf, 4, &utf8Len, &ch, 1, &convErr);
        std::string charStr = (U_SUCCESS(convErr)) ? utf8Buf : "?";

        int32_t visualPos = logicalMap[i];
        bool mirrored = (u_charMirror(ch) != ch);

        std::cout << std::setw(12) << i << std::setw(12) << charStr << std::hex
                  << std::showbase << std::setw(12) << cp << std::dec << std::noshowbase
                  << std::setw(8) << static_cast<int>(levels[i]) << std::setw(12)
                  << visualPos << (mirrored ? "Yes" : "No") << '\n';
    }

    // ---------- 6. 构建视觉顺序字符串 ----------
    std::string visualString;

    int32_t runCount = ubidi_countRuns(bidi, &err);
    if (U_FAILURE(err))
    {
        std::cerr << "ubidi_countRuns failed: " << u_errorName(err) << '\n';
        delete[] logicalMap;
        ubidi_close(bidi);
        return 1;
    }

    for (int32_t i = 0; i < runCount; ++i)
    {
        int32_t start, length;
        ubidi_getVisualRun(bidi, i, &start, &length);
        UBiDiLevel level = ubidi_getLevelAt(bidi, start);
        bool isRTL = (level % 2 == 1); // 奇数级别为 RTL

        if (!isRTL)
        {
            // LTR run：正向遍历
            for (int32_t j = 0; j < length; ++j)
            {
                int32_t logicalPos = start + j;
                UChar ch = utf16[logicalPos];
                // LTR 中的字符不需要镜像（即使可镜像也不应用）
                char utf8Buf[5] = {0};
                UErrorCode convErr = U_ZERO_ERROR;
                int32_t utf8Len = 0;
                u_strToUTF8(utf8Buf, 4, &utf8Len, &ch, 1, &convErr);
                visualString.append(utf8Buf, utf8Len);
            }
        }
        else
        {
            // RTL run：反向遍历，并应用镜像
            for (int32_t j = length - 1; j >= 0; --j)
            {
                int32_t logicalPos = start + j;
                UChar ch = utf16[logicalPos];
                if (u_isMirrored(ch))
                {
                    ch = u_charMirror(ch);
                }
                char utf8Buf[5] = {0};
                UErrorCode convErr = U_ZERO_ERROR;
                int32_t utf8Len = 0;
                u_strToUTF8(utf8Buf, 4, &utf8Len, &ch, 1, &convErr);
                visualString.append(utf8Buf, utf8Len);
            }
        }
    }

    std::cout << "\nReordered Display (visual order):\n";
    std::cout << visualString << std::endl;

    // ---------- 7. 清理资源 ----------
    delete[] logicalMap;
    ubidi_close(bidi);
    return 0;
}