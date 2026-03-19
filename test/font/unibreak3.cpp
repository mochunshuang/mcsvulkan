#include <algorithm>
#include <bit>
#include <hb.h>
#include <hb-ft.h>
#include <hb-ot.h>
#include <ft2build.h>
#include <string_view>
#include FT_FREETYPE_H
#include <utf8proc.h>
#include <SheenBidi/SheenBidi.h>
#include <SheenBidi/SBScript.h>
#include <SheenBidi/SBAlgorithm.h>

#include <linebreak.h>   // libunibreak 核心头文件
#include <unibreakdef.h> // 基础定义

// 在文件开头添加 ICU 头文件（请确保已安装 ICU 库）
#include <unicode/brkiter.h>
#include <unicode/ustring.h>

#include <filesystem>
#include <vector>
#include <string>
#include <unordered_map>
#include <set>
#include <array>
#include <span>
#include <print>
#include <algorithm>
#include <cstdint>
#include <sstream>

namespace fs = std::filesystem;

// -----------------------------------------------------------------------------
// 字体信息结构（不变）
// -----------------------------------------------------------------------------
struct FontInfo
{
    std::string file_path;
    std::string family_name;
    hb_set_t *unicode_set;
    std::set<hb_script_t> scripts;
    std::set<hb_tag_t> lang_tags;

    FontInfo() : unicode_set(hb_set_create()) {}
    ~FontInfo()
    {
        hb_set_destroy(unicode_set);
    }
    FontInfo(const FontInfo &) = delete;
    FontInfo &operator=(const FontInfo &) = delete;
    FontInfo(FontInfo &&other) noexcept
        : file_path(std::move(other.file_path)),
          family_name(std::move(other.family_name)), unicode_set(other.unicode_set),
          scripts(std::move(other.scripts)), lang_tags(std::move(other.lang_tags))
    {
        other.unicode_set = nullptr;
    }
    FontInfo &operator=(FontInfo &&other) noexcept
    {
        if (this != &other)
        {
            file_path = std::move(other.file_path);
            family_name = std::move(other.family_name);
            std::swap(unicode_set, other.unicode_set);
            scripts = std::move(other.scripts);
            lang_tags = std::move(other.lang_tags);
        }
        return *this;
    }
};

// -----------------------------------------------------------------------------
// 字体扫描器（不变）
// -----------------------------------------------------------------------------
class FontScanner
{
  public:
    static std::vector<FontInfo> scan_directories(
        std::span<const std::string> directories)
    {
        std::vector<FontInfo> fonts;
        FT_Library ft_library;
        if (FT_Init_FreeType(&ft_library) != FT_Err_Ok)
        {
            std::println(stderr, "FT_Init_FreeType failed");
            return fonts;
        }

        for (const auto &dir : directories)
        {
            if (!fs::exists(dir) || !fs::is_directory(dir))
                continue;
            for (const auto &entry : fs::recursive_directory_iterator(dir))
            {
                if (!entry.is_regular_file())
                    continue;
                auto ext = entry.path().extension().string();
                if (ext != ".ttf" && ext != ".ttc" && ext != ".otf" && ext != ".otc")
                    continue;

                int face_index = 0;
                while (true)
                {
                    FT_Face ft_face;
                    if (FT_New_Face(ft_library, entry.path().string().c_str(), face_index,
                                    &ft_face) != FT_Err_Ok)
                        break;

                    hb_face_t *hb_face = hb_ft_face_create(ft_face, nullptr);
                    if (hb_face)
                    {
                        FontInfo info;
                        info.file_path = entry.path().string();
                        info.family_name =
                            ft_face->family_name ? ft_face->family_name : "";

                        hb_tag_t tables[] = {HB_OT_TAG_GSUB, HB_OT_TAG_GPOS};
                        for (hb_tag_t table : tables)
                        {
                            unsigned script_count = hb_ot_layout_table_get_script_tags(
                                hb_face, table, 0, nullptr, nullptr);
                            std::vector<hb_tag_t> script_tags(script_count);
                            hb_ot_layout_table_get_script_tags(
                                hb_face, table, 0, &script_count, script_tags.data());

                            for (hb_tag_t tag : script_tags)
                            {
                                hb_script_t script = hb_ot_tag_to_script(tag);
                                if (script != HB_SCRIPT_INVALID)
                                    info.scripts.insert(script);
                            }

                            for (unsigned i = 0; i < script_count; ++i)
                            {
                                unsigned lang_count =
                                    hb_ot_layout_script_get_language_tags(
                                        hb_face, table, i, 0, nullptr, nullptr);
                                std::vector<hb_tag_t> lang_tags(lang_count);
                                hb_ot_layout_script_get_language_tags(
                                    hb_face, table, i, 0, &lang_count, lang_tags.data());
                                info.lang_tags.insert(lang_tags.begin(), lang_tags.end());
                            }
                        }

                        hb_face_collect_unicodes(hb_face, info.unicode_set);

                        fonts.push_back(std::move(info));
                        hb_face_destroy(hb_face);
                    }
                    FT_Done_Face(ft_face);
                    ++face_index;
                }
            }
        }

        FT_Done_FreeType(ft_library);
        return fonts;
    }
};

// -----------------------------------------------------------------------------
// 字体匹配器（添加 const 支持）
// -----------------------------------------------------------------------------
class FontMatcher
{
  public:
    explicit FontMatcher(std::vector<FontInfo> fonts)
        : fonts_(std::move(fonts)), ufuncs_(hb_unicode_funcs_get_default())
    {
    }

    void set_preferred_language(const std::string &bcp47)
    {
        preferred_lang_tag_ = language_to_ot_tag(bcp47);
    }

    hb_tag_t preferred_lang_tag() const
    {
        return preferred_lang_tag_;
    }

