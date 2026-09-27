#pragma once

#include <cstddef>
#include <format>
#include <functional>
#include <string_view>

#include <utility>
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

#include "../event/char_event_dispatcher.hpp"
#include "../event/drop_event_dispatcher.hpp"
#include "../event/window_focus_event_dispatcher.hpp"
#include "../event/window_pos_event_dispatcher.hpp"
#include "../event/window_size_event_dispatcher.hpp"
#include "../event/window_iconify_event_dispatcher.hpp"
#include "../event/window_maximize_event_dispatcher.hpp"
#include "../event/framebuffer_size_event_dispatcher.hpp"

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
        constexpr explicit GlfwException(std::string_view msg, int errorCode = 0)
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

        constexpr void setup(Size size, const char *title,
                             ::GLFWmonitor *monitor = nullptr,
                             ::GLFWwindow *share = nullptr)
        {

            setup(size.width, size.height, title, monitor, share);
        }

        constexpr void setup(int width, int height, const char *title,
                             ::GLFWmonitor *monitor, ::GLFWwindow *share) noexcept
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

            ::glfwSetCharCallback(window_, &charCallback);
            ::glfwSetDropCallback(window_, &dropCallback);
        }
        constexpr void teardown() noexcept
        {
            destroy();
        }

        //
        constexpr void toCenter()
        {
            const GLFWvidmode *mode = ::glfwGetVideoMode(::glfwGetPrimaryMonitor());
            int centeredX = (mode->width - width_) / 2;
            int centeredY = (mode->height - height_) / 2;
            ::glfwSetWindowPos(window_, centeredX, centeredY);
            x_ = centeredX;
            y_ = centeredY;
        }

        constexpr int shouldClose() noexcept
        {
            return ::glfwWindowShouldClose(window_);
        }
        constexpr static void pollEvents() noexcept
        {
            ::glfwPollEvents();
        }
        constexpr void waitGoodFramebufferSize() const
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

        using ContentScaleCallbackType =
            std::function<void(void *self, float xscale, float yscale)>;
        void enableContentScaleCallback(ContentScaleCallbackType fun) noexcept
        {
            contentScaleback_ = std::move(fun);
            glfwSetWindowContentScaleCallback(window_, &onContentScaleChange);
        }
        void disableContentScaleCallback() const noexcept
        {
            glfwSetWindowContentScaleCallback(window_, nullptr);
        }
        [[nodiscard]] auto getContentScale() const noexcept
        {
            struct scale_factor
            {
                float xscale;
                float yscale;
            };
            scale_factor ret; // NOLINT
            glfwGetWindowContentScale(window_, &ret.xscale, &ret.yscale);
            return ret;
        }
        [[nodiscard]] auto &reContentScaleChange() noexcept
        {
            return contentScaleChange_;
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

        // ontentScale
        bool contentScaleChange_ = false;
        ContentScaleCallbackType contentScaleback_;
        static void onContentScaleChange(GLFWwindow *window, float xscale,
                                         float yscale) noexcept
        {
            auto *app = static_cast<Window *>(::glfwGetWindowUserPointer(window));
            app->contentScaleChange_ = true; //NOTE: 暂时保留。 yoga 相关的需要
            app->contentScaleback_(app, xscale, yscale); //NOTE: 存函数该替换为 scale
        }

        void destroy() noexcept
        {
            if (window_ != nullptr)
            {
                ::glfwDestroyWindow(window_);
                window_ = nullptr;
            }
            teardownGlfw();
        }

        static void framebufferResizeCallback(GLFWwindow *window, int width,
                                              int height) noexcept
        {
            auto *app = static_cast<Window *>(::glfwGetWindowUserPointer(window));
            app->framebufferResized_ = true;
            event::distribute<event::framebuffer_size_event_dispatcher>(
                {.width = width, .height = height});
        }
        static void windowPosCallback(GLFWwindow *window, int xpos, int ypos)
        {
            auto *self = static_cast<Window *>(::glfwGetWindowUserPointer(window));
            if (!self->isFullscreen_)
            {
                self->x_ = xpos;
                self->y_ = ypos;
            }
            event::distribute<event::window_pos_event_dispatcher>({.x = xpos, .y = ypos});
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
            event::distribute<event::window_size_event_dispatcher>(
                {.width = width, .height = height});
        }

        // 窗口焦点回调
        static void windowFocusCallback(GLFWwindow * /*window*/, int focused)
        {
            event::distribute<event::window_focus_event_dispatcher>(
                {.focused = focused != 0});
        }

        // 窗口最小化/最大化回调
        static void windowIconifyCallback(GLFWwindow * /*window*/, int iconified)
        {
            event::distribute<event::window_iconify_event_dispatcher>(
                {.iconified = iconified != 0});
        }
        static void windowMaximizeCallback(GLFWwindow * /*window*/, int maximized)
        {
            event::distribute<event::window_maximize_event_dispatcher>(
                {.maximized = maximized != 0});
        }

        static void charCallback(GLFWwindow * /*window*/, unsigned int codepoint) noexcept
        {
            event::distribute<event::char_event_dispatcher>({.codepoint = codepoint});
        }

        static void dropCallback(GLFWwindow * /*window*/, int count,
                                 const char **paths) noexcept
        {
            event::distribute<event::drop_event_dispatcher>(
                event::drop_event{.count = count, .paths = paths});
        }

        // 广播 窗口 状态
        constexpr void broadcastWindowState() const noexcept
        {
            // 位置（当前窗口化位置）
            event::distribute<event::window_pos_event_dispatcher>({.x = x_, .y = y_});

            // 尺寸
            event::distribute<event::window_size_event_dispatcher>(
                {.width = width_, .height = height_});

            // framebuffer
            {
                int fw, fh; // NOLINT
                ::glfwGetFramebufferSize(window_, &fw, &fh);
                event::distribute<event::framebuffer_size_event_dispatcher>(
                    {.width = fw, .height = fh});
            }

            // 布尔状态：查当前真实值
            event::distribute<event::window_focus_event_dispatcher>(
                {.focused = ::glfwGetWindowAttrib(window_, GLFW_FOCUSED) != 0});
            event::distribute<event::window_iconify_event_dispatcher>(
                {.iconified = ::glfwGetWindowAttrib(window_, GLFW_ICONIFIED) != 0});
            event::distribute<event::window_maximize_event_dispatcher>(
                {.maximized = ::glfwGetWindowAttrib(window_, GLFW_MAXIMIZED) != 0});
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

        constexpr VkSurfaceKHR createVkSurfaceKHR(VkInstance instance) const
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
            if (capabilities.currentExtent.width !=
                (std::numeric_limits<uint32_t>::max)())
                return capabilities.currentExtent;

            auto [width, height] = getFramebufferSize();
            return {std::clamp<uint32_t>(width, capabilities.minImageExtent.width,
                                         capabilities.maxImageExtent.width),
                    std::clamp<uint32_t>(height, capabilities.minImageExtent.height,
                                         capabilities.maxImageExtent.height)};
        }
    };

}; // namespace mcs::vulkan::wsi::glfw