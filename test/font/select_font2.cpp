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
#include <optional>
#include <span>
#include <print>
#include <algorithm>
#include <cstdint>

namespace fs = std::filesystem;

// -----------------------------------------------------------------------------
// 字体信息结构（缓存不变数据）
// -----------------------------------------------------------------------------
struct FontInfo
{
    std::string file_path;         // 字体文件路径
    std::string family_name;       // 族名（可选）
    hb_set_t *unicode_set;         // 字体支持的 Unicode 码点集合（需要释放）
    std::set<hb_script_t> scripts; // 字体支持的脚本（从 GSUB/GPOS 提取）
    std::set<hb_tag_t> lang_tags;  // 字体支持的语言标签

    FontInfo() : unicode_set(hb_set_create()) {}
    ~FontInfo()
    {
        hb_set_destroy(unicode_set);
    }

    // 禁止拷贝，允许移动
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
// 字体扫描器（支持多个目录）
// -----------------------------------------------------------------------------
class FontScanner
{
  public:
    // 扫描多个目录下的所有字体文件（TTF/OTF/TTC/OTC）
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

                        // 收集脚本和语言标签（从 GSUB 和 GPOS 表）
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

                        // 收集 Unicode 覆盖
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
// 字体匹配器（核心选择逻辑）
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

    hb_script_t get_script(uint32_t codepoint)
    {
        return get_cached_script(codepoint);
    }

