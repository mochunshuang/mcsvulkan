#include <memory>
#include <print>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// NOLINTBEGIN
// ═══════════════════════════════════════════════════════════════════════
// 类型标签：每个 T 一个唯一地址
// ═══════════════════════════════════════════════════════════════════════
template <typename T>
struct type_token
{
    static constexpr char v = 0;
};
template <typename T>
inline constexpr const void *type_of = &type_token<T>::v;

template <typename... Ts>
struct type_list
{
};

// ═══════════════════════════════════════════════════════════════════════
// 擦除句柄：void* + tag
//   data 指向被引用对象本身，tag = type_of<T&&>（折叠后已编码 cv + 值类别）
// ═══════════════════════════════════════════════════════════════════════
struct erased_ref
{
    void *data = nullptr;
    const void *tag = nullptr;
    explicit operator bool() const noexcept
    {
        return tag != nullptr;
    }
};

template <typename T>
[[nodiscard]] erased_ref erase(T &&t) noexcept
{
    return erased_ref{const_cast<void *>(static_cast<const void *>(std::addressof(t))),
                      type_of<T &&>};
}

// ═══════════════════════════════════════════════════════════════════════
// dynamic_pointer：运行时值类别路由
// ═══════════════════════════════════════════════════════════════════════
struct dynamic_pointer
{
    using Fn = void (*)(void *ptr, erased_ref ref) noexcept;

    template <class Agg, typename... Ts>
    static constexpr dynamic_pointer make(type_list<Ts...>) noexcept
    {
        return dynamic_pointer{[](void *ptr, erased_ref ref) noexcept {
            auto *agg = static_cast<std::decay_t<Agg> *>(ptr);
            auto try_one = [&]<typename T>() noexcept {
                using U = std::remove_reference_t<T>;
                if (ref.tag == type_of<T>)
                    agg->runtime_invoke(static_cast<T>(*static_cast<U *>(ref.data)));
            };
            (try_one.template operator()<Ts>(), ...);
        }};
    }

    constexpr void dispatch(void *ptr, auto &&p) noexcept
    {
        fn(ptr, erase(std::forward<decltype(p)>(p)));
    }

    dynamic_pointer() = delete;

  private:
    Fn fn;
    explicit constexpr dynamic_pointer(Fn f) noexcept : fn(f) {}
};

// ═══════════════════════════════════════════════════════════════════════
// 1) Tracker
// ═══════════════════════════════════════════════════════════════════════
struct Tracker
{
    std::string name;
    Tracker(std::string n) : name(std::move(n)) {}
    Tracker(const Tracker &o) : name(o.name)
    {
        std::println("      [COPY] {}", name);
    }
    Tracker(Tracker &&o) noexcept : name(std::move(o.name))
    {
        std::println("      [MOVE] {}", name);
    }
};

struct handler_tracker
{
    std::vector<Tracker> sink;
    handler_tracker()
    {
        sink.reserve(8);
    }

    void runtime_invoke(Tracker &v)
    {
        std::println("  hit Tracker&");
        sink.push_back(v);
    }
    void runtime_invoke(const Tracker &v)
    {
        std::println("  hit const Tracker&");
        sink.push_back(v);
    }
    void runtime_invoke(Tracker &&v)
    {
        std::println("  hit Tracker&&");
        sink.push_back(std::move(v));
    }
};

// ═══════════════════════════════════════════════════════════════════════
// 2) unique_ptr
// ═══════════════════════════════════════════════════════════════════════
struct handler_uptr
{
    std::vector<std::unique_ptr<int>> sink;
    void runtime_invoke(std::unique_ptr<int> &)
    {
        std::println("  hit unique_ptr&       (alias)");
    }
    void runtime_invoke(const std::unique_ptr<int> &)
    {
        std::println("  hit const unique_ptr& (alias)");
    }
    void runtime_invoke(std::unique_ptr<int> &&v)
    {
        std::println("  hit unique_ptr&&      (steal)");
        sink.push_back(std::move(v));
    }
};

// ═══════════════════════════════════════════════════════════════════════
// 3) shared_ptr
// ═══════════════════════════════════════════════════════════════════════
struct handler_sptr
{
    std::vector<std::shared_ptr<int>> sink;
    void runtime_invoke(std::shared_ptr<int> &v)
    {
        std::println("  hit shared_ptr&   uc_before={}", v.use_count());
        sink.push_back(v);
        std::println("                    uc_after ={}", v.use_count());
    }
    void runtime_invoke(std::shared_ptr<int> &&v)
    {
        std::println("  hit shared_ptr&&  uc_before={}", v.use_count());
        sink.push_back(std::move(v));
        std::println("                    uc_after ={}", v.use_count());
    }
};

