#include <algorithm>
#include <bit>
#include <hb.h>
#include <hb-ft.h>
#include <hb-ot.h>
#include <ft2build.h>
#include <string_view>
#include FT_FREETYPE_H
#include <utf8proc.h>

#include <fribidi/fribidi.h>

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
BreakResult analyze_line_breaks(const std::vector<uint32_t> &codepoints);
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
// NOTE: 更垃圾
BidiResult analyze_bidi(const NormalizedText &norm, const FontMatcher &matcher)
{
    BidiResult result;
    result.codepoints = norm.codepoints;
    result.original_codepoints = norm.codepoints;
    result.byte_offsets = norm.byte_offsets;
    size_t char_count = result.codepoints.size();

    // 初始化输出数组
    result.bidi_levels.assign(char_count, 0);
    result.mirror.assign(char_count, 0);
    result.is_mirrored.assign(char_count, false);
    result.hb_scripts.resize(char_count);
    for (size_t i = 0; i < char_count; ++i)
    {
        result.hb_scripts[i] = matcher.get_script(result.codepoints[i]);
    }

    if (char_count == 0)
    {
        return result;
    }

    // FriBidi 直接使用 UTF-32 码点
    const FriBidiChar *fribidi_text =
        reinterpret_cast<const FriBidiChar *>(result.codepoints.data());
    FriBidiStrIndex len = static_cast<FriBidiStrIndex>(char_count);

    // 步骤1: 获取每个字符的 Bidi 类型
    std::vector<FriBidiCharType> bidi_types(len);
    fribidi_get_bidi_types(fribidi_text, len, bidi_types.data());

    // 步骤2: 分配嵌入级别数组
    std::vector<FriBidiLevel> embed_levels(len);

    // 步骤3: 获取嵌入级别和段落方向（自动检测）
    FriBidiParType par_type = FRIBIDI_PAR_ON; // 请求自动检测
    FriBidiLevel max_level =
        fribidi_get_par_embedding_levels_ex(bidi_types.data(),
                                            nullptr, // bracket_types，不使用括号处理
                                            len,
                                            &par_type, // 输入请求，输出实际段落方向
                                            embed_levels.data());
    if (max_level == 0)
    {
        std::println(stderr, "FriBidi: fribidi_get_par_embedding_levels_ex failed");
        return result;
    }

    // 将级别存入 result.bidi_levels
    for (size_t i = 0; i < char_count; ++i)
    {
        result.bidi_levels[i] = static_cast<uint8_t>(embed_levels[i]);
    }

    // 步骤4: 划分逻辑运行（基于级别变化）
    if (char_count > 0)
    {
        size_t run_start = 0;
        uint8_t current_level = result.bidi_levels[0];
        for (size_t i = 1; i <= char_count; ++i)
        {
            if (i == char_count || result.bidi_levels[i] != current_level)
            {
                result.bidi_runs.push_back({run_start, i, current_level});
                if (i < char_count)
                {
                    run_start = i;
                    current_level = result.bidi_levels[i];
                }
            }
        }
    }

    // 步骤5: 生成视觉顺序的码点序列（同时应用镜像）
    std::vector<FriBidiChar> visual_str(len);
    FriBidiLevel line_max_level =
        fribidi_reorder_line(FRIBIDI_FLAGS_DEFAULT, // 默认标志，包括镜像
                             bidi_types.data(), len,
                             0,        // 起始偏移（整个段落）
                             par_type, // 段落方向
                             embed_levels.data(), visual_str.data(),
                             nullptr // map，不需要
        );
    if (line_max_level == 0)
    {
        std::println(stderr, "FriBidi: fribidi_reorder_line failed");
        return result;
    }

    // 将视觉顺序码点转换为 UTF-8 字符串
    result.visual_string.clear();
    for (FriBidiStrIndex i = 0; i < len; ++i)
    {
        result.visual_string += cp_to_utf8(static_cast<uint32_t>(visual_str[i]));
    }

    // 步骤6: 计算镜像信息（用于打印输出）
    for (size_t i = 0; i < char_count; ++i)
    {
        if (result.bidi_levels[i] % 2 == 1)
        {
            FriBidiChar mirror = fribidi_get_mirror_char(result.codepoints[i], nullptr);
            if (mirror != 0 && mirror != static_cast<FriBidiChar>(result.codepoints[i]))
            {
                result.mirror[i] = static_cast<uint32_t>(mirror);
                result.is_mirrored[i] = true;
            }
        }
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
                                     const FontMatcher &matcher,
                                     hb_tag_t preferred_lang_tag)
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
BreakResult analyze_line_breaks(const std::vector<uint32_t> &codepoints)
{
    BreakResult result;
    result.break_after.assign(codepoints.size(), false);
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
    std::println("(Not implemented yet)");
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

    // 创建匹配器
    FontMatcher matcher(std::move(fonts));
    matcher.set_preferred_language("zh-CN");
    hb_tag_t preferred_tag = matcher.preferred_lang_tag();
    std::println("Preferred language tag: {}",
                 preferred_tag != HB_TAG_NONE ? tag_to_string(preferred_tag) : "none");

    // 测试用例
    constexpr std::array<const char8_t *, 41> test_strings = {
        u8"Hello, world!",
        u8"السلام عليكم",
        u8"Hello in Arabic is مرحبا",
        u8"car (سيارة) fast",
        u8"देवनागरी தமிழ் ಕನ್ನಡ",
        u8"汉字 ひらがな カタカナ 한국어",
        u8"👨‍👩‍👧‍👦",
        u8"x² + y² = z²",
        u8"عدد 123 و 456",
        u8"zero\u200Bwidth",
        u8"   ",
        u8"𑀪𑀸𑀭𑀢 (भारत) - India",
        u8"",
        u8" \t\n\r\v\f",
        u8"\u2066Hello\u2069",
        u8"∫ f(x) dx + ∑_{n=1}^{∞} 1/n² = π²/6",
        u8"یہ ایک )car( ہے۔",
        u8"是",
        // 增加
        u8"a\u200Eb",                                         // 18
        u8"a\u200Fb",                                         // 19
        u8"\u200Eabc",                                        // 20
        u8"abc\u200F",                                        // 21
        u8"\u202Aabc\u202C",                                  // 22
        u8"\u202Babc\u202C",                                  // 23
        u8"\u202Dabc\u202C",                                  // 24
        u8"\u202Eabc\u202C",                                  // 25
        u8"\u2066abc\u2069",                                  // 26
        u8"\u2067abc\u2069",                                  // 27
        u8"\u2068abc\u2069",                                  // 28
        u8"\u2068\u05D0abc\u2069",                            // 29
        u8"x\u202By\u202Cz",                                  // 30
        u8"\u202A\u202Babc\u202C\u202C",                      // 31
        u8"\u2066Hello \u2067\u05D0\u05D1\u2069 World\u2069", // 32
        u8"\u200E",                                           // 33
        u8"\u202A\u202C",                                     // 34
        u8"\u200E\u200F\u202A\u202C\u2066\u2069",             // 35
        u8"car \u202B(\u202C)",                               // 36
        u8"\u202E(\u202C)",                                   // 37
        u8"\u2067(\u2069)",                                   // 38
        u8"line1\u200E\nline2",                               // 39
        u8"line1\u202B\nline2\u202C",                         // 40
    };
    // NOTE: 不太符合标准。换库
    //  预期的视觉顺序字符串（与旧代码输出完全一致）
    constexpr std::array<const char8_t *, 41> expected_visual = {
        // 原有的 0-17（保持原样）
        u8"Hello, world!",
        u8"مكيلع مالسلا",
        u8"Hello in Arabic is ابحرم",
        u8"car (ةرايس) fast",
        u8"देवनागरी தமிழ் ಕನ್ನಡ",
        u8"汉字 ひらがな カタカナ 한국어",
        u8"👨‍👩‍👧‍👦",
        u8"x² + y² = z²",
        u8"456 و 123 ددع",
        u8"zero\u200Bwidth",
        u8"   ",
        u8"𑀪𑀸𑀭𑀢 (भारत) - India",
        u8"",
        u8" \t\n\r\v\f",
        u8"\u2066Hello\u2069",
        u8"∫ f(x) dx + ∑_{n=1}^{∞} 1/n² = π²/6",
        u8"۔ےہ )car( کیا ہی",
        u8"是",

        // 新增 18-40
        u8"a\u200Eb",                                         // 18
        u8"a\u200Fb",                                         // 19
        u8"\u200Eabc",                                        // 20
        u8"abc\u200F",                                        // 21
        u8"\u202Aabc\u202C",                                  // 22
        u8"\u202Babc\u202C",                                  // 23
        u8"\u202Dabc\u202C",                                  // 24
        u8"\u202Eabc\u202C",                                  // 25
        u8"\u2066abc\u2069",                                  // 26
        u8"\u2067abc\u2069",                                  // 27
        u8"\u2068abc\u2069",                                  // 28
        u8"\u2068\u05D0abc\u2069",                            // 29
        u8"x\u202By\u202Cz",                                  // 30
        u8"\u202A\u202Babc\u202C\u202C",                      // 31
        u8"\u2066Hello \u2067\u05D0\u05D1\u2069 World\u2069", // 32
        u8"\u200E",                                           // 33
        u8"\u202A\u202C",                                     // 34
        u8"\u200E\u200F\u202A\u202C\u2066\u2069",             // 35
        u8"car \u202B(\u202C)",                               // 36
        u8"\u202E(\u202C)",                                   // 37
        u8"\u2067(\u2069)",                                   // 38
        u8"line1\u200E\nline2",                               // 39
        u8"line1\u202B\nline2\u202C",                         // 40
    };
    constexpr auto Taget = u8"ا";
    static_assert(test_strings[1][0] == Taget[0]);

    // 全局 FreeType 库（用于后续整形）
    FT_Library ft_lib;
    FT_Init_FreeType(&ft_lib);

    for (size_t i = 0; i < test_strings.size(); ++i)
    {
        std::println("\n========== Test case {} ==========", i);

        // 阶段1
        auto norm = normalize(test_strings[i]);
        print_normalized(norm, i, test_strings[i]);

        if (norm.codepoints.empty())
        {
            std::println("Empty string, skipped.");
            continue;
        }

        // 阶段2
        auto bidi = analyze_bidi(norm, matcher);
        print_bidi(bidi);

        // 断言视觉顺序
        {
            // 断言视觉顺序
            std::string expected = reinterpret_cast<const char *>(expected_visual[i]);
            if (bidi.visual_string != expected)
            {
                std::println(stderr, "ERROR: Visual string mismatch for test case {}", i);
                // 辅助函数：将UTF-8字符串转换为逗号分隔的转义字符序列
                auto format_codepoints = [](const std::string &s) -> std::string {
                    std::string result;
                    const unsigned char *p =
                        reinterpret_cast<const unsigned char *>(s.data());
                    const unsigned char *end = p + s.size();
                    bool first = true;
                    while (p < end)
                    {
                        utf8proc_int32_t cp;
                        utf8proc_ssize_t bytes = utf8proc_iterate(p, end - p, &cp);
                        if (bytes < 0)
                            break;
                        if (!first)
                            result += ", ";
                        result += cp_to_escaped(static_cast<uint32_t>(cp));
                        p += bytes;
                        first = false;
                    }
                    return result;
                };

                std::println(stderr, "  Expected (codepoints): {}",
                             format_codepoints(expected));
                std::println(stderr, "  Got (codepoints):      {}",
                             format_codepoints(bidi.visual_string));

                // 将 UTF-8 字符串转换为码点序列以便逐个比较
                auto to_codepoints = [](const std::string &s) {
                    std::vector<uint32_t> cps;
                    const unsigned char *p =
                        reinterpret_cast<const unsigned char *>(s.data());
                    const unsigned char *end = p + s.size();
                    while (p < end)
                    {
                        utf8proc_int32_t cp;
                        utf8proc_ssize_t bytes = utf8proc_iterate(p, end - p, &cp);
                        if (bytes < 0)
                            break;
                        cps.push_back(static_cast<uint32_t>(cp));
                        p += bytes;
                    }
                    return cps;
                };

                auto expected_cps = to_codepoints(expected);
                auto got_cps = to_codepoints(bidi.visual_string);

                std::println(stderr, "\n  Detailed comparison (logical order):");
                std::println(stderr, "  {:<4} {:<12} {:<12} {:<6} {:<10} {:<10}", "Idx",
                             "Expected U+", "Got U+", "Match", "Expected Char",
                             "Got Char");

                size_t max_len = std::max(expected_cps.size(), got_cps.size());
                for (size_t j = 0; j < max_len; ++j)
                {
                    uint32_t exp_cp = (j < expected_cps.size()) ? expected_cps[j] : 0;
                    uint32_t got_cp = (j < got_cps.size()) ? got_cps[j] : 0;
                    bool match = (j < expected_cps.size() && j < got_cps.size() &&
                                  exp_cp == got_cp);
                    std::string exp_str = cp_to_escaped(exp_cp);
                    std::string got_str = cp_to_escaped(got_cp);
                    std::println(
                        stderr,
                        "  {:<4} U+{:06X} ({})  U+{:06X} ({})  {:<6} {:<10} {:<10}", j,
                        exp_cp, exp_str, got_cp, got_str, match ? "✓" : "✗", exp_str,
                        got_str);
                }

                // 如果长度不同，额外提示
                if (expected_cps.size() != got_cps.size())
                {
                    std::println(stderr, "  Note: Length mismatch (expected {}, got {})",
                                 expected_cps.size(), got_cps.size());
                }
            }
        }

        // 阶段3
        auto script_runs = segment_scripts(bidi);
        print_script_runs(script_runs, bidi.codepoints);

        // 阶段4
        auto shaping_runs =
            assign_fonts(script_runs, bidi.codepoints, matcher, preferred_tag);
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
                                test_strings[i]);

        // 阶段5
        auto breaks = analyze_line_breaks(bidi.codepoints);
        print_breaks(breaks, bidi.codepoints);

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