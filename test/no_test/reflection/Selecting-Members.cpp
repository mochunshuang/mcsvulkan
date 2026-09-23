#include <cassert>
#include <iostream>
#include <exception>

#include <meta>

// NOLINTBEGIN
struct S
{
    unsigned i : 2, j : 6;
};

consteval auto member_number(int n)
{
    if (n == 0)
        return ^^S::i;
    else if (n == 1)
        return ^^S::j;
}

consteval auto member_number2(int n)
{
    return std::meta::nonstatic_data_members_of(^^S,
                                                std::meta::access_context::current())[n];
}

struct A
{
    int x = 1;
    int y = 2;

    // NOTE: 不同返回值是不允许的
    // 运行时用字符串访问成员
    template <typename Self>
    int &get(this Self &&self, std::string_view name)
    {
        constexpr auto self_type = std::meta::remove_cvref(^^Self);
        // 1. 在编译期获取成员列表，并转换为静态数组
        constexpr static auto members =
            std::define_static_array(std::meta::nonstatic_data_members_of(
                self_type, std::meta::access_context::current()));

        // 2. 在运行时遍历静态数组，进行字符串匹配
        template for (constexpr auto m : members)
        {
            if (name == std::meta::identifier_of(m))
            {
                // splice 操作在编译期展开，运行时不会产生分支跳转开销
                return self.[:m:];
            }
        }
        // throw std::meta::exception(u8"no such member",
        //                            std::meta::current_function());

        // 3. 异常处理中不再调用 consteval 函数
        throw std::runtime_error("no such member");
    }
};
int main()
try
{
    S s{0, 0};
    s.[:member_number(1):] = 42; // Same as: s.j = 42;
    // s.[:member_number(5):] = 0;  // Error (member_number(5) is not a constant).

    assert(s.j == 42);

    s.[:member_number2(0):] = 1;
    assert(s.i == 1);

    {

        A s{10, 20};
        s.get("x") = 99;
        std::cout << s.x << "\n";        // 99
        std::cout << s.get("y") << "\n"; // 20
    }

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
// NOLINTEND