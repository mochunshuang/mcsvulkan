#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <print>
#include <ranges>
#include <string>
#include <vector>
#include <memory>
#include <utf8proc.h>
#include <SheenBidi/SheenBidi.h>

#include "./freetype_face.hpp"

#include <hb.h>
#include <hb-ft.h>
#include <hb-ot.h>

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

// ========== 归一化结果 ==========
struct NormalizedText
{
    std::unique_ptr<char[]> utf8_buf;
    size_t utf8_len;
    std::vector<uint32_t> codepoints;
};

NormalizedText normalize_to_nfc(const std::string &raw_utf8)
{
    NormalizedText result;
    utf8proc_uint8_t *raw =
        utf8proc_NFC(reinterpret_cast<const utf8proc_uint8_t *>(raw_utf8.c_str()));
    if (!raw)
        throw std::runtime_error("UTF-8 NFC normalization failed");
    result.utf8_buf.reset(reinterpret_cast<char *>(raw));
    result.utf8_len = std::char_traits<char>::length(result.utf8_buf.get());

    const uint8_t *p = reinterpret_cast<const uint8_t *>(result.utf8_buf.get());
    const uint8_t *end = p + result.utf8_len;
    while (p < end)
    {
        utf8proc_int32_t cp;
        utf8proc_ssize_t bytes = utf8proc_iterate(p, end - p, &cp);
        if (bytes < 0)
            break;
        result.codepoints.push_back(static_cast<uint32_t>(cp));
        p += bytes;
    }
    return result;
}

void print_codepoint(uint32_t cp)
{
    if (cp >= 0x20 && cp <= 0x7E)
    {
        std::cout << static_cast<char>(cp);
    }
    else
    {
        std::cout << "U+" << std::hex << cp << std::dec;
    }
}

static constexpr std::string_view codepoint_to_utf8(char32_t cp,
                                                    char (&buf)[4]) noexcept // NOLINT
{
    utf8proc_ssize_t len =
        utf8proc_encode_char(static_cast<utf8proc_int32_t>(cp), // NOLINTNEXTLINE
                             reinterpret_cast<utf8proc_uint8_t *>(buf));
    if (len > 0)
        return {buf, static_cast<size_t>(len)};
    buf[0] = 'E';
    buf[1] = 'O';
    buf[2] = 'R';
    buf[3] = '\0';
    return {buf, 3};
}

