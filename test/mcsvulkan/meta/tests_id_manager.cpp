#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <exception>
#include <memory>
#include <vector>

// NOLINTBEGIN

struct point
{
    int x;
    int y;
};

template <typename Component, size_t N = 4096>
struct ComponentManager
{
    using id_type = uint32_t;
    using component_type = Component;
    static constexpr auto chunk_size = N;

    class ComponentIDPool
    {
        std::array<uint32_t, chunk_size> ids{};
        size_t available_id{};

        //NOTE: 永远保证末尾的可以分配
        constexpr auto allocate(id_type id) noexcept
        {
            ids[available_id++] = id;
        }
        constexpr auto release(id_type release_id) noexcept
        {
            auto last_id = available_id - 1;

            ids[release_id] = ids[last_id];
            ids[last_id] = {};

            --available_id;
        }
        constexpr auto is_full() noexcept
        {
            return available_id == chunk_size;
        }
    };
    std::vector<std::unique_ptr<ComponentIDPool>> componentIDGenerator;
    std::vector<component_type> components;

    auto newComponent()
    {
        int select_id_pool = -1;
        for (int i = 0; componentIDGenerator.size(); ++i)
        {
            if (not componentIDGenerator[i]->is_full())
            {
                select_id_pool = i;
                break;
            }
        }
        if (select_id_pool == -1)
        {
            // new chunk_size to allocate
        }
    }
};

// NOLINTEND

int main()
try
{
    std::cout << "main done\n";
    return 0;
}
catch (const std::exception &e)
{
    std::cerr << "Exception: " << e.what() << '\n';
    return 1;
}
catch (...)
{
    std::cerr << "Unknown exception\n";
    return 1;
}