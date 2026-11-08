#pragma once
// ============================================================================
// test_intent7_pass_data.hpp
//
// Pass data schema (vertical axis):
//   PassData
//     - PassTarget          VkRenderingInfo + attachments
//     - PipelineBinding     pipeline / layout / rendering formats / ownership
//     - DescriptorBinding   sets / dynamic offsets / buffers / heaps
//     - PushConstants       inline push constant block
//     - VertexInputBinding  vertex/index buffers
//     - DynamicState        VkDynamicState -> inline value bytes
//     - DrawArgs            direct/indirect draw operands
//     - SyncState           barriers / events / queries / timeline
//     - Inheritance         full command buffer inheritance data
//     - FeatureSet*         device capability reference
//
// Op/OpStream lives in test_intent7_pass_opcode.hpp.
// Op -> vkCmd* lives in test_intent7_pass_emitter.hpp.
// ============================================================================

#include "../head.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>

namespace pass_data
{

inline constexpr std::uint32_t MAX_COLOR_ATTACHMENTS = 8;

// ---- convenience constructors: fields are VkRenderingAttachmentInfo fields --
inline VkRenderingAttachmentInfo color_attachment(
    VkImageView imageView, VkImageLayout imageLayout,
    VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
    VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    VkClearValue clearValue = {},
    VkResolveModeFlagBits resolveMode = VK_RESOLVE_MODE_NONE,
    VkImageView resolveImageView = VK_NULL_HANDLE,
    VkImageLayout resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) noexcept
{
    VkRenderingAttachmentInfo result{};
    result.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    result.imageView = imageView;
    result.imageLayout = imageLayout;
    result.resolveMode = resolveMode;
    result.resolveImageView = resolveImageView;
    result.resolveImageLayout = resolveImageLayout;
    result.loadOp = loadOp;
    result.storeOp = storeOp;
    result.clearValue = clearValue;
    return result;
}

inline VkRenderingAttachmentInfo depth_attachment(
    VkImageView imageView, VkImageLayout imageLayout,
    VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
    VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    VkClearValue clearValue = {}) noexcept
{
    VkRenderingAttachmentInfo result{};
    result.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    result.imageView = imageView;
    result.imageLayout = imageLayout;
    result.loadOp = loadOp;
    result.storeOp = storeOp;
    result.clearValue = clearValue;
    return result;
}

inline VkRenderingAttachmentInfo stencil_attachment(
    VkImageView imageView, VkImageLayout imageLayout,
    VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
    VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    VkClearValue clearValue = {}) noexcept
{
    return depth_attachment(imageView, imageLayout, loadOp, storeOp, clearValue);
}

// ---- pipeline rendering compatibility metadata ----------------------------
struct PipelineRenderingInfo
{
    std::array<VkFormat, MAX_COLOR_ATTACHMENTS> colorFormats{};
    std::uint32_t colorCount{0};
    VkFormat depthFormat{VK_FORMAT_UNDEFINED};
    VkFormat stencilFormat{VK_FORMAT_UNDEFINED};
    VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};
    std::uint32_t viewMask{0};
};

// Compatibility data copied into VkCommandBufferInheritanceRenderingInfo for
// secondary command buffers. Owned arrays, no dangling pointers.
struct InheritanceRenderingInfo
{
    VkRenderingFlags flags{0};
    std::uint32_t viewMask{0};
    std::uint32_t colorAttachmentCount{0};
    std::array<VkFormat, MAX_COLOR_ATTACHMENTS> colorFormats{};
    VkFormat depthFormat{VK_FORMAT_UNDEFINED};
    VkFormat stencilFormat{VK_FORMAT_UNDEFINED};
    VkSampleCountFlagBits rasterizationSamples{VK_SAMPLE_COUNT_1_BIT};
};

// ---- pass domain -----------------------------------------------------------
enum class PassKind : std::uint8_t
{
    Graphics,
    Compute,
    Transfer,
};

// ---- pass target: one beginRendering/endRendering instance ----------------
struct PassTarget
{
    PassKind kind{PassKind::Graphics};
    VkRect2D renderArea{};
    std::uint32_t layerCount{1};
    std::uint32_t viewMask{0};
    VkRenderingFlags flags{0};
    const void *pNext{nullptr};

