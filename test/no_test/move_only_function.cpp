#include <cassert>
#include <functional> // std::move_only_function
#include <memory>
#include <iostream>

// NOLINTBEGIN
// 用于测试异常规格的辅助类型
struct nothrow_callable
{
    int operator()(int x) const noexcept
    {
        return x + 1;
    }
};

struct may_throw_callable
{
    int operator()(int x) const
    {
        return x + 2;
    }
};

// 测试 const / ref 限定
struct qualified_callable
{
    int value = 10;

    int operator()(int x) &
    {
        return value + x;
    }
    int operator()(int x) const &
    {
        return value + x + 100;
    }
    int operator()(int x) &&
    {
        return value + x + 200;
    }
};

int main()
{
    // =========================================================
    // 1. 默认构造和 nullptr
    // =========================================================
    {
        std::move_only_function<void()> f;
        assert(!f);
        assert(f == nullptr);
        assert(nullptr == f);
    }

    // =========================================================
    // 2. 存储简单函数指针
    // =========================================================
    {
        auto twice = [](int x) -> int {
            return x * 2;
        };
        std::move_only_function<int(int)> f = twice;
        assert(f);
        assert(f(5) == 10);

        // 移动后源变为空
        auto f2 = std::move(f);
        assert(!f);
        assert(f2(5) == 10);
    }

    // =========================================================
    // 3. 存储捕获 unique_ptr 的 lambda（不可拷贝）
    // =========================================================
    {
        auto p = std::make_unique<int>(42);
        auto lambda = [u = std::move(p)](int x) {
            return x + *u;
        };
        std::move_only_function<int(int)> f = std::move(lambda);
        assert(f(10) == 52);

        // 移动构造
        auto f2 = std::move(f);
        assert(!f);
        assert(f2(0) == 42);
    }

    // =========================================================
    // 4. 存储状态 lambda（引用捕获），注意生命周期
    // =========================================================
    {
        int counter = 0;
        auto lambda = [&counter](int x) {
            counter += x;
            return counter;
        };
        std::move_only_function<int(int)> f = std::move(lambda);
        f(10);
        f(5);
        assert(counter == 15);
    }

    // =========================================================
    // 5. 移动赋值
    // =========================================================
    {
        std::move_only_function<int(int)> f1 = [](int x) {
            return x * 3;
        };
        std::move_only_function<int(int)> f2;
        f2 = std::move(f1);
        assert(!f1);
        assert(f2(4) == 12);

        // 自我赋值安全
        f2 = std::move(f2);
        assert(f2(4) == 12); // 仍然可用（实现保证）
    }

    // =========================================================
    // 6. swap
    // =========================================================
    {
        std::move_only_function<int(int)> f1 = [](int x) {
            return x + 1;
        };
        std::move_only_function<int(int)> f2 = [](int x) {
            return x * 10;
        };
        f1.swap(f2);
        assert(f1(5) == 50);
        assert(f2(5) == 6);

        using std::swap;
        swap(f1, f2);
        assert(f1(5) == 6);
        assert(f2(5) == 50);
    }

    // =========================================================
    // 7. const 限定版本
    // =========================================================
    {
        // const & 调用
        std::move_only_function<int(int) const> f = [v = 10](int x) {
            return x + v;
        };
        assert(f(5) == 15);
        const auto &cf = f;
        assert(cf(5) == 15); // 通过 const 引用调用
    }

    // =========================================================
    // 8. ref 限定版本（& 或 &&）
    // =========================================================
    {
        // 左值引用限定
        std::move_only_function<int(int) &> f = qualified_callable{20};
        assert(f(5) == 25); // 调用 operator() &

        // 右值引用限定
        std::move_only_function<int(int) &&> g = qualified_callable{30};
        assert(std::move(g)(5) == 235); // 调用 operator() && (30+5+200)
        // g 可能仍有效，但不应再使用（标准不保证）
    }

    // =========================================================
    // 9. noexcept 限定
    // =========================================================
    {
        // 强制要求 noexcept，只能包装 noexcept 可调用对象
        std::move_only_function<int(int) noexcept> f1 = nothrow_callable{};
        assert(f1(5) == 6);

        // 下面的代码应该编译失败（如果取消注释）：
        // std::move_only_function<int(int) noexcept> f2 = may_throw_callable{};
        // 错误：may_throw_callable 的 operator() 不是 noexcept
    }

    // =========================================================
    // 10. 存储成员函数指针（通过 lambda 包装）
    // =========================================================
    {
        struct Foo
        {
            int add(int a, int b) const
            {
                return a + b;
            }
        };
        Foo foo;
        auto lambda = [&foo](int a, int b) {
            return foo.add(a, b);
        };
        std::move_only_function<int(int, int)> f = std::move(lambda);
        assert(f(3, 4) == 7);
    }

    // =========================================================
    // 11. 返回 void
    // =========================================================
    {
        int side_effect = 0;
        std::move_only_function<void(int)> f = [&side_effect](int x) {
            side_effect = x;
        };
        f(42);
        assert(side_effect == 42);
    }

    // =========================================================
    // 12. 空状态调用（不测试，因为未定义行为）
    // 仅验证空对象可以安全赋值
    {
        std::move_only_function<int()> f;
        f = [] {
            return 7;
        };
        assert(f() == 7);
    }

    // =========================================================
    // 13. 使用 in_place_type 构造
    // =========================================================
    {
        struct S
        {
            int val;
            S(int v) : val(v) {}
            int operator()(int x) const
            {
                return val + x;
            }
        };
        std::move_only_function<int(int)> f(std::in_place_type<S>, 100);
        assert(f(5) == 105);
    }
    { // const 包装器
        int a = 0;
        std::move_only_function<void() const> f2 = [&]() {
            a = 1;
        };
        f2();
        assert(a == 1);
    }

    std::cout << "All move_only_function tests passed!\n";
    return 0;
} // NOLINTEND