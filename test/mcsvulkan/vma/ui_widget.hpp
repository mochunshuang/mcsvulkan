#pragma once
// ============================================================================
// ui_widget.hpp —— UI 库的组件层（不依赖任何测试文件，也不含 Vulkan）
//
//   职责只有两件：
//     layout：自上而下约束，自下而上尺寸；同时把"相对父的偏移"定下来
//     paint ：只读地画自己（拿到的 PaintContext 里只有编码器 + 视口 + 裁剪）
//
//   —— 没有 Recorder、没有 pass、没有 Vulkan、没有 input/clock。
// ============================================================================
#include "ui_protocol.hpp"

#include <algorithm>
#include <memory>
#include <span>
#include <vector>

namespace ui
{
    struct Size
    {
        float w{}, h{};
    };
    struct Offset
    {
        float x{}, y{};
    };
    struct BoxConstraints
    {
        float min_w{0}, max_w{1e30F}, min_h{0}, max_h{1e30F};
    };

    // 收窄的绘制上下文：一个 sink + 两个只读量。没有 input / clock / screen。
    struct PaintContext
    {
        ui_proto::CmdEncoder &out;
        float width{};
        float height{};
        ui_proto::ClipId clip{};
    };

    class Widget
    {
      public:
        virtual ~Widget() = default;

        virtual Size layout(const BoxConstraints &) = 0;
        virtual void paint(const PaintContext &) const {}
        virtual std::span<const std::unique_ptr<Widget>> children() const { return {}; }

        [[nodiscard]] Size size() const noexcept { return size_; }
        [[nodiscard]] Offset offset() const noexcept { return offset_; }

        Size size_{};
        Offset origin_{}; // 相对父
        Offset offset_{}; // 绝对（place 之后）

      protected:
        void set_size(Size s) noexcept { size_ = s; }
    };

    // 纵向排列
    class Column : public Widget
    {
      public:
        Column &add(std::unique_ptr<Widget> w)
        {
            kids_.push_back(std::move(w));
            return *this;
        }

        Size layout(const BoxConstraints &c) override
        {
            float y = 0.0F, w = 0.0F;
            for (auto &k : kids_)
            {
                const Size s = k->layout(BoxConstraints{0, c.max_w, 0, 1e30F});
                k->origin_ = {0, y};
                y += s.h;
                w = std::max(w, s.w);
            }
            set_size({w, y});
            return size_;
        }

        std::span<const std::unique_ptr<Widget>> children() const override { return kids_; }

      private:
        std::vector<std::unique_ptr<Widget>> kids_;
    };

    // 绝对偏移：一趟 DFS
    inline void place(Widget &w, Offset parent = {})
    {
        w.offset_ = {parent.x + w.origin_.x, parent.y + w.origin_.y};
        for (auto &c : w.children())
            place(*c, w.offset_);
    }

    // 绘制：一趟 DFS，只有 paint，没有 push/pop
    inline void paint_tree(const Widget &w, const PaintContext &ctx)
    {
        w.paint(ctx);
        for (auto &c : w.children())
            paint_tree(*c, ctx);
    }

} // namespace ui
