#pragma once

#include "../utils/check_vkresult.hpp"
#include "sType.hpp"
#include "pNext.hpp"
#include "Flags.hpp"
#include "../Instance.hpp"
#include <span>

namespace mcs::vulkan::tool
{
    /*
    即使实现支持该扩展，除非在 VkInstance 或 VkDevice
    创建时启用该扩展，否则使用该扩展的功能是未定义的行为。这就是create_instance的原因
    */
    struct create_instance
    {

        /*
        typedef struct VkApplicationInfo {
            VkStructureType    sType;
            const void*        pNext;
            const char*        pApplicationName;
            uint32_t           applicationVersion;
            const char*        pEngineName;
            uint32_t           engineVersion;
            uint32_t           apiVersion;
        } VkApplicationInfo;
        */
        struct app_info // NOLINTBEGIN
        {
            constexpr VkApplicationInfo operator()() const noexcept
            {
                return {.sType = sType<VkApplicationInfo>(),
                        .pNext = pNext.value(),
                        .pApplicationName = pApplicationName,
                        .applicationVersion = applicationVersion,
                        .pEngineName = pEngineName,
                        .engineVersion = engineVersion,
                        .apiVersion = apiVersion};
            }
            pNext pNext;
            const char *pApplicationName{};
            uint32_t applicationVersion{};
            const char *pEngineName{};
            uint32_t engineVersion{};
            uint32_t apiVersion{};
        }; // NOLINTEND

        /*
        typedef struct VkInstanceCreateInfo {
            VkStructureType             sType;
            const void*                 pNext;
            VkInstanceCreateFlags       flags;
            const VkApplicationInfo*    pApplicationInfo;
            uint32_t                    enabledLayerCount;
            const char* const*          ppEnabledLayerNames;
            uint32_t                    enabledExtensionCount;
            const char* const*          ppEnabledExtensionNames;
        } VkInstanceCreateInfo;
        */
        struct create_info // NOLINTBEGIN
        {
            pNext pNext;
            Flags<VkInstanceCreateFlagBits> flags;
            app_info applicationInfo{};
            std::span<const char *const> enabledLayers;
            std::span<const char *const> enabledExtensions;

            constexpr auto operator()() const noexcept
            {
                struct result_type
                {
                    constexpr result_type(VkApplicationInfo app,
                                          VkInstanceCreateInfo create) noexcept
                        : app_info{app}, create_info{create}
                    {
                        create_info.pApplicationInfo = &app_info;
                    }
                    [[nodiscard]] constexpr auto &createInfo() const & noexcept
                    {
                        return create_info;
                    }
                    VkApplicationInfo app_info;
                    VkInstanceCreateInfo create_info;
                };
                return result_type{
                    applicationInfo(),
                    {.sType = sType<VkInstanceCreateInfo>(),
                     .pNext = pNext.value(),
                     .flags = flags,
                     .enabledLayerCount = static_cast<uint32_t>(enabledLayers.size()),
                     .ppEnabledLayerNames =
                         enabledLayers.empty() ? nullptr : enabledLayers.data(),
                     .enabledExtensionCount =
                         static_cast<uint32_t>(enabledExtensions.size()),
                     .ppEnabledExtensionNames =
                         enabledExtensions.empty() ? nullptr : enabledExtensions.data()}};
            }
        }; // NOLINTEND

        [[nodiscard]] constexpr Instance build(
            VkAllocationCallbacks *pAllocator = nullptr) const
        {
            auto result = createInfo_();
            VkInstance instance; // NOLINT

            check_vkresult(vkCreateInstance(&result.createInfo(), pAllocator, &instance));
            return Instance{instance, pAllocator};
        }

        [[nodiscard]] constexpr const create_info &createInfo() const noexcept
        {
            return createInfo_;
        }
        constexpr auto &setCreateInfo(create_info &&createInfo) noexcept
        {
            createInfo_ = std::move(createInfo);
            return *this;
        }

      private:
        create_info createInfo_;
    };

}; // namespace mcs::vulkan::tool