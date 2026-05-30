#pragma once

#include "runtime_type.hpp"

template <static_string... Name, typename... T>
auto make_runtime_type(T &&...member)
    -> runtime_type<runtime_info<Name...>, std::decay_t<T>...>
{
    return {std::forward<T>(member)...};
}