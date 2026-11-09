#pragma once
// ============================================================================
// test_intent8_draw_pipeline.hpp
//
// The new intent8 D model:
//
//   D = ConfigStore + ResourceTable
//
//   ConfigStore   : pipeline config, shader interface, descriptor layout,
//                   render target, dynamic state, feature/format.
//   ResourceTable : real VkPipeline / VkDescriptorSet / VkImage / VkBuffer /
//                   VkSampler / VkRenderPass / VkFramebuffer + state.
//
// The central view is DrawPipelineData:
//
//   DrawPipelineData = PipelineConfig + PipelineResource
//                    + DescriptorLayout + RenderTarget + DrawResources
//
// A DrawCall is an operation group that consumes DrawPipelineData:
//
//   bind pipeline
//   bind descriptor set
//   push constants
//   bind index / vertex / indirect
//   draw
//
// This header is data only. It records nothing and emits nothing.
// ============================================================================

#include "../head.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace intent8
{

inline constexpr std::uint32_t MAX_COLOR_ATTACHMENTS = 8;
inline constexpr std::uint32_t MAX_PUSH_CONSTANT_BYTES = 256;
inline constexpr std::uint32_t MAX_SHADER_STAGES = 5;
inline constexpr std::uint32_t MAX_DYNAMIC_STATES = 16;
inline constexpr std::uint32_t MAX_VERTEX_BINDINGS = 8;
inline constexpr std::uint32_t MAX_BARRIERS = 8;

enum class TargetKind : std::uint8_t
{
    DynamicRendering,
    ClassicRenderPass,
};

// ---------------------------------------------------------------------------
// ConfigStore: dynamic state
// ---------------------------------------------------------------------------
class DynamicStateConfig
{
  public:
    constexpr void add(VkDynamicState state) noexcept
    {
        for (std::uint32_t i = 0; i < count_; ++i)
            if (states_[i] == state)
                return;
        assert(count_ < MAX_DYNAMIC_STATES);
        states_[count_++] = state;
    }

    [[nodiscard]] constexpr bool has(VkDynamicState state) const noexcept
    {
        for (std::uint32_t i = 0; i < count_; ++i)
            if (states_[i] == state)
                return true;
        return false;
    }

    [[nodiscard]] constexpr std::span<const VkDynamicState> states() const noexcept
    {
        return {states_.data(), count_};
    }
    [[nodiscard]] constexpr std::uint32_t size() const noexcept { return count_; }

  private:
    std::array<VkDynamicState, MAX_DYNAMIC_STATES> states_{};
    std::uint32_t count_{0};
};

// ---------------------------------------------------------------------------
// ConfigStore: programmable stages
// ---------------------------------------------------------------------------
struct ShaderStageConfig
{
    VkShaderStageFlagBits stage{VK_SHADER_STAGE_VERTEX_BIT};
    VkShaderModule module{};
    const char *entryPoint{"main"};
};

struct ShaderProgram
{
    std::array<ShaderStageConfig, MAX_SHADER_STAGES> stages{};
    std::uint32_t stageCount{0};

    VkShaderStageFlags pushConstantStages{0};
    std::uint32_t pushConstantOffset{0};
    std::uint32_t pushConstantSize{0};

    constexpr void addStage(const ShaderStageConfig &s) noexcept
    {
        assert(stageCount < MAX_SHADER_STAGES);
        for (std::uint32_t i = 0; i < stageCount; ++i)
            assert(stages[i].stage != s.stage);
        stages[stageCount++] = s;
    }

    [[nodiscard]] const ShaderStageConfig *stage(VkShaderStageFlagBits s) const noexcept
    {
        for (std::uint32_t i = 0; i < stageCount; ++i)
            if (stages[i].stage == s)
                return &stages[i];
        return nullptr;
    }
};

// ---------------------------------------------------------------------------
// ConfigStore: pipeline / target interface compatibility
// ---------------------------------------------------------------------------
struct PipelineRenderingConfig
{
    std::array<VkFormat, MAX_COLOR_ATTACHMENTS> colorFormats{};
    std::uint32_t colorCount{0};
    VkFormat depthFormat{VK_FORMAT_UNDEFINED};
    VkFormat stencilFormat{VK_FORMAT_UNDEFINED};
    VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};
    std::uint32_t viewMask{0};

    [[nodiscard]] bool matchesTargetFormats(const PipelineRenderingConfig &t) const
        noexcept
    {
        if (colorCount != t.colorCount)
            return false;
        for (std::uint32_t i = 0; i < colorCount; ++i)
        {
            const VkFormat a = colorFormats[i];
            const VkFormat b = t.colorFormats[i];
            if (a != VK_FORMAT_UNDEFINED && b != VK_FORMAT_UNDEFINED && a != b)
                return false;
        }
        if (depthFormat != VK_FORMAT_UNDEFINED && t.depthFormat != VK_FORMAT_UNDEFINED &&
            depthFormat != t.depthFormat)
            return false;
        if (stencilFormat != VK_FORMAT_UNDEFINED &&
            t.stencilFormat != VK_FORMAT_UNDEFINED && stencilFormat != t.stencilFormat)
            return false;
        if (viewMask != t.viewMask)
            return false;
        if (samples != t.samples)
            return false;
        return true;
    }
};