    std::array<VkRenderingAttachmentInfo, MAX_COLOR_ATTACHMENTS> colors{};
    std::uint32_t colorCount{0};

    // Compatibility metadata used by the validator.
    std::array<VkFormat, MAX_COLOR_ATTACHMENTS> colorFormats{};
    VkFormat depthFormat{VK_FORMAT_UNDEFINED};
    VkFormat stencilFormat{VK_FORMAT_UNDEFINED};
    VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};

    VkRenderingAttachmentInfo depth{};
    VkRenderingAttachmentInfo stencil{};
    bool hasDepth{false};
    bool hasStencil{false};

    [[nodiscard]] VkRenderingInfo info() const noexcept
    {
        VkRenderingInfo result{};
        result.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        result.pNext = pNext;
        result.flags = flags;
        result.renderArea = renderArea;
        result.layerCount = layerCount;
        result.viewMask = viewMask;
        result.colorAttachmentCount = colorCount;
        result.pColorAttachments = colorCount == 0 ? nullptr : colors.data();
        result.pDepthAttachment = hasDepth ? &depth : nullptr;
        result.pStencilAttachment = hasStencil ? &stencil : nullptr;
        return result;
    }

    void setColor(std::uint32_t index, const VkRenderingAttachmentInfo &attachment) noexcept
    {
        assert(index < MAX_COLOR_ATTACHMENTS);
        colors[index] = attachment;
        if (colorCount <= index)
            colorCount = index + 1;
    }

    void addColor(const VkRenderingAttachmentInfo &attachment) noexcept
    {
        assert(colorCount < MAX_COLOR_ATTACHMENTS);
        colors[colorCount++] = attachment;
    }

    void setDepth(const VkRenderingAttachmentInfo &attachment) noexcept
    {
        depth = attachment;
        hasDepth = true;
    }

    void setStencil(const VkRenderingAttachmentInfo &attachment) noexcept
    {
        stencil = attachment;
        hasStencil = true;
    }

    void setLoadOp(VkAttachmentLoadOp op) noexcept
    {
        for (std::uint32_t i = 0; i < colorCount; ++i)
            colors[i].loadOp = op;
        if (hasDepth)
            depth.loadOp = op;
        if (hasStencil)
            stencil.loadOp = op;
    }

    void setStoreOp(VkAttachmentStoreOp op) noexcept
    {
        for (std::uint32_t i = 0; i < colorCount; ++i)
            colors[i].storeOp = op;
        if (hasDepth)
            depth.storeOp = op;
        if (hasStencil)
            stencil.storeOp = op;
    }

    void setImageLayout(VkImageLayout layout) noexcept
    {
        for (std::uint32_t i = 0; i < colorCount; ++i)
            colors[i].imageLayout = layout;
        if (hasDepth)
            depth.imageLayout = layout;
        if (hasStencil)
            stencil.imageLayout = layout;
    }

    [[nodiscard]] PassTarget withLoad(VkAttachmentLoadOp op) const noexcept
    {
        PassTarget result = *this;
        result.setLoadOp(op);
        return result;
    }

    [[nodiscard]] PassTarget withStore(VkAttachmentStoreOp op) const noexcept
    {
        PassTarget result = *this;
        result.setStoreOp(op);
        return result;
    }
};

// Compatibility name used by the existing recorder/manager code.
using PassDesc = PassTarget;

// ---- pipeline state ownership ---------------------------------------------
// Pipelines 10.13: a state is Static, Dynamic, Absent, or Inherited.
enum class StateOwnership : std::uint8_t
{
    Static,
    Dynamic,
    Absent,
    Inherited,
};

enum class DynamicStateKey : std::uint8_t
{
    Viewport,
    Scissor,
    DepthTestEnable,
    DepthWriteEnable,
    CullMode,
    FrontFace,
    PrimitiveTopology,
};

// Vulkan has 50+ core/extension dynamic states, so an array indexed by
// DynamicStateKey cannot cover future APIs. This registry is keyed by the real
// VkDynamicState value and can grow without changing PassData.
struct StateOwnershipEntry
{
    VkDynamicState state{VK_DYNAMIC_STATE_VIEWPORT};
    StateOwnership ownership{StateOwnership::Static};
};

