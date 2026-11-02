#pragma once
// ============================================================================
// ui_shader_device.hpp —— shader 侧的服务对象（内部头，只给后端用）
//
//   它同时是：
//     · shader 数据的拥有者：程序表 / ABI 表 / 三个堆 / arena
//     · 无状态解码器 decode() 的 Sink：Handle → Vk 句柄
//
//   两个 app 用**同一个结构体名**、同一份实现；差异全部由 InitInfo 注入。
//   它不认识 Widget，也不认识 Canvas —— 只吃 List。
// ============================================================================
#include "ui_backend.hpp"
#include "ui_protocol.hpp"
#include "ui_shader_abi.hpp"

#include <array>

namespace ui_gfx
{
    // 类型擦除的 host-visible 堆
    struct heap_ref
    {
        void *self{};
        void (*write_fn)(void *, uint64_t, const void *, uint64_t){};
        uint64_t address{};
        VkBuffer buffer{};

        void write(uint64_t off, const void *src, uint64_t n) const
        {
            if (n != 0)
                write_fn(self, off, src, n);
        }
        template <class H>
        static heap_ref of(H &h) // 只需要 buffer（间接命令）
        {
            return heap_ref{&h,
                            +[](void *p, uint64_t o, const void *s, uint64_t n) {
                                static_cast<H *>(p)->write(o, s, n);
                            },
                            0, h.buffer.buffer()};
        }
        template <class H>
        static heap_ref of_addr(H &h) // 需要设备地址（实例堆 / 常量堆）
        {
            return heap_ref{&h,
                            +[](void *p, uint64_t o, const void *s, uint64_t n) {
                                static_cast<H *>(p)->write(o, s, n);
                            },
                            h.address, h.buffer.buffer()};
        }
    };

    // 资源表：id → (image, view)。协议里只出现 id。
    struct resource_desc
    {
        VkImage image{};
        VkImageView view{};
    };

    struct target_desc
    {
        VkImageView color{};
        VkImageView resolve{};
        VkImageView pick{};
        VkImageView pick_resolve{};
        VkImageView depth{};
        VkExtent2D extent{};
        bool has_stencil{false};
        bool use_pick{true};
        VkClearValue color_clear{};
        VkClearValue pick_clear{};
    };

    struct shader_device
    {
        struct InitInfo
        {
            ui_gfx::Registry registry;
            const ui_abi::TypeTable *abi{};
            heap_ref heap{};
            heap_ref cc{};
            heap_ref indirect{};
            heap_ref vertex{};   // 顶点缓冲：由 device 填地址，app 不接触 uint64
            uint8_t vertex_slot{0xFF};
            uint8_t heap_slot{0};
            uint8_t cc_slot{1};
            ui_abi::PushBlock push{};
            VkExtent2D extent{};
        };

        void init(const InitInfo &in) noexcept
        {
            registry_ = in.registry;
            abi_ = in.abi;
            heap_ = in.heap;
            cc_ = in.cc;
            indirect_ = in.indirect;
            vertex_ = in.vertex;
            vertex_slot_ = in.vertex_slot;
            heap_slot_ = in.heap_slot;
            cc_slot_ = in.cc_slot;
            push_ = in.push;
            extent_ = in.extent;
        }

        // ── 每帧 / 每 pass ─────────────────────────────────────────────
        void begin_frame() noexcept { arena_ = {}; }
        void set_upload(bool v) noexcept { arena_.upload = v; }
        void set_push(const ui_abi::PushBlock &p) noexcept { push_ = p; }
        void set_extent(VkExtent2D e) noexcept { extent_ = e; }

        void submit(const mcs::vulkan::CommandBufferView &cmd,
                    const ui_proto::List &list, uint32_t desc_set = 0) noexcept
        {
            cmd_ = cmd;
            desc_set_ = desc_set;
            ui_gfx::decode(*this, list, registry_, arena_, desc_set);
        }

        void set_targets(std::span<const target_desc> t) noexcept { targets_ = t; }
        void set_resources(std::span<const resource_desc> r) noexcept { resources_ = r; }

        // ── 语义用途 → Vulkan layout（只有后端知道这个映射）────────────
        static VkImageLayout layout_of(uint32_t usage) noexcept
        {
            switch (static_cast<ui_proto::Usage>(usage))
            {
            case ui_proto::Usage::ColorTarget:
                return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            case ui_proto::Usage::DepthTarget:
                return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            case ui_proto::Usage::Sampled:
                return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            case ui_proto::Usage::TransferSrc:
                return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            case ui_proto::Usage::TransferDst:
                return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            case ui_proto::Usage::Present: return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            default: return VK_IMAGE_LAYOUT_UNDEFINED;
            }
        }

