#include <hb.h>
#include <hb-ft.h>
#include <hb-ot.h>

#include <ft2build.h>
#include <iostream>
#include <memory>
#include FT_FREETYPE_H

#include <exception>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// 2. Library Initialization
struct freetype_version
{
    int major;
    int minor;
    int patch;
};

template <>
struct std::formatter<freetype_version>
{
    static constexpr auto parse(format_parse_context &ctx)
    {
        return ctx.begin();
    }
    static auto format(const freetype_version &ver, format_context &ctx)
    {
        return std::format_to(ctx.out(), "freetype_version: {}.{}.{}", ver.major,
                              ver.minor, ver.patch);
    }
};

struct freetype_loader
{
    constexpr operator bool() const noexcept
    {
        return value_ != nullptr;
    }
    constexpr auto &operator*() noexcept
    {
        return value_;
    }
    constexpr const auto &operator*() const noexcept
    {
        return value_;
    }

    [[nodiscard]] auto version() const noexcept
    {
        freetype_version ver{};
        FT_Library_Version(value_, &ver.major, &ver.minor, &ver.patch);
        return ver;
    }

    constexpr freetype_loader()
    {
        auto error = FT_Init_FreeType(&value_);
        if (error != FT_Err_Ok)
            throw std::runtime_error{
                "an error occurred during FT_Library initialization"};
        std::println("freetype_loader: init library: {} {}", static_cast<void *>(value_),
                     version());
    }

    freetype_loader(const freetype_loader &) = delete;
    constexpr freetype_loader(freetype_loader &&o) noexcept
        : value_{std::exchange(o.value_, {})}
    {
    }
    freetype_loader &operator=(const freetype_loader &) = delete;
    freetype_loader &operator=(freetype_loader &&o) noexcept
    {
        if (&o != this)
        {
            destroy();
            value_ = std::exchange(o.value_, {});
        }
        return *this;
    }

    constexpr void destroy() noexcept
    {
        if (value_ != nullptr)
        {
            FT_Done_FreeType(value_);
            value_ = nullptr;
        }
    }
    constexpr ~freetype_loader() noexcept
    {
        destroy();
    }

  private:
    FT_Library value_{};
};

// 3. Loading a Font Face
struct freetype_face
{
    constexpr operator bool() const noexcept
    {
        return value_ != nullptr;
    }
    constexpr auto &operator*() noexcept
    {
        return value_;
    }
    constexpr const auto &operator*() const noexcept
    {
        return value_;
    }

    freetype_face(const freetype_face &) = delete;
    freetype_face(freetype_face &&o) noexcept : value_{std::exchange(o.value_, {})} {}
    freetype_face &operator=(const freetype_face &) = delete;
    freetype_face &operator=(freetype_face &&o) noexcept
    {
        if (&o != this)
        {
            destroy();
            value_ = std::exchange(o.value_, {});
        }
        return *this;
    }

    // a. From a Font File
    freetype_face(FT_Library library, const std::string &fontPath, FT_Long face_index = 0)
    {
        auto error = FT_New_Face(library, fontPath.data(), face_index, &value_);
        if (error == FT_Err_Unknown_File_Format)
            throw std::runtime_error{"the font file could be opened and read, but it "
                                     "appears that its font format is unsupported"};
        if (error != FT_Err_Ok)
            throw std::runtime_error{"another error code means that the font file could "
                                     "not be opened or read, or that it is broken"};
    }

    // b. From Memory
    freetype_face(FT_Library library, const std::span<const FT_Byte> &bufferView,
                  FT_Long face_index = 0)
    {
        auto error = FT_New_Memory_Face(library, bufferView.data(),
                                        static_cast<FT_Long>(bufferView.size()),
                                        face_index, &value_);
        if (error != FT_Err_Ok)
            throw std::runtime_error{"init FT_Face From Memory error."};
    }

    constexpr void destroy() noexcept
    {
        if (value_ != nullptr)
        {
            FT_Done_Face(value_);
            value_ = nullptr;
        }
    }
    constexpr ~freetype_face() noexcept
    {
        destroy();
    }

  private:
    FT_Face value_{};
};

