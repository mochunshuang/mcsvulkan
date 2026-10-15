#include <cassert>
#include <iostream>
#include <exception>
#include <functional>
#include <string>
#include <tuple>
#include <type_traits>
// NOLINTBEGIN
#include "../../../include/detail/__meta.hpp"

using mcs::vulkan::meta::make_aggregate;
using mcs::vulkan::meta::field;
using mcs::vulkan::meta::method;
using mcs::vulkan::meta::static_string;
using mcs::vulkan::meta::name_spec;
using mcs::vulkan::meta::class_utils;

constexpr auto call(std::function_ref<int(int)> f)
{
    return f(10);
}
constexpr auto call2(std::function_ref<double(double)> f)
{
    return f(10);
}

struct handler
{
    using return_type = int;

    static auto make_handle(auto fn, void *value)
    {
        return [](void *, void *) {
        };
    }

    void set_value(return_type v) noexcept
    {
        value = v;
    }
    void *ref_value() noexcept
    {
        return &value;
    }
    return_type value;
};

// ---------------------------------
static consteval std::optional<std::meta::info> find_function(std::meta::info cls,
                                                              static_string name)
{
    return class_utils::find_member(
        cls, name,
        [](std::meta::info m) -> bool {
            return std::meta::is_function(m) || std::meta::is_function_template(m);
        },
        std::meta::access_context::current());
}
static consteval std::optional<std::meta::info> find_nonstatic_data_member(
    std::meta::info cls, static_string name)
{
    return class_utils::find_member(
        cls, name,
        [](std::meta::info m) -> bool { return std::meta::is_nonstatic_data_member(m); },
        std::meta::access_context::current());
}

template <static_string fn_name, class T>
static consteval std::meta::info resolve_callable()
{
    if (auto f = find_function(^^T, fn_name); f.has_value())
        return *f;
    if (auto d = find_nonstatic_data_member(^^T, fn_name); d.has_value())
        return *d;
    return std::meta::info{};
}

template <static_string fn_name, class Obj, class... Args>
static constexpr bool can_noexcept_invoke() noexcept
{
    using T = std::remove_cvref_t<Obj>;

    if constexpr (requires(T &o, Args &&...as) {
                      { o.template invoke<fn_name>(std::forward<Args>(as)...) } noexcept;
                  })
        return true;

    constexpr auto callable = resolve_callable<fn_name, T>();
    if constexpr (callable == std::meta::info{})
        return false;
    else
        return requires(T &o, Args &&...as) {
            { o.[:callable:](std::forward<Args>(as)...) } noexcept;
        } || requires(T &o, Args &&...as) {
            { o.template[:callable:](std::forward<Args>(as)...) } noexcept;
        };
}

template <static_string fn_name, class Obj, class... Args>
    requires(can_noexcept_invoke<fn_name, std::remove_cvref_t<Obj>, Args...>())
static constexpr decltype(auto) invoke_member(Obj &obj, Args &&...args) noexcept
{
    using T = std::remove_cvref_t<Obj>;
    constexpr auto m = resolve_callable<fn_name, T>();

    if constexpr (requires { obj.[:m:](std::forward<Args>(args)...); })
        return obj.[:m:](std::forward<Args>(args)...);
    else if constexpr (requires { obj.template[:m:](std::forward<Args>(args)...); })
        return obj.template[:m:](std::forward<Args>(args)...);
    else
        static_assert(sizeof(Obj) == 0, "invoke_member: member exists but not callable");
}

enum class submit_flag : std::uint8_t
{
    lose,
    win,
};

using declare_interface = void (*)(void *obj, submit_flag flag, void *ctx,
                                   static_string declare_fn) noexcept;
template <typename Arena, class Obj>
constexpr auto make_declare_adapter() noexcept -> declare_interface
{
    return [](void *p, submit_flag flag, void *ctx, static_string fn) noexcept {
        constexpr auto name = static_string{"declare"};

        auto *arena = static_cast<Arena *>(p);
        auto *obj = static_cast<Obj *>(ctx);
        if constexpr (requires() {
                          arena->template invoke<name>(submit_flag::win, obj, fn);
                      })
            arena->template invoke<name>(submit_flag::win, obj, std::move(fn));
        else
            invoke_member<name>(*arena, submit_flag::win, obj, fn);
    };
}

enum class resolve_result : std::uint8_t
{
    rejection,
    accept
};

struct submit_function
{
    using resolve_interface = void (*)(resolve_result result, void *obj,
                                       static_string fn) noexcept;

    template <class Obj>
    auto build() noexcept -> resolve_interface
    {
        return [](resolve_result result, void *ptr, static_string fn) noexcept {
            constexpr auto name = static_string{"resolve"};
            auto *obj = static_cast<Obj *>(ptr);
            if constexpr (requires() { obj->template invoke<name>(result, fn); })
                obj->template invoke<name>(result, fn);
            else
                invoke_member<name>(*obj, result, fn);
        };
    }
    submit_function() = default;

    void resolve(submit_flag win)
    {
        //
    }

  private:
    void *recognizer{};
    static_string fn{};
};