struct PipelineConfig
{
    VkPipelineBindPoint bindPoint{VK_PIPELINE_BIND_POINT_GRAPHICS};
    ShaderProgram shaders{};
    VkPipelineLayout layout{};
    PipelineRenderingConfig rendering{};
    DynamicStateConfig dynamicState{};
    VkPipelineCreateFlags flags{0};

    // Captured fixed-function facts used by the validator.
    bool depthTestEnable{false};
    bool depthWriteEnable{false};
    bool stencilTestEnable{false};
    std::uint32_t colorAttachmentCount{0};
    std::uint32_t blendAttachmentCount{0};
};

struct PipelineResource
{
    VkPipeline pipeline{};
    VkPipelineLayout layout{};

    [[nodiscard]] bool valid() const noexcept
    {
        return pipeline != VK_NULL_HANDLE && layout != VK_NULL_HANDLE;
    }
};

// ---------------------------------------------------------------------------
// ConfigStore: descriptor set layout + ResourceTable: descriptor set
// ---------------------------------------------------------------------------
struct DescriptorSetLayoutConfig
{
    std::span<const VkDescriptorSetLayoutBinding> bindings{};
    VkDescriptorSetLayoutCreateFlags flags{0};
};

struct DescriptorLayout
{
    VkDescriptorSetLayout layout{};
    std::span<const VkDescriptorSetLayoutBinding> bindings{};
    VkDescriptorSetLayoutCreateFlags flags{0};

    [[nodiscard]] const VkDescriptorSetLayoutBinding *binding(
        std::uint32_t index) const noexcept
    {
        for (const auto &b : bindings)
            if (b.binding == index)
                return &b;
        return nullptr;
    }

    [[nodiscard]] bool valid() const noexcept { return layout != VK_NULL_HANDLE; }
};

struct DescriptorSetResources
{
    VkDescriptorSet set{};
    const DescriptorLayout *layout{nullptr};
};

// ---------------------------------------------------------------------------
// RenderTarget: config (formats / load-store / clear) + resources (views)
// ---------------------------------------------------------------------------
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

struct RenderTargetConfig
{
    TargetKind kind{TargetKind::DynamicRendering};
    VkRect2D renderArea{};
    std::uint32_t layerCount{1};
    std::uint32_t viewMask{0};
    VkRenderingFlags flags{0};

