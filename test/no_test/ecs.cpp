#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

// NOLINTBEGIN
template <std::size_t N>
    requires(N > 0)
struct component_name
{
    char data[N]{};                                         // NOLINT
    constexpr component_name(const char (&str)[N]) noexcept // NOLINT
    {
        std::copy_n(str, N, data);
    }
    // 必须提供默认比较运算符（C++20 自动生成也可以）
    constexpr bool operator==(const component_name &) const = default;

    // 返回不含结尾 '\0' 的 string_view
    [[nodiscard]] constexpr std::string_view as_view() const noexcept // NOLINT
    {
        // N >= 1 且 data[N-1] 应为 '\0'，合法字面量保证这一点
        return {data, N - 1};
    }

    // 与 string_view 比较
    constexpr bool operator==(std::string_view sv) const noexcept
    {
        return as_view() == sv;
    }

    // 与 const char* 比较
    constexpr bool operator==(const char *s) const noexcept
    {
        return as_view() == std::string_view(s);
    }

    // 可选：让任意长度的 component_name 也能互相比较（基于字符串内容）
    template <std::size_t M>
    constexpr bool operator==(const component_name<M> &other) const noexcept
    {
        if constexpr (M != N)
            return false;
        else
            return as_view() == other.as_view();
    }
};
// 流输出
template <std::size_t N>
std::ostream &operator<<(std::ostream &os, const component_name<N> &cn) noexcept
{
    return os << cn.as_view();
}

// 格式化支持 (C++20/23)
template <std::size_t N>
struct std::formatter<component_name<N>> : std::formatter<std::string_view>
{
    auto format(const component_name<N> &cn, std::format_context &ctx) const
    {
        return std::formatter<std::string_view>::format(cn.as_view(), ctx);
    }
};

// ====================== 编译期类型 ID 分配器 ======================
namespace detail
{
    inline int next_type_id() noexcept
    {
        static int counter = 0;
        return counter++;
    }
} // namespace detail

template <typename T>
struct type_id
{
    static const int value;
};

template <typename T>
const int type_id<T>::value = detail::next_type_id();

// ====================== 编译期字符串哈希 (FNV-1a) ======================
consteval size_t fnv1a_hash(const char *str, size_t h = 14695981039346656037ull) noexcept
{
    return (*str)
               ? fnv1a_hash(str + 1, (h ^ static_cast<size_t>(*str)) * 1099511628211ull)
               : h;
}

// ====================== 统一组件键类型（基于编译期哈希） ======================
using component_id = size_t;

// ====================== 外部静态绑定：component_key（C++20 NTTP 版本） ======================
template <component_name Name, typename T>
struct component_key
{
    static constexpr component_name name = Name;
    using type = T;
    static constexpr component_id id = fnv1a_hash(Name.data); // 编译期计算
};

// ====================== ECS 核心：World ======================
class World
{
  public:
    // ---------- 实体定义 ----------
    struct Entity
    {
        int id = -1;
        // 组件键 -> {类型ID, 非拥有指针}
        std::unordered_map<component_id, std::pair<int, void *>> components;
    };

    // ---------- 实体生命周期 ----------
    Entity &create_entity()
    {
        entities_.emplace_back(std::make_unique<Entity>(next_entity_id_++));
        return *entities_.back(); // 地址稳定，不会因为后续扩容失效
    }

    void destroy_entity(Entity &entity)
    {
        for (auto &[cid, pair] : entity.components)
        {
            remove_component_from_pool(entity, cid);
        }
        entity.components.clear();
        if (auto it = std::ranges::find_if(
                entities_, [&](const auto &e) { return e->id == entity.id; });
            it != entities_.end())
        {
            *it = std::move(entities_.back());
            entities_.pop_back();
        }
    }

    // ---------- 添加组件（核心：component_id + 值）----------
    template <typename T>
    void add_component(Entity &entity, component_id cid, T component)
    {
        int tid = type_id<T>::value;
        auto &pool = get_pool<T>();

        auto it = entity.components.find(cid);
        if (it != entity.components.end())
        {
            std::cout << "      >>> move not new .....\n";
            // 已经存在，原地赋值
            T &old = *static_cast<T *>(it->second.second);
            old = std::move(component); // 直接替换内容
            return;                     // 不用动 pool
        }
        // 不存在才走 new + push_back
        T *ptr = new T(std::move(component));
        pool.push_back(ptr);
        entity.components[cid] = {tid, ptr};
    }

