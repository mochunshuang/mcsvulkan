#pragma once

#include "sType.hpp"
#include "pNext.hpp"
#include "Flags.hpp"
#include "../surface.hpp"
#include "../utils/mcslog.hpp"
#include "../Swapchain.hpp"

#include <span>
#include <vector>

namespace mcs::vulkan::tool
{
    template <surface Surface>
    struct create_swap_chain
    {
        constexpr static VkSurfaceFormatKHR selectSurfaceFormat(
            const std::span<VkSurfaceFormatKHR> &selects,
            const std::vector<VkSurfaceFormatKHR> &availableFormats) noexcept
        {
            MCS_ASSERT(availableFormats.size() > 0);
            for (auto &select : selects)
            {
                for (const auto &availableFormat : availableFormats)
                {
                    if (availableFormat.format == select.format &&
                        availableFormat.colorSpace == select.colorSpace)
                        return availableFormat;
                }
            }
            MCSLOG_WARN("the selected surfaceFormat is availableFormats[0]");
            return availableFormats[0];
        }
        constexpr static VkPresentModeKHR selectPresentMode(
            const std::span<VkPresentModeKHR> &selects,
            const std::vector<VkPresentModeKHR> &availablePresentModes) noexcept
        {
            MCS_ASSERT(availablePresentModes.size() > 0);
            for (auto &select : selects)
                for (const auto &availablePresentMode : availablePresentModes)
                {
                    if (availablePresentMode == select)
                        return availablePresentMode;
                }
            MCSLOG_WARN("the selected presentMode is availablePresentModes[0]");
            return availablePresentModes[0];
        }
        // auto changeMinImageCount(uint32_t minImageCount) {}
        using changeMinImageCountFn = uint32_t(uint32_t minImageCount) noexcept;

        /*
        typedef struct VkSwapchainCreateInfoKHR {
            VkStructureType                  sType;
            const void*                      pNext;
            VkSwapchainCreateFlagsKHR        flags;
            VkSurfaceKHR                     surface;
            uint32_t                         minImageCount;
            VkFormat                         imageFormat;
            VkColorSpaceKHR                  imageColorSpace;
            VkExtent2D                       imageExtent;
            uint32_t                         imageArrayLayers;
            VkImageUsageFlags                imageUsage;
            VkSharingMode                    imageSharingMode;
            uint32_t                         queueFamilyIndexCount;
            const uint32_t*                  pQueueFamilyIndices;
            VkSurfaceTransformFlagBitsKHR    preTransform;
            VkCompositeAlphaFlagBitsKHR      compositeAlpha;
            VkPresentModeKHR                 presentMode;
            VkBool32                         clipped;
            VkSwapchainKHR                   oldSwapchain;
        } VkSwapchainCreateInfoKHR;
        */
        struct create_info // NOLINTBEGIN
        {
            pNext pNext;
            Flags<VkSwapchainCreateFlagBitsKHR> flags;
            changeMinImageCountFn *changeMinImageCount{};
            std::vector<VkSurfaceFormatKHR> candidateSurfaceFormats;
            uint32_t imageArrayLayers{};
            Flags<VkImageUsageFlagBits> imageUsage;
            VkSharingMode imageSharingMode{};
            std::vector<uint32_t> queueFamilyIndices;
            VkSurfaceTransformFlagBitsKHR preTransform{};
            VkCompositeAlphaFlagBitsKHR compositeAlpha{};
            std::vector<VkPresentModeKHR> candidatePresentModes;
            VkBool32 clipped{};
            VkSwapchainKHR oldSwapchain{};
        }; // NOLINTEND

        /*
        typedef struct VkImageViewCreateInfo {
            VkStructureType            sType;
            const void*                pNext;
            VkImageViewCreateFlags     flags;
            VkImage                    image;
            VkImageViewType            viewType;
            VkFormat                   format;
            VkComponentMapping         components;
            VkImageSubresourceRange    subresourceRange;
        } VkImageViewCreateInfo;
        */
        struct view_create_info // NOLINTBEGIN
        {
            VkImageViewCreateInfo operator()() noexcept
            {
                return {.sType = sType<VkImageViewCreateInfo>(),
                        .pNext = pNext.value(),
                        .flags = flags,
                        .viewType = viewType,
                        .components = components,
                        .subresourceRange = subresourceRange};
            }
            pNext pNext;
            Flags<VkImageViewCreateFlagBits> flags;
            VkImageViewType viewType{};
            VkComponentMapping components{};
            VkImageSubresourceRange subresourceRange{};
        }; // NOLINTEND

        // NOTE: 这样确实更好
        constexpr auto rebuild() &
        {
            auto capabilities = surfaceInterface_->surfaceCapabilities();
            VkExtent2D imageExtent = surfaceInterface_->chooseSwapExtent(capabilities);
            swapchainCreateInfo_.imageExtent = imageExtent;

            VkSwapchainKHR swapchain = nullptr;
            std::vector<VkImage> swapChainImages;
            std::vector<VkImageView> swapChainImageViews;
            try
            {
                auto &CI = swapchainCreateInfo_;
                auto &viewCI = imageViewCreateInfo_;
                swapchain = device_->createSwapchainKHR(CI, device_->allocator());
                swapChainImages = device_->getSwapchainImagesKHR(swapchain);

                swapChainImageViews.reserve(swapChainImages.size());
                for (auto *image : swapChainImages)
                {
                    viewCI.image = image;
                    swapChainImageViews.emplace_back(
                        device_->createImageView(viewCI, device_->allocator()));
                }
                return Swapchain(device_, swapchain, std::move(swapChainImages),
                                 std::move(swapChainImageViews), imageExtent);
            }
            catch (...)
            {
                for (auto *imageView : swapChainImageViews)
                    device_->destroyImageView(imageView, device_->allocator());
                swapChainImageViews.clear();

                if (swapchain != nullptr)
                    device_->destroySwapchainKHR(swapchain, device_->allocator());

                throw;
            }
        }

