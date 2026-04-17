#pragma once

#include "Node.hpp"
#include "layout_size.hpp"

#include <print>

namespace mcs::vulkan::yoga
{
    class Layout // NOLINT
    {
        Node *root_;

        static void printNodeLayout(const Node *node, int depth = 0)
        {
            if (node == nullptr)
                return;
            YGNodeRef ygNode = **node;

            std::string indent(depth * 2, ' ');

            float left = YGNodeLayoutGetLeft(ygNode);
            float top = YGNodeLayoutGetTop(ygNode);
            float right = YGNodeLayoutGetRight(ygNode);
            float bottom = YGNodeLayoutGetBottom(ygNode);
            float width = YGNodeLayoutGetWidth(ygNode);
            float height = YGNodeLayoutGetHeight(ygNode);

            std::println("{}Node Layout [{}]:", indent, node->id());
            std::println("{}  position: left={}, top={}, right={}, bottom={}", indent,
                         left, top, right, bottom);
            std::println("{}  size: width={}, height={}", indent, width, height);

            YGDirection dir = YGNodeLayoutGetDirection(ygNode);
            bool overflow = YGNodeLayoutGetHadOverflow(ygNode);
            std::println("{}  direction: {}", indent,
                         (dir == YGDirectionLTR ? "LTR" : "RTL"));
            std::println("{}  hadOverflow: {}", indent, overflow ? "true" : "false");

            std::println("{}  margin: top={}, left={}, bottom={}, right={}", indent,
                         YGNodeLayoutGetMargin(ygNode, YGEdgeTop),
                         YGNodeLayoutGetMargin(ygNode, YGEdgeLeft),
                         YGNodeLayoutGetMargin(ygNode, YGEdgeBottom),
                         YGNodeLayoutGetMargin(ygNode, YGEdgeRight));

            std::println("{}  border: top={}, left={}, bottom={}, right={}", indent,
                         YGNodeLayoutGetBorder(ygNode, YGEdgeTop),
                         YGNodeLayoutGetBorder(ygNode, YGEdgeLeft),
                         YGNodeLayoutGetBorder(ygNode, YGEdgeBottom),
                         YGNodeLayoutGetBorder(ygNode, YGEdgeRight));

            std::println("{}  padding: top={}, left={}, bottom={}, right={}", indent,
                         YGNodeLayoutGetPadding(ygNode, YGEdgeTop),
                         YGNodeLayoutGetPadding(ygNode, YGEdgeLeft),
                         YGNodeLayoutGetPadding(ygNode, YGEdgeBottom),
                         YGNodeLayoutGetPadding(ygNode, YGEdgeRight));

            // 递归打印子节点
            for (const auto &child : node->childrens())
            {
                printNodeLayout(child.get(), depth + 1);
            }
        }

      public:
        explicit Layout(Node &root) noexcept : root_{&root} {}
        auto calculate(layout_size size) const
        {
            root_->calculateLayout(size);
        }
        auto update()
        {
            root_->update();
        }
        void print()
        {
            printNodeLayout(root_);
        }

        [[nodiscard]] Node *root() const noexcept
        {
            return root_;
        }
    };
}; // namespace mcs::vulkan::yoga