#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <unordered_map>

#include <print>

#include <SheenBidi/SheenBidi.h>
#include <SheenBidi/SBCodepointSequence.h>
#include <SheenBidi/SBAlgorithm.h>
#include <SheenBidi/SBLine.h>
#include <SheenBidi/SBMirrorLocator.h>

// UTF-8 → 码点数组 + 字节偏移
struct CodepointInfo
{
    std::vector<char32_t> codepoints;
    std::vector<size_t> byteOffsets; // 每个码点的起始字节偏移
};

CodepointInfo utf8ToCodepointsWithOffsets(const std::string &utf8)
{
    CodepointInfo info;
    const char *p = utf8.data();
    const char *end = p + utf8.size();
    while (p < end)
    {
        size_t offset = p - utf8.data();
        char32_t cp;
        if ((*p & 0x80) == 0)
        {
            cp = *p++;
        }
        else if ((*p & 0xE0) == 0xC0)
        {
            cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
            p += 2;
        }
        else if ((*p & 0xF0) == 0xE0)
        {
            cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
            p += 3;
        }
        else if ((*p & 0xF8) == 0xF0)
        {
            cp = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) |
                 (p[3] & 0x3F);
            p += 4;
        }
        else
        {
            ++p;
            continue;
        }
        info.codepoints.push_back(cp);
        info.byteOffsets.push_back(offset);
    }
    return info;
}

// 码点数组 → UTF-8
std::string codepointsToUtf8(const std::vector<char32_t> &cps)
{
    std::string result;
    for (char32_t cp : cps)
    {
        if (cp <= 0x7F)
        {
            result.push_back(static_cast<char>(cp));
        }
        else if (cp <= 0x7FF)
        {
            result.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
            result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else if (cp <= 0xFFFF)
        {
            result.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
            result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else if (cp <= 0x10FFFF)
        {
            result.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
            result.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return result;
}

int main()
{
    try
    {
        const char *bidiText = "یہ ایک )car( ہے۔";
        size_t textLen = strlen(bidiText);

        // 1. 逻辑码点与字节偏移
        auto info = utf8ToCodepointsWithOffsets(bidiText);
        const auto &logical = info.codepoints;
        const auto &byteOffsets = info.byteOffsets;

        // 2. SheenBidi 获取运行和镜像
        SBCodepointSequence codepointSequence = {SBStringEncodingUTF8, (void *)bidiText,
                                                 textLen};
        SBAlgorithmRef bidiAlgorithm = SBAlgorithmCreate(&codepointSequence);
        SBParagraphRef firstParagraph =
            SBAlgorithmCreateParagraph(bidiAlgorithm, 0, INT32_MAX, SBLevelDefaultLTR);
        SBUInteger paragraphLength = SBParagraphGetLength(firstParagraph);
        SBLineRef paragraphLine =
            SBParagraphCreateLine(firstParagraph, 0, paragraphLength);
        SBUInteger runCount = SBLineGetRunCount(paragraphLine);
        const SBRun *runArray = SBLineGetRunsPtr(paragraphLine);

        struct Run
        {
            size_t offset, length, level;
        };
        std::vector<Run> runs;
        for (SBUInteger i = 0; i < runCount; ++i)
        {
            runs.push_back({runArray[i].offset, runArray[i].length, runArray[i].level});
            std::println("Run Offset: {}", runArray[i].offset);
            std::println("Run Length: {}", runArray[i].length);
            std::println("Run Level: {}\n", runArray[i].level);
        }

        // 镜像映射：字节偏移 → 镜像码点
        SBMirrorLocatorRef mirrorLocator = SBMirrorLocatorCreate();
        SBMirrorLocatorLoadLine(mirrorLocator, paragraphLine, (void *)bidiText);
        const SBMirrorAgent *mirrorAgent = SBMirrorLocatorGetAgent(mirrorLocator);

        std::unordered_map<size_t, char32_t> mirrorMap;
        while (SBMirrorLocatorMoveNext(mirrorLocator))
        {
            std::println("Mirror Index: {}", mirrorAgent->index);
            std::println("Actual Code Point: {}", mirrorAgent->codepoint);
            std::println("Mirrored Code Point: {}\n", mirrorAgent->mirror);
            mirrorMap[mirrorAgent->index] = static_cast<char32_t>(mirrorAgent->mirror);
        }

        SBMirrorLocatorRelease(mirrorLocator);
        SBLineRelease(paragraphLine);
        SBParagraphRelease(firstParagraph);
        SBAlgorithmRelease(bidiAlgorithm);

        // 3. 将字节偏移的运行转换为字符索引范围
        struct CharRun
        {
            size_t start, end, level;
        };
        std::vector<CharRun> charRuns;
        for (const auto &run : runs)
        {
            size_t startByte = run.offset;
            size_t endByte = run.offset + run.length;
            size_t first = byteOffsets.size(), last = 0;
            for (size_t i = 0; i < byteOffsets.size(); ++i)
            {
                if (byteOffsets[i] >= startByte && byteOffsets[i] < endByte)
                {
                    if (first == byteOffsets.size())
                        first = i;
                    last = i;
                }
            }
            if (first < byteOffsets.size())
            {
                charRuns.push_back({first, last + 1, run.level});
            }
        }

        // 4. 构建视觉字符串（baseLevel = 0 LTR，1 RTL）
        auto buildVisual = [&](int baseLevel) -> std::string {
            // 决定运行顺序
            std::vector<size_t> order;
            if (baseLevel % 2 == 0)
            { // LTR 基础
                for (size_t i = 0; i < charRuns.size(); ++i)
                    order.push_back(i);
            }
            else
            { // RTL 基础
                for (size_t i = charRuns.size(); i-- > 0;)
                    order.push_back(i);
            }

            std::vector<char32_t> visual;
            for (size_t idx : order)
            {
                const auto &run = charRuns[idx];
                std::vector<char32_t> runChars;
                for (size_t i = run.start; i < run.end; ++i)
                {
                    auto it = mirrorMap.find(byteOffsets[i]);
                    runChars.push_back(it != mirrorMap.end() ? it->second : logical[i]);
                }
                if (run.level % 2 == 1)
                {
                    std::reverse(runChars.begin(), runChars.end());
                }
                visual.insert(visual.end(), runChars.begin(), runChars.end());
            }
            return codepointsToUtf8(visual);
        };

        std::string ltrVisual = buildVisual(0); // LTR 基础方向
        std::string rtlVisual = buildVisual(1); // RTL 基础方向

        std::cout << "LTR 段落显示: " << ltrVisual << std::endl;
        std::cout << "RTL 段落显示: " << rtlVisual << std::endl;
        std::cout << "main done\n";
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
    return 0;
}