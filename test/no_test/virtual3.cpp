#include <assert.h>
#include <cassert>
#include <iostream>
#include <exception>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <memory>
#include <map>

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

struct name_component
{
    std::string name;
};
class A : public name_component
{
  public:
    A(const A &) = delete;
    A(A &&) = delete;
    A &operator=(const A &) = delete;
    A &operator=(A &&) = delete;
    A(std::string name, int v) noexcept : name_component{std::move(name)}, v{v} {}
    int v{};
    ~A() noexcept
    {
        std::cout << "~A()" << '\n';
    }
};

class B : public name_component
{
  public:
    B(const B &) = delete;
    B(B &&) = delete;
    B &operator=(const B &) = delete;
    B &operator=(B &&) = delete;
    B(std::string name, int v) noexcept : name_component{std::move(name)}, v{v} {}
    int v{};
    ~B() noexcept
    {
        std::cout << "~B()" << '\n';
    }
};

using resource_handle =
    std::unique_ptr<name_component, void (*)(name_component *) noexcept>;

// 自定义的一个 name 唯一集合
class component_set
{
  public:
    // 插入：重名时抛出异常
    void insert_or_throw(resource_handle comp)
    {
        const auto &name = comp->name;
        if (items_.count(name))
        {
            throw std::invalid_argument("duplicate component name: " + name);
        }
        items_.emplace(name, std::move(comp));
    }

    // 插入：重名时返回 false，否则返回 true 并接管所有权
    bool insert(resource_handle comp)
    {
        const auto &name = comp->name;
        if (items_.count(name))
            return false; // 重名，comp 随后析构（释放内存）
        items_.emplace(name, std::move(comp));
        return true;
    }

    // 查找，返回裸指针（或引用），未找到返回 nullptr
    name_component *find(const std::string &name) noexcept
    {
        auto it = items_.find(name);
        return (it != items_.end()) ? it->second.get() : nullptr;
    }

    [[nodiscard]] const name_component *find(const std::string &name) const noexcept
    {
        auto it = items_.find(name);
        return (it != items_.end()) ? it->second.get() : nullptr;
    }

    // 移除并释放（也可以返回对象，按需设计）
    bool erase(const std::string &name) noexcept
    {
        return items_.erase(name) > 0;
    }

    // 所有元素个数
    [[nodiscard]] size_t size() const noexcept
    {
        return items_.size();
    }

    // 清空
    void clear() noexcept
    {
        items_.clear();
    }

  private:
    std::map<std::string, resource_handle> items_;
};

template <component_name Id, typename Fn>
struct name_fn
{
    Fn fn;

    static consteval std::string_view id() noexcept
    {
        return Id.as_view();
    }

    template <typename... T>
        requires std::invocable<Fn, T...>
    auto operator()(T &&...args) noexcept(std::is_nothrow_invocable_v<Fn, T...>)
    {
        return fn(std::forward<T>(args)...);
    }
};
template <typename T>
struct __is_name_fn : std::false_type
{
};
template <auto Name, typename T>
struct __is_name_fn<name_fn<Name, T>> : std::true_type
{
};
template <typename T>
concept is_name_fn = __is_name_fn<T>::value; // NOLINT

template <component_name Id, typename Property>
struct name_property
{

    Property property;                                 // NOLINT
    constexpr auto &operator=(Property value) noexcept // NOLINT
    {
        property = std::move(value);
        return *this;
    }

    static consteval std::string_view id() noexcept
    {
        return Id.as_view();
    }

    // 新增：从 Property 值构造
    // constexpr explicit name_property(Property val) noexcept : property(std::move(val)) {}

    constexpr auto &operator*(this auto &&self) noexcept
    {
        return self.property;
    }

    constexpr bool operator==(Property value) const noexcept
        requires(requires { value == property; })
    {
        return value == property;
    }
    constexpr bool operator!=(Property value) const noexcept
        requires(requires { value != property; })
    {
        return value != property;
    }
    constexpr auto operator<=>(Property value) noexcept // NOLINT
        requires(requires() { value <=> property; })
    {
        return value <=> property;
    }

    // 三路比较（如果 Property 支持 <=>）
    constexpr auto operator<=>(const name_property &other) const noexcept
        requires(requires { property <=> other.property; })
    {
        if (auto cmp = Id.as_view() <=> name_property::id(); cmp != 0)
            return cmp;
        return property <=> other.property;
    }
};
template <typename T>
struct __is_name_property : std::false_type
{
};
template <auto Name, typename T>
struct __is_name_property<name_property<Name, T>> : std::true_type
{
};
template <typename T>
concept is_name_property = __is_name_property<T>::value; // NOLINT