class StateOwnershipMap
{
  public:
    inline static constexpr std::uint32_t CAPACITY = 16;

    void set(VkDynamicState state, StateOwnership ownership) noexcept
    {
        for (std::uint32_t i = 0; i < count_; ++i)
        {
            if (entries_[i].state == state)
            {
                entries_[i].ownership = ownership;
                return;
            }
        }
        assert(count_ < CAPACITY);
        entries_[count_++] = {.state = state, .ownership = ownership};
    }

    [[nodiscard]] StateOwnership get(VkDynamicState state) const noexcept
    {
        for (std::uint32_t i = 0; i < count_; ++i)
            if (entries_[i].state == state)
                return entries_[i].ownership;
        return StateOwnership::Static;
    }

    [[nodiscard]] bool contains(VkDynamicState state) const noexcept
    {
        for (std::uint32_t i = 0; i < count_; ++i)
            if (entries_[i].state == state)
                return true;
        return false;
    }

    void clear() noexcept { count_ = 0; }
    [[nodiscard]] std::span<const StateOwnershipEntry> entries() const noexcept
    {
        return {entries_.data(), count_};
    }

  private:
    std::array<StateOwnershipEntry, CAPACITY> entries_{};
    std::uint32_t count_{0};
};

struct PipelineBinding
{
    VkPipelineBindPoint bindPoint{VK_PIPELINE_BIND_POINT_GRAPHICS};
    VkPipeline pipeline{};
    VkPipelineLayout layout{};
    PipelineRenderingInfo rendering{};
    StateOwnershipMap ownership{};
};

struct DescriptorBinding
{
    enum class Kind : std::uint8_t
    {
        Set,
        Buffer,
        Heap,
    };

    inline static constexpr std::uint32_t MAX_SETS = 8;
    inline static constexpr std::uint32_t MAX_DYNAMIC_OFFSETS = 32;
    inline static constexpr std::uint32_t MAX_BUFFERS = 8;

    Kind kind{Kind::Set};
    VkPipelineLayout layout{};
    VkBindHeapInfoEXT resourceHeap{};
    VkBindHeapInfoEXT samplerHeap{};
    bool resourceHeapBound{false};
    bool samplerHeapBound{false};

    // Full vkCmdBindDescriptorSets data: multiple sets, firstSet, dynamic
    // offsets. Adding a new descriptor command does not need new fields.
    std::array<VkDescriptorSet, MAX_SETS> sets{};
    std::uint32_t firstSet{0};
    std::uint32_t setCount{0};
    std::array<std::uint32_t, MAX_DYNAMIC_OFFSETS> dynamicOffsets{};
    std::uint32_t dynamicOffsetCount{0};

    // Full vkCmdBindDescriptorBuffersEXT / SetDescriptorBufferOffsetsEXT data.
    std::array<VkDescriptorBufferBindingInfoEXT, MAX_BUFFERS> bufferBindings{};
    std::uint32_t bufferBindingCount{0};
    std::array<std::uint32_t, MAX_BUFFERS> bufferOffsets{};
    std::uint32_t bufferOffsetCount{0};

    // Push-descriptor data. Non-owning: caller keeps the arrays alive for the
    // duration of the record call; PassData does not allocate.
    VkDescriptorSetLayout pushDescriptorSetLayout{};
    std::span<const VkWriteDescriptorSet> pushDescriptorWrites{};
    std::span<const VkDescriptorBufferInfo> pushDescriptorBuffers{};
    std::span<const VkDescriptorImageInfo> pushDescriptorImages{};
    std::span<const VkBufferView> pushDescriptorBufferViews{};
    std::span<const VkAccelerationStructureKHR> pushDescriptorAccelerationStructures{};
};

struct PushConstants
{
    inline static constexpr std::uint32_t MAX_BYTES = 256;

    VkPipelineLayout layout{};
    VkShaderStageFlags stages{0};
    std::uint32_t offset{0};
    std::uint32_t size{0};
    std::array<std::uint8_t, MAX_BYTES> bytes{};
    bool valid{false};

