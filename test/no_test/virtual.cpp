#include <assert.h>
#include <algorithm>
#include <iostream>
#include <exception>

#include <memory>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

template <std::size_t N>
struct component_name
{
    char data[N]{};                                         // NOLINT
    constexpr component_name(const char (&str)[N]) noexcept // NOLINT
    {
        std::copy_n(str, N, data);
    }
    // 必须提供默认比较运算符（C++20 自动生成也可以）
    constexpr bool operator==(const component_name &) const = default;
};

template <typename T, auto... V>
struct component_id
{
};
template <typename T, auto... V>
struct alignas_component
{
    using type = component_id<T, V...>;
};
template <typename T, auto... V>
using alignas_component_t = alignas_component<T, V...>::type;

class Component
{
    using update_fun = void (*)(Component *, float deltaTime) noexcept;

    update_fun update_;
    std::type_index type_;

  public:
    using update_type = update_fun;
    constexpr Component(update_fun update, std::type_index type) noexcept
        : update_{update}, type_{type}
    {
        assert(update_ != nullptr);
    }
    Component(const Component &) = delete;
    Component(Component &&) = delete;
    Component &operator=(const Component &) = delete;
    Component &operator=(Component &&) = delete;
    ~Component() noexcept = default;
    void update(float deltaTime) noexcept
    {
        update_(this, deltaTime);
    }
    [[nodiscard]] std::type_index type() const noexcept
    {
        return type_;
    }
};

template <typename Impl, auto... fun>
    requires(requires(Impl data, float deltaTime) {
        { fun(&data, deltaTime) } noexcept;
    } && ...)
class ComponentImpl : public Component
{
    Impl data_;

  public:
    explicit ComponentImpl(Impl data,
                           std::type_index type = typeid(ComponentImpl)) noexcept
        : Component(&ComponentImpl::doUpdate, type), data_{std::move(data)}
    {
    }
    static void doUpdate(Component *ptr, [[maybe_unused]] float deltaTime) noexcept
    {
        [[maybe_unused]] auto *self = static_cast<ComponentImpl *>(ptr);
        (fun(&self->data_, deltaTime), ...);
    }
    constexpr auto &get() noexcept
    {
        return data_;
    }
    [[nodiscard]] constexpr const auto &get() const noexcept
    {
        return data_;
    }
};

class Entity
{
  private:
    using resource_handle = std::unique_ptr<Component, void (*)(Component *) noexcept>;
    std::vector<resource_handle> components_; //NOTE: 热加载也快

  public:
    [[nodiscard]] auto size() const noexcept
    {
        return components_.size();
    }
    template <typename T, typename... Args>
        requires(std::is_base_of_v<Component, T>)
    T *addComponent(Args &&...args)
        requires(requires() { new T{std::forward<Args>(args)...}; })
    {
        resource_handle component(
            new T{std::forward<Args>(args)...},
            [](Component *ptr) noexcept { delete static_cast<T *>(ptr); });
        T *componentPtr = static_cast<T *>(component.get());
        components_.push_back(std::move(component));
        return componentPtr;
    }

    template <typename T>
    T *getComponent(std::type_index type) noexcept
    {
        for (const auto &component : components_)
            if (auto *result = component.get(); type == result->type())
                return static_cast<T *>(result);
        return nullptr;
    }
    template <typename T>
    T *getComponent() noexcept
    {
        for (const auto &component : components_)
            if (auto *result = component.get(); typeid(T) == result->type())
                return static_cast<T *>(result);
        return nullptr;
    }

    void update(float deltaTime) noexcept
    {
        for (const auto &component : components_)
        {
            component->update(deltaTime);
        }
    }

    // bool removeComponent(std::type_index type) noexcept
    // {
    //     if (auto it = std::ranges::find_if(
    //             components_,
    //             [&](const resource_handle &h) noexcept { return h->type() == type; });
    //         it != components_.end())
    //     {
    //         components_.erase(it);
    //         return true;
    //     }
    //     return false;
    // }
};

int main()
try
{
    struct a_class
    {
        int v = 0;
    };

    struct b_class
    {
        int v = 0;
    };

    Entity e{};
    e.addComponent<ComponentImpl<a_class>>(
        a_class{1},
        typeid(alignas_component_t<a_class, component_name{"first a_class"}>));
    e.addComponent<ComponentImpl<a_class>>(a_class{2},
                                           typeid(alignas_component_t<a_class, 2>));
    e.addComponent<ComponentImpl<b_class>>(b_class{3});

    assert(e.getComponent<ComponentImpl<a_class>>(
                typeid(alignas_component_t<a_class, component_name{"first a_class"}>))
               ->get()
               .v == 1);
    assert(e.getComponent<ComponentImpl<a_class>>(typeid(alignas_component_t<a_class, 2>))
               ->get()
               .v == 2);
    assert(e.getComponent<ComponentImpl<b_class>>()->get().v == 3);

    // assert(e.removeComponent(typeid(ComponentImpl<b_class>)));
    // std::cout << "e.getComponent<ComponentImpl<b_class>>() == nullptr: "
    //           << (e.getComponent<ComponentImpl<b_class>>() == nullptr) << '\n';

    std::cout << "e.size(): " << e.size() << '\n';

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