    std::array<VkFormat, MAX_COLOR_ATTACHMENTS> colorFormats{};
    std::uint32_t colorCount{0};
    VkFormat depthFormat{VK_FORMAT_UNDEFINED};
    VkFormat stencilFormat{VK_FORMAT_UNDEFINED};
    VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};

    // Classic render pass path (config + resources).
    std::array<VkClearValue, MAX_COLOR_ATTACHMENTS + 1> clearValues{};
    std::uint32_t clearValueCount{0};
    VkSubpassContents contents{VK_SUBPASS_CONTENTS_INLINE};

    [[nodiscard]] PipelineRenderingConfig rendering() const noexcept
    {
        PipelineRenderingConfig result{};
        result.colorFormats = colorFormats;
        result.colorCount = colorCount;
        result.depthFormat = depthFormat;
        result.stencilFormat = stencilFormat;
        result.samples = samples;
        result.viewMask = viewMask;
        return result;
    }
};

struct RenderTargetResources
{
    std::array<VkImageView, MAX_COLOR_ATTACHMENTS> colorViews{};
    std::array<VkImageView, MAX_COLOR_ATTACHMENTS> resolveViews{};
    VkImageView depthView{};
    VkImageView stencilView{};
    VkRenderPass renderPass{};
    VkFramebuffer framebuffer{};
};

// RenderTarget owns configuration values (clear values, formats, area) and
// references attachment resources. The resolved VkRenderingAttachmentInfo
// array is the exact data handed to vkCmdBeginRendering.
struct RenderTarget
{
    RenderTargetConfig config{};
    RenderTargetResources resources{};

    std::array<VkRenderingAttachmentInfo, MAX_COLOR_ATTACHMENTS> colors{};
    std::uint32_t colorCount{0};
    VkRenderingAttachmentInfo depth{};
    VkRenderingAttachmentInfo stencil{};
    bool hasDepth{false};
    bool hasStencil{false};

    void setColorFormat(std::uint32_t index, VkFormat format) noexcept
    {
        assert(index < MAX_COLOR_ATTACHMENTS);
        config.colorFormats[index] = format;
        if (config.colorCount <= index)
            config.colorCount = index + 1;
    }

    void addColor(const VkRenderingAttachmentInfo &attachment) noexcept
    {
        assert(colorCount < MAX_COLOR_ATTACHMENTS);
        colors[colorCount] = attachment;
        resources.colorViews[colorCount] = attachment.imageView;
        resources.resolveViews[colorCount] = attachment.resolveImageView;
        if (config.colorCount < colorCount + 1)
            config.colorCount = colorCount + 1;
        ++colorCount;
    }

    void setColor(std::uint32_t index, const VkRenderingAttachmentInfo &attachment) noexcept
    {
        assert(index < MAX_COLOR_ATTACHMENTS);
        colors[index] = attachment;
        resources.colorViews[index] = attachment.imageView;
        resources.resolveViews[index] = attachment.resolveImageView;
        if (colorCount <= index)
            colorCount = index + 1;
        if (config.colorCount <= index)
            config.colorCount = index + 1;
    }

    void setDepth(const VkRenderingAttachmentInfo &attachment) noexcept
    {
        depth = attachment;
        resources.depthView = attachment.imageView;
        hasDepth = true;
    }

