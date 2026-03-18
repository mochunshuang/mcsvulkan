#include <hb.h>
#include <hb-ft.h>
#include <hb-ot.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <utf8proc.h>

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
                            // 获取所有脚本标签
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

                            // 对每个脚本，获取其语言标签
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
    // 构造函数：接收扫描好的字体列表
    explicit FontMatcher(std::vector<FontInfo> fonts)
        : fonts_(std::move(fonts)), ufuncs_(hb_unicode_funcs_get_default())
    {
    }

    // 设置用户首选语言（BCP 47 格式，如 "zh-CN"）
    void set_preferred_language(const std::string &bcp47)
    {
        preferred_lang_tag_ = language_to_ot_tag(bcp47);
    }

    // 获取首选语言标签
    hb_tag_t preferred_lang_tag() const
    {
        return preferred_lang_tag_;
    }

    // 获取字符的脚本（公共接口）
    hb_script_t get_script(uint32_t codepoint)
    {
        return get_cached_script(codepoint);
    }

    // 为单个 Unicode 码点选择最佳字体
    // 返回指向字体的指针，若无则返回 nullptr
    const FontInfo *select_font(uint32_t codepoint)
    {
        // 0. 缓存脚本
        hb_script_t script = get_cached_script(codepoint);

        // 1. 首选语言优先：寻找支持该字符且带有首选语言标签的字体
        if (preferred_lang_tag_ != HB_TAG_NONE)
        {
            for (const auto &font : fonts_)
            {
                if (hb_set_has(font.unicode_set, codepoint) &&
                    font.lang_tags.count(preferred_lang_tag_) != 0) // 修正 bool 转换
                {
                    return &font;
                }
            }
        }

        // 2. 脚本匹配：寻找支持该字符且支持该脚本的字体
        for (const auto &font : fonts_)
        {
            if (hb_set_has(font.unicode_set, codepoint) &&
                font.scripts.count(script) != 0) // 修正 bool 转换
            {
                return &font;
            }
        }

        // 3. 其他支持该字符的字体（通用回退）
        for (const auto &font : fonts_)
        {
            if (hb_set_has(font.unicode_set, codepoint))
            {
                return &font;
            }
        }

        // 4. 没有字体支持该字符
        return nullptr;
    }

  private:
    std::vector<FontInfo> fonts_;
    hb_unicode_funcs_t *ufuncs_;                // 全局 Unicode 函数集
    hb_tag_t preferred_lang_tag_ = HB_TAG_NONE; // 首选语言标签

    // 脚本缓存（避免重复调用 hb_unicode_script）
    std::unordered_map<uint32_t, hb_script_t> script_cache_;

    // 获取字符的脚本（带缓存）
    hb_script_t get_cached_script(uint32_t codepoint)
    {
        auto it = script_cache_.find(codepoint);
        if (it != script_cache_.end())
            return it->second;

        hb_script_t script = hb_unicode_script(ufuncs_, codepoint);
        script_cache_[codepoint] = script;
        return script;
    }

    // 将 BCP 47 语言标签转换为 OpenType 语言标签（示例，可扩展）
    static hb_tag_t language_to_ot_tag(const std::string &bcp47)
    {
        // 这里只列出常用语言，实际可维护一个完整映射表
        if (bcp47 == "zh-CN" || bcp47 == "zh-Hans")
            return HB_TAG('Z', 'H', 'S', ' ');
        if (bcp47 == "zh-TW" || bcp47 == "zh-Hant")
            return HB_TAG('Z', 'H', 'T', ' ');
        if (bcp47 == "ja")
            return HB_TAG('J', 'A', 'N', ' ');
        if (bcp47 == "ko")
            return HB_TAG('K', 'O', 'R', ' ');
        if (bcp47 == "en")
            return HB_TAG('E', 'N', 'G', ' ');
        if (bcp47 == "fr")
            return HB_TAG('F', 'R', 'A', ' ');
        if (bcp47 == "de")
            return HB_TAG('D', 'E', 'U', ' ');
        if (bcp47 == "ru")
            return HB_TAG('R', 'U', 'S', ' ');
        // 其他返回空标签
        return HB_TAG_NONE;
    }
};

