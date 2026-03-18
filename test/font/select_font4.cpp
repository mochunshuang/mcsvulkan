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
std::string script_to_string(hb_script_t script)
{
    hb_tag_t tag = hb_script_to_iso15924_tag(script);
    if (tag == HB_TAG_NONE)
        return "Invalid";
    char buf[5] = {0};
    hb_tag_to_string(tag, buf);
    return {buf};
}

std::string tag_to_string(hb_tag_t tag)
{
    char buf[5] = {0};
    hb_tag_to_string(tag, buf);
    return {buf};
}

std::string cp_to_utf8(char32_t cp)
{
    char buf[8] = {0};
    utf8proc_ssize_t len = utf8proc_encode_char(
        static_cast<utf8proc_int32_t>(cp), reinterpret_cast<utf8proc_uint8_t *>(buf));
    return (len > 0) ? std::string(buf) : "?";
}

static int utf8_char_len(unsigned char c)
{
    if (c < 0x80)
        return 1;
    else if ((c >> 5) == 0x06)
        return 2;
    else if ((c >> 4) == 0x0E)
        return 3;
    else if ((c >> 3) == 0x1E)
        return 4;
    else
        return 1;
}

static uint32_t utf8_decode(const unsigned char **ptr)
{
    const unsigned char *p = *ptr;
    uint32_t cp;
    int len = utf8_char_len(*p);
    if (len == 1)
    {
        cp = *p;
        *ptr = p + 1;
    }
    else if (len == 2)
    {
        cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
        *ptr = p + 2;
    }
    else if (len == 3)
    {
        cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        *ptr = p + 3;
    }
    else if (len == 4)
    {
        cp = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) |
             (p[3] & 0x3F);
        *ptr = p + 4;
    }
    else
    {
        cp = *p;
        *ptr = p + 1;
    }
    return cp;
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
// 分析结果结构体（增加 codepoints 字段）
// -----------------------------------------------------------------------------
struct TextAnalysis
{
    std::string visual_string;             // 视觉顺序字符串（原始字符）
    std::vector<uint32_t> codepoints;      // 逻辑顺序的码点（新增）
    std::vector<std::string> font_names;   // 每个字符使用的字体文件名（按逻辑顺序）
    std::vector<std::string> char_display; // 每个字符的显示形式（转义后）
    std::vector<hb_script_t> scripts;      // 每个字符的脚本
    std::vector<bool> lang_match;          // 每个字符是否语言匹配
    std::vector<bool> script_match;        // 每个字符是否脚本匹配
    size_t char_count;

    std::vector<uint32_t> mirror;  // 每个逻辑字符的镜像码点（0 表示无镜像）
    std::vector<bool> is_mirrored; // 每个逻辑字符是否被镜像（可选）
};

