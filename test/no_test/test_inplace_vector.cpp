// test_inplace_vector.cpp
// 测试 std::inplace_vector (C++26, P0843R14)
// 编译: g++ -std=c++26 -Wall -Wextra -O2 test_inplace_vector.cpp -o test_inplace_vector

#include <inplace_vector>
#include <iostream>
#include <cassert>
#include <algorithm>
#include <stdexcept>
#include <vector> // 添加 vector 头文件
#include <initializer_list>

// NOLINTBEGIN
// 验证特性宏
static_assert(__cpp_lib_inplace_vector >= 202406L,
              "std::inplace_vector requires C++26 support");

// 辅助函数：避免 assert 被逗号干扰，使用括号包裹整个表达式
#define MY_ASSERT(expr) assert(bool(expr))

// ============================================================================
// 1. 基础构造测试
// ============================================================================
void basic_construction_test()
{
    std::cout << "\n=== 1. 基础构造 ===\n";

    // 默认构造：空容器，capacity = 5
    std::inplace_vector<int, 5> v1;
    std::cout << "v1: size = " << v1.size() << ", capacity = " << v1.capacity() << '\n';
    assert(v1.empty() && v1.capacity() == 5);

    // 从 initializer_list 构造
    std::inplace_vector<int, 10> v2 = {1, 2, 3, 4, 5};
    std::cout << "v2: " << v2.size() << " elements\n";
    assert(v2[0] == 1 && v2[4] == 5);

    // 拷贝构造
    std::inplace_vector<int, 10> v3 = v2;
    MY_ASSERT(v3 == v2);

    // 移动构造
    std::inplace_vector<int, 10> v4 = std::move(v3);
    assert(v4.size() == 5);
    assert(not v3.empty() && v3.size() == 5); //NOTE: 平凡类型

    // 范围构造
    std::inplace_vector<int, 10> v5(v2.begin(), v2.end());
    MY_ASSERT(v5 == v2);
}

// ============================================================================
// 2. 容量与元素访问
// ============================================================================
void capacity_access_test()
{
    std::cout << "\n=== 2. 容量与访问 ===\n";

    std::inplace_vector<int, 8> v = {10, 20, 30};

    // reserve 不会改变 capacity (固定)
    v.reserve(6);
    assert(v.capacity() == 8 && v.size() == 3);
    v.shrink_to_fit(); // 无效果
    assert(v.capacity() == 8);

    // 元素访问
    assert(v.front() == 10 && v.back() == 30);
    assert(v.data()[0] == 10 && *v.data() == 10);
    assert(v[1] == 20 && v.at(2) == 30);

    // 边界检查 (忽略返回值以避免 warning)
    try
    {
        (void)v.at(10);
    }
    catch (const std::out_of_range &e)
    {
        std::cout << "at() 越界异常: " << e.what() << '\n';
    }
}

// ============================================================================
// 3. 插入操作
// ============================================================================
void insertion_test()
{
    std::cout << "\n=== 3. 插入操作 ===\n";

    // push_back / try_push_back
    std::inplace_vector<int, 4> v;
    v.push_back(1);
    v.push_back(2);
    assert(v.size() == 2 && v.back() == 2);

    v.try_push_back(3);
    assert(v.try_push_back(4));
    assert(!v.try_push_back(5)); // 容量满，插入失败
    assert(v.size() == 4);

    // emplace_back / try_emplace_back
    struct Point
    {
        int x, y;
    };
    std::inplace_vector<Point, 3> points;
    points.emplace_back(1, 2);
    points.try_emplace_back(3, 4);
    assert(points.size() == 2 && points[1].x == 3);
    points.try_emplace_back(5, 6);
    assert(!points.try_emplace_back(7, 8));

    // insert 在中间
    std::inplace_vector<int, 10> vec = {1, 2, 4, 5};
    auto it = vec.insert(vec.begin() + 2, 3);
    MY_ASSERT((vec == std::inplace_vector<int, 10>{1, 2, 3, 4, 5}));

    // emplace 在中间
    vec.emplace(vec.begin() + 1, 99);
    assert(vec[1] == 99);

    // insert_range 需要传递一个范围，使用 initializer_list
    vec.clear();
    vec.insert_range(vec.begin(), std::initializer_list<int>{1, 2, 3});
    assert(vec.size() == 3);

    // append_range
    vec.append_range(std::initializer_list<int>{4, 5});
    assert(vec.size() == 5);
}