// ═══════════════════════════════════════════════════════════════════════
// 4) primitives
// ═══════════════════════════════════════════════════════════════════════
struct handler_prim
{
    void runtime_invoke(int &v)
    {
        std::println("  int&        = {}", v);
    }
    void runtime_invoke(int &&v)
    {
        std::println("  int&&       = {}", v);
    }
    void runtime_invoke(const int &v)
    {
        std::println("  const int&  = {}", v);
    }
    void runtime_invoke(double &&v)
    {
        std::println("  double&&    = {}", v);
    }
    void runtime_invoke(int *&p)
    {
        std::println("  int*&       = {}", static_cast<void *>(p));
    }
};

// ═══════════════════════════════════════════════════════════════════════
// 5) 自定义结构体（非智能指针）—— 移动构造把源对象掏空，肉眼可验
// ═══════════════════════════════════════════════════════════════════════
struct Point
{
    int x = 0, y = 0;
    Point(int x_, int y_) : x(x_), y(y_) {}
    Point(const Point &o) : x(o.x), y(o.y)
    {
        std::println("      [COPY] Point({},{})", x, y);
    }
    Point(Point &&o) noexcept : x(o.x), y(o.y)
    {
        std::println("      [MOVE] Point({},{})", x, y);
        o.x = o.y = -1; // 掏空证据
    }
};

struct handler_point
{
    std::vector<Point> sink;
    handler_point()
    {
        sink.reserve(8);
    }

    void runtime_invoke(Point &p)
    {
        std::println("  hit Point&       ({},{})", p.x, p.y);
        sink.push_back(p);
    }
    void runtime_invoke(const Point &p)
    {
        std::println("  hit const Point& ({},{})", p.x, p.y);
        sink.push_back(p);
    }
    void runtime_invoke(Point &&p)
    {
        std::println("  hit Point&&      ({},{})", p.x, p.y);
        sink.push_back(std::move(p));
    }
};

// ═══════════════════════════════════════════════════════════════════════
int main()
{
    {
        std::println("== 1. Tracker ==");
        using supported = type_list<Tracker &, const Tracker &, Tracker &&>;
        handler_tracker h;
        auto dp = dynamic_pointer::make<handler_tracker>(supported{});

        Tracker lv{"L"};
        const Tracker clv{"C"};

        dp.dispatch(&h, lv);
        dp.dispatch(&h, clv);
        dp.dispatch(&h, Tracker{"R"});
        dp.dispatch(&h, std::move(lv));
        std::println("sink.size={}", h.sink.size());
    }

    {
        std::println("\n== 2. unique_ptr (move-only) ==");
        using supported = type_list<std::unique_ptr<int> &, const std::unique_ptr<int> &,
                                    std::unique_ptr<int> &&>;
        handler_uptr h;
        auto dp = dynamic_pointer::make<handler_uptr>(supported{});

        std::unique_ptr<int> p = std::make_unique<int>(7);
        dp.dispatch(&h, p);
        dp.dispatch(&h, std::move(p));
        std::println("p is null? {}", (p == nullptr));
    }

    {
        std::println("\n== 3. shared_ptr (use_count) ==");
        using supported = type_list<std::shared_ptr<int> &, std::shared_ptr<int> &&>;
        handler_sptr h;
        auto dp = dynamic_pointer::make<handler_sptr>(supported{});

        auto sp = std::make_shared<int>(42);
        std::println("init uc={}", sp.use_count());
        dp.dispatch(&h, sp);
        dp.dispatch(&h, std::move(sp));
        std::println("after  uc={} (moved-out)", sp.use_count());
    }

    {
        std::println("\n== 4. primitives / pointer ==");
        using supported = type_list<int &, int &&, const int &, double &&, int *&>;
        handler_prim h;
        auto dp = dynamic_pointer::make<handler_prim>(supported{});

        int x = 42;
        const int cx = 7;
        int *px = &x;

        dp.dispatch(&h, x);
        dp.dispatch(&h, 100);
        dp.dispatch(&h, std::move(x));
        dp.dispatch(&h, cx);
        dp.dispatch(&h, 1.5);
        dp.dispatch(&h, px);

        std::println("x = {} (int&& 是平凡类型，没被掏空)", x);
    }

    {
        std::println("\n== 5. 自定义结构体 Point ==");
        using supported = type_list<Point &, const Point &, Point &&>;
        handler_point h;
        auto dp = dynamic_pointer::make<handler_point>(supported{});

        Point p{3, 4};
        const Point cp{5, 6};
        Point movable{7, 8};

        dp.dispatch(&h, p);                  // 左值       → Point&        → COPY
        dp.dispatch(&h, cp);                 // const 左值 → const Point&  → COPY
        dp.dispatch(&h, Point{1, 2});        // prvalue    → Point&&       → MOVE
        dp.dispatch(&h, std::move(movable)); // xvalue     → Point&&       → MOVE

        std::println("movable after move: ({},{})   ← -1 是被掏空的证据", movable.x,
                     movable.y);
        std::println("sink.size={}", h.sink.size());
    }
} // NOLINTEND