// -----------------------------------------------------------------------------
// 处理单个文本字符串
// -----------------------------------------------------------------------------
TextAnalysis process_text(const char8_t *text, const FontMatcher &matcher,
                          hb_tag_t preferred_tag)
{
    TextAnalysis result;

    // UAX #15 正规化
    utf8proc_uint8_t *normalized =
        utf8proc_NFC(reinterpret_cast<const utf8proc_uint8_t *>(text));
    if (!normalized)
    {
        std::println(stderr, "Normalization failed for text");
        return result;
    }
    const char *bidiText = reinterpret_cast<const char *>(normalized);
    size_t byte_len = strlen(bidiText);

    // 转换为 UTF-32 并记录字节偏移
    std::vector<uint32_t> codepoints;
    std::vector<size_t> byte_offsets;
    const unsigned char *p = (const unsigned char *)bidiText;
    const unsigned char *end = p + byte_len;
    while (p < end)
    {
        byte_offsets.push_back(p - (const unsigned char *)bidiText);
        codepoints.push_back(utf8_decode(&p));
    }
    size_t char_count = codepoints.size();
    result.char_count = char_count;
    if (char_count == 0)
    {
        free(normalized);
        return result;
    }

    // 保存码点
    result.codepoints = codepoints; // 新增

    // BIDI 分析
    SBCodepointSequence seq = {SBStringEncodingUTF8, (void *)bidiText, byte_len};
    SBAlgorithmRef algo = SBAlgorithmCreate(&seq);
    SBParagraphRef para =
        SBAlgorithmCreateParagraph(algo, 0, INT32_MAX, SBLevelDefaultLTR);
    SBLineRef line = SBParagraphCreateLine(para, 0, SBParagraphGetLength(para));
    SBUInteger runCount = SBLineGetRunCount(line);
    const SBRun *runs = SBLineGetRunsPtr(line);

    // 镜像映射
    std::vector<uint32_t> mirror(char_count, 0);
    std::vector<bool> is_mirrored(char_count);
    SBMirrorLocatorRef mirrorLocator = SBMirrorLocatorCreate();
    SBMirrorLocatorLoadLine(mirrorLocator, line, (void *)bidiText);
    const SBMirrorAgent *agent = SBMirrorLocatorGetAgent(mirrorLocator);
    while (SBMirrorLocatorMoveNext(mirrorLocator))
    {
        SBUInteger byte_idx = agent->index;
        auto it = std::lower_bound(byte_offsets.begin(), byte_offsets.end(), byte_idx);
        if (it != byte_offsets.end() && *it == byte_idx)
        {
            auto i = it - byte_offsets.begin();
            mirror[i] = static_cast<uint32_t>(agent->mirror);
            is_mirrored[i] = (mirror[i] != 0);
        }
    }
    result.mirror = mirror;
    result.is_mirrored = is_mirrored;

    // 构建视觉顺序字符串（已镜像）
    std::vector<uint32_t> visual_codepoints;
    std::vector<size_t> visual_to_logical;

    for (SBUInteger i = 0; i < runCount; ++i)
    {
        const SBRun &run = runs[i];
        auto start_it =
            std::lower_bound(byte_offsets.begin(), byte_offsets.end(), run.offset);
        auto end_it = std::lower_bound(byte_offsets.begin(), byte_offsets.end(),
                                       run.offset + run.length);
        size_t logical_start = start_it - byte_offsets.begin();
        size_t logical_end = end_it - byte_offsets.begin();

        if (run.level % 2) // RTL
        {
            for (size_t j = logical_end; j > logical_start; --j)
            {
                size_t logical = j - 1;
                uint32_t final_cp =
                    mirror[logical] ? mirror[logical] : codepoints[logical];
                visual_codepoints.push_back(final_cp);
                visual_to_logical.push_back(logical);
            }
        }
        else // LTR
        {
            for (size_t j = logical_start; j < logical_end; ++j)
            {
                size_t logical = j;
                uint32_t final_cp =
                    mirror[logical] ? mirror[logical] : codepoints[logical];
                visual_codepoints.push_back(final_cp);
                visual_to_logical.push_back(logical);
            }
        }
    }

    // 构建视觉字符串
    for (uint32_t cp : visual_codepoints)
        result.visual_string += cp_to_utf8(cp);

    // 获取每个字符的脚本和字体信息（按逻辑顺序）
    result.scripts.resize(char_count);
    result.font_names.resize(char_count);
    result.char_display.resize(char_count);
    result.lang_match.resize(char_count, false);
    result.script_match.resize(char_count, false);

    for (size_t i = 0; i < char_count; ++i)
    {
        uint32_t cp = codepoints[i];
        hb_script_t script = matcher.get_script(cp);
        result.scripts[i] = script;
        result.char_display[i] = cp_to_escaped(cp);

        auto font = matcher.select_font(cp);
        if (font)
        {
            result.font_names[i] = fs::path(font->file_path).filename().string();
            result.lang_match[i] = (preferred_tag != HB_TAG_NONE &&
                                    font->lang_tags.count(preferred_tag) != 0);
            result.script_match[i] = (font->scripts.count(script) != 0);
        }
        else
        {
            result.font_names[i] = "NOT_SUPPORTED";
            result.lang_match[i] = false;
            result.script_match[i] = false;
        }
    }

    // 释放资源
    SBMirrorLocatorRelease(mirrorLocator);
    SBLineRelease(line);
    SBParagraphRelease(para);
    SBAlgorithmRelease(algo);
    free(normalized);

    return result;
}