struct arena_members
{
};

#include <chrono>
using Clock = std::chrono::steady_clock;
using time_point = Clock::time_point;

template <typename... Ts>
struct filter_types
{
    template <typename T>
    static constexpr bool has_type()
    {
        return (std::is_same_v<T, Ts> || ...);
    };
};
template <class Self, class Data>
consteval bool requires_type_from_self()
{
    using FT = std::remove_pointer_t<decltype(std::declval<Self>().filter_types())>;
    return FT::template has_type<Data>();
}

void test_filter_types()
{
    constexpr auto recognizer = make_aggregate<"recognizer">(method<"return_data">(
        []<typename Self, typename Data>(Self &&self, Data d) noexcept
            requires(requires_type_from_self<Self, Data>())
        {
            // do somathing

            return d;
        }),
        method<"filter_types">([]() consteval noexcept {
            return static_cast<filter_types<int, double> *>(nullptr);
        }));
    static_assert(recognizer.template invoke<"return_data">(int{1}) == 1);
}

int main()
try
{
    test_filter_types();
    // NOTE: 测试no_unique_address 和 成员地址
    {
        struct test_address
        {
            [[no_unique_address]] struct
            {
            } add;
            [[no_unique_address]] struct
            {
            } value;
        };
        constexpr test_address t{};
        static_assert(static_cast<const void *>(&t.add) ==
                      static_cast<const void *>(&t.value));
        static_assert(static_cast<const void *>(&t) ==
                      static_cast<const void *>(&t.value));
    }
    // NOTE: function_ref 可以和lambda一起使用
    {
        constexpr auto generic_lambda = [](auto x) {
            return x + 1;
        };
        assert(call(generic_lambda) == 11);
        static_assert(std::is_same_v<int, decltype(call(generic_lambda))>);

        assert(call2(generic_lambda) == double{11});
        static_assert(std::is_same_v<double, decltype(call2(generic_lambda))>);
    }
    {
        auto receiver = make_aggregate<"GlfwReceiver">(
            method<"input_event">([](auto e) noexcept { return e + 1; }));

        assert(call2(receiver.input_event) == double{11});
        static_assert(std::is_same_v<double, decltype(call2(receiver.input_event))>);
    }
    {

        handler h{};
        void *data = h.ref_value();
        void *fn;
    }

    auto arena = make_aggregate<"arena">(
        method<"input_event">([](auto e) noexcept { return e + 1; }),
        method<"declare">([]<typename Self>(Self &self, submit_flag result, auto *ctx,
                                            static_string fn) noexcept {
            switch (result)
            {
            case submit_flag::win: /* 赢 */
            {
                if constexpr (requires() { ctx->pending_flag; })
                {
                    constexpr auto name = static_string{"resolve_pending"};
                    if constexpr (requires() {
                                      {
                                          ctx->template invoke<name>(result, fn)
                                      } noexcept -> std::same_as<bool>;
                                  })
                    {
                        bool pending = ctx->template invoke<name>(result, fn);
                        if (not pending)
                        {
                        }
                    }
                    else
                    {
                        bool pending = invoke_member<name>(*ctx, result, fn);
                        if (not pending)
                        {
                        }
                    }
                }
            }
            break;
            case submit_flag::lose: /* 输 */
                break;
            }
        }),
        method<"return_value">([](auto *sndr, auto e) noexcept { return e + 1; }),
        method<"on_tick">([](time_point point) noexcept {
            // do something
        }));

    // NOTE: 结果走 arena 的唯一出口? 没必要. 直接 resume recognizer 关联的函数即可
    constexpr auto null_submit_ptr = static_cast<declare_interface>(nullptr);
    auto recognizer = make_aggregate<"recognizer">(
        field<"arena_ptr">(static_cast<void *>(nullptr)),
        field<"submit_click">(null_submit_ptr),
        field<"submit_double_click">(null_submit_ptr),
        method<"set_arena">(
            []<typename Self, typename Arena>(Self &self, Arena &arena) noexcept {
                self.arena_ptr = &arena;
                self.submit_click = make_declare_adapter<Arena, Self>();
                self.submit_double_click = make_declare_adapter<Arena, Self>();
            }),
        method<"click">([](auto &self, auto e) noexcept {
            self.submit_click(self.arena_ptr, submit_flag::win, &self, "click");
        }),
        method<"double_click">([](auto &self, auto e) noexcept {
            self.submit_double_click(self.arena_ptr, submit_flag::win, &self,
                                     "double_click");
        }),
        method<"resolve">([](auto &self, auto e) noexcept {
            self.submit_double_click(self.arena_ptr, submit_flag::win, &self,
                                     "double_click");
        }),
        method<"resolve_pending">([](auto &self, auto e) noexcept {
            self.submit_double_click(self.arena_ptr, submit_flag::win, &self,
                                     "double_click");
        }));

    recognizer.template invoke<"set_arena">(arena);
    // TODO: BUG 的来源.  反射 T 和 反射 T的基类的不一样的
    // invoke_member<"set_arena">(recognizer, arena);

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