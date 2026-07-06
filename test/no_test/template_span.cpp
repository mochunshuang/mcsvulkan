#include <span>
#include <cstddef>

template <std::span<const int> s>
struct test
{
};

template <auto span>
struct test2
{
};

constexpr int arr[] = {1, 2, 3};
constexpr std::span<const int> my_span{arr};

static constexpr int arr2[] = {1, 2, 3};
static constexpr std::span<const int> my_span2{arr2};

template <typename T>
struct ConstSpan
{
    T *data;
    std::size_t size;
};

int main()
{
    // test<my_span> t; // 若编译通过，则支持

    // test2<my_span> t;

    // test<my_span2> t;
    // test2<my_span2> t;

    //NOTE: 碰到指针，必须 static 存储的才行，下面的不行
    // constexpr int arr[] = {1, 2, 3};
    // constexpr ConstSpan<const int> my_span{arr, 3};
    // template <ConstSpan<const int> sp>
    // struct Test
    // {
    // };
    // Test<my_span> t; // ✅

    return 0;
}