    hb_script_t get_script(uint32_t codepoint) const
    {
        auto it = script_cache_.find(codepoint);
        if (it != script_cache_.end())
            return it->second;
        hb_script_t script = hb_unicode_script(ufuncs_, codepoint);
        script_cache_[codepoint] = script;
        return script;
    }

    const FontInfo *select_font(uint32_t codepoint) const
    {
        hb_script_t script = get_script(codepoint);

        if (preferred_lang_tag_ != HB_TAG_NONE)
        {
            for (const auto &font : fonts_)
            {
                if (hb_set_has(font.unicode_set, codepoint) &&
                    font.lang_tags.count(preferred_lang_tag_) != 0)
                {
                    return &font;
                }
            }
        }

        for (const auto &font : fonts_)
        {
            if (hb_set_has(font.unicode_set, codepoint) &&
                font.scripts.count(script) != 0)
            {
                return &font;
            }
        }

        for (const auto &font : fonts_)
        {
            if (hb_set_has(font.unicode_set, codepoint))
            {
                return &font;
            }
        }

        return nullptr;
    }

    const std::vector<FontInfo> &get_fonts() const
    {
        return fonts_;
    }

  private:
    std::vector<FontInfo> fonts_;
    hb_unicode_funcs_t *ufuncs_;
    hb_tag_t preferred_lang_tag_ = HB_TAG_NONE;
    mutable std::unordered_map<uint32_t, hb_script_t> script_cache_;

    static hb_tag_t language_to_ot_tag(const std::string &bcp47)
    {
        static const std::unordered_map<std::string, hb_tag_t> map = {
            {"zh-CN", HB_TAG('Z', 'H', 'S', ' ')},
            {"zh-Hans", HB_TAG('Z', 'H', 'S', ' ')},
            {"zh-TW", HB_TAG('Z', 'H', 'T', ' ')},
            {"zh-Hant", HB_TAG('Z', 'H', 'T', ' ')},
            {"zh-HK", HB_TAG('Z', 'H', 'H', ' ')},
            {"ja", HB_TAG('J', 'A', 'N', ' ')},
            {"ko", HB_TAG('K', 'O', 'R', ' ')},
            {"en", HB_TAG('E', 'N', 'G', ' ')},
            {"fr", HB_TAG('F', 'R', 'A', ' ')},
            {"de", HB_TAG('D', 'E', 'U', ' ')},
            {"it", HB_TAG('I', 'T', 'A', ' ')},
            {"es", HB_TAG('S', 'P', 'A', ' ')},
            {"pt", HB_TAG('P', 'T', 'G', ' ')},
            {"nl", HB_TAG('N', 'L', 'D', ' ')},
            {"sv", HB_TAG('S', 'V', 'E', ' ')},
            {"ru", HB_TAG('R', 'U', 'S', ' ')},
            {"uk", HB_TAG('U', 'K', 'R', ' ')},
            {"pl", HB_TAG('P', 'O', 'L', ' ')},
            {"cs", HB_TAG('C', 'S', 'Y', ' ')},
            {"ar", HB_TAG('A', 'R', 'A', ' ')},
            {"he", HB_TAG('H', 'E', 'B', ' ')},
            {"fa", HB_TAG('F', 'A', 'R', ' ')},
            {"ur", HB_TAG('U', 'R', 'D', ' ')},
            {"hi", HB_TAG('H', 'I', 'N', ' ')},
            {"bn", HB_TAG('B', 'E', 'N', ' ')},
            {"ta", HB_TAG('T', 'A', 'M', ' ')},
            {"te", HB_TAG('T', 'E', 'L', ' ')},
            {"th", HB_TAG('T', 'H', 'A', ' ')},
            {"lo", HB_TAG('L', 'A', 'O', ' ')},
            {"my", HB_TAG('M', 'Y', 'A', ' ')},
            {"km", HB_TAG('K', 'H', 'M', ' ')},
            {"vi", HB_TAG('V', 'I', 'E', ' ')},
            {"am", HB_TAG('A', 'M', 'H', ' ')},
            {"sw", HB_TAG('S', 'W', 'A', ' ')},
            {"el", HB_TAG('G', 'R', 'E', ' ')},
            {"hy", HB_TAG('H', 'Y', ' ', ' ')}};
        auto it = map.find(bcp47);
        return it != map.end() ? it->second : HB_TAG_NONE;
    }
};

// -----------------------------------------------------------------------------
// 辅助函数
// -----------------------------------------------------------------------------
std::string tag_to_string(hb_tag_t tag)
{
    char buf[5] = {0};
    hb_tag_to_string(tag, buf);
    return {buf};
}
std::string script_to_string(hb_script_t script)
{
    hb_tag_t tag = hb_script_to_iso15924_tag(script);
    if (tag == HB_TAG_NONE)
        return "Invalid";
    return tag_to_string(tag);
}

std::string cp_to_utf8(char32_t cp)
{
    char buf[8] = {0};
    utf8proc_ssize_t len = utf8proc_encode_char(
        static_cast<utf8proc_int32_t>(cp), reinterpret_cast<utf8proc_uint8_t *>(buf));
    return (len > 0) ? std::string(buf) : "?";
}

std::string cp_to_escaped(char32_t cp)
{
    if (cp < 0x20 || cp == 0x7F)
    {
        static const char *c0_names[] = {
            "NUL", "SOH", "STX", "ETX", "EOT", "ENQ", "ACK", "BEL", "BS",  "HT",  "LF",
            "VT",  "FF",  "CR",  "SO",  "SI",  "DLE", "DC1", "DC2", "DC3", "DC4", "NAK",
            "SYN", "ETB", "CAN", "EM",  "SUB", "ESC", "FS",  "GS",  "RS",  "US"};
        if (cp < 0x20)
            return std::string("\\") + c0_names[cp];
        else
            return "\\DEL";
    }
    if (cp >= 0x80 && cp <= 0x9F)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "\\u%04X", static_cast<unsigned>(cp));
        return buf;
    }
    switch (cp)
    {
    case 0x200B:
        return "\\u200B";
    case 0x200C:
        return "\\u200C";
    case 0x200D:
        return "\\u200D";
    case 0x2066:
        return "\\u2066";
    case 0x2067:
        return "\\u2067";
    case 0x2068:
        return "\\u2068";
    case 0x2069:
        return "\\u2069";
    case 0x202A:
        return "\\u202A";
    case 0x202B:
        return "\\u202B";
    case 0x202C:
        return "\\u202C";
    case 0x202D:
        return "\\u202D";
    case 0x202E:
        return "\\u202E";
    case 0x061C:
        return "\\u061C";
    }
    return cp_to_utf8(cp);
}