    void setStencil(const VkRenderingAttachmentInfo &attachment) noexcept
    {
        stencil = attachment;
        resources.stencilView = attachment.imageView;
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

    [[nodiscard]] RenderTarget withLoad(VkAttachmentLoadOp op) const noexcept
    {
        RenderTarget result = *this;
        result.setLoadOp(op);
        return result;
    }

    [[nodiscard]] RenderTarget withStore(VkAttachmentStoreOp op) const noexcept
    {
        RenderTarget result = *this;
        result.setStoreOp(op);
        return result;
    }

    [[nodiscard]] VkRenderingInfo info() const noexcept
    {
        VkRenderingInfo result{};
        result.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        result.flags = config.flags;
        result.renderArea = config.renderArea;
        result.layerCount = config.layerCount;
        result.viewMask = config.viewMask;
        result.colorAttachmentCount = colorCount;
        result.pColorAttachments = colorCount == 0 ? nullptr : colors.data();
        result.pDepthAttachment = hasDepth ? &depth : nullptr;
        result.pStencilAttachment = hasStencil ? &stencil : nullptr;
        return result;
    }

    [[nodiscard]] VkRenderPassBeginInfo classicInfo() const noexcept
    {
        VkRenderPassBeginInfo result{};
        result.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        result.renderPass = resources.renderPass;
        result.framebuffer = resources.framebuffer;
        result.renderArea = config.renderArea;
        result.clearValueCount = config.clearValueCount;
        result.pClearValues =
            config.clearValueCount == 0 ? nullptr : config.clearValues.data();
        return result;
    }
};

// ---------------------------------------------------------------------------
// DrawResources: everything the draw operation needs
// ---------------------------------------------------------------------------
struct PushConstantBlock
{
    VkPipelineLayout layout{};
    VkShaderStageFlags stages{0};
    std::uint32_t offset{0};
    std::uint32_t size{0};
    std::array<std::uint8_t, MAX_PUSH_CONSTANT_BYTES> bytes{};

    void set(const void *data, std::size_t byteCount) noexcept
    {
        assert(byteCount <= MAX_PUSH_CONSTANT_BYTES);
        if (byteCount != 0 && data != nullptr)
            std::memcpy(bytes.data(), data, byteCount);
        size = static_cast<std::uint32_t>(byteCount);
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return layout != VK_NULL_HANDLE && size != 0;
    }
};

enum class DrawKind : std::uint8_t
{
    Draw,
    DrawIndexed,
    DrawIndirect,
    DrawIndexedIndirect,
    DrawIndexedIndirectCount,
};

struct DrawArgs
{
    DrawKind kind{DrawKind::DrawIndexedIndirect};

    // direct
    std::uint32_t vertexCount{0};
    std::uint32_t instanceCount{1};
    std::uint32_t firstVertex{0};
    std::uint32_t firstInstance{0};
    std::uint32_t indexCount{0};
    std::uint32_t firstIndex{0};
    std::int32_t vertexOffset{0};

    // indirect
    VkBuffer indirectBuffer{};
    VkDeviceSize indirectOffset{0};
    std::uint32_t drawCount{0};
    std::uint32_t stride{0};
    VkBuffer countBuffer{};
    VkDeviceSize countOffset{0};
    std::uint32_t maxDrawCount{0};
};

struct DrawResources
{
    DescriptorSetResources descriptors{};

    VkBuffer indexBuffer{};
    VkDeviceSize indexOffset{0};
    VkIndexType indexType{VK_INDEX_TYPE_UINT32};

    VkBuffer vertexBuffer{};
    VkDeviceSize vertexOffset{0};

    VkBuffer indirectBuffer{};
    VkDeviceSize indirectOffset{0};
    std::uint32_t drawCount{0};
    std::uint32_t stride{0};

    VkViewport viewport{};
    VkRect2D scissor{};
    bool dynamicViewport{true};
    bool dynamicScissor{true};

    PushConstantBlock pushConstants{};

    [[nodiscard]] bool valid() const noexcept
    {
        return descriptors.set != VK_NULL_HANDLE;
    }
};

// ---------------------------------------------------------------------------
// Global config: stable IDs / stable objects (lifetime + addressability)
// ---------------------------------------------------------------------------
struct DrawPipeline
{
    std::string_view name{};
    const PipelineConfig *config{nullptr};
    const PipelineResource *resource{nullptr};
    const DescriptorLayout *descriptorLayout{nullptr};

    [[nodiscard]] bool valid() const noexcept
    {
        return config != nullptr && resource != nullptr && resource->valid();
    }

    [[nodiscard]] VkPipeline pipeline() const noexcept
    {
        return resource == nullptr ? VK_NULL_HANDLE : resource->pipeline;
    }