    void setBytes(const void *source, std::size_t count) noexcept
    {
        assert(count <= MAX_BYTES);
        if (count != 0)
            std::memcpy(bytes.data(), source, count);
        size = static_cast<std::uint32_t>(count);
        valid = true;
    }
};

struct VertexInputBinding
{
    inline static constexpr std::uint32_t MAX_BINDINGS = 16;

    VkBuffer indexBuffer{};
    VkDeviceSize indexOffset{0};
    VkIndexType indexType{VK_INDEX_TYPE_UINT32};

    // Full vkCmdBindVertexBuffers / BindVertexBuffers2 data.
    std::array<VkBuffer, MAX_BINDINGS> vertexBuffers{};
    std::array<VkDeviceSize, MAX_BINDINGS> vertexOffsets{};
    std::array<VkDeviceSize, MAX_BINDINGS> vertexSizes{};
    std::array<VkDeviceSize, MAX_BINDINGS> vertexStrides{};
    std::uint32_t firstBinding{0};
    std::uint32_t bindingCount{0};
};

struct DynamicStateValue
{
    inline static constexpr std::uint32_t MAX_BYTES = 128;

    VkDynamicState state{VK_DYNAMIC_STATE_VIEWPORT};
    std::array<std::uint8_t, MAX_BYTES> bytes{};
    std::uint32_t size{0};

    template <typename T> void set(const T &value) noexcept
    {
        static_assert(sizeof(T) <= MAX_BYTES);
        std::memcpy(bytes.data(), &value, sizeof(T));
        size = sizeof(T);
    }

    template <typename T> [[nodiscard]] const T *get() const noexcept
    {
        return size == sizeof(T) ? reinterpret_cast<const T *>(bytes.data())
                                 : nullptr;
    }
};

struct DynamicState
{
    inline static constexpr std::uint32_t CAPACITY = 16;

    std::array<DynamicStateValue, CAPACITY> values{};
    std::uint32_t valueCount{0};

    void setValue(VkDynamicState state, const void *data, std::size_t size) noexcept
    {
        assert(size <= DynamicStateValue::MAX_BYTES);
        for (std::uint32_t i = 0; i < valueCount; ++i)
        {
            if (values[i].state == state)
            {
                std::memcpy(values[i].bytes.data(), data, size);
                values[i].size = static_cast<std::uint32_t>(size);
                return;
            }
        }
        assert(valueCount < CAPACITY);
        auto &value = values[valueCount++];
        value.state = state;
        std::memcpy(value.bytes.data(), data, size);
        value.size = static_cast<std::uint32_t>(size);
    }

    [[nodiscard]] const DynamicStateValue *findValue(VkDynamicState state) const
        noexcept
    {
        for (std::uint32_t i = 0; i < valueCount; ++i)
            if (values[i].state == state)
                return &values[i];
        return nullptr;
    }
};

enum class DrawKind : std::uint8_t
{
    Draw,
    DrawIndexed,
    DrawIndirect,
    DrawIndexedIndirect,
    DrawIndirectCount,
    DrawIndexedIndirectCount,
    DrawMulti,
    DrawMultiIndexed,
    DrawMeshTasks,
    DrawMeshTasksIndirect,
    DrawMeshTasksIndirectCount,
};

struct DrawArgs
{
    DrawKind kind{DrawKind::Draw};
    // direct
    std::uint32_t vertexCount{0};
    std::uint32_t instanceCount{1};
    std::uint32_t firstVertex{0};
    std::uint32_t firstInstance{0};
    std::uint32_t indexCount{0};
    std::uint32_t firstIndex{0};
    std::int32_t vertexOffset{0};
    // indirect
    VkBuffer buffer{};
    VkDeviceSize offset{0};
    std::uint32_t drawCount{0};
    std::uint32_t stride{0};
    VkBuffer countBuffer{};
    VkDeviceSize countOffset{0};
    std::uint32_t maxDrawCount{0};

    // Mesh tasks / multi-draw data.
    std::uint32_t groupCountX{0};
    std::uint32_t groupCountY{0};
    std::uint32_t groupCountZ{0};
    std::span<const VkMultiDrawInfoEXT> multiDraw{};
    std::span<const VkMultiDrawIndexedInfoEXT> multiDrawIndexed{};
};

