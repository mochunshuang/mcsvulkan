#include <iostream>
#include <iomanip>
#include <vector>
#include <cstring>
#include <algorithm>

#include <SheenBidi/SheenBidi.h>

// -------------------- UTF-8 解码（保持不变） --------------------
static std::vector<uint32_t> decodeUTF8(const char *str, std::vector<size_t> &offsets,
                                        std::vector<size_t> &lens)
{
    std::vector<uint32_t> cps;
    size_t pos = 0;
    while (str[pos])
    {
        offsets.push_back(pos);
        unsigned char c = str[pos];
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
        cps.push_back(cp);
        lens.push_back(len);
        pos += len;
    }
    return cps;
}

static std::string cpToUTF8(uint32_t cp)
{
    char buf[5] = {0};
    if (cp <= 0x7F)
    {
        buf[0] = cp;
    }
    else if (cp <= 0x7FF)
    {
        buf[0] = 0xC0 | (cp >> 6);
        buf[1] = 0x80 | (cp & 0x3F);
    }
    else if (cp <= 0xFFFF)
    {
        buf[0] = 0xE0 | (cp >> 12);
        buf[1] = 0x80 | ((cp >> 6) & 0x3F);
        buf[2] = 0x80 | (cp & 0x3F);
    }
    else if (cp <= 0x10FFFF)
    {
        buf[0] = 0xF0 | (cp >> 18);
        buf[1] = 0x80 | ((cp >> 12) & 0x3F);
        buf[2] = 0x80 | ((cp >> 6) & 0x3F);
        buf[3] = 0x80 | (cp & 0x3F);
    }
    else
        return "?";
    return buf;
}

static int byteToIndex(const std::vector<size_t> &offsets, size_t byte)
{
    auto it = std::lower_bound(offsets.begin(), offsets.end(), byte);
    return (it != offsets.end() && *it == byte) ? static_cast<int>(it - offsets.begin())
                                                : -1;
}

struct CharRun
{
    size_t offset, length;
    SBLevel level;
};
const SBLevel baseLevel = 0; // 0 = LTR, 1 = RTL

int main()
{

    const char *text = "یہ ایک )car( ہے۔";
    std::vector<size_t> offsets, lens;
    auto cps = decodeUTF8(text, offsets, lens);
    size_t n = cps.size();

    // 获取双向级别
    SBCodepointSequence seq = {SBStringEncodingUTF8, (void *)text, strlen(text)};
    SBAlgorithmRef alg = SBAlgorithmCreate(&seq);
    SBParagraphRef para = SBAlgorithmCreateParagraph(alg, 0, INT32_MAX, baseLevel);
    const SBLevel *levelsPtr = SBParagraphGetLevelsPtr(para);
    std::vector<SBLevel> levels(n);
    for (size_t i = 0; i < n; ++i)
        levels[i] = levelsPtr[offsets[i]];

    // 提取逻辑运行（手动分组）
    std::vector<CharRun> runs;
    for (size_t i = 0; i < n;)
    {
        size_t j = i + 1;
        while (j < n && levels[j] == levels[i])
            ++j;
        runs.push_back({i, j - i, levels[i]});
        i = j;
    }

    std::vector<size_t> visualOrder;
    visualOrder.reserve(n);
    // 构建视觉顺序（使用 lambda 复用添加逻辑）
    auto addRun = [&](const CharRun &run) {
        if ((run.level & 1) == 0)
        { // LTR 运行，正序
            for (size_t k = run.offset; k < run.offset + run.length; ++k)
                visualOrder.push_back(k);
        }
        else
        { // RTL 运行，逆序
            for (size_t k = run.offset + run.length; k-- > run.offset;)
                visualOrder.push_back(k);
        }
    };

    if (baseLevel == 0)
    { // LTR 段落，正序遍历 runs
        for (auto &r : runs)
            addRun(r);
    }
    else
    { // RTL 段落，逆序遍历 runs
        for (auto it = runs.rbegin(); it != runs.rend(); ++it)
            addRun(*it);
    }

    // 逻辑到视觉映射
    std::vector<size_t> logToVis(n, -1);
    for (size_t v = 0; v < visualOrder.size(); ++v)
        logToVis[visualOrder[v]] = v;

    // 镜像信息（仅表格）
    SBLineRef line = SBParagraphCreateLine(para, 0, SBParagraphGetLength(para));
    SBMirrorLocatorRef ml = SBMirrorLocatorCreate();
    SBMirrorLocatorLoadLine(ml, line, (void *)text);
    std::vector<bool> mirrored(n, false);
    while (SBMirrorLocatorMoveNext(ml))
    {
        int idx = byteToIndex(offsets, SBMirrorLocatorGetAgent(ml)->index);
        if (idx >= 0)
            mirrored[idx] = true;
    }

    // 输出表格
    std::cout << std::left << std::setw(12) << "Memory pos." << std::setw(12)
              << "Character" << std::setw(12) << "Code point" << std::setw(8) << "Level"
              << std::setw(12) << "Visual pos."
              << "Mirrored?" << std::endl;
    for (size_t i = 0; i < n; ++i)
    {
        std::cout << std::setw(12) << i << std::setw(12) << cpToUTF8(cps[i]) << std::hex
                  << std::showbase << std::setw(12) << cps[i] << std::dec
                  << std::noshowbase << std::setw(8) << static_cast<int>(levels[i])
                  << std::setw(12) << logToVis[i] << (mirrored[i] ? "Yes" : "No")
                  << std::endl;
    }

    // 视觉顺序字符串
    std::string visStr;
    for (size_t idx : visualOrder)
        visStr += cpToUTF8(cps[idx]);
    std::cout << "\nReordered Display (left-to-right):\n" << visStr << std::endl;

    // 输出运行信息
    std::cout << "\nCharRun array (logical order):\n";
    for (auto &r : runs)
    {
        std::cout << "  offset=" << r.offset << ", length=" << r.length
                  << ", level=" << static_cast<int>(r.level) << std::endl;
    }

    // 清理资源
    SBMirrorLocatorRelease(ml);
    SBLineRelease(line);
    SBParagraphRelease(para);
    SBAlgorithmRelease(alg);

    return 0;
}