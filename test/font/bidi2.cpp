#include <iostream>
#include <iomanip>
#include <vector>
#include <cstring>
#include <algorithm>

#include <SheenBidi/SheenBidi.h>

// ----------------------------------------------------------------------
// UTF-8 解码函数
static std::vector<uint32_t> decodeUTF8(const char *str, std::vector<size_t> &byteOffsets,
                                        std::vector<size_t> &byteLengths)
{
    std::vector<uint32_t> codepoints;
    byteOffsets.clear();
    byteLengths.clear();
    size_t pos = 0;
    while (str[pos] != '\0')
    {
        byteOffsets.push_back(pos);
        unsigned char c = static_cast<unsigned char>(str[pos]);
        uint32_t cp;
        size_t len;
        if ((c & 0x80) == 0)
        {
            cp = c;
            len = 1;
        }
        else if ((c & 0xE0) == 0xC0)
        {
            cp = ((c & 0x1F) << 6) | (str[pos + 1] & 0x3F);
            len = 2;
        }
        else if ((c & 0xF0) == 0xE0)
        {
            cp =
                ((c & 0x0F) << 12) | ((str[pos + 1] & 0x3F) << 6) | (str[pos + 2] & 0x3F);
            len = 3;
        }
        else if ((c & 0xF8) == 0xF0)
        {
            cp = ((c & 0x07) << 18) | ((str[pos + 1] & 0x3F) << 12) |
                 ((str[pos + 2] & 0x3F) << 6) | (str[pos + 3] & 0x3F);
            len = 4;
        }
        else
        {
            cp = 0xFFFD;
            len = 1;
        }
        codepoints.push_back(cp);
        byteLengths.push_back(len);
        pos += len;
    }
    return codepoints;
}

