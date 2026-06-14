#include <array>
#include <cassert>
#include <functional>
#include <iostream>
#include <exception>
#include <memory>
#include <utility>

// NOLINTBEGIN
template <typename T, typename Fn>
struct lazy_value;

template <typename T>
struct lazy_value<T, std::move_only_function<T()>>
{
    using Fn = std::move_only_function<T()>;
    constexpr operator T()
    {
        std::cout << "std::move_only_function<T()> 隐式转化调用，生成对象\n";
        return this->fn();
    }
    Fn fn;
};

template <typename T>
struct lazy_value<T, std::move_only_function<T() noexcept>>
{
    using Fn = std::move_only_function<T() noexcept>;
    constexpr operator T() noexcept
    {
        return this->fn();
    }
    Fn fn;
};

template <typename T>
auto make_lazy_throw(std::move_only_function<T()> fn) noexcept
{
    return lazy_value<T, std::move_only_function<T()>>{std::move(fn)};
}
template <typename T>
auto make_lazy_noexcept(std::move_only_function<T() noexcept> fn) noexcept
{
    return lazy_value<T, std::move_only_function<T() noexcept>>{std::move(fn)};
}
auto make_lazy_value(auto fn) noexcept(noexcept(fn()))
{
    constexpr bool is_noexcept = noexcept(fn());
    using T = decltype(fn());
    if constexpr (is_noexcept)
        return make_lazy_noexcept<T>(std::move(fn));
    else
        return make_lazy_throw<T>(std::move(fn));
}

struct UniqueID
{
    std::unique_ptr<int> id;
    UniqueID() = default;
    UniqueID(int v) : id(std::make_unique<int>(v)) {}
    UniqueID(UniqueID &&) = default;
    UniqueID &operator=(UniqueID &&) = default;
    void print() const
    {
        std::cout << *id << ' ';
    }
};

struct Store
{
    UniqueID data_store0[2];
    UniqueID data_store1[2];

    std::array<UniqueID *, 2> memory;

    Store()
        : data_store0{}, data_store1{},
          memory{std::array<UniqueID *, 2>{data_store0, data_store1}}
    {
    }

    template <typename Arg>
    auto new_entity(int slot, Arg &&args)
    {
        // 检查用 args 在目标位置构造是否为 noexcept
        constexpr bool is_noexcept_construct =
            noexcept(std::construct_at(memory[0] + slot, std::forward<Arg>(args)));

        if constexpr (is_noexcept_construct)
        {
            std::cout << "分支 1\n";
            // 分支 1：构造不会抛异常 → 直接原地构造，零开销
            for (int i = 0; i < 2; ++i)
            {
                auto *ptr = memory[i] + slot;
                std::construct_at(ptr, std::forward<Arg>(args));
            }
        }
        else
        {
            std::cout << "分支 2\n";
            // 分支 2：构造可能抛异常 → 需要强异常安全保证
            // 要求移动构造必须为 noexcept（否则无法保证）
            static_assert(
                std::is_nothrow_move_constructible_v<UniqueID>,
                "UniqueID must be nothrow move constructible for exception safety");

            std::cout << "  生成目标：temp start\n";

            //NOTE: 下面的是BUG。 。 因为一份的 UniqueID 不能被分成多个
            // 先构造临时对象（可能抛异常）
            // auto &&temp = UniqueID{std::forward<Arg>(args)}; // 如果抛异常，不会影响内存
            auto &&temp = std::forward<Arg>(args); //NOTE: 让隐式转化被调用
            std::cout << "  生成目标：temp end\n";

            // 再用 noexcept 移动将临时对象转移到两个槽位
            for (int i = 0; i < 2; ++i)
            {
                auto *ptr = memory[i] + slot;
                std::construct_at(ptr, std::move(temp)); // noexcept 保证
            }
        }
    }
};

int main()
try
{
    Store s;
    s.new_entity(0, make_lazy_value([]() { return UniqueID{0}; }));
    s.new_entity(1, make_lazy_value([]() noexcept { return UniqueID{1}; }));

    std::cout << "测试：\n";
    assert(*s.data_store0[0].id == 0);
    assert(*s.data_store1[0].id == 0);

    assert(*s.data_store0[1].id == 1);
    assert(*s.data_store1[1].id == 1);

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
} // NOLINTEND