// test_function_ref_internals.cpp - 基于 GCC bits/funcref_impl.h 的深入测试
// 编译: g++ -std=c++26 -Wall -Wextra -O2 test_function_ref_internals.cpp -o test.exe
// 运行: ./test.exe

#include <cstdint>
#include <functional>
#include <iostream>
#include <type_traits>
#include <random>

// NOLINTBEGIN
#include <meta>
#include <vector>
struct static_string
{
    const char *value{}; // NOLINT

    template <size_t N>
    consteval static_string(const char (&str)[N]) noexcept // NOLINT
        : value{std::define_static_string(str)}
    {
    }
    [[nodiscard]] constexpr std::string_view view() const noexcept
    {
        return std::string_view{value};
    }
    constexpr bool operator==(const static_string &o) const noexcept
    {
        return view() == o;
    }
    constexpr bool operator==(const std::string_view &o) const noexcept
    {
        return view() == o;
    }
    template <size_t N>
    consteval bool operator==(const char (&str)[N]) const noexcept // NOLINT
    {
        return view() == std::string_view{str, N - 1};
    }
};

// ============================================================================
// 测试1: _S_nttp 路径 (constant_wrapper + 静态函数，无绑定对象)
// 源码: function_ref(constant_wrapper<__cwfn, _Fn>) -> _M_invoke = &_Invoker::_S_nttp<__fn>;
// ============================================================================
static int add(int a, int b)
{
    return a + b;
}
static int mul(int a, int b)
{
    return a * b;
}
static int no_throw_add(int a, int b) noexcept
{
    return a + b;
}

void test_nttp_path()
{
    std::cout << "\n=== 测试1: _S_nttp 路径 (constant_wrapper + 静态函数) ===\n";
    constexpr std::function_ref<int(int, int)> ref_add = std::constant_wrapper<add>{};
    constexpr std::function_ref<int(int, int)> ref_mul = std::constant_wrapper<mul>{};
    constexpr std::function_ref<int(int, int) noexcept> ref_noexcept =
        std::constant_wrapper<no_throw_add>{};

    constexpr std::function_ref<int(int, int)> ref_noexcept2 [[maybe_unused]] =
        std::constant_wrapper<no_throw_add>{};

    std::cout << "add(3,4) = " << ref_add(3, 4) << std::endl;               // 7
    std::cout << "mul(3,4) = " << ref_mul(3, 4) << std::endl;               // 12
    std::cout << "no_throw_add(5,6) = " << ref_noexcept(5, 6) << std::endl; // 11

    static_assert(sizeof(ref_add) == 2 * sizeof(void *));
    std::cout << "路径正常，无绑定对象。\n";
}

// ============================================================================
// 测试2: _S_ptrs 路径 (普通函数指针直接构造)
// 源码: function_ref(_Fn* __fn) -> _M_invoke = _Invoker::_S_ptrs<_Fn*>();
// ============================================================================
int square(int x)
{
    return x * x;
}
int cube(int x)
{
    return x * x * x;
}
int no_throw_square(int x) noexcept
{
    return x * x;
}

void test_ptrs_path()
{
    std::cout << "\n=== 测试2: _S_ptrs 路径 (函数指针直接构造) ===\n";
    std::function_ref<int(int)> ref_square = square;
    std::function_ref<int(int)> ref_cube = cube;
    std::function_ref<int(int) noexcept> ref_noexcept = no_throw_square;

    std::cout << "square(5) = " << ref_square(5) << std::endl;            // 25
    std::cout << "cube(3) = " << ref_cube(3) << std::endl;                // 27
    std::cout << "no_throw_square(6) = " << ref_noexcept(6) << std::endl; // 36

    // 验证 noexcept 约束：不能从非 noexcept 构造 noexcept
    static_assert(
        !std::is_constructible_v<std::function_ref<int(int) noexcept>, decltype(square)>);
    static_assert(
        std::is_constructible_v<std::function_ref<int(int)>, decltype(no_throw_square)>);
    std::cout << "noexcept 约束正确。\n";
}