    // ---------- 便捷重载：通过 component_key 推导 id ----------
    template <typename Key, typename... Args>
    void add_component(Entity &entity, Args &&...args)
    {
        add_component<typename Key::type>(
            entity, Key::id, typename Key::type{std::forward<Args>(args)...});
    }

    // ---------- 移除组件 ----------
    void remove_component(Entity &entity, component_id cid)
    {
        auto it = entity.components.find(cid);
        if (it != entity.components.end())
        {
            remove_component_from_pool(entity, cid);
            entity.components.erase(it);
        }
    }

    // ---------- 静态获取（硬编码类型，零开销）----------
    template <typename T>
    T &get(Entity &entity, component_id cid)
    {
        auto &[tid, ptr] = entity.components.at(cid);
        assert(tid == type_id<T>::value && "Type mismatch in get<T>");
        return *static_cast<T *>(ptr);
    }

    // ---------- 动态 visit：按候选类型列表分发 ----------
    template <typename... Types, typename Visitor>
    void visit(Entity &entity, component_id cid, const Visitor &visitor)
    {
        auto it = entity.components.find(cid);
        if (it == entity.components.end())
            return;

        auto [tid, ptr] = it->second;
        bool handled =
            ((tid == type_id<Types>::value ? (visitor(*static_cast<Types *>(ptr)), true)
                                           : false) ||
             ...);
        if (!handled)
            throw std::runtime_error("Component type not in provided type list");
    }
    // ---------- 便捷重载：通过 component_key 推导 id ----------
    template <typename Key, typename Visitor>
    void visit(Entity &entity, Visitor &&visitor)
    {
        using type = typename Key::type;

        auto it = entity.components.find(Key::id);
        if (it == entity.components.end())
            return;

        if (auto [tid, ptr] = it->second; tid == type_id<type>::value)
        {
            if constexpr (requires() { std::visit(visitor, *static_cast<type *>(ptr)); })
            {
#if (__cpp_lib_variant >= 202306L)
                *static_cast<type *>(ptr).visit(visitor);
#else
                std::visit(visitor, *static_cast<type *>(ptr));
#endif
            }
            else
                visitor(*static_cast<type *>(ptr));
            return;
        }
        throw std::runtime_error("Component type not in provided type list");
    }

    // ---------- 析构：清理全部池子 ----------
    ~World()
    {
        for (auto &[tid, ptrs] : pools_)
        {
            auto it = deleters_.find(tid);
            if (it != deleters_.end())
            {
                for (void *p : ptrs)
                {
                    it->second(p);
                }
            }
        }
    }

  private:
    // ---------- 组件池管理 ----------
    std::unordered_map<int, std::vector<void *>> pools_;
    std::unordered_map<int, void (*)(void *) noexcept> deleters_;

    template <typename T>
    std::vector<void *> &get_pool()
    {
        const auto &tid = type_id<T>::value;
        auto it = pools_.find(tid);
        if (it == pools_.end())
        {
            pools_[tid] = {};
            deleters_[tid] = [](void *p) noexcept {
                delete static_cast<T *>(p);
            };
        }
        return pools_[tid];
    }

    void remove_component_from_pool(Entity &entity, component_id cid)
    {
        auto &[tid, ptr] = entity.components.at(cid);
        auto &pool = pools_.at(tid);
        if (auto it = std::ranges::find(pool, ptr); it != pool.end())
        {
            deleters_.at(tid)(ptr);
            *it = pool.back();
            pool.pop_back();
        }
    }

    // ---------- 实体列表 ----------
    std::vector<std::unique_ptr<Entity>> entities_;
    int next_entity_id_ = 0;
};

// ====================== 示例组件 ======================
struct Health
{
    int value = 100;
};

struct Position
{
    float x = 0.0f, y = 0.0f;
};

// 定义组件键：无需宏，直接使用 C++20 NTTP
using hp_key = component_key<"hp", Health>;
using pos_key = component_key<"pos", Position>;