int main()
{
    freetype_loader loader{};
    freetype_face face{*loader, "C:/Windows/Fonts/arial.ttf"};
    hb_font_t *font = hb_ft_font_create(*face, nullptr);
    std::vector<uint32_t> shape_index;
    try
    {
        constexpr SBLevel LEVEL_LTR [[maybe_unused]] = 0;
        constexpr SBLevel LEVEL_RTL [[maybe_unused]] = 1;
        constexpr auto BASE_LEVEL = LEVEL_RTL;
        struct expected_data
        {
            std::vector<uint32_t> code;
            std::vector<uint8_t> level;
            std::vector<uint32_t> dispaly_pos;
            std::vector<uint32_t> dispaly_code;
        };
        // NOLINTBEGIN
        expected_data expected_LTR = {
            .code = {0X06CC, 0X06C1, 0X0020, 0X0627, 0X06CC, 0X06A9, 0X0020, 0X0029,
                     0X0063, 0X0061, 0X0072, 0X0028, 0X0020, 0X06C1, 0X06D2, 0X06D4},
            .level = {1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1},
            .dispaly_pos = {5, 4, 3, 2, 1, 0, 6, 7, 8, 9, 10, 11, 12, 15, 14, 13},
            .dispaly_code = {0X06A9, 0X06CC, 0X0627, 0X0020, 0X06C1, 0X06CC, 0X0020,
                             0X0029, 0X0063, 0X0061, 0X0072, 0X0028, 0X0020, 0X06D4,
                             0X06D2, 0X06C1}};
        expected_data expected_RTL = {
            .code = {0X06CC, 0X06C1, 0X0020, 0X0627, 0X06CC, 0X06A9, 0X0020, 0X0029,
                     0X0063, 0X0061, 0X0072, 0X0028, 0X0020, 0X06C1, 0X06D2, 0X06D4},
            .level = {1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1},
            .dispaly_pos = {15, 14, 13, 12, 11, 8, 9, 10, 7, 6, 5, 4, 3, 2, 1, 0},
            .dispaly_code = {0X06D4, 0X06D2, 0X06C1, 0X0020, 0X0028, 0X0063, 0X0061,
                             0X0072, 0X0029, 0X0020, 0X06A9, 0X06CC, 0X0627, 0X0020,
                             0X06C1, 0X06CC}};
        std::u32string expected_LTR_str = U"کیا ہی )car( ۔ےہ";
        // https://util.unicode.org/UnicodeJsps/bidic.jsp?s=%DB%8C%DB%81+%D8%A7%DB%8C%DA%A9+%29car%28+%DB%81%DB%92%DB%94&b=1&u=170&d=2
        std::u32string expected_RTL_str = U"۔ےہ (car) کیا ہی"; // NOTE: 内存序 从左到右
        assert(std::ranges::equal(expected_LTR_str, expected_LTR.dispaly_code));
        assert(expected_LTR.dispaly_code[0] == U"ک"[0]);
        assert(expected_LTR.dispaly_code[1] == U"ی"[0]);
        // NOTE: 并没有重排序
        assert(std::ranges::equal(expected_RTL_str, expected_RTL.dispaly_code));
        const std::string rawText = "یہ ایک )car( ہے۔";

        auto reordered_str = [](const expected_data &data) {
            std::u32string result;
            result.resize(data.dispaly_pos.size());
            for (auto pos : data.dispaly_pos)
                result[pos] = static_cast<char32_t>(data.dispaly_code[pos]);
            return result;
        };
        auto reordered_ltr_str = reordered_str(expected_LTR);
        auto reordered_rtl_str = reordered_str(expected_RTL);
        {
            std::cout << "  reordered_ltr_str: \"";
            for (auto i : reordered_ltr_str)
            {
                char view[4]; // NOLINT
                auto str = codepoint_to_utf8(i, view);
                std::cout << str;
            }
            std::cout << "\"\n";

            std::cout << "  reordered_rtl_str: \"";
            for (auto i : reordered_rtl_str)
            {
                char view[4]; // NOLINT
                auto str = codepoint_to_utf8(i, view);
                std::cout << str;
            }
            std::cout << "\"\n";
        }
        // NOLINTEND

        NormalizedText norm = normalize_to_nfc(rawText);
        std::cout << "归一化后文本: " << norm.utf8_buf.get() << "\n";
        std::cout << "码点数量: " << norm.codepoints.size() << "\n\n";
        if (BASE_LEVEL == LEVEL_LTR)
            assert(std::ranges::equal(norm.codepoints, expected_LTR.code));
        else
            assert(std::ranges::equal(norm.codepoints, expected_RTL.code));

        SBCodepointSequence sequence = {SBStringEncodingUTF32, norm.codepoints.data(),
                                        static_cast<SBUInteger>(norm.codepoints.size())};

        SBAlgorithmPtr algorithm(SBAlgorithmCreate(&sequence));
        if (!algorithm)
            throw std::runtime_error("SBAlgorithmCreate failed");

        // 关键修正：使用 SBLevelAuto 自动检测段落基础方向（官方示例为 RTL）

        SBParagraphPtr paragraph(
            SBAlgorithmCreateParagraph(algorithm.get(), 0, INT32_MAX, BASE_LEVEL));
        if (!paragraph)
            throw std::runtime_error("SBParagraphCreate failed");

        SBUInteger paraLen = SBParagraphGetLength(paragraph.get());
        SBLinePtr line(SBParagraphCreateLine(paragraph.get(), 0, paraLen));
        if (!line)
            throw std::runtime_error("SBLineCreate failed");

        SBUInteger runCount = SBLineGetRunCount(line.get());
        const SBRun *runs = SBLineGetRunsPtr(line.get());

        // 镜像定位器（使用 UTF-32 字符串）
        SBMirrorLocatorPtr mirrorLocator(SBMirrorLocatorCreate());
        if (!mirrorLocator)
            throw std::runtime_error("SBMirrorLocatorCreate failed");
        SBMirrorLocatorLoadLine(mirrorLocator.get(), line.get(),
                                reinterpret_cast<char *>(norm.codepoints.data()));

        std::cout << "========== 镜像字符 ==========\n";
        auto &mirrored_code = norm.codepoints;
        bool foundMirror = false;
        while (SBMirrorLocatorMoveNext(mirrorLocator.get()) != 0)
        {
            const SBMirrorAgent *agent = SBMirrorLocatorGetAgent(mirrorLocator.get());
            SBUInteger charIdx = agent->index;
            if (charIdx < norm.codepoints.size())
            {
                uint32_t actual = norm.codepoints[charIdx];
                uint32_t mirror = agent->mirror;
                std::cout << "字符索引: " << charIdx << "\n";
                std::cout << "  实际字符: ";
                print_codepoint(actual);
                std::cout << " (U+" << std::hex << actual << std::dec << ")\n";
                std::cout << "  镜像字符: ";
                print_codepoint(mirror);
                std::cout << " (U+" << std::hex << mirror << std::dec << ")\n";
                foundMirror = true;

                // NOTE: 不重要。 run 已经解决了
                //  mirrored_code[charIdx] = agent->mirror;
            }
        }
        if (!foundMirror)
            std::cout << "未找到需要镜像的字符。\n";

        std::cout << "========== 双向运行（UTF-32 码点） ==========\n";
        std::vector<uint32_t> visual_pos;
        visual_pos.resize(mirrored_code.size());

        std::vector<uint32_t> visual_code;
        visual_code.resize(mirrored_code.size());
        uint32_t visal_index = 0;
        for (SBUInteger i = 0; i < runCount; ++i)
        {
            const SBRun &run = runs[i];
            SBUInteger start = run.offset;
            SBUInteger end = run.offset + run.length - 1;
            std::cout << "Run " << i << ":\n";
            std::cout << "  码点范围: [" << start << ", " << end
                      << "] 长度=" << run.length << "\n";
            std::cout << "  双向级别: " << static_cast<int>(run.level) << " ("
                      << ((run.level & 1) ? "RTL" : "LTR") << ")\n";
            std::cout << "  字符序列: ";
            for (SBUInteger idx = start; idx <= end; ++idx)
            {
                print_codepoint(mirrored_code[idx]);
                if (idx < end)
                    std::cout << " ";
            }
            std::cout << "\n\n";

            // assert level
            for (SBUInteger idx = start; idx <= end; ++idx)
            {
                if (BASE_LEVEL == LEVEL_LTR)
                    assert(expected_LTR.level[idx] == run.level);
                else
                    assert(expected_RTL.level[idx] == run.level);
            }

            auto run_pos = std::views::iota(run.offset) | std::views::take(run.length);

            // run.level & 1 用于检测最低位：结果为 1 表示奇数（RTL），结果为 0
            // 表示偶数（LTR）。
            bool runleval_is_ltr = (run.level & 1) == 0;
            if (runleval_is_ltr)
            {
                std::cout << "  视觉序列: \"";
                for (auto i : run_pos)
                {
                    char view[4]; // NOLINT
                    auto str = codepoint_to_utf8(mirrored_code[i], view);
                    std::cout << str;

                    visual_pos[visal_index] = i;
                    visual_code[visal_index++] = mirrored_code[i];
                }
                std::cout << "\"\n\n";
            }
            else
            {
                std::cout << "  视觉序列: \"";
                for (auto i : run_pos | std::ranges::views::reverse)
                {
                    char view[4]; // NOLINT
                    auto str = codepoint_to_utf8(mirrored_code[i], view);
                    std::cout << str;

                    visual_pos[visal_index] = i;
                    visual_code[visal_index++] = mirrored_code[i];
                }
                std::cout << "\"\n\n";
            }
            {
                // 2. 整个字符串一次性塑造
                hb_buffer_t *buf = hb_buffer_create();
                hb_buffer_add_utf32(buf, mirrored_code.data() + run.offset, run.length, 0,
                                    run.length);
                hb_buffer_set_direction(buf, HB_DIRECTION_LTR); // 视觉顺序已是 LTR
                hb_buffer_guess_segment_properties(buf);        // 自动检测脚本/语言
                hb_shape(font, buf, nullptr, 0);

                // 3. 获取字形和位置
                unsigned glyph_count;
                hb_glyph_info_t *glyph_info =
                    hb_buffer_get_glyph_infos(buf, &glyph_count);
                // hb_glyph_position_t *glyph_pos =
                //     hb_buffer_get_glyph_positions(buf, &glyph_count);
                std::cout << "cluster" << " (" << ((run.level & 1) ? "RTL" : "LTR")
                          << ") :";
                // 4. 直接绘制：第 i 个字形对应视觉上的第 i 个字符（或连字/分解）
                for (unsigned i = 0; i < glyph_count; ++i)
                {
                    std::cout << " " << glyph_info[i].cluster;
                }
                std::cout << "\n";

                std::cout << "codepoint" << " (" << (runleval_is_ltr ? "RTL" : "LTR")
                          << ") :";
                // 4. 直接绘制：第 i 个字形对应视觉上的第 i 个字符（或连字/分解）
                // 在 shaping 之后
                std::cout << "视觉字符 -> 字形 ID 对应:\n\"";

                if (runleval_is_ltr)
                {
                    size_t run_start_in_visual = run.offset;
                    for (unsigned i = 0; i < glyph_count; ++i)
                    {
                        auto logical_idx = run_start_in_visual + glyph_info[i].cluster;
                        char view[4]; // NOLINT
                        auto str = codepoint_to_utf8(mirrored_code[logical_idx], view);
                        std::cout << str << "[glyph_idx: " << glyph_info[i].codepoint
                                  << "]";
                    }
                    std::cout << "\"\n";
                }
                else
                {
                    size_t run_start_in_visual = run.offset;
                    for (unsigned i = glyph_count; i-- > 0;)
                    {
                        auto logical_idx = run_start_in_visual + glyph_info[i].cluster;
                        char view[4]; // NOLINT
                        auto str = codepoint_to_utf8(mirrored_code[logical_idx], view);
                        std::cout << str << "[glyph_idx: " << glyph_info[i].codepoint
                                  << "]";
                    }
                    std::cout << "\"\n";
                }
                hb_buffer_destroy(buf);
            }
        }

        auto &source_pos = (BASE_LEVEL == LEVEL_LTR) ? expected_LTR.dispaly_pos
                                                     : expected_RTL.dispaly_pos;
        assert(std::ranges::equal(source_pos, visual_pos));
        auto &source_code = (BASE_LEVEL == LEVEL_LTR) ? expected_LTR.dispaly_code
                                                      : expected_RTL.dispaly_code;
        assert(std::ranges::equal(source_code, visual_code));

        {
            std::cout << "  visual_str: \"";
            for (auto i : visual_code)
            {
                char view[4]; // NOLINT
                auto str = codepoint_to_utf8(i, view);
                std::cout << str;
            }
            std::cout << "\"\n";

            auto &reordered_str =
                (BASE_LEVEL == LEVEL_LTR) ? reordered_ltr_str : reordered_rtl_str;
            assert(std::ranges::equal(reordered_str, visual_code));
        }
    }
    catch (const std::exception &ex)
    {
        std::cerr << "错误: " << ex.what() << std::endl;
        return 1;
    }
    hb_font_destroy(font);
    return 0;
}