// ============================================================================
// 测试3: _S_static 路径 (无捕获 lambda / 无状态仿函数)
// 源码: 在 function_ref(_Fn&&) 中，如果 _Fd::operator() 存在且无捕获，走 _S_static
// ============================================================================
struct StatelessFunctor
{
    int operator()(int x) const
    {
        return x + 100;
    }
};

void test_static_path()
{
    std::cout << "\n=== 测试3: _S_static 路径 (无状态可调用对象) ===\n";
    // 无捕获 lambda (C++20 起无捕获 lambda 可默认构造，且 operator() 为 static? 实际触发 _S_static)
    auto lambda = [](int x) {
        return x * 2;
    };
    std::function_ref<int(int)> ref_lambda = lambda;

    StatelessFunctor functor;
    std::function_ref<int(int)> ref_functor = functor;

    std::cout << "lambda(10) = " << ref_lambda(10) << std::endl; // 20
    std::cout << "functor(5) = " << ref_functor(5) << std::endl; // 105

    // 注意：有捕获的 lambda 会走 _S_ptrs 路径（绑定对象指针）
    int capture = 42;
    auto capturing = [capture](int x) {
        return x + capture;
    };
    std::function_ref<int(int)> ref_capturing = capturing;
    static_assert(sizeof(ref_capturing) == 2 * sizeof(void *));
    std::cout << "capturing lambda(8) = " << ref_capturing(8) << std::endl; // 50
    std::cout << "有捕获 lambda 走 _S_ptrs 路径。\n";
}

// ============================================================================
// 测试4: _S_bind_ref 路径 (constant_wrapper + 成员函数 + 对象引用)
// 源码: function_ref(constant_wrapper<__cwfn, _Fn>, _Up&& __ref)
//       当 _Fn 是成员函数指针时，走 _S_bind_ref<__fn, _Td&> 路径
// ============================================================================
struct Widget
{
    int factor = 10;
    int multiply(int x) const
    {
        return factor * x;
    }
    int add(int x) const
    {
        return factor + x;
    }
    int square(int x)
    {
        return x * x;
    } // 非 const 成员函数
};

void test_bind_ref_path()
{
    std::cout
        << "\n=== 测试4: _S_bind_ref 路径 (constant_wrapper + 成员函数 + 对象引用) ===\n";
    Widget w;

    // 绑定 const 成员函数 + 对象引用
    std::function_ref<int(int) const> ref_multiply(
        std::constant_wrapper<&Widget::multiply>{}, w);
    std::function_ref<int(int) const> ref_add(std::constant_wrapper<&Widget::add>{}, w);

    //NOTE: 编译错误
    // std::function_ref<int(int) const> ref_square_(std::constant_wrapper<&Widget::square>{},
    //                                              w);

    std::cout << "multiply(5) = " << ref_multiply(5) << std::endl; // 50
    std::cout << "add(5) = " << ref_add(5) << std::endl;           // 15

    // 绑定非 const 成员函数：签名需要匹配非 const
    std::function_ref<int(int)> ref_square(std::constant_wrapper<&Widget::square>{}, w);
    std::cout << "square(6) = " << ref_square(6) << std::endl; // 36

    // 绑定 const 对象
    const Widget cw{20};
    std::function_ref<int(int) const> ref_const(
        std::constant_wrapper<&Widget::multiply>{}, cw);
    std::cout << "const object multiply(3) = " << ref_const(3) << std::endl; // 60

    std::cout << "_S_bind_ref 路径正常工作。\n";
}

// ============================================================================
// 附加测试: __is_std_op_wrapper 特化 (从 std::function / std::move_only_function 构造)
// 源码中 __is_std_op_wrapper 为 true 时使用 _S_nttp<_Fd{}> 路径
// ============================================================================

void test_std_wrapper_path()
{
    std::cout << "\n=== 附加: __is_std_op_wrapper 特化路径 ===\n";
    std::function<int(int)> func = [](int x) {
        return x * x;
    };
    std::function_ref<int(int)> ref_func = func;
    std::cout << "from std::function(7) = " << ref_func(7) << std::endl; // 49

#ifdef __cpp_lib_move_only_function
    std::move_only_function<int(int)> mof = [](int x) {
        return x + 3;
    };
    std::function_ref<int(int)> ref_mof = mof;
    std::cout << "from move_only_function(5) = " << ref_mof(5) << std::endl; // 8
#endif
    std::cout << "特化路径使用成功。\n";
}

