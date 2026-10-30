#pragma once

#include "static_string.hpp"
#include "aggregate_type.hpp"

namespace mcs::vulkan::meta
{
    struct compose_part
    {
        std::meta::info packed; // reflect_constant_array 的结果

        consteval compose_part(std::meta::info p) : packed(p) {}

        template <std::size_t N>
        consteval compose_part(std::array<std::meta::info, N> p)
            : compose_part(std::meta::reflect_constant_array(p))
        {
        }
        consteval std::size_t size() const noexcept
        {
            return std::meta::extent(std::meta::type_of(packed), 0);
        }
        consteval const std::meta::info *data() const noexcept
        {
            return std::meta::extract<const std::meta::info *>(packed);
        }
        friend consteval bool operator==(const compose_part &a,
                                         const compose_part &b) noexcept
        {
            return a.packed == b.packed;
        }
    };

    struct named_member_group
    {
        static_string name;
        compose_part infos;
    };

    // ======================================================================
    // [compose.hpp] unpack & nested slot
    // ======================================================================
    // 从 compose_part 解回 std::array<info, N>
    template <named_member_group M>
    consteval auto unpack_to_array()
    {
        constexpr auto n = std::meta::extent(std::meta::type_of(M.infos.packed), 0);
        std::array<std::meta::info, n> arr{};
        auto ptr = std::meta::extract<const std::meta::info *>(M.infos.packed);
        for (std::size_t i = 0; i < n; ++i)
            arr[i] = ptr[i];
        return arr;
    }

    // 每个 named_member_group 对应一个独立的嵌套类型槽。
    // 复用 aggregate_type，避免重复的 define_aggregate 逻辑。
    template <named_member_group M>
    using nested_slot = aggregate_type<unpack_to_array<M>()>;

    // ======================================================================
    // [compose.hpp] compose factory
    // ======================================================================
    template <named_member_group... Ms>
    struct compose_impl
    {
        struct type;
        consteval
        {
            constexpr auto N = sizeof...(Ms);
            std::vector<std::meta::info> desc;
            template for (constexpr auto I : std::ranges::views::indices(N))
            {
                auto const &[name, infos] = Ms...[I];
                constexpr auto nested_ty = ^^nested_slot<Ms...[I]>;
                auto is_empty = std::meta::is_empty_type(nested_ty);
                desc.push_back(std::meta::data_member_spec(
                    nested_ty, {.name = name.view(), .no_unique_address = is_empty}));
            }
            std::meta::define_aggregate(^^type, desc);
        }
    };

    template <named_member_group... Ms>
    using composed_type = typename compose_impl<Ms...>::type;

}; // namespace mcs::vulkan::meta