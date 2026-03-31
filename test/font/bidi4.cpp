#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <memory>
#include <optional>
#include <utf8proc.h>
#include <SheenBidi/SheenBidi.h>

// ========== 智能指针删除器 ==========
struct SBAlgorithmDeleter
{
    void operator()(SBAlgorithmRef p) const
    {
        if (p)
            SBAlgorithmRelease(p);
    }
};
struct SBParagraphDeleter
{
    void operator()(SBParagraphRef p) const
    {
        if (p)
            SBParagraphRelease(p);
    }
};
struct SBLineDeleter
{
    void operator()(SBLineRef p) const
    {
        if (p)
            SBLineRelease(p);
    }
};
struct SBMirrorLocatorDeleter
{
    void operator()(SBMirrorLocatorRef p) const
    {
        if (p)
            SBMirrorLocatorRelease(p);
    }
};

using SBAlgorithmPtr =
    std::unique_ptr<std::remove_pointer_t<SBAlgorithmRef>, SBAlgorithmDeleter>;
using SBParagraphPtr =
    std::unique_ptr<std::remove_pointer_t<SBParagraphRef>, SBParagraphDeleter>;
using SBLinePtr = std::unique_ptr<std::remove_pointer_t<SBLineRef>, SBLineDeleter>;
using SBMirrorLocatorPtr =
    std::unique_ptr<std::remove_pointer_t<SBMirrorLocatorRef>, SBMirrorLocatorDeleter>;

// ========== UTF-8 辅助函数 ==========
std::pair<utf8proc_int32_t, int> safe_utf8_next(const char *ptr, size_t remaining)
{
    utf8proc_int32_t codepoint;
    int advance = utf8proc_iterate(reinterpret_cast<const utf8proc_uint8_t *>(ptr),
                                   static_cast<int>(remaining), &codepoint);
    if (advance <= 0)
    {
        return {0, 1};
    }
    return {codepoint, advance};
}

std::vector<size_t> build_char_start_offsets(const std::string &text)
{
    std::vector<size_t> offsets;
    const char *ptr = text.data();
    const char *end = ptr + text.size();
    while (ptr < end)
    {
        offsets.push_back(ptr - text.data());
        auto [codepoint, advance] = safe_utf8_next(ptr, end - ptr);
        (void)codepoint;
        ptr += advance;
    }
    return offsets;
}

std::optional<size_t> byte_offset_to_char_index(const std::vector<size_t> &start_offsets,
                                                size_t byte_offset)
{
    auto it = std::lower_bound(start_offsets.begin(), start_offsets.end(), byte_offset);
    if (it != start_offsets.end() && *it == byte_offset)
    {
        return static_cast<size_t>(it - start_offsets.begin());
    }
    return std::nullopt;
}

utf8proc_int32_t get_codepoint_at_char_index(const std::string &text,
                                             const std::vector<size_t> &start_offsets,
                                             size_t char_index)
{
    if (char_index >= start_offsets.size())
        return 0;
    size_t offset = start_offsets[char_index];
    const char *ptr = text.data() + offset;
    auto [codepoint, advance] = safe_utf8_next(ptr, text.size() - offset);
    return codepoint;
}

void print_char(utf8proc_int32_t codepoint)
{
    if (codepoint >= 0x20 && codepoint <= 0x7E)
    {
        std::cout << static_cast<char>(codepoint);
    }
    else if (codepoint == 0x0A)
    {
        std::cout << "\\n";
    }
    else if (codepoint == 0x0D)
    {
        std::cout << "\\r";
    }
    else if (codepoint == 0x09)
    {
        std::cout << "\\t";
    }
    else
    {
        std::cout << "U+" << std::hex << codepoint << std::dec;
    }
}

// 根据起始字节偏移和字节长度，计算字符索引范围
std::optional<std::pair<size_t, size_t>> get_char_range_for_byte_range(
    const std::string &text, const std::vector<size_t> &start_offsets, size_t start_byte,
    size_t byte_length)
{
    auto start_opt = byte_offset_to_char_index(start_offsets, start_byte);
    if (!start_opt)
        return std::nullopt;
    size_t start_idx = *start_opt;

    size_t accumulated = 0;
    size_t end_idx = start_idx;
    for (size_t i = start_idx; i < start_offsets.size(); ++i)
    {
        size_t this_char_byte_len;
        if (i + 1 < start_offsets.size())
            this_char_byte_len = start_offsets[i + 1] - start_offsets[i];
        else
            this_char_byte_len = text.size() - start_offsets[i];

        if (accumulated + this_char_byte_len > byte_length)
        {
            break; // 不应该发生
        }
        accumulated += this_char_byte_len;
        end_idx = i;
        if (accumulated == byte_length)
            break;
    }

    if (accumulated != byte_length)
        return std::nullopt;
    return std::make_pair(start_idx, end_idx);
}

