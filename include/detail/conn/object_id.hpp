#pragma once

#include <bit>
#include <functional>

namespace mcs::vulkan::conn
{
    struct object_id final
    {
        constexpr explicit object_id(const void *ptr) noexcept
            : value_{std::bit_cast<size_t>(ptr)}
        {
        }
        constexpr explicit object_id(size_t v) noexcept : value_{v} {}

        bool operator==(const object_id &other) const noexcept = default;
        auto operator<=>(const object_id &b) const = default;

        // NOTE: 使用地址避免 typeid 的运行时计算. 能减4ns左右
        // template <typename signal_key>
        // static constexpr object_id make_signal_id() noexcept // NOLINT
        // {
        //     return object_id{typeid(signal_key).hash_code()};
        // }
        template <typename signal_key>
        struct signal_id_tag
        {
            constexpr static const char instance = 0; // NOLINT
        };
        template <typename signal_key>
        static constexpr object_id make_signal_id() noexcept // NOLINT
        {
            return object_id{std::bit_cast<size_t>(&signal_id_tag<signal_key>::instance)};
        }
        friend struct std::hash<object_id>;

      private:
        size_t value_;
    };

}; // namespace mcs::vulkan::conn

namespace std
{
    template <>
    struct hash<mcs::vulkan::conn::object_id>
    {
        size_t operator()(const mcs::vulkan::conn::object_id &id) const noexcept
        {
            return id.value_;
        }
    };
}; // namespace std