#include <assert.h>
#include <cstdint>
#include <exception>
#include <iostream>
#include <meta>
#include <print>
#include <string_view>
#include <type_traits>

namespace meta = std::meta;

namespace id_gen
{
    // ------ 反射维护的票号池 ------
    template <uint32_t N>
    struct IDSlot; // 不完整 = 未占用，完整 = 已占用

    consteval bool id_slot_is_complete(uint32_t i)
    {
        return meta::is_complete_type(
            meta::substitute(^^IDSlot, {
                                           std::meta::reflect_constant(i)}));
    }

    struct TicketManager
    {
        static consteval uint32_t latest()
        {
            uint32_t k = 0;
            while (id_slot_is_complete(k))
                ++k;
            return k;
        }

        static consteval void increment()
        {
            meta::define_aggregate(
                meta::substitute(^^IDSlot,
                                 {
                                     std::meta::reflect_constant(latest())}),
                {}); // 空聚合体，目的只是让类型“完整”
        }
    };
    // ------ type_id 主模板：从外部接收 ID ------
    template <typename T>
    struct type_id;

    template <uint32_t>
    struct id_type;

    template <typename T>
    constexpr uint32_t type_id_v = type_id<T>::id;

    template <uint32_t id>
    using id_type_t = typename id_type<id>::type;

    template <size_t N>
    struct fixed_string
    {
        char data[N]{};
        constexpr fixed_string(const char (&str)[N])
        {
            std::copy_n(str, N, data);
        }
        constexpr bool operator==(const std::string_view &o) const noexcept
        {
            return std::string_view{data, N - 1} == o;
        }
    };

    template <uint32_t I>
    consteval std::string_view get_id_type_name()
    {
        // 正确做法：反射底层类型，//NOTE: 必须 dealias
        return std::meta::display_string_of(
            meta::dealias(^^typename id_gen::id_type<I>::type));
    }

    template <uint32_t I>
    consteval std::string_view identifier_name()
    {
        return std::meta::identifier_of(^^typename id_gen::id_type<I>::type);
    }

    template <uint32_t I, fixed_string Name>
    consteval auto is_match_type_name() noexcept
    {
        return Name == get_id_type_name<I>();
    }

}; // namespace id_gen

// 格式化支持 (C++20/23)
template <std::size_t N>
struct std::formatter<id_gen::fixed_string<N>> : std::formatter<std::string_view>
{
    auto format(const id_gen::fixed_string<N> &cn, std::format_context &ctx) const
    {
        return std::formatter<std::string_view>::format(cn.as_view(), ctx);
    }
};

// ------ 宏：声明一个类型，并为其分配反射票号 ------
#define REGISTER_TYPE(T)                                                \
    namespace id_gen                                                    \
    {                                                                   \
        consteval                                                       \
        {                                                               \
            TicketManager::increment();                                 \
        };                                                              \
        template <>                                                     \
        struct type_id<T>                                               \
        {                                                               \
            static constexpr uint32_t id = TicketManager::latest() - 1; \
        };                                                              \
        template <>                                                     \
        struct id_type<type_id<T>::id>                                  \
        {                                                               \
            using type = T;                                             \
        };                                                              \
    };

// ===== 客户端用法 =====
using id_gen::type_id;
using id_gen::id_type;
using id_gen::type_id_v;
using id_gen::id_type_t;
using id_gen::TicketManager;
using id_gen::id_slot_is_complete;
using id_gen::get_id_type_name;
using id_gen::is_match_type_name;
using id_gen::fixed_string;
using id_gen::identifier_name;

struct A
{
    int x = 1;
};
struct B
{
    double y = 2.0;
};
struct C
{
    char z = 'c';
};

struct D
{
};
static_assert(meta::is_complete_type(^^D));

static_assert(not meta::is_complete_type(^^type_id<A>));

REGISTER_TYPE(A); // id = 0
REGISTER_TYPE(B); // id = 1
REGISTER_TYPE(C); // id = 2

static_assert(type_id<A>::id == 0);
static_assert(type_id<B>::id == 1);
static_assert(type_id<C>::id == 2);
static_assert(std::is_same_v<A, id_type<0>::type>);
static_assert(std::is_same_v<B, id_type<1>::type>);
static_assert(std::is_same_v<C, id_type<2>::type>);

static_assert(meta::is_complete_type(^^type_id<A>));

