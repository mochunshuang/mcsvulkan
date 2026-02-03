#include <bit>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <type_traits>
#include <utility>

struct move_only
{
    std::unique_ptr<int> value;
    double d;
};

template <typename T>
auto make_array(T &&t)
{
    return move_only{std::move(t)};
}

template <typename T>
struct wrap_move_only
{
    using type = move_only;
};

template <typename T>
struct move_only_proxy
{
    T ptr;

    operator move_only() && noexcept
    {
        return move_only{std::move(ptr)};
    }
};

auto make_array6(move_only &&args) -> move_only
{
    return move_only{std::move(args)};
}

auto make_array7(move_only_proxy<move_only> args) -> move_only
{
    move_only tem = std::move(args);
    return move_only{std::move(tem)};
}
template <typename T>
auto make_array8(std::initializer_list<T> args) -> move_only
{
    move_only_proxy<move_only> tem = std::move(args);
    return make_array7(std::move(tem));
}

template <typename T>
auto make_array9(move_only_proxy<T> args) -> move_only
{
    move_only tem = std::move(args);
    return move_only{std::move(tem)};
}

int main()
{
    {
        make_array(move_only{std::make_unique<int>(1)});
    }

    {
        auto value = move_only_proxy<move_only>{{std::make_unique<int>(1)}};
        move_only b = std::move(value);
    }

    {
        make_array6({std::make_unique<int>(1)});
    }
    {
        make_array7({std::make_unique<int>(1)});
    }
    {
        // make_array8({std::make_unique<int>(1), .d = 0});
    }
    {
        // make_array9({std::make_unique<int>(1), .d = 0}); //NOTE: 不可能
    }
    std::cout << "main done\n";
    return 0;
}