    [[nodiscard]] VkPipelineLayout layout() const noexcept
    {
        return resource == nullptr ? VK_NULL_HANDLE : resource->layout;
    }
};

// ---------------------------------------------------------------------------
// DrawPipelineData: the logical view over config + resources
//
//   = PipelineConfig + PipelineResource + DescriptorLayout
//   + RenderTarget + DrawResources
//
// It is not one giant owning struct: every pointer names a real object that
// lives in GlobalConfig / GlobalResources / the frame recorder.
// ---------------------------------------------------------------------------
struct DrawPipelineData
{
    const DrawPipeline *pipeline{nullptr};
    const RenderTarget *target{nullptr};
    const DrawResources *resources{nullptr};

    [[nodiscard]] const PipelineConfig *config() const noexcept
    {
        return pipeline == nullptr ? nullptr : pipeline->config;
    }
    [[nodiscard]] const PipelineResource *resource() const noexcept
    {
        return pipeline == nullptr ? nullptr : pipeline->resource;
    }
    [[nodiscard]] const DescriptorLayout *descriptorLayout() const noexcept
    {
        return pipeline == nullptr ? nullptr : pipeline->descriptorLayout;
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return pipeline != nullptr && pipeline->valid() && resources != nullptr &&
               resources->valid();
    }
};

// A draw call is an operation group consuming one DrawPipelineData view.
struct DrawCall
{
    const DrawPipelineData *pipelineData{nullptr};
    DrawArgs args{};
};

struct PipelineEntry
{
    PipelineConfig config{};
    PipelineResource resource{};
    const DescriptorLayout *descriptorLayout{nullptr};
    std::string name{};
    DrawPipeline view{};
};

class PipelineTable
{
  public:
    const DrawPipeline *add(std::string name, PipelineConfig config,
                            PipelineResource resource,
                            const DescriptorLayout *descriptorLayout)
    {
        entries_.push_back(PipelineEntry{.config = std::move(config),
                                         .resource = resource,
                                         .descriptorLayout = descriptorLayout,
                                         .name = std::move(name)});
        auto &entry = entries_.back();
        entry.view = DrawPipeline{.name = entry.name,
                                  .config = &entry.config,
                                  .resource = &entry.resource,
                                  .descriptorLayout = entry.descriptorLayout};
        return &entry.view;
    }

    [[nodiscard]] const DrawPipeline *find(VkPipeline pipeline) const noexcept
    {
        if (pipeline == VK_NULL_HANDLE)
            return nullptr;
        for (const auto &entry : entries_)
            if (entry.resource.pipeline == pipeline)
                return &entry.view;
        return nullptr;
    }

    [[nodiscard]] const DrawPipeline *find(std::string_view name) const noexcept
    {
        for (const auto &entry : entries_)
            if (std::string_view(entry.name) == name)
                return &entry.view;
        return nullptr;
    }

    [[nodiscard]] const std::deque<PipelineEntry> &entries() const noexcept
    {
        return entries_;
    }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

  private:
    std::deque<PipelineEntry> entries_{};
};

struct DescriptorLayoutEntry
{
    std::vector<VkDescriptorSetLayoutBinding> storage{};
    DescriptorLayout view{};
};

class DescriptorLayoutTable
{
  public:
    const DescriptorLayout *add(VkDescriptorSetLayout layout,
                                std::span<const VkDescriptorSetLayoutBinding> bindings,
                                VkDescriptorSetLayoutCreateFlags flags = 0)
    {
        DescriptorLayoutEntry entry{};
        entry.storage.assign(bindings.begin(), bindings.end());
        entry.view = DescriptorLayout{.layout = layout,
                                      .bindings = {entry.storage.data(),
                                                   entry.storage.size()},
                                      .flags = flags};
        entries_.push_back(std::move(entry));
        return &entries_.back().view;
    }

    [[nodiscard]] const DescriptorLayout *find(VkDescriptorSetLayout layout) const
        noexcept
    {
        for (const auto &entry : entries_)
            if (entry.view.layout == layout)
                return &entry.view;
        return nullptr;
    }

