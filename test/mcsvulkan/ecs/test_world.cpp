#include "head.hpp"
#include <array>
#include <cassert>
#include <iostream>
#include <optional>
#include <print>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using mcs::vulkan::ecs::static_string;
using mcs::vulkan::ecs::soa_class;
using mcs::vulkan::ecs::world;
using mcs::vulkan::ecs::gen_soa_vector;
using mcs::vulkan::ecs::proxy_value;
using mcs::vulkan::ecs::name_spec;

#define TEST(name) std::cout << "[TEST] " << name << " ... "
#define PASS() std::cout << "PASSED\n"
#define CHECK(cond)                                                              \
    do                                                                           \
    {                                                                            \
        if (!(cond))                                                             \
        {                                                                        \
            std::cerr << "FAILED at line " << __LINE__ << ": " << #cond << "\n"; \
            std::abort();                                                        \
        }                                                                        \
    } while (false)
#define REQUIRE(cond) CHECK(cond)

struct SimplePod
{
    int x;
    double y;
    char z;
};

struct NonTrivial
{
    int id;
    std::string name;
    NonTrivial(int i = 0, std::string n = "") : id(i), name(std::move(n)) {}
    NonTrivial(const NonTrivial &) = delete;
    NonTrivial &operator=(const NonTrivial &) = delete;
    NonTrivial(NonTrivial &&) noexcept = default;
    NonTrivial &operator=(NonTrivial &&) noexcept = default;
};

struct DestructionCounter
{
    static inline int destroy_count = 0;
    bool active = true; // 只有 active 才在析构时 +1

    DestructionCounter() = default;
    ~DestructionCounter()
    {
        if (active)
            ++destroy_count;
    }

    // 禁止拷贝
    DestructionCounter(const DestructionCounter &) = delete;
    DestructionCounter &operator=(const DestructionCounter &) = delete;

    // 移动：源对象失效，不再计数
    DestructionCounter(DestructionCounter &&other) noexcept
        : active(std::exchange(other.active, false))
    {
    }

    DestructionCounter &operator=(DestructionCounter &&other) noexcept
    {
        if (this != &other)
        {
            active = std::exchange(other.active, false);
        }
        return *this;
    }
};
struct Tracker
{
    DestructionCounter dc; // 析构时自动 +1
    int val;
    std::string s;

    Tracker(int v, std::string str) : val(v), s(std::move(str)) {}
    Tracker(const Tracker &) = delete;
    Tracker &operator=(const Tracker &) = delete;
    Tracker(Tracker &&) noexcept = default;
    Tracker &operator=(Tracker &&) noexcept = default;
};

struct WorldTracker
{
    DestructionCounter dc;
    int val;
};

