#pragma once

#include "gen_soa_aggregate.hpp"

namespace mcs::vulkan::ecs
{
    template <class T, multiple_field... info>
        requires(detail::unique_field_name(std::array{info...}))
    struct gen_soa_struct : gen_soa_aggregate<info...>
    {
        using base_type = gen_soa_aggregate<info...>;
        using base_type::base_type;
        using trait_type = T;
    };
}; // namespace mcs::vulkan::ecs