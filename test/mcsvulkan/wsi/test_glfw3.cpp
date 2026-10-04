#include <cassert>
#include <concepts>
#include <cstdint>
#include <exception>
#include <iostream>
#include <print>
#include <chrono>
#include <span>
#include <tuple>
#include <type_traits>
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

int main()
try
{

    surface window{};
    window.setup({.width = WIDTH, .height = HEIGHT}, TITLE); // NOLINT
    // auto input = glfw_input{};

    auto forward = glfw_forward{};
    hardware_layer_input h{forward};

    struct receiver
    {
        void runtime_invoke(position2d_event pos,
                            std::chrono::steady_clock::time_point time_point,
                            keyboard_event e) noexcept
        {
            std::println("onKeyboard cursor: {}", e);
        }
    };
    receiver some_receiver;

    struct not_receiver
    {
    };
    not_receiver not_a_receiver;

    auto agg_receiver = make_aggregate<"TextBox">(
        field<"text">(std::string("CD")),
        method<"runtime_invoke">(
            []<class E>(auto &&self, position2d_event pos,
                        std::chrono::steady_clock::time_point time_point, E e) noexcept
            //NOTE: 只有 keyboard_event 被转发
            // requires(std::same_as<E, keyboard_event>)
            {
                std::println("runtime_invoke: {} , {} ,{}", pos,
                             time_point.time_since_epoch(), e);
            }));
    {
        auto agg_receiver = make_aggregate<"TextBox">(
            field<"text">(std::string("CD")),
            method<"runtime_invoke">(
                []<class E>(auto &&self, position2d_event pos,
                            std::chrono::steady_clock::time_point time_point,
                            E e) noexcept
                    requires(std::same_as<E, keyboard_event>)
                {
                    std::println("runtime_invoke: {} , {} ,{}", pos,
                                 time_point.time_since_epoch(), e);
                }));
        agg_receiver.template invoke<"runtime_invoke">(
            position2d_event{}, std::chrono::steady_clock::time_point{},
            keyboard_event{});
        // NOTE: 下面编译错误
        // agg_receiver.template invoke<"runtime_invoke">(
        //     position2d_event{}, std::chrono::steady_clock::time_point{},
        //     mousebutton_event{});
    }

    // h.setReceiver(&some_receiver);
    // h.setReceiver(&not_a_receiver);
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