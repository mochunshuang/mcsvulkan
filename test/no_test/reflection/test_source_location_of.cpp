#include <iostream>
#include <exception>

#include <meta>
#include <source_location>
#include <string_view>

using namespace std::meta;
using namespace std::literals;

// ===== 声明一些实体 =====
struct MyClass
{
};
int myVar;
void myFunc() {}
enum MyEnum
{
    me
};
template <typename T>
struct Temp
{
};

// ===== 测试1：具名声明实体应有有效位置 =====
static_assert(source_location_of(^^MyClass).line() == 12); //NOTE: 按照文档算
static_assert(source_location_of(^^MyClass).column() != 0);
static_assert(source_location_of(^^MyClass).file_name()[0] != '\0');

static_assert(source_location_of(^^myVar).line() != 0);
static_assert(source_location_of(^^myFunc).line() != 0);
static_assert(source_location_of(^^MyEnum).line() != 0);

// 命名空间
namespace ns
{
}
static_assert(source_location_of(^^ns).line() != 0);

// 成员
struct Member
{
    int x;
};
static_assert(source_location_of(^^Member::x).line() != 0);

// 函数参数
constexpr bool test_param_loc()
{
    auto lam = [](int p) constexpr {
        return source_location_of(^^p).line() != 0;
    };
    return lam(0);
}
static_assert(test_param_loc());

// ===== 测试2：无名/合成实体应返回默认 source_location =====
// 基本类型
static_assert(source_location_of(^^int).line() == 0);
static_assert(source_location_of(^^int).column() == 0);
static_assert(source_location_of(^^int).file_name()[0] == '\0');

static_assert(source_location_of(^^double).line() == 0);

// 组合类型（指针、引用、数组、函数类型）
int arr[5];
int *ptr = arr;
int &ref = arr[0];
static_assert(source_location_of(^^decltype(arr)).line() == 0);
static_assert(source_location_of(^^decltype(ptr)).line() == 0);
static_assert(source_location_of(^^decltype(ref)).line() == 0);
static_assert(source_location_of(^^decltype(myFunc)).line() == 0); // 函数类型

// 匿名结构体/联合体/枚举类型
struct
{
    int a;
} anonStruct;
union {
    double x;
} anonUnion;
enum
{
    ae = 0
} anonEnum;
static_assert(source_location_of(^^decltype(anonStruct)).line() != 0);
static_assert(source_location_of(^^decltype(anonUnion)).line() != 0);
static_assert(source_location_of(^^decltype(anonEnum)).line() != 0);

// Lambda 闭包类型
auto lam = []() {
};
static_assert(source_location_of(^^decltype(lam)).line() != 0);

// 模板实例化
static_assert(source_location_of(^^Temp<int>).line() != 0);

// 值反射 / 对象反射
static_assert(source_location_of(reflect_constant(42)).line() == 0);
static_assert(source_location_of(reflect_object(myVar)).line() != 0);

// ===== 测试3：同一实体多次获取的位置信息应一致 =====
static_assert(source_location_of(^^MyClass).line() ==
              source_location_of(^^MyClass).line());
// 文件名用 string_view 比较内容（避免指针地址不确定）
static_assert(std::string_view(source_location_of(^^MyClass).file_name()) ==
              std::string_view(source_location_of(^^MyClass).file_name()));

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