// -----------------------------------------------------------------------------
// 阶段数据结构定义
// -----------------------------------------------------------------------------

// 阶段1输出
struct NormalizedText
{
    std::string utf8;                 // NFC 后的 UTF-8 字符串
    std::vector<size_t> byte_offsets; // 每个字符的起始字节偏移（用于 BIDI 定位）
    std::vector<uint32_t> codepoints; // 解码后的 UTF-32 码点
};

// 阶段2输出
struct BidiRun
{
    size_t logical_start;
    size_t logical_end; // exclusive
    uint8_t level;
    hb_direction_t direction() const
    {
        return (level % 2) ? HB_DIRECTION_RTL : HB_DIRECTION_LTR;
    }
};

struct BidiResult
{
    std::vector<uint32_t> codepoints;          // 逻辑顺序码点
    std::vector<uint32_t> original_codepoints; // 原始码点（这里与 codepoints 相同）
    std::vector<size_t> byte_offsets;          // 每个字符在 UTF-8 中的起始字节偏移
    std::vector<uint8_t> bidi_levels;          // 每个字符的嵌入级别（尚未填充，可留空）
    std::vector<uint32_t> mirror;              // 镜像码点（0 表示无镜像）
    std::vector<bool> is_mirrored;             // 是否应用镜像
    std::vector<BidiRun> bidi_runs;            // 方向运行（逻辑区间）
    std::vector<hb_script_t> hb_scripts; // 每个字符的 HarfBuzz 脚本（用于打印和划分）
    std::string visual_string;           // 视觉顺序字符串（用于原始打印）
};

// 阶段3输出
struct ScriptRun
{
    size_t logical_start;
    size_t logical_end;
    hb_script_t script;
    hb_direction_t direction; // 继承自所属 BidiRun
};

// 阶段4输出
struct ShapingRun
{
    size_t logical_start;
    size_t logical_end;
    hb_script_t script;
    hb_direction_t direction;
    const FontInfo *font;   // 指向字体信息（全局字体库中的对象）
    hb_language_t language; // 语言标签（用于 HarfBuzz）
    std::string font_name;  // 仅用于打印
};

// 阶段5输出（暂为空）
struct BreakResult
{
    std::vector<bool> break_after;
};

// 阶段6输出（暂为空）
struct ShapedGlyph
{
    unsigned int glyph_id;
    unsigned int cluster;
    double x_advance, y_advance;
    double x_offset, y_offset;
};
struct ShapedSegment
{
    size_t logical_start;
    size_t logical_end;
    std::vector<ShapedGlyph> glyphs;
};

// 阶段7输出
struct Line
{
    std::vector<ShapedGlyph> glyphs;
    double width;
};

// 将 BCP47 语言代码转换为 libunibreak 能识别的代码
static std::string bcp47_to_libunibreak_lang(const std::string &bcp47)
{
    // 提取主要语言部分（如 "zh-CN" -> "zh"）
    std::string lang = bcp47;
    size_t dash = lang.find('-');
    if (dash != std::string::npos)
    {
        lang = lang.substr(0, dash);
    }
    // libunibreak 支持的语言列表（根据其文档）
    static const std::set<std::string> supported = {"en", "de", "es", "fr", "ru", "zh"};
    if (supported.count(lang))
    {
        return lang;
    }
    return ""; // 使用默认规则
}

// -----------------------------------------------------------------------------
// 阶段函数声明
// -----------------------------------------------------------------------------

NormalizedText normalize(const char8_t *text);
BidiResult analyze_bidi(const NormalizedText &norm, const FontMatcher &matcher);
std::vector<ScriptRun> segment_scripts(const BidiResult &bidi);
std::vector<ShapingRun> assign_fonts(const std::vector<ScriptRun> &script_runs,
                                     const std::vector<uint32_t> &codepoints,
                                     const FontMatcher &matcher,
                                     hb_tag_t preferred_lang_tag);
BreakResult analyze_line_breaks(const std::string &utf8,
                                const std::vector<size_t> &byte_offsets,
                                const std::string &lang_bcp47);
std::vector<ShapedSegment> shape_all(const std::vector<ShapingRun> &shaping_runs,
                                     const std::vector<uint32_t> &codepoints,
                                     FT_Library ft_lib);
std::vector<Line> layout_lines(const std::vector<ShapedSegment> &shaped_segments,
                               const BreakResult &breaks, double line_width);

// 打印函数
void print_normalized(const NormalizedText &norm, size_t index, const char8_t *raw_text);
void print_bidi(const BidiResult &bidi);
void print_script_runs(const std::vector<ScriptRun> &runs,
                       const std::vector<uint32_t> &codepoints);
void print_shaping_runs(const std::vector<ShapingRun> &runs,
                        const std::vector<uint32_t> &codepoints);
void print_breaks(const BreakResult &breaks, const std::vector<uint32_t> &codepoints);
void print_shaped_segments(const std::vector<ShapedSegment> &segments);
void print_lines(const std::vector<Line> &lines);

// 原始表格打印（模仿 process_text2 的输出）
void print_original_analysis(const BidiResult &bidi,
                             const std::vector<std::string> &font_names,
                             const std::vector<bool> &lang_match,
                             const std::vector<bool> &script_match, size_t index,
                             const char8_t *raw_text);