// 代码点转 UTF-8 字符串
static std::string codepointToUTF8(uint32_t cp)
{
    char buf[5] = {0};
    if (cp <= 0x7F)
    {
        buf[0] = static_cast<char>(cp);
    }
    else if (cp <= 0x7FF)
    {
        buf[0] = static_cast<char>(0xC0 | (cp >> 6));
        buf[1] = static_cast<char>(0x80 | (cp & 0x3F));
    }
    else if (cp <= 0xFFFF)
    {
        buf[0] = static_cast<char>(0xE0 | (cp >> 12));
        buf[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = static_cast<char>(0x80 | (cp & 0x3F));
    }
    else if (cp <= 0x10FFFF)
    {
        buf[0] = static_cast<char>(0xF0 | (cp >> 18));
        buf[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = static_cast<char>(0x80 | (cp & 0x3F));
    }
    else
    {
        return "?";
    }
    return std::string(buf);
}

// 根据字节偏移查找字符索引
static int byteOffsetToCharIndex(const std::vector<size_t> &byteOffsets,
                                 size_t byteOffset)
{
    auto it = std::lower_bound(byteOffsets.begin(), byteOffsets.end(), byteOffset);
    if (it != byteOffsets.end() && *it == byteOffset)
        return static_cast<int>(it - byteOffsets.begin());
    return -1;
}

// 运行信息结构体（逻辑顺序，连续相同级别）
struct CharRun
{
    size_t offset; // 逻辑起始索引
    size_t length; // 字符数量
    SBLevel level; // 级别
};

// ----------------------------------------------------------------------
int main()
{
    // 选择段落方向：0 = LTR, 1 = RTL
    const SBLevel baseLevel = 1; // 改为 0 切换到 LTR 段落

    const char *bidiText = "یہ ایک )car( ہے۔";
    size_t textLenBytes = strlen(bidiText);

    // 1. 解码 UTF-8
    std::vector<size_t> byteOffsets, byteLengths;
    std::vector<uint32_t> codepoints = decodeUTF8(bidiText, byteOffsets, byteLengths);
    size_t charCount = codepoints.size();

    // 2. 创建 SheenBidi 对象，使用指定的基础级别
    SBCodepointSequence codepointSequence = {SBStringEncodingUTF8, (void *)bidiText,
                                             textLenBytes};
    SBAlgorithmRef algorithm = SBAlgorithmCreate(&codepointSequence);
    SBParagraphRef paragraph =
        SBAlgorithmCreateParagraph(algorithm, 0, INT32_MAX, baseLevel);
    SBUInteger paragraphLength = SBParagraphGetLength(paragraph);

    // 3. 获取每个字符的级别（按字节偏移索引）
    const SBLevel *levelsPtr = SBParagraphGetLevelsPtr(paragraph);
    std::vector<SBLevel> levels(charCount, 0);
    for (size_t i = 0; i < charCount; ++i)
    {
        size_t byteOffset = byteOffsets[i];
        if (byteOffset < paragraphLength)
        {
            levels[i] = levelsPtr[byteOffset];
        }
    }

    // 4. 提取运行信息（逻辑顺序，级别连续相同的段）
    std::vector<CharRun> runs;
    size_t start = 0;
    while (start < charCount)
    {
        SBLevel currentLevel = levels[start];
        size_t end = start + 1;
        while (end < charCount && levels[end] == currentLevel)
            ++end;
        runs.push_back({start, end - start, currentLevel});
        start = end;
    }

    // 5. 构建视觉顺序（从左到右显示顺序）
    std::vector<size_t> visualToLogical;
    visualToLogical.reserve(charCount);

    if (baseLevel == 0)
    { // LTR 段落
        for (const auto &run : runs)
        {
            if ((run.level & 1) == 0)
            { // LTR 运行
                for (size_t i = run.offset; i < run.offset + run.length; ++i)
                {
                    visualToLogical.push_back(i);
                }
            }
            else
            { // RTL 运行
                for (size_t i = run.offset + run.length; i-- > run.offset;)
                {
                    visualToLogical.push_back(i);
                }
            }
        }
    }
    else
    { // RTL 段落
        for (auto it = runs.rbegin(); it != runs.rend(); ++it)
        {
            const auto &run = *it;
            if ((run.level & 1) == 0)
            { // LTR 运行
                for (size_t i = run.offset; i < run.offset + run.length; ++i)
                {
                    visualToLogical.push_back(i);
                }
            }
            else
            { // RTL 运行
                for (size_t i = run.offset + run.length; i-- > run.offset;)
                {
                    visualToLogical.push_back(i);
                }
            }
        }
    }

    // 构建逻辑到视觉的映射
    std::vector<size_t> logicalToVisual(charCount, static_cast<size_t>(-1));
    for (size_t vis = 0; vis < visualToLogical.size(); ++vis)
    {
        logicalToVisual[visualToLogical[vis]] = vis;
    }

    // 6. 获取镜像信息（仅用于表格）
    SBLineRef line = SBParagraphCreateLine(paragraph, 0, paragraphLength);
    SBMirrorLocatorRef mirrorLocator = SBMirrorLocatorCreate();
    SBMirrorLocatorLoadLine(mirrorLocator, line, (void *)bidiText);
    std::vector<bool> isMirrored(charCount, false);
    while (SBMirrorLocatorMoveNext(mirrorLocator))
    {
        const SBMirrorAgent *agent = SBMirrorLocatorGetAgent(mirrorLocator);
        int charIndex = byteOffsetToCharIndex(byteOffsets, agent->index);
        if (charIndex >= 0)
        {
            isMirrored[charIndex] = true;
        }
    }

    // 7. 输出表格（逻辑顺序）
    std::cout << std::left << std::setw(12) << "Memory pos." << std::setw(12)
              << "Character" << std::setw(12) << "Code point" << std::setw(8) << "Level"
              << std::setw(12) << "Visual pos."
              << "Mirrored?" << std::endl;

    for (size_t i = 0; i < charCount; ++i)
    {
        std::string charStr = codepointToUTF8(codepoints[i]);
        std::cout << std::setw(12) << i << std::setw(12) << charStr << std::hex
                  << std::showbase << std::setw(12) << codepoints[i] << std::dec
                  << std::noshowbase << std::setw(8) << static_cast<int>(levels[i])
                  << std::setw(12) << logicalToVisual[i] << (isMirrored[i] ? "Yes" : "No")
                  << std::endl;
    }

    // 8. 视觉顺序字符串（不应用镜像）
    std::string visualString;
    for (size_t visPos = 0; visPos < visualToLogical.size(); ++visPos)
    {
        size_t logicalIdx = visualToLogical[visPos];
        visualString += codepointToUTF8(codepoints[logicalIdx]);
    }
    std::cout << "\nReordered Display (visual order, left-to-right):\n"
              << visualString << std::endl;

    // 9. 输出运行信息（CharRun 数组）
    std::cout << "\nCharRun array (logical order, contiguous same level):\n";
    for (const auto &run : runs)
    {
        std::cout << "  offset=" << run.offset << ", length=" << run.length
                  << ", level=" << static_cast<int>(run.level) << std::endl;
    }

    // 10. 清理资源
    SBMirrorLocatorRelease(mirrorLocator);
    SBLineRelease(line);
    SBParagraphRelease(paragraph);
    SBAlgorithmRelease(algorithm);

    return 0;
}