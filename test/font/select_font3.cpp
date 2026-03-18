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
    [[nodiscard]] const std::vector<FontInfo> &get_fonts() const
    {
        return fonts_;
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
    return {buf};
}

std::string tag_to_string(hb_tag_t tag)
{
    char buf[5] = {0};
    hb_tag_to_string(tag, buf);
    return {buf};
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

// 将单个码点转换为转义字符串（用于安全显示）
std::string cp_to_escaped(char32_t cp)
{
    // C0 控制字符（U+0000 - U+001F）和 DEL（U+007F）
    if (cp < 0x20 || cp == 0x7F)
    {
        static const char *c0_names[] = {
            "NUL", "SOH", "STX", "ETX", "EOT", "ENQ", "ACK", "BEL", "BS",  "HT",  "LF",
            "VT",  "FF",  "CR",  "SO",  "SI",  "DLE", "DC1", "DC2", "DC3", "DC4", "NAK",
            "SYN", "ETB", "CAN", "EM",  "SUB", "ESC", "FS",  "GS",  "RS",  "US"};
        if (cp < 0x20)
            return std::string("\\") + c0_names[cp];
        else // DEL
            return "\\DEL";
    }
    // C1 控制字符（U+0080 - U+009F）
    if (cp >= 0x80 && cp <= 0x9F)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "\\u%04X", static_cast<unsigned>(cp));
        return buf;
    }
    // 常用格式控制符
    switch (cp)
    {
    case 0x200B:
        return "\\u200B"; // 零宽空格
    case 0x200C:
        return "\\u200C"; // 零宽非连接符
    case 0x200D:
        return "\\u200D"; // 零宽连接符
    case 0x2066:
        return "\\u2066"; // LRI
    case 0x2067:
        return "\\u2067"; // RLI
    case 0x2068:
        return "\\u2068"; // FSI
    case 0x2069:
        return "\\u2069"; // PDI
    case 0x202A:
        return "\\u202A"; // LRE
    case 0x202B:
        return "\\u202B"; // RLE
    case 0x202C:
        return "\\u202C"; // PDF
    case 0x202D:
        return "\\u202D"; // LRO
    case 0x202E:
        return "\\u202E"; // RLO
    case 0x061C:
        return "\\u061C"; // ALM
        // 可根据需要补充其他
    }
    // 其他可见字符直接 UTF-8 编码
    return cp_to_utf8(cp);
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

    // 测试用例数组：包含 12 个 UTF-8 字符串
    constexpr std::array<const char8_t *const, 16> test_strings = {
        // 基础功能测试（12个）
        u8"Hello, world!",                 // 纯英文
        u8"السلام عليكم",                  // 纯阿拉伯语
        u8"Hello in Arabic is مرحبا",      // 英文 + 阿拉伯语混合
        u8"car (سيارة) fast",              // 带括号的 BIDI 测试
        u8"देवनागरी தமிழ் ಕನ್ನಡ",             // 多种印度系文字
        u8"汉字 ひらがな カタカナ 한국어", // CJK 混合
        u8"👨‍👩‍👧‍👦",     // 表情符号（含 ZWJ 序列）
        u8"x² + y² = z²",                  // 数学符号和上标
        u8"عدد 123 و 456",                 // 阿拉伯语中的数字
        u8"zero\u200Bwidth",               // 含零宽空格
        u8"   ",                           // 仅空白字符
        u8"𑀪𑀸𑀭𑀢 (भारत) - India",            // 婆罗米文 + 天城文 + 英文

        // 边界/健壮性测试（新增4个）
        u8"",                                    // 空字符串
        u8" \t\n\r\v\f",                         // 仅空白控制字符（空格、制表、换行等）
        u8"\u2066Hello\u2069",                   // 包含 Bidi 隔离符（LRI 和 PDI）
        u8"∫ f(x) dx + ∑_{n=1}^{∞} 1/n² = π²/6", // 数学符号、下标、上标混合
    };
    constexpr auto index = 13;
    constexpr auto *rawText = test_strings[index];
    // constexpr auto *rawText = u8"یہ ایک )car( ہے۔ 中 A é ß あ ア 한 ع א प ก ဍ Ꮳ ∑ 😊";

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

    // ===== 使用 HarfBuzz 获取每个字符的脚本 (UAX #24) =====
    std::vector<hb_script_t> char_scripts(char_count);
    for (size_t i = 0; i < char_count; ++i)
    {
        char_scripts[i] = matcher.get_script(codepoints[i]);
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

    // ===== 详细的视觉顺序调试输出 =====
    std::println("\n=== Visual order detailed (logical index -> visual index) ===");
    std::println("{:<12} {:<12} {:<8} {:<8} {:<6} {:<10}", "Logical idx", "Visual idx",
                 "Code", "Mirror", "Mirrored?", "Char");

    size_t visual_idx = 0;
    for (SBUInteger i = 0; i < runCount; ++i)
    {
        const SBRun &run = runs[i];
        auto start_it =
            std::lower_bound(byte_offsets.begin(), byte_offsets.end(), run.offset);
        auto end_it = std::lower_bound(byte_offsets.begin(), byte_offsets.end(),
                                       run.offset + run.length);
        size_t start = start_it - byte_offsets.begin();
        size_t end = end_it - byte_offsets.begin();

        if (run.level % 2) // RTL
        {
            for (size_t j = end; j > start; --j)
            {
                size_t logical = j - 1;
                uint32_t cp = codepoints[logical];
                uint32_t mirrored = mirror[logical];
                uint32_t final_cp = mirrored ? mirrored : cp;
                std::println("{:<12} {:<12} U+{:04X}   U+{:04X}   {:<6}   {}", logical,
                             visual_idx, cp, mirrored ? mirrored : 0,
                             mirrored ? "Yes" : "No", cp_to_utf8(final_cp));
                ++visual_idx;
            }
        }
        else // LTR
        {
            for (size_t j = start; j < end; ++j)
            {
                size_t logical = j;
                uint32_t cp = codepoints[logical];
                uint32_t mirrored = mirror[logical];
                uint32_t final_cp = mirrored ? mirrored : cp;
                std::println("{:<12} {:<12} U+{:04X}   U+{:04X}   {:<6}   {}", logical,
                             visual_idx, cp, mirrored ? mirrored : 0,
                             mirrored ? "Yes" : "No", cp_to_utf8(final_cp));
                ++visual_idx;
            }
        }
    }

    // ===== 追加：基于 UAX#24 脚本的逐字符字体选择表格 =====
    std::println("\n=== Font selection results (using UAX#24 scripts from HarfBuzz) ===");
    std::println("{:<10} {:<4} {:<12} {:<20} {:<10} {}", "U+", "Char", "Script", "Font",
                 "LangMatch", "ScriptMatch");

    for (size_t i = 0; i < char_count; ++i)
    {
        uint32_t cp = codepoints[i];
        auto font = matcher.select_font(cp);
        hb_script_t script = char_scripts[i]; // 直接使用 HarfBuzz 脚本
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

    // NOTE: BIDI（双向文本）与字形无关。BIDI 负责处理 Unicode
    // 字符的视觉顺序和方向（如阿拉伯语从右向左显示），它仅对文本中的字符进行逻辑重排和镜像映射，而不涉及字符如何被渲染为字形。
    // NOTE:shaping 失败（即输出 .notdef 字形）本质上就是字形回退的问题。
    /*
这两种回退在本质上都是寻找能够正确渲染该字符的字体，但在不同阶段触发，且目标略有不同：

1. Unicode 选择阶段回退（select_font 返回 nullptr）
触发条件：没有任何字体在 unicode_set 中声明支持该字符。

回退目标：找到一个至少声明支持该字符的字体（即 unicode_set 包含该码点）。

最终兜底：如果所有字体都不支持，则没有任何字体可选。此时需要在渲染时使用替代符号（如 �、□
或 ?）来表示缺失字符。这个替代符号通常由一个全局后备字体（如包含 Unicode
通用符号的字体）提供，或者由应用程序自行绘制一个占位符图形。

2. Shaping 阶段字形回退（glyph_id == 0）
触发条件：字体虽然声明支持该字符（unicode_set
包含），但实际字体文件中缺少对应的字形（可能字体损坏，或该字符未被正确映射）。

回退目标：寻找另一个能实际提供该字符字形的字体（即不仅 unicode_set 包含，且 hb_shape
后不产生 .notdef）。

最终兜底：如果所有候选字体尝试后仍产生 .notdef，则同样需要回到替代符号。
*/
    //-----------------------
    {
        /**
         * @brief
visual_codepoints：视觉顺序的码点列表（已应用镜像），用于断行和确定显示顺序。
visual_to_logical：视觉索引到逻辑索引的映射，用于将视觉位置关联回逻辑属性（如脚本、字体）。
bidi_runs：每个逻辑运行的区间和方向，用于在 shaping
时为每个逻辑片段设置正确的缓冲区方向，并帮助后续布局。
         *
         */
        // ===== 提取 BIDI 结果以供后续使用（断行、shaping）=====
        struct BidiRunInfo
        {
            size_t logical_start; // 逻辑起始索引
            size_t logical_end;   // 逻辑结束索引（不包含）
            SBLevel level;        // BIDI 级别（偶数=LTR，奇数=RTL）
        };
        std::vector<BidiRunInfo> bidi_runs;
        std::vector<uint32_t> visual_codepoints; // 视觉顺序的码点（已镜像）
        std::vector<size_t> visual_to_logical;   // 视觉索引 -> 逻辑索引

        // 重新遍历 runs，构建交付数据
        for (SBUInteger i = 0; i < runCount; ++i)
        {
            const SBRun &run = runs[i];
            auto start_it =
                std::lower_bound(byte_offsets.begin(), byte_offsets.end(), run.offset);
            auto end_it = std::lower_bound(byte_offsets.begin(), byte_offsets.end(),
                                           run.offset + run.length);
            size_t logical_start = start_it - byte_offsets.begin();
            size_t logical_end = end_it - byte_offsets.begin();

            // 记录 BIDI 运行信息（逻辑区间 + 方向）
            bidi_runs.push_back({logical_start, logical_end, run.level});

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

        // ===== 打印交付信息（供 HarfBuzz 和 libunibreak 使用）=====
        std::println("\n=== Delivery data for line breaking (libunibreak) and shaping "
                     "(HarfBuzz) ===");

        // 1. 视觉顺序码点列表（已镜像）
        std::println("Visual order codepoints ({} chars):", visual_codepoints.size());
        for (size_t vi = 0; vi < visual_codepoints.size(); ++vi)
        {
            std::print(" U+{:04X}", visual_codepoints[vi]);
            if ((vi + 1) % 10 == 0)
                std::println("");
        }
        std::println("");

        // 2. 视觉顺序字符串
        std::string visual_str;
        for (uint32_t cp : visual_codepoints)
            visual_str += cp_to_utf8(cp);
        std::println("Visual string: \"{}\"", visual_str);

        // 3.
        // 详细映射表（视觉索引、逻辑索引、原始码点、镜像码点、最终码点、字符、所在运行方向）
        std::println("\nDetailed mapping (visual index -> logical):");
        std::println("{:>4} {:>10} {:>8} {:>8} {:>8} {:>4} {:>6} {:>10}", "Vis", "Log",
                     "Orig", "Mirror", "Final", "Char", "Level", "Direction");
        for (size_t vi = 0; vi < visual_codepoints.size(); ++vi)
        {
            size_t logical = visual_to_logical[vi];
            uint32_t orig = codepoints[logical];
            uint32_t mirr = mirror[logical];
            uint32_t final = visual_codepoints[vi];
            std::string ch = cp_to_utf8(final);
            // 查找该字符所属的 run 层级
            uint8_t level = 0;
            for (const auto &run : bidi_runs)
            {
                if (logical >= run.logical_start && logical < run.logical_end)
                {
                    level = run.level;
                    break;
                }
            }
            std::string dir = (level % 2) ? "RTL" : "LTR";
            std::println("{:4} {:10} U+{:04X}   U+{:04X}   U+{:04X}   {:4}   {:6} {:10}",
                         vi, logical, orig, mirr ? mirr : 0, final, ch, level, dir);
        }

        // 4. BIDI 运行信息（逻辑区间、方向）
        std::println("\nBidi runs (logical intervals):");
        std::println("{:>4} {:>12} {:>12} {:>6} {:>10}", "Run", "Logical start",
                     "Logical end", "Level", "Direction");
        for (size_t i = 0; i < bidi_runs.size(); ++i)
        {
            const auto &run = bidi_runs[i];
            std::string dir = (run.level % 2) ? "RTL" : "LTR";
            std::println("{:4} {:12} {:12} {:6} {:10}", i, run.logical_start,
                         run.logical_end, run.level, dir);
        }

        // 此时 visual_codepoints、visual_to_logical、bidi_runs 已完整构建，
        // 可供后续断行（libunibreak）和 shaping（HarfBuzz）使用。
        // 可以安全释放 BIDI 对象（algo, para, line, mirrorLocator）。
    }

    // 释放资源
    SBMirrorLocatorRelease(mirrorLocator);
    SBLineRelease(line);
    SBParagraphRelease(para);
    SBAlgorithmRelease(algo);
    free(normalized);

    {
        // ===== 测试字体回退（模拟 .notdef 场景）=====
        std::println("\n=== Testing font fallback for .notdef glyphs ===");

        // 选择一个测试字符，例如 U+4E2D "中"
        uint32_t test_cp = 0x4E2D;

        // 从字体列表中找出一个肯定不支持汉字的字体（即 scripts 中不含 HB_SCRIPT_HAN）
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

            // 初始化 FreeType 并创建 HarfBuzz 字体对象
            FT_Library ft_lib;
            if (FT_Init_FreeType(&ft_lib) == 0)
            {
                FT_Face ft_face;
                if (FT_New_Face(ft_lib, font_without_han->file_path.c_str(), 0,
                                &ft_face) == 0)
                {
                    hb_face_t *hb_face = hb_ft_face_create(ft_face, nullptr);
                    hb_font_t *hb_font = hb_font_create(hb_face);

                    // 创建 HarfBuzz 缓冲区并添加测试字符
                    hb_buffer_t *buf = hb_buffer_create();
                    hb_buffer_add_utf32(buf, &test_cp, 1, 0, 1);
                    hb_buffer_set_script(buf, HB_SCRIPT_HAN);
                    hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
                    hb_buffer_set_language(buf, hb_language_from_string("zh", -1));

                    // 执行 shaping
                    hb_shape(hb_font, buf, nullptr, 0);

                    // 检查 shaping 结果中是否有 .notdef (glyph_id == 0)
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

                        // 回退选择：排除当前字体，再次调用 select_font
                        // 由于 select_font
                        // 目前不支持排除列表，这里手动模拟：遍历字体并跳过当前字体
                        const FontInfo *fallback = nullptr;
                        hb_script_t script = matcher.get_script(test_cp);
                        for (const auto &f : matcher.get_fonts())
                        {
                            if (&f == font_without_han)
                                continue; // 跳过已失败的字体
                            if (hb_set_has(f.unicode_set, test_cp))
                            {
                                // 按原有优先级：语言匹配、脚本匹配、仅支持字符
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
                                fallback = &f; // 至少支持字符
                                break;
                            }
                        }

                        if (fallback)
                        {
                            std::println(
                                "-> Fallback font selected: '{}'",
                                fs::path(fallback->file_path).filename().string());
                            // 可选：用后备字体再次 shaping 验证
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

                    // 清理资源
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