// NOLINTBEGIN
void base()
{
    {
        resource_handle component(new A{"A", 1}, [](name_component *ptr) noexcept {
            delete static_cast<A *>(ptr);
        });
        assert(static_cast<A *>(component.get())->v == 1);
    }
    {
        resource_handle component(new B{"B", 2}, [](name_component *ptr) noexcept {
            delete static_cast<B *>(ptr);
        });
        assert(static_cast<B *>(component.get())->v == 2);

        component.release();
        assert(component.get() == nullptr); //NOTE: 间接的性质
    }
}

void test_component_set()
{
    component_set cs;

    // 1. 用返回值的版本
    bool ok =
        cs.insert(resource_handle(new A("alpha", 10), [](name_component *p) noexcept {
            delete static_cast<A *>(p);
        }));
    assert(ok);
    assert(cs.find("alpha") != nullptr);

    // 重复插入应该失败
    ok = cs.insert(resource_handle(new B("alpha", 20),
                                   [](name_component *p) noexcept {
                                       delete static_cast<B *>(p);
                                   } // 删除器必须匹配真实类型
                                   ));
    assert(!ok); // 失败，之前 new 的 B 已经被删除（unique_ptr 析构）

    // 2. 抛异常的版本
    try
    {
        cs.insert_or_throw(
            resource_handle(new A("beta", 30), [](name_component *p) noexcept {
                delete static_cast<A *>(p);
            }));
        cs.insert_or_throw(
            resource_handle(new B("beta", 40), [](name_component *p) noexcept {
                delete static_cast<B *>(p);
            }));
    }
    catch (const std::exception &e)
    {
        std::cerr << "Expected: " << e.what() << '\n'; // 捕获重复异常
    }
}

// 查找继承列表中 name == Name 的那个基类类型
// ---------- 极简查找 ----------
template <component_name Name, typename... Bases>
struct find_base;
template <component_name Name, typename First, typename... Rest>
struct find_base<Name, First, Rest...>
{
    using type = std::conditional_t<First::id() == Name.as_view(), First,
                                    typename find_base<Name, Rest...>::type>;
};
template <component_name Name>
struct find_base<Name>
{
    using type = void;
};

template <std::size_t I, typename... Bases>
struct base_at;

template <std::size_t I, typename First, typename... Rest>
struct base_at<I, First, Rest...> : base_at<I - 1, Rest...>
{
};

template <typename First, typename... Rest>
struct base_at<0, First, Rest...>
{
    using type = First;
};

template <component_name Id, typename... Bases>
concept has_name = !std::is_same_v<typename find_base<Id, Bases...>::type, void>;

template <typename... T>
consteval bool names_unique() noexcept
{
    std::array<std::string_view, sizeof...(T)> names{T::id()...};
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        for (std::size_t j = i + 1; j < names.size(); ++j)
            if (names[i] == names[j])
                return false;
    }
    return true;
}
template <typename... T>
concept id_unique = names_unique<T...>();

template <component_name Id, typename... Bases>
    requires(id_unique<Bases...>)
struct Component : Bases...
{
    static consteval std::string_view id() noexcept
    {
        return Id.as_view();
    }

    template <component_name Name>
        requires(has_name<Name, Bases...>)
    decltype(auto) get(this auto &&self) noexcept
    {
        using base_type = typename find_base<Name, Bases...>::type;
        using lref =
            std::conditional_t<std::is_const_v<std::remove_reference_t<decltype(self)>>,
                               const base_type &, base_type &>;
        return std::forward_like<decltype(self)>(static_cast<lref>(self));
    }

    template <std::size_t I>
        requires(I < sizeof...(Bases))
    constexpr decltype(auto) get(this auto &&self)
    {
        using base_type = typename base_at<I, Bases...>::type;
        using lref =
            std::conditional_t<std::is_const_v<std::remove_reference_t<decltype(self)>>,
                               const base_type &, base_type &>;
        return std::forward_like<decltype(self)>(static_cast<lref>(self));
    }

    template <typename Fn, typename... As>
    auto invoke(Fn &&fn, As &&...as) noexcept(
        noexcept(std::forward<Fn>(fn)(this, std::forward<As>(as)...)))
        requires(requires() { std::forward<Fn>(fn)(this, std::forward<As>(as)...); })
    {
        return std::forward<Fn>(fn)(this, std::forward<As>(as)...);
    }
};

namespace std
{
    template <component_name Id, typename... Bases>
    struct tuple_size<::Component<Id, Bases...>>
        : integral_constant<size_t, sizeof...(Bases)>
    {
    };

    template <size_t I, component_name Id, typename... Bases>
    struct tuple_element<I, ::Component<Id, Bases...>>
    {
        using type = typename base_at<I, Bases...>::type;
    };
} // namespace std

template <component_name Name, typename Property>
constexpr auto make_property(Property &&val) noexcept(
    noexcept(name_property<Name, Property>{std::forward<Property>(val)}))
{
    return name_property<Name, Property>{std::forward<Property>(val)};
}