struct SyncState
{
    // Full barrier data (same shape as VkDependencyInfo). Non-owning views;
    // the command payload owns storage for the duration of the emit.
    VkDependencyFlags dependencyFlags{0};
    std::span<const VkMemoryBarrier2> memoryBarriers{};
    std::span<const VkBufferMemoryBarrier2> bufferBarriers{};
    std::span<const VkImageMemoryBarrier2> imageBarriers{};

    // Event / conditional rendering.
    VkEvent event{};
    VkPipelineStageFlags2 eventStages{0};
    VkConditionalRenderingBeginInfoEXT conditionalRendering{};

    // Query / timestamp.
    VkQueryPool queryPool{};
    std::uint32_t query{0};
    VkQueryControlFlags queryFlags{0};
    VkPipelineStageFlags2 timestampStage{0};

    // Timeline semaphore / fence.
    VkSemaphore semaphore{};
    std::uint64_t semaphoreValue{0};
    VkFence fence{};
};

struct Inheritance
{
    bool enabled{false};
    VkCommandBufferInheritanceInfo info{};
    VkCommandBufferInheritanceRenderingInfo rendering{};
    std::array<VkFormat, MAX_COLOR_ATTACHMENTS> colorFormats{};
    VkCommandBufferInheritanceConditionalRenderingInfoEXT conditionalRendering{};
    VkCommandBufferInheritanceDescriptorHeapInfoEXT descriptorHeap{};
    VkCommandBufferInheritanceViewportScissorInfoNV viewportScissor{};
};

struct FeatureSet
{
    bool dynamicRendering{true};
    bool synchronization2{true};
    bool descriptorIndexing{true};
    bool bufferDeviceAddress{true};
    bool extendedDynamicState{true};
    bool descriptorBuffer{false};
    bool descriptorHeap{false};
    bool dynamicRenderingLocalRead{false};

    struct RawFeature
    {
        VkStructureType sType{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        const void *data{nullptr};
    };

    // Full device capability data. The booleans above are fast predicates;
    // these structs are the complete Vulkan data source. Variable-length
    // extension data is caller-owned (no per-pass heap allocation).
    VkPhysicalDeviceFeatures2 features2{};
    VkPhysicalDeviceVulkan11Features vulkan11{};
    VkPhysicalDeviceVulkan12Features vulkan12{};
    VkPhysicalDeviceVulkan13Features vulkan13{};
    VkPhysicalDeviceVulkan14Features vulkan14{};
    VkPhysicalDeviceProperties properties{};
    const char *const *extensionNames{nullptr};
    std::uint32_t extensionCount{0};
    const void *extensionFeatureChain{nullptr};
};

// Query the features this app actually enables at device creation.
// descriptorBuffer / descriptorHeap / dynamicRenderingLocalRead are not in
// this app's enable chain, so they stay false until the app enables them too.
// (Querying support would be wrong here: support != enabled.)
template <typename PhysicalDevice>
FeatureSet query_features(const PhysicalDevice &physical)
{
    FeatureSet result{};

    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extended{
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT,
        .pNext = nullptr,
        .extendedDynamicState = VK_FALSE,
    };
    VkPhysicalDeviceVulkan12Features vulkan12{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &extended,
    };
    VkPhysicalDeviceVulkan13Features vulkan13{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &vulkan12,
    };
    VkPhysicalDeviceFeatures2 core{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &vulkan13,
    };
    physical.getFeatures2(&core);

    result.dynamicRendering = vulkan13.dynamicRendering == VK_TRUE;
    result.synchronization2 = vulkan13.synchronization2 == VK_TRUE;
    result.bufferDeviceAddress = vulkan12.bufferDeviceAddress == VK_TRUE;
    result.descriptorIndexing = vulkan12.descriptorIndexing == VK_TRUE;
    result.extendedDynamicState = extended.extendedDynamicState == VK_TRUE;
    result.dynamicRenderingLocalRead = false;
    result.descriptorBuffer = false;
    result.descriptorHeap = false;
    return result;
}

struct TransferState
{
    VkBuffer srcBuffer{};
    VkBuffer dstBuffer{};
    VkImage srcImage{};
    VkImage dstImage{};
    VkImageLayout srcLayout{VK_IMAGE_LAYOUT_UNDEFINED};
    VkImageLayout dstLayout{VK_IMAGE_LAYOUT_UNDEFINED};
    VkFilter filter{VK_FILTER_NEAREST};

