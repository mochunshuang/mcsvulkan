#pragma once

#include "Initialization.hpp"

#include "../utils/make_vk_exception.hpp"
#include <algorithm>

namespace mcs::vulkan::tool
{
    struct enable_bulid
    {
        static constexpr auto checkExtensionSupport(
            const std::vector<const char *> &required,
            const std::vector<VkExtensionProperties> &available)
        {
            for (const auto *extension_name : required)
            {
                bool found = std::ranges::any_of(
                    available, [&](auto const &available_extension) noexcept {
                        return ::strcmp(available_extension.extensionName,
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
                if (bool found = std::ranges::any_of(available,
                                                     [&](auto const &lp) noexcept {
                                                         return ::strcmp(lp.layerName,
                                                                         layer_name) == 0;
                                                     });
                    not found)
                    // Output an error message for the missing extension
                    throw make_vk_exception(
                        std::format("Required layer not found: {}", layer_name));
            }
        }

        //-------------------------------get-------------------------
        [[nodiscard]] auto &enabledLayers() noexcept
        {
            return enabledLayer_;
        }

        [[nodiscard]] auto &enabledExtensions() noexcept
        {
            return enabledExtension_;
        }

        [[nodiscard]] auto &enabledLayers() const noexcept
        {
            return enabledLayer_;
        }

        [[nodiscard]] auto &enabledExtensions() const noexcept
        {
            return enabledExtension_;
        }

        //-------------------------------update-------------------------
        auto &addEnableExtension(const char *const EXTENSION)
        {
            enabledExtension_.emplace_back(EXTENSION);
            return *this;
        }
        auto &addEnableLayer(const char *const LAYER)
        {
            enabledLayer_.emplace_back(LAYER);
            return *this;
        }
        auto &enableDebugExtension()
        {
            addEnableExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            return *this;
        }
        auto &enableValidationLayer()
        {
            addEnableLayer("VK_LAYER_KHRONOS_validation");
            return *this;
        }
        template <typename surface>
        auto &enableSurfaceExtension()
        {
            addEnableExtension(VK_KHR_SURFACE_EXTENSION_NAME);
            enabledExtension_.append_range(surface::requiredVulkanInstanceExtensions());
            return *this;
        }

        void check() const
        {
            const auto &enabledLayer = enabledLayers();
            const auto &enabledExtension = enabledExtensions();
            checkExtensionSupport(enabledExtension,
                                  Initialization::instanceExtensionPropertie());
            checkLayerSupport(enabledLayer, Initialization::instanceLayerProperties());
        }

        auto layerContains(const char *const LAYER) const noexcept
        {
            return std::ranges::find(enabledLayer_, LAYER) != enabledLayer_.end();
        }
        auto extensionContains(const char *const EXTENSION) const noexcept
        {
            return std::ranges::find(enabledExtension_, EXTENSION) !=
                   enabledExtension_.end();
        }

      private:
        std::vector<const char *> enabledLayer_;
        std::vector<const char *> enabledExtension_;
    };

}; // namespace mcs::vulkan::tool