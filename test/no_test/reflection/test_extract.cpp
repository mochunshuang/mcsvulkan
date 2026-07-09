#include <meta>
#include <iostream>
#include <type_traits>
using namespace std::meta;

// ============================================================
// 辅助实体（全部为 constexpr / consteval）
// ============================================================
constexpr int global_int = 100;
constexpr double global_double = 3.14;

struct MyClass
{
    int member_var = 42;
    static constexpr int static_var = 77;

    consteval int get_value() const
    {
        return member_var;
    }
    static constexpr int static_func()
    {
        return 1;
    }
};

consteval int free_func(int x)
{
    return x * 2;
}
using FuncPtr = int (*)(int);

constexpr int arr[] = {1, 2, 3};

// 辅助：按名字查找成员反射
consteval info find_member(info class_ref, const char *name)
{
    for (info m : nonstatic_data_members_of(class_ref, access_context::unchecked()))
        if (has_identifier(m) && identifier_of(m) == name)
            return m;
    for (info m : members_of(class_ref, access_context::unchecked()))
        if (has_identifier(m) && identifier_of(m) == name)
            return m;
    return info{};
}

// ============================================================
// 测试 1：extract-ref（提取变量/对象引用）
// ============================================================
consteval bool test_extract_ref()
{
    const int &ref = extract<const int &>(^^global_int);
    if (ref != 100)
        return false;

    const int &sref = extract<const int &>(^^MyClass::static_var);
    if (sref != 77)
        return false;

    const double &dref = extract<const double &>(^^global_double);
    if (dref != 3.14)
        return false;

    const auto &arr_ref = extract<const int (&)[3]>(^^arr);
    if (arr_ref[0] != 1 || arr_ref[2] != 3)
        return false;

    return true;
}
static_assert(test_extract_ref());

// ============================================================
// 测试 2：extract-member-or-function（成员指针 / 函数指针）
// ============================================================
consteval bool test_extract_member_or_function()
{
    constexpr MyClass obj; // 编译期对象，用于测试成员指针

    // 非静态数据成员 -> 成员指针
    info mem_var = find_member(^^MyClass, "member_var");
    if (mem_var == info{})
        return false;
    int MyClass::*pm = extract<int MyClass::*>(mem_var);
    if (obj.*pm != 42)
        return false;

    // 成员函数 -> 成员函数指针（get_value 是 consteval，可在编译期调用）
    info mem_func = find_member(^^MyClass, "get_value");
    if (mem_func == info{})
        return false;
    int (MyClass::*pmf)() const = extract<int (MyClass::*)() const>(mem_func);
    if ((obj.*pmf)() != 42)
        return false;

    // 静态成员函数 -> 函数指针
    info static_func_ref = find_member(^^MyClass, "static_func");
    if (static_func_ref == info{})
        return false;
    int (*sf_ptr)() = extract<int (*)()>(static_func_ref);
    if (sf_ptr() != 1)
        return false;

    // 自由函数（consteval） -> 函数指针
    FuncPtr fp = extract<FuncPtr>(^^free_func);
    if (fp(5) != 10)
        return false;

    return true;
}
static_assert(test_extract_member_or_function());

// ============================================================
// 测试 3：extract-value（值提取与类型转换）
// ============================================================
consteval bool test_extract_value()
{
    // 3.1 提取静态成员变量的地址（反射一个指针值）
    // 3. 静态成员对象 -> 值
    int v2 = extract<int>(^^MyClass::static_var);
    if (v2 != 77)
        return false;

    // 3.2 提取普通值
    int val = extract<int>(^^global_int);
    if (val != 100)
        return false;

    // 3.3 数组到指针的转换
    constexpr int arr2[] = {10, 20, 30};
    const int *parr = extract<const int *>(^^arr2);
    if (parr[0] != 10 || parr[2] != 30)
        return false;

    // 3.4 闭包类型 -> 函数指针（若编译器支持可启用，目前暂时跳过）
    auto lam = [](int x) {
        return x + 1;
    };
    using LamFn = int (*)(int);
    LamFn lam_ptr = extract<LamFn>(^^lam);
    if (lam_ptr(5) != 6)
        return false;

    return true;
}
static_assert(test_extract_value());

// ============================================================
// 测试 4：extract 分发逻辑
// ============================================================
consteval bool test_extract_dispatch()
{
    // 变量 + 引用类型 -> extract-ref
    const int &vref = extract<const int &>(^^global_int);
    if (vref != 100)
        return false;

    // 变量 + 值类型 -> extract-value
    int vval = extract<int>(^^global_int);
    if (vval != 100)
        return false;

    // 非静态数据成员 -> 成员指针（仅检查编译通过，值已在别处验证）
    info mem_var = find_member(^^MyClass, "member_var");
    if (mem_var == info{})
        return false;
    [[maybe_unused]] int MyClass::*pm = extract<int MyClass::*>(mem_var);

    // 自由函数 -> 函数指针
    FuncPtr fp = extract<FuncPtr>(^^free_func);
    if (fp(3) != 6)
        return false;

    return true;
}
static_assert(test_extract_dispatch());

// ============================================================
// 测试 5：常量表达式上下文验证
// ============================================================
consteval bool test_extract_constexpr()
{
    constexpr int extracted = extract<int>(^^global_int);
    static_assert(extracted == 100);

    constexpr info mem_var = find_member(^^MyClass, "member_var");
    if (mem_var == info{})
        return false;
    constexpr int MyClass::*pm = extract<int MyClass::*>(mem_var);
    // pm 永远非空，仅验证类型
    (void)pm;
    return true;
}
static_assert(test_extract_constexpr());

// ============================================================
// 测试 6：从 T... 生成 info 集合，并通过 extract 取回
// ============================================================
template <typename... Ts>
consteval bool test_extract_array_of_infos()
{
    constexpr std::size_t N = sizeof...(Ts);
    using Aarry = std::array<std::meta::info, N>;
    // using Aarry = std::span<const std::meta::info>;
    // 1. 用类型包生成 array<meta::info, N>
    Aarry original = {^^Ts...};

    // 2. 打包成单一 info
    std::meta::info packed = std::meta::reflect_constant(original);

    // 3. 提取回 array<meta::info, N>
    auto extracted = extract<Aarry>(packed);

    // 4. 利用 array 自身的 operator== 一次完成全量比较
    return original == extracted;
}

static_assert(test_extract_array_of_infos<int, double, char>());
static_assert(test_extract_array_of_infos<>()); // 空包也 OK

// ============================================================
struct Test
{
    static constexpr int a = 10; // ✅ 常量
    static int b;                // 声明，定义在别处
};
int Test::b = 20; // 运行时初始化

consteval bool test()
{
    [[maybe_unused]] int x = extract<int>(^^Test::a); // ✅ 通过
    //  error: the value of 'Test::b' is not usable in a constant expression
    // int y = extract<int>(^^Test::b); // ❌ 编译错误：b 不是常量
    return true;
}
static_assert(test());
int main()
{
    std::cout << "All extract tests passed.\n";
    return 0;
}