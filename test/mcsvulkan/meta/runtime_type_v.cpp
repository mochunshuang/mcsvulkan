#include <cassert>
#include <print>
#include <type_traits>
#include <typeinfo>
#include <utility>

// ---------- 1. 类型标签 ----------
template <typename T>
struct type_token
{
    static constexpr char v = 0;
};

template <typename T>
inline constexpr const void *type_of = &type_token<T>::v;

// ---------- 2. 原理：不同 T 的标签地址必然不同（编译期）----------
static_assert(type_of<int> != type_of<double>);
static_assert(type_of<int> != type_of<const int>);
static_assert(type_of<int> != type_of<int &>);
static_assert(type_of<int &> != type_of<int &&>);
static_assert(type_of<int &> != type_of<const int &>);
static_assert(type_of<int> != type_of<int *>);
static_assert(type_of<int *> != type_of<const int *>);

// ---------- 3. 运行时按 tag 还原 T，并按 T 的值类别转发 ----------
// 契约：tag == type_of<T>，payload 指向 T 本体
template <typename F>
void dispatch(const void *tag, void *payload, F &&f)
{
    if (tag == type_of<int>)
        std::forward<F>(f)(*static_cast<int *>(payload));
    else if (tag == type_of<const int>)
        std::forward<F>(f)(*static_cast<const int *>(payload));
    else if (tag == type_of<double>)
        std::forward<F>(f)(*static_cast<double *>(payload));
    else if (tag == type_of<int *>)
        std::forward<F>(f)(*static_cast<int **>(payload));
    else if (tag == type_of<const int *>)
        std::forward<F>(f)(*static_cast<const int **>(payload));
    else if (tag == type_of<int &>)
        std::forward<F>(f)(*static_cast<int *>(payload));
    else if (tag == type_of<int &&>)
        std::forward<F>(f)(std::move(*static_cast<int *>(payload)));
    else if (tag == type_of<const int &>)
        std::forward<F>(f)(*static_cast<const int *>(payload));
    else
        std::println("[unknown tag] {}", tag);
}

int main()
{
    // handler 是泛型 lambda：body 必须对所有可能 T 都合法
    // 因此这里不能写 T-特定的 static_assert，只能写对所有 T 都成立的代码
    auto show = [](auto &&v) {
        using T = std::remove_reference_t<decltype(v)>;
        constexpr const char *cat = std::is_lvalue_reference_v<decltype(v)>   ? "lvalue"
                                    : std::is_rvalue_reference_v<decltype(v)> ? "rvalue"
                                                                              : "value";

        if constexpr (std::is_pointer_v<T>)
            std::println("  T={:<4} cat={:<6} ptr={}", typeid(T).name(), cat,
                         static_cast<const void *>(v));
        else
            std::println("  T={:<4} cat={:<6} value={}", typeid(T).name(), cat, v);
    };

    int x = 42;
    const int cx = 7;
    double d = 1.5;
    int *px = &x;
    const int *pcx = &cx;

    dispatch(type_of<int>, &x, show);
    dispatch(type_of<const int>, const_cast<int *>(&cx), show);
    dispatch(type_of<double>, &d, show);
    dispatch(type_of<int *>, &px, show);
    dispatch(type_of<const int *>, &pcx, show);
    dispatch(type_of<int &>, &x, show);
    dispatch(type_of<int &&>, &x, show);
    dispatch(type_of<const int &>, const_cast<int *>(&cx), show);
}