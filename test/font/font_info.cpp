#include <hb.h>
#include <hb-ft.h>
#include <hb-ot.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <print>
#include <filesystem>
#include <vector>
#include <string>
#include <set>
#include <map>
#include <algorithm>

namespace fs = std::filesystem;

// ---------- 辅助函数：将 hb_tag_t 转换为字符串 ----------
static std::string tag_to_string(hb_tag_t tag)
{
    char buf[5] = {0};
    hb_tag_to_string(tag, buf);
    return std::string(buf);
}

// ---------- 字体信息结构 ----------
struct FontInfo
{
    std::string file_path;        // 字体文件路径
    std::string family_name;      // 族名（可选）
    std::set<hb_tag_t> lang_tags; // 支持的语言标签
    hb_set_t *unicode_set;        // 支持的 Unicode 码点集合（需要释放）

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
          family_name(std::move(other.family_name)),
          lang_tags(std::move(other.lang_tags)), unicode_set(other.unicode_set)
    {
        other.unicode_set = nullptr;
    }
    FontInfo &operator=(FontInfo &&other) noexcept
    {
        if (this != &other)
        {
            file_path = std::move(other.file_path);
            family_name = std::move(other.family_name);
            lang_tags = std::move(other.lang_tags);
            std::swap(unicode_set, other.unicode_set);
        }
        return *this;
    }
};