  private:
    std::deque<DescriptorLayoutEntry> entries_{};
};

struct ShaderModuleRecord
{
    VkShaderModule module{};
    VkShaderStageFlagBits stage{VK_SHADER_STAGE_VERTEX_BIT};
    std::string path{};
    std::string entryPoint{"main"};
};

class ShaderLibrary
{
  public:
    const ShaderModuleRecord *load(const mcs::vulkan::LogicalDevice &device,
                                   const std::string &path,
                                   VkShaderStageFlagBits stage,
                                   const char *entryPoint = "main")
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
            return nullptr;
        const std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        if (size <= 0 || (size % 4) != 0)
            return nullptr;

        std::vector<std::uint32_t> code(static_cast<std::size_t>(size) / 4U);
        if (!file.read(reinterpret_cast<char *>(code.data()), size))
            return nullptr;

        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = code.size() * sizeof(std::uint32_t);
        info.pCode = code.data();

        const VkShaderModule module =
            device.createShaderModule(info, device.allocator());
        if (module == VK_NULL_HANDLE)
            return nullptr;

        modules_.push_back(ShaderModuleRecord{.module = module,
                                              .stage = stage,
                                              .path = path,
                                              .entryPoint = entryPoint});
        return &modules_.back();
    }

    void destroy(const mcs::vulkan::LogicalDevice &device)
    {
        for (const auto &record : modules_)
            if (record.module != VK_NULL_HANDLE)
                device.destroyShaderModule(record.module, device.allocator());
        modules_.clear();
    }

    [[nodiscard]] std::size_t size() const noexcept { return modules_.size(); }
    [[nodiscard]] const std::deque<ShaderModuleRecord> &modules() const noexcept
    {
        return modules_;
    }

  private:
    std::deque<ShaderModuleRecord> modules_{};
};

struct DeviceCapabilities
{
    std::uint32_t apiVersion{0};
    VkPhysicalDeviceProperties properties{};
    bool dynamicRendering{false};
    bool synchronization2{false};
    bool bufferDeviceAddress{false};
    bool descriptorIndexing{false};
    bool extendedDynamicState{false};
    bool multiDrawIndirect{false};
    bool independentBlend{false};
};

struct FeatureSet
{
    bool dynamicRendering{true};
    bool synchronization2{true};
    bool bufferDeviceAddress{true};
    bool multiDrawIndirect{true};
};

struct GlobalConfig
{
    DeviceCapabilities capabilities{};
    ShaderLibrary shaders{};
    DescriptorLayoutTable descriptorLayouts{};
    PipelineTable pipelines{};
    FeatureSet features{};
};

// ---------------------------------------------------------------------------
// ResourceTable: real handles + metadata + current state (non-owning)
// ---------------------------------------------------------------------------
enum class ResourceKind : std::uint8_t
{
    Buffer,
    Image,
    ImageView,
    Sampler,
    DescriptorSet,
    Pipeline,
    PipelineLayout,
    RenderPass,
    Framebuffer,
    CommandBuffer,
    Swapchain,
};

union ResourceHandle
{
    VkBuffer buffer;
    VkImage image;
    VkImageView imageView;
    VkSampler sampler;
    VkDescriptorSet descriptorSet;
    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;
    VkRenderPass renderPass;
    VkFramebuffer framebuffer;
    VkCommandBuffer commandBuffer;
    VkSwapchainKHR swapchain;
    std::uint64_t raw;

    constexpr ResourceHandle() noexcept : raw{0} {}
};

struct ImageConfig
{
    VkFormat format{VK_FORMAT_UNDEFINED};
    VkExtent3D extent{};
    VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};
    VkImageUsageFlags usage{0};
    VkImageAspectFlags aspect{VK_IMAGE_ASPECT_COLOR_BIT};
    VkImageLayout initialLayout{VK_IMAGE_LAYOUT_UNDEFINED};
    const char *name{nullptr};
};