// -----------------------------------------------------------------------------
// 处理单个文本字符串（优化版：直接使用 utf8proc 获取 UTF-32 码点）
// -----------------------------------------------------------------------------
TextAnalysis process_text2(const char8_t *text, const FontMatcher &matcher,
                           hb_tag_t preferred_tag)
{
    TextAnalysis result;

    // 1. UAX #15 正规化：得到 UTF-8 字符串（NFC）
    auto normalized =
        std::unique_ptr<utf8proc_uint8_t, decltype([](auto p) { free(p); })>(
            utf8proc_NFC(reinterpret_cast<const utf8proc_uint8_t *>(text)));
    if (!normalized)
    {
        std::println(stderr, "Normalization failed for text");
        return result;
    }

    const char *bidiText = reinterpret_cast<const char *>(normalized.get());
    size_t byte_len = std::strlen(bidiText);

    // 2. 使用 utf8proc_iterate 解码为 UTF-32，并记录字节偏移
    std::vector<uint32_t> codepoints;
    std::vector<size_t> byte_offsets;
    const unsigned char *p = reinterpret_cast<const unsigned char *>(bidiText);
    const unsigned char *const end = p + byte_len;

    // 预分配容量（通常每个字符最多4字节，但安全起见预留大致空间）
    codepoints.reserve(byte_len);
    byte_offsets.reserve(byte_len);

    while (p < end)
    {
        byte_offsets.push_back(p - reinterpret_cast<const unsigned char *>(bidiText));

        utf8proc_int32_t cp;
        utf8proc_ssize_t bytes = utf8proc_iterate(p, end - p, &cp);
        if (bytes < 0)
        {
            std::println(stderr, "UTF-8 decoding error at offset {}",
                         p - reinterpret_cast<const unsigned char *>(bidiText));
            break;
        }
        codepoints.push_back(static_cast<uint32_t>(cp));
        p += bytes;
    }

    size_t char_count = codepoints.size();
    result.char_count = char_count;
    if (char_count == 0)
    {
        return result;
    }

    // 保存码点
    result.codepoints = std::move(codepoints);

    // 3. BIDI 分析 (UAX #9) 使用 UTF-8 字符串
    SBCodepointSequence seq = {SBStringEncodingUTF8, const_cast<char *>(bidiText),
                               byte_len};
    SBAlgorithmRef algo = SBAlgorithmCreate(&seq);
    SBParagraphRef para =
        SBAlgorithmCreateParagraph(algo, 0, INT32_MAX, SBLevelDefaultLTR);
    SBLineRef line = SBParagraphCreateLine(para, 0, SBParagraphGetLength(para));

    // 使用 RAII 包装 BIDI 资源（可选，为简洁仍手动释放）
    // 为保持清晰，继续使用原始 API，但注意异常安全（本函数内不抛出异常）

    SBUInteger runCount = SBLineGetRunCount(line);
    const SBRun *runs = SBLineGetRunsPtr(line);

    // 4. 镜像映射（通过字节偏移查字符索引）
    std::vector<uint32_t> mirror(char_count, 0);
    std::vector<bool> is_mirrored(char_count, false);

    SBMirrorLocatorRef mirrorLocator = SBMirrorLocatorCreate();
    SBMirrorLocatorLoadLine(mirrorLocator, line, const_cast<char *>(bidiText));
    const SBMirrorAgent *agent = SBMirrorLocatorGetAgent(mirrorLocator);
    while (SBMirrorLocatorMoveNext(mirrorLocator))
    {
        SBUInteger byte_idx = agent->index;
        auto it = std::lower_bound(byte_offsets.begin(), byte_offsets.end(), byte_idx);
        if (it != byte_offsets.end() && *it == byte_idx)
        {
            auto i = static_cast<size_t>(it - byte_offsets.begin());
            mirror[i] = static_cast<uint32_t>(agent->mirror);
            is_mirrored[i] = (mirror[i] != 0);
        }
    }

    result.mirror = std::move(mirror);
    result.is_mirrored = std::move(is_mirrored);

    // 5. 构建视觉顺序字符串（已镜像）
    std::vector<uint32_t> visual_codepoints;
    std::vector<size_t> visual_to_logical;
    visual_codepoints.reserve(char_count);
    visual_to_logical.reserve(char_count);

    for (SBUInteger i = 0; i < runCount; ++i)
    {
        const SBRun &run = runs[i];
        auto start_it =
            std::lower_bound(byte_offsets.begin(), byte_offsets.end(), run.offset);
        auto end_it = std::lower_bound(byte_offsets.begin(), byte_offsets.end(),
                                       run.offset + run.length);
        size_t logical_start = static_cast<size_t>(start_it - byte_offsets.begin());
        size_t logical_end = static_cast<size_t>(end_it - byte_offsets.begin());

        if (run.level % 2) // RTL
        {
            for (size_t j = logical_end; j > logical_start; --j)
            {
                size_t logical = j - 1;
                uint32_t final_cp = result.mirror[logical] ? result.mirror[logical]
                                                           : result.codepoints[logical];
                visual_codepoints.push_back(final_cp);
                visual_to_logical.push_back(logical);
            }
        }
        else // LTR
        {
            for (size_t j = logical_start; j < logical_end; ++j)
            {
                size_t logical = j;
                uint32_t final_cp = result.mirror[logical] ? result.mirror[logical]
                                                           : result.codepoints[logical];
                visual_codepoints.push_back(final_cp);
                visual_to_logical.push_back(logical);
            }
        }
    }

    // 构建视觉字符串（原始字符，用于验证 BIDI 结果）
    result.visual_string.clear();
    result.visual_string.reserve(visual_codepoints.size() *
                                 4); // 粗略估计 UTF-8 最大字节数
    for (uint32_t cp : visual_codepoints)
        result.visual_string += cp_to_utf8(cp);

    // 6. 获取每个字符的脚本和字体信息（按逻辑顺序）
    result.scripts.resize(char_count);
    result.font_names.resize(char_count);
    result.char_display.resize(char_count);
    result.lang_match.assign(char_count, false);
    result.script_match.assign(char_count, false);

    for (size_t i = 0; i < char_count; ++i)
    {
        uint32_t cp = result.codepoints[i];
        hb_script_t script = matcher.get_script(cp);
        result.scripts[i] = script;
        result.char_display[i] = cp_to_escaped(cp);

        if (auto font = matcher.select_font(cp))
        {
            result.font_names[i] = fs::path(font->file_path).filename().string();
            result.lang_match[i] = (preferred_tag != HB_TAG_NONE &&
                                    font->lang_tags.count(preferred_tag) != 0);
            result.script_match[i] = (font->scripts.count(script) != 0);
        }
        else
        {
            result.font_names[i] = "NOT_SUPPORTED";
            // lang_match 和 script_match 默认为 false
        }
    }

    // 释放 BIDI 资源
    SBMirrorLocatorRelease(mirrorLocator);
    SBLineRelease(line);
    SBParagraphRelease(para);
    SBAlgorithmRelease(algo);

    return result;
}

