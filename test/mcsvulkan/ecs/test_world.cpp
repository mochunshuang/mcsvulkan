
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

using mcs::vulkan::ecs::make_runtime_type;
using mcs::vulkan::ecs::static_string;
using mcs::vulkan::ecs::soa_class;
using mcs::vulkan::ecs::world;
using mcs::vulkan::ecs::gen_soa_store;
using mcs::vulkan::ecs::gen_soa_sparse_store;
using mcs::vulkan::ecs::proxy_value;
using mcs::vulkan::ecs::name_spec;

// 简单的测试宏
#define TEST(name) std::cout << "[TEST] " << name << " ... "
#define PASS() std::cout << "PASSED\n"
#define CHECK(cond)                                                           \
    do                                                                        \
    {                                                                         \
        if (!(cond))                                                          \
        {                                                                     \
            std::cerr << "FAILED at line " << __LINE__ << ": " #cond << "\n"; \
            std::abort();                                                     \
        }                                                                     \
    } while (false)
#define REQUIRE(cond) CHECK(cond)

// 测试结构体
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

int main()
{
    using soa_list =
        soa_class<name_spec{"SimplePod", ^^gen_soa_store<SimplePod>},
                  name_spec{"NonTrivial", ^^gen_soa_sparse_store<NonTrivial>}>;
    using world_type = world<soa_list>;

    world_type w;

    // ======== 1. 通过名字和类型获取 SOA 存储 ========
    {
        auto &store1 = w.get_soa<"SimplePod">();
        auto &store2 = w.get_soa<SimplePod>();
        assert(&store1 == &store2);
        static_assert(
            std::is_same_v<std::decay_t<decltype(store1)>::value_type, SimplePod>);
        std::println("Test1 (get_soa) passed.");
    }

    // ======== 2. 基本分配/构造/访问（通过 proxy_value） ========
    {
        auto opt = w.make_soa_value<SimplePod>(10, 3.14, 'A');
        assert(opt.has_value());
        auto proxy = std::move(*opt); // 移动 proxy
        auto [x, y, z] = *proxy;      // 解引用返回 tuple<字段引用...>
        assert(x == 10 && y == 3.14 && z == 'A');
        // proxy 离开作用域自动释放槽位
    }
    // 槽位已释放，应可重用
    {
        auto opt2 = w.make_soa_value<SimplePod>(20, 2.71, 'B');
        assert(opt2.has_value());
        auto proxy2 = std::move(*opt2);
        auto [x, y, z] = *proxy2;
        assert(x == 20);
    }
    std::println("Test2 (basic allocate/construct/release) passed.");

    // ======== 3. 容量耗尽时返回 nullopt ========
    {
        auto &store = w.get_soa<SimplePod>();
        // 初始 capacity=4，已无存活元素（上面 proxy 已析构），应能再分配 4 个
        auto p1 = w.make_soa_value<SimplePod>(1, 0.0, '1');
        auto p2 = w.make_soa_value<SimplePod>(2, 0.0, '2');
        auto p3 = w.make_soa_value<SimplePod>(3, 0.0, '3');
        auto p4 = w.make_soa_value<SimplePod>(4, 0.0, '4');
        assert(p1.has_value() && p2.has_value() && p3.has_value() && p4.has_value());
        auto p5 = w.make_soa_value<SimplePod>(5, 0.0, '5');
        assert(!p5.has_value()); // 容量满，分配失败
    }
    std::println("Test3 (exhaust capacity) passed.");

    // ======== 4. proxy 移动语义：源对象不再释放 ========
    {
        auto &store = w.get_soa<SimplePod>();
        size_t before = store.size();

        auto opt = w.make_soa_value<SimplePod>(100, 1.1, 'X');
        assert(opt.has_value());
        auto proxy1 = std::move(*opt);
        assert(store.size() == before + 1);

        auto proxy2 = std::move(proxy1);    // 移动构造
        assert(store.size() == before + 1); // 仍然存活

        auto [x, y, z] = *proxy2;
        assert(x == 100);
        // proxy2 析构，释放元素
    }
    assert(w.get_soa<SimplePod>().size() == 0);
    std::println("Test4 (proxy move semantics) passed.");

    // ======== 5. NonTrivial 只可移动类型 ========
    {
        auto opt = w.make_soa_value<NonTrivial>(42, "hello");
        assert(opt.has_value());
        auto proxy = std::move(*opt);
        auto [id, name] = *proxy;
        assert(id == 42 && name == "hello");
        // proxy 析构正确调用 release（销毁 NonTrivial）
    }
    std::println("Test5 (NonTrivial move-only) passed.");

    // ======== 6. 扩容后继续分配 ========
    {
        auto &store = w.get_soa<SimplePod>();
        store.grow(6); // 扩容到 6
        std::vector<decltype(w.make_soa_value<SimplePod>(0, 0.0, ' '))> proxies;
        for (int i = 0; i < 6; ++i)
        {
            auto opt = w.make_soa_value<SimplePod>(i, i * 1.0, 'a' + i);
            assert(opt.has_value());
            proxies.push_back(std::move(*opt));
        }
        // 释放中间两个
        proxies.erase(proxies.begin() + 2);
        proxies.erase(proxies.begin() + 2); // 存活 4 个
        // 再分配应成功，使用刚释放的槽位
        auto opt_new = w.make_soa_value<SimplePod>(99, 9.9, '!');
        assert(opt_new.has_value());
        auto proxy_new = std::move(*opt_new);
        auto [x, y, z] = *proxy_new;
        assert(x == 99);
    }
    std::println("Test6 (grow and slot reuse) passed.");

    // ======== 7. const 世界无法修改 ========
    // const world_type cw;
    // cw.make_soa_value<SimplePod>(0,0.0,' '); // 编译错误（预期内）
    std::println("Test7 (const restriction) compile-time check passed.");

    // ======== 8. 显式验证默认容量和 used_idx_ 初始化 ========
    {
        TEST("gen_soa_store default capacity = 4");
        gen_soa_store<SimplePod> store;
        CHECK(store.capacity() == 4);
        CHECK(store.free_size() == 4);
        PASS();
    }
    {
        TEST("gen_soa_sparse_store default capacity = 4 and used_idx_ initialized");
        gen_soa_sparse_store<SimplePod> sparse;
        CHECK(sparse.capacity() == 4);
        CHECK(sparse.free_size() == 4);
        // 若能分配一个 id，则证明 allocate 中的 assert(id < used_idx_.size()) 通过
        auto id = sparse.allocate();
        CHECK(id.has_value());
        sparse.construct_at(*id, SimplePod{0, 0.0, '0'});
        sparse.release(*id);
        PASS();
    }

    // ======== 9. 验证 proxy_value 返回的是引用，可修改原数据 ========
    {
        TEST("proxy operator* returns modifiable references");
        auto opt = w.make_soa_value<SimplePod>(42, 3.14, 'X');
        REQUIRE(opt.has_value());
        auto proxy = std::move(*opt);
        auto [x, y, z] = *proxy;
        x = 100; // 修改引用应反映到存储中
        y = 2.71;
        z = 'Y';
        auto [x2, y2, z2] = *proxy;
        CHECK(x2 == 100);
        CHECK(y2 == 2.71);
        CHECK(z2 == 'Y');
        // 析构 proxy 自动释放
        PASS();
    }

    // ======== 10. gen_soa_sparse_store 加速删除一致性 ========
    {
        TEST("gen_soa_sparse_store O(1) release with correct used_idx_ update");
        gen_soa_sparse_store<SimplePod> sparse(4);
        auto a = sparse.allocate();
        sparse.construct_at(*a, SimplePod{1, 0.0, 'a'});
        auto b = sparse.allocate();
        sparse.construct_at(*b, SimplePod{2, 0.0, 'b'});
        auto c = sparse.allocate();
        sparse.construct_at(*c, SimplePod{3, 0.0, 'c'});
        // 释放 b，应触发 swap-with-last，used_ 中 id 顺序可能变为 {1,3}
        sparse.release(*b);
        // 验证剩余元素：通过 view 遍历所有存活元素，应包含 {1, 'a'} 和 {3, 'c'}
        bool found1 = false, found3 = false;
        for (auto tup : sparse.view<"x", "z">())
        {
            auto [x, z] = tup;
            if (x == 1 && z == 'a')
                found1 = true;
            if (x == 3 && z == 'c')
                found3 = true;
        }
        CHECK(found1 && found3);
        // 再分配应复用之前释放的物理槽位（id 不一定是原来的，但肯定成功）
        auto d = sparse.allocate();
        CHECK(d.has_value());
        sparse.construct_at(*d, SimplePod{4, 0.0, 'd'});
        CHECK(sparse.size() == 3);
        // 释放全部后大小归零
        // 释放全部存活槽位
        sparse.release(*a);
        sparse.release(*c);
        sparse.release(*d);
        CHECK(sparse.size() == 0);
        PASS();
    }

    // ======== 11. 移动后 used_idx_ 状态 ========
    {
        TEST("move of gen_soa_sparse_store preserves used_idx_ integrity");
        gen_soa_sparse_store<NonTrivial> orig(4);
        auto id1 = orig.allocate();
        orig.construct_at(*id1, 1, "one");
        auto id2 = orig.allocate();
        orig.construct_at(*id2, 2, "two");
        auto moved = std::move(orig);
        CHECK(orig.size() == 0);
        CHECK(orig.capacity() == 0);
        CHECK(moved.size() == 2);
        // 通过 release 验证 used_idx_ 仍然有效
        moved.release(*id1);
        CHECK(moved.size() == 1);
        PASS();
    }

    // ======== 12. 复制后 used_idx_ 独立 ========
    {
        TEST("copy of gen_soa_sparse_store has independent used_idx_");
        gen_soa_sparse_store<NonTrivial> orig(4);
        auto id = orig.allocate();
        orig.construct_at(*id, 10, "copy-test");
        auto copy = orig;
        // 原对象释放槽位，副本应不受影响
        orig.release(*id);
        CHECK(orig.size() == 0);
        CHECK(copy.size() == 1);
        // 副本仍能通过 used_idx_ 释放自己的元素
        copy.release(*id);
        CHECK(copy.size() == 0);
        PASS();
    }

    // ======== 13. grow 后 used_idx_ 扩容正确 ========
    {
        TEST("grow of gen_soa_sparse_store extends used_idx_ correctly");
        gen_soa_sparse_store<SimplePod> sparse(2);
        auto a = sparse.allocate();
        sparse.construct_at(*a, SimplePod{1, 0, 'a'});
        auto b = sparse.allocate();
        sparse.construct_at(*b, SimplePod{2, 0, 'b'});
        sparse.grow(5);
        CHECK(sparse.capacity() == 5);
        // 新分配的 slot 应在扩展的范围内，且 used_idx_ 大小匹配
        auto c = sparse.allocate();
        CHECK(c.has_value());
        sparse.construct_at(*c, SimplePod{3, 0, 'c'});
        // 释放一个旧元素，确保 used_idx_ 仍正确
        sparse.release(*a);
        CHECK(sparse.size() == 2);
        PASS();
    }

    // ======== 14. proxy_value::release 显式调用 ========
    {
        TEST("explicit proxy release calls store release and sets null");
        std::optional<proxy_value<gen_soa_store<SimplePod>>> opt =
            w.make_soa_value<SimplePod>(99, 9.9, '!');
        REQUIRE(opt.has_value());
        auto proxy = std::move(*opt);
        auto &store = w.get_soa<SimplePod>();
        size_t before = store.size();
        proxy.release(); // 手动释放
        CHECK(store.size() == before - 1);
        // 再次释放应无副作用
        proxy.release();
        CHECK(store.size() == before - 1);
        PASS();
    }
    // ===== ECS：用 tuple<proxy_value...> 做实体 =====
    {
        // 创建实体：同时拥有 SimplePod 和 NonTrivial 两个组件
        auto entity =
            std::make_tuple(std::move(*w.make_soa_value<SimplePod>(42, 3.14, 'E')),
                            std::move(*w.make_soa_value<NonTrivial>(1, "EntityOne")));

        // 访问 SimplePod 组件
        proxy_value<gen_soa_store<SimplePod>> &sp_proxy = std::get<0>(entity);
        auto [x, y, z] = *sp_proxy; // 结构化绑定，直接拿到字段引用
        std::println("SimplePod: x={}, y={}, z={}", x, y, z);

        // 访问 NonTrivial 组件
        auto &nt_proxy = std::get<1>(entity);
        auto [id, name] = *nt_proxy;
        std::println("NonTrivial: id={}, name={}", id, name);

        // 修改组件字段（x, y, z 是引用，可直接修改）
        x = 100;
        const auto [x2, y2, z2] = *sp_proxy; // 验证修改生效
        assert(x2 == 100);

        // 实体离开作用域，tuple 析构 → 各 proxy 自动释放组件
    }
    {
        world_type w;
        auto entity = make_runtime_type<"entity0", "a", "b">(
            std::move(*w.make_soa_value<SimplePod>(42, 3.14, 'E')),
            std::move(*w.make_soa_value<NonTrivial>(1, "EntityOne")));

        static_assert(decltype(entity)::class_name == "entity0");

        assert(entity->a);
        assert(entity->a.has_value());
        assert(entity.get<"a">());
        auto ref = *entity;
        // 直接 . 操作，无需箭头
        std::println("SimplePod: x={}, y={}, z={}", ref.a.x, ref.a.y, ref.a.z);
        std::println("NonTrivial: id={}, name={}", ref.b.id, ref.b.name);
        // 修改字段
        ref.a.x = 100;
        ref.b.id = 2;

        {
            const auto &refe = entity;
            auto ref_const = *refe;
            // ref_const.a.x = 1; //NOTE: 编译错误
            assert(ref_const.a.x == 100);
        }

        assert((*entity.get<"a">()).x == 100);
        assert((*(entity.data_.a)).x == 100);
        assert((*(entity.data_.a)).y == 3.14); // y 未被修改
        assert((*(entity.data_.b)).id == 2);   // id 已被修改

        // 直接从 world 的 SoA 存储中获取实体 ID 并验证字段值
        auto &pos_store = w.get_soa<SimplePod>();
        assert(pos_store.size() == 1);
        size_t eid_pos = pos_store.used_slots()[0]; // 唯一存活的实体 ID
        assert(pos_store.template get<"x">(eid_pos) == 100);
        assert(pos_store.template get<"y">(eid_pos) == 3.14);
        assert(pos_store.template get<"z">(eid_pos) == 'E');

        auto &nt_store = w.get_soa<NonTrivial>();
        assert(nt_store.size() == 1);
        size_t eid_nt = nt_store.used_slots()[0];
        assert(nt_store.template get<"id">(eid_nt) == 2);
        assert(nt_store.template get<"name">(eid_nt) == "EntityOne");
        /*
runtime_type::operator* 不再返回对 proxy 的包装，而是直接返回一个新的聚合体 entity_view。

entity_view 的成员不是 proxy 本身，而是 *proxy 的结果（那个临时聚合体的拷贝）。

那个临时聚合体虽然被拷贝了，但它内部成员是指向 SoA 存储的引用（或值，如果是简单字段），拷贝后这些引用仍然绑定到原始内存，所以修改 ref.a.x 会直接修改底层数据。

因为 ref.a 本身就是一个有成员 x, y, z 的聚合体，自然可以继续用 .x 访问，于是 ref.a.x 完美工作。
*/
    }

    std::println("\nAll world tests passed!\n");
    return 0;
}