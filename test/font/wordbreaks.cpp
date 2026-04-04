#include <print>
#include <vector>
#include <wordbreak.h> // 替换 linebreak.h
#include <utf8proc.h>
#include <array>

// NOLINTBEGIN
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
static constexpr std::string_view codepoint_to_utf8(char32_t cp,
                                                    std::array<char, 4> &buf) noexcept
{
    utf8proc_ssize_t len =
        utf8proc_encode_char(static_cast<utf8proc_int32_t>(cp), // NOLINTNEXTLINE
                             reinterpret_cast<utf8proc_uint8_t *>(buf.data()));
    if (len > 0)
        return {buf.data(), static_cast<size_t>(len)};
    buf = {'E', 'O', 'R', '\0'};
    return {buf.data(), 3};
}
static constexpr std::string codepoint_to_utf8(char32_t cp)
{
    std::array<char, 4> buf; // 最大 4 字节 // NOLINT
    utf8proc_ssize_t len =
        utf8proc_encode_char(static_cast<utf8proc_int32_t>(cp),
                             reinterpret_cast<utf8proc_uint8_t *>(buf.data()));
    if (len > 0)
        return {buf.data(), static_cast<size_t>(len)};
    return "ERR";
}

// ---------- 单词边界结果结构 ----------
struct word_break_result
{
    // 每个字符后的单词边界类型（来自 libunibreak）
    // 可能取值：WORDBREAK_BREAK, WORDBREAK_NOBREAK, WORDBREAK_INSIDEACHAR,
    // WORDBREAK_INDETERMINATE
    std::vector<char> types;

    [[nodiscard]] constexpr bool is_word_break(size_t idx) const noexcept
    {
        return types[idx] == WORDBREAK_BREAK;
    }
    [[nodiscard]] constexpr bool no_word_break(size_t idx) const noexcept
    {
        return types[idx] == WORDBREAK_NOBREAK;
    }
    [[nodiscard]] constexpr bool inside_char(size_t idx) const noexcept
    {
        return types[idx] == WORDBREAK_INSIDEACHAR;
    }
};

// 分析单词边界（UTF-32 版本）
constexpr static word_break_result analyze_word_breaks(
    const std::vector<uint32_t> &codepoints, std::string_view langBcp47)
{
    size_t char_count = codepoints.size();

    if (char_count == 0)
        return {};

    word_break_result result{.types = std::vector<char>(char_count)};
    set_wordbreaks_utf32(reinterpret_cast<const utf32_t *>(codepoints.data()), char_count,
                         langBcp47.empty() ? "" : langBcp47.data(), result.types.data());
    return result;
}

// 打印单词边界结果
constexpr static void print_word_break_result(word_break_result result,
                                              const std::vector<uint32_t> &codepoints)
{
    std::println("\n---Stage 5: Word Breaking (UAX #29) ---");
    std::println("Character count: {}", codepoints.size());
    std::println("{:<4} {:<10} {:<8} {}", "Idx", "U+", "Char", "Word Break Type");

    for (size_t i = 0; i < codepoints.size(); ++i)
    {
        uint32_t cp = codepoints[i];
        char utf8_buf[4]; // NOLINT
        std::string_view char_str = codepoint_to_utf8(cp, utf8_buf);
        if (cp == 0x20)     // NOLINT
            char_str = " "; // 确保空格可见

        const char *break_str; // NOLINT
        switch (result.types[i])
        {
        case WORDBREAK_BREAK:
            break_str = "BREAK";
            break;
        case WORDBREAK_NOBREAK:
            break_str = "NOBREAK";
            break;
        case WORDBREAK_INSIDEACHAR:
            break_str = "INSIDEACHAR";
            break;
        default:
            break_str = "UNKNOWN";
            break;
        }

        std::println("{:<4} U+{:06X} {:<8} {}", i, cp, char_str, break_str);
    }
}
// NOLINTEND

#include <iostream>
#include <exception>

// 将 UTF-8 字符串转换为 UTF-32 码点向量
std::vector<uint32_t> utf8_to_utf32(std::string_view utf8)
{
    std::vector<uint32_t> result;
    const auto *ptr = reinterpret_cast<const utf8proc_uint8_t *>(utf8.data());
    const auto *end = ptr + utf8.size();
    while (ptr < end)
    {
        utf8proc_int32_t cp;
        auto len = utf8proc_iterate(ptr, end - ptr, &cp);
        if (len > 0)
        {
            result.push_back(static_cast<uint32_t>(cp));
            ptr += len;
        }
        else
        {
            // 遇到无效 UTF-8 时跳过（不应发生）
            ++ptr;
        }
    }
    return result;
}

int main()
try
{
    // NOTE: 在线测试 https://util.unicode.org/UnicodeJsps/breaks.jsp
    //  1. 基础英文
    auto text1 = utf8_to_utf32("Hello, world! This is a test.");
    std::println("\n=== Test 1: English ===");
    auto res1 = analyze_word_breaks(text1, "");
    print_word_break_result(res1, text1);

    // 2. 混合亚洲语言 + 表情符号
    auto text2 = utf8_to_utf32("你好，世界！ こんにちは 세계 🌍✨");
    std::println("\n=== Test 2: Mixed CJK + Emoji ===");
    auto res2 = analyze_word_breaks(text2, "");
    print_word_break_result(res2, text2);

    // 3. 泰语（不指定语言）
    auto text3 = utf8_to_utf32("Hello ภาษาไทย World");
    std::println("\n=== Test 3: Thai without dictionary ===");
    auto res3 = analyze_word_breaks(text3, "");
    print_word_break_result(res3, text3);

    // 4. 泰语（指定语言为 "th"）
    std::println("\n=== Test 4: Thai with language 'th' ===");
    auto res4 = analyze_word_breaks(text3, "th");
    print_word_break_result(res4, text3);

    // 5. 特殊字符：软连字符 U+00AD、零宽空格 U+200B、换行符 U+000A
    auto text5 = utf8_to_utf32("soft\xC2\xADhyphen zero\xE2\x80\x8Bwidth newline\nbreak");
    std::println("\n=== Test 5: Special characters (soft hyphen, ZWSP, newline) ===");
    auto res5 = analyze_word_breaks(text5, "");
    print_word_break_result(res5, text5);

    // 6. 复杂混合文本
    //[comment/wordbreaks_0.png]
    auto text6 =
        utf8_to_utf32("Hello, 世界! 🌍 ภาษาไทย こんにちは. (test) [2023] \"quote\"");
    std::println("\n=== Test 6: Complex mixture ===");
    auto res6 = analyze_word_breaks(text6, "");
    print_word_break_result(res6, text6);

    std::cout << "\nAll tests completed.\n";
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
/*
Idx  Char  Word Break Type
0    H     NOBREAK    ← H 之后不能断（H和e之间）
1    e     NOBREAK    ← e 之后不能断（e和l之间）
2    l     NOBREAK    ← l 之后不能断（l和l之间）
3    l     NOBREAK    ← l 之后不能断（l和o之间）
4    o     BREAK      ← o 之后可以断 ✅
5    ,     BREAK      ← 逗号之后可以断 ✅
6    空格  BREAK      ← 空格之后可以断 ✅
*/