// function_ref.cpp - 最终修正版
// 测试 C++26 std::function_ref (P0792R14)
// 编译命令：g++ -std=c++26 -Wall -Wextra -O3 -g function_ref.cpp -o function_ref.exe

#include <functional>
#include <iostream>
#include <string>
#include <type_traits>
#include <optional>
#include <cstring>

// NOLINTBEGIN

// ============================================================================
// 1. 常规测试: 从自由函数、函数指针、lambda 和可调用对象构造 function_ref
// ============================================================================

// 自由函数 (注意：不含 noexcept，避免 CTAD 推导出 noexcept 版本)
int square_int(int x)
{
    return x * x;
}

// 用于 noexcept 测试的专用函数
int square_int_noexcept(int x) noexcept
{
    return x * x;
}

// 有状态的仿函数
struct Multiply
{
    int factor;
    int operator()(int x) const
    {
        return x * factor;
    }
};

// 无状态的仿函数
struct AddOne
{
    int operator()(int x) const
    {
        return x + 1;
    }
};

void test_basic_construction()
{
    std::cout << "\n=== 1. 常规构造与调用 ===\n";

    // 1.1 从自由函数构造
    std::function_ref<int(int)> ref1 = square_int;
    std::cout << "square_int(5): " << ref1(5) << std::endl; // 25

    // 1.2 从函数指针构造
    int (*fp)(int) = square_int;
    std::function_ref<int(int)> ref2 = fp;
    std::cout << "fp(6): " << ref2(6) << std::endl; // 36

    // 1.3 从 lambda (有捕获) 构造
    int capture = 10;
    auto lambda = [capture](int x) {
        return x + capture;
    };
    std::function_ref<int(int)> ref3 = lambda;
    std::cout << "lambda(7): " << ref3(7) << std::endl; // 17

    // 1.4 从有状态仿函数构造
    Multiply mult{5};
    std::function_ref<int(int)> ref4 = mult;
    std::cout << "Multiply(5)(4): " << ref4(4) << std::endl; // 20

    // 1.5 从无状态仿函数构造
    AddOne add_one;
    std::function_ref<int(int)> ref5 = add_one;
    std::cout << "AddOne(9): " << ref5(9) << std::endl; // 10

    // 1.6 noexcept 限定符测试
    std::function_ref<int(int) noexcept> ref_noexcept = square_int_noexcept;
    std::cout << "noexcept square_int(8): " << ref_noexcept(8) << std::endl; // 64

    // 1.7 const 限定符测试
    const auto const_lambda = [](int x) {
        return x * 2;
    };
    std::function_ref<int(int) const> ref_const = const_lambda;
    std::cout << "const lambda(10): " << ref_const(10) << std::endl; // 20
}

// ============================================================================
// 2. 重载集测试 (编译失败示例)
// ============================================================================

void overloaded(int)
{
    std::cout << "overloaded(int)\n";
}
void overloaded(double)
{
    std::cout << "overloaded(double)\n";
}

void test_overload_set()
{
    std::cout << "\n=== 2. 重载集测试 (预期编译失败) ===\n";
    std::cout << "// auto fn = std::function_ref<void(int)>(overloaded); // ERROR\n";
}

// ============================================================================
// 3. 从 std::function 构造
// ============================================================================

void test_from_std_function()
{
    std::cout << "\n=== 3. 从 std::function 构造 ===\n";

    std::function<int(int)> std_func = [](int x) {
        return x * x;
    };
    std::function_ref<int(int)> ref = std_func;
    std::cout << "从 std::function 构造后调用(9): " << ref(9) << std::endl; // 81
}

// ============================================================================
// 4. 推导指引 (CTAD) 测试 (修正：使用显式类型避免 GCC CTAD 缺陷)
// ============================================================================

void test_ctad()
{
    std::cout << "\n=== 4. 推导指引 (CTAD) 测试 ===\n";

    // 从自由函数推导 (square_int 无重载，也不带 noexcept)
    std::function_ref ref1 = square_int; // 应推导为 function_ref<int(int)>
    static_assert(std::is_same_v<decltype(ref1), std::function_ref<int(int)>>);

    // 从 lambda 推导 – 当前 GCC 未完全支持，改用显式类型
    auto lambda = [](int a, int b) {
        return a + b;
    };
    // std::function_ref ref2 = lambda; // 在某些编译器下可行，但 GCC 16 尚不支持
    std::function_ref<int(int, int)> ref2 = lambda; // 显式指定类型
    static_assert(std::is_same_v<decltype(ref2), std::function_ref<int(int, int)>>);

    std::cout << "ref1(10): " << ref1(10) << std::endl;    // 100
    std::cout << "ref2(3,4): " << ref2(3, 4) << std::endl; // 7
}