// -----------------------------------------------------------------------------
// 阶段1实现
// -----------------------------------------------------------------------------
NormalizedText normalize(const char8_t *text)
{
    NormalizedText result;
    auto normalized =
        std::unique_ptr<utf8proc_uint8_t, decltype([](auto p) { free(p); })>(
            utf8proc_NFC(reinterpret_cast<const utf8proc_uint8_t *>(text)));
    if (!normalized)
    {
        std::println(stderr, "Normalization failed");
        return result;
    }
    result.utf8 = reinterpret_cast<const char *>(normalized.get());

    // 解码为 UTF-32 并记录字节偏移
    const unsigned char *p = reinterpret_cast<const unsigned char *>(result.utf8.data());
    const unsigned char *end = p + result.utf8.size();
    result.byte_offsets.clear();
    result.codepoints.clear();
    while (p < end)
    {
        result.byte_offsets.push_back(
            p - reinterpret_cast<const unsigned char *>(result.utf8.data()));
        utf8proc_int32_t cp;
        utf8proc_ssize_t bytes = utf8proc_iterate(p, end - p, &cp);
        if (bytes < 0)
            break;
        result.codepoints.push_back(static_cast<uint32_t>(cp));
        p += bytes;
    }
    return result;
}

// -----------------------------------------------------------------------------
// 阶段2实现
// -----------------------------------------------------------------------------
BidiResult analyze_bidi(const NormalizedText &norm, const FontMatcher &matcher)
{
    BidiResult result;
    result.codepoints = norm.codepoints;
    result.original_codepoints = norm.codepoints;
    result.byte_offsets = norm.byte_offsets;
    size_t char_count = result.codepoints.size();

    result.bidi_levels.assign(char_count, 0);
    result.mirror.assign(char_count, 0);
    result.is_mirrored.assign(char_count, false);
    result.hb_scripts.resize(char_count);
    for (size_t i = 0; i < char_count; ++i)
    {
        result.hb_scripts[i] = matcher.get_script(result.codepoints[i]);
    }

    SBCodepointSequence seq;
    seq.stringEncoding = SBStringEncodingUTF32;
    seq.stringBuffer = const_cast<SBCodepoint *>(result.codepoints.data());
    seq.stringLength = result.codepoints.size();

    SBAlgorithmRef algo = SBAlgorithmCreate(&seq);
    std::vector<uint32_t> visual_cp;

    SBUInteger cp_offset = 0;
    while (cp_offset < char_count)
    {
        SBUInteger actualLength, separatorLength;
        SBAlgorithmGetParagraphBoundary(algo, cp_offset, char_count - cp_offset,
                                        &actualLength, &separatorLength);

        // --- 处理段落内容（actualLength > 0）---
        if (actualLength > 0)
        {
            SBParagraphRef para = SBAlgorithmCreateParagraph(
                algo, cp_offset, actualLength, SBLevelDefaultLTR);
            if (para)
            {
                SBUInteger para_cp_len = SBParagraphGetLength(para);
                SBLineRef line = SBParagraphCreateLine(para, 0, para_cp_len);
                if (line)
                {
                    // 镜像处理
                    SBMirrorLocatorRef mirrorLocator = SBMirrorLocatorCreate();
                    SBMirrorLocatorLoadLine(mirrorLocator, line, seq.stringBuffer);
                    const SBMirrorAgent *agent = SBMirrorLocatorGetAgent(mirrorLocator);
                    while (SBMirrorLocatorMoveNext(mirrorLocator))
                    {
                        size_t idx = agent->index;
                        if (idx < char_count)
                        {
                            result.mirror[idx] = static_cast<uint32_t>(agent->mirror);
                            result.is_mirrored[idx] = (result.mirror[idx] != 0);
                        }
                    }
                    SBMirrorLocatorRelease(mirrorLocator);

                    // 获取运行
                    SBUInteger runCount = SBLineGetRunCount(line);
                    const SBRun *runs = SBLineGetRunsPtr(line);

                    // 记录运行
                    for (SBUInteger i = 0; i < runCount; ++i)
                    {
                        const SBRun &run = runs[i];
                        size_t start = cp_offset + run.offset;
                        size_t end = cp_offset + run.offset + run.length;
                        result.bidi_runs.push_back({start, end, run.level});
                        for (size_t j = start; j < end; ++j)
                        {
                            result.bidi_levels[j] = run.level;
                        }
                    }

                    // 构建视觉顺序（如果运行数为0，则手动添加）
                    if (runCount == 0)
                    {
                        // 罕见情况：行中没有运行，则按逻辑顺序添加字符（视为 LTR）
                        for (SBUInteger i = 0; i < para_cp_len; ++i)
                        {
                            size_t idx = cp_offset + i;
                            visual_cp.push_back(result.codepoints[idx]);
                        }
                    }
                    else
                    {
                        for (SBUInteger i = 0; i < runCount; ++i)
                        {
                            const SBRun &run = runs[i];
                            size_t start = cp_offset + run.offset;
                            size_t end = cp_offset + run.offset + run.length;
                            if (run.level % 2)
                            { // RTL
                                for (size_t j = end; j > start; --j)
                                {
                                    size_t idx = j - 1;
                                    uint32_t cp = result.mirror[idx]
                                                      ? result.mirror[idx]
                                                      : result.codepoints[idx];
                                    visual_cp.push_back(cp);
                                }
                            }
                            else
                            { // LTR
                                for (size_t j = start; j < end; ++j)
                                {
                                    uint32_t cp = result.mirror[j] ? result.mirror[j]
                                                                   : result.codepoints[j];
                                    visual_cp.push_back(cp);
                                }
                            }
                        }
                    }

                    SBLineRelease(line);
                }
                else
                {
                    // line 创建失败：手动添加所有字符（视为 LTR）
                    for (SBUInteger i = 0; i < para_cp_len; ++i)
                    {
                        size_t idx = cp_offset + i;
                        visual_cp.push_back(result.codepoints[idx]);
                        result.bidi_runs.push_back({idx, idx + 1, 0});
                        result.bidi_levels[idx] = 0;
                    }
                }
                SBParagraphRelease(para);
            }
            else
            {
                // paragraph 创建失败：手动添加所有字符（视为 LTR）
                for (SBUInteger i = 0; i < actualLength; ++i)
                {
                    size_t idx = cp_offset + i;
                    visual_cp.push_back(result.codepoints[idx]);
                    result.bidi_runs.push_back({idx, idx + 1, 0});
                    result.bidi_levels[idx] = 0;
                }
            }
        }

        // --- 处理分隔符字符（separatorLength > 0）---
        for (SBUInteger i = 0; i < separatorLength; ++i)
        {
            size_t idx = cp_offset + actualLength + i;
            uint32_t cp =
                result.mirror[idx] ? result.mirror[idx] : result.codepoints[idx];
            visual_cp.push_back(cp);
            result.bidi_runs.push_back({idx, idx + 1, 0});
            result.bidi_levels[idx] = 0;
        }

        // --- 更新偏移量 ---
        cp_offset += actualLength + separatorLength;
    }

    SBAlgorithmRelease(algo);

    // 将视觉码点转换为 UTF-8
    result.visual_string.clear();
    for (uint32_t cp : visual_cp)
    {
        result.visual_string += cp_to_utf8(cp);
    }

    return result;
}
// -----------------------------------------------------------------------------
// 阶段3实现
// -----------------------------------------------------------------------------
std::vector<ScriptRun> segment_scripts(const BidiResult &bidi)
{
    std::vector<ScriptRun> result;
    for (const auto &run : bidi.bidi_runs)
    {
        size_t start = run.logical_start;
        while (start < run.logical_end)
        {
            hb_script_t current = bidi.hb_scripts[start];
            if (current == HB_SCRIPT_INVALID)
                current = HB_SCRIPT_LATIN;
            size_t end = start + 1;
            while (end < run.logical_end && bidi.hb_scripts[end] == current)
                ++end;
            result.push_back({start, end, current, run.direction()});
            start = end;
        }
    }
    return result;
}

