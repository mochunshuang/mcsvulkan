#pragma once

#include "sType.hpp"
#include "pNext.hpp"
#include "Flags.hpp"
#include "../Instance.hpp"
#include "../Debugger.hpp"
#include "../utils/mcslog.hpp"

namespace mcs::vulkan::tool
{
    struct create_debugger
    {
        /// @brief A debug callback called from Vulkan validation layers.
        static VKAPI_ATTR VkBool32 VKAPI_CALL
        defaultDebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                             VkDebugUtilsMessageTypeFlagsEXT /*message_types*/,
                             const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
                             void * /*user_data*/) noexcept
        {

            if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
            {
                MCSLOG_ERROR("{} Validation Layer: Error: {}: {}",
                             callback_data->messageIdNumber,
                             callback_data->pMessageIdName, callback_data->pMessage);
            }
            else if ((message_severity &
                      VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0)
            {
                MCSLOG_WARN("{} Validation Layer: Warning: {}: {}",
                            callback_data->messageIdNumber, callback_data->pMessageIdName,
                            callback_data->pMessage);
            }
            else if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) !=
                     0)
            {
                MCSLOG_INFO("{} Validation Layer: Information: {}: {}",
                            callback_data->messageIdNumber, callback_data->pMessageIdName,
                            callback_data->pMessage);
            }
            else if ((message_severity &
                      VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) != 0)
            {
                MCSLOG_DEBUG("{} Validation Layer: Verbose: {}: {}",
                             callback_data->messageIdNumber,
                             callback_data->pMessageIdName, callback_data->pMessage);
            }
            return VK_FALSE;
        }

        /*
       typedef struct VkDebugUtilsMessengerCreateInfoEXT {
           VkStructureType                         sType;
           const void*                             pNext;
           VkDebugUtilsMessengerCreateFlagsEXT     flags;
           VkDebugUtilsMessageSeverityFlagsEXT     messageSeverity;
           VkDebugUtilsMessageTypeFlagsEXT         messageType;
           PFN_vkDebugUtilsMessengerCallbackEXT    pfnUserCallback;
           void*                                   pUserData;
       } VkDebugUtilsMessengerCreateInfoEXT;
       */
        struct create_info // NOLINTBEGIN
        {
            constexpr VkDebugUtilsMessengerCreateInfoEXT operator()() const noexcept
            {
                return {.sType = sType<VkDebugUtilsMessengerCreateInfoEXT>(),
                        .pNext = pNext.value(),
                        .flags = flags,
                        .messageSeverity = messageSeverity,
                        .messageType = messageType,
                        .pfnUserCallback = pfnUserCallback,
                        .pUserData = pUserData};
            }
            pNext pNext;
            VkDebugUtilsMessengerCreateFlagsEXT flags{};
            VkDebugUtilsMessageSeverityFlagsEXT messageSeverity{};
            Flags<VkDebugUtilsMessageTypeFlagBitsEXT> messageType;
            PFN_vkDebugUtilsMessengerCallbackEXT pfnUserCallback{};
            void *pUserData{};
        }; // NOLINTEND

        static constexpr create_info defaultCreateInfo() noexcept
        {
            return {.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                       VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                       VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
                    .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                   VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                   VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
                    .pfnUserCallback = &defaultDebugCallback};
        }

        [[nodiscard]] constexpr auto &createInfo() const noexcept
        {
            return createInfo_;
        }
        constexpr auto &setCreateInfo(create_info &&createInfo) noexcept
        {
            createInfo_ = std::move(createInfo);
            return *this;
        }
        [[nodiscard]] constexpr Debugger build(const Instance &instance) const
        {
            auto CI = createInfo_();
            VkDebugUtilsMessengerEXT messenger =
                instance.createDebugUtilsMessengerEXT(&CI, instance.allocator());
            return Debugger{instance, messenger};
        }

      private:
        create_info createInfo_;
    };

}; // namespace mcs::vulkan::tool