void test_gen_soa_vector()
{
    // 1. 基本分配与构造
    {
        gen_soa_vector<SimplePod> vec(4);
        auto e0 = vec.allocate();
        REQUIRE(e0.has_value());
        auto e1 = vec.allocate();
        REQUIRE(e1.has_value());
        vec.construct_at(*e0, 10, 3.14, 'A');
        vec.construct_at(*e1, 20, 2.71, 'B');
        CHECK(vec.template get<"x">(*e0) == 10);
        CHECK(vec.template get<"y">(*e1) == 2.71);
        std::cout << "Test 1 passed\n";
    }

    // 2. 删除与令牌稳定性
    {
        gen_soa_vector<SimplePod> vec(4);
        auto e0 = vec.allocate();
        REQUIRE(e0);
        auto e1 = vec.allocate();
        REQUIRE(e1);
        vec.construct_at(*e0, 100, 1.1, 'X');
        vec.construct_at(*e1, 200, 2.2, 'Y');
        vec.release(*e0);
        CHECK(vec.size() == 1);
        CHECK(vec.template get<"x">(*e1) == 200);

        auto e2 = vec.allocate();
        REQUIRE(e2.has_value());
        vec.construct_at(*e2, 300, 3.3, 'Z');
        CHECK(vec.template get<"x">(*e1) == 200);
        CHECK(vec.template get<"x">(*e2) == 300);
        std::cout << "Test 2 passed\n";
    }

    // 3. 遍历 (view)
    {
        gen_soa_vector<SimplePod> vec(4);
        for (int i = 0; i < 3; ++i)
        {
            auto e = vec.allocate();
            REQUIRE(e);
            vec.construct_at(*e, i + 1, (i + 1) * 1.0, char('a' + i));
        }
        int count = 0;
        for (auto [x, z] : vec.view<"x", "z">())
        {
            std::cout << "x=" << x << ", z=" << z << '\n';
            ++count;
        }
        CHECK(count == 3);
        std::cout << "Test 3 passed\n";
    }

    // 4. 扩容
    {
        gen_soa_vector<SimplePod> vec(2);
        auto e0 = vec.allocate();
        REQUIRE(e0);
        vec.construct_at(*e0, 0, 0.0, '0');
        auto e1 = vec.allocate();
        REQUIRE(e1);
        vec.construct_at(*e1, 1, 0.0, '1');
        vec.reserve(5);
        auto e2 = vec.allocate();
        REQUIRE(e2);
        vec.construct_at(*e2, 2, 0.0, '2');
        CHECK(vec.size() == 3);
        CHECK(vec.template get<"x">(*e0) == 0);
        CHECK(vec.template get<"x">(*e1) == 1);
        std::cout << "Test 4 passed\n";
    }

    // 5. 移动与复制
    {
        gen_soa_vector<NonTrivial> a(4);
        auto id = a.allocate();
        REQUIRE(id);
        a.construct_at(*id, 42, "hello");
        auto b = std::move(a);
        CHECK(a.size() == 0);
        CHECK(b.size() == 1);
        CHECK(b.template get<"id">(*id) == 42);
        CHECK(b.template get<"name">(*id) == "hello");
        std::cout << "Test 5 passed\n";
    }

    // 6. 迭代器（安全遍历，无空洞）
    {
        gen_soa_vector<SimplePod> vec(4);
        auto e0 = vec.allocate();
        REQUIRE(e0);
        vec.construct_at(*e0, 10, 1.0, 'A');
        auto e1 = vec.allocate();
        REQUIRE(e1);
        vec.construct_at(*e1, 20, 2.0, 'B');
        int count = 0;
        for (auto item : vec)
        {
            item.x += 1;
            ++count;
        }
        CHECK(count == 2);
        CHECK(vec.template get<"x">(*e0) == 11);
        std::cout << "Test 6 passed\n";
    }

    // 7. 复制构造与赋值
    {
        gen_soa_vector<SimplePod> a(4);
        auto e0 = a.allocate();
        REQUIRE(e0);
        a.construct_at(*e0, 100, 1.0, 'X');
        auto e1 = a.allocate();
        REQUIRE(e1);
        a.construct_at(*e1, 200, 2.0, 'Y');

        // 复制构造
        auto b = a;
        CHECK(b.size() == 2);
        CHECK(b.template get<"x">(*e0) == 100);
        CHECK(b.template get<"x">(*e1) == 200);

        // 原容器修改不影响副本
        a.release(*e0);
        CHECK(a.size() == 1);
        CHECK(b.size() == 2);                   // 副本仍完整
        CHECK(b.template get<"x">(*e0) == 100); // 副本中的实体仍可访问

        // 赋值运算
        gen_soa_vector<SimplePod> c;
        c = b; // 拷贝赋值
        CHECK(c.size() == 2);
        CHECK(c.template get<"x">(*e0) == 100);
        b.release(*e1);
        CHECK(c.size() == 2); // 赋值后的对象独立

        std::cout << "Test 7 (copy) passed\n";
    }

    // 8. 构造/销毁/重新构造 (destroy_at 安全用法)
    {
        gen_soa_vector<SimplePod> vec(4);
        auto e = vec.allocate();
        REQUIRE(e);
        vec.construct_at(*e, 42, 3.14, '?');
        CHECK(vec.template get<"x">(*e) == 42);

        vec.destroy_at(*e); // 只析构，实体仍然存在于 dense_ 中
        // 此时不能通过 get/operator[] 访问，因为对象已析构（UB），
        // 正确做法是紧接着 construct_at 或 release。
        vec.construct_at(*e, 99, 9.9, '!'); // 重新构造
        CHECK(vec.template get<"x">(*e) == 99);
        CHECK(vec.size() == 1); // 实体数未变

        vec.release(*e); // 正常释放
        CHECK(vec.size() == 0);
        std::cout << "Test 8 (destroy/reconstruct) passed\n";
    }

    // 9. 全字段视图 (无模板参数的 view())
    {
        gen_soa_vector<SimplePod> vec(4);
        for (int i = 0; i < 3; ++i)
        {
            auto e = vec.allocate();
            REQUIRE(e);
            vec.construct_at(*e, i + 1, (i + 1) * 1.0, char('a' + i));
        }
        int count = 0;
        for (auto item : vec.view())
        {
            // item 是 bind_result 临时对象，字段可读写
            item.x += 1;
            ++count;
        }
        CHECK(count == 3);
        // 验证修改已写回 SoA
        CHECK(vec.template get<"x">(0) == 2); // 第一个实体 x=1+1
        CHECK(vec.template get<"x">(1) == 3); // 第二个实体 x=2+1
        CHECK(vec.template get<"x">(2) == 4); // 第三个实体 x=3+1
        std::cout << "Test 9 (full view) passed\n";
    }

    // 10. 删除后的迭代器安全遍历（无空洞）
    {
        gen_soa_vector<SimplePod> vec(4);
        auto e0 = vec.allocate();
        REQUIRE(e0);
        vec.construct_at(*e0, 10, 1.0, '0');
        auto e1 = vec.allocate();
        REQUIRE(e1);
        vec.construct_at(*e1, 20, 2.0, '1');
        auto e2 = vec.allocate();
        REQUIRE(e2);
        vec.construct_at(*e2, 30, 3.0, '2');
        auto e3 = vec.allocate();
        REQUIRE(e3);
        vec.construct_at(*e3, 40, 4.0, '3');

        vec.release(*e1); // 删除中间实体，物理槽位应被最后一个实体填充
        // 此时存活实体：e0, e3, e2（顺序取决于 swap-pop）
        // 但无论如何，size() == 3，迭代器只访问前 3 个物理槽位，都是有效对象。
        int count = 0;
        for (auto item : vec)
        {
            // 确保每个 item 的字段都可以正常读写
            item.x += 1;
            ++count;
        }
        CHECK(count == 3);
        // 验证所有实体都仍然存在且数据正确（仅检查 x 字段，因为已 +1）
        // 由于我们不知道内部顺序，可以用 used_slots() 和 get 来验证
        bool found_e0 = false, found_e2 = false, found_e3 = false;
        for (size_t id : vec.used_slots())
        {
            int val = vec.template get<"x">(id);
            if (id == *e0 && val == 11)
                found_e0 = true;
            if (id == *e2 && val == 31)
                found_e2 = true;
            if (id == *e3 && val == 41)
                found_e3 = true;
        }
        CHECK(found_e0 && found_e2 && found_e3);
        std::cout << "Test 10 (safe iteration after deletion) passed\n";
    }

    // 11. free_size / capacity 查询
    {
        gen_soa_vector<SimplePod> vec(4);
        CHECK(vec.capacity() == 4);
        CHECK(vec.free_size() == 4);
        auto e0 = vec.allocate();
        CHECK(vec.free_size() == 3);
        auto e1 = vec.allocate();
        CHECK(vec.free_size() == 2);
        vec.release(*e0);
        CHECK(vec.free_size() == 3);
        vec.release(*e1);
        CHECK(vec.free_size() == 4);
        std::cout << "Test 11 (capacity/free) passed\n";
    }

    // 12. construct_at 移动重载
    {
        gen_soa_vector<SimplePod> vec(4);
        auto e = vec.allocate();
        REQUIRE(e);
        SimplePod obj{77, 7.7, 'M'};
        vec.construct_at(*e, std::move(obj)); // 移动构造
        CHECK(vec.template get<"x">(*e) == 77);
        CHECK(vec.template get<"y">(*e) == 7.7);
        CHECK(vec.template get<"z">(*e) == 'M');
        std::cout << "Test 12 (move construct_at) passed\n";
    }

    // 13. 全字段视图在删除/重用后的安全性（覆盖之前的漏洞）
    {
        gen_soa_vector<SimplePod> vec(4);
        auto e0 = vec.allocate();
        vec.construct_at(*e0, 1, 0.0, 'a');
        auto e1 = vec.allocate();
        vec.construct_at(*e1, 2, 0.0, 'b');
        auto e2 = vec.allocate();
        vec.construct_at(*e2, 3, 0.0, 'c');

        vec.release(*e1);         // dense 变为 {0, 2}（实体 ID）
        auto e3 = vec.allocate(); // 可能重用 1 或分配 3
        vec.construct_at(*e3, 4, 0.0, 'd');

        int count = 0;
        for (auto item : vec.view())
        { // 遍历物理槽位，应无越界
            item.x += 1;
            ++count;
        }
        CHECK(count == 3);
        // 通过 used_slots() 验证数据，而非依赖实体ID顺序
        bool ok = true;
        for (auto id : vec.used_slots())
        {
            if (id == *e0 && vec.template get<"x">(id) != 2)
                ok = false;
            if (id == *e2 && vec.template get<"x">(id) != 4)
                ok = false;
            if (id == *e3 && vec.template get<"x">(id) != 5)
                ok = false;
        }
        CHECK(ok);
        std::cout << "Test 13 (safe view after reuse) passed\n";
    }

    // 14. release 后移动操作的正确性（针对非平凡类型）
    {
        gen_soa_vector<NonTrivial> vec(4);
        auto e0 = vec.allocate();
        vec.construct_at(*e0, 1, "first");
        auto e1 = vec.allocate();
        vec.construct_at(*e1, 2, "second");
        auto e2 = vec.allocate();
        vec.construct_at(*e2, 3, "third");

        vec.release(*e1); // 释放中间元素，应安全
        CHECK(vec.size() == 2);

        // 验证剩余两个实体的字符串正确
        bool has_first = false, has_third = false;
        for (auto id : vec.used_slots())
        {
            auto &name = vec.template get<"name">(id);
            if (id == *e0 && name == "first")
                has_first = true;
            if (id == *e2 && name == "third")
                has_third = true;
        }
        CHECK(has_first && has_third);
        std::cout << "Test 14 (non-trivial release correctness) passed\n";
    }

    // NOTE: gen_soa_vector RAII 保证测试
    // 15. 验证容器析构时自动销毁所有未释放的实体（通过析构计数器）
    {
        DestructionCounter::destroy_count = 0;
        {
            gen_soa_vector<Tracker> vec(4);
            auto e0 = vec.allocate();
            auto e1 = vec.allocate();

            // Tracker 有三个成员：dc, val, s
            vec.construct_at(*e0, Tracker{1, "one"});
            vec.construct_at(*e1, Tracker{2, "two"});
            CHECK(DestructionCounter::destroy_count == 0); // 构造没有副作用
        } // vec 析构 → destroy_at(0) 和 destroy_at(1) → 每个 dc 析构 → destroy_count 变成 2
        CHECK(DestructionCounter::destroy_count == 2); // 确实析构了 2 次
    }
    // 16. 验证 world 销毁时内部所有存储都被析构，自动清理实体
    {
        DestructionCounter::destroy_count = 0;
        using soa_list2 = soa_class<name_spec{"Tracker", ^^gen_soa_vector<WorldTracker>}>;
        using world_type2 = world<soa_list2>;

        {
            world_type2 w;
            auto &store = w.get_soa<"Tracker">();
            store.reserve(2);
            auto e0 = store.allocate();
            store.construct_at(*e0, DestructionCounter{}, 100); // dc, val
            auto e1 = store.allocate();
            store.construct_at(*e1, DestructionCounter{}, 200);
            CHECK(DestructionCounter::destroy_count == 0);
        } // w 析构 → store 析构 → destroy_at 两个实体 → destroy_count == 2
        CHECK(DestructionCounter::destroy_count == 2);
    }
}