// -----------------------------------------------------------------------------
// 阶段4实现
// -----------------------------------------------------------------------------
std::vector<ShapingRun> assign_fonts(const std::vector<ScriptRun> &script_runs,
                                     const std::vector<uint32_t> &codepoints,
                                     const FontMatcher &matcher)
{
    std::vector<ShapingRun> result;
    for (const auto &run : script_runs)
    {
        // 使用段内第一个字符选择字体
        uint32_t first_cp = codepoints[run.logical_start];
        const FontInfo *font = matcher.select_font(first_cp);
        std::string font_name =
            font ? fs::path(font->file_path).filename().string() : "NOT_FOUND";

        // 推断语言（简单映射）
        hb_language_t lang = nullptr;
        if (run.script == HB_SCRIPT_ARABIC)
            lang = hb_language_from_string("ar", -1);
        else if (run.script == HB_SCRIPT_HEBREW)
            lang = hb_language_from_string("he", -1);
        else if (run.script == HB_SCRIPT_HAN)
            lang = hb_language_from_string("zh", -1);
        else if (run.script == HB_SCRIPT_CYRILLIC)
            lang = hb_language_from_string("ru", -1);
        else
            lang = hb_language_from_string("en", -1);
        if (!lang)
            lang = hb_language_get_default();

        result.push_back({run.logical_start, run.logical_end, run.script, run.direction,
                          font, lang, font_name});
    }
    return result;
}

// -----------------------------------------------------------------------------
// 阶段5-7留空实现
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// 阶段5实现（使用 ICU UAX #14）
// -----------------------------------------------------------------------------
BreakResult analyze_line_breaks(const std::string &utf8,
                                const std::vector<size_t> &byte_offsets,
                                const std::string &lang_bcp47)
{
    BreakResult result;
    size_t char_count = byte_offsets.size();
    result.break_after.assign(char_count, false);
    if (char_count == 0)
        return result;

    UErrorCode status = U_ZERO_ERROR;
    const char *locale_name = lang_bcp47.empty() ? "en" : lang_bcp47.c_str();

    // 1. 将 UTF-8 转换为 UTF-16，并建立 UTF-16 到字符索引的映射
    std::vector<UChar> u16_buffer;
    std::vector<int32_t> utf16_to_char; // 每个 UTF-16 代码单元对应的字符索引

    for (size_t i = 0; i < char_count; ++i)
    {
        // 获取当前字符的 UTF-8 子串
        size_t start_byte = byte_offsets[i];
        size_t end_byte = (i + 1 < char_count) ? byte_offsets[i + 1] : utf8.size();
        std::string_view char_utf8 =
            std::string_view(utf8).substr(start_byte, end_byte - start_byte);

        // 将单个字符转换为 UTF-16
        int32_t u16_len = 0;
        u_strFromUTF8(nullptr, 0, &u16_len,
                      reinterpret_cast<const char *>(char_utf8.data()),
                      static_cast<int32_t>(char_utf8.size()), &status);
        if (status != U_BUFFER_OVERFLOW_ERROR)
        {
            std::println(stderr,
                         "ICU: u_strFromUTF8 length calculation failed for character {}",
                         i);
            return result;
        }
        status = U_ZERO_ERROR;
        std::vector<UChar> char_u16(u16_len);
        u_strFromUTF8(char_u16.data(), u16_len, &u16_len,
                      reinterpret_cast<const char *>(char_utf8.data()),
                      static_cast<int32_t>(char_utf8.size()), &status);
        if (U_FAILURE(status))
        {
            std::println(stderr, "ICU: u_strFromUTF8 conversion failed for character {}",
                         i);
            return result;
        }

        // 追加到总 UTF-16 缓冲区，并填充映射表
        for (UChar c : char_u16)
        {
            u16_buffer.push_back(c);
            utf16_to_char.push_back(static_cast<int32_t>(i));
        }
    }

    // 2. 创建 ICU 换行迭代器
    icu::Locale locale(locale_name);
    icu::BreakIterator *brk = icu::BreakIterator::createLineInstance(locale, status);
    if (U_FAILURE(status))
    {
        std::println(stderr, "ICU: createLineInstance failed: {}", u_errorName(status));
        delete brk;
        return result;
    }

    icu::UnicodeString uText(false, u16_buffer.data(),
                             static_cast<int32_t>(u16_buffer.size()));
    brk->setText(uText);

    // 3. 获取所有断点位置并映射到字符索引
    int32_t pos = brk->first();
    while (pos != icu::BreakIterator::DONE)
    {
        if (pos > 0)
        {
            int32_t char_idx = utf16_to_char[pos - 1];
            result.break_after[char_idx] = true;
        }
        pos = brk->next();
    }

    delete brk;
    return result;
}

