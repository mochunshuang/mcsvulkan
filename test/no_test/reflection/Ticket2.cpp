#include <cstdint>
#include <iostream>
#include <meta>

namespace meta = std::meta;

// ------ 反射维护的票号池 ------
template <uint32_t N>
struct IDSlot; // 不完整 = 未占用，完整 = 已占用

struct IdManager
{

    static consteval uint32_t latest()
    {
        uint32_t k = 0;
        while (meta::is_complete_type(
            meta::substitute(^^IDSlot, {
                                           std::meta::reflect_constant(k)})))
            ++k;

        return k;
    }
};
constexpr auto idgen = ^^IDSlot;
consteval
{
    meta::define_aggregate(
        meta::substitute(idgen, {std::meta::reflect_constant(IdManager::latest())}), {});
}

constexpr auto lambda = [] consteval {
    meta::define_aggregate(
        meta::substitute(idgen, {std::meta::reflect_constant(IdManager::latest())}), {});
};

static_assert(IdManager::latest() == 1);

template <uint32_t i>
struct type_id
{
    static constexpr auto id = i;
};

template <uint32_t i>
constexpr uint32_t type_id_v = type_id<i>::id;

static consteval uint32_t next_type_id()
{
    uint32_t k = 0;
    while (meta::is_complete_type(
        meta::substitute(^^type_id, {
                                        std::meta::reflect_constant(k)})))
        ++k;

    return k;
}

template <typename T>
struct get_type_id
{
    constexpr static auto id = type_id_v<next_type_id()>;
};
template <typename T>
constexpr uint32_t get_type_id_v = get_type_id<T>::id;

//NOTE: 会死循环，因为用永远为真、只有声明才有用
// static_assert(get_type_id<int>::id == 0);
// static_assert(get_type_id<double>::id == 1);

int main()
{
    std::cout << "main done\n";
    return 0;
}