    std::span<const VkBufferCopy2> bufferCopies{};
    std::span<const VkImageCopy2> imageCopies{};
    std::span<const VkBufferImageCopy2> bufferImageCopies{};
    std::span<const VkImageBlit2> blits{};
    std::span<const VkImageResolve2> resolves{};
};

// Classic render-pass data, kept separate from dynamic-rendering PassTarget.
struct RenderPassTarget
{
    VkRenderPass renderPass{};
    VkFramebuffer framebuffer{};
    VkRect2D renderArea{};
    std::span<const VkClearValue> clearValues{};
    VkSubpassContents contents{VK_SUBPASS_CONTENTS_INLINE};
};

// Frame/queue submission data: the system layer above PassData.
struct QueueState
{
    VkQueue queue{};
    std::uint32_t familyIndex{0};
    std::uint32_t queueIndex{0};
    VkQueueFlags flags{0};
    std::uint32_t queueCount{0};
    float priority{1.0F};
    VkQueueFamilyProperties properties{};
};

struct SubmissionState
{
    std::span<const VkSemaphoreSubmitInfo> waitSemaphores{};
    std::span<const VkSemaphoreSubmitInfo> signalSemaphores{};
    std::span<const VkCommandBufferSubmitInfo> commandBuffers{};
    VkSubmitFlags flags{0};
    VkFence fence{};
};

struct ClearState
{
    std::span<const VkClearAttachment> attachments{};
    std::span<const VkClearRect> rects{};
};

struct QueryState
{
    VkQueryPool pool{};
    std::uint32_t query{0};
    std::uint32_t queryIndex{0};
    VkQueryControlFlags controlFlags{0};
    VkQueryResultFlags resultFlags{0};
    VkPipelineStageFlags2 stage{0};
    VkBuffer dstBuffer{};
    VkDeviceSize dstOffset{0};
    VkDeviceSize dstStride{0};
};

struct DebugState
{
    VkDebugUtilsLabelEXT label{};
    VkDebugUtilsObjectNameInfoEXT objectName{};
};

struct AcquireState
{
    VkSwapchainKHR swapchain{};
    std::uint64_t timeout{0};
    VkSemaphore semaphore{};
    VkFence fence{};
    std::uint32_t imageIndex{0};
};

struct PresentState
{
    std::span<const VkSemaphore> waitSemaphores{};
    std::span<const VkSwapchainKHR> swapchains{};
    std::span<const std::uint32_t> imageIndices{};
    std::span<const VkResult> results{};
};

struct CommandBufferState
{
    VkCommandBufferLevel level{VK_COMMAND_BUFFER_LEVEL_PRIMARY};
    VkCommandBufferUsageFlags usage{0};
    VkCommandBufferInheritanceInfo inheritance{};
};

// Escape hatch for vendor/future commands: raw argument bytes + entry point.
// Adding such an API does not require a new PassData field.
struct RawCommandData
{
    const char *name{nullptr};
    PFN_vkVoidFunction function{nullptr};
    std::span<const std::byte> arguments{};
};

struct FrameData
{
    QueueState queue{};
    SubmissionState submission{};
    AcquireState acquire{};
    PresentState present{};
    CommandBufferState commandBuffer{};
};

// ---- the core pass state record -----------------------------------------
// Command-specific data (transfer/classic/clear/query/debug/raw/frame) lives
// in its own structs and OpPayloads, not in the per-pass state cache.
struct PassData
{
    PassTarget target{};
    PipelineBinding pipeline{};
    DescriptorBinding descriptors{};
    PushConstants pushConstants{};
    VertexInputBinding vertexInput{};
    DynamicState dynamicState{};
    DrawArgs draw{};
    SyncState sync{};
    Inheritance inheritance{};
    const FeatureSet *features{nullptr};
    bool passActive{false};
};

} // namespace pass_data
