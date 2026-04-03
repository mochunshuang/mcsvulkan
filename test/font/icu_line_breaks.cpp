#include <print>
#include <vector>
#include <string_view>
#include <cstdint>
#include <array>
#include <iostream>
#include <exception>

// ICU 头文件
#include <unicode/brkiter.h> // BreakIterator
#include <unicode/locid.h>   // Locale
#include <unicode/errorcode.h>
#include <unicode/localpointer.h>

// 断行类型常量（与 libunibreak 兼容）
#define LINEBREAK_MUSTBREAK 0
#define LINEBREAK_ALLOWBREAK 1
#define LINEBREAK_NOBREAK 2
#define LINEBREAK_INSIDEACHAR 3
#define LINEBREAK_INDETERMINATE 4

// utf8proc 保持不变（用于码点显示）
#include <utf8proc.h>

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

struct break_result // NOLINTBEGIN
{
    // 每个字符后的断行类型，直接来自
    // libunibreak（0=强制换行，1=允许换行，2=禁止换行，3=不完整）
    std::vector<char> types;

    // 便捷查询
    [[nodiscard]] constexpr bool must_break(size_t idx) const noexcept
    {
        return types[idx] == LINEBREAK_MUSTBREAK;
    }
    [[nodiscard]] constexpr bool allow_break(size_t idx) const noexcept
    {
        return types[idx] == LINEBREAK_ALLOWBREAK;
    }
    [[nodiscard]] constexpr bool no_break(size_t idx) const noexcept
    {
        return types[idx] == LINEBREAK_NOBREAK;
    }
    // 是否可以换行（包括强制和允许）
    [[nodiscard]] constexpr bool can_break(size_t idx) const noexcept
    {
        char t = types[idx];
        return t == LINEBREAK_MUSTBREAK || t == LINEBREAK_ALLOWBREAK;
    }
}; // NOLINTEND

// ------------------------------------------------------------
// 使用 ICU 实现 analyze_line_breaks
// ------------------------------------------------------------
break_result analyze_line_breaks(const std::vector<uint32_t> &codepoints,
                                 std::string_view langBcp47)
{
    size_t char_count = codepoints.size();
    if (char_count == 0)
        return {};

    break_result result{.types = std::vector<char>(char_count, LINEBREAK_NOBREAK)};

    // 转换为 UTF-16 字符串（ICU 使用 UTF-16）
    icu::UnicodeString ustr;
    for (uint32_t cp : codepoints)
    {
        ustr.append(static_cast<UChar32>(cp));
    }

    // 语言设置
    std::string locale_str(langBcp47);
    if (locale_str.empty())
        locale_str = "root";
    UErrorCode status = U_ZERO_ERROR;
    icu::Locale locale(locale_str.c_str());
    icu::BreakIterator *bi = icu::BreakIterator::createLineInstance(locale, status);
    if (U_FAILURE(status) || !bi)
    {
        std::cerr << "Failed to create line break iterator: " << u_errorName(status)
                  << '\n';
        return result;
    }
    bi->setText(ustr);

    // 遍历断行位置
    int32_t pos = bi->first();
    while ((pos = bi->next()) != icu::BreakIterator::DONE)
    {
        // 将 UTF-16 偏移量转换为 UTF-32 码点索引
        int32_t cpIndex = ustr.countChar32(0, pos);
        if (cpIndex > 0 && cpIndex <= static_cast<int32_t>(char_count))
        {
            char brkType = LINEBREAK_ALLOWBREAK;
            // 检查前一个字符是否为强制换行符
            uint32_t lastCp = codepoints[cpIndex - 1];
            if (lastCp == 0x000A || lastCp == 0x000D || lastCp == 0x000C ||
                lastCp == 0x000B || lastCp == 0x0085 || lastCp == 0x2028 ||
                lastCp == 0x2029)
            {
                brkType = LINEBREAK_MUSTBREAK;
            }
            result.types[cpIndex - 1] = brkType;
        }
    }

    delete bi; // 手动释放

    // 最后一个字符处理
    if (char_count > 0)
    {
        char lastType = result.types[char_count - 1];
        if (lastType != LINEBREAK_MUSTBREAK && lastType != LINEBREAK_ALLOWBREAK)
        {
            result.types[char_count - 1] = LINEBREAK_INDETERMINATE;
        }
    }

    return result;
}

// ------------------------------------------------------------
// 打印函数和辅助工具保持不变
// ------------------------------------------------------------
constexpr static void print_break_result(break_result result,
                                         const std::vector<uint32_t> &codepoints)
{
    std::println("\n---Stage 5: Line Breaking (UAX #14) ---");
    std::println("Character count: {}", codepoints.size());
    std::println("{:<4} {:<10} {:<8} {}", "Idx", "U+", "Char", "Break Type");

    for (size_t i = 0; i < codepoints.size(); ++i)
    {
        uint32_t cp = codepoints[i];
        char utf8_buf[4]; // NOLINT
        std::string_view char_str = codepoint_to_utf8(cp, utf8_buf);
        if (cp == 0x20)     // NOLINT
            char_str = " "; // 确保空格可见

        const char *break_str;
        switch (result.types[i])
        {
        case LINEBREAK_MUSTBREAK:
            break_str = "MUSTBREAK";
            break;
        case LINEBREAK_ALLOWBREAK:
            break_str = "ALLOWBREAK";
            break;
        case LINEBREAK_NOBREAK:
            break_str = "NOBREAK";
            break;
        case LINEBREAK_INSIDEACHAR:
            break_str = "INSIDEACHAR";
            break;
        case LINEBREAK_INDETERMINATE:
            break_str = "INDETERMINATE";
            break;
        default:
            break_str = "UNKNOWN";
            break;
        }

        std::println("{:<4} U+{:06X} {:<8} {}", i, cp, char_str, break_str);
    }
}

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
            ++ptr; // 跳过无效字节
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
    auto res1 = analyze_line_breaks(text1, "");
    print_break_result(res1, text1);

    // 2. 混合亚洲语言 + 表情符号
    auto text2 = utf8_to_utf32("你好，世界！ こんにちは 세계 🌍✨");
    std::println("\n=== Test 2: Mixed CJK + Emoji ===");
    auto res2 = analyze_line_breaks(text2, "");
    print_break_result(res2, text2);

    // 3. 泰语（不指定语言）
    auto text3 = utf8_to_utf32("Hello ภาษาไทย World");
    std::println("\n=== Test 3: Thai without dictionary ===");
    auto res3 = analyze_line_breaks(text3, "");
    print_break_result(res3, text3);

    // 4. 泰语（指定语言为 "th"）
    std::println("\n=== Test 4: Thai with language 'th' ===");
    auto res4 = analyze_line_breaks(text3, "th");
    print_break_result(res4, text3);

    // 5. 特殊字符：软连字符 U+00AD、零宽空格 U+200B、换行符 U+000A
    auto text5 = utf8_to_utf32("soft\xC2\xADhyphen zero\xE2\x80\x8Bwidth newline\nbreak");
    std::println("\n=== Test 5: Special characters (soft hyphen, ZWSP, newline) ===");
    auto res5 = analyze_line_breaks(text5, "");
    print_break_result(res5, text5);

    // 6. 复杂混合文本
    auto text6 =
        utf8_to_utf32("Hello, 世界! 🌍 ภาษาไทย こんにちは. (test) [2023] \"quote\"");
    std::println("\n=== Test 6: Complex mixture ===");
    auto res6 = analyze_line_breaks(text6, "");
    print_break_result(res6, text6);

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