#include "gen_soa_sparse_store.hpp"
#include "world.hpp"
#include <iostream>
#include <print>
#include <ranges>

// NOLINTBEGIN
// ===== 将实体所有字段定义在一个 struct 内 =====
struct Entity
{
    struct Position
    {
        float x, y;
    } pos;
    struct Velocity
    {
        float vx, vy;
    } vel;
    struct Health
    {
        int hp;
    } hp;
};
int main()
{
    gen_soa_sparse_store<Entity> store(1024); // 预分配 1024 个实体槽位
    auto e1 = store.allocate().value();
    store.construct_at(e1, Entity::Position{1.0f, 2.0f}, Entity::Velocity{0.5f, -0.2f},
                       Entity::Health{100});
    auto e2 = store.allocate().value();
    store.construct_at(e2, Entity::Position{10.0f, 20.0f}, Entity::Velocity{-1.0f, 0.3f},
                       Entity::Health{200});
    // 移动系统：只访问 pos 和 vel 字段（Health 列根本不加载）
    for (size_t id : store.used_slots())
    {
        auto &[x, y] = store.template get<"pos">(id);
        auto &[vx, vy] = store.template get<"vel">(id);
        x += vx;
        y += vy;
    }
    // 中毒系统：只访问 hp 字段
    for (size_t id : store.used_slots())
    {
        auto &[hp] = store.template get<"hp">(id);
        hp -= 10;
    }
    assert(bool(store.template get<"pos">(e1).x == 1.5f));
    assert(store.template get<"hp">(e1).hp == 90);
    store.release(e1);
    {
        using soa_list =
            soa_class<name_spec{"Entity_soa_store", ^^gen_soa_sparse_store<Entity>}>;
        using world_type = world<soa_list>;
        world_type w;
        auto &store = w.get_soa<"Entity_soa_store">();
        auto e1 = store.allocate().value();
        store.construct_at(e1, Entity::Position{1.0f, 2.0f},
                           Entity::Velocity{0.5f, -0.2f}, Entity::Health{100});
        auto e2 = store.allocate().value();
        store.construct_at(e2, Entity::Position{10.0f, 20.0f},
                           Entity::Velocity{-1.0f, 0.3f}, Entity::Health{200});
        // 移动系统：只访问 pos 和 vel 字段（Health 列根本不加载）
        for (size_t id : store.used_slots())
        {
            auto &[x, y] = store.template get<"pos">(id);
            auto &[vx, vy] = store.template get<"vel">(id);
            x += vx;
            y += vy;
        }
        // 中毒系统：只访问 hp 字段
        for (size_t id : store.used_slots())
        {
            auto &[hp] = store.template get<"hp">(id);
            hp -= 10;
        }
        assert(bool(store.template get<"pos">(e1).x == 1.5f));
        assert(store.template get<"hp">(e1).hp == 90);
    }

    std::println("ECS demo with single gen_soa_sparse_store passed.");
    return 0;
} // NOLINTEND