// 辅助函数：将 hb_tag_t 转换为字符串
static std::string tag_to_string(hb_tag_t tag)
{
    char buf[5] = {0};
    hb_tag_to_string(tag, buf);
    return std::string(buf);
}

// https://learn.microsoft.com/zh-cn/typography/opentype/spec/languagetags
// 语言标签到中文名称的映射（使用 switch-case，仅列出常见标签）
static const char *lang_tag_to_chinese_name(hb_tag_t tag)
{
    switch (tag)
    {
    // ----- 中文及东亚语言 -----
    case HB_TAG('Z', 'H', 'S', ' '):
        return "简体中文";
    case HB_TAG('Z', 'H', 'T', ' '):
        return "繁体中文";
    case HB_TAG('Z', 'H', 'H', ' '):
        return "香港中文";
    case HB_TAG('Z', 'H', 'P', ' '):
        return "拼音";
    case HB_TAG('J', 'A', 'N', ' '):
        return "日语";
    case HB_TAG('K', 'O', 'R', ' '):
        return "韩语";
    case HB_TAG('M', 'N', 'G', ' '):
        return "蒙古语";
    case HB_TAG('T', 'I', 'B', ' '):
        return "藏语";
    case HB_TAG('U', 'I', 'G', ' '):
        return "维吾尔语";

    // ----- 英语及其变体 -----
    case HB_TAG('E', 'N', 'G', ' '):
        return "英语";
    case HB_TAG('E', 'N', 'U', ' '):
        return "英语 (美国)";
    case HB_TAG('E', 'N', 'G', 'B'):
        return "英语 (英国)";
    case HB_TAG('E', 'N', 'G', 'C'):
        return "英语 (加拿大)";
    case HB_TAG('E', 'N', 'G', 'A'):
        return "英语 (澳大利亚)";

    // ----- 西欧语言 -----
    case HB_TAG('F', 'R', 'A', ' '):
        return "法语";
    case HB_TAG('F', 'R', 'N', ' '):
        return "法语 (法国)";
    case HB_TAG('F', 'R', 'C', ' '):
        return "法语 (加拿大)";
    case HB_TAG('D', 'E', 'U', ' '):
        return "德语";
    case HB_TAG('D', 'E', 'N', ' '):
        return "德语 (德国)";
    case HB_TAG('D', 'E', 'C', ' '):
        return "德语 (瑞士)";
    case HB_TAG('S', 'P', 'A', ' '):
        return "西班牙语";
    case HB_TAG('E', 'S', 'N', ' '):
        return "西班牙语 (西班牙)";
    case HB_TAG('E', 'S', 'M', ' '):
        return "西班牙语 (墨西哥)";
    case HB_TAG('I', 'T', 'A', ' '):
        return "意大利语";
    case HB_TAG('P', 'T', 'G', ' '):
        return "葡萄牙语";
    case HB_TAG('P', 'T', 'B', ' '):
        return "葡萄牙语 (巴西)";
    case HB_TAG('N', 'L', 'D', ' '):
        return "荷兰语";
    case HB_TAG('N', 'L', 'B', ' '):
        return "荷兰语 (比利时)";
    case HB_TAG('S', 'V', 'E', ' '):
        return "瑞典语";
    case HB_TAG('D', 'A', 'N', ' '):
        return "丹麦语";
    case HB_TAG('N', 'O', 'R', ' '):
        return "挪威语";
    case HB_TAG('F', 'I', 'N', ' '):
        return "芬兰语";
    case HB_TAG('I', 'S', 'L', ' '):
        return "冰岛语";

    // ----- 东欧及斯拉夫语言 -----
    case HB_TAG('R', 'U', 'S', ' '):
        return "俄语";
    case HB_TAG('U', 'K', 'R', ' '):
        return "乌克兰语";
    case HB_TAG('B', 'U', 'L', ' '):
        return "保加利亚语";
    case HB_TAG('C', 'Z', 'E', ' '):
        return "捷克语";
    case HB_TAG('S', 'L', 'K', ' '):
        return "斯洛伐克语";
    case HB_TAG('P', 'O', 'L', ' '):
        return "波兰语";
    case HB_TAG('H', 'U', 'N', ' '):
        return "匈牙利语";
    case HB_TAG('R', 'O', 'M', ' '):
        return "罗马尼亚语";
    case HB_TAG('S', 'R', 'B', ' '):
        return "塞尔维亚语";
    case HB_TAG('C', 'R', 'O', ' '):
        return "克罗地亚语";
    case HB_TAG('S', 'L', 'O', ' '):
        return "斯洛文尼亚语";
    case HB_TAG('L', 'T', 'H', ' '):
        return "立陶宛语";
    case HB_TAG('L', 'V', 'I', ' '):
        return "拉脱维亚语";
    case HB_TAG('E', 'S', 'T', ' '):
        return "爱沙尼亚语";
    case HB_TAG('B', 'E', 'L', ' '):
        return "白俄罗斯语";
    case HB_TAG('M', 'K', 'D', ' '):
        return "马其顿语";

    // ----- 中东及阿拉伯语系 -----
    case HB_TAG('A', 'R', 'A', ' '):
        return "阿拉伯语";
    case HB_TAG('F', 'A', 'R', ' '):
        return "波斯语";
    case HB_TAG('P', 'E', 'R', ' '):
        return "波斯语 (旧式)";
    case HB_TAG('U', 'R', 'D', ' '):
        return "乌尔都语";
    case HB_TAG('H', 'E', 'B', ' '):
        return "希伯来语";
    case HB_TAG('I', 'W', 'R', ' '):
        return "希伯来语 (旧式)";
    case HB_TAG('T', 'U', 'R', ' '):
        return "土耳其语";
    case HB_TAG('K', 'U', 'R', ' '):
        return "库尔德语";
    case HB_TAG('P', 'U', 'S', ' '):
        return "普什图语";

    // ----- 南亚及东南亚语言 -----
    case HB_TAG('H', 'I', 'N', ' '):
        return "印地语";
    case HB_TAG('B', 'E', 'N', ' '):
        return "孟加拉语";
    case HB_TAG('T', 'A', 'M', ' '):
        return "泰米尔语";
    case HB_TAG('T', 'E', 'L', ' '):
        return "泰卢固语";
    case HB_TAG('M', 'A', 'R', ' '):
        return "马拉地语";
    case HB_TAG('G', 'U', 'J', ' '):
        return "古吉拉特语";
    case HB_TAG('P', 'U', 'N', ' '):
        return "旁遮普语";
    case HB_TAG('M', 'A', 'L', ' '):
        return "马拉雅拉姆语";
    case HB_TAG('K', 'A', 'N', ' '):
        return "卡纳达语";
    case HB_TAG('O', 'R', 'Y', ' '):
        return "奥里亚语";
    case HB_TAG('S', 'A', 'N', ' '):
        return "梵语";
    case HB_TAG('T', 'H', 'A', ' '):
        return "泰语";
    case HB_TAG('L', 'A', 'O', ' '):
        return "老挝语";
    case HB_TAG('K', 'H', 'M', ' '):
        return "高棉语";
    case HB_TAG('M', 'Y', 'A', ' '):
        return "缅甸语";
    case HB_TAG('V', 'I', 'E', ' '):
        return "越南语";
    case HB_TAG('I', 'N', 'D', ' '):
        return "印尼语";
    case HB_TAG('M', 'A', 'Y', ' '):
        return "马来语";
    case HB_TAG('T', 'G', 'L', ' '):
        return "他加禄语";

    // ----- 非洲语言 -----
    case HB_TAG('A', 'M', 'H', ' '):
        return "阿姆哈拉语";
    case HB_TAG('T', 'I', 'R', ' '):
        return "提格里尼亚语";
    case HB_TAG('S', 'W', 'A', ' '):
        return "斯瓦希里语";
    case HB_TAG('H', 'A', 'U', ' '):
        return "豪萨语";
    case HB_TAG('Y', 'O', 'R', ' '):
        return "约鲁巴语";
    case HB_TAG('I', 'G', 'O', ' '):
        return "伊博语";
    case HB_TAG('Z', 'U', 'L', ' '):
        return "祖鲁语";
    case HB_TAG('X', 'H', 'O', ' '):
        return "科萨语";
    case HB_TAG('S', 'O', 'M', ' '):
        return "索马里语";

    // ----- 高加索及中亚语言 -----
    case HB_TAG('G', 'E', 'O', ' '):
        return "格鲁吉亚语";
    case HB_TAG('A', 'R', 'M', ' '):
        return "亚美尼亚语";
    case HB_TAG('A', 'Z', 'E', ' '):
        return "阿塞拜疆语";
    case HB_TAG('K', 'A', 'Z', ' '):
        return "哈萨克语";
    case HB_TAG('U', 'Z', 'B', ' '):
        return "乌兹别克语";
    case HB_TAG('T', 'U', 'K', ' '):
        return "土库曼语";
    case HB_TAG('K', 'Y', 'R', ' '):
        return "吉尔吉斯语";
    case HB_TAG('T', 'A', 'T', ' '):
        return "鞑靼语";
    case HB_TAG('C', 'H', 'E', ' '):
        return "车臣语";

    // ----- 特殊语言系统 -----
    case HB_TAG('d', 'f', 'l', 't'):
        return "默认语言";
    case HB_TAG('I', 'P', 'P', 'H'):
        return "国际音标";

    // ----- 其他常见语言（按字母序补充）-----
    case HB_TAG('A', 'B', 'K', ' '):
        return "阿布哈兹语";
    case HB_TAG('A', 'F', 'R', ' '):
        return "南非荷兰语";
    case HB_TAG('A', 'L', 'B', ' '):
        return "阿尔巴尼亚语";
    case HB_TAG('B', 'A', 'K', ' '):
        return "巴什基尔语";
    case HB_TAG('B', 'A', 'S', ' '):
        return "巴斯克语";
    case HB_TAG('B', 'O', 'S', ' '):
        return "波斯尼亚语";
    case HB_TAG('B', 'R', 'E', ' '):
        return "布列塔尼语";
    case HB_TAG('C', 'A', 'T', ' '):
        return "加泰罗尼亚语";
    case HB_TAG('C', 'E', 'B', ' '):
        return "宿务语";
    case HB_TAG('C', 'O', 'R', ' '):
        return "康沃尔语";
    case HB_TAG('D', 'I', 'V', ' '):
        return "迪维希语";
    case HB_TAG('D', 'Z', 'O', ' '):
        return "宗卡语";
    case HB_TAG('E', 'W', 'E', ' '):
        return "埃维语";
    case HB_TAG('F', 'A', 'O', ' '):
        return "法罗语";
    case HB_TAG('F', 'I', 'J', ' '):
        return "斐济语";
    case HB_TAG('F', 'U', 'L', ' '):
        return "富拉语";
    case HB_TAG('G', 'A', 'E', ' '):
        return "苏格兰盖尔语";
    case HB_TAG('G', 'L', 'G', ' '):
        return "加利西亚语";
    case HB_TAG('G', 'R', 'E', ' '):
        return "希腊语";
    case HB_TAG('H', 'A', 'W', ' '):
        return "夏威夷语";
    case HB_TAG('H', 'I', 'M', ' '):
        return "希马查利语";
    case HB_TAG('H', 'M', 'O', ' '):
        return "苗语";
    case HB_TAG('I', 'B', 'O', ' '):
        return "伊博语";
    case HB_TAG('I', 'N', 'U', ' '):
        return "因纽特语";
    case HB_TAG('I', 'R', 'I', ' '):
        return "爱尔兰语";
    case HB_TAG('K', 'A', 'L', ' '):
        return "格陵兰语";
    case HB_TAG('K', 'A', 'S', ' '):
        return "克什米尔语";
    case HB_TAG('K', 'A', 'T', ' '):
        return "格鲁吉亚语";
    case HB_TAG('K', 'A', 'U', ' '):
        return "卡努里语";
    case HB_TAG('K', 'O', 'K', ' '):
        return "孔卡尼语";
    case HB_TAG('K', 'O', 'M', ' '):
        return "科米语";
    case HB_TAG('K', 'O', 'N', ' '):
        return "刚果语";
    case HB_TAG('L', 'A', 'H', ' '):
        return "旁遮普语";
    case HB_TAG('L', 'A', 'M', ' '):
        return "兰巴语";
    case HB_TAG('L', 'A', 'T', ' '):
        return "拉丁语";
    case HB_TAG('L', 'A', 'Z', ' '):
        return "拉兹语";
    case HB_TAG('L', 'I', 'M', ' '):
        return "林堡语";
    case HB_TAG('L', 'I', 'N', ' '):
        return "林加拉语";
    case HB_TAG('L', 'O', 'Z', ' '):
        return "洛齐语";
    case HB_TAG('L', 'U', 'B', ' '):
        return "卢巴语";
    case HB_TAG('L', 'U', 'G', ' '):
        return "干达语";
    case HB_TAG('M', 'A', 'C', ' '):
        return "马其顿语";
    case HB_TAG('M', 'A', 'N', ' '):
        return "曼丁哥语";
    case HB_TAG('M', 'A', 'O', ' '):
        return "毛利语";
    case HB_TAG('M', 'L', 'T', ' '):
        return "马耳他语";
    case HB_TAG('M', 'O', 'N', ' '):
        return "蒙古语";
    case HB_TAG('M', 'O', 'S', ' '):
        return "莫西语";
    case HB_TAG('N', 'A', 'P', ' '):
        return "那不勒斯语";
    case HB_TAG('N', 'D', 'S', ' '):
        return "低地德语";
    case HB_TAG('N', 'E', 'P', ' '):
        return "尼泊尔语";
    case HB_TAG('N', 'G', 'O', ' '):
        return "恩东加语";
    case HB_TAG('N', 'N', 'O', ' '):
        return "新挪威语";
    case HB_TAG('N', 'Y', 'A', ' '):
        return "齐切瓦语";
    case HB_TAG('O', 'C', 'I', ' '):
        return "奥克语";
    case HB_TAG('O', 'R', 'I', ' '):
        return "奥里亚语";
    case HB_TAG('O', 'R', 'M', ' '):
        return "奥罗莫语";
    case HB_TAG('P', 'A', 'L', ' '):
        return "巴利语";
    case HB_TAG('P', 'A', 'N', ' '):
        return "旁遮普语";
    case HB_TAG('P', 'A', 'P', ' '):
        return "帕皮阿门托语";
    case HB_TAG('P', 'O', 'R', ' '):
        return "葡萄牙语";
    case HB_TAG('Q', 'U', 'E', ' '):
        return "克丘亚语";
    case HB_TAG('R', 'O', 'H', ' '):
        return "罗曼什语";
    case HB_TAG('R', 'U', 'N', ' '):
        return "隆迪语";
    case HB_TAG('R', 'W', 'D', ' '):
        return "卢旺达语";
    case HB_TAG('S', 'A', 'M', ' '):
        return "萨摩亚语";
    case HB_TAG('S', 'G', 'B', ' '):
        return "桑戈语";
    case HB_TAG('S', 'I', 'N', ' '):
        return "僧伽罗语";
    case HB_TAG('S', 'M', 'A', ' '):
        return "萨米语 (北方)";
    case HB_TAG('S', 'M', 'E', ' '):
        return "萨米语 (伊纳里)";
    case HB_TAG('S', 'M', 'J', ' '):
        return "萨米语 (吕勒)";
    case HB_TAG('S', 'M', 'N', ' '):
        return "萨米语 (南萨米)";
    case HB_TAG('S', 'M', 'S', ' '):
        return "萨米语 (斯科尔特)";
    case HB_TAG('S', 'N', 'K', ' '):
        return "索宁克语";
    case HB_TAG('S', 'O', 'T', ' '):
        return "南索托语";
    case HB_TAG('S', 'U', 'K', ' '):
        return "苏库马语";
    case HB_TAG('S', 'U', 'N', ' '):
        return "巽他语";
    case HB_TAG('S', 'W', 'Z', ' '):
        return "斯瓦特语";
    case HB_TAG('T', 'A', 'J', ' '):
        return "塔吉克语";
    case HB_TAG('T', 'O', 'N', ' '):
        return "汤加语";
    case HB_TAG('T', 'S', 'N', ' '):
        return "茨瓦纳语";
    case HB_TAG('T', 'S', 'O', ' '):
        return "聪加语";
    case HB_TAG('U', 'G', 'A', ' '):
        return "乌加里特语";
    case HB_TAG('U', 'Y', 'G', ' '):
        return "维吾尔语";
    case HB_TAG('V', 'E', 'N', ' '):
        return "文达语";
    case HB_TAG('W', 'A', 'L', ' '):
        return "沃拉普克语";
    case HB_TAG('W', 'A', 'R', ' '):
        return "瓦瑞语";
    case HB_TAG('W', 'E', 'L', ' '):
        return "威尔士语";
    case HB_TAG('W', 'O', 'L', ' '):
        return "沃洛夫语";

    case HB_TAG('Y', 'I', 'D', ' '):
        return "意第绪语";

    default:
        return nullptr;
    }
}

