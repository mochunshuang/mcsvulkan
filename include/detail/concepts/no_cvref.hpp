#pragma once

#include <type_traits>

namespace mcs::vulkan::concepts
{
    template <typename... T>
    concept no_cvref = (std::is_same_v<T, std::remove_cvref_t<T>> && ...);
};
