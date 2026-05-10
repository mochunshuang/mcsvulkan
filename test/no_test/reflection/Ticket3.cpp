#include <array>
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
    using id_vaule_type = uint32_t;
    template <id_vaule_type N>
    struct IDSlot; // 不完整 = 未占用，完整 = 已占用

    consteval bool id_slot_is_complete(id_vaule_type i)
    {
        return meta::is_complete_type(
            meta::substitute(^^IDSlot, {
                                           std::meta::reflect_constant(i)}));
    }

    struct TicketManager
    {
        static consteval id_vaule_type latest()
        {
            id_vaule_type k = 0;
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

    template <id_vaule_type>
    struct id_type;

    template <typename T>
    constexpr id_vaule_type type_id_v = type_id<T>::id;

    template <id_vaule_type id>
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

    template <id_vaule_type I>
    consteval std::string_view get_id_type_name()
    {
        // 正确做法：反射底层类型，//NOTE: 必须 dealias
        return std::meta::display_string_of(
            meta::dealias(^^typename id_gen::id_type<I>::type));
    }

    template <id_vaule_type I>
    consteval std::string_view identifier_name()
    {
        return std::meta::identifier_of(^^typename id_gen::id_type<I>::type);
    }

    template <id_vaule_type I, fixed_string Name>
    consteval auto is_match_type_name() noexcept
    {
        return Name == get_id_type_name<I>();
    }

    // 辅助：在编译期整数序列中查找匹配名称的 ID
    template <fixed_string Name, id_vaule_type... Is>
    consteval id_vaule_type find_id_in_seq(std::integer_sequence<id_vaule_type, Is...>)
    {
        template for (constexpr auto I : std::array<id_vaule_type, sizeof...(Is)>{Is...})
        {
            if (meta::is_complete_type(^^id_gen::id_type<I>))
            {
                if (id_gen::is_match_type_name<I, Name>()) // 仅在编译期调用
                    return I;
            }
        }
        return UINT32_MAX;
    }

    // ------ 宏：声明一个类型，并为其分配反射票号 ------
#define REGISTER_TYPE(T)                                            \
    template <>                                                     \
    struct id_gen::type_id<T>                                       \
    {                                                               \
        static constexpr auto id = id_gen::TicketManager::latest(); \
    };                                                              \
    template <>                                                     \
    struct id_gen::id_type<type_id<T>::id>                          \
    {                                                               \
        using type = T;                                             \
    };                                                              \
    consteval                                                       \
    {                                                               \
        id_gen::TicketManager::increment();                         \
    };

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

namespace soa
{
    template <typename T, size_t N>
    struct struct_of_arrays_impl // NOLINT
    {
        struct impl;
        consteval
        {
            auto ctx = std::meta::access_context::current();
            std::vector<std::meta::info> old_members =
                std::meta::nonstatic_data_members_of(^^T, ctx);
            std::vector<std::meta::info> new_members = {};
            for (auto member : old_members)
            {
                //NOTE: substitute 模板实例化？
                auto array_type = std::meta::substitute(
                    ^^std::array, {
                                      std::meta::type_of(member),
                                      std::meta::reflect_constant(N),
                                  });
                // identifier_of 获得字符串名词？
                auto mem_descr = std::meta::data_member_spec(
                    array_type, {.name = std::meta::identifier_of(member)});
                new_members.push_back(mem_descr);
            }
            std::meta::define_aggregate(^^impl, new_members);
        }
    };
    template <typename T, size_t N>
    using struct_of_arrays = struct_of_arrays_impl<T, N>::impl;

    template <typename T>         // N 可选，用于构造时预分配
    struct struct_of_vectors_impl // NOLINT
    {
        struct impl;

        consteval
        {
            auto ctx = std::meta::access_context::current();
            auto old_members = std::meta::nonstatic_data_members_of(^^T, ctx);
            std::vector<std::meta::info> new_members;
            for (auto member : old_members)
            {
                // 成员类型：std::vector<原类型>
                auto vec_type =
                    std::meta::substitute(^^std::vector, {
                                                             std::meta::type_of(member)});
                auto mem_descr = std::meta::data_member_spec(
                    vec_type, {.name = std::meta::identifier_of(member)});
                new_members.push_back(mem_descr);
            }
            std::meta::define_aggregate(^^impl, new_members);
        }
    };

    template <typename T>
    using struct_of_vectors = struct_of_vectors_impl<T>::impl;

}; // namespace soa

using soa::struct_of_arrays;
using soa::struct_of_vectors;
void test_soa()
{
    struct point
    {
        float x;
        float y;
        float z;
    };

    using points = struct_of_arrays<point, 30>;
    // equivalent to:
    // struct points {
    //   std::array<float, 30> x;
    //   std::array<float, 30> y;
    //   std::array<float, 30> z;
    // };
    using x_type = decltype(points{}.x);
    static_assert(std::is_same_v<x_type, std::array<float, 30>>);

    // NOTE: 下面是失败的
    // using points2 = struct_of_arrays2<point, 30>;

    {
        using points_vec = struct_of_vectors<point>;
        points_vec pv;
        // 成员类型都是 std::vector<float>
        static_assert(std::is_same_v<decltype(pv.x), std::vector<float>>);
        static_assert(std::is_same_v<decltype(pv.y), std::vector<float>>);
        static_assert(std::is_same_v<decltype(pv.z), std::vector<float>>);

        // NOLINTBEGIN
        // 使用前需要 resize（或 push_back 等）
        pv.x.resize(30);
        pv.y.resize(30);
        pv.z.resize(30);

        pv.x[0] = 1.0f; // NOLINTEND
        std::cout << "x[0] = " << pv.x[0] << '\n';
    }
}

// ===== 客户端用法 =====
using id_gen::id_vaule_type;
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
using id_gen::find_id_in_seq;

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

static_assert(not meta::is_complete_type(^^type_id<A>));

REGISTER_TYPE(A); // id = 0
REGISTER_TYPE(B); // id = 1
REGISTER_TYPE(C); // id = 2
static_assert(meta::is_complete_type(^^type_id<A>));

template <id_vaule_type... v>
struct type_mask
{
};

// 用 index_sequence 展开所有可能 ID 的 if 分支
template <typename F, id_vaule_type... Is>
decltype(auto) dispatch_impl(F &&func, type_mask<Is...>, id_vaule_type id,
                             auto &&...ags) noexcept
{
    template for (constexpr auto I : std::array<id_vaule_type, sizeof...(Is)>{Is...})
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
    constexpr void operator()(auto &&func, id_vaule_type id, auto &&...ags) const
    {
        dispatch_impl(std::forward<decltype(func)>(func), mask{}, id,
                      std::forward<decltype(ags)>(ags)...);
    }
};

// NOTE: 必须放在最后面。 并且 类型定义必须在前面。
template <fixed_string Name>
consteval auto type_from_name()
{
    constexpr auto max = id_gen::TicketManager::latest();
    static_assert(max == 3, "pass");
    constexpr id_vaule_type id =
        id_gen::find_id_in_seq<Name>(std::make_integer_sequence<id_vaule_type, max>{});
    static_assert(id != UINT32_MAX, "not find type_id");
    return typename id_gen::id_type<id>::type{};
}

// NOTE: 所有可能的 entity 。动态绑定？ 类型如何返回。通过system 或手动强转是很简单的
//NOTE: 如果可能，还可以人工对ID 分组？ 手工优化好像也可以哦
struct Entity
{
    id_vaule_type impl_entity;
};

struct EntityManager
{
    std::array<id_vaule_type, id_gen::TicketManager::latest()> all_entuty;
};

int main()
{
    test_soa();

    std::println("A_string: {}", get_id_type_name<0>());
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