struct BufferConfig
{
    VkDeviceSize size{0};
    VkBufferUsageFlags usage{0};
    VkMemoryPropertyFlags memory{0};
    const char *name{nullptr};
};

struct ResourceRecord
{
    std::uint64_t id{0};
    ResourceKind kind{ResourceKind::Buffer};
    const void *config{nullptr};
    ResourceHandle handle{};
    ImageConfig imageConfig{};
    BufferConfig bufferConfig{};
    bool hasImageConfig{false};
    bool hasBufferConfig{false};
    VkImageLayout layout{VK_IMAGE_LAYOUT_UNDEFINED};
    VkAccessFlags2 access{0};
    VkPipelineStageFlags2 stage{VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT};
};

class ResourceTable
{
  public:
    std::uint64_t addBuffer(std::uint64_t id, VkBuffer buffer,
                            const BufferConfig &config = {})
    {
        ResourceRecord record{};
        record.id = id;
        record.kind = ResourceKind::Buffer;
        record.handle.buffer = buffer;
        record.bufferConfig = config;
        record.hasBufferConfig = true;
        records_.push_back(record);
        return id;
    }

    std::uint64_t addImage(std::uint64_t id, VkImage image,
                           const ImageConfig &config = {})
    {
        ResourceRecord record{};
        record.id = id;
        record.kind = ResourceKind::Image;
        record.handle.image = image;
        record.imageConfig = config;
        record.hasImageConfig = true;
        record.layout = config.initialLayout;
        record.stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        records_.push_back(record);
        return id;
    }

    // Resize/fullscreen replaces the real handle behind the same stable ID.
    void upsertImage(std::uint64_t id, VkImage image, const ImageConfig &config)
    {
        if (auto *record = mutableFind(id); record != nullptr)
        {
            record->handle.image = image;
            record->imageConfig = config;
            record->hasImageConfig = true;
            return;
        }
        addImage(id, image, config);
    }

    std::uint64_t addImageView(std::uint64_t id, VkImageView view,
                               const void *config = nullptr)
    {
        ResourceRecord record{};
        record.id = id;
        record.kind = ResourceKind::ImageView;
        record.config = config;
        record.handle.imageView = view;
        records_.push_back(record);
        return id;
    }

    void upsertImageView(std::uint64_t id, VkImageView view)
    {
        if (auto *record = mutableFind(id); record != nullptr)
        {
            record->handle.imageView = view;
            return;
        }
        addImageView(id, view);
    }

    std::uint64_t addSampler(std::uint64_t id, VkSampler sampler,
                             const void *config = nullptr)
    {
        ResourceRecord record{};
        record.id = id;
        record.kind = ResourceKind::Sampler;
        record.config = config;
        record.handle.sampler = sampler;
        records_.push_back(record);
        return id;
    }

    std::uint64_t addDescriptorSet(std::uint64_t id, VkDescriptorSet set,
                                   const DescriptorLayout *layout)
    {
        ResourceRecord record{};
        record.id = id;
        record.kind = ResourceKind::DescriptorSet;
        record.config = layout;
        record.handle.descriptorSet = set;
        records_.push_back(record);
        return id;
    }

    std::uint64_t addSwapchain(std::uint64_t id, VkSwapchainKHR swapchain)
    {
        ResourceRecord record{};
        record.id = id;
        record.kind = ResourceKind::Swapchain;
        record.handle.swapchain = swapchain;
        records_.push_back(record);
        return id;
    }

    std::uint64_t addPipeline(std::uint64_t id, VkPipeline pipeline,
                              const PipelineConfig *config = nullptr)
    {
        ResourceRecord record{};
        record.id = id;
        record.kind = ResourceKind::Pipeline;
        record.config = config;
        record.handle.pipeline = pipeline;
        records_.push_back(record);
        return id;
    }

    std::uint64_t addRenderPass(std::uint64_t id, VkRenderPass renderPass,
                                const RenderTargetConfig *config = nullptr)
    {
        ResourceRecord record{};
        record.id = id;
        record.kind = ResourceKind::RenderPass;
        record.config = config;
        record.handle.renderPass = renderPass;
        records_.push_back(record);
        return id;
    }

