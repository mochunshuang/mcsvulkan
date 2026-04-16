#pragma once

#include "object_id.hpp"
#include "signal_slot_match.hpp"
#include "slot_impl.hpp"
#include "slot_interface.hpp"
#include "traits_slot.hpp"
#include "valid_signal.hpp"
#include "valid_traits_slot.hpp"

#include "connect_ptr.hpp"

#include <cassert>

#include <unordered_map>

#include <utility>
#include <vector>

namespace mcs::vulkan::conn
{

    struct connect_object
    {
        using map_type = std::unordered_map<object_id, std::vector<connect_ptr *>>;
        using connect_ptr = conn::connect_ptr;

      private:
        //--------------------------------rcvr--------------------------------------------
        constexpr bool connect_sndr(connect_ptr *ptr) // NOLINT
        {
            slots.emplace_back(ptr);
            return true;
        };
        constexpr void unsafe_remove_by_rcvr(connect_ptr *ptr) // NOLINT
        {
            std::erase_if(
                slots, [&](connect_ptr *item) constexpr noexcept { return ptr == item; });
        }
        constexpr void as_rcvr_destroy() noexcept // NOLINT
        {
            for (connect_ptr *shared : slots)
                shared->release();
            slots.clear();
        }
        constexpr void disconnect_rcvr(connect_ptr *ptr) // NOLINT
        {
            assert(ptr->rcvr_hold());
            [[maybe_unused]] auto count = std::erase_if(
                slots, [&](connect_ptr *item) constexpr noexcept { return ptr == item; });
            assert(count == 1);
            ptr->release();
        }

        std::vector<connect_ptr *> slots; // NOLINT
        //--------------------------------rcvr end----------------------------------------

        //--------------------------------sndr--------------------------------------------

        constexpr bool connect_rcvr(object_id signal_key, connect_ptr *ptr) // NOLINT
        {
            signal_slot_map[signal_key].emplace_back(ptr);
            return true;
        };
        // NOLINTNEXTLINE
        constexpr void unsafe_remove_by_sndr(object_id signal_key, connect_ptr *ptr)
        {
            std::erase_if(
                signal_slot_map[signal_key],
                [&](connect_ptr *item) constexpr noexcept { return ptr == item; });
        }
        constexpr void as_sndr_destroy() noexcept // NOLINT
        {
            for (auto &[_, shareds] : signal_slot_map)
            {
                for (connect_ptr *shared : shareds)
                    shared->release();
                shareds.clear();
            }
            signal_slot_map.clear();
        }
        constexpr void as_sndr_remove_expired_slot() // NOLINT
        {
            for (auto &[_, shareds] : signal_slot_map)
            {
                std::erase_if(shareds, [&](connect_ptr *shared) constexpr noexcept {
                    if (not shared->rcvr_hold()) // find expired
                    {
                        shared->release();
                        return true;
                    }
                    return false;
                });
            }
        }
        constexpr void disconnect_sndr(object_id signal_key, connect_ptr *ptr) // NOLINT
        {
            assert(not ptr->rcvr_hold());
            [[maybe_unused]] auto count = std::erase_if(
                signal_slot_map[signal_key],
                [&](connect_ptr *item) constexpr noexcept { return ptr == item; });
            assert(count == 1);
            ptr->release();
        }

        map_type signal_slot_map; // NOLINT
        //--------------------------------sndr end----------------------------------------

        constexpr auto &as_sndr() noexcept // NOLINT
        {
            return *this;
        }
        constexpr auto &as_rcvr() noexcept // NOLINT
        {
            return *this;
        }

      public:
        connect_object() = default;
        constexpr ~connect_object() noexcept
        {
            as_rcvr_destroy();
            as_sndr_destroy();
        }
        connect_object(connect_object &&) = default;
        connect_object &operator=(connect_object &&) = default;

        connect_object(const connect_object &) = delete;
        connect_object &operator=(const connect_object &) = delete;

        //--------------------------------static function---------------------------
        template <typename signal_key, typename... Args>
            requires(valid_signal_args<signal_key, Args...>)
        constexpr void emit(Args... args)
        {
            auto it = signal_slot_map.find(object_id::make_signal_id<signal_key>());
            if (it != signal_slot_map.end())
            {
                bool has_expired{};
                for (connect_ptr *shared : it->second)
                {
                    if (not shared->rcvr_hold())
                    {
                        has_expired = true;
                        continue;
                    }
                    shared->invoke(args...);
                }
                if (has_expired)
                    as_sndr().as_sndr_remove_expired_slot();
            }
        }
        template <typename signal_key, std::derived_from<connect_object> Rcvr,
                  typename slot_type>
            requires(valid_traits_slot<traits_slot<slot_type>> &&
                     valid_signal<signal_key> && signal_slot_match<signal_key, slot_type>)
        static constexpr auto slot_impl_string(connect_object *, // NOLINT
                                               Rcvr * /*recr*/,
                                               slot_type /*slot*/) noexcept
        {
            return detail::slot_function_type_string(slot_impl<Rcvr, slot_type>::id_type);
        }

        template <typename signal_key, std::derived_from<connect_object> Rcvr,
                  typename slot_type>
            requires(valid_traits_slot<traits_slot<slot_type>> &&
                     valid_signal<signal_key> && signal_slot_match<signal_key, slot_type>)
        constexpr static auto connect(connect_object *sndr, Rcvr *recr,
                                      slot_type slot) noexcept -> connect_ptr *
        {
            // NOTE: 多线程需要保持线程安全
            slot_interface *s =
                new (std::nothrow) slot_impl<Rcvr, slot_type>{recr, std::move(slot)};
            if (s == nullptr)
                return nullptr;

            auto *shared = new (std::nothrow) connect_ptr{s};
            if (shared == nullptr)
            {
                delete s;
                return nullptr;
            }

            try
            {
                static_cast<connect_object *>(recr)->as_rcvr().connect_sndr(shared);
                sndr->as_sndr().connect_rcvr(object_id::make_signal_id<signal_key>(),
                                             shared);
                return shared;
            }
            catch (...)
            {
                sndr->as_sndr().unsafe_remove_by_sndr(
                    object_id::make_signal_id<signal_key>(), shared);
                static_cast<connect_object *>(recr)->as_rcvr().unsafe_remove_by_rcvr(
                    shared);
                delete s;
                delete shared;
                return nullptr;
            }
        }

        template <typename signal_key> // NOLINTNEXTLINE
        constexpr static auto disconnect(connect_object *sndr, connect_object *recr,
                                         connect_ptr *slot)
        {
            assert(slot->rcvr_hold());
            recr->as_rcvr().disconnect_rcvr(slot);
            assert(not slot->rcvr_hold());
            sndr->as_sndr().disconnect_sndr(object_id::make_signal_id<signal_key>(),
                                            slot);
        }
    };

}; // namespace mcs::vulkan::conn