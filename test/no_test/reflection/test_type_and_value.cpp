#include <meta>
#include <string_view>
using namespace std::meta;

// ========== 声明区 ==========
int globalVar;              // 静态存储期，但非常量
constexpr int kConst = 100; // 编译期常量
int arr[3];                 // 数组变量，用于测试 type_of

enum E
{
    e0
};
enum class EC
{
    x
};

struct S
{
    static double staticMem; // 静态成员
    int nonStaticMem;        // 非静态成员（无静态存储期）
};

// ========== 测试宏：检测表达式是否编译期合法 ==========
// 以下辅助函数用于在 consteval 中捕获异常（编译错误），但无法直接用于 static_assert 条件。
// 我们改用模板 + requires 或单独编译测试。

// 1. type_of：对所有“非类型反射”均可用，类型反射自身不可用
consteval void test_type_of()
{
    [[maybe_unused]] auto t1 = type_of(^^globalVar);    // OK，返回 ^^int
    [[maybe_unused]] auto t2 = type_of(^^kConst);       // OK，返回 ^^const int
    [[maybe_unused]] auto t3 = type_of(^^arr);          // OK，返回 ^^int[3]
    [[maybe_unused]] auto t4 = type_of(^^e0);           // OK，返回 ^^E
    [[maybe_unused]] auto t5 = type_of(^^S::staticMem); // OK，返回 ^^double
    // [[maybe_unused]] auto t6 = type_of(^^int);          // 编译错误：类型反射无类型
}

// 2. object_of：仅可用于静态存储期对象或函数（但函数对象测试失败，此处忽略函数）
consteval void test_object_of()
{
    [[maybe_unused]] auto o1 = object_of(^^globalVar); // OK，全局变量有静态存储期
    [[maybe_unused]] auto o2 = object_of(^^kConst);    // OK，constexpr 变量也是静态存储期
    [[maybe_unused]] auto o3 = object_of(^^S::staticMem); // OK，静态成员
    // [[maybe_unused]] auto o4 =
    //     object_of(^^S::nonStaticMem); // 编译错误：非静态成员无静态存储期
    // [[maybe_unused]] auto o5 = object_of(^^e0); // 编译错误：枚举器不是对象
    // [[maybe_unused]] auto o6 = object_of(
    //     reflect_constant(1)); // 编译错误：值反射不是对象（当前 GCC 可能抛出异常）
}

// 3. constant_of：仅可用于编译期常量（包括枚举器、constexpr 变量、非类型模板参数等）
consteval void test_constant_of()
{
    [[maybe_unused]] auto c1 = constant_of(^^kConst); // OK，kConst 是编译期常量
    [[maybe_unused]] auto c2 = constant_of(^^e0);     // OK，枚举器是常量
    [[maybe_unused]] auto c3 = constant_of(^^EC::x);  // OK，限定枚举器
    // [[maybe_unused]] auto o4 = constant_of(^^globalVar); // 编译错误：globalVar 不是常量
    // [[maybe_unused]] auto o5 =
    //     constant_of(reflect_object(globalVar)); // 编译错误：对象反射不是常量
    [[maybe_unused]] auto c6 = constant_of(reflect_constant(42)); // OK，值反射是常量
}

// 静态断言验证（可直接使用，因为在全局作用域中，常量求值上下文）
static_assert(type_of(^^globalVar) == ^^int);
static_assert(type_of(^^kConst) == ^^const int);
static_assert(type_of(^^e0) == ^^E);

static_assert(!has_identifier(object_of(^^globalVar)));  // 对象反射无名
static_assert(type_of(object_of(^^globalVar)) == ^^int); // 对象反射的类型为 int

static_assert(extract<int>(constant_of(^^kConst)) == 100);
static_assert(extract<E>(constant_of(^^e0)) == e0);

// 验证非法调用（以下若取消注释将导致编译错误，这正是预期行为）
// constexpr info bad_obj = object_of(^^e0); // 错误
// constexpr info bad_const = constant_of(^^globalVar); // 错误

int main()
{
    // 触发 consteval 函数实例化，内部已通过编译检查
    test_type_of();
    test_object_of();
    test_constant_of();
    return 0;
}