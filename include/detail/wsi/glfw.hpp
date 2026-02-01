#pragma once

#include <cstddef>
#include <format>
#include <string_view>

#include <vector>
#include "../__vulkan.hpp"

#include "../utils/mcslog.hpp"

#include "./glfw_event_mapping.hpp"

#include "../event/keyboard_event_dispatcher.hpp"
#include "../event/mousebutton_event_dispatcher.hpp"
#include "../event/scroll_event_dispatcher.hpp"
#include "../event/cursor_pos_event_dispatcher.hpp"
#include "../event/cursor_enter_event_dispatcher.hpp"
#include "../event/distribute.hpp"

namespace mcs::vulkan::wsi::glfw
{

    class Size
    {
      public:
        int width;
        int height;
    };
    class Position
    {
      public:
        int x;
        int y;
    };
    class Geometry
    {
      public:
        Size size;
        Position position;
    };
    class GlfwException : public std::runtime_error
    {
      public:
        explicit GlfwException(std::string_view msg, int errorCode = 0)
            : std::runtime_error(std::format("[GLFW Error: {}]: {}", errorCode, msg))
        {
        }
    };

    class Window
    {
      private:
        static constexpr void setupGlfw()
        {
            if (::glfwInit() != GLFW_TRUE)
            {
                const char *description = nullptr;
                int code = ::glfwGetError(&description);
                throw GlfwException(description, code);
            }
        }
        static constexpr void teardownGlfw() noexcept
        {
            ::glfwTerminate();
        }

      public:
        using window_type = ::GLFWwindow;
        using window_pointer = window_type *;

        constexpr Window()
        {
            setupGlfw();
        }
        Window(const Window &) = delete;
        Window(Window &&) = delete;
        Window &operator=(const Window &) = delete;
        Window &operator=(Window &&) = delete;
        constexpr ~Window() noexcept
        {
            destroy();
        }

        void setup(Size size, const char *title, ::GLFWmonitor *monitor = nullptr,
                   ::GLFWwindow *share = nullptr)
        {

            setup(size.width, size.height, title, monitor, share);
        }

        void setup(int width, int height, const char *title, ::GLFWmonitor *monitor,
                   ::GLFWwindow *share) noexcept
        {
            width_ = width;
            height_ = height;

            ::glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            ::glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

            window_ = ::glfwCreateWindow(width, height, title, monitor, share);
            toCenter();

            // set context value
            ::glfwSetWindowUserPointer(window_, this);

            // In main function, register callbacks:
            ::glfwSetWindowPosCallback(window_, &windowPosCallback);
            ::glfwSetWindowSizeCallback(window_, &windowSizeCallback);
            ::glfwSetWindowFocusCallback(window_, &windowFocusCallback);
            ::glfwSetWindowIconifyCallback(window_, &windowIconifyCallback);
            ::glfwSetWindowMaximizeCallback(window_, &windowMaximizeCallback);
            ::glfwSetKeyCallback(window_, &keyCallback);
            ::glfwSetCursorPosCallback(window_, &cursorPosCallback);
            ::glfwSetMouseButtonCallback(window_, &mouseButtonCallback);
            ::glfwSetScrollCallback(window_, &scrollCallback);
            ::glfwSetCursorEnterCallback(window_, &cursorEnterCallback);
            ::glfwSetFramebufferSizeCallback(window_, &framebufferResizeCallback);
        }
        void teardown() noexcept
        {
            destroy();
        }

        //
        void toCenter()
        {
            const GLFWvidmode *mode = ::glfwGetVideoMode(::glfwGetPrimaryMonitor());
            int centeredX = (mode->width - width_) / 2;
            int centeredY = (mode->height - height_) / 2;
            ::glfwSetWindowPos(window_, centeredX, centeredY);
            x_ = centeredX;
            y_ = centeredY;
        }

        int shouldClose() noexcept
        {
            return ::glfwWindowShouldClose(window_);
        }
        static void pollEvents() noexcept
        {
            ::glfwPollEvents();
        }
        void waitGoodFramebufferSize()
        {
            int width, height; // NOLINT
            ::glfwGetFramebufferSize(window_, &width, &height);
            while (width == 0 || height == 0)
            {
                ::glfwGetFramebufferSize(window_, &width, &height);
                ::glfwWaitEvents();
            }
        }

        [[nodiscard]] window_pointer data() const noexcept
        {
            return window_;
        }
        [[nodiscard]] window_pointer data() noexcept
        {
            return window_;
        }
        [[nodiscard]] auto getFramebufferSize() const noexcept
        {
            int width, height; // NOLINT
            glfwGetFramebufferSize(window_, &width, &height);
            return VkExtent2D(width, height);
        }

        [[nodiscard]] bool framebufferResized() const noexcept
        {
            return framebufferResized_;
        }
        [[nodiscard]] auto &refFramebufferResized() noexcept
        {
            return framebufferResized_;
        }

      private:
        window_pointer window_ = nullptr;
        bool framebufferResized_{};

        // 只保存窗口模式时的位置和大小，用于切换回窗口模式
        int width_{};
        int height_{};
        int x_{};
        int y_{};
        bool isFullscreen_ = false;

        void destroy() noexcept
        {
            if (window_ != nullptr)
            {
                ::glfwDestroyWindow(window_);
                window_ = nullptr;
            }
            teardownGlfw();
        }