std::vector<ShapedSegment> shape_all(const std::vector<ShapingRun> &shaping_runs,
                                     const std::vector<uint32_t> &codepoints,
                                     FT_Library ft_lib)
{
    std::vector<ShapedSegment> result;
    return result;
}

std::vector<Line> layout_lines(const std::vector<ShapedSegment> &shaped_segments,
                               const BreakResult &breaks, double line_width)
{
    std::vector<Line> result;
    return result;
}

// -----------------------------------------------------------------------------
// 打印函数实现
// -----------------------------------------------------------------------------
void print_normalized(const NormalizedText &norm, size_t index, const char8_t *raw_text)
{
    std::println("--- Stage 1: Normalization (UAX #15) ---");
    std::println("Original: {}", reinterpret_cast<const char *>(raw_text));
    std::println("Normalized (NFC): {}", norm.utf8);
}

void print_bidi(const BidiResult &bidi)
{
    std::println("--- Stage 2: Bidi Analysis (UAX #9) ---");
    std::println("Character count: {}", bidi.codepoints.size());
    std::println("{:<4} {:<10} {:<10} {:<10} {:<6} {:<8}", "Idx", "U+", "Orig", "Script",
                 "Level", "Mirror");
    for (size_t i = 0; i < bidi.codepoints.size(); ++i)
    {
        std::println("{:<4} U+{:06X}  {:<10} {:<10} {:<6} {}", i,
                     static_cast<unsigned>(bidi.codepoints[i]),
                     cp_to_escaped(bidi.original_codepoints[i]),
                     script_to_string(bidi.hb_scripts[i]),
                     bidi.bidi_levels.empty() ? 0 : bidi.bidi_levels[i],
                     bidi.is_mirrored[i] ? "Yes" : "No");
    }
    std::println("Bidi Runs (logical):");
    for (const auto &run : bidi.bidi_runs)
    {
        std::println("  [{}, {}) direction {}", run.logical_start, run.logical_end,
                     (run.level % 2) ? "RTL" : "LTR");
    }
}

void print_script_runs(const std::vector<ScriptRun> &runs,
                       const std::vector<uint32_t> &codepoints)
{
    std::println("--- Stage 3: Script Segmentation (UAX #24) ---");
    std::println("Script runs (logical):");
    for (const auto &run : runs)
    {
        std::string script_str = script_to_string(run.script);
        std::println("  [{}, {}) script {} direction {}", run.logical_start,
                     run.logical_end, script_str,
                     (run.direction == HB_DIRECTION_RTL) ? "RTL" : "LTR");
    }
}

void print_shaping_runs(const std::vector<ShapingRun> &runs,
                        const std::vector<uint32_t> &codepoints)
{
    std::println("--- Stage 4: Font Selection ---");
    std::println("Shaping runs (logical):");
    for (const auto &run : runs)
    {
        std::string script_str = script_to_string(run.script);
        std::println("  [{}, {}) script {} direction {} font {} language {}",
                     run.logical_start, run.logical_end, script_str,
                     (run.direction == HB_DIRECTION_RTL) ? "RTL" : "LTR", run.font_name,
                     hb_language_to_string(run.language));
    }
}

void print_breaks(const BreakResult &breaks, const std::vector<uint32_t> &codepoints)
{
    std::println("--- Stage 5: Line Breaking (UAX #14+29) ---");
    std::println("Break opportunities (after each character):");

    // 输出字符序列（方便对照）
    for (size_t i = 0; i < codepoints.size(); ++i)
    {
        std::print("{}", cp_to_utf8(codepoints[i]));
    }
    std::println("");

    // 输出断点标记（'|' 表示允许换行）
    for (size_t i = 0; i < codepoints.size(); ++i)
    {
        std::print("{}", breaks.break_after[i] ? "|" : " ");
    }
    std::println("");
}

void print_shaped_segments(const std::vector<ShapedSegment> &segments)
{
    std::println("--- Stage 6: Shaping (OpenType) ---");
    std::println("(Not implemented yet)");
}

void print_lines(const std::vector<Line> &lines)
{
    std::println("--- Stage 7: Line Layout ---");
    std::println("(Not implemented yet)");
}