// ============================================================================
// 4. 删除与修改
// ============================================================================
void deletion_modification_test()
{
    std::cout << "\n=== 4. 删除与修改 ===\n";

    std::inplace_vector<int, 10> v = {1, 2, 3, 4, 5};

    v.pop_back();
    assert(v.size() == 4 && v.back() == 4);

    auto it = v.erase(v.begin() + 1);
    MY_ASSERT((v == std::inplace_vector<int, 10>{1, 3, 4}));

    auto rit = v.erase(v.begin(), v.begin() + 2);
    assert(v.size() == 1 && v[0] == 4);

    v.clear();
    assert(v.empty());

    v.assign({5, 6, 7});
    MY_ASSERT((v == std::inplace_vector<int, 10>{5, 6, 7}));

    // swap
    std::inplace_vector<int, 10> v2 = {10, 11};
    v.swap(v2);
    assert(v.size() == 2 && v2.size() == 3);
}

// ============================================================================
// 5. 比较运算符
// ============================================================================
void comparison_test()
{
    std::cout << "\n=== 5. 比较运算符 ===\n";

    std::inplace_vector<int, 5> a = {1, 2, 3};
    std::inplace_vector<int, 5> b = {1, 2, 3};
    std::inplace_vector<int, 5> c = {1, 2, 4};

    MY_ASSERT(a == b);
    MY_ASSERT(a != c);
    MY_ASSERT(a < c);
    MY_ASSERT(a <= b);
    MY_ASSERT(c > a);
    MY_ASSERT(c >= c);
}

// ============================================================================
// 6. 迭代器与算法
// ============================================================================
void iterator_algorithm_test()
{
    std::cout << "\n=== 6. 迭代器与算法 ===\n";

    std::inplace_vector<int, 20> v = {5, 1, 4, 2, 3};

    // 范围 for
    std::cout << "原始: ";
    for (auto x : v)
        std::cout << x << ' ';
    std::cout << '\n';

    std::sort(v.begin(), v.end());
    MY_ASSERT((v == std::inplace_vector<int, 20>{1, 2, 3, 4, 5}));

    auto it = std::find(v.begin(), v.end(), 3);
    assert(it != v.end() && *it == 3);

    std::transform(v.begin(), v.end(), v.begin(), [](int x) { return x * 2; });
    assert(v[0] == 2 && v[4] == 10);
}

// ============================================================================
// 7. 性能与布局
// ============================================================================
void performance_layout_test()
{
    std::cout << "\n=== 7. 性能与内存布局 ===\n";

    std::cout << "sizeof(inplace_vector<int, 8>) = "
              << sizeof(std::inplace_vector<int, 8>) << " bytes\n";
    std::cout << "sizeof(inplace_vector<int, 0>) = "
              << sizeof(std::inplace_vector<int, 0>) << " bytes\n";
    std::cout << "sizeof(std::vector<int>) = " << sizeof(std::vector<int>) << " bytes\n";

    std::inplace_vector<int, 8> v = {1, 2, 3, 4};
    std::vector<int> vec = {1, 2, 3, 4};

    std::cout << "inplace_vector 数据地址: " << v.data() << '\n';
    std::cout << "inplace_vector 自身地址: " << &v << '\n';
    std::cout << "vector 数据地址: " << vec.data() << '\n';
    std::cout << "vector 自身地址: " << &vec << '\n';
}

// ============================================================================
// 8. 边界与异常
// ============================================================================
void edge_cases_test()
{
    std::cout << "\n=== 8. 边界条件与异常 ===\n";

    // 零容量特化
    std::inplace_vector<int, 0> zero;
    assert(zero.empty() && zero.capacity() == 0);

    // 截断 initializer_list
    // std::inplace_vector<int, 3> trunc = {1, 2, 3, 4, 5}; //NOTE: 直接异常
    // assert(trunc.size() == 3);

    // try_xxx 满时返回 false
    std::inplace_vector<int, 2> full = {1, 2};
    assert(!full.try_push_back(3));

    // 超出容量抛出 bad_alloc
    try
    {
        std::inplace_vector<int, 2> over;
        over.push_back(1);
        over.push_back(2);
        over.push_back(3);
    }
    catch (const std::bad_alloc &e)
    {
        std::cout << "捕获 bad_alloc: " << e.what() << '\n';
    }
}

// ============================================================================
// 9. constexpr 支持 (仅对平凡类型)
// ============================================================================
consteval void constexpr_test()
{
    constexpr std::inplace_vector<int, 5> v = {1, 2, 3};
    static_assert(v.size() == 3);
    static_assert(v.capacity() == 5);
    constexpr auto copy = v;
    static_assert(copy == v);
}

// ============================================================================
// 主函数
// ============================================================================
int main()
{
    std::cout << "========== std::inplace_vector 测试 (P0843R14) ==========\n";
    basic_construction_test();
    capacity_access_test();
    insertion_test();
    deletion_modification_test();
    comparison_test();
    iterator_algorithm_test();
    performance_layout_test();
    edge_cases_test();
    constexpr_test();
    std::cout << "\n所有测试通过！\n";
    return 0;
} // NOLINTEND