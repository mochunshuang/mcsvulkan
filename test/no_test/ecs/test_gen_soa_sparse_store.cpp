#include "gen_soa_sparse_store.hpp"
#include "gen_soa_store.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <algorithm>

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

// 测试用结构体
struct Vec3
{
    float x, y, z;
    bool operator==(const Vec3 &) const = default; // C++20
};

struct Person
{
    std::string name;
    int age;
    // 仅用于测试显示，不要求 operator==
};

struct Tracker
{
    inline static int ctor = 0;
    inline static int copy_ctor = 0;
    inline static int move_ctor = 0;
    inline static int copy_assign = 0;
    inline static int move_assign = 0;
    inline static int dtor = 0;

    int val;
    Tracker(int v = 0) : val(v)
    {
        ++ctor;
    }
    Tracker(const Tracker &o) : val(o.val)
    {
        ++copy_ctor;
    }
    Tracker(Tracker &&o) noexcept : val(o.val)
    {
        ++move_ctor;
        o.val = 0;
    }
    Tracker &operator=(const Tracker &o)
    {
        val = o.val;
        ++copy_assign;
        return *this;
    }
    Tracker &operator=(Tracker &&o) noexcept
    {
        val = o.val;
        ++move_assign;
        return *this;
    }
    ~Tracker()
    {
        ++dtor;
    }

    static void reset()
    {
        ctor = copy_ctor = move_ctor = copy_assign = move_assign = dtor = 0;
    }
};

template <template <typename T, template <typename> class Alloc = std::allocator,
                    template <typename, template <typename> class> class Make =
                        soa_memory> class SOA_STORE>
