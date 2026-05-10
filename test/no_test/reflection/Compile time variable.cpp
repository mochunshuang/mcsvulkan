#include <iostream>
#include <exception>
#include <meta>

struct Foo
{
};
struct Bar
{
};

// 不同的类型会生成不同的 info 对象
constexpr std::meta::info info_foo = ^^Foo;
constexpr std::meta::info info_bar = ^^Bar;

// info 对象可以作为编译期“身份证”直接进行比较
static_assert(info_foo != info_bar);
static_assert(info_foo == ^^Foo); // 同一实体的 info 总是相等

consteval
{
    std::meta::info info_foo = ^^Foo;
    // info_bar = ^^Bar; //NOTE: 只读
}

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