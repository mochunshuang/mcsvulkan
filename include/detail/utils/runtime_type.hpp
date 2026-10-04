#pragma once

namespace mcs::vulkan
{
    template <typename T>
    struct runtime_type
    {
        static constexpr char value = 0; // NOLINT
    };

    template <typename T>
    inline const void *runtime_type_v = &runtime_type<T>::value; // NOLINT
}; // namespace mcs::vulkan