// 原始表格打印（完全模仿 process_text2 的 print_analysis）
void print_original_analysis(const BidiResult &bidi,
                             const std::vector<std::string> &font_names,
                             const std::vector<bool> &lang_match,
                             const std::vector<bool> &script_match, size_t index,
                             const char8_t *raw_text)
{
    std::println("\n=== Test case {} ===", index);
    std::println("Original (logical): {}", reinterpret_cast<const char *>(raw_text));
    std::println("Visual string: \"{}\"", bidi.visual_string);

    if (bidi.codepoints.empty())
    {
        std::println("Empty string, skipped.");
        return;
    }

    std::println("\nFont selection (logical order):");
    std::println("{:<4} {:<10} {:<12} {:<20} {:<10} {}", "Idx", "U+", "Char", "Font",
                 "LangMatch", "ScriptMatch");
    for (size_t i = 0; i < bidi.codepoints.size(); ++i)
    {
        std::println("{:<4} U+{:06X}  {:<10} {:<20} {:<10} {}", i,
                     static_cast<unsigned>(bidi.codepoints[i]),
                     cp_to_escaped(bidi.codepoints[i]), font_names[i],
                     lang_match[i] ? "Yes" : "No", script_match[i] ? "Yes" : "No");
    }

    std::println("\nMirror status (logical order):");
    std::println("{:<4} {:<10} {:<10} {:<10} {:<10}", "Idx", "Orig", "Mirror", "Char",
                 "Mirrored?");
    for (size_t i = 0; i < bidi.codepoints.size(); ++i)
    {
        uint32_t orig = bidi.codepoints[i];
        uint32_t mirr = bidi.mirror[i];
        std::string orig_char = cp_to_escaped(orig);
        std::string mirr_char = mirr ? cp_to_escaped(mirr) : "";
        std::println("{:<4} U+{:06X} {:<10} U+{:06X} {:<10} {:<6}", i, orig, orig_char,
                     mirr, mirr_char, mirr ? "Yes" : "No");
    }
}

/*

[原始文本 UTF-8]
    ↓
1. 正规化 (UAX #15) ─→ 规范化的 UTF-8 序列
    ↓
2. 双向分析 (UAX #9) ─→ 逻辑顺序字符信息 + 镜像映射 + 方向运行
    ↓
3. 脚本划分 (UAX #24) ─→ 脚本段（逻辑区间 + 脚本 + 方向）
    ↓
4. 字体选择 ─→ 每个脚本段绑定字体 + 语言标签
    ↓
5. 断行分析 (UAX #14+29) ─→ 候选断点列表（逻辑索引）
    ↓
6. 整形 (OpenType) ─→ 视觉顺序的字形序列（含位置、宽度、簇信息）
    ↓
7. 换行布局 ─→ 按行宽切分的行（每行字形序列及位置）
    ↓
8. 渲染 (FreeType) ─→ 屏幕输出 //我们应该是处于第五步的开始的地方了
*/
/*
阶段	是否需要语言	作用
1. 正规化	否
2. BIDI	间接	帮助确定段落基础方向
3. 脚本划分	否
4. 字体选择	是	优先选择支持该语言的字体，并为整形提供语言标签
5. 断行分析	是	应用特定语言的换行规则
6. 整形	是	激活 OpenType 语言特性，选择正确字形
7. 布局	可能	处理标点、对齐等
*/