// ---------- 扫描系统字体，收集信息 ----------
std::vector<FontInfo> scan_fonts(const std::string &directory)
{
    std::vector<FontInfo> fonts;

    FT_Library ft_library;
    if (FT_Init_FreeType(&ft_library))
    {
        std::println(stderr, "FT_Init_FreeType failed");
        return fonts;
    }

    for (const auto &entry : fs::directory_iterator(directory))
    {
        if (!entry.is_regular_file())
            continue;
        auto path = entry.path().string();
        std::string ext = entry.path().extension().string();
        if (ext != ".ttf" && ext != ".ttc" && ext != ".otf" && ext != ".otc")
            continue;

        int face_index = 0;
        while (true)
        {
            FT_Face ft_face;
            FT_Error error = FT_New_Face(ft_library, path.c_str(), face_index, &ft_face);
            if (error)
            {
                // 对于 TTF，索引 0 成功，索引 1 会失败，此时停止循环
                break;
            }

            // 创建 hb_face
            hb_face_t *hb_face = hb_ft_face_create(ft_face, nullptr);
            if (hb_face)
            {
                FontInfo info;
                info.file_path = path;
                info.family_name = ft_face->family_name ? ft_face->family_name : "";

                // 收集语言标签（使用 GSUB/GPOS）
                hb_tag_t tables[] = {HB_OT_TAG_GSUB, HB_OT_TAG_GPOS};
                for (hb_tag_t table : tables)
                {
                    unsigned int script_count = hb_ot_layout_table_get_script_tags(
                        hb_face, table, 0, nullptr, nullptr);
                    std::vector<hb_tag_t> scripts(script_count);
                    hb_ot_layout_table_get_script_tags(hb_face, table, 0, &script_count,
                                                       scripts.data());

                    for (unsigned int i = 0; i < scripts.size(); ++i)
                    {
                        unsigned int lang_count = hb_ot_layout_script_get_language_tags(
                            hb_face, table, i, 0, nullptr, nullptr);
                        std::vector<hb_tag_t> langs(lang_count);
                        hb_ot_layout_script_get_language_tags(hb_face, table, i, 0,
                                                              &lang_count, langs.data());
                        info.lang_tags.insert(langs.begin(), langs.end());
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

    FT_Done_FreeType(ft_library);
    return fonts;
}

// ---------- 预设的默认字体列表（按优先级降序） ----------
static const std::vector<std::string> DEFAULT_FONTS = {
    "Microsoft YaHei",  // Windows 简体中文
    "PingFang SC",      // macOS 简体中文
    "Noto Sans CJK SC", // Linux/Android
    "SimHei",           // 老Windows
    "Arial",            // 英文通用
    "Segoe UI",         // Windows UI
    "Helvetica",        // macOS 英文
    "sans-serif"        // 通用族名（实际需映射）
};

// ---------- 将首选语言字符串（如 "zh-CN"）转换为语言标签 ----------
// 这里简单映射，可根据需要扩展
hb_tag_t language_to_tag(const std::string &lang)
{
    if (lang == "zh-CN" || lang == "zh-Hans")
        return HB_TAG('Z', 'H', 'S', ' ');
    if (lang == "zh-TW" || lang == "zh-Hant")
        return HB_TAG('Z', 'H', 'T', ' ');
    if (lang == "ja")
        return HB_TAG('J', 'A', 'N', ' ');
    if (lang == "ko")
        return HB_TAG('K', 'O', 'R', ' ');
    return HB_TAG_NONE;
}

// ---------- 判断字体是否属于默认字体列表（基于族名） ----------
bool is_default_font(const FontInfo &font, const std::string &family)
{
    // 简单比较族名（可能需要模糊匹配）
    return font.family_name == family;
}

// ---------- 查找能渲染指定字符的最佳字体 ----------
std::string find_best_font(uint32_t codepoint, const std::vector<FontInfo> &fonts,
                           hb_tag_t preferred_lang_tag)
{
    // 筛选出能渲染该字符的字体
    std::vector<const FontInfo *> capable_fonts;
    for (const auto &font : fonts)
    {
        if (hb_set_has(font.unicode_set, codepoint))
            capable_fonts.push_back(&font);
    }

    // 分层
    std::vector<const FontInfo *> layer1, layer2, layer3;

    for (const auto *font : capable_fonts)
    {
        // 层1：支持首选语言
        if (font->lang_tags.count(preferred_lang_tag))
        {
            layer1.push_back(font);
        }
        // 层2：属于默认字体列表
        else if (std::any_of(
                     DEFAULT_FONTS.begin(), DEFAULT_FONTS.end(),
                     [&](const std::string &def) { return is_default_font(*font, def); }))
        {
            layer2.push_back(font);
        }
        // 层3：其他
        else
        {
            layer3.push_back(font);
        }
    }

    // 在各层内部排序（这里简单按族名字母序，你也可以按其他规则）
    auto sort_by_name = [](const FontInfo *a, const FontInfo *b) {
        return a->family_name < b->family_name;
    };
    std::sort(layer1.begin(), layer1.end(), sort_by_name);
    std::sort(layer2.begin(), layer2.end(), sort_by_name);
    std::sort(layer3.begin(), layer3.end(), sort_by_name);

    // 依次返回第一个
    if (!layer1.empty())
        return layer1[0]->file_path;
    if (!layer2.empty())
        return layer2[0]->file_path;
    if (!layer3.empty())
        return layer3[0]->file_path;
    return ""; // 无字体
}

// ---------- 示例主函数 ----------
int main()
{
    // 1. 扫描字体目录（这里以 Windows 字体目录为例）
    std::string font_dir = "C:/Windows/Fonts";
    auto fonts = scan_fonts(font_dir);
    std::println("Scanned {} fonts.", fonts.size());

    // 2. 用户首选语言（例如简体中文）

    struct TestChar
    {
        uint32_t codepoint;
        std::string description;
    };

    // 定义测试字符集
    std::vector<TestChar> test_chars = {
        // 东亚文字
        {U'中', "CJK Unified Ideograph (Chinese)"},
        {U'文', "CJK Unified Ideograph (Chinese)"},
        {U'あ', "Hiragana (Japanese)"},
        {U'ア', "Katakana (Japanese)"},
        {U'한', "Hangul (Korean)"},
        {U'𠀀', "CJK Extension B (rare Chinese)"}, // U+20000

        // 拉丁字母及变体
        {U'A', "Latin Capital A"},
        {U'é', "Latin e with acute (French)"},
        {U'ß', "German sharp s"},
        {U'ñ', "Spanish n with tilde"},
        {U'ğ', "Turkish g with breve"},
        {U'Ø', "Danish O with stroke"},
        {U'Ł', "Polish L with stroke"},
        {U'č', "Czech c with caron"},
        {U'œ', "French ligature oe"},

        // 西里尔字母
        {U'А', "Cyrillic A (Russian)"},
        {U'Љ', "Cyrillic LJE (Serbian)"},
        {U'Ѣ', "Cyrillic Yat (Old Slavic)"},

        // 希腊字母
        {U'Ω', "Greek Omega"},
        {U'ϑ', "Greek theta symbol"},

        // 阿拉伯字母
        {U'ع', "Arabic Ain"},
        {U'غ', "Arabic Ghain"},
        {U'پ', "Arabic Pe (Persian/Urdu)"},
        {U'گ', "Arabic Gaf (Persian/Urdu)"},
        {U'٠', "Arabic-Indic digit zero"},

        // 希伯来字母
        {U'א', "Hebrew Alef"},
        {U'ת', "Hebrew Tav"},
        {U'װ', "Hebrew double vav (Yiddish)"},

        // 天城文（印地语）
        {U'प', "Devanagari Pa"},
        {U'ॐ', "Devanagari Om"},

        // 泰文
        {U'ก', "Thai ko kai"},
        {U'๙', "Thai digit nine"},

        // 缅甸文
        {U'ဍ', "Myanmar Dda"},

        // 高棉文
        {U'ក', "Khmer Ka"},

        // 格鲁吉亚文
        {U'ა', "Georgian Ani"},

        // 亚美尼亚文
        {U'Ա', "Armenian Ayb"},

        // 切罗基文
        {U'Ꮳ', "Cherokee Tsa"},

        // 埃塞俄比亚音节文字
        {U'አ', "Ethiopic Glottal A"},

        // 加拿大原住民音节文字
        {U'ᐃ', "Canadian Syllabics I"},

        // 数学符号
        {U'∑', "Summation sign"},
        {U'∞', "Infinity"},
        {U'∮', "Contour integral"},

        // 货币符号
        {U'€', "Euro sign"},
        {U'£', "Pound sign"},
        {U'¥', "Yen sign"},
        {U'₽', "Ruble sign"},

        // 标点符号
        {U'“', "Left double quotation mark"},
        {U'—', "Em dash"},

        // 箭头符号
        {U'→', "Rightwards arrow"},

        // 表情符号（单个码点）
        {U'😊', "Smiling face"},
        {U'👍', "Thumbs up"},
        {U'🏳',
         "White flag (part of rainbow flag)"}, // 旗帜通常由两个码点组成，这里仅测试基础码点
        {U'★', "Black star"},

        // BIDI 控制字符
        {U'‮', "Left-to-right mark"},
        {U'‫', "Right-to-left embedding"},

        // 古文字（可能需要特殊字体）
        {U'𐤀', "Phoenician aleph"},
        {U'𓂀', "Egyptian hieroglyph"},
    };

    // 用户首选语言（例如简体中文）
    std::string preferred_lang = "zh-CN";
    hb_tag_t preferred_tag = language_to_tag(preferred_lang);

    std::println("\n=== Testing font fallback for {} characters ===", test_chars.size());

    for (const auto &tc : test_chars)
    {
        std::string font_path = find_best_font(tc.codepoint, fonts, preferred_tag);
        std::println("U+{:06X} {:30} -> {}", tc.codepoint, tc.description,
                     font_path.empty() ? "NOT FOUND" : font_path);
    }

    return 0;
}