// -----------------------------------------------------------------------------
// 辅助函数：将 hb_script_t 转换为字符串（通过 ISO 15924 标签）
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

// -----------------------------------------------------------------------------
// 辅助函数：将 hb_tag_t 转换为字符串
// -----------------------------------------------------------------------------
std::string tag_to_string(hb_tag_t tag)
{
    char buf[5] = {0};
    hb_tag_to_string(tag, buf);
    return std::string(buf);
}

// -----------------------------------------------------------------------------
// UTF-8 → UTF-32 转换（使用 utf8proc_iterate 逐个解析）
// -----------------------------------------------------------------------------
std::u32string utf8_to_utf32(const std::string &utf8)
{
    std::u32string result;
    const utf8proc_uint8_t *data =
        reinterpret_cast<const utf8proc_uint8_t *>(utf8.data());
    utf8proc_ssize_t len = utf8.size();
    utf8proc_int32_t codepoint;
    utf8proc_ssize_t pos = 0;

    while (pos < len)
    {
        utf8proc_ssize_t bytes = utf8proc_iterate(data + pos, len - pos, &codepoint);
        if (bytes < 0)
        {
            // 解码错误，可以记录并继续，或终止
            std::println(stderr, "UTF-8 decoding error at position {}", pos);
            break;
        }
        result.push_back(static_cast<char32_t>(codepoint));
        pos += bytes;
    }
    return result;
}

// -----------------------------------------------------------------------------
// 主函数（演示）
// -----------------------------------------------------------------------------
int main()
{
    // 检查 utf8proc 版本（可选）
    std::println("utf8proc version: {}", utf8proc_version());

    // 1. 扫描系统字体目录（可根据操作系统调整）
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

    // 2. 创建匹配器，设置首选语言为简体中文
    FontMatcher matcher(std::move(fonts));
    matcher.set_preferred_language("zh-CN");
    hb_tag_t preferred_tag = matcher.preferred_lang_tag();
    std::string preferred_tag_str =
        (preferred_tag != HB_TAG_NONE) ? tag_to_string(preferred_tag) : "none";

    std::println("Preferred language tag: {}", preferred_tag_str);

    // 3. 测试字符集（包含各种文字）
    std::string test_text = "中 A é ß あ ア 한 ع א प ก ဍ Ꮳ ∑ 😊";
    auto utf32 = utf8_to_utf32(test_text);

    if (utf32.empty())
    {
        std::println(stderr, "UTF-32 conversion failed or empty string.");
        return 1;
    }

    std::println("\n=== Font selection results ===");
    std::println("{:<10} {:<4} {:<12} {:<20} {:<10} {}", "U+", "Char", "Script", "Font",
                 "LangMatch", "ScriptMatch");

    for (char32_t ch : utf32)
    {
        auto font = matcher.select_font(ch);
        hb_script_t script = matcher.get_script(ch);
        std::string script_str = script_to_string(script);

        // 将字符转换为 UTF-8 字符串用于显示
        std::string_view char_str;
        char utf8_buf[8] = {0};
        utf8proc_ssize_t len =
            utf8proc_encode_char(static_cast<utf8proc_int32_t>(ch),
                                 reinterpret_cast<utf8proc_uint8_t *>(utf8_buf));
        if (len > 0)
        {
            char_str = utf8_buf;
        }
        else
        {
            char_str = "?";
        }

        std::string font_path;
        bool has_lang = false;
        bool has_script = false;

        if (font)
        {
            font_path = font->file_path;
            // 检查字体是否包含首选语言标签
            has_lang = (preferred_tag != HB_TAG_NONE &&
                        font->lang_tags.count(preferred_tag) != 0);
            // 检查字体是否包含期望脚本
            has_script = (font->scripts.count(script) != 0);
        }
        else
        {
            font_path = "NOT SUPPORTED";
        }

        // 简化路径显示（只显示文件名）
        std::string short_name = fs::path(font_path).filename().string();

        std::println("U+{:06X}  {:<4} {:<12} {:<20} {:<10} {}", static_cast<uint32_t>(ch),
                     char_str, script_str, short_name, has_lang ? "Yes" : "No",
                     has_script ? "Yes" : "No");
    }

    return 0;
}