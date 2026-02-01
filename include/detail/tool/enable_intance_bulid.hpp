#pragma once

#include "Initialization.hpp"

#include "../utils/make_vk_exception.hpp"
#include <algorithm>
#include <print>

namespace mcs::vulkan::tool
{
    struct enable_intance_bulid
    {
        static constexpr auto checkExtensionSupport(
            const std::vector<const char *> &required,
            const std::vector<VkExtensionProperties> &available)
        {
            for (const auto *extension_name : required)
            {
                bool found = std::ranges::any_of(
                    available, [&](auto const &available_extension) noexcept {
                        return std::strcmp(available_extension.extensionName,
                                           extension_name) == 0;
                    });
                if (!found)
                    throw make_vk_exception(
                        std::format("Required extension not found: {}", extension_name));
            }
        }
        constexpr static void checkLayerSupport(
            const std::vector<const char *> &required,
            const std::vector<VkLayerProperties> &available)
        {
            for (const auto *layer_name : required)
            {
                if (bool found = std::ranges::any_of(
                        available,
                        [&](auto const &lp) noexcept {
                            return std::strcmp(lp.layerName, layer_name) == 0;
                        });
                    not found)
                    // Output an error message for the missing extension
                    throw make_vk_exception(
                        std::format("Required layer not found: {}", layer_name));
            }
        }

        constexpr auto layerContains(const char *const LAYER) const noexcept
        {
            return std::ranges::find(enabledLayer_, LAYER) != enabledLayer_.end();
        }
        constexpr auto extensionContains(const char *const EXTENSION) const noexcept
        {
            return std::ranges::find(enabledExtension_, EXTENSION) !=
                   enabledExtension_.end();
        }

        //-------------------------------get-------------------------
        [[nodiscard]] constexpr auto &enabledLayers() noexcept
        {
            return enabledLayer_;
        }

        [[nodiscard]] constexpr auto &enabledExtensions() noexcept
        {
            return enabledExtension_;
        }

        [[nodiscard]] constexpr auto &enabledLayers() const noexcept
        {
            return enabledLayer_;
        }

        [[nodiscard]] constexpr auto &enabledExtensions() const noexcept
        {
            return enabledExtension_;
        }

        //-------------------------------update-------------------------
        constexpr auto &addEnableExtension(const char *const EXTENSION)
        {
            enabledExtension_.emplace_back(EXTENSION);
            return *this;
        }
        constexpr auto &addEnableLayer(const char *const LAYER)
        {
            enabledLayer_.emplace_back(LAYER);
            return *this;
        }
        constexpr auto &enableDebugExtension()
        {
            addEnableExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            return *this;
        }
        constexpr auto &enableValidationLayer()
        {
            addEnableLayer("VK_LAYER_KHRONOS_validation");
            return *this;
        }
        template <typename surface>
        constexpr auto &enableSurfaceExtension()
        {
            enabledExtension_.append_range(surface::requiredVulkanInstanceExtensions());
            if (not extensionContains(VK_KHR_SURFACE_EXTENSION_NAME))
                addEnableExtension(VK_KHR_SURFACE_EXTENSION_NAME);
            return *this;
        }

        constexpr void check() const
        {
            const auto &enabledLayer = enabledLayers();
            const auto &enabledExtension = enabledExtensions();
            checkExtensionSupport(enabledExtension,
                                  Initialization::instanceExtensionPropertie());
            checkLayerSupport(enabledLayer, Initialization::instanceLayerProperties());
        }

        constexpr void print()
        {
            std::println("enabledLayers: ");
            for (auto &layer : enabledLayer_)
            {
                std::println("  {}", layer);
            }
            std::println("enabledExtensions: ");
            for (auto &extension : enabledExtension_)
            {
                std::println("  {}", extension);
            }
        }

      private:
        std::vector<const char *> enabledLayer_;
        std::vector<const char *> enabledExtension_;
    };

}; // namespace mcs::vulkan::tool