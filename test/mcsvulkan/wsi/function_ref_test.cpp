// function_ref_test.cpp
// 编译: g++ -std=c++26 -Wall -Wextra -O2 function_ref_test.cpp -o function_ref_test

#include <functional>
#include <iostream>
#include <string>
#include <cassert>
#include <vector>
#include <utility>
#include <cstddef>

// NOLINTBEGIN

// ============================================================
// 待测函数：接收任意 F，在内部显式构造 std::function_ref
// ============================================================
template <class R, class... Args, class F>
R call(F &&f, Args... args)
{
    std::function_ref<R(Args...)> ref(std::forward<F>(f));
    return ref(args...);
}

// 编译期版本
template <class F, class... Args>
constexpr auto call_cexpr(F &&f, Args &&...args)
{
    return std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
}

// ============================================================
// 测试辅助
// ============================================================
int add_one(int x)
{
    return x + 1;
}

struct Multiplier
{
    int k;
    int operator()(int x) const
    {
        return x * k;
    }
};

struct Widget
{
    int value;
    int get() const
    {
        return value;
    }
};

// ============================================================
// 1. 普通函数
// ============================================================
void test_free_function()
{
    auto r = call<int, int>(add_one, 10);
    assert(r == 11);
    std::cout << "[OK] free function\n";
}

// ============================================================
// 2. 无捕获 lambda
// ============================================================
void test_captureless_lambda()
{
    auto lam = [](int x) {
        return x * 2;
    };
    auto r = call<int, int>(lam, 5);
    assert(r == 10);
    std::cout << "[OK] captureless lambda\n";
}

// ============================================================
// 3. 无捕获 lambda 经函数指针
// ============================================================
void test_captureless_lambda_via_fp()
{
    int (*fp)(int) = [](int x) {
        return x - 1;
    };
    auto r = call<int, int>(fp, 10);
    assert(r == 9);
    std::cout << "[OK] captureless lambda via function pointer\n";
}

// ============================================================
// 4. 有捕获 lambda
// ============================================================
void test_capturing_lambda()
{
    int base = 100;
    auto lam = [base](int x) {
        return base + x;
    };
    auto r = call<int, int>(lam, 23);
    assert(r == 123);
    std::cout << "[OK] capturing lambda\n";
}

// ============================================================
// 5. 函数对象
// ============================================================
void test_functor()
{
    Multiplier m{7};
    auto r = call<int, int>(m, 6);
    assert(r == 42);
    std::cout << "[OK] functor\n";
}

// ============================================================
// 6. 泛型 lambda
// ============================================================
void test_generic_lambda()
{
    auto generic = [](auto x) {
        return x + x;
    };
    auto r1 = call<int, int>(generic, 21);
    assert(r1 == 42);
    auto r2 = call<double, double>(generic, 1.5);
    assert(r2 == 3.0);
    std::cout << "[OK] generic lambda\n";
}

// ============================================================
// 7. 多参数
// ============================================================
void test_multiple_args()
{
    auto lam = [](int a, int b, int c) {
        return a + b + c;
    };
    auto r = call<int, int, int, int>(lam, 1, 2, 3);
    assert(r == 6);
    std::cout << "[OK] multiple args\n";
}

// ============================================================
// 8. 返回 void
// ============================================================
void test_void_return()
{
    int sink = 0;
    auto lam = [&sink](int x) {
        sink = x;
    };
    call<void, int>(lam, 99);
    assert(sink == 99);
    std::cout << "[OK] void return\n";
}

// ============================================================
// 9. 返回引用
// ============================================================
void test_return_reference()
{
    static int storage = 5;
    auto lam = [](int &r) -> int & {
        return r;
    };
    int &ref = call<int &, int &>(lam, storage);
    ref = 77;
    assert(storage == 77);
    std::cout << "[OK] return reference\n";
}

// ============================================================
// 10. 引用参数
// ============================================================
void test_reference_arg()
{
    auto lam = [](int &x) {
        x += 10;
    };
    int v = 5;
    call<void, int &>(lam, v);
    assert(v == 15);
    std::cout << "[OK] reference arg\n";
}

// ============================================================
// 11. const 引用参数
// ============================================================
void test_const_reference_arg()
{
    auto lam = [](const std::string &s) {
        return s.size();
    };
    std::string s = "hello";
    auto r = call<std::size_t, const std::string &>(lam, s);
    assert(r == 5);
    std::cout << "[OK] const reference arg\n";
}

// ============================================================
// 12. 同一签名下混用多种 callable
// ============================================================
void test_mixed_callables_same_signature()
{
    int offset = 1;
    auto capturing = [offset](int x) {
        return x + offset;
    };
    struct Adder
    {
        int operator()(int x) const
        {
            return x + 2;
        }
    };
    Adder adder{};

    std::vector<std::function_ref<int(int)>> refs;
    refs.emplace_back(capturing);
    refs.emplace_back(adder);
    refs.emplace_back(add_one);

    auto r0 = call<int, int>(refs[0], 10);
    assert(r0 == 11);
    auto r1 = call<int, int>(refs[1], 10);
    assert(r1 == 12);
    auto r2 = call<int, int>(refs[2], 10);
    assert(r2 == 11);
    std::cout << "[OK] mixed callables under same signature\n";
}

// ============================================================
// 13. 成员函数指针（用 lambda 包一层）
// ============================================================
void test_member_function_pointer()
{
    Widget w{42};
    auto mf = &Widget::get;
    auto lam = [mf](const Widget &obj) {
        return (obj.*mf)();
    };
    auto r = call<int, const Widget &>(lam, w);
    assert(r == 42);
    std::cout << "[OK] member function pointer\n";
}
// void test_member_function_pointer_direct()
// {
//     Widget w{42};
//     auto r = call<int, const Widget &>(&Widget::get, w);
//     assert(r == 42);
//     std::cout << "[OK] member function pointer direct\n";
// }

template <auto Mf>
struct MemberFn
{
    template <class Obj, class... A>
    auto operator()(Obj &&obj, A &&...a) const
        -> decltype(std::invoke(Mf, std::forward<Obj>(obj), std::forward<A>(a)...))
    {
        return std::invoke(Mf, std::forward<Obj>(obj), std::forward<A>(a)...);
    }
};

void test_member_function_pointer_adapter()
{
    Widget w{42};
    auto r = call<int, const Widget &>(MemberFn<&Widget::get>{}, w);
    assert(r == 42);
    std::cout << "[OK] member function pointer via adapter\n";
}

// ============================================================
// 14. 临时 lambda 作为实参
// ============================================================
void test_temp_lambda_as_argument()
{
    auto r = call<int, int>([](int x) { return x + 1; }, 10);
    assert(r == 11);
    std::cout << "[OK] temporary lambda as argument\n";
}

// ============================================================
// 15. 编译期求值路径
// ============================================================
void test_constexpr_path()
{
    constexpr auto lam = [](int x) {
        return x + 1;
    };
    static_assert(call_cexpr(lam, 10) == 11);
    std::cout << "[OK] constexpr path\n";
}

// ============================================================
// main
// ============================================================
int main()
{
    test_free_function();
    test_captureless_lambda();
    test_captureless_lambda_via_fp();
    test_capturing_lambda();
    test_functor();
    test_generic_lambda();
    test_multiple_args();
    test_void_return();
    test_return_reference();
    test_reference_arg();
    test_const_reference_arg();
    test_mixed_callables_same_signature();

    test_member_function_pointer();
    test_member_function_pointer_adapter();

    test_temp_lambda_as_argument();
    test_constexpr_path();

    std::cout << "\nall tests passed\n";
    return 0;
} // NOLINTEND