template <typename T>
void print_size(const char *name)
{
    std::cout << name << " : " << sizeof(T) << " bytes" << std::endl;
}

void test_size()
{
    // 基础验证
    static_assert(sizeof(std::function_ref<int(int)>) == 2 * sizeof(void *));
    static_assert(sizeof(std::function_ref<void()>) == 2 * sizeof(void *));

    // 1. 无捕获 lambda -> function_ref 大小仍为 2*void*
    auto lambda0 = [](int x) {
        return x * 2;
    };
    std::function_ref<int(int)> ref0 = lambda0;
    print_size<decltype(ref0)>("无捕获 lambda");

    // 2. 捕获一个变量 -> 仍为 2*void*
    int a = 42;
    auto lambda1 = [a](int x) {
        return x + a;
    };
    std::function_ref<int(int)> ref1 = lambda1;
    print_size<decltype(ref1)>("捕获一个变量");

    // 3. 捕获多个变量
    double b = 3.14;
    auto lambda2 = [a, b](int x) {
        return x + a + static_cast<int>(b);
    };
    std::function_ref<int(int)> ref2 = lambda2;
    print_size<decltype(ref2)>("捕获多个变量");

    // 4. 捕获引用
    auto lambda3 = [&a](int x) {
        return x + a;
    };
    std::function_ref<int(int)> ref3 = lambda3;
    print_size<decltype(ref3)>("捕获引用");

    // 5. mutable lambda
    auto lambda4 = [a](int x) mutable {
        a += x;
        return a;
    };
    std::function_ref<int(int)> ref4 = lambda4;
    print_size<decltype(ref4)>("mutable lambda");

    // 6. constexpr lambda (C++17)
    auto lambda5 = [](int x) constexpr {
        return x * x;
    };
    std::function_ref<int(int)> ref5 = lambda5;
    print_size<decltype(ref5)>("constexpr lambda");

    // 7. 泛型 lambda
    auto lambda6 = [](auto x) {
        return x * 2;
    };
    std::function_ref<int(int)> ref6 = lambda6; // 实例化后大小仍相同
    print_size<decltype(ref6)>("泛型 lambda (实例化)");

    // 8. 从 std::function 包装
    std::function<int(int)> func = [](int x) {
        return x;
    };
    std::function_ref<int(int)> ref_func = func;
    print_size<decltype(ref_func)>("std::function");

    // 9. 带 noexcept 的 lambda
    auto lambda7 = [](int x) noexcept {
        return x;
    };
    std::function_ref<int(int) noexcept> ref7 = lambda7;
    print_size<decltype(ref7)>("noexcept lambda");

    // 10. const 限定符的 function_ref 包装 lambda (lambda 默认 operator() const)
    std::function_ref<int(int) const> ref_const = [](int x) {
        return x;
    };
    print_size<decltype(ref_const)>("const 限定 function_ref");

    // 11. 可变参数 lambda
    auto lambda8 = [](int x, int y, int z) {
        return x + y + z;
    };
    std::function_ref<int(int, int, int)> ref8 = lambda8;
    print_size<decltype(ref8)>("可变参数 lambda");

    std::cout << "\n所有类型大小均为 " << 2 * sizeof(void *) << " 字节" << std::endl;
}

namespace math
{
    static int add(int a, int b)
    {
        return a + b;
    }
    static int sub(int a, int b)
    {
        return a - b;
    }
} // namespace math

namespace io
{
    static int print_and_return(int a, int b)
    {
        std::cout << "io::print_and_return(" << a << ',' << b << ")\n";
        return a + b;
    }
} // namespace io