using var_hp_pos_key =
    component_key<"var_hp_pos", std::variant<std::monostate, Health, Position>>;

// ====================== 使用示例 ======================
int main()
{
    //NOTE: 间接引用，也没有实现 缓存友好的
    World world;
    auto &hero = world.create_entity();
    auto &monster = world.create_entity();
    auto &var_hp_pos = world.create_entity();

    {
        auto var_hp_pos_visitor = [](var_hp_pos_key::type &comp) {
            std::visit(
                [](auto &comp) {
                    using T = std::decay_t<decltype(comp)>;
                    if constexpr (std::is_same_v<T, std::monostate>)
                    {
                        std::cout << "var_hp_pos_visitor2 std::monostate: " << '\n';
                    }
                    else if constexpr (std::is_same_v<T, Position>)
                    {
                        std::cout << "var_hp_pos_visitor2 Position: " << comp.x << ", "
                                  << comp.y << '\n';
                    }
                    else if constexpr (std::is_same_v<T, Health>)
                    {
                        std::cout << "var_hp_pos_visitor2 Health: " << comp.value << '\n';
                    }
                },
                comp);
        };

        world.add_component<var_hp_pos_key>(var_hp_pos, Position{1.0f, 2.0f});
        // 4. 多类型 visit：根据键的实际类型分发
        world.visit<var_hp_pos_key>(var_hp_pos, var_hp_pos_visitor);

        world.add_component<var_hp_pos_key>(var_hp_pos, std::monostate{});
        // 4. 多类型 visit：根据键的实际类型分发
        world.visit<var_hp_pos_key>(var_hp_pos, [](auto &comp) {
            using T = std::decay_t<decltype(comp)>;
            if constexpr (std::is_same_v<T, std::monostate>)
            {
                std::cout << "var_hp_pos_visitor std::monostate: " << '\n';
            }
            else if constexpr (std::is_same_v<T, Position>)
            {
                std::cout << "var_hp_pos_visitor Position: " << comp.x << ", " << comp.y
                          << '\n';
            }
            else if constexpr (std::is_same_v<T, Health>)
            {
                std::cout << "var_hp_pos_visitor Health: " << comp.value << '\n';
            }
        });
    }

    // 1. 通过 component_key 添加组件（完全静态）
    world.add_component<hp_key>(hero, Health{100});
    world.add_component<pos_key>(hero, Position{1.0f, 2.0f});
    world.add_component<hp_key>(monster, Health{200});
    world.add_component<hp_key>(monster, Health{200});

    // 2. 静态访问
    Health &heroHp = world.get<Health>(hero, hp_key::id);
    heroHp.value = 150;
    std::cout << "Hero HP: " << heroHp.value << '\n';

    // 3. 动态 visit：自动匹配类型
    world.visit<Health>(hero, hp_key::id, [](Health &health) {
        std::cout << "Visited Health: " << health.value << '\n';
        health.value += 50;
    });
    world.visit<hp_key>(hero, [](Health &health) {
        std::cout << "Visited Health: " << health.value << '\n';
        health.value += 50;
    });

    auto hero_visitor = [](auto &comp) {
        using T = std::decay_t<decltype(comp)>;
        if constexpr (std::is_same_v<T, Position>)
        {
            std::cout << "Visited Position: " << comp.x << ", " << comp.y << '\n';
        }
        else if constexpr (std::is_same_v<T, Health>)
        {
            std::cout << "Visited Health: " << comp.value << '\n';
        }
    };

    //NOTE: 顺序是如何保证呢？
    // 4. 多类型 visit：根据键的实际类型分发
    world.visit<Health, Position>(hero, pos_key::id, hero_visitor);
    world.visit<Health, Position>(hero, hp_key::id, hero_visitor);

    // 5. 热插拔：同名组件覆盖
    world.add_component<hp_key>(hero, Health{999});
    std::cout << "After hot-swap: " << world.get<Health>(hero, hp_key::id).value << '\n';

    // 6. 移除组件
    world.remove_component(hero, pos_key::id);

    // 7. 销毁实体
    world.destroy_entity(monster);

    return 0;
}
// NOLINTEND