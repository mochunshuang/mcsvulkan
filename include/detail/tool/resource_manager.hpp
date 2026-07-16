#pragma once

#include <array>
#include <cassert>
#include <optional>

namespace mcs::vulkan::tool
{
    template <class resource_type, uint32_t MAX_TEXTURES>
    struct resource_manager
    {
        using slot_type = uint32_t;
        static constexpr slot_type MARK_FREE = 0;
        static constexpr slot_type MARK_USED = 1;
        static constexpr auto invalid_index = (std::numeric_limits<slot_type>::max)();

        // 状态标记：0 空闲，1 已占用
        std::array<slot_type, MAX_TEXTURES> slot_status{};
        // 纹理资源数组
        std::array<resource_type, MAX_TEXTURES> resources;
        std::array<std::string, MAX_TEXTURES> resources_key;

        constexpr void use_slot(slot_type index, resource_type resource,
                                std::string key) noexcept
        {
            mark_used(index);
            resources[index] = std::move(resource);
            resources_key[index] = std::move(key);
        }
        constexpr void free_slot(slot_type index) noexcept
        {
            mark_free(index);
            resources[index] = {};
            resources_key[index] = {};
        }
        [[nodiscard]] constexpr std::optional<slot_type> find_slot_by_name(
            std::string key) const noexcept
        {
            for (slot_type i = 0; i < resources_key.size(); ++i)
            {
                if (resources_key[i] == key)
                    return i;
            }
            return std::nullopt;
        }

        constexpr auto free_count() const noexcept
        {
            return std::ranges::count(slot_status, MARK_FREE);
        }
        constexpr auto used_count() const noexcept
        {
            return std::ranges::count(slot_status, MARK_USED);
        }
        [[nodiscard]] constexpr bool is_free(slot_type index) const noexcept
        {
            return slot_status[index] == MARK_FREE;
        }
        [[nodiscard]] constexpr bool is_used(slot_type index) const noexcept
        {
            return slot_status[index] == MARK_USED;
        }
        constexpr void mark_free(slot_type index) noexcept
        {
            assert(is_used(index) && "该槽已经是空闲的(Double free)");
            slot_status[index] = MARK_FREE;
        }
        constexpr void mark_used(slot_type index) noexcept
        {
            assert(is_free(index) && "该槽已经是空闲的(Double used)");
            slot_status[index] = MARK_USED;
        }
        constexpr auto view_free_indexes() const noexcept
        {
            return slot_status | std::views::enumerate |
                   std::views::filter([](auto tup) noexcept {
                       auto [index, status] = tup;
                       return status == MARK_FREE;
                   }) |
                   std::views::transform([](auto tup) noexcept -> slot_type {
                       auto [index, status] = tup;
                       return index;
                   });
        }
        constexpr auto view_used_indexes() const noexcept
        {
            return slot_status | std::views::enumerate |
                   std::views::filter([](auto tup) noexcept {
                       auto [index, status] = tup;
                       return status == MARK_USED;
                   }) |
                   std::views::transform([](auto tup) noexcept -> slot_type {
                       auto [index, status] = tup;
                       return index;
                   });
        }
        constexpr auto view_used_textures() const noexcept
        {
            return view_used_indexes() |
                   std::views::transform([&](slot_type i) noexcept -> auto {
                       return std::make_pair(i, &resources[i]);
                   });
        }
        struct auto_free_slot_type
        {
          private:
            resource_manager *manager{nullptr};
            slot_type index{invalid_index};

          public:
            constexpr auto valid() const noexcept
            {
                return manager != nullptr;
            }
            constexpr operator bool() const noexcept
            {
                return valid();
            }
            auto_free_slot_type() = default;
            constexpr auto_free_slot_type(resource_manager &mgr, slot_type idx) noexcept
                : manager(&mgr), index(idx)
            {
            }
            constexpr ~auto_free_slot_type() noexcept
            {
                if (manager)
                {
                    manager->free_slot(index);
                    manager = nullptr;
                    index = invalid_index;
                }
            }
            auto_free_slot_type(const auto_free_slot_type &) = delete;
            auto_free_slot_type &operator=(const auto_free_slot_type &) = delete;
            constexpr auto_free_slot_type(auto_free_slot_type &&other) noexcept
                : manager(std::exchange(other.manager, nullptr)),
                  index(std::exchange(other.index, invalid_index))
            {
            }
            constexpr auto_free_slot_type &operator=(auto_free_slot_type &&other) noexcept
            {
                if (this != &other)
                {
                    if (manager)
                        manager->free_slot(index);
                    manager = std::exchange(other.manager, nullptr);
                    index =
                        std::exchange(other.index, std::numeric_limits<slot_type>::max());
                }
                return *this;
            }
        };
    };
}; // namespace mcs::vulkan::tool