// -----------------------------------------------------------------------------
// 处理单个文本字符串（UTF-32 输入版，直接使用 utf8proc 处理码点）
// 输入：UTF-32 字符串（以空字符结尾）
// -----------------------------------------------------------------------------
TextAnalysis process_text3(const char32_t *text, const FontMatcher &matcher,
                           hb_tag_t preferred_tag)
{
    TextAnalysis result;

    // 计算码点数量（直到空字符）
    size_t char_count = 0;
    while (text[char_count] != 0)
        ++char_count;
    if (char_count == 0)
    {
        return result; // 空字符串，直接返回（所有向量默认空）
    }

    // 复制码点到可变缓冲区，因为 utf8proc_normalize_utf32 会原地修改
    std::vector<uint32_t> codepoints(text, text + char_count);

    // 应用 NFC 规范化（原地）
    utf8proc_option_t compose_opts =
        static_cast<utf8proc_option_t>(UTF8PROC_COMPOSE | UTF8PROC_STABLE);
    utf8proc_ssize_t composed_len =
        utf8proc_normalize_utf32(reinterpret_cast<utf8proc_int32_t *>(codepoints.data()),
                                 codepoints.size(), compose_opts);
    if (composed_len < 0)
    {
        std::println(stderr, "utf8proc_normalize_utf32 failed: {}",
                     utf8proc_errmsg(composed_len));
        return result;
    }
    codepoints.resize(composed_len);
    char_count = composed_len;
    result.char_count = char_count;

    // 保存码点
    result.codepoints = codepoints;

    // **关键修复：现在根据最终的 char_count 调整所有向量大小**
    result.scripts.resize(char_count);
    result.font_names.resize(char_count);
    result.char_display.resize(char_count);
    result.lang_match.resize(char_count, false);
    result.script_match.resize(char_count, false);
    result.mirror.resize(char_count, 0);
    result.is_mirrored.resize(char_count, false);

    // BIDI 分析直接使用 UTF-32 字符串
    SBCodepointSequence seq = {
        SBStringEncodingUTF32, codepoints.data(),
        static_cast<SBUInteger>(char_count * 4) // 字节长度
    };
    SBAlgorithmRef algo = SBAlgorithmCreate(&seq);
    SBParagraphRef para =
        SBAlgorithmCreateParagraph(algo, 0, INT32_MAX, SBLevelDefaultLTR);
    SBLineRef line = SBParagraphCreateLine(para, 0, SBParagraphGetLength(para));
    SBUInteger runCount = SBLineGetRunCount(line);
    const SBRun *runs = SBLineGetRunsPtr(line);

    // 镜像映射（直接使用字符索引，因为 UTF-32 是定长）
    std::vector<uint32_t> mirror(char_count, 0);
    std::vector<bool> is_mirrored(char_count, false);
    SBMirrorLocatorRef mirrorLocator = SBMirrorLocatorCreate();
    SBMirrorLocatorLoadLine(mirrorLocator, line, codepoints.data());
    const SBMirrorAgent *agent = SBMirrorLocatorGetAgent(mirrorLocator);
    while (SBMirrorLocatorMoveNext(mirrorLocator))
    {
        SBUInteger byte_idx = agent->index;
        size_t char_idx = byte_idx / 4;
        if (char_idx < char_count)
        {
            mirror[char_idx] = static_cast<uint32_t>(agent->mirror);
            is_mirrored[char_idx] = (mirror[char_idx] != 0);
        }
    }
    result.mirror = mirror;
    result.is_mirrored = is_mirrored;

    // 构建视觉顺序字符串（已镜像）
    std::vector<uint32_t> visual_codepoints;
    std::vector<size_t> visual_to_logical;

    for (SBUInteger i = 0; i < runCount; ++i)
    {
        const SBRun &run = runs[i];
        size_t logical_start = run.offset / 4;
        size_t logical_end = (run.offset + run.length) / 4;

        if (run.level % 2)
        { // RTL
            for (size_t j = logical_end; j > logical_start; --j)
            {
                size_t logical = j - 1;
                uint32_t final_cp =
                    mirror[logical] ? mirror[logical] : codepoints[logical];
                visual_codepoints.push_back(final_cp);
                visual_to_logical.push_back(logical);
            }
        }
        else
        { // LTR
            for (size_t j = logical_start; j < logical_end; ++j)
            {
                size_t logical = j;
                uint32_t final_cp =
                    mirror[logical] ? mirror[logical] : codepoints[logical];
                visual_codepoints.push_back(final_cp);
                visual_to_logical.push_back(logical);
            }
        }
    }

    // 构建视觉字符串
    for (uint32_t cp : visual_codepoints)
        result.visual_string += cp_to_utf8(cp);

    // 获取每个字符的脚本和字体信息（按逻辑顺序）
    result.scripts.resize(char_count);
    result.font_names.resize(char_count);
    result.char_display.resize(char_count);
    result.lang_match.resize(char_count, false);
    result.script_match.resize(char_count, false);

    for (size_t i = 0; i < char_count; ++i)
    {
        uint32_t cp = codepoints[i];
        hb_script_t script = matcher.get_script(cp);
        result.scripts[i] = script;
        result.char_display[i] = cp_to_escaped(cp);

        auto font = matcher.select_font(cp);
        if (font)
        {
            result.font_names[i] = fs::path(font->file_path).filename().string();
            result.lang_match[i] = (preferred_tag != HB_TAG_NONE &&
                                    font->lang_tags.count(preferred_tag) != 0);
            result.script_match[i] = (font->scripts.count(script) != 0);
        }
        else
        {
            result.font_names[i] = "NOT_SUPPORTED";
            result.lang_match[i] = false;
            result.script_match[i] = false;
        }
    }

    // 释放资源
    SBMirrorLocatorRelease(mirrorLocator);
    SBLineRelease(line);
    SBParagraphRelease(para);
    SBAlgorithmRelease(algo);

    return result;
}