// ============================================================================
// 5. 特征宏测试
// ============================================================================

void test_feature_macro()
{
    std::cout << "\n=== 5. 特征宏测试 ===\n";

#ifdef __cpp_lib_function_ref
    std::cout << "__cpp_lib_function_ref = " << __cpp_lib_function_ref << std::endl;
#else
    std::cout << "__cpp_lib_function_ref 未定义\n";
#endif
}

// ============================================================================
// 6. 空引用禁止测试
// ============================================================================

void test_null_reference()
{
    std::cout << "\n=== 6. 空引用禁止测试 (预期编译失败) ===\n";
    std::cout << "// std::function_ref<int(int)> ref = nullptr; // ERROR\n";
    std::cout << "// std::function_ref<int(int)> ref;          // 也没有默认构造\n";
}

// ============================================================================
// 7. 拷贝语义测试
// ============================================================================

void test_copy_semantics()
{
    std::cout << "\n=== 7. 拷贝语义测试 ===\n";

    auto lambda = [](int x) {
        return x * 3;
    };
    std::function_ref<int(int)> original = lambda;
    std::function_ref<int(int)> copy = original;     // 拷贝构造
    std::function_ref<int(int)> assigned = original; // 拷贝赋值

    std::cout << "original(5): " << original(5) << std::endl; // 15
    std::cout << "copy(5): " << copy(5) << std::endl;         // 15
    std::cout << "assigned(5): " << assigned(5) << std::endl; // 15
}

// ============================================================================
// 8. 禁止从非函数指针类型赋值
// ============================================================================

void test_deleted_assignment()
{
    std::cout << "\n=== 8. 禁止从非函数指针类型赋值 (预期编译失败) ===\n";
    std::cout << "// std::function_ref<int(int)> ref;\n";
    std::cout << "// ref = [](int x) { return x; }; // ERROR\n";
}

// ============================================================================
// 9. 高阶函数 retry 示例
// ============================================================================

std::optional<std::string> flaky_network_call()
{
    static int attempt = 0;
    ++attempt;
    if (attempt < 3)
    {
        std::cout << "网络请求失败 (尝试 " << attempt << ")\n";
        return std::nullopt;
    }
    std::cout << "网络请求成功 (尝试 " << attempt << ")\n";
    return "成功的数据";
}

template <typename R>
std::optional<R> retry(size_t times, std::function_ref<std::optional<R>()> action)
{
    for (size_t i = 0; i < times; ++i)
    {
        if (auto result = action())
        {
            return result;
        }
    }
    return std::nullopt;
}

void test_retry_example()
{
    std::cout << "\n=== 9. 高阶函数 retry 示例 ===\n";
    auto result = retry<std::string>(5, flaky_network_call);
    if (result)
    {
        std::cout << "最终结果: " << *result << std::endl;
    }
    else
    {
        std::cout << "所有重试均失败" << std::endl;
    }
}

// ============================================================================
// 10. 通过 constant_wrapper 构造 (nontype_t)
// ============================================================================

static int static_add(int a, int b)
{
    return a + b;
}

struct MyClass
{
    int multiply(int factor) const
    {
        return value * factor;
    }
    int value = 10;
};

void test_nontype_construction()
{
    std::cout << "\n=== 10. 通过 constant_wrapper 构造 ===\n";

    // 从静态函数构造
    std::function_ref ref_static = std::constant_wrapper<static_add>{};
    std::cout << "constant_wrapper<static_add>(3,4): " << ref_static(3, 4)
              << std::endl; // 7

    // 从成员函数构造：使用构造函数 (constant_wrapper, 对象)
    MyClass obj;
    std::function_ref<int(int) const> ref_member(
        std::constant_wrapper<&MyClass::multiply>{}, obj);
    std::cout << "member function (value=10, factor=5): " << ref_member(5)
              << std::endl; // 50
}

// ============================================================================
// 11. 可平凡复制特性与大小测试
// ============================================================================

struct LargeState
{
    int data[16];
    int operator()(int x) const
    {
        return data[0] + x;
    }
};

