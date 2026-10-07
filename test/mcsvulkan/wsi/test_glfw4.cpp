#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <print>
#include <chrono>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "head.hpp"

// NOLINTBEGIN
using surface = mcs::vulkan::wsi::glfw::Window;

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;
constexpr auto TITLE = "test_my_triangle";

using mcs::vulkan::input::glfw_input;
using mcs::vulkan::input::glfw_window_state;
using mcs::vulkan::input::glfw_forward;

using mcs::vulkan::event::keyboard_event;
using mcs::vulkan::event::scroll_event;
using mcs::vulkan::event::mousebutton_event;
using mcs::vulkan::event::position2d_event;
using mcs::vulkan::event::cursor_enter_event;

using mcs::vulkan::event::Key;
using mcs::vulkan::event::MouseButtons;
using mcs::vulkan::event::ModifierKey;
using mcs::vulkan::event::Action;

using mcs::vulkan::meta::make_aggregate;
using mcs::vulkan::meta::field;
using mcs::vulkan::meta::method;
using mcs::vulkan::meta::static_string;
using mcs::vulkan::meta::name_spec;

using mcs::vulkan::runtime_type_v;

template <typename Obj, typename P>
concept can_invoke = requires(Obj *ptr, position2d_event pos,
                              std::chrono::steady_clock::time_point time_point, P p) {
    requires requires {
        { ptr->template invoke<"runtime_invoke">(pos, time_point, p) } noexcept;
    } || requires {
        { ptr->runtime_invoke(pos, time_point, p) } noexcept;
    };
};

struct dynamic_pointer
{
    using Fn = void (*)(void *ptr, position2d_event pos,
                        std::chrono::steady_clock::time_point tp, const void *type,
                        void *p) noexcept;

    template <class Dispach, class Agg>
    static constexpr dynamic_pointer make() noexcept
    {
        return dynamic_pointer{[](void *ptr, position2d_event pos,
                                  std::chrono::steady_clock::time_point tp,
                                  const void *type, void *p) noexcept {
            auto *agg = static_cast<std::decay_t<Agg> *>(ptr);
            Dispach::dispach(agg, pos, tp, type, p);
        }};
    }

    template <typename P>
        requires(!std::is_reference_v<P> && std::is_same_v<P, std::remove_cv_t<P>>)
    constexpr void dispach(position2d_event pos,
                           std::chrono::steady_clock::time_point time_point, void *ptr,
                           P p) noexcept
    {
        void *volatile vp = &p; // volatile 指针，阻止编译器追踪其指向对象的类型
        fn(ptr, pos, time_point, runtime_type_v<P>, vp);
    }

    dynamic_pointer() = default;

  private:
    Fn fn{};
    explicit constexpr dynamic_pointer(Fn f) noexcept : fn(f) {}
};

struct hardware_layer_input
{
    using enable_forword = std::tuple<keyboard_event, mousebutton_event>;

    template <typename Obj, typename... Ts>
    static constexpr void runtime_invoke(Obj *ptr, position2d_event pos,
                                         std::chrono::steady_clock::time_point time_point,
                                         const void *type, void *p) noexcept
    {
        auto invoke = [&](auto &e) noexcept {
            if constexpr (requires() {
                              ptr->template invoke<"runtime_invoke">(pos, time_point, e);
                          })
                ptr->template invoke<"runtime_invoke">(pos, time_point, e);
            else if constexpr (requires() { ptr->runtime_invoke(pos, time_point, e); })
                ptr->runtime_invoke(pos, time_point, e);
        };
        ((can_invoke<Obj, Ts> && type == runtime_type_v<Ts> &&
          (invoke(*static_cast<Ts *>(p)), true)) ||
         ...);
    }
    template <typename Obj>
    static constexpr void dispach(Obj *ptr, position2d_event pos,
                                  std::chrono::steady_clock::time_point time_point,
                                  const void *type, void *p) noexcept
    {
        [&]<typename... Ts>(std::tuple<Ts...>) {
            runtime_invoke<Obj, Ts...>(ptr, pos, time_point, type, p);
        }(enable_forword{});
    }

    constexpr explicit hardware_layer_input(glfw_forward &input) : input_{input}
    {
        input_.subscribe<glfw_forward::keyboard_change_fn>(
            this, hardware_layer_input::onKeyboard);
        input_.subscribe<glfw_forward::mousebutton_change_fn>(
            this, hardware_layer_input::onMousebutton);
    }
    constexpr ~hardware_layer_input() noexcept
    {
        input_.unsubscribe<glfw_forward::keyboard_change_fn>(
            this, hardware_layer_input::onKeyboard);
        input_.unsubscribe<glfw_forward::mousebutton_change_fn>(
            this, hardware_layer_input::onMousebutton);
    }
    hardware_layer_input(const hardware_layer_input &) = delete;
    hardware_layer_input(hardware_layer_input &&) = delete;
    hardware_layer_input &operator=(const hardware_layer_input &) = delete;
    hardware_layer_input &operator=(hardware_layer_input &&) = delete;