// -----------------------------------------------------------------------------
// 打印分析结果（修复 U+ 列，显示真实码点）
// -----------------------------------------------------------------------------
void print_analysis(const TextAnalysis &analysis, size_t index, const char8_t *raw_text)
{
    std::println("\n=== Test case {} ===", index);
    std::println("Original (logical): {}", reinterpret_cast<const char *>(raw_text));
    std::println("Visual string: \"{}\"", analysis.visual_string);

    if (analysis.char_count == 0)
    {
        std::println("Empty string, skipped.");
        return;
    }

    std::println("\nFont selection (logical order):");
    // 调整列宽以容纳6位十六进制数（U+xxxxxx）
    std::println("{:<4} {:<10} {:<12} {:<20} {:<10} {}", "Idx", "U+", "Char", "Font",
                 "LangMatch", "ScriptMatch");
    for (size_t i = 0; i < analysis.char_count; ++i)
    {
        // 使用 analysis.codepoints[i] 打印真实码点，格式 U+%06X
        std::println("{:<4} U+{:06X}  {:<10} {:<20} {:<10} {}", i,
                     static_cast<unsigned>(analysis.codepoints[i]),
                     analysis.char_display[i], analysis.font_names[i],
                     analysis.lang_match[i] ? "Yes" : "No",
                     analysis.script_match[i] ? "Yes" : "No");
    }

    std::println("\nMirror status (logical order):");
    std::println("{:<4} {:<10} {:<10} {:<10} {:<10}", "Idx", "Orig", "Mirror", "Char",
                 "Mirrored?");
    for (size_t i = 0; i < analysis.char_count; ++i)
    {
        uint32_t orig = analysis.codepoints[i];
        uint32_t mirr = analysis.mirror[i];
        std::string orig_char = cp_to_escaped(orig);
        std::string mirr_char = mirr ? cp_to_escaped(mirr) : "";
        std::println("{:<4} U+{:06X} {:<10} U+{:06X} {:<10} {:<6}", i, orig, orig_char,
                     mirr, mirr_char, mirr ? "Yes" : "No");
    }
}
// -----------------------------------------------------------------------------
// 打印分析结果（重载，接受 UTF-32 原始文本）
// -----------------------------------------------------------------------------
void print_analysis(const TextAnalysis &analysis, size_t index, const char32_t *raw_text)
{
    // 将 UTF-32 字符串转换为 UTF-8 用于显示
    std::string utf8_original;
    for (size_t i = 0; raw_text[i] != 0; ++i)
    {
        utf8_original += cp_to_utf8(static_cast<char32_t>(raw_text[i]));
    }
    if (analysis.char_count == 0)
    {
        std::println("Empty string, skipped.");
        return;
    }

    // 可选：检查向量大小一致性（调试用）
    if (analysis.codepoints.size() != analysis.char_count ||
        analysis.lang_match.size() != analysis.char_count ||
        analysis.script_match.size() != analysis.char_count ||
        analysis.char_display.size() != analysis.char_count ||
        analysis.font_names.size() != analysis.char_count ||
        analysis.mirror.size() != analysis.char_count)
    {
        std::println(stderr, "Internal error: vector size mismatch in TextAnalysis");
        return;
    }

    std::println("\n=== Test case {} ===", index);
    std::println("Original (logical): {}", utf8_original);
    std::println("Visual string: \"{}\"", analysis.visual_string);

    if (analysis.char_count == 0)
    {
        std::println("Empty string, skipped.");
        return;
    }

    std::println("\nFont selection (logical order):");
    std::println("{:<4} {:<10} {:<12} {:<20} {:<10} {}", "Idx", "U+", "Char", "Font",
                 "LangMatch", "ScriptMatch");
    for (size_t i = 0; i < analysis.char_count; ++i)
    {
        std::println("{:<4} U+{:06X}  {:<10} {:<20} {:<10} {}", i,
                     static_cast<unsigned>(analysis.codepoints[i]),
                     analysis.char_display[i], analysis.font_names[i],
                     analysis.lang_match[i] ? "Yes" : "No",
                     analysis.script_match[i] ? "Yes" : "No");
    }

    std::println("\nMirror status (logical order):");
    std::println("{:<4} {:<10} {:<10} {:<10} {:<10}", "Idx", "Orig", "Mirror", "Char",
                 "Mirrored?");
    for (size_t i = 0; i < analysis.char_count; ++i)
    {
        uint32_t orig = analysis.codepoints[i];
        uint32_t mirr = analysis.mirror[i];
        std::string orig_char = analysis.char_display[i]; // 已转义
        std::string mirr_char = mirr ? cp_to_escaped(mirr) : "";
        std::println("{:<4} U+{:06X} {:<10} U+{:06X} {:<10} {:<6}", i, orig, orig_char,
                     mirr, mirr_char, mirr ? "Yes" : "No");
    }
}
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
    constexpr std::array<const char8_t *, 18> test_strings = {
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
    };

    constexpr std::array<const char32_t *, 18> test_strings32 = {
        U"Hello, world!",
        U"السلام عليكم",
        U"Hello in Arabic is مرحبا",
        U"car (سيارة) fast",
        U"देवनागरी தமிழ் ಕನ್ನಡ",
        U"汉字 ひらがな カタカナ 한국어",
        U"👨‍👩‍👧‍👦",
        U"x² + y² = z²",
        U"عدد 123 و 456",
        U"zero\u200Bwidth",
        U"   ",
        U"𑀪𑀸𑀭𑀢 (भारत) - India",
        U"",
        U" \t\n\r\v\f",
        U"\u2066Hello\u2069",
        U"∫ f(x) dx + ∑_{n=1}^{∞} 1/n² = π²/6",
        U"یہ ایک )car( ہے۔",
        U"是",
    };

    for (size_t i = 0; i < test_strings.size(); ++i)
    {
        // auto analysis = process_text(test_strings[i], matcher, preferred_tag);
        auto analysis = process_text2(test_strings[i], matcher, preferred_tag);
        print_analysis(analysis, i, test_strings[i]);

        // NOTE: BUG. 还是 u8
        //  auto analysis = process_text3(test_strings32[i], matcher, preferred_tag);
        //  print_analysis(analysis, i, test_strings32[i]);
    }

    // 字体回退测试
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