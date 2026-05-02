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
    // 若不需要排序可改用 std::unordered_map
};

template <component_name Name, typename Fn>
struct name_fn
{
    static constexpr auto name = component_name{Name}; // NOLINT
    Fn fn;

    // 新增：从 Fn 对象构造
    // constexpr explicit name_fn(Fn f) noexcept : fn(std::move(f)) {}

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

template <component_name Name, typename Property>
struct name_property
{
    static constexpr auto name = Name;                 // NOLINT
    Property property;                                 // NOLINT
    constexpr auto &operator=(Property value) noexcept // NOLINT
    {
        property = std::move(value);
        return *this;
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
        if (auto cmp = name.as_view() <=> name_property::name.as_view(); cmp != 0)
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

template <auto str, typename T>
struct mapped_type
{
    static constexpr auto name = component_name{str}; // NOLINT
    using type = T;
};

template <auto Name, is_name_fn... Fn>
    requires(requires() { component_name{Name}; })
struct method_component
{
};

template <auto Name, typename... property>
struct data_component
{
};

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

// 一些测试用的函数和仿函数
int add(int a, int b) noexcept
{
    return a + b;
}

struct multiplier
{
    int factor;
    int operator()(int x) const noexcept
    {
        return x * factor;
    }
};

// 一个可能抛出异常的函数
int safe_divide(int a, int b)
{
    if (b == 0)
        throw std::domain_error("division by zero");
    return a / b;
}

void test_name_fn()
{
    // 1. 使用普通函数
    name_fn<component_name{"add"}, decltype(&add)> fn_add{&add};
    static_assert(decltype(fn_add)::name == "add");
    assert(fn_add(2, 3) == 5);
    std::cout << fn_add.name << "(2,3) = " << fn_add(2, 3) << '\n';

    // 2. 使用 lambda（无捕获）
    auto times2 = [](int x) noexcept {
        return x * 2;
    };
    name_fn<component_name{"times2"}, decltype(times2)> fn_times2{times2};
    static_assert(decltype(fn_times2)::name == "times2");
    assert(fn_times2(10) == 20);

    // 3. 使用有状态的仿函数
    name_fn<component_name{"triple"}, multiplier> fn_triple{multiplier{3}};
    static_assert(decltype(fn_triple)::name == "triple");
    assert(fn_triple(7) == 21);

    // 4. 验证 noexcept 推导
    static_assert(noexcept(fn_add(1, 2))); // add 是 noexcept
    static_assert(!noexcept(name_fn<component_name{"div"}, decltype(&safe_divide)>{
        &safe_divide}(1, 0))); // safe_divide 可能抛

    // 5. 验证约束：传递错误参数类型会触发编译错误（概念检查）
    // fn_add("hello", "world");  // 取消注释会编译失败，因为 add 不接受字符串
}

void test_name_property()
{
    // 1. 创建一个 name_property，名字为 "score"，类型为 int
    name_property<component_name{"score"}, int> score{100};

    // 验证编译期名字
    static_assert(score.name == "score");
    assert(score.name == "score");

    // 验证属性值
    assert(score.property == 100);

    // 2. 使用赋值运算符修改属性
    score = 200;
    assert(score.property == 200);

    // 3. 另一个例子：名字为 "name"，类型为 std::string
    name_property<component_name{"name"}, std::string> player_name{std::string{"Alice"}};
    static_assert(player_name.name.as_view() == "name");
    assert(player_name.property == "Alice");

    player_name = std::string{"Bob"};
    assert(player_name.property == "Bob");

    player_name = "Bob2";
    assert(player_name.property == "Bob2");

    // 4. 与 component 结合（假设 component 接受任意满足 name 约束的类型）
    //    但 name_property 不满足 is_name_fn 概念（如果概念要求有 operator()），
    //    可以单独使用，也可以作为普通成员放在 component 中。
    // 示例：将两个 name_property 作为成员包装进一个聚合体
    struct character
    {
        name_property<component_name{"hp"}, int> hp{100};
        name_property<component_name{"mana"}, double> mana{50.0};
    };

    character hero;
    assert(hero.hp.property == 100);
    hero.hp = 80;
    assert(hero.hp == 80);
    assert(hero.mana.name.as_view() == "mana");

    std::cout << "All name_property tests passed.\n";
}

// 查找继承列表中 name == Name 的那个基类类型
// ---------- 极简查找 ----------
template <auto Name, typename... Bases>
struct find_base;

template <auto Name, typename First, typename... Rest>
struct find_base<Name, First, Rest...>
{
    using type = std::conditional_t<First::name == Name, First,
                                    typename find_base<Name, Rest...>::type>;
};

template <auto Name>
struct find_base<Name>
{
    using type = void; // ← 补上 type，找不到时为 void
};

template <typename... Bases>
struct Component : public Bases...
{
    template <auto Name>
    decltype(auto) get(this auto &&self)
    {
        using base_type = typename find_base<Name, Bases...>::type;
        static_assert(!std::is_same_v<base_type, void>, "not find");
        return static_cast<base_type &>(self);
    }
};
void test_compact_injection()
{
    // 直接用 name_property / name_fn 作为基类注入
    using Hp = name_property<component_name{"hp"}, int>;
    using Mana = name_property<component_name{"mana"}, double>;
    using AddFn = name_fn<component_name{"add"}, decltype(&add)>;

    struct Hero : Component<Hp, Mana, AddFn>
    {
    };

    // 直接构造，参数顺序与基类顺序一致
    Hero hero{
        100,  // Hp
        50.0, // Mana
        &add  // AddFn
    };

    // 现在通过 get<Name>() 反射式访问
    // 获取属性
    assert(hero.get<component_name{"hp"}>() == 100);
    hero.get<component_name{"hp"}>() = 200;
    assert(hero.get<component_name{"hp"}>() == 200);

    // 调用方法
    assert(hero.get<component_name{"add"}>()(2, 3) == 5);

    // 编译期验证类型
    static_assert(std::is_same_v<decltype(hero.get<component_name{"mana"}>()),
                                 name_property<component_name{"mana"}, double> &>);

    std::cout << "Compact injection test passed.\n";
}

template <component_name Name>
consteval auto operator""_cn() noexcept
{
    return Name;
}
template <auto Name, typename Property>
using property = name_property<Name, Property>;

template <auto Name, typename Fn>
using method = name_fn<Name, Fn>;

template <auto Name, typename Property>
constexpr auto make_property(Property val)
{
    return name_property<Name, Property>{val};
}

template <auto Name, typename Fn>
constexpr auto make_method(Fn f)
{
    return name_fn<Name, Fn>{f};
}

template <typename... Bases>
struct Component2 : Bases...
{
    // // 构造函数：按顺序初始化各基类
    // constexpr explicit(sizeof...(Bases) > 0) Component2(Bases... args)
    //     : Bases{std::move(args)}...
    // {
    // }

    template <auto Name>
    decltype(auto) get(this auto &&self) noexcept
    {
        using base_type = typename find_base<Name, Bases...>::type;
        static_assert(!std::is_same_v<base_type, void>, "not found");
        return static_cast<base_type &>(self);
    }
};
template <typename... Ts>
constexpr auto make_component(Ts &&...args)
{
    return Component2<std::remove_cvref_t<Ts>...>{std::forward<Ts>(args)...};
    //                                           ^ 花括号，聚合初始化
}

template <component_name Name, typename Property>
constexpr auto make_property2(Property val)
{
    return name_property<component_name{Name}, Property>{val};
}

template <component_name Name, typename Fn>
constexpr auto make_method2(Fn f)
{
    return name_fn<Name, Fn>{f};
}

template <auto Name, typename... Bases>
concept has_name = !std::is_same_v<typename find_base<Name, Bases...>::type, void>;
template <typename... Bases>
struct Component3 : Bases...
{

    template <component_name Name>
        requires(has_name<Name, Bases...>)
    decltype(auto) get(this auto &&self) noexcept
    {
        using base_type = typename find_base<Name, Bases...>::type;

        // 1. std::forward_like 不能帮我们做类型转换
        // 左值引用类型，保留 self 的 const 限定
        using lref =
            std::conditional_t<std::is_const_v<std::remove_reference_t<decltype(self)>>,
                               const base_type &, base_type &>;

        // 2. static_cast 不能丢弃 const
        // 先用安全的左值转型，再用 forward_like 修饰值类别
        return std::forward_like<decltype(self)>(static_cast<lref>(self));
    }
};
template <typename... Ts>
constexpr auto make_component2(Ts &&...args)
{
    return Component3<std::remove_cvref_t<Ts>...>{std::forward<Ts>(args)...};
    //                                           ^ 花括号，聚合初始化
}

void test_ultra_short()
{
    auto hero =
        make_component(make_property<"hp"_cn>(100), make_property<"mana"_cn>(50.0),
                       make_method<"add"_cn>(&add));

    // 反射式访问
    assert(hero.get<"hp"_cn>() == 100);
    hero.get<"hp"_cn>() = 200;
    assert(hero.get<"hp"_cn>() == 200);
    assert(hero.get<"add"_cn>()(2, 3) == 5);

    // 类型检查依然自动
    // ---------- 类型断言修正 ----------
    static_assert(std::is_same_v<decltype(hero.get<"mana"_cn>()),
                                 name_property<"mana"_cn, double> &>);
    // 直接比较 decltype（引用），或使用 std::is_same_v<decltype(...), Base &>
}

void test_ultra_short2()
{
    auto hero = make_component2(make_property2<"hp">(100), make_property2<"mana">(50.0),
                                make_method2<"add">(&add));

    // 反射式访问
    assert(hero.get<"hp">() == 100);
    hero.get<"hp">() = 200;
    assert(hero.get<"hp">() == 200);
    assert(hero.get<"add">()(2, 3) == 5);

    // 类型检查依然自动
    // ---------- 类型断言修正 ----------
    static_assert(
        std::is_same_v<decltype(hero.get<"mana">()), name_property<"mana", double> &>);
    // 直接比较 decltype（引用），或使用 std::is_same_v<decltype(...), Base &>
}

void test_forward_like_correctness()
{
    using Hp = name_property<component_name{"hp"}, int>;
    using Mana = name_property<component_name{"mana"}, double>;
    using AddFn = name_fn<component_name{"add"}, decltype(&add)>;

    // 构造一个 Component3 对象
    auto hero = make_component2(make_property2<"hp">(100), make_property2<"mana">(50.0),
                                make_method2<"add">(&add));

    // ---------- 1. 非 const 左值 ----------
    {
        auto &hp = hero.get<"hp">();
        static_assert(!std::is_const_v<std::remove_reference_t<decltype(hp)>>);
        static_assert(std::is_lvalue_reference_v<decltype(hp)>);
        hp = 200; // 可修改
        assert(hp == 200);
    }

    // ---------- 2. const 左值 ----------
    const auto &chero = hero;
    {
        const auto &hp = chero.get<"hp">();
        static_assert(std::is_const_v<std::remove_reference_t<decltype(hp)>>);
        static_assert(std::is_lvalue_reference_v<decltype(hp)>);
        // hp = 300;                 // 禁止修改，编译错误（符合预期）
        assert(hp == 200); // 可读
    }

    // ---------- 3. 右值 ----------
    {
        auto &&hp = std::move(hero).get<"hp">();
        static_assert(!std::is_const_v<std::remove_reference_t<decltype(hp)>>);
        static_assert(std::is_rvalue_reference_v<decltype(hp)>);
        hp = 300; // 修改右值（数据有效）
        assert(hp == 300);
        // 注意 hero 已被移动，但 hero 本身仍有效（我们的对象是栈上的聚合体）
        // 恢复原值以便后续测试
        hero.get<"hp">() = 100;
    }

    // ---------- 4. const 右值 ----------
    {
        const auto &&hp = std::move(std::as_const(hero)).get<"hp">();
        static_assert(std::is_const_v<std::remove_reference_t<decltype(hp)>>);
        static_assert(std::is_rvalue_reference_v<decltype(hp)>);
        // hp = 500;                 // 编译错误
        assert(hp == 100); // 可读
    }

    // ---------- 5. 函数调用（非 const 左值） ----------
    assert(hero.get<"add">()(2, 3) == 5);

    // ---------- 6. 类型精确匹配 ----------
    static_assert(std::is_same_v<decltype(hero.get<"mana">()), Mana &>);
    static_assert(
        std::is_same_v<decltype(std::as_const(hero).get<"mana">()), const Mana &>);
    static_assert(std::is_same_v<decltype(std::move(hero).get<"mana">()), Mana &&>);
    static_assert(std::is_same_v<decltype(std::move(std::as_const(hero)).get<"mana">()),
                                 const Mana &&>);

    std::cout << "All forward_like correctness tests passed.\n";
}

void test_final_forward_like()
{
    using Hp = name_property<component_name{"hp"}, int>;
    using Mana = name_property<component_name{"mana"}, double>;
    using AddFn = name_fn<component_name{"add"}, decltype(&add)>;

    auto hero = make_component2(make_property2<"hp">(100), make_property2<"mana">(50.0),
                                make_method2<"add">(&add));

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

    std::cout << "\n>>>   test_name_fn()\n";
    test_name_fn();

    std::cout << "\n>>>   test_name_property()\n";
    test_name_property();

    std::cout << "\n>>>   test_compact_injection()\n";
    test_compact_injection();

    std::cout << "\n>>>   test_ultra_short()\n";
    test_ultra_short();

    std::cout << "\n>>>   test_ultra_short2()\n";
    test_ultra_short2();

    std::cout << "\n>>>   test_forward_like_correctness()\n";
    test_forward_like_correctness();

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