    static void onKeyboard(void *self, position2d_event pos,
                           std::chrono::steady_clock::time_point time_point,
                           keyboard_event e) noexcept
    {
        static_cast<hardware_layer_input *>(self)->forword(pos, time_point, e);
    }
    static void onMousebutton(void *self, position2d_event pos,
                              std::chrono::steady_clock::time_point time_point,
                              mousebutton_event e) noexcept
    {
        static_cast<hardware_layer_input *>(self)->forword(pos, time_point, e);
    }

    constexpr void forword(position2d_event pos,
                           std::chrono::steady_clock::time_point time_point,
                           auto e) noexcept
    {
        if (receiver_ == nullptr)
            return;
        fn_.dispach(pos, time_point, receiver_, e);
    }

    template <typename Obj>
    constexpr void setReceiver(Obj *obj) noexcept
    {
        receiver_ = obj;
        fn_ = dynamic_pointer::make<hardware_layer_input, Obj>();
    }

  private:
    void *receiver_{};
    dynamic_pointer fn_;
    glfw_forward &input_;
};

// NOTE: 识别器
struct recognize
{
};
// NOTE:
struct arena
{
};

struct ui_gen
{

    //

    struct padding_result
    {
    };

    struct recognize_item
    {

        ui_gen *reciver;
    };

    void setResult(padding_result result)
    {
        // NOTE:
    }

    padding_result padding_result;
    recognize_item root;
};

void test_future()
{
    using clock = std::chrono::steady_clock;

    auto start = clock::now();
    auto deadline = start + std::chrono::seconds(2);

    while (clock::now() < deadline)
    {
        // 还没到未来那个点
        // 做点事，或者稍微睡一下
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // get future: 2009 ms
    // 到这里说明已经到达或超过 deadline
    std::println(
        "get future: {} ms",
        std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - start)
            .count());
}

template <class Agg, name_spec... Info>
struct result_awaiter
{
    using result_variant = std::variant<std::monostate, typename[:Info.info:]...>;

    static consteval auto unique_type()
    {
        constexpr std::array<name_spec, sizeof...(Info)> input{Info...};
        for (std::size_t i = 0; i < input.size(); ++i)
        {
            auto spec_i = input[i];
            for (std::size_t j = i + 1; j < input.size(); ++j)
            {
                auto spec_j = input[j];
                if (spec_i.info == spec_j.info)
                    return false;
            }
        }
        return true;
    };

    using IndexStoreType = uint8_t;
    struct empty_type
    {
        constexpr empty_type(IndexStoreType) {}
    };
    static_assert(sizeof(empty_type) == 1);
    using IndexType = std::conditional_t<unique_type(), empty_type, IndexStoreType>;
    static constexpr IndexType invalid_idx =
        static_cast<IndexType>(std::numeric_limits<IndexStoreType>::max());
    static_assert(sizeof...(Info) < std::numeric_limits<IndexStoreType>::max());

    struct Dispach
    {
        template <static_string fn_name, class T>
        void invoke(void *obj, T &&value) noexcept
            requires(requires() {
                {
                    static_cast<Agg *>(obj)->template invoke<fn_name>(
                        std::forward<T>(value))
                } noexcept;
            })
        {
            static_cast<Agg *>(obj)->template invoke<fn_name>(std::forward<T>(value));
        }
    };
    constexpr void resume() noexcept
    {
        if constexpr (unique_type())
        {
            auto index = result.index();
            if (index == 0 || obj == nullptr)
                return;
            template for (constexpr auto I : std::views::iota(0u, sizeof...(Info)))
            {
                constexpr name_spec spec = Info...[I];
                if (I == index - 1)
                {
                    dispach.template invoke<spec.name>(
                        obj, std::move(std::get<I + 1>(result)));
                    result = std::monostate{};
                    return;
                }
            }
        }
        else
        {
            if (idx == invalid_idx || obj == nullptr || result.index() == 0)
                return;
            template for (constexpr auto I :
                          std::views::indices(sizeof...(Info) + 1) | std::views::drop(1))
            {
                constexpr name_spec spec = Info...[I - 1];
                if (I == idx)
                {
                    dispach.template invoke<spec.name>(obj,
                                                       std::move(std::get<I>(result)));

                    result = std::monostate{};
                    idx = ~0;
                    return;
                }
            }
        }
    }
    void *obj;
    [[no_unique_address]] IndexType idx{invalid_idx};
    [[no_unique_address]] Dispach dispach{};
    [[no_unique_address]] result_variant result{};

    constexpr explicit result_awaiter(Agg &p) noexcept : obj{&p} {}

    template <typename T>
    constexpr void setResult(T &&value) noexcept(
        noexcept(result = std::forward<T>(value)))
        requires(requires() { result = std::forward<T>(value); })
    {
        result = std::forward<T>(value);
    }

    template <std::size_t I, typename T>
    constexpr void setResult(T &&value) noexcept(
        noexcept(result.template emplace<I>(std::forward<T>(value))))
        requires requires { result.template emplace<I>(std::forward<T>(value)); }
    {
        result.template emplace<I>(std::forward<T>(value));
        idx = static_cast<IndexType>(I);
    }
};

