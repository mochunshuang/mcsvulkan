#include <meta>

namespace meta = std::meta;

// ── make_signed ──
// 有符号整数类型 -> 自身
static_assert(meta::is_same_type(meta::make_signed(^^int), ^^int));
static_assert(meta::is_same_type(meta::make_signed(^^signed char), ^^signed char));
static_assert(meta::is_same_type(meta::make_signed(^^short), ^^short));
static_assert(meta::is_same_type(meta::make_signed(^^long), ^^long));
static_assert(meta::is_same_type(meta::make_signed(^^long long), ^^long long));

// 无符号整数类型 -> 对应的有符号类型
static_assert(meta::is_same_type(meta::make_signed(^^unsigned int), ^^int));
static_assert(meta::is_same_type(meta::make_signed(^^unsigned char), ^^signed char));
static_assert(meta::is_same_type(meta::make_signed(^^unsigned short), ^^short));
static_assert(meta::is_same_type(meta::make_signed(^^unsigned long), ^^long));
static_assert(meta::is_same_type(meta::make_signed(^^unsigned long long), ^^long long));

// ── make_unsigned ──
// 无符号整数类型 -> 自身
static_assert(meta::is_same_type(meta::make_unsigned(^^unsigned int), ^^unsigned int));
static_assert(meta::is_same_type(meta::make_unsigned(^^unsigned char), ^^unsigned char));
static_assert(meta::is_same_type(meta::make_unsigned(^^unsigned short),
                                 ^^unsigned short));
static_assert(meta::is_same_type(meta::make_unsigned(^^unsigned long), ^^unsigned long));
static_assert(meta::is_same_type(meta::make_unsigned(^^unsigned long long),
                                 ^^unsigned long long));

// 有符号整数类型 -> 对应的无符号类型
static_assert(meta::is_same_type(meta::make_unsigned(^^int), ^^unsigned int));
static_assert(meta::is_same_type(meta::make_unsigned(^^signed char), ^^unsigned char));
static_assert(meta::is_same_type(meta::make_unsigned(^^short), ^^unsigned short));
static_assert(meta::is_same_type(meta::make_unsigned(^^long), ^^unsigned long));
static_assert(meta::is_same_type(meta::make_unsigned(^^long long), ^^unsigned long long));

#include <iostream>
#include <exception>

int main()
try
{
    std::cout << "main done\n";
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