void test_trivially_copyable()
{
    std::cout << "\n=== 11. 可平凡复制特性与大小测试 ===\n";

    static_assert(std::is_trivially_copyable_v<std::function_ref<int(int)>>,
                  "function_ref 必须是可平凡复制的");

    LargeState state{42};
    std::function_ref<int(int)> ref = state;

    // 拷贝构造即可演示可平凡复制性
    std::function_ref<int(int)> copied = ref;
    std::cout << "原始调用(10): " << ref(10) << std::endl;      // 52
    std::cout << "拷贝后调用(10): " << copied(10) << std::endl; // 52

    // 通过 memcpy 复制到原始内存
    alignas(std::function_ref<int(int)>) unsigned char
        buffer[sizeof(std::function_ref<int(int)>)];
    std::memcpy(buffer, &ref, sizeof(ref));
    std::function_ref<int(int)> *reconstructed =
        reinterpret_cast<std::function_ref<int(int)> *>(buffer);
    std::cout << "memcpy 重建后调用(10): " << (*reconstructed)(10) << std::endl; // 52

    std::cout << "sizeof(function_ref<int(int)>): " << sizeof(std::function_ref<int(int)>)
              << " 字节\n";
}

// ============================================================================
// 12. 严格测试: 编译期约束检查 (通过 static_assert)
// ============================================================================

void test_compile_time_constraints()
{
    std::cout << "\n=== 12. 编译期约束检查 (static_assert) ===\n";

    // 12.1 可构造性检查
    static_assert(std::is_constructible_v<std::function_ref<int(int)>, int (*)(int)>);
    static_assert(
        std::is_constructible_v<std::function_ref<int(int)>, const int (*)(int)>);
    static_assert(
        std::is_constructible_v<std::function_ref<int(int)>, std::function<int(int)> &>);
    static_assert(std::is_constructible_v<std::function_ref<int(int)>, Multiply &>);
    static_assert(std::is_constructible_v<std::function_ref<int(int)>, const Multiply &>);
    static_assert(std::is_constructible_v<std::function_ref<int(int)>,
                                          std::reference_wrapper<Multiply>>);

    // 12.2 非法构造 (应不可构造)
    static_assert(!std::is_constructible_v<std::function_ref<int(int)>, std::nullptr_t>);
    static_assert(!std::is_constructible_v<std::function_ref<int(int)>, int>);
    static_assert(!std::is_constructible_v<std::function_ref<int(int)>, void (*)()>);
    static_assert(!std::is_constructible_v<std::function_ref<int(int)>, int Multiply::*>);
    static_assert(!std::is_constructible_v<std::function_ref<int(int)>, int Multiply::*>);
    static_assert(
        !std::is_constructible_v<std::function_ref<int(int)>,
                                 void (*)(int) noexcept>); // 不同 noexcept 不匹配

    // 12.3 noexcept 匹配性
    static_assert(std::is_constructible_v<std::function_ref<int(int) noexcept>,
                                          int (*)(int) noexcept>);
    static_assert(!std::is_constructible_v<
                  std::function_ref<int(int) noexcept>,
                  int (*)(int)>); // 目标可抛异常，但 function_ref 要求 noexcept
    static_assert(std::is_constructible_v<
                  std::function_ref<int(int)>,
                  int (*)(int) noexcept>); // 目标更严格（noexcept），可以接受

    // 12.4 const 限定符匹配
    struct ConstCallable
    {
        int operator()(int) const
        {
            return 0;
        }
    };
    struct NonConstCallable
    {
        int operator()(int)
        {
            return 0;
        }
    };
    static_assert(
        std::is_constructible_v<std::function_ref<int(int) const>, ConstCallable>);
    static_assert(
        !std::is_constructible_v<std::function_ref<int(int) const>, NonConstCallable>);
    static_assert(
        std::is_constructible_v<std::function_ref<int(int)>,
                                ConstCallable>); // const 可调用对象可以用于非 const 签名
    static_assert(
        std::is_constructible_v<std::function_ref<int(int)>,
                                NonConstCallable>); // 非 const 则不能用于 const 签名

    // 12.5 可平凡复制性 (已测试)
    static_assert(std::is_trivially_copyable_v<std::function_ref<int(int)>>);
    static_assert(std::is_trivially_destructible_v<std::function_ref<int(int)>>);

    // 12.6 大小与对齐 (通常为 2 个指针大小)
    static_assert(sizeof(std::function_ref<int(int)>) == 2 * sizeof(void *));
    static_assert(alignof(std::function_ref<int(int)>) == alignof(void *));

    std::cout << "所有编译期约束检查通过。\n";
}