void test_vector()
{
    // 使用完全限定名直接获取函数指针，然后放入 function_ref 数组
    std::vector<std::function_ref<int(int, int)>> ops;
    ops.push_back(math::add);
    ops.push_back(math::sub);
    ops.push_back(io::print_and_return);

    auto lambda = [c = 20](int a, int b) {
        std::cout << "lambda invoke....\n";
        return a + b + c;
    };
    ops.push_back(lambda);

    // 统一调用
    int a = 10, b = 5;
    for (auto &op : ops)
    {
        std::cout << op(a, b) << '\n';
    }
}

struct object_handle
{
    uint32_t id;
};
template <static_string name>
struct dispatch_type
{
    static constexpr auto class_name = name;
    uint32_t id;
};
static_assert(sizeof(object_handle) == sizeof(dispatch_type<"name">));

struct A
{
    int x;
};
struct ext_A_y
{
    int y;
};

struct B
{
    std::string name;
};

template <typename World, typename... Components>
struct function_set;

// 单组件 A
template <typename World>
struct function_set<World, A>
{
    static void update(World &w, size_t index)
    {
        A &a = w.a[index];
        std::cout << "A.x = " << a.x << '\n';
    }
};

// 组件 A + ext_A_y
template <typename World>
struct function_set<World, A, ext_A_y>
{
    static void update(World &w, size_t idx_a, size_t idx_y)
    {
        A &a = w.a[idx_a];
        ext_A_y &y = w.ext_a[idx_y];
        std::cout << "A.x = " << a.x << ", ext_A_y.y = " << y.y << '\n';
    }
};

// 单组件 B
template <typename World>
struct function_set<World, B>
{
    static void update(World &w, size_t index)
    {
        B &b = w.b[index];
        std::cout << "B.name = " << b.name << '\n';
    }
};

void test_ecs()
{
    struct world_type
    {
        std::vector<A> a;
        std::vector<ext_A_y> ext_a;
        std::vector<B> b;
    };
    world_type w{.a = {{1}, {2}}, .ext_a = {{3}}, .b = {{"b_name"}}};

    [](auto t) {
    }(std::constant_wrapper<[](world_type w) noexcept {
        function_set<world_type, A>::update(w, 0);
    }>{});

    std::vector<std::function_ref<void(world_type w) noexcept>> execution;
    {
        execution.push_back(std::constant_wrapper<[](world_type w) noexcept {
            function_set<world_type, A>::update(w, 0);
        }>{});
        execution.push_back(std::constant_wrapper<[](world_type w) noexcept {
            function_set<world_type, A, ext_A_y>::update(w, 1, 0);
        }>{});
        execution.push_back(std::constant_wrapper<[](world_type w) noexcept {
            function_set<world_type, B>::update(w, 0);
        }>{});

        // NOTE: auto 自动解析成 world_type。完美
        execution.push_back(std::constant_wrapper<[](auto w) noexcept {
            function_set<world_type, B>::update(w, 0);
        }>{});
    }
    auto Heartbeat = [&]() {
        std::random_device rd;
        std::mt19937 g1(rd()); //NOTE: 模拟重排任务
        std::shuffle(execution.begin(), execution.end(), g1);

        std::cout << "Heartbeat: ...\n";
        for (auto e : execution)
            e(w);
    };
    Heartbeat();
    Heartbeat();
}

// ============================================================================
// 主函数
// ============================================================================
int main()
{
    std::cout << "====== 基于 GCC bits/funcref_impl.h 的深入测试 ======\n";
    test_nttp_path();
    test_ptrs_path();
    test_static_path();
    test_bind_ref_path();
    test_std_wrapper_path();
    std::cout << "\n所有测试完成，内部路径已验证。\n";

    auto f1 = [](int x) {
        return x * 2;
    };
    auto f2 = [](int x) {
        return x + 3;
    };
    std::vector<std::function_ref<int(int)>> refs;
    refs.push_back([&](int x) { return f1(x); }); //NOTE: 未定义行为吧？
    {

        [[maybe_unused]] std::function_ref<int(int)> arr[] = {f1, f2}; // 大小推导为2
    }

    test_size();
    test_vector();
    test_ecs();
    return 0;
} // NOLINTEND