    std::uint64_t addFramebuffer(std::uint64_t id, VkFramebuffer framebuffer,
                                 const RenderTargetResources *config = nullptr)
    {
        ResourceRecord record{};
        record.id = id;
        record.kind = ResourceKind::Framebuffer;
        record.config = config;
        record.handle.framebuffer = framebuffer;
        records_.push_back(record);
        return id;
    }

    void updateImage(std::uint64_t id, VkImageLayout layout, VkAccessFlags2 access,
                     VkPipelineStageFlags2 stage) noexcept
    {
        if (auto *record = mutableFind(id); record != nullptr)
        {
            record->layout = layout;
            record->access = access;
            record->stage = stage;
        }
    }

    [[nodiscard]] const ResourceRecord *find(std::uint64_t id) const noexcept
    {
        for (const auto &record : records_)
            if (record.id == id)
                return &record;
        return nullptr;
    }

    [[nodiscard]] const ResourceRecord *findImage(VkImage image) const noexcept
    {
        for (const auto &record : records_)
            if (record.kind == ResourceKind::Image && record.handle.image == image)
                return &record;
        return nullptr;
    }

    [[nodiscard]] const ResourceRecord *findBuffer(VkBuffer buffer) const noexcept
    {
        for (const auto &record : records_)
            if (record.kind == ResourceKind::Buffer && record.handle.buffer == buffer)
                return &record;
        return nullptr;
    }

    [[nodiscard]] const std::deque<ResourceRecord> &records() const noexcept
    {
        return records_;
    }
    [[nodiscard]] std::size_t size() const noexcept { return records_.size(); }

  private:
    ResourceRecord *mutableFind(std::uint64_t id) noexcept
    {
        for (auto &record : records_)
            if (record.id == id)
                return &record;
        return nullptr;
    }

    std::deque<ResourceRecord> records_{};
};

struct GlobalResources
{
    VkDevice device{VK_NULL_HANDLE};
    VkQueue queue{VK_NULL_HANDLE};
    std::uint32_t queueFamily{0};
    std::uint32_t frameCount{0};
    std::array<DescriptorSetResources, 8> frameSets{};
    ResourceTable resources{};
};

// ---------------------------------------------------------------------------
// DrawContext: non-owning view of everything a recorded draw needs
// ---------------------------------------------------------------------------
struct DrawContext
{
    const GlobalConfig *config{nullptr};
    const GlobalResources *resources{nullptr};

    VkExtent2D extent{};
    std::uint32_t frameIndex{0};
    std::uint32_t imageIndex{0};
    VkDescriptorSet descriptorSet{VK_NULL_HANDLE};

    const RenderTarget *mainTarget{nullptr};

    [[nodiscard]] const DrawPipeline *pipeline(std::string_view name) const noexcept
    {
        return config == nullptr ? nullptr : config->pipelines.find(name);
    }

    [[nodiscard]] const DrawPipeline *pipeline(VkPipeline handle) const noexcept
    {
        return config == nullptr ? nullptr : config->pipelines.find(handle);
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return config != nullptr && resources != nullptr &&
               resources->device != VK_NULL_HANDLE;
    }
};

// Helpers used by the app when capturing views of init-created objects.
inline PipelineConfig make_pipeline_config(
    VkPipelineLayout layout, const ShaderProgram &program,
    const PipelineRenderingConfig &rendering, const DynamicStateConfig &dynamic,
    VkPipelineCreateFlags flags = 0)
{
    PipelineConfig result{};
    result.bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    result.shaders = program;
    result.layout = layout;
    result.rendering = rendering;
    result.dynamicState = dynamic;
    result.flags = flags;
    result.colorAttachmentCount = rendering.colorCount;
    result.blendAttachmentCount = rendering.colorCount;
    return result;
}

} // namespace intent8