// -----------------------------------------------------------------------------
// 主函数
// -----------------------------------------------------------------------------
int main()
{
    std::println("utf8proc version: {}", utf8proc_version());

    // 扫描字体
    std::vector<std::string> font_dirs;
#ifdef _WIN32
    font_dirs.push_back("C:/Windows/Fonts");
#elif __APPLE__
    font_dirs.push_back("/System/Library/Fonts");
    font_dirs.push_back("/Library/Fonts");
#else
    font_dirs.push_back("/usr/share/fonts");
    font_dirs.push_back("/usr/local/share/fonts");
#endif

    auto fonts = FontScanner::scan_directories(font_dirs);
    std::println("Loaded {} fonts.", fonts.size());

    struct BreakTestCase
    {
        const char8_t *text;
        std::string_view
            expected_breaks; // 与文本字符数相同的字符串，'|' 表示允许换行，空格表示禁止
    };

    constexpr BreakTestCase break_tests[] = {
        // 已验证的用例
        {u8"Hello, world!", "      |     |"}, // 空格后、!后换行（LB18）
        {u8"server-side", "      |   |"},     // 连字符后、末尾换行（LB21）
        {u8"中文测试", "||||"},               // 汉字后均可换行（LB30）
        {u8"$9", " |"},                       // 货币符号后不换行，数字后可换行（LB25）
        {u8"A——B", "| ||"}, // 破折号(BA)后允许换行，第二个破折号后也允许？请根据实际调整
        {u8"non-breaking\u00A0space", "   |             |"}, // 不换行空格前后均不换行
        {u8"“quote”", "      |"},                            // 右引号后换行（LB19）

        // NOTE: 期望的前提是你知道你在做什么......
        // 修正后的用例 (7-18)
        {u8"Line 1\nLine 2", "    | |    ||"}, // 原期望错误
        {u8"CRLF\r\nshould not break inside",
         "     |      |   |     |     |"},                // 原期望错误
        {u8"a\u200Db", "  |"},                            // 原期望错误
        {u8"e\u0301", "|"},                               // 原期望"|"，实际"|"（一致）
        {u8"no\u200Bbreak", "  |    |"},                  // 原期望错误
        {u8"12.5%", "    |"},                             // 原期望错误
        {u8"1,234", "    |"},                             // 原期望错误
        {u8"break after space", "     |     |    |"},     // 原期望错误
        {u8"Hello! How are you?", "      |   |   |   |"}, // 原期望错误
        {u8"“He said, ‘Good morning.’”", "   |     |     |         |"}, // 原期望错误
        {u8"א-ת", " ||"},                                               // 已通过
        {u8"👩‍❤️‍👩", "     |"}, // 原期望为空，实际为"     |"
    };

    // 创建匹配器
    FontMatcher matcher(std::move(fonts));
    std::string preferred_lang_bcp47 = "en"; // 英文规则
    matcher.set_preferred_language(preferred_lang_bcp47);
    hb_tag_t preferred_tag = matcher.preferred_lang_tag();
    std::println("Preferred language tag: {}",
                 preferred_tag != HB_TAG_NONE ? tag_to_string(preferred_tag) : "none");

    // 全局 FreeType 库（用于后续整形）
    FT_Library ft_lib;
    FT_Init_FreeType(&ft_lib);

    for (size_t i = 0; i < std::size(break_tests); ++i)
    {
        std::println("\n========== Break Test Case {} ==========", i);

        const auto &test_strings = break_tests[i].text;

        // 阶段1
        auto norm = normalize(test_strings);
        print_normalized(norm, i, test_strings);

        if (norm.codepoints.empty())
        {
            std::println("Empty string, skipped.");
            continue;
        }

        // 阶段2
        auto bidi = analyze_bidi(norm, matcher);
        print_bidi(bidi);

        // 阶段3
        auto script_runs = segment_scripts(bidi);
        print_script_runs(script_runs, bidi.codepoints);

        // 阶段4
        auto shaping_runs = assign_fonts(script_runs, bidi.codepoints, matcher);
        print_shaping_runs(shaping_runs, bidi.codepoints);

        // 生成逐字符字体信息用于原始表格（与 process_text2 完全一致）
        std::vector<std::string> font_names;
        std::vector<bool> lang_match, script_match;
        for (size_t j = 0; j < bidi.codepoints.size(); ++j)
        {
            uint32_t cp = bidi.codepoints[j];
            hb_script_t script = bidi.hb_scripts[j]; // 使用已缓存的脚本
            const FontInfo *font = matcher.select_font(cp);
            font_names.push_back(font ? fs::path(font->file_path).filename().string()
                                      : "NOT_SUPPORTED");
            lang_match.push_back(font && preferred_tag != HB_TAG_NONE &&
                                 font->lang_tags.count(preferred_tag));
            script_match.push_back(font && font->scripts.count(script));
        }

        // 打印原始表格
        print_original_analysis(bidi, font_names, lang_match, script_match, i,
                                test_strings);

        // 阶段5
        auto breaks =
            analyze_line_breaks(norm.utf8, bidi.byte_offsets, preferred_lang_bcp47);
        print_breaks(breaks, bidi.codepoints);

        // 验证
        std::string actual;
        for (bool b : breaks.break_after)
            actual += b ? '|' : ' ';
        if (actual != break_tests[i].expected_breaks)
        {
            std::println(stderr, "ERROR: Break mismatch");
            std::println(stderr, "  Expected: {}", break_tests[i].expected_breaks);
            std::println(stderr, "  Got:      {}", actual);
        }
        else
        {
            std::println("  PASS");
        }

        // 阶段6
        auto shaped_segments = shape_all(shaping_runs, bidi.codepoints, ft_lib);
        print_shaped_segments(shaped_segments);

        // 阶段7
        auto lines = layout_lines(shaped_segments, breaks, 500.0);
        print_lines(lines);
    }

    FT_Done_FreeType(ft_lib);

    // 字体回退测试（保持不变）
    {
        std::println("\n=== Testing font fallback for .notdef glyphs ===");
        uint32_t test_cp = 0x4E2D; // "中"

        const FontInfo *font_without_han = nullptr;
        for (const auto &f : matcher.get_fonts())
        {
            if (f.scripts.count(HB_SCRIPT_HAN) == 0)
            {
                font_without_han = &f;
                break;
            }
        }

        if (!font_without_han)
        {
            std::println("No font without Han script found, skipping fallback test.");
        }
        else
        {
            std::println("Using font '{}' (lacks Han script) to shape U+{:04X}...",
                         fs::path(font_without_han->file_path).filename().string(),
                         test_cp);

            FT_Library ft_lib;
            if (FT_Init_FreeType(&ft_lib) == 0)
            {
                FT_Face ft_face;
                if (FT_New_Face(ft_lib, font_without_han->file_path.c_str(), 0,
                                &ft_face) == 0)
                {
                    hb_face_t *hb_face = hb_ft_face_create(ft_face, nullptr);
                    hb_font_t *hb_font = hb_font_create(hb_face);

                    hb_buffer_t *buf = hb_buffer_create();
                    hb_buffer_add_utf32(buf, &test_cp, 1, 0, 1);
                    hb_buffer_set_script(buf, HB_SCRIPT_HAN);
                    hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
                    hb_buffer_set_language(buf, hb_language_from_string("zh", -1));

                    hb_shape(hb_font, buf, nullptr, 0);

                    unsigned glyph_count;
                    hb_glyph_info_t *glyph_infos =
                        hb_buffer_get_glyph_infos(buf, &glyph_count);
                    bool missing = false;
                    for (unsigned i = 0; i < glyph_count; ++i)
                    {
                        if (glyph_infos[i].codepoint == 0)
                        {
                            missing = true;
                            break;
                        }
                    }

                    if (missing)
                    {
                        std::println("-> Character missing in this font (glyph_id == 0). "
                                     "Triggering fallback...");

                        const FontInfo *fallback = nullptr;
                        hb_script_t script = matcher.get_script(test_cp);
                        for (const auto &f : matcher.get_fonts())
                        {
                            if (&f == font_without_han)
                                continue;
                            if (hb_set_has(f.unicode_set, test_cp))
                            {
                                if (preferred_tag != HB_TAG_NONE &&
                                    f.lang_tags.count(preferred_tag))
                                {
                                    fallback = &f;
                                    break;
                                }
                                if (f.scripts.count(script))
                                {
                                    fallback = &f;
                                    break;
                                }
                                fallback = &f;
                                break;
                            }
                        }

                        if (fallback)
                        {
                            std::println(
                                "-> Fallback font selected: '{}'",
                                fs::path(fallback->file_path).filename().string());
                        }
                        else
                        {
                            std::println("-> No fallback font found for U+{:04X}!",
                                         test_cp);
                        }
                    }
                    else
                    {
                        std::println("-> Character present in this font (unexpected, "
                                     "test may be invalid).");
                    }

                    hb_buffer_destroy(buf);
                    hb_font_destroy(hb_font);
                    hb_face_destroy(hb_face);
                    FT_Done_Face(ft_face);
                }
                FT_Done_FreeType(ft_lib);
            }
        }
    }

    return 0;
}