#include <cassert>
#include <iostream>
#include <exception>
#include <utility>

/*

结构化绑定声明会引入一个隐藏变量 e，声明形式相当于：

[引用限定符] e = 初始化器;
auto [x,y] = expr; → auto e = expr;

auto& [x,y] = expr; → auto& e = expr;

auto&& [x,y] = expr; → auto&& e = expr;

然后根据 e 的类型 E = std::remove_reference_t<decltype((e))> 进行“元组式绑定”。
每个绑定标识符最终成为左值（指代元组某个元素的引用），其 decltype（被引用类型）是 std::tuple_element<I, E>::type。
外层写的 auto / auto& / auto&& 只决定 e 的声明方式，不直接决定绑定变量的引用性；绑定变量的引用性由元组元素本身是不是引用决定。
*/
// NOLINTBEGIN
int main()
try
{
    struct Point
    {
        double x, y;
    };
    //auto [] —— 拷贝结构体
    {
        Point p{1, 2};
        auto [a, b] = p; // 拷贝 p 到隐藏对象 e，a 是 e.x 的别名，b 是 e.y 的别名
        a = 10;          // 只修改 e.x，p.x 不变

        assert(p.x == 1);
    }
    // auto& [] —— 引用原始对象
    {
        Point p{1, 2};
        auto &[a, b] = p; // e 是 p 的引用，a 直接是 p.x 的别名
        a = 10;           // 修改了 p.x

        assert(p.x == 10);
    }
    // auto&& [] —— 转发引用
    {
        auto &&[a, b] = Point{3, 4}; // e 是右值引用，临时对象生命期延长
        a = 5;                       // 修改临时对象（有用，若通过函数返回则不影响原对象）
        Point p{6, 7};
        auto &&[c, d] = p; // 等价于 auto&，c,d 引用 p 的成员
    }

    // NOTE: 现成包也是一样的
    {
        struct C
        {
            int x, y, z;
            std::string s = "";
        };

        //NOTE: 只能在模板内部使用
        // auto [a, ... b] = C{1, 2, 3}; // a 绑定 x, ...b 是“包”，展开后指代 y, z
        // auto [... c, d] = C{1, 2, 3}; // ...c 包指代 x, y，d 指代 z
        // auto [e, f, ... g] = C{};     // g 是空包，指代 z 后的元素（这里为空）
        [](auto &&c) {
            auto [a, ... _, b] = c;
            static_assert(not std::is_reference_v<decltype(a)>);
            static_assert(not std::is_reference_v<decltype(b)>);
        }(C{1, 2, 3, ""});
        [](auto &&c) {
            auto [a, ... _, b] = c;
            static_assert(not std::is_reference_v<decltype(a)>);
            static_assert(not std::is_reference_v<decltype(b)>);
        }(C{1, 2, 3});
        [](auto &&c) {
            auto [a, ... _, b] = c;
            static_assert(not std::is_reference_v<decltype(a)>);
            static_assert(not std::is_reference_v<decltype(b)>);
        }(C{1, 2, 3});

        [](auto &&c) {
            auto [a, ... _, b] = std::forward<decltype(c)>(c);
            static_assert(not std::is_reference_v<decltype(a)>);
            static_assert(not std::is_reference_v<decltype(b)>);
        }(C{1, 2, 3, ""});
        [](auto &&c) {
            // auto &[a, ... _, b] = std::forward<decltype(c)>(c); //NOTE: 编译失败
            // static_assert(not std::is_reference_v<decltype(a)>);
            // static_assert(not std::is_reference_v<decltype(b)>);
        }(C{1, 2, 3});
        [](auto &&c) {
            auto &&[a, ... _, b] = std::forward<decltype(c)>(c);
            static_assert(not std::is_reference_v<decltype(a)>);
            static_assert(not std::is_reference_v<decltype(b)>);
        }(C{1, 2, 3});

        {
            C c{1, 2, 3};
            [](auto &&c) {
                auto [a, ... _, b] = std::forward<decltype(c)>(c);
                static_assert(not std::is_reference_v<decltype(a)>);
                static_assert(not std::is_reference_v<decltype(b)>);
            }(c);
            [](auto &&c) {
                auto &[a, ... _, b] = std::forward<decltype(c)>(c);
                static_assert(not std::is_reference_v<decltype(a)>);
                static_assert(not std::is_reference_v<decltype(b)>);
            }(c);
            [](auto &&c) {
                auto &&[a, ... _, b] = std::forward<decltype(c)>(c);
                static_assert(not std::is_reference_v<decltype(a)>);
                static_assert(not std::is_reference_v<decltype(b)>);
            }(c);
            // NOTE: 测试修改. 静态断言说明不了什么
            [](auto &&c) {
                auto [a, ... _, b] = std::forward<decltype(c)>(c);
                static_assert(not std::is_reference_v<decltype(a)>);
                static_assert(not std::is_reference_v<decltype(b)>);

                a = 100;
                b = "str";
            }(c);
            assert(c.x == 1);
            assert(c.s == "");

            [](auto &&c) {
                auto &[a, ... _, b] = std::forward<decltype(c)>(c);
                static_assert(not std::is_reference_v<decltype(a)>);
                static_assert(not std::is_reference_v<decltype(b)>);

                a = 100;
                b = "str";
            }(c);
            assert(c.x == 100);
            assert(c.s == "str");

            [](auto &&c) {
                auto &&[a, ... _, b] = std::forward<decltype(c)>(c);
                static_assert(not std::is_reference_v<decltype(a)>);
                static_assert(not std::is_reference_v<decltype(b)>);

                a = 10;
                b = "s";
            }(c);
            assert(c.x == 10);
            assert(c.s == "s");
        }
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
} // NOLINTEND