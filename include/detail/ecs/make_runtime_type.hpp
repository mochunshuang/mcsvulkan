#pragma once

#include "runtime_type.hpp"

namespace mcs::vulkan::ecs
{
    template <static_string... Name, typename... T>
    auto make_runtime_type(T &&...member)
        -> runtime_type<runtime_info<Name...>, std::decay_t<T>...>
    {
        return {std::forward<T>(member)...};
    }
}; // namespace mcs::vulkan::ecs