        static void framebufferResizeCallback(GLFWwindow *window, int /*width*/,
                                              int /*height*/) noexcept
        {
            auto *app = static_cast<Window *>(::glfwGetWindowUserPointer(window));
            app->framebufferResized_ = true;
        }
        static void windowPosCallback(GLFWwindow *window, int xpos, int ypos)
        {
            auto *self = static_cast<Window *>(::glfwGetWindowUserPointer(window));
            auto &isFullscreen = self->isFullscreen_;
            auto &x_ = self->x_;
            auto &y_ = self->y_;
            if (!isFullscreen)
            {
                x_ = xpos;
                y_ = ypos;
                MCSLOG_INFO("Pos change to ({},{})", x_, y_);
            }
        }

        // NOLINTBEGIN
        // Callback for keyboard input
        static void keyCallback(GLFWwindow *window, int key, int scancode, int action,
                                int mods)
        {
            auto *self = static_cast<Window *>(::glfwGetWindowUserPointer(window));
            auto &isFullscreen = self->isFullscreen_;
            auto &x_ = self->x_;
            auto &y_ = self->y_;
            auto &width_ = self->width_;
            auto &height_ = self->height_;

            switch (key)
            {
            case GLFW_KEY_ESCAPE:
                ::glfwSetWindowShouldClose(window, GLFW_TRUE);
                // std::cout << "按下了 ESC 键，退出程序" << std::endl; //乱码
                MCSLOG_DEBUG("按下了 ESC 键，退出程序");
                break;

            case GLFW_KEY_F1:
                isFullscreen = !isFullscreen;
                if (isFullscreen)
                {
                    // 保存窗口化时的位置和大小
                    ::glfwGetWindowPos(window, &x_, &y_);
                    ::glfwGetWindowSize(window, &width_, &height_);

                    // 切换到全屏
                    auto primaryMonitor = ::glfwGetPrimaryMonitor();
                    const GLFWvidmode *mode = ::glfwGetVideoMode(primaryMonitor);
                    ::glfwSetWindowMonitor(window, primaryMonitor, 0, 0, mode->width,
                                           mode->height, mode->refreshRate);
                }
                else
                    // 切换回窗口模式
                    ::glfwSetWindowMonitor(window, nullptr, x_, y_, width_, height_,
                                           GLFW_DONT_CARE);
                break;

            default:
                event::distribute<event::keyboard_event_dispatcher>(
                    {.key = input::mappingKey(key),
                     .action = input::mappingAction(action),
                     .modifier_key = event::ModifierKey(mods),
                     .scancode = scancode});
                break;
            }
        }

        // Callback for mouse buttons
        static void mouseButtonCallback(GLFWwindow *window, int button, int action,
                                        int mods)
        {
            // 使用新的映射函数
            event::distribute<event::mousebutton_event_dispatcher>(
                {.button = input::mappingMouseButton(button),
                 .action = input::mappingAction(action),
                 .modifier_key = event::ModifierKey(mods)});
        }
        static void scrollCallback(GLFWwindow *window, double xoffset, double yoffset)
        {
            event::distribute<event::scroll_event_dispatcher>(
                {.xoffset = xoffset, .yoffset = yoffset});
        }
        static void cursorPosCallback(GLFWwindow * /*window*/, double xpos, double ypos)
        {
            event::distribute<event::cursor_pos_event_dispatcher>(
                {.xpos = xpos, .ypos = ypos});
        }

        static void cursorEnterCallback(GLFWwindow *window, int entered)
        {
            event::distribute<event::cursor_enter_event_dispatcher>(
                {.value = entered != 0});
        }

        // NOLINTEND

        //------------defalt print
        // 窗口大小回调
        static void windowSizeCallback(GLFWwindow * /*window*/, int width, int height)
        {
            MCSLOG_INFO("windowSize: {} x {}", width, height);
        }

        // 窗口焦点回调
        static void windowFocusCallback(GLFWwindow * /*window*/, int focused)
        {
            MCSLOG_INFO("windowFocus: {}.",
                        ((focused != 0) ? "get focus" : "lost focus"));
        }

        // 窗口最小化/最大化回调
        static void windowIconifyCallback(GLFWwindow * /*window*/, int iconified)
        {

            MCSLOG_INFO("iconified: {}.", (iconified != 0));
        }
        static void windowMaximizeCallback(GLFWwindow * /*window*/, int maximized)
        {
            MCSLOG_INFO("maximized: {}.", (maximized != 0));
        }

        // -------------------------vulkan api-------------------------
      public:
        static void addRequiredExtension(std::vector<const char *> &required_extensions)
        {
            uint32_t glfw_extension_count{0};
            const char **names =
                ::glfwGetRequiredInstanceExtensions(&glfw_extension_count);
            required_extensions.append_range(
                std::vector<const char *>{names, names + glfw_extension_count});
        }
        static auto requiredVulkanInstanceExtensions()
        {
            uint32_t glfw_extension_count{0};
            const char **names =
                ::glfwGetRequiredInstanceExtensions(&glfw_extension_count);
            return std::vector<const char *>{names, names + glfw_extension_count};
        }

        constexpr VkSurfaceKHR createVkSurfaceKHR(VkInstance &instance) const
        {
            VkSurfaceKHR surface; // NOLINT
            if (::glfwCreateWindowSurface(instance, data(), nullptr, &surface) !=
                VK_SUCCESS)
                throw std::runtime_error("failed to create window surface!");
            return surface;
        }
        [[nodiscard]] VkExtent2D chooseSwapExtent(
            const VkSurfaceCapabilitiesKHR &capabilities) const noexcept
        {
            if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
                return capabilities.currentExtent;

            auto [width, height] = getFramebufferSize();
            return {std::clamp<uint32_t>(width, capabilities.minImageExtent.width,
                                         capabilities.maxImageExtent.width),
                    std::clamp<uint32_t>(height, capabilities.minImageExtent.height,
                                         capabilities.maxImageExtent.height)};
        }
    };

}; // namespace mcs::vulkan::wsi::glfw