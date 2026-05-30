#pragma once

#include "name_spec.hpp"

namespace mcs::vulkan::meta
{
    template <name_spec... infos>
    struct make_aggregate
    {
        struct type;
        consteval
        {
            std::vector<std::meta::info> nsdms;
            template for (const auto &spec : {infos...})
            {
                nsdms.push_back(
                    std::meta::data_member_spec(spec.info, {.name = spec.name.view()}));
            }
            std::meta::define_aggregate(^^type, nsdms);
        }
    };

    template <name_spec... infos>
    using make_aggregate_t = make_aggregate<infos...>::type;

}; // namespace mcs::vulkan::meta