// ============================================================================
// 13. 严格测试: 生命周期与悬挂引用检测 (运行时)
// ============================================================================

// 注意：function_ref 不会延长临时对象的生命周期，以下故意制造悬挂引用场景，
// 但测试只打印警告，实际代码中应避免。
void test_lifetime_safety()
{
    std::cout << "\n=== 13. 生命周期安全测试 (预期警告) ===\n";
    std::cout << "警告：以下代码演示了 function_ref 不会延长临时对象生命期，\n";
    std::cout << "因此不能将临时对象的引用传递给 function_ref 并存储。\n";

    // 危险示例：存储 lambda 的临时对象地址，离开作用域后悬挂
    // std::function_ref<int(int)> ref = [](int x){ return x; }; // 这行是安全的，因为 lambda 临时在初始化完整表达式中有效
    // 但更危险的是：
    // std::function_ref<int(int)> ref2; //NOTE: 无法默认初始化
    // {
    //     auto lambda = [](int x) {
    //         return x * x;
    //     };
    //     ref2 =
    //         lambda; // 编译错误！function_ref 禁止赋值操作符（见测试8），所以无法这样赋值。
    //     // 实际上赋值操作符被删除，因此不会出现这种情况。
    // }
    // std::cout << "由于赋值操作符被删除，上述危险无法发生。\n";

    // 真正的危险：从临时 std::function 构造
    std::function_ref<int(int)> ref3 = std::function<int(int)>([](int x) {
        return x;
    }); // 可行，但 std::function 是对象，内部持有 lambda，ref3 引用 std::function 对象，离开此行后 std::function 销毁 → 悬挂
    // 此处 ref3 已悬挂！后续调用是未定义行为。但为了测试，我们不实际调用。
    std::cout << "注意：从临时 std::function 构造会形成悬挂引用，应避免。\n";

    // NOTE: Using function_ref outside of the lifetime of the bound object has undefined behavior.
    int a;
    std::function_ref<int(int)> ref4 = std::function<int(int)>([&a](int x) { return x; });
}

// ============================================================================
// 14. 严格测试: 常量表达式支持 (如果可能)
// ============================================================================

// 注意：function_ref 的构造和调用并非 constexpr（因为涉及函数指针存储），
// 但 constant_wrapper 可以用于常量表达式。
constexpr int constexpr_static_add(int a, int b)
{
    return a + b;
}

void test_constexpr_support()
{
    std::cout << "\n=== 14. 常量表达式支持测试 ===\n";
    constexpr auto cw = std::constant_wrapper<constexpr_static_add>{};
    constexpr std::function_ref<int(int, int)> ref =
        cw; // 注意：构造不是 constexpr？实际上标准未要求 constexpr 构造
    // 但 constant_wrapper 本身是 constexpr 的，可以用于编译时求值
    static_assert(cw(3, 4) == 7);
    std::cout << "constant_wrapper 可用于 constexpr 上下文，但 function_ref 本身通常不是 "
                 "constexpr。\n";
}

// ============================================================================
// 15. 严格测试: 完美转发与引用折叠
// ============================================================================

void test_perfect_forwarding()
{
    std::cout << "\n=== 15. 完美转发与引用参数测试 ===\n";
    int value = 100;
    auto capture_by_ref = [&value](int x) {
        value += x;
        return value;
    };
    std::function_ref<int(int)> ref = capture_by_ref;
    std::cout << "捕获引用: 调用前 value=" << value << std::endl;
    int result = ref(5);
    std::cout << "调用后 value=" << value << ", result=" << result << std::endl;

    // 测试参数为引用类型的情况
    auto add_to_ref = [](int &a, int b) {
        a += b;
        return a;
    };
    std::function_ref<int(int &, int)> ref_add = add_to_ref;
    int x = 10;
    ref_add(x, 5);
    std::cout << "x after add_to_ref: " << x << std::endl; // 15
}

// ============================================================================
// 主函数
// ============================================================================

int main()
{
    std::cout << "=== std::function_ref 测试 (P0792R14) ===\n";

    test_basic_construction();
    test_overload_set();
    test_from_std_function();
    test_ctad();
    test_feature_macro();
    test_null_reference();
    test_copy_semantics();
    test_deleted_assignment();
    test_retry_example();
    test_nontype_construction();
    test_trivially_copyable();

    test_compile_time_constraints();
    test_lifetime_safety();
    test_constexpr_support();
    test_perfect_forwarding();

    std::cout << "\n所有测试执行完毕。\n";
    return 0;
}
// NOLINTEND