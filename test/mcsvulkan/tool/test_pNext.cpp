#include <cassert>
#include <exception>
#include <iostream>
#include <print>

#include "../head.hpp"

using pNext = mcs::vulkan::tool::pNext;
using mcs::vulkan::tool::structure_chain;
using mcs::vulkan::tool::make_pNext;

using mcs::vulkan::tool::sType;

using mcs::vulkan::MCS_ASSERT;

int main()
try
{
    using mcs::vulkan::check_vkresult;
    check_vkresult(::volkInitialize());

    structure_chain<VkPhysicalDeviceFeatures2, VkPhysicalDeviceVulkan13Features,
                    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT>
        enablefeatureChain = {{},
                              {.synchronization2 = VK_TRUE, .dynamicRendering = VK_TRUE},
                              {.extendedDynamicState = VK_TRUE}};
    {
        std::print("enablefeatureChain: {}, head: {}, equal: {}\n",
                   static_cast<void *>(&enablefeatureChain),
                   static_cast<void *>(&enablefeatureChain.head()),
                   static_cast<void *>(&enablefeatureChain) ==
                       static_cast<void *>(&enablefeatureChain.head()));
        MCS_ASSERT(static_cast<void *>(&enablefeatureChain) ==
                   static_cast<void *>(&enablefeatureChain.head()));

        // NOTE: 只有 C 结构体才具备
        static_assert(not std::is_standard_layout_v<decltype(enablefeatureChain)>);
        static_assert(not std::is_trivial_v<decltype(enablefeatureChain)>);
    }
    auto next = make_pNext(enablefeatureChain);
    auto *ptr = static_cast<VkPhysicalDeviceFeatures2 *>(next.value());
    auto *p = static_cast<VkPhysicalDeviceVulkan13Features *>(ptr->pNext);
    auto *p2 = static_cast<VkPhysicalDeviceExtendedDynamicStateFeaturesEXT *>(p->pNext);

    MCS_ASSERT(ptr->sType == sType<VkPhysicalDeviceFeatures2>());
    MCS_ASSERT(p->sType == sType<VkPhysicalDeviceVulkan13Features>());
    MCS_ASSERT(p2->sType == sType<VkPhysicalDeviceExtendedDynamicStateFeaturesEXT>());

    MCS_ASSERT(p->synchronization2 == VK_TRUE);
    MCS_ASSERT(p->dynamicRendering == VK_TRUE);
    MCS_ASSERT(p2->extendedDynamicState == VK_TRUE);

    // MCS_ASSERT(*ptr == *ptr); //NOTE: 默认不支持没办法

    // NOTE: 其他的必须是 VK_FALSE
    MCS_ASSERT(p->shaderZeroInitializeWorkgroupMemory == VK_FALSE);

    {
        auto next = make_pNext(
            structure_chain<VkPhysicalDeviceFeatures2, VkPhysicalDeviceVulkan13Features,
                            VkPhysicalDeviceExtendedDynamicStateFeaturesEXT>{
                {},
                {.synchronization2 = VK_TRUE, .dynamicRendering = VK_TRUE},
                {.extendedDynamicState = VK_TRUE}});
        MCS_ASSERT(next);
    }
    {
        auto next = pNext{};
        MCS_ASSERT(not next);
        MCS_ASSERT(next.value() == nullptr);
    }

    {

        using T = decltype(enablefeatureChain);
        auto storage = std::make_unique_for_overwrite<uint8_t[]>(sizeof(T)); // NOLINT

        T *ptr = reinterpret_cast<T *>(storage.get()); // NOLINT
        // 构造对象
        new (ptr) T(enablefeatureChain);
        VkPhysicalDeviceFeatures2 *feature = &ptr->head();

        auto *p1 = static_cast<T *>(static_cast<void *>(storage.get())); // NOLINT
        MCS_ASSERT(feature == &p1->head());

        auto storage2 = std::move(storage);
        MCS_ASSERT(nullptr == storage);

        // NOTE: 移动的指针 是 值赋值的
        MCS_ASSERT(p1 == static_cast<void *>(storage2.get()));
        // NOLINTNEXTLINE
        MCS_ASSERT(feature ==
                   &static_cast<T *>(static_cast<void *>(storage2.get()))->head());

        // NOTE: 这是优化的点.
        MCS_ASSERT(static_cast<void *>(ptr) == static_cast<void *>(&ptr->head()));
        MCS_ASSERT(static_cast<void *>(feature) == static_cast<void *>(ptr));
    }

    ::volkFinalize();
    std::cout << "main done\n";
    return 0;
}
catch (std::exception &e)
{
    std::println("main catch exception: {}", e.what());
}