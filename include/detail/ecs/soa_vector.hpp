#pragma once
#include "gen_soa_vector.hpp"

namespace mcs::vulkan::ecs
{

    template <typename T, template <typename> class Alloc = std::allocator,
              template <typename, template <typename> class> class Make = soa_memory>
    struct soa_vector : public gen_soa_vector<T, Alloc, Make>
    {
        using base_type = gen_soa_vector<T, Alloc, Make>;
        using base_type::base_type; // 继承构造函数

        // 隐藏基类的 allocate（返回 optional），提供自动扩容版本
        constexpr size_type allocate()
        {
            if (this->free_size() == 0)
            {
                size_type new_cap = (this->capacity() == 0) ? 4 : this->capacity() * 2;
                this->reserve(new_cap); // 可能抛出异常（如 bad_alloc）
            }
            auto opt = base_type::allocate();
            assert(opt.has_value()); // 容量已确保，不可能失败
            return *opt;
        }

        // 可选：提供 try_allocate，返回 optional 版本
        constexpr std::optional<size_type> try_allocate() noexcept
        {
            try
            {
                return allocate();
            }
            catch (...)
            {
                return std::nullopt;
            }
        }

        // 自动扩容版本的 make_soa_value，返回 proxy_value 而非 optional
        template <typename... Args>
        constexpr proxy_value<soa_vector> make_soa_value(size_type id, Args &&...args)
            requires(requires() { this->construct_at(id, std::forward<Args>(args)...); })
        {
            this->construct_at(id, std::forward<Args>(args)...);
            return proxy_value<soa_vector>(*this, id);
        }
        template <typename... Args>
        constexpr proxy_value<soa_vector> make_soa_value(Args &&...args)
        {
            return make_soa_value(allocate(), std::forward<Args>(args)...);
        }

        // 可选：try_make_soa_value，返回 optional<proxy_value>
        template <typename... Args>
        constexpr std::optional<proxy_value<soa_vector>> try_make_soa_value(
            Args &&...args) noexcept
        {
            try
            {
                return make_soa_value(std::forward<Args>(args)...);
            }
            catch (...)
            {
                return std::nullopt;
            }
        }
    };

}; // namespace mcs::vulkan::ecs