template <component_name Name, typename Fn>
constexpr auto make_method(Fn &&f) noexcept(noexcept(name_fn<Name, Fn>{
    std::forward<Fn>(f)}))
{
    return name_fn<Name, Fn>{std::forward<Fn>(f)};
}
template <component_name Id, typename... Ts>
constexpr auto make_component(Ts &&...args) noexcept(
    noexcept(Component<Id, std::remove_cvref_t<Ts>...>{std::forward<Ts>(args)...}))
    requires(requires() {
        Component<Id, std::remove_cvref_t<Ts>...>{std::forward<Ts>(args)...};
    })
{
    return Component<Id, std::remove_cvref_t<Ts>...>{std::forward<Ts>(args)...};
}

int add(int a, int b) noexcept
{
    return a + b;
}
void test_final_forward_like()
{
    using Hp = name_property<"hp", int>;
    using Mana = name_property<"mana", double>;

    auto hero = make_component<"hero">(
        make_property<"hp">(100), make_property<"mana">(50.0),
        make_method<"add">([](int a, int b) noexcept { return a + b; }));
    {
        auto r = hero.get<"add">()(1, 1);
        assert(r == 2);
    }

    {
        auto ret = hero.invoke([](decltype(hero) *self) noexcept {
            std::cout << "self->get<\"hp\">() == 100: " << bool(self->get<"hp">() == 100)
                      << '\n';
            return 1;
        });
        assert(ret == 1);

        auto &[hp, mana, add] = hero; //NOTE:结构化表达
        assert(hp == 100);
        assert(mana == 50.0);
        assert(add(25, 25) == 50.0);
    }

    auto heros = make_component<"heros">(
        std::move(hero),
        make_component<"hero1">(
            make_property<"hp">(100), make_property<"mana">(50.0),
            make_method<"add">([](int a, int b) noexcept { return a + b; })));
    auto refHero = heros.get<"hero">();
    auto refHero1 = heros.get<"hero1">();
    static_assert(std::is_same_v<decltype(hero), decltype(refHero)>);
    static_assert(!std::is_same_v<decltype(refHero1), decltype(refHero)>); //lambda 不同

    {
        auto hero = make_component<"hero0">(make_property<"hp">(100),
                                            make_property<"mana">(50.0));
        auto heros = make_component<"heros">(
            std::move(hero), make_component<"hero1">(make_property<"hp">(100),
                                                     make_property<"mana">(50.0)));
        auto refHero = heros.get<"hero0">();
        auto refHero1 = heros.get<"hero1">();
        static_assert(!std::is_same_v<decltype(refHero1), decltype(refHero)>);

        //NOTE: 与构造参数无关
        static_assert(
            std::is_same_v<decltype(make_component<"hero0">(make_property<"hp">(100),
                                                            make_property<"mana">(50.0))),
                           decltype(make_component<"hero0">(
                               make_property<"hp">(10), make_property<"mana">(5.0)))>);

        //NOTE: 不一样，因为字符串是类型的一部分
        static_assert(not std::is_same_v<
                      decltype(make_component<"hero0">(make_property<"hp">(100),
                                                       make_property<"mana">(50.0))),
                      decltype(make_component<"hero1">(make_property<"hp">(100),
                                                       make_property<"mana">(50.0)))>);
    }

    constexpr auto size = sizeof(decltype(hero));
    static_assert(size == 24);
    struct hero_type
    {
        using add_type = int (*)(int a, int b) noexcept;
        int hp;
        double mana;
        add_type add = [](int a, int b) noexcept {
            return a + b;
        };
    };
    static_assert(sizeof(decltype(hero)) == sizeof(hero_type));

    // 1. 非 const 左值
    {
        auto &hp = hero.get<"hp">();
        static_assert(std::is_same_v<decltype(hp), Hp &>);
        hp = 200;
        assert(hp == 200);
    }

    // 2. const 左值
    {
        const auto &chero = hero;
        const auto &hp = chero.get<"hp">();
        static_assert(std::is_same_v<decltype(hp), const Hp &>);
        // hp = 300; // 禁止
        assert(hp == 200);
    }

    // 3. 非 const 右值
    {
        auto &&hp = std::move(hero).get<"hp">();
        static_assert(std::is_same_v<decltype(hp), Hp &&>);
        hp = 300;
        assert(hp == 300);
        hero.get<"hp">() = 200; // 恢复
    }

    // 4. const 右值 ← 这就是你问的！
    {
        const auto &&hp = std::move(std::as_const(hero)).get<"hp">();
        static_assert(std::is_same_v<decltype(hp), const Hp &&>);
        // hp.property = 100; // 禁止修改
        assert(hp.property == 200);
    }

    std::cout << "All 4 value categories (including const&&) work correctly.\n";
}
// NOLINTEND

int main()
try
{
    std::cout << ">>>   base()\n";
    base();
    std::cout << "\n>>>   test_component_set()\n";
    test_component_set();

    std::cout << "\n>>>   test_final_forward_like()\n";
    test_final_forward_like();

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