// ========== 主函数 ==========
int main()
{
    const std::string bidiText = "یہ ایک )car( ہے۔";

    // 构建字符偏移表
    std::vector<size_t> char_start_offsets = build_char_start_offsets(bidiText);
    std::cout << "========== 字符偏移映射验证 ==========\n";
    std::cout << "文本: " << bidiText << "\n";
    std::cout << "总字符数: " << char_start_offsets.size() << "\n";
    for (size_t i = 0; i < char_start_offsets.size(); ++i)
    {
        utf8proc_int32_t cp =
            get_codepoint_at_char_index(bidiText, char_start_offsets, i);
        std::cout << "  字符[" << i << "] 字节偏移=" << char_start_offsets[i] << " 码点=";
        print_char(cp);
        std::cout << " (U+" << std::hex << cp << std::dec << ")\n";
    }
    std::cout << "\n";

    // SheenBidi 处理
    SBCodepointSequence codepointSequence = {SBStringEncodingUTF8, bidiText.c_str(),
                                             static_cast<SBUInteger>(bidiText.size())};

    SBAlgorithmPtr algorithm(SBAlgorithmCreate(&codepointSequence));
    if (!algorithm)
    {
        std::cerr << "错误: 无法创建 SBAlgorithm\n";
        return 1;
    }

    SBParagraphPtr paragraph(
        SBAlgorithmCreateParagraph(algorithm.get(), 0, INT32_MAX, SBLevelDefaultLTR));
    if (!paragraph)
    {
        std::cerr << "错误: 无法创建 SBParagraph\n";
        return 1;
    }

    SBUInteger paragraphLength = SBParagraphGetLength(paragraph.get());
    SBLinePtr line(SBParagraphCreateLine(paragraph.get(), 0, paragraphLength));
    if (!line)
    {
        std::cerr << "错误: 无法创建 SBLine\n";
        return 1;
    }

    SBUInteger runCount = SBLineGetRunCount(line.get());
    const SBRun *runArray = SBLineGetRunsPtr(line.get());

    std::cout << "========== 双向运行 (Runs) ==========\n";
    for (SBUInteger i = 0; i < runCount; ++i)
    {
        const SBRun &run = runArray[i];
        std::cout << "Run " << i << ":\n";
        std::cout << "  字节范围: [" << run.offset << ", "
                  << (run.offset + run.length - 1) << "] 长度=" << run.length << "\n";
        std::cout << "  双向级别: " << run.level << " ("
                  << ((run.level & 1) ? "RTL" : "LTR") << ")\n";

        auto char_range = get_char_range_for_byte_range(bidiText, char_start_offsets,
                                                        run.offset, run.length);
        if (char_range)
        {
            auto [start_char, end_char] = *char_range;
            std::cout << "  字符范围: [" << start_char << ", " << end_char << "]\n";
            std::cout << "  字符序列: ";
            for (size_t idx = start_char; idx <= end_char; ++idx)
            {
                utf8proc_int32_t cp =
                    get_codepoint_at_char_index(bidiText, char_start_offsets, idx);
                print_char(cp);
                if (idx < end_char)
                    std::cout << " ";
            }
            std::cout << "\n";
        }
        else
        {
            std::cout << "  警告: 无法计算字符范围\n";
        }
        std::cout << "\n";
    }

    // 镜像字符定位
    SBMirrorLocatorPtr mirrorLocator(SBMirrorLocatorCreate());
    if (!mirrorLocator)
    {
        std::cerr << "错误: 无法创建 SBMirrorLocator\n";
        return 1;
    }
    SBMirrorLocatorLoadLine(mirrorLocator.get(), line.get(),
                            const_cast<char *>(bidiText.c_str()));

    std::cout << "========== 镜像字符 ==========\n";
    bool foundMirror = false;
    while (SBMirrorLocatorMoveNext(mirrorLocator.get()))
    {
        const SBMirrorAgent *agent = SBMirrorLocatorGetAgent(mirrorLocator.get());
        auto char_idx_opt = byte_offset_to_char_index(char_start_offsets, agent->index);
        if (char_idx_opt)
        {
            utf8proc_int32_t actual_cp =
                get_codepoint_at_char_index(bidiText, char_start_offsets, *char_idx_opt);
            std::cout << "字符索引: " << *char_idx_opt << "  字节偏移: " << agent->index
                      << "\n";
            std::cout << "  实际字符: ";
            print_char(actual_cp);
            std::cout << " (U+" << std::hex << actual_cp << std::dec << ")\n";
            std::cout << "  镜像字符: ";
            print_char(static_cast<utf8proc_int32_t>(agent->mirror));
            std::cout << " (U+" << std::hex << agent->mirror << std::dec << ")\n";
            foundMirror = true;
        }
        else
        {
            std::cerr << "警告: 无法为字节偏移 " << agent->index
                      << " 找到对应的字符索引\n";
        }
    }
    if (!foundMirror)
        std::cout << "未找到需要镜像的字符。\n";

    return 0;
}