void test_result_awaiter()
{
    using result_variant = std::variant<int, double, std::string>;
    auto agg_receiver = make_aggregate<"TextBox">(
        field<"result_variant">(result_variant{double{}}),
        method<"runtime_invoke">([]<class E>(auto &&self, E e) noexcept {
            self.result_variant.template emplace<E>(e);
        }),
        method<"invoke_int">([](auto &&self, int e) noexcept {
            self.result_variant.template emplace<int>(e);
        }));
    assert(agg_receiver.result_variant.index() == 1);

    // ===== 唯一类型分支 =====
    using AwaiterUnique =
        result_awaiter<decltype(agg_receiver), {"runtime_invoke", ^^double},
                       {"invoke_int", ^^int}, {"runtime_invoke", ^^std::string}>;
    static_assert(AwaiterUnique::unique_type());

    // 基线：只用 obj + 同 variant
    struct BaselineUnique
    {
        void *obj;
        typename AwaiterUnique::result_variant result;
    };
    // empty_type idx + 空 Dispach 都被 [[no_unique_address]] 吸收
    static_assert(sizeof(AwaiterUnique) == sizeof(BaselineUnique));

    AwaiterUnique awaiter{agg_receiver};

    awaiter.setResult(int{0});
    awaiter.resume();
    assert(agg_receiver.result_variant.index() == 0);

    awaiter.setResult(double{0});
    awaiter.resume();
    assert(agg_receiver.result_variant.index() == 1);

    awaiter.setResult(std::string{});
    awaiter.resume();
    assert(agg_receiver.result_variant.index() == 2);

    // ===== 非唯一类型分支（^^int 出现两次） =====
    using AwaiterNonUnique =
        result_awaiter<decltype(agg_receiver), {"runtime_invoke", ^^double},
                       {"invoke_int", ^^int}, {"invoke_int", ^^int}>;
    static_assert(!AwaiterNonUnique::unique_type());

    // 基线：obj + 它自己的 variant
    struct BaselineNonUnique
    {
        void *obj;
        typename AwaiterNonUnique::result_variant result;
    };
    // idx 是 size_t，不能被吸收 → 大出至少一个 size_t
    static_assert(sizeof(AwaiterNonUnique) > sizeof(BaselineNonUnique));

    AwaiterNonUnique awaiter2{agg_receiver};

    // Info...[1] = {"invoke_int", ^^int} → invoke<"invoke_int">(int)
    awaiter2.setResult<2>(int{42});
    awaiter2.resume();
    assert(agg_receiver.result_variant.index() == 0);

    // Info...[2] = {"invoke_int", ^^int} → 同样派发 invoke<"invoke_int">(int)
    awaiter2.setResult<3>(int{99});
    awaiter2.resume();
    assert(agg_receiver.result_variant.index() == 0);

    // idx 已被 resume 重置为 ~0 → 再次 resume 直接 return，不崩
    awaiter2.resume();
    assert(agg_receiver.result_variant.index() == 0);
}

void test()
{
    using result_variant = std::variant<int, double, std::string>;
    {
        constexpr result_variant a{int{2}};
        constexpr result_variant b{double{2}};
        result_variant c{std::string{""}};
        static_assert(a.index() == 0);
        static_assert(b.index() == 1);
        assert(c.index() == 2);
    }
    {
        auto agg_receiver = make_aggregate<"TextBox">(method<"runtime_invoke">(
            []<class E>(auto &&self, E e) noexcept { std::println("agg_receiver"); }));
        using R = decltype(agg_receiver);
        void *obj = &agg_receiver;
        auto a = [](void *obj) {
            static_cast<R *>(obj)->template invoke<"runtime_invoke">(int{});
        };
        a(obj);
    }
    {
    }
}

int main()
try
{
    test_result_awaiter();
    test();
    test_future();

    surface window{};
    window.setup({.width = WIDTH, .height = HEIGHT}, TITLE); // NOLINT
    // auto input = glfw_input{};

    auto forward = glfw_forward{};
    hardware_layer_input h{forward};

    auto agg_receiver = make_aggregate<"TextBox">(
        field<"time_point">(std::optional<std::chrono::steady_clock::time_point>{}),
        method<"runtime_invoke">(
            []<class E>(auto &&self, position2d_event pos,
                        std::chrono::steady_clock::time_point time_point, E e) noexcept {
                std::println("runtime_invoke: {} , {} ,{}", pos,
                             time_point.time_since_epoch(), e);
                using namespace std::chrono;
                using namespace std::chrono_literals; // C++14 起可用
                if (not self.time_point)
                    self.time_point = time_point;

                // 计算时间差，得到 duration
                auto elapsed = time_point - *self.time_point;
                auto ms = duration_cast<milliseconds>(elapsed).count();
                if (ms > 1000)
                {
                    std::println("elapsed: {}", ms);
                }
                self.time_point = time_point;
            }));
    h.setReceiver(&agg_receiver);

    while (window.shouldClose() == 0)
    {

        surface::pollEvents();
    }

    std::cout << "main done\n";
    return 0;
}
catch (std::exception &e)
{
    std::println("main catch exception: {}", e.what());
} // NOLINTEND