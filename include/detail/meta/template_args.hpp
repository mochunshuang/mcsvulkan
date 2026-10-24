#pragma once

#include <meta>

namespace mcs::vulkan::meta
{
    // std::tuple<Ts...> -> {^^Ts...}
    template <class Tuple>
    static consteval auto template_args() noexcept
    {
        return std::meta::template_arguments_of(std::meta::dealias(^^Tuple));
    }
}; // namespace mcs::vulkan::meta
