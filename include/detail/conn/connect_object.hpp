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
        // 单端清理。延迟释放共享连接
        constexpr void clearConnection() noexcept
        {
            as_rcvr_destroy();
            as_sndr_destroy();
        }
        connect_object() = default;
        constexpr ~connect_object() noexcept
        {
            clearConnection();
        }
        //NOTE: 组合的时候，连接可能需要重置+重建。这个时候旧连接可能被当前挂载的宿主使用
        connect_object(connect_object &&) = delete;
        connect_object &operator=(connect_object &&) = delete;

        connect_object(const connect_object &) = delete;
        connect_object &operator=(const connect_object &) = delete;

        //--------------------------------static function---------------------------
        template <typename signal_key, typename... Args>
            requires(valid_signal_args<signal_key, Args...>)
        constexpr void emit(Args... args)
        {
            if (signal_slot_map.empty())
                return;
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

        // -------- 3 参数版本：继承风格（保持原有调用点不变）--------
        template <typename signal_key, std::derived_from<connect_object> Rcvr,
                  typename slot_type>
            requires(valid_traits_slot<traits_slot<slot_type>> &&
                     valid_signal<signal_key> && signal_slot_match<signal_key, slot_type>)
        constexpr static auto connect(connect_object *sndr, Rcvr *recr,
                                      slot_type slot) noexcept -> connect_ptr *
        {
            return connect<signal_key>(sndr, recr, static_cast<connect_object *>(recr),
                                       std::move(slot));
        }

        // -------- 4 参数版本：组合风格（新增，唯一真正实现）--------
        template <typename signal_key, typename Rcvr, typename slot_type>
            requires(valid_traits_slot<traits_slot<slot_type>> &&
                     valid_signal<signal_key> && signal_slot_match<signal_key, slot_type>)
        constexpr static auto connect(connect_object *sndr_hub, Rcvr *recr,
                                      connect_object *rcvr_hub, slot_type slot) noexcept
            -> connect_ptr *
        {
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
                rcvr_hub->as_rcvr().connect_sndr(shared);
                sndr_hub->as_sndr().connect_rcvr(object_id::make_signal_id<signal_key>(),
                                                 shared);
                return shared;
            }
            catch (...)
            {
                sndr_hub->as_sndr().unsafe_remove_by_sndr(
                    object_id::make_signal_id<signal_key>(), shared);
                rcvr_hub->as_rcvr().unsafe_remove_by_rcvr(shared);
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

      private:
        constexpr connect_ptr *find_existing(object_id sig, const void *tag, // NOLINT
                                             const void *recvr) noexcept
        {
            auto it = signal_slot_map.find(sig);
            if (it == signal_slot_map.end())
                return nullptr;
            for (connect_ptr *p : it->second)
            {
                if (not p->rcvr_hold()) // 孤儿：slot_ 已删，跳过
                    continue;
                if (p->slot()->matches(tag, recvr))
                    return p;
            }
            return nullptr;
        }

        constexpr void erase_matching(object_id sig, const void *tag, // NOLINT
                                      const void *recvr,
                                      connect_object *rcvr_hub) noexcept
        {
            auto it = signal_slot_map.find(sig);
            if (it == signal_slot_map.end())
                return;

            // 1. 先收集匹配指针，避免迭代中修改 vec
            std::vector<connect_ptr *> victims;
            for (connect_ptr *p : it->second)
            {
                if (not p->rcvr_hold())
                    continue; // 孤儿交给过期清理
                if (p->slot()->matches(tag, recvr))
                    victims.push_back(p);
            }

            // 2. 逐个按现有两条路径清
            for (connect_ptr *p : victims)
            {
                rcvr_hub->as_rcvr().disconnect_rcvr(p); // rcvr 侧 erase + 2→1
                disconnect_sndr(sig, p);                // sndr 侧 erase + 1→0
            }
        }

      public:
        /*
        // 追加（默认，对齐 Qt）
        connect<Sig>(&sndr, &recvr, &rcvr_hub, slot);

        // 去重（对齐 Qt::UniqueConnection）
        connect_unique<Sig>(&sndr, &recvr, &rcvr_hub, slot);

        // 替换全部（对齐 disconnect + connect 的糖）
        connect_replace<Sig>(&sndr, &recvr, &rcvr_hub, slot);
        */
        // 去重：四元组命中 → 返回旧句柄，新 lambda 不构造
        template <typename signal_key, typename Rcvr, typename slot_type>
            requires(valid_traits_slot<traits_slot<slot_type>> &&
                     valid_signal<signal_key> && signal_slot_match<signal_key, slot_type>)
        constexpr static auto connect_unique(connect_object *sndr_hub, // NOLINT
                                             Rcvr *recr, connect_object *rcvr_hub,
                                             slot_type slot) noexcept -> connect_ptr *
        {
            const void *tag =
                &object_id::signal_id_tag<slot_impl<Rcvr, slot_type>>::instance;
            if (auto *p = sndr_hub->as_sndr().find_existing(
                    object_id::make_signal_id<signal_key>(), tag, recr))
                return p;
            return connect<signal_key>(sndr_hub, recr, rcvr_hub, std::move(slot));
        }

        // 替换：四元组下全部清掉，只留新建的 1 个
        template <typename signal_key, typename Rcvr, typename slot_type>
            requires(valid_traits_slot<traits_slot<slot_type>> &&
                     valid_signal<signal_key> && signal_slot_match<signal_key, slot_type>)
        constexpr static auto connect_replace(connect_object *sndr_hub, // NOLINT
                                              Rcvr *recr, connect_object *rcvr_hub,
                                              slot_type slot) noexcept -> connect_ptr *
        {
            const void *tag =
                &object_id::signal_id_tag<slot_impl<Rcvr, slot_type>>::instance;
            sndr_hub->as_sndr().erase_matching(object_id::make_signal_id<signal_key>(),
                                               tag, recr, rcvr_hub);
            return connect<signal_key>(sndr_hub, recr, rcvr_hub, std::move(slot));
        }
    };

}; // namespace mcs::vulkan::conn