void test()
{
    // ---------- 基础构造与分配 ----------
    {
        TEST("construction and free_size");
        SOA_STORE<Vec3> store(4);
        CHECK(store.capacity() == 4);
        CHECK(store.size() == 0);
        CHECK(store.free_size() == 4); // 初始空闲槽位数
        PASS();
    }
    {
        TEST("allocate and construct (value_type)");
        SOA_STORE<Vec3> store(4);
        auto id0 = store.allocate();
        CHECK(id0.has_value());
        store.construct_at(*id0, Vec3{1.0f, 2.0f, 3.0f});
        CHECK(store.size() == 1);
        CHECK(store.free_size() == 3);

        auto id1 = store.allocate();
        store.construct_at(*id1, 4.0f, 5.0f, 6.0f); // 逐字段构造
        CHECK(store.size() == 2);

        // 通过 operator[] 访问逻辑元素
        auto t0 = store[0];
        CHECK(std::get<0>(t0) == 1.0f);
        CHECK(std::get<1>(t0) == 2.0f);
        CHECK(std::get<2>(t0) == 3.0f);

        auto t1 = store[1];
        CHECK(std::get<0>(t1) == 4.0f);
        CHECK(std::get<1>(t1) == 5.0f);
        CHECK(std::get<2>(t1) == 6.0f);
        PASS();
    }
    {
        TEST("allocate failure when no free slots");
        SOA_STORE<Vec3> store(1);
        auto id = store.allocate();
        CHECK(id.has_value());
        store.construct_at(*id, Vec3{0, 0, 0});
        auto id2 = store.allocate();
        CHECK(!id2.has_value()); // 无空闲槽位
        PASS();
    }

    // ---------- 释放与重用 ----------
    {
        TEST("release and reuse slot");
        SOA_STORE<Vec3> store(3);
        auto a = store.allocate();
        store.construct_at(*a, Vec3{1, 1, 1});
        auto b = store.allocate();
        store.construct_at(*b, Vec3{2, 2, 2});
        auto c = store.allocate();
        store.construct_at(*c, Vec3{3, 3, 3});
        CHECK(store.size() == 3);

        store.release(*b);
        CHECK(store.size() == 2);
        CHECK(store.free_size() == 1);

        // 再次分配，应复用刚刚释放的物理槽位
        auto d = store.allocate();
        CHECK(d.has_value());
        store.construct_at(*d, Vec3{4, 4, 4});
        CHECK(store.size() == 3);

        // 收集所有元素验证内容（逻辑顺序不一定与物理槽位一致）
        std::vector<Vec3> values;
        for (auto tup : store.view())
        {
            auto [x, y, z] = tup;
            values.push_back({x, y, z});
        }
        CHECK(values.size() == 3);
        // 应包含 {1,1,1}, {3,3,3}, {4,4,4}
        auto has = [&](Vec3 v) {
            return std::find(values.begin(), values.end(), v) != values.end();
        };
        CHECK(has(Vec3{1, 1, 1}));
        CHECK(has(Vec3{3, 3, 3}));
        CHECK(has(Vec3{4, 4, 4}));
        CHECK(!has(Vec3{2, 2, 2})); // 已销毁
        PASS();
    }

    // ---------- 扩容 (grow) ----------
    {
        TEST("grow preserves existing elements and extends free list");
        SOA_STORE<Person> store(2);
        auto id0 = store.allocate();
        store.construct_at(*id0, std::string("Alice"), 30);
        auto id1 = store.allocate();
        store.construct_at(*id1, Person{"Bob", 25});
        CHECK(store.size() == 2);

        store.grow(5);
        CHECK(store.capacity() == 5);
        CHECK(store.size() == 2);
        CHECK(store.free_size() == 3); // 原来满的，扩容增加 3 个空闲槽位

        // 验证原有数据仍存在（遍历 view）
        bool alice = false, bob = false;
        for (auto tup : store.template view<"name", "age">())
        {
            auto [n, a] = tup;
            if (n == "Alice" && a == 30)
                alice = true;
            if (n == "Bob" && a == 25)
                bob = true;
        }
        CHECK(alice && bob);

        // 扩容后继续分配
        auto id2 = store.allocate();
        CHECK(id2.has_value());
        store.construct_at(*id2, std::string("Charlie"), 35);
        CHECK(store.size() == 3);
        PASS();
    }
    {
        TEST("grow with no-op (new_cap <= old_cap)");
        SOA_STORE<Vec3> store(4);
        auto id = store.allocate();
        store.construct_at(*id, Vec3{1, 1, 1});
        store.grow(4); // 不应改变任何东西
        CHECK(store.capacity() == 4);
        CHECK(store.size() == 1);
        CHECK(store.free_size() == 3);
        PASS();
    }

    // ---------- 视图 (view) ----------
    {
        TEST("view with specific field names");
        SOA_STORE<Vec3> store(5);
        auto id = store.allocate();
        store.construct_at(*id, Vec3{1, 2, 3});
        id = store.allocate();
        store.construct_at(*id, Vec3{4, 5, 6});
        id = store.allocate();
        store.construct_at(*id, Vec3{7, 8, 9});

        // 单字段视图
        {
            auto vx = store.template view<"x">();
            std::vector<float> xs;
            for (auto tup : vx)
            {
                auto [x] = tup; // zip 返回 tuple<引用>
                xs.push_back(x);
            }
            CHECK(xs.size() == 3);
            CHECK(xs[0] == 1.0f);
            CHECK(xs[1] == 4.0f);
            CHECK(xs[2] == 7.0f);
        }

        // 多字段视图
        {
            auto vxz = store.template view<"x", "z">();
            int i = 0;
            for (auto [x, z] : vxz)
            {
                CHECK(x == (float)(i * 3 + 1));
                CHECK(z == (float)(i * 3 + 3));
                ++i;
            }
        }

        // 无参 view：返回 tuple<全部字段>
        {
            int i = 0;
            for (auto tup : store.view())
            {
                auto [x, y, z] = tup;
                CHECK(x == (float)(i * 3 + 1));
                CHECK(y == (float)(i * 3 + 2));
                CHECK(z == (float)(i * 3 + 3));
                ++i;
            }
        }
        PASS();
    }

    // ---------- 迭代器 ----------
    {
        TEST("iterator and range-based for");
        SOA_STORE<Vec3> store(3);
        auto id0 = store.allocate();
        store.construct_at(*id0, Vec3{10, 20, 30});
        auto id1 = store.allocate();
        store.construct_at(*id1, Vec3{40, 50, 60});

        std::vector<Vec3> values;
        for (auto it = store.begin(); it != store.end(); ++it)
        {
            auto [x, y, z] = *it;
            values.push_back({x, y, z});
        }
        CHECK(values.size() == 2);
        CHECK(bool(values[0] == Vec3{10, 20, 30}));
        CHECK(bool(values[1] == Vec3{40, 50, 60}));

        // const 迭代
        const auto &cstore = store.read_only();
        std::vector<Vec3> cvalues;
        for (auto it = cstore.begin(); it != cstore.end(); ++it)
        {
            auto [x, y, z] = *it; // const tuple
            cvalues.push_back({x, y, z});
        }
        CHECK(cvalues == values);
        PASS();
    }

    // ---------- 析构与清理 ----------
    {
        TEST("destructor cleans up all alive objects (no leak check)");
        {
            SOA_STORE<Person> store(10);
            for (int i = 0; i < 10; ++i)
            {
                auto id = store.allocate();
                store.construct_at(*id, std::string("P") + std::to_string(i), i);
            }
            store.release(3); // 销毁 Person "P3"
            store.release(7); // 销毁 Person "P7"
            // 离开作用域，应正确析构剩余 8 个对象
        }
        PASS();
    }
    // --- 复制：修改原对象不影响副本 ---
    {
        TEST("copy independence");
        SOA_STORE<Person> orig(5);
        auto a = orig.allocate();
        orig.construct_at(*a, std::string("Alice"), 30);
        auto b = orig.allocate();
        orig.construct_at(*b, Person{"Bob", 25});
        orig.release(*b);

        SOA_STORE<Person> copy = orig;
        CHECK(copy.size() == 1);
        CHECK(copy.capacity() == 5);

        // 修改原对象：新增一个元素，副本不变
        orig.allocate();
        orig.construct_at(1, Person{"Charlie", 35});
        CHECK(orig.size() == 2);
        CHECK(copy.size() == 1);
        auto [name, age] = copy[0];
        CHECK(name == "Alice" && age == 30);

        std::cout << "  passed\n";
    }

    // --- 移动：资源转移，源对象变空 ---
    {
        TEST("move resource transfer");
        SOA_STORE<Vec3> orig(3);
        orig.allocate();
        orig.construct_at(0, Vec3{1, 2, 3});
        orig.allocate();
        orig.construct_at(1, Vec3{4, 5, 6});

        SOA_STORE<Vec3> moved = std::move(orig);
        CHECK(orig.size() == 0); // 源被清空
        CHECK(orig.capacity() == 0);
        CHECK(moved.size() == 2);
        CHECK(moved.capacity() == 3);
        auto [x, y, z] = moved[0];
        CHECK(x == 1.0f && y == 2.0f && z == 3.0f);

        std::cout << "  passed\n";
    }

    // --- 自赋值安全 ---
    {
        TEST("self assignment safety");
        SOA_STORE<Vec3> store(2);
        store.allocate();
        store.construct_at(0, Vec3{7, 7, 7});
        store = store; // 复制自赋值
        CHECK(store.size() == 1);
        store = std::move(store); // 移动自赋值
        CHECK(store.size() == 1);
        CHECK(std::get<0>(store[0]) == 7.0f);
        std::cout << "  passed\n";
    }
    // ---------- 分配失败后扩容 ----------
    {
        TEST("grow after allocation failure");
        SOA_STORE<Vec3> store(2);
        // 填满两个槽位
        auto id0 = store.allocate();
        store.construct_at(*id0, Vec3{1, 1, 1});
        auto id1 = store.allocate();
        store.construct_at(*id1, Vec3{2, 2, 2});
        CHECK(store.size() == 2);
        CHECK(store.free_size() == 0);

        // 尝试分配，应失败
        auto fail_id = store.allocate();
        CHECK(!fail_id.has_value());

        // 扩容到 5
        store.grow(5);
        CHECK(store.capacity() == 5);
        CHECK(store.free_size() == 3); // 新增 3 个空闲槽位
        CHECK(store.size() == 2);      // 原有元素仍存活

        // 现在分配应成功
        auto id2 = store.allocate();
        CHECK(id2.has_value());
        store.construct_at(*id2, Vec3{3, 3, 3});
        CHECK(store.size() == 3);

        // 验证数据完整
        bool has1 = false, has2 = false, has3 = false;
        for (auto tup : store.view())
        {
            auto [x, y, z] = tup;
            if (x == 1 && y == 1 && z == 1)
                has1 = true;
            if (x == 2 && y == 2 && z == 2)
                has2 = true;
            if (x == 3 && y == 3 && z == 3)
                has3 = true;
        }
        CHECK(has1 && has2 && has3);

        // 再次填满后继续扩容
        store.allocate();
        store.construct_at(3, Vec3{4, 4, 4});
        store.allocate();
        store.construct_at(4, Vec3{5, 5, 5});
        CHECK(store.allocate().has_value() == false);
        store.grow(7);
        CHECK(store.allocate().has_value() == true);
        PASS();
    }

    // ====================================================================
    // 精确验证特殊成员函数调用次数（基于源码逻辑）
    // ====================================================================
    {

        struct TrackedVec3
        {
            Tracker x, y, z;
        };

        // 注意：construct_at 接收 Tracker{} 纯右值，每次调用会触发移动构造（逐字段）
        // 因此每插入一个元素，move_ctor += 3, ctor += 3, dtor 最终也会 +=3（临时对象析构）

        {
            TEST("copy construction: each alive field copy-constructed once, no extra "
                 "move");
            Tracker::reset();
            SOA_STORE<TrackedVec3> orig(5);
            auto a = orig.allocate();
            orig.construct_at(*a, Tracker{1}, Tracker{2}, Tracker{3});
            auto b = orig.allocate();
            orig.construct_at(*b, Tracker{4}, Tracker{5}, Tracker{6});

            // 插入两个元素：每个字段 1 次移动构造，共 6 次
            int expected_move = 6;
            CHECK(Tracker::move_ctor == expected_move);
            int ctor_after_insert = Tracker::ctor;      // 6
            int copy_after_insert = Tracker::copy_ctor; // 0
            int dtor_after_insert = Tracker::dtor;      // 6 (临时 Tracker 析构)

            auto copy = orig;
            // 复制构造遍历 used_，对每个字段调用 construct_field<I>(slot, o.data_.get<I>(slot))
            // get 返回左值引用，construct_field 使用 placement new + const& → 调用拷贝构造
            // 应增加 6 次拷贝构造
            CHECK(Tracker::copy_ctor - copy_after_insert == 6);
            // 移动构造不增加
            CHECK(Tracker::move_ctor == expected_move);
            // 普通构造次数不变
            CHECK(Tracker::ctor == ctor_after_insert);
            // 析构次数不变（原对象和副本各自存活，没有额外销毁）
            CHECK(Tracker::dtor == dtor_after_insert);
            PASS();
        }

        {
            TEST("copy assignment: temporary copy adds 3 copy ctor, old elements "
                 "destroyed (3 dtor)");
            Tracker::reset();
            SOA_STORE<TrackedVec3> orig(3);
            orig.allocate();
            orig.construct_at(0, Tracker{1}, Tracker{2}, Tracker{3}); // 1 元素
            SOA_STORE<TrackedVec3> dest(5);
            dest.allocate();
            dest.construct_at(0, Tracker{9}, Tracker{9}, Tracker{9}); // 1 元素

            int move_initial = Tracker::move_ctor; // 2元素×3=6
            int ctor_initial = Tracker::ctor;      // 6
            int copy_initial = Tracker::copy_ctor; // 0
            int dtor_initial = Tracker::dtor;      // 6 (临时对象)

            dest = orig;

            // copy-and-swap:
            // 1) auto tmp = orig → 调用复制构造，增加 3 次拷贝构造（orig 的 1 个元素）
            // 2) *this = std::move(tmp) → 移动赋值：
            //    - 先销毁 dest 旧元素 (3 次析构)
            //    - 然后将 tmp 的成员移动到 dest，tmp 变为空状态
            // 因此 dtor 增量 = 3（仅 dest 旧元素），临时对象 tmp 析构时无元素可销毁
            CHECK(Tracker::copy_ctor - copy_initial == 3);
            CHECK(Tracker::dtor - dtor_initial == 3);
            CHECK(Tracker::move_ctor == move_initial);
            CHECK(Tracker::ctor == ctor_initial);
            PASS();
        }

        {
            TEST("move construction: no field copy/move ctor or dtor");
            Tracker::reset();
            SOA_STORE<TrackedVec3> orig(4);
            orig.allocate();
            orig.construct_at(0, Tracker{1}, Tracker{2}, Tracker{3});
            orig.allocate();
            orig.construct_at(1, Tracker{4}, Tracker{5}, Tracker{6});

            int move_now = Tracker::move_ctor; // 6
            int ctor_now = Tracker::ctor;      // 6
            int copy_now = Tracker::copy_ctor; // 0
            int dtor_now = Tracker::dtor;      // 6

            SOA_STORE<TrackedVec3> moved = std::move(orig);

            // 移动构造仅转移指针，不触及 Tracker
            CHECK(Tracker::move_ctor == move_now);
            CHECK(Tracker::ctor == ctor_now);
            CHECK(Tracker::copy_ctor == copy_now);
            CHECK(Tracker::dtor == dtor_now);
            PASS();
        }

        {
            TEST("move assignment: destroys old elements (3 dtor), no field copy/move");
            Tracker::reset();
            SOA_STORE<TrackedVec3> orig(3);
            orig.allocate();
            orig.construct_at(0, Tracker{10}, Tracker{10}, Tracker{10}); // 1元素
            SOA_STORE<TrackedVec3> dest(5);
            dest.allocate();
            dest.construct_at(0, Tracker{0}, Tracker{0}, Tracker{0}); // 1元素

            int move_now = Tracker::move_ctor; // 6
            int ctor_now = Tracker::ctor;      // 6
            int copy_now = Tracker::copy_ctor; // 0
            int dtor_before = Tracker::dtor;   // 6

            dest = std::move(orig);
            // 源码：先 for (id : used_) data_.destroy_at(id); → 销毁 dest 旧元素 (3 次析构)
            // 然后移动数据成员，不触及 Tracker
            CHECK(Tracker::dtor - dtor_before == 3);
            CHECK(Tracker::move_ctor == move_now);
            CHECK(Tracker::ctor == ctor_now);
            CHECK(Tracker::copy_ctor == copy_now);
            PASS();
        }

        {
            TEST("destructor: calls dtor on each alive field");
            Tracker::reset();
            {
                SOA_STORE<TrackedVec3> store(2);
                store.allocate();
                store.construct_at(0, Tracker{1}, Tracker{2}, Tracker{3});
                store.allocate();
                store.construct_at(1, Tracker{4}, Tracker{5}, Tracker{6});
                // 状态：move_ctor=6, ctor=6, dtor=6 (临时)
            }
            // store 析构销毁 2 个元素 (6 次析构) + 之前临时对象 6 次 = 12
            CHECK(Tracker::dtor == 12);
            CHECK(Tracker::move_ctor == 6);
            CHECK(Tracker::copy_ctor == 0);
            CHECK(Tracker::ctor == 6);
            PASS();
        }

        {
            TEST("grow: relocates alive elements via move ctor (6) + dtor old (6)");
            Tracker::reset();
            SOA_STORE<TrackedVec3> store(2);
            store.allocate();
            store.construct_at(0, Tracker{1}, Tracker{2}, Tracker{3});
            store.allocate();
            store.construct_at(1, Tracker{4}, Tracker{5}, Tracker{6});

            int move_initial = Tracker::move_ctor; // 6
            int ctor_initial = Tracker::ctor;      // 6
            int dtor_initial = Tracker::dtor;      // 6
            int copy_initial = Tracker::copy_ctor; // 0

            store.grow(5);
            // 搬迁：对每个存活字段使用 std::move(old) + placement new → 移动构造 6 次
            // 然后 destroy_at(old) → 析构 6 次
            CHECK(Tracker::move_ctor - move_initial == 6);
            CHECK(Tracker::dtor - dtor_initial == 6);
            CHECK(Tracker::ctor == ctor_initial);
            CHECK(Tracker::copy_ctor == copy_initial);
            PASS();
        }
    }
}

int main()
{
    std::cout << "=== Testing gen_soa_sparse_store ===\n";
    test<gen_soa_sparse_store>();
    //NOTE: 需要手动调用 allocate/construct_at/grow
    std::cout << "=== All gen_soa_sparse_store tests passed ===\n";

    std::cout << "=== Testing gen_soa_store ===\n";
    test<gen_soa_store>();
    //NOTE: 需要手动调用 allocate/construct_at/grow
    std::cout << "=== All gen_soa_store tests passed ===\n";
    return 0;
}