int main()
{
    test_gen_soa_vector();

    // ---- world 集成测试 (全部基于 gen_soa_vector) ----
    using soa_list = soa_class<name_spec{"SimplePod", ^^gen_soa_vector<SimplePod>},
                               name_spec{"NonTrivial", ^^gen_soa_vector<NonTrivial>}>;
    using world_type = world<soa_list>;

    // proxy_value 大小不变
    static_assert(sizeof(proxy_value<gen_soa_vector<SimplePod>>) == 16);
    static_assert(sizeof(std::optional<proxy_value<gen_soa_vector<SimplePod>>>) == 24);

    world_type w;

    // 1. get_soa 按名称和类型
    {
        auto &store1 = w.get_soa<"SimplePod">();
        auto &store2 = w.get_soa<SimplePod>();
        store1.reserve(4);
        store2.reserve(4);
        CHECK(&store1 == &store2);
        static_assert(
            std::is_same_v<std::decay_t<decltype(store1)>::value_type, SimplePod>);
        std::println("Test1 (get_soa) passed.");
    }

    // 2. 基本分配/构造/访问 (proxy_value)
    {
        auto opt = w.make_soa_value<SimplePod>(10, 3.14, 'A');
        CHECK(opt.has_value());
        auto proxy = std::move(*opt);
        auto [x, y, z] = *proxy;
        CHECK(x == 10 && y == 3.14 && z == 'A');
    }
    {
        auto opt2 = w.make_soa_value<SimplePod>(20, 2.71, 'B');
        CHECK(opt2.has_value());
        auto proxy2 = std::move(*opt2);
        auto [x, y, z] = *proxy2;
        CHECK(x == 20);
    }
    std::println("Test2 (basic proxy) passed.");

    // 3. 容量耗尽
    {
        auto &store = w.get_soa<SimplePod>();
        std::vector<proxy_value<gen_soa_vector<SimplePod>>> proxies;
        for (int i = 0; i < 4; ++i)
        {
            auto opt = w.make_soa_value<SimplePod>(i, i * 1.0, char('a' + i));
            REQUIRE(opt.has_value());
            proxies.push_back(std::move(*opt));
        }
        CHECK(store.size() == 4);
        auto full = w.make_soa_value<SimplePod>(99, 0.0, 'X');
        CHECK(!full.has_value());
    }
    std::println("Test3 (capacity) passed.");

    // 4. proxy 移动语义
    {
        auto &store = w.get_soa<SimplePod>();
        auto opt = w.make_soa_value<SimplePod>(100, 1.1, 'X');
        REQUIRE(opt.has_value());
        auto p1 = std::move(*opt);
        CHECK(store.size() == 1);
        auto p2 = std::move(p1);
        CHECK(store.size() == 1);
        auto [x, y, z] = *p2;
        CHECK(x == 100);
    }
    std::println("Test4 (proxy move) passed.");

    // 5. NonTrivial 移动
    {
        w.get_soa<NonTrivial>().reserve(4);
        auto opt = w.make_soa_value<NonTrivial>(42, "hello");
        REQUIRE(opt.has_value());
        auto proxy = std::move(*opt);
        auto [id, name] = *proxy;
        CHECK(id == 42 && name == "hello");
    }
    std::println("Test5 (NonTrivial) passed.");

    // 6. 扩容后继续分配
    {
        auto &store = w.get_soa<SimplePod>();
        store.reserve(6);
        std::vector<proxy_value<gen_soa_vector<SimplePod>>> proxies;
        for (int i = 0; i < 6; ++i)
        {
            auto opt = w.make_soa_value<SimplePod>(i, i * 1.0, char('a' + i));
            REQUIRE(opt.has_value());
            proxies.push_back(std::move(*opt));
        }
        proxies.erase(proxies.begin() + 2);
        proxies.erase(proxies.begin() + 2); // 存活 4
        auto opt_new = w.make_soa_value<SimplePod>(99, 9.9, '!');
        REQUIRE(opt_new.has_value());
        auto [x, y, z] = *std::move(*opt_new);
        CHECK(x == 99);
    }
    std::println("Test6 (grow) passed.");

    // 7. proxy 可修改数据
    {
        auto opt = w.make_soa_value<SimplePod>(42, 3.14, 'X');
        REQUIRE(opt.has_value());
        auto proxy = std::move(*opt);
        auto [x, y, z] = *proxy;
        x = 100;
        y = 2.71;
        z = 'Y';
        auto [x2, y2, z2] = *proxy;
        CHECK(x2 == 100 && y2 == 2.71 && z2 == 'Y');
    }
    std::println("Test7 (modify) passed.");

    // 8. 显式 proxy::release
    {
        auto opt = w.make_soa_value<SimplePod>(99, 9.9, '!');
        REQUIRE(opt.has_value());
        auto proxy = std::move(*opt);
        auto &store = w.get_soa<SimplePod>();
        size_t before = store.size();
        proxy.release();
        CHECK(store.size() == before - 1);
        proxy.release(); // 无害
        CHECK(store.size() == before - 1);
    }
    std::println("Test8 (explicit release) passed.");

    // 9. tuple<proxy_value...> 实体
    {
        auto entity =
            std::make_tuple(std::move(*w.make_soa_value<SimplePod>(42, 3.14, 'E')),
                            std::move(*w.make_soa_value<NonTrivial>(1, "EntityOne")));
        auto &sp = std::get<0>(entity);
        auto [x, y, z] = *sp;
        CHECK(x == 42);
        x = 100;
        auto [x2, y2, z2] = *sp;
        CHECK(x2 == 100);
    }
    std::println("Test9 (tuple entity) passed.");

    std::println("\nAll tests passed!");
    return 0;
}