int main()
try
{
    constexpr auto *UNICODE_FONT_PATH = "C:/Windows/Fonts/msyh.ttc"; // 中文字体
    freetype_loader loader{};
    freetype_face face{*loader, UNICODE_FONT_PATH};
    const FT_Face raw_face = *face;

    // 从 FT_Face 创建 hb_face_t（使用智能指针管理）
    hb_face_t *hb_face_raw = hb_ft_face_create(raw_face, nullptr);
    if (!hb_face_raw)
    {
        throw std::runtime_error{"hb_ft_face_create failed"};
    }
    // 使用自定义删除器自动销毁
    std::unique_ptr<hb_face_t, decltype(&hb_face_destroy)> hb_face(hb_face_raw,
                                                                   hb_face_destroy);

    // 查询表
    struct TableInfo
    {
        hb_tag_t tag;
        std::string_view name;
    };
    const TableInfo tables[] = {{HB_OT_TAG_GSUB, "GSUB"}, {HB_OT_TAG_GPOS, "GPOS"}};

    for (const auto &[table_tag, table_name] : tables)
    {
        // 检查表是否存在
        bool has_table = false;
        if (table_tag == HB_OT_TAG_GSUB)
            has_table = hb_ot_layout_has_substitution(hb_face.get());
        else if (table_tag == HB_OT_TAG_GPOS)
            has_table = hb_ot_layout_has_positioning(hb_face.get());

        if (!has_table)
            continue;

        std::println("\n--- {} table ---", table_name);

        // 获取所有脚本标签
        unsigned int script_count = hb_ot_layout_table_get_script_tags(
            hb_face.get(), table_tag, 0, nullptr, nullptr);
        std::vector<hb_tag_t> script_tags(script_count);
        hb_ot_layout_table_get_script_tags(hb_face.get(), table_tag, 0, &script_count,
                                           script_tags.data());

        for (size_t i = 0; i < script_tags.size(); ++i)
        {
            std::string script_str = tag_to_string(script_tags[i]);
            std::println("  Script: {}", script_str);

            // 获取显式语言标签
            unsigned int lang_count = hb_ot_layout_script_get_language_tags(
                hb_face.get(), table_tag, i, 0, nullptr, nullptr);
            if (lang_count > 0)
            {
                std::vector<hb_tag_t> lang_tags(lang_count);
                hb_ot_layout_script_get_language_tags(hb_face.get(), table_tag, i, 0,
                                                      &lang_count, lang_tags.data());

                for (hb_tag_t lang_tag : lang_tags)
                {
                    std::string lang_str = tag_to_string(lang_tag);
                    const char *chinese = lang_tag_to_chinese_name(lang_tag);
                    if (chinese)
                    {
                        std::println("    Language: {} ({})", lang_str, chinese);
                    }
                    else
                    {
                        std::println("    Language: {}", lang_str);
                    }
                }
            }
            else
            {
                // 检查默认语言系统 'dflt'
                unsigned int lang_idx;
                auto tag = HB_TAG('d', 'f', 'l', 't');
                hb_bool_t has_default = hb_ot_layout_script_select_language(
                    hb_face.get(), table_tag, i, 1, &tag, &lang_idx);
                if (has_default)
                    std::println("    Language: dflt (default)");
                else
                    std::println("    Language: (none)");
            }
        }
    }

    // 打印系统默认语言
    // NOTE: Default language (from system): c 表示系统区域为 C locale，没有特定语言。
    hb_language_t default_lang = hb_language_get_default();
    std::println("Default language (from system): {}",
                 hb_language_to_string(default_lang));

    std::println("main done");
    return 0;
}
catch (const std::exception &e)
{
    std::cerr << "Exception: " << e.what() << '\n';
    return 1;
}
catch (...)
{
    std::cerr << "Unknown exception\n";
    return 1;
}