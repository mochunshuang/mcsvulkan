#include "head.hpp"

#include <assert.h>
#include <cassert>
#include <cstddef>
#include <functional>
#include <iostream>
#include <exception>
#include <type_traits>
#include <utility>
#include <vector>

using mcs::vulkan::meta::make_aggregate;
using bind_member = mcs::vulkan::meta::bind_member;
using mcs::vulkan::meta::static_string;
using mcs::vulkan::meta::static_nsdms_of;
using mcs::vulkan::meta::static_parameters_of;

static constexpr auto static_fun(int x[[= bind_member{"x"}]], int y)
{
    return x + y;
}
static_assert(std::meta::is_function(^^static_fun));
static_assert(std::meta::is_function_type(std::meta::type_of(^^static_fun)));
static_assert(std::meta::annotations_of_with_type(^^static_fun, ^^bind_member).size() ==
              0);
//

//NOTE: BUG 也不算 BUG。 函数变成指针，丢失注解信息
constexpr auto parameters = static_parameters_of(^^static_fun);
static_assert(std::meta::annotations_of_with_type(parameters[0], ^^bind_member).size() >
              0);
constexpr auto v = static_fun; //NOTE: 变成指针，丢失注解信息
static_assert(std::is_same_v<std::decay_t<decltype(v)>, int (*)(int, int)>);
constexpr auto parameters2 =
    static_parameters_of(^^std::remove_pointer_t<std::decay_t<decltype(v)>>);
static_assert(std::meta::annotations_of_with_type(parameters2[0], ^^bind_member).size() ==
              0);

// static_assert(std::meta::is_function_type(std::meta::type_of(^^decltype([]() {}))));

// 测试 make_aggregate_ref: 生成成员全是引用的 aggregate
void test_make_aggregate_ref()
{
    std::cout << "\n=== test_make_aggregate_ref ===\n";

    int a = 42;
    const int b = 100;
    double c = 3.14;

    // 使用 make_aggregate_ref 创建引用聚合
    auto ref_agg =
        mcs::vulkan::meta::make_aggregate_ref<"Refs", "ref_a", "ref_b", "ref_c">(a, b, c);
    static_assert(decltype(ref_agg)::class_name == "Refs");

    // 验证成员类型为引用且值正确
    static_assert(std::is_same_v<decltype(ref_agg.ref_a), int &>);
    static_assert(std::is_same_v<decltype(ref_agg.ref_b), const int &>);
    static_assert(std::is_same_v<decltype(ref_agg.ref_c), double &>);

    assert(ref_agg.ref_a == 42);
    assert(ref_agg.ref_b == 100);
    assert(ref_agg.ref_c == 3.14);

    // 修改非 const 引用成员，原始变量应同步改变
    ref_agg.ref_a = 99;
    assert(a == 99);
    // 不可修改 const 引用成员（编译期错误，取消注释会失败）
    // ref_agg.ref_b = 200; // 错误：常量引用不可修改

    // 测试 with 方法：从聚合中绑定引用成员到函数参数
    // 定义一个 lambda，使用 bind_member 注解来声明需要的成员
    auto adder = []([[= bind_member{"ref_a"}]] int &ra,
                    [[= bind_member{"ref_c"}]] double &rc, int extra) -> double {
        ra += extra;
        rc += extra;
        return ra + rc;
    };

    // 调用 with，它会根据注解自动提取对应的引用成员并传递给 adder
    auto result = ref_agg.with(adder, 5);
    assert(ref_agg.ref_a == 104);  // 原 a = 99 + 5 = 104
    assert(ref_agg.ref_c == 8.14); // 原 c = 3.14 + 5 = 8.14
    assert(result == 112.14);      // 104 + 8.14

    // 测试 invoke_static: 直接调用成员函数（成员是 lambda，需要匹配签名）
    // 注意：成员 "ref_c" 是 double&，但 invoke_static 要求成员是可调用的，这里仅演示语法
    // 实际场景中，成员可以是函数对象，然后 invoke_static 调用它。
    // 由于本示例成员是基本类型，跳过 invoke 测试。

    std::cout << "make_aggregate_ref test passed.\n";
    {
        int value = 42;
        const int const_value = 100;

        // 1. 值语义版本
        auto agg_value =
            mcs::vulkan::meta::make_aggregate<"ValueAgg", "v1", "v2">(value, const_value);
        static_assert(
            std::is_same_v<decltype(agg_value.v1), int>); // 注意：是 int，不是 int&
        static_assert(
            std::is_same_v<decltype(agg_value.v2), int>); // const 被剥离，因为拷贝

        // 2. 引用语义版本
        auto agg_ref = mcs::vulkan::meta::make_aggregate_ref<"RefAgg", "r1", "r2">(
            value, const_value);
        static_assert(std::is_same_v<decltype(agg_ref.r1), int &>);       // 左值引用
        static_assert(std::is_same_v<decltype(agg_ref.r2), const int &>); // const 保留
    }
}

int main()
try
{

    auto b = make_aggregate<"class", "x", "fn">(1, [](auto &&self, auto value) noexcept {
        return std::forward_like<decltype(self)>(self.x) + value;
    });
    assert(b.x == 1);
    static_assert(decltype(b)::class_name == "class");

    assert(b.template invoke<"fn">(2) == 3);
    assert(b.template with([]([[= bind_member{"x"}]] int a, int y) { return a + y; },
                           2) == 3);
    // assert(b.template with(static_fun, 2) == 3);

    //NOTE: 编译失败：
    // decltype(b) c{1, [](auto &&self, auto value) noexcept {
    //                   return std::forward_like<decltype(self)>(self.x) + value;
    //               }};
    {
        //NOTE: 提取存储类型就行
        auto member_fn = [](auto &&self, auto value) noexcept {
            return std::forward_like<decltype(self)>(self.x) + value;
        };
        auto b = make_aggregate<"class", "x", "fn">(1, member_fn);

        decltype(b) c{1, member_fn};
    }

    test_make_aggregate_ref();

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