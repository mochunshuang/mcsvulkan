#include <cstdint>
#include <iostream>
#include <meta>

namespace meta = std::meta;

// ------ 反射维护的票号池 ------
template <uint32_t N>
struct IDSlot; // 不完整 = 未占用，完整 = 已占用

struct TicketManager
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

    static consteval void increment()
    {
        meta::define_aggregate(
            meta::substitute(^^IDSlot,
                             {
                                 std::meta::reflect_constant(latest())}),
            {}); // 空聚合体，目的只是让类型“完整”
    }
};

// ------ type_id 主模板：从外部接收 ID ------
template <typename T>
struct type_id; // 不提供默认定义，必须通过 REGISTER_TYPE 特化

// ------ 宏：声明一个类型，并为其分配反射票号 ------
#define REGISTER_TYPE(T)                                            \
    consteval                                                       \
    {                                                               \
        TicketManager::increment();                                 \
    }                                                               \
    template <>                                                     \
    struct type_id<T>                                               \
    {                                                               \
        static constexpr uint32_t id = TicketManager::latest() - 1; \
    }

// ===== 客户端用法 =====
struct A;
struct B;
struct C;

REGISTER_TYPE(A); // id = 0
REGISTER_TYPE(B); // id = 1
REGISTER_TYPE(C); // id = 2

static_assert(type_id<A>::id == 0);
static_assert(type_id<B>::id == 1);
static_assert(type_id<C>::id == 2);

int main()
{
    std::cout << "A: " << type_id<A>::id << '\n';
    std::cout << "B: " << type_id<B>::id << '\n';
    std::cout << "C: " << type_id<C>::id << '\n';

    //NOTE: 下面的不行
    // struct C;
    // REGISTER_TYPE(C);
    // std::cout << "C2: " << type_id<C>::id << '\n';
    return 0;
}