    // 为单个字符选择字体
    const FontInfo *select_font(uint32_t codepoint)
    {
        hb_script_t script = get_cached_script(codepoint);

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

  private:
    std::vector<FontInfo> fonts_;
    hb_unicode_funcs_t *ufuncs_;
    hb_tag_t preferred_lang_tag_ = HB_TAG_NONE;
    std::unordered_map<uint32_t, hb_script_t> script_cache_;

    hb_script_t get_cached_script(uint32_t codepoint)
    {
        auto it = script_cache_.find(codepoint);
        if (it != script_cache_.end())
            return it->second;
        hb_script_t script = hb_unicode_script(ufuncs_, codepoint);
        script_cache_[codepoint] = script;
        return script;
    }

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
    return std::string(buf);
}

std::string tag_to_string(hb_tag_t tag)
{
    char buf[5] = {0};
    hb_tag_to_string(tag, buf);
    return std::string(buf);
}

// UTF-8 → UTF-32 转换（使用 utf8proc）
std::u32string utf8_to_utf32(const std::string &utf8)
{
    std::u32string result;
    const utf8proc_uint8_t *data =
        reinterpret_cast<const utf8proc_uint8_t *>(utf8.data());
    utf8proc_ssize_t len = utf8.size();
    utf8proc_int32_t cp;
    utf8proc_ssize_t pos = 0;
    while (pos < len)
    {
        utf8proc_ssize_t bytes = utf8proc_iterate(data + pos, len - pos, &cp);
        if (bytes < 0)
        {
            std::println(stderr, "UTF-8 decoding error at {}", pos);
            break;
        }
        result.push_back(static_cast<char32_t>(cp));
        pos += bytes;
    }
    return result;
}

// 将单个码点转换为 UTF-8 字符串
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
        return 2; // 110xxxxx
    else if ((c >> 4) == 0x0E)
        return 3; // 1110xxxx
    else if ((c >> 3) == 0x1E)
        return 4; // 11110xxx
    else
        return 1; // 无效序列，保守处理为 1 字节
}
// 新增：解码当前 UTF-8 字符，返回码点，并移动指针
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

// -----------------------------------------------------------------------------
// 主函数：集成字体选择与 BIDI 处理
// -----------------------------------------------------------------------------
int main()
{
    std::println("utf8proc version: {}", utf8proc_version());

    // 1. 扫描字体
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

    // 2. 创建匹配器，设置首选语言（如简体中文）
    FontMatcher matcher(std::move(fonts));
    matcher.set_preferred_language("zh-CN");
    hb_tag_t preferred_tag = matcher.preferred_lang_tag();
    std::println("Preferred language tag: {}",
                 preferred_tag != HB_TAG_NONE ? tag_to_string(preferred_tag) : "none");

    // 3. 测试文本（原始逻辑顺序）
    constexpr auto *rawText = u8"یہ ایک )car( ہے۔ 中 A é ß あ ア 한 ع א प ก ဍ Ꮳ ∑ 😊";
    // std::println("\nLogical order: {}", std::u8string_view{rawText});

    // ===== UAX #15 正规化 =====
    utf8proc_uint8_t *normalized =
        utf8proc_NFC(reinterpret_cast<const utf8proc_uint8_t *>(rawText));
    if (!normalized)
    {
        std::println(stderr, "Normalization failed");
        return 1;
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

    // ===== BIDI 分析 (UAX #9) =====
    SBCodepointSequence seq = {SBStringEncodingUTF8, (void *)bidiText, byte_len};
    SBAlgorithmRef algo = SBAlgorithmCreate(&seq);
    SBParagraphRef para =
        SBAlgorithmCreateParagraph(algo, 0, INT32_MAX, SBLevelDefaultLTR);
    SBLineRef line = SBParagraphCreateLine(para, 0, SBParagraphGetLength(para));
    SBUInteger runCount = SBLineGetRunCount(line);
    const SBRun *runs = SBLineGetRunsPtr(line);

    // ===== 镜像映射 =====
    std::vector<uint32_t> mirror(char_count, 0);
    SBMirrorLocatorRef mirrorLocator = SBMirrorLocatorCreate();
    SBMirrorLocatorLoadLine(mirrorLocator, line, (void *)bidiText);
    const SBMirrorAgent *agent = SBMirrorLocatorGetAgent(mirrorLocator);
    while (SBMirrorLocatorMoveNext(mirrorLocator))
    {
        SBUInteger byte_idx = agent->index;
        auto it = std::lower_bound(byte_offsets.begin(), byte_offsets.end(), byte_idx);
        if (it != byte_offsets.end() && *it == byte_idx)
        {
            mirror[it - byte_offsets.begin()] = static_cast<uint32_t>(agent->mirror);
        }
    }

    // ===== 获取每个字符的脚本 (UAX #24) =====
    std::vector<hb_script_t> char_scripts(char_count, HB_SCRIPT_INVALID);
    {
        SBScriptLocatorRef scriptLocator = SBScriptLocatorCreate();
        SBCodepointSequence seq_script = {SBStringEncodingUTF32,
                                          (void *)codepoints.data(), char_count};
        SBScriptLocatorLoadCodepoints(scriptLocator, &seq_script);
        const SBScriptAgent *scriptAgent = SBScriptLocatorGetAgent(scriptLocator);
        while (SBScriptLocatorMoveNext(scriptLocator))
        {
            uint32_t scriptTag = SBScriptGetUnicodeTag(scriptAgent->script);
            hb_script_t script = (scriptTag != 0) ? hb_script_from_iso15924_tag(scriptTag)
                                                  : HB_SCRIPT_INVALID;
            for (SBUInteger k = 0; k < scriptAgent->length; ++k)
            {
                char_scripts[scriptAgent->offset + k] = script;
            }
        }
        SBScriptLocatorRelease(scriptLocator);
    }

    // ===== 按 shaping runs 输出并选择字体 =====
    std::println("\nShaping runs (direction, script, language, font):");
    for (SBUInteger i = 0; i < runCount; ++i)
    {
        const SBRun &run = runs[i];
        auto start_it =
            std::lower_bound(byte_offsets.begin(), byte_offsets.end(), run.offset);
        auto end_it = std::lower_bound(byte_offsets.begin(), byte_offsets.end(),
                                       run.offset + run.length);
        size_t run_start = start_it - byte_offsets.begin();
        size_t run_end = end_it - byte_offsets.begin();

        size_t sub_start = run_start;
        while (sub_start < run_end)
        {
            hb_script_t current_script = char_scripts[sub_start];
            if (current_script == HB_SCRIPT_INVALID)
                current_script = HB_SCRIPT_LATIN; // 默认

            size_t sub_end = sub_start + 1;
            while (sub_end < run_end && char_scripts[sub_end] == current_script)
                ++sub_end;

            // 确定该 run 使用的语言（示例：根据脚本映射）
            hb_language_t lang = nullptr;
            if (current_script == HB_SCRIPT_ARABIC)
                lang = hb_language_from_string("ar", -1);
            else if (current_script == HB_SCRIPT_HEBREW)
                lang = hb_language_from_string("he", -1);
            else if (current_script == HB_SCRIPT_LATIN)
                lang = hb_language_from_string("en", -1);
            else if (current_script == HB_SCRIPT_HAN)
                lang = hb_language_from_string("zh", -1);
            else if (current_script == HB_SCRIPT_CYRILLIC)
                lang = hb_language_from_string("ru", -1);
            if (!lang)
                lang = hb_language_get_default();

            // 选择字体（使用 run 内第一个字符）
            uint32_t first_cp = codepoints[sub_start];
            const FontInfo *font = matcher.select_font(first_cp);
            std::string font_name =
                font ? fs::path(font->file_path).filename().string() : "NOT_FOUND";

            // 输出运行信息
            uint32_t script_tag = hb_script_to_iso15924_tag(current_script);
            std::println("  Run (direction {}, script {}{}{}{}, language {}, font {}): "
                         "logical indices [{}, {})",
                         (run.level % 2) ? "RTL" : "LTR",
                         static_cast<char>((script_tag >> 24) & 0xFF),
                         static_cast<char>((script_tag >> 16) & 0xFF),
                         static_cast<char>((script_tag >> 8) & 0xFF),
                         static_cast<char>((script_tag) & 0xFF),
                         hb_language_to_string(lang), font_name, sub_start, sub_end);

            sub_start = sub_end;
        }
    }

    // ===== 可选：视觉顺序输出 =====
    printf("\nVisual order: ");
    for (SBUInteger i = 0; i < runCount; ++i)
    {
        const SBRun &run = runs[i];
        auto start_it =
            std::lower_bound(byte_offsets.begin(), byte_offsets.end(), run.offset);
        auto end_it = std::lower_bound(byte_offsets.begin(), byte_offsets.end(),
                                       run.offset + run.length);
        size_t start = start_it - byte_offsets.begin();
        size_t end = end_it - byte_offsets.begin();
        if (run.level % 2)
        { // RTL
            for (size_t j = end; j > start; --j)
            {
                uint32_t cp = codepoints[j - 1];
                if (mirror[j - 1])
                    cp = mirror[j - 1];
                std::print("{}", cp_to_utf8(cp));
            }
        }
        else
        {
            for (size_t j = start; j < end; ++j)
            {
                uint32_t cp = codepoints[j];
                if (mirror[j])
                    cp = mirror[j];
                std::print("{}", cp_to_utf8(cp));
            }
        }
    }
    std::println("");

    // ===== 追加：基于 UAX#24 脚本的逐字符字体选择表格 =====
    std::println("\n=== Font selection results (using UAX#24 scripts from BIDI) ===");
    std::println("{:<10} {:<4} {:<12} {:<20} {:<10} {}", "U+", "Char", "Script", "Font",
                 "LangMatch", "ScriptMatch");

    for (size_t i = 0; i < char_count; ++i)
    {
        uint32_t cp = codepoints[i];
        auto font = matcher.select_font(cp);
        // NOTE: 不可靠。不改使用BIDI的脚本
        //  hb_script_t script = char_scripts[i]; // 直接使用 BIDI 分析得到的脚本
        hb_script_t script =
            matcher.get_script(cp); // 使用 HarfBuzz 脚本，而非 char_scripts[i]
        std::string script_str = script_to_string(script);
        std::string char_str = cp_to_utf8(cp);
        std::string font_path;
        bool has_lang = false;
        bool has_script = false;

        if (font)
        {
            font_path = font->file_path;
            has_lang = (preferred_tag != HB_TAG_NONE &&
                        font->lang_tags.count(preferred_tag) != 0);
            has_script = (font->scripts.count(script) != 0);
        }
        else
        {
            font_path = "NOT SUPPORTED";
        }

        std::string short_name = fs::path(font_path).filename().string();
        std::println("U+{:06X}  {:<4} {:<12} {:<20} {:<10} {}", static_cast<uint32_t>(cp),
                     char_str, script_str, short_name, has_lang ? "Yes" : "No",
                     has_script ? "Yes" : "No");
    }

    // 释放资源
    SBMirrorLocatorRelease(mirrorLocator);
    SBLineRelease(line);
    SBParagraphRelease(para);
    SBAlgorithmRelease(algo);
    free(normalized);
    return 0;
}