        void barrier(uint32_t image, uint32_t from, uint32_t to) const
        {
            const VkImageMemoryBarrier2 b{
                .sType = mcs::vulkan::tool::sType<VkImageMemoryBarrier2>(),
                .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT |
                                 VK_ACCESS_2_MEMORY_WRITE_BIT,
                .oldLayout = layout_of(from),
                .newLayout = layout_of(to),
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = resources_[image].image,
                .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                     .baseMipLevel = 0,
                                     .levelCount = VK_REMAINING_MIP_LEVELS,
                                     .baseArrayLayer = 0,
                                     .layerCount = VK_REMAINING_ARRAY_LAYERS}};
            cmd_.pipelineBarrier2(VkDependencyInfo{
                .sType = mcs::vulkan::tool::sType<VkDependencyInfo>(),
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = &b});
        }

        void blit(uint32_t src, uint32_t dst, uint32_t w, uint32_t h) const
        {
            const VkImageBlit region{
                .srcSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                   .mipLevel = 0,
                                   .baseArrayLayer = 0,
                                   .layerCount = 1},
                .srcOffsets = {{0, 0, 0},
                               {static_cast<int32_t>(w), static_cast<int32_t>(h), 1}},
                .dstSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                   .mipLevel = 0,
                                   .baseArrayLayer = 0,
                                   .layerCount = 1},
                .dstOffsets = {{0, 0, 0},
                               {static_cast<int32_t>(w), static_cast<int32_t>(h), 1}}};
            cmd_.blitImage(resources_[src].image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           resources_[dst].image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           std::span{&region, 1}, VK_FILTER_LINEAR);
        }

        [[nodiscard]] const ui_gfx::Arena &arena() const noexcept { return arena_; }

        // ── Sink 接口（decode 调）──────────────────────────────────────
        void upload_data(std::span<const std::byte> b, uint64_t base) const
        {
            if (!b.empty())
                heap_.write(base, b.data(), b.size());
        }
        void upload_cc(std::span<const ui_proto::Constant> xs, uint64_t base) const
        {
            if (!xs.empty())
                cc_.write(base, xs.data(), xs.size() * sizeof(xs[0]));
        }
        void upload_ind(std::span<const ui_proto::DrawCmd> xs, uint64_t base) const
        {
            if (!xs.empty())
                indirect_.write(base, xs.data(), xs.size() * sizeof(xs[0]));
        }

        void set_pipeline(Handle h) const
        {
            cmd_.bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                              reinterpret_cast<VkPipeline>(h.v));
        }
        void set_descriptor_set(Handle h) const
        {
            VkDescriptorSet ds = reinterpret_cast<VkDescriptorSet>(h.v);
            cmd_.bindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, layout_(), 0, 1, &ds,
                                    0, nullptr);
        }
        void set_index_buffer(Handle h) const
        {
            cmd_.bindIndexBuffer(reinterpret_cast<VkBuffer>(h.v), 0,
                                 VK_INDEX_TYPE_UINT32);
        }
        void set_viewport() const
        {
            const VkViewport vp{.x = 0.0F,
                                .y = 0.0F,
                                .width = static_cast<float>(extent_.width),
                                .height = static_cast<float>(extent_.height),
                                .minDepth = 0.0F,
                                .maxDepth = 1.0F};
            cmd_.setViewport(0, std::span{&vp, 1});
        }
        void set_scissor(const ui_proto::ClipRect &c) const
        {
            auto rnd = [](float v) {
                return static_cast<int32_t>(v >= 0.0F ? v + 0.5F : v - 0.5F);
            };
            VkRect2D r{{rnd(c.x), rnd(c.y)},
                       {static_cast<uint32_t>(rnd(c.w)), static_cast<uint32_t>(rnd(c.h))}};
            if (r.offset.x < 0)
                r.offset.x = 0;
            if (r.offset.y < 0)
                r.offset.y = 0;
            if (static_cast<uint32_t>(r.offset.x) + r.extent.width > extent_.width)
                r.extent.width = extent_.width - static_cast<uint32_t>(r.offset.x);
            if (static_cast<uint32_t>(r.offset.y) + r.extent.height > extent_.height)
                r.extent.height = extent_.height - static_cast<uint32_t>(r.offset.y);
            cmd_.setScissor(0, std::span{&r, 1});
        }
        void set_stencil_ref(uint32_t v) const
        {
            cmd_.setStencilReference(VK_STENCIL_FACE_FRONT_AND_BACK, v);
        }

        // ── pass 记录：beginRendering + 三条不变量 ─────────────────────
        void begin_pass(uint32_t target, bool clear)
        {
            const target_desc &t = targets_[target];
            extent_ = t.extent;
            VkRenderingAttachmentInfo color{};
            color.sType = mcs::vulkan::tool::sType<VkRenderingAttachmentInfo>();
            color.imageView = t.color;
            color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            color.resolveMode = t.resolve != VK_NULL_HANDLE ? VK_RESOLVE_MODE_AVERAGE_BIT
                                                         : VK_RESOLVE_MODE_NONE;
            color.resolveImageView = t.resolve;
            color.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            color.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            color.clearValue = t.color_clear;

            VkRenderingAttachmentInfo pick{};
            pick.sType = mcs::vulkan::tool::sType<VkRenderingAttachmentInfo>();
            pick.imageView = t.pick;
            pick.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            pick.resolveMode = t.pick_resolve != VK_NULL_HANDLE
                                   ? VK_RESOLVE_MODE_SAMPLE_ZERO_BIT
                                   : VK_RESOLVE_MODE_NONE;
            pick.resolveImageView = t.pick_resolve;
            pick.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            pick.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            pick.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            pick.clearValue = t.pick_clear;

            VkRenderingAttachmentInfo depth{};
            depth.sType = mcs::vulkan::tool::sType<VkRenderingAttachmentInfo>();
            depth.imageView = t.depth;
            depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depth.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depth.clearValue = {.depthStencil = {.depth = 1.0F}};

            std::array<VkRenderingAttachmentInfo, 2> atts{color, pick};
            cmd_.beginRendering({.sType = mcs::vulkan::tool::sType<VkRenderingInfo>(),
                                 .renderArea = {.offset = {0, 0}, .extent = t.extent},
                                 .layerCount = 1,
                                 .colorAttachmentCount =
                                     static_cast<uint32_t>(t.use_pick ? 2u : 1u),
                                 .pColorAttachments = atts.data(),
                                 .pDepthAttachment =
                                     t.depth != VK_NULL_HANDLE ? &depth : nullptr,
                                 .pStencilAttachment =
                                     (t.has_stencil && t.depth != VK_NULL_HANDLE)
                                         ? &depth
                                         : nullptr});

            // pass 级不变量：一次绑定，后续 draw 只管状态/数据
            set_descriptor_set(registry_.descSets[desc_set_]);
            set_index_buffer(registry_.ibo);
            set_viewport();
        }
        void end_pass() { cmd_.endRendering(); }
        void push_segment(uint32_t cc_index) const
        {
            ui_abi::PushBlock pb = push_;
            if (vertex_slot_ < 3)
                slot64(pb, vertex_slot_) = vertex_.address;
            slot64(pb, heap_slot_) = heap_.address + arena_.heap;
            slot64(pb, cc_slot_) = cc_.address + arena_.cc +
                                   uint64_t(cc_index) * sizeof(ui_proto::Constant);
            cmd_.pushConstants(layout_(), VK_SHADER_STAGE_VERTEX_BIT |
                                              VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(pb), &pb);
        }
        void draw(uint64_t ind_index, uint32_t k) const
        {
            cmd_.drawIndexedIndirect(indirect_.buffer,
                                     arena_.ind + ind_index * sizeof(ui_proto::DrawCmd),
                                     k, sizeof(VkDrawIndexedIndirectCommand));
        }

      private:
        static uint64_t &slot64(ui_abi::PushBlock &b, uint8_t i) noexcept
        {
            return i == 0 ? b.s0 : (i == 1 ? b.s1 : b.s2);
        }
        [[nodiscard]] VkPipelineLayout layout_() const noexcept
        {
            return reinterpret_cast<VkPipelineLayout>(registry_.layouts[0].v);
        }

        ui_gfx::Registry registry_{};
        const ui_abi::TypeTable *abi_{};
        heap_ref heap_{}, cc_{}, indirect_{};
        heap_ref vertex_{};
        uint8_t vertex_slot_{0xFF};
        uint8_t heap_slot_{0}, cc_slot_{1};
        ui_abi::PushBlock push_{};
        VkExtent2D extent_{};
        ui_gfx::Arena arena_{};
        uint32_t desc_set_{0};
        std::span<const target_desc> targets_{};
        std::span<const resource_desc> resources_{};
        mcs::vulkan::CommandBufferView cmd_{};
    };

    static_assert(ui_gfx::Sink<shader_device>);

} // namespace ui_gfx
