#include "soa_memory.hpp"
#include <cassert>
#include <iostream>
#include <string>

// 简单的测试辅助：断言并打印
#define TEST(name) std::cout << "  [TEST] " << name << " ... ";
#define PASS() std::cout << "PASSED\n";
#define CHECK(cond)                                                         \
    do                                                                      \
    {                                                                       \
        if (!(cond))                                                        \
        {                                                                   \
            std::cerr << "FAILED at " << __LINE__ << ": " << #cond << "\n"; \
            std::abort();                                                   \
        }                                                                   \
    } while (0)

// 用于测试的结构体
struct Vec3
{
    float x, y, z;
};

struct Person
{
    std::string name;
    int age;
};

int main()
{
    std::cout << "=== Testing soa_memory ===\n";

    // ---------- 基础分配 ----------
    {
        TEST("default construction with capacity 0");
        soa_memory<Vec3> mem0(0);
        CHECK(mem0.capacity() == 0);
        PASS();
    }
    {
        TEST("allocates memory for each field");
        constexpr size_t N = 10;
        soa_memory<Vec3> mem(N);
        CHECK(mem.capacity() == N);

        // 每个字段的指针应该非空
        auto x_field = mem.field<"x">();
        auto y_field = mem.field<"y">();
        auto z_field = mem.field<"z">();
        CHECK(x_field.data() != nullptr);
        CHECK(y_field.data() != nullptr);
        CHECK(z_field.data() != nullptr);

        // span 视图
        auto sx = mem.span<"x">();
        auto sy = mem.span<"y">();
        auto sz = mem.span<"z">();
        CHECK(sx.size() == N);
        CHECK(sy.size() == N);
        CHECK(sz.size() == N);
        CHECK(sx.data() == x_field.data());
        PASS();
    }

    // ---------- 元素访问 ----------
    {
        TEST("element access via get<> and get<\"name\">");
        soa_memory<Vec3> mem(3);
        // 先构造数据
        mem.construct_at(0, Vec3{1.0f, 2.0f, 3.0f});
        mem.construct_at(1, 4.0f, 5.0f, 6.0f); // 逐个字段
        mem.construct_field<1>(2, 7.0f);       // 单独构造 y
        mem.construct_field<0>(2, 8.0f);       // x
        mem.construct_field<2>(2, 9.0f);       // z

        // 检查
        CHECK(mem.get<0>(0) == 1.0f);
        CHECK(mem.get<1>(0) == 2.0f);
        CHECK(mem.get<2>(0) == 3.0f);
        CHECK(mem.get<"x">(1) == 4.0f);
        CHECK(mem.get<"y">(1) == 5.0f);
        CHECK(mem.get<"z">(1) == 6.0f);
        CHECK(mem.get<"x">(2) == 8.0f);
        CHECK(mem.get<"y">(2) == 7.0f);
        CHECK(mem.get<"z">(2) == 9.0f);

        // 通过 operator[] 获取 tuple
        auto tup = mem[1];
        CHECK(std::get<0>(tup) == 4.0f);
        CHECK(std::get<1>(tup) == 5.0f);
        CHECK(std::get<2>(tup) == 6.0f);
        PASS();
    }

    // ---------- view 和迭代 ----------
    {
        TEST("zip view and iteration");
        soa_memory<Vec3> mem(2);
        mem.construct_at(0, Vec3{10.f, 20.f, 30.f});
        mem.construct_at(1, Vec3{40.f, 50.f, 60.f});

        // 使用 view()
        auto v = mem.view();
        auto it = v.begin();
        auto [x0, y0, z0] = *it++;
        CHECK(x0 == 10.f && y0 == 20.f && z0 == 30.f);
        auto [x1, y1, z1] = *it;
        CHECK(x1 == 40.f && y1 == 50.f && z1 == 60.f);

        // 使用 begin/end
        int count = 0;
        for (auto [x, y, z] : mem)
        {
            ++count;
        }
        CHECK(count == 2);

        // 指定字段名 view
        auto v2 = mem.view<"x", "z">();
        auto it2 = v2.begin();
        auto [a, b] = *it2;
        CHECK(a == 10.f && b == 30.f);
        PASS();
    }

    // ---------- 销毁元素 ----------
    {
        TEST("destroy_at single element");
        soa_memory<Person> mem(2);
        // 构造
        mem.construct_at(0, std::string("Alice"), 30); // 逐个字段构造
        mem.construct_at(1, Person{"Bob", 25});        // 整体构造

        CHECK(mem.get<"name">(0) == "Alice");
        CHECK(mem.get<"age">(0) == 30);
        CHECK(mem.get<"name">(1) == "Bob");

        // 销毁 idx 0
        mem.destroy_at(0);
        // 之后不应再访问 idx 0，但 idx 1 依然有效
        CHECK(mem.get<"name">(1) == "Bob");
        // 允许再次在 idx 0 构造
        mem.construct_at(0, std::string("Charlie"), 40);
        CHECK(mem.get<"name">(0) == "Charlie");

        // 析构时会调用 destroy() 清理剩余元素
        PASS();
    }

    // ---------- 移动语义 ----------
    {
        TEST("move constructor");
        soa_memory<Vec3> mem(5);
        mem.construct_at(0, Vec3{1, 1, 1});
        mem.construct_at(4, Vec3{5, 5, 5});

        auto *old_x_ptr = mem.field<"x">().data();

        soa_memory<Vec3> mem2(std::move(mem));
        // 原对象 capacity 被置零
        CHECK(mem.capacity() == 0);
        // 新对象拥有相同的 capacity 和指针
        CHECK(mem2.capacity() == 5);
        CHECK(mem2.field<"x">().data() == old_x_ptr);
        // 数据仍然有效
        CHECK(mem2.get<"x">(0) == 1.0f);
        CHECK(mem2.get<"z">(4) == 5.0f);
        PASS();
    }
    {
        TEST("move assignment");
        soa_memory<Vec3> mem1(3);
        mem1.construct_at(0, Vec3{1, 2, 3});
        soa_memory<Vec3> mem2(2);
        mem2.construct_at(0, Vec3{4, 5, 6});

        auto *ptr1 = mem1.field<"y">().data();
        mem2 = std::move(mem1);
        // mem1 被重置
        CHECK(mem1.capacity() == 0);
        // mem2 拥有原 mem1 的资源
        CHECK(mem2.capacity() == 3);
        CHECK(mem2.field<"y">().data() == ptr1);
        CHECK(mem2.get<"y">(0) == 2.0f);
        PASS();
    }

    // ---------- 析构后内存释放（间接验证） ----------
    {
        TEST("destructor releases memory (no leak check, but runs)");
        // 简单创建大量元素然后析构，依赖 sanitizer 或手动观察
        soa_memory<Vec3> mem(1000);
        for (size_t i = 0; i < 1000; ++i)
        {
            mem.construct_at(i, Vec3{float(i), float(i + 1), float(i + 2)});
        }
        // 离开作用域自动销毁
        PASS();
    }

    std::cout << "=== All tests passed ===\n";
    return 0;
}