        constexpr auto build() &
        {
            auto capabilities = surfaceInterface_->surfaceCapabilities();
            std::vector<VkSurfaceFormatKHR> availableFormats =
                surfaceInterface_->surfaceFormats();
            std::vector<VkPresentModeKHR> availablePresentModes =
                surfaceInterface_->surfacePresentModes();

            auto minImageCount = capabilities.minImageCount;
            if (createInfo_.changeMinImageCount != nullptr)
                minImageCount = createInfo_.changeMinImageCount(minImageCount);
            if (minImageCount > capabilities.maxImageCount)
            {
                minImageCount = capabilities.maxImageCount;
                MCSLOG_WARN("minImageCount change to capabilities.maxImageCount: {}",
                            minImageCount);
            }
            auto [imageFormat, imageColorSpace] = selectSurfaceFormat(
                createInfo_.candidateSurfaceFormats, availableFormats);
            VkExtent2D imageExtent = surfaceInterface_->chooseSwapExtent(capabilities);

            auto imageArrayLayers = createInfo_.imageArrayLayers;
            if (imageArrayLayers > capabilities.maxImageArrayLayers)
            {
                imageArrayLayers = capabilities.maxImageArrayLayers;
                MCSLOG_WARN(
                    "imageArrayLayers change to capabilities.maxImageArrayLayers");
            }
            auto preTransform = createInfo_.preTransform;
            if ((preTransform & capabilities.supportedTransforms) == 0)
            {
                preTransform = capabilities.currentTransform;
                MCSLOG_WARN("preTransform change to  capabilities.currentTransform");
            }
            auto compositeAlpha = createInfo_.compositeAlpha;
            if ((compositeAlpha & capabilities.supportedCompositeAlpha) == 0)
            {
                compositeAlpha = {};
                MCSLOG_WARN("compositeAlpha change to defalut value.");
            }
            auto presentMode = selectPresentMode(createInfo_.candidatePresentModes,
                                                 availablePresentModes);

            VkSwapchainCreateInfoKHR CI{
                .sType = sType<VkSwapchainCreateInfoKHR>(),
                .pNext = createInfo_.pNext.value(),
                .flags = createInfo_.flags,
                .surface = **surfaceInterface_,
                .minImageCount = minImageCount,
                .imageFormat = imageFormat,
                .imageColorSpace = imageColorSpace,
                .imageExtent = imageExtent,
                .imageArrayLayers = imageArrayLayers,
                .imageUsage = createInfo_.imageUsage,
                .imageSharingMode = createInfo_.imageSharingMode,
                .queueFamilyIndexCount =
                    static_cast<uint32_t>(createInfo_.queueFamilyIndices.size()),
                .pQueueFamilyIndices = createInfo_.queueFamilyIndices.empty()
                                           ? nullptr
                                           : createInfo_.queueFamilyIndices.data(),
                .preTransform = preTransform,
                .compositeAlpha = compositeAlpha,
                .presentMode = presentMode,
                .clipped = createInfo_.clipped,
                .oldSwapchain = createInfo_.oldSwapchain};

            VkImageViewCreateInfo viewCI = viewCreateInfo_();
            viewCI.format = imageFormat;

            VkSwapchainKHR swapchain = nullptr;
            std::vector<VkImage> swapChainImages;
            std::vector<VkImageView> swapChainImageViews;
            try
            {
                swapchain = device_->createSwapchainKHR(CI, device_->allocator());
                swapChainImages = device_->getSwapchainImagesKHR(swapchain);

                swapChainImageViews.reserve(swapChainImages.size());
                for (auto *image : swapChainImages)
                {
                    viewCI.image = image;
                    swapChainImageViews.emplace_back(
                        device_->createImageView(viewCI, device_->allocator()));
                }
                swapchainCreateInfo_ = CI;
                imageViewCreateInfo_ = viewCI;
                return Swapchain(device_, swapchain, std::move(swapChainImages),
                                 std::move(swapChainImageViews), imageExtent);
            }
            catch (...)
            {
                for (auto *imageView : swapChainImageViews)
                    device_->destroyImageView(imageView, device_->allocator());
                swapChainImageViews.clear();

                if (swapchain != nullptr)
                    device_->destroySwapchainKHR(swapchain, device_->allocator());

                throw;
            }
        }

        constexpr auto &&setCreateInfo(create_info createInfo) && noexcept
        {
            createInfo_ = std::move(createInfo);
            return std::move(*this);
        }

        constexpr auto &&setViewCreateInfo(view_create_info viewCreateInfo) && noexcept
        {
            viewCreateInfo_ = std::move(viewCreateInfo);
            return std::move(*this);
        }

        constexpr create_swap_chain(const Surface &surface,
                                    const LogicalDevice &device) noexcept
            : surfaceInterface_{&surface}, device_{&device}
        {
        }

      private:
        const Surface *surfaceInterface_{};
        const LogicalDevice *device_{};
        create_info createInfo_;
        view_create_info viewCreateInfo_;
        VkSwapchainCreateInfoKHR swapchainCreateInfo_{};
        VkImageViewCreateInfo imageViewCreateInfo_{};
    };

    template <surface Surface>
    create_swap_chain(const Surface &surface, const LogicalDevice &device)
        -> create_swap_chain<Surface>;

}; // namespace mcs::vulkan::tool