template <uint32_t... v>
struct type_mask
{
};

// 用 index_sequence 展开所有可能 ID 的 if 分支
template <typename F, uint32_t... Is>
decltype(auto) dispatch_impl(F &&func, type_mask<Is...>, uint32_t id,
                             auto &&...ags) noexcept
{
    template for (constexpr auto I : std::array<uint32_t, sizeof...(Is)>{Is...})
    {
        if (id == I)
        {
            std::forward<F>(func).template operator()<I>(
                std::forward<decltype(ags)>(ags)...);
            return;
        }
    }
    std::println("discard Unknown type id: {}", id);
}

template <typename mask>
struct dispatcher
{
    constexpr void operator()(auto &&func, uint32_t id, auto &&...ags) const
    {
        dispatch_impl(std::forward<decltype(func)>(func), mask{}, id,
                      std::forward<decltype(ags)>(ags)...);
    }
};

// 辅助：在编译期整数序列中查找匹配名称的 ID
template <fixed_string Name, uint32_t... Is>
consteval uint32_t find_id_in_seq(std::integer_sequence<uint32_t, Is...>)
{
    template for (constexpr auto I : std::array<uint32_t, sizeof...(Is)>{Is...})
    {
        if (meta::is_complete_type(^^id_type<I>))
        {
            if (is_match_type_name<I, Name>()) // 仅在编译期调用
                return I;
        }
    }
    return UINT32_MAX;
}

static_assert(TicketManager::latest() == 3);

template <fixed_string Name>
consteval auto type_from_name()
{

    constexpr uint32_t id = find_id_in_seq<Name>(
        std::make_integer_sequence<uint32_t, TicketManager::latest()>{});

    static_assert(id != UINT32_MAX, "not find type_id");
    return typename id_type<id>::type{};
}
constexpr std::string_view current_name =
    std::meta::display_string_of(^^typename id_type<0>::type);
static_assert(current_name == std::string_view{"A"});
static_assert(current_name == fixed_string{"A"});

static_assert(std::meta::display_string_of(^^typename id_type<0>::type) == "A");

constexpr auto A_string = get_id_type_name<0>();

//

constexpr uint32_t id_A =
    find_id_in_seq<"A">(std::make_integer_sequence<uint32_t, TicketManager::latest()>{});
static_assert(id_A == 0, "should 0");

static_assert(is_match_type_name<0, "A">());

int main()
{
    std::println("A_string: {}", std::string_view{A_string});
    std::println("identifier_name: {}", identifier_name<0>()); //NOTE: 别名很可怕
    std::cout << "A: " << type_id<A>::id << '\n';
    std::cout << "B: " << type_id<B>::id << '\n';
    std::cout << "C: " << type_id<C>::id << '\n';

    A a{1};
    B b{2.0};
    C c{.z = 'z'};

    using MyA = decltype(type_from_name<"A">()); // 得到 A
    static_assert(std::is_same_v<MyA, A>);

    using mask_A_C = type_mask<type_id_v<A>, type_id_v<C>>;
    constexpr auto dispatch_a_c = dispatcher<mask_A_C>{};

    constexpr auto fun = []<size_t I>(auto &&...ags) noexcept {
        if constexpr (I == type_id_v<A>)
        {
            if constexpr (meta::is_same_type(^^A,
                                             ^^std::remove_cvref_t<decltype(ags...[0])>))
                std::println("A eat dispatch: {}, id: {}", ags...[0].x, I);
        }
        else if (I == type_id_v<C>)
        {
            if constexpr (meta::is_same_type(^^C,
                                             ^^std::remove_cvref_t<decltype(ags...[0])>))
                std::println("C eat dispatch: {}, id: {}", ags...[0].z, I);
        }
    };
    try
    {
        dispatch_a_c(fun, type_id<A>::id, a);
        dispatch_a_c(fun, type_id<C>::id, c);
        dispatch_a_c(fun, type_id<B>::id, b, 1, 2);
    }
    catch (const std::exception &e)
    {
        std::cout << "what: " << e.what() << '\n';
    }

    //NOTE: 下面不允许
    // int i = {};
    // constexpr auto v = std::meta::reflect_constant(i);

    //NOTE: 下面的不行
    // struct C;
    // REGISTER_TYPE(C);
    // std::cout << "C2: " << type_id<C>::id << '\n';

    return 0;
}