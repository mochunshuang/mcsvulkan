#pragma once

#include "Config.hpp"
#include "Style.hpp"
#include "layout_size.hpp"

#include <cassert>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>

namespace mcs::vulkan::yoga
{
    class Node
    {
        using value_type = YGNodeRef;

        std::string id_;
        Config config_;
        value_type value_;
        Style style_;
        Node *parent_{nullptr};

        std::vector<std::unique_ptr<Node>> childrens_;

        constexpr auto chnageChildrens(
            std::vector<std::unique_ptr<Node>> childrens_) noexcept
        {
            for (auto &child : childrens_)
            {
                assert(child != nullptr);
                child->parent_ = this;
            }
            return childrens_;
        }
        std::unique_ptr<Node> disconenctChildPtr(
            std::vector<std::unique_ptr<Node>>::iterator it)
        {
            auto owned_ptr = std::move(*it);
            childrens_.erase(it);

            // 断言：Yoga 树中子节点确实属于当前父节点
            assert(YGNodeGetParent(owned_ptr->value_) == value_);

            owned_ptr->parent_ = nullptr;
            YGNodeRemoveChild(value_, owned_ptr->value_);
            return owned_ptr;
        }

      public:
        constexpr operator bool() const noexcept // NOLINT
        {
            return value_ != nullptr;
        }
        constexpr auto &operator*() noexcept
        {
            return value_;
        }
        constexpr const auto &operator*() const noexcept
        {
            return value_;
        }
        constexpr bool operator==(const Node &o) const noexcept
        {
            return value_ == o.value_;
        }
        constexpr bool operator!=(const Node &o) const noexcept = default;
        [[nodiscard]] constexpr auto data() const noexcept
        {
            return value_;
        }
        // 移动构造函数：目标节点始终为孤儿，源节点失去所有权且 parent_ 置空
        Node(Node &&o) noexcept
            : id_{std::exchange(o.id_, {})}, config_{std::exchange(o.config_, {})},
              value_{std::exchange(o.value_, {})}, style_{std::exchange(o.style_, {})},
              parent_{std::exchange(o.parent_, {})},
              childrens_{chnageChildrens(std::move(o.childrens_))}
        {
        }

        // 移动赋值运算符
        Node &operator=(Node &&o) noexcept
        {
            if (this != &o)
            {
                release(); // 清理当前资源
                id_ = std::exchange(o.id_, {});
                config_ = std::exchange(o.config_, {});
                value_ = std::exchange(o.value_, {});
                style_ = std::exchange(o.style_, {});
                parent_ = std::exchange(o.parent_, {});
                childrens_ = chnageChildrens(std::move(o.childrens_));
            }
            return *this;
        }

        // append 保持不变，但更安全：要求 child 是孤儿
        void emplace_back(std::unique_ptr<Node> child) // NOLINT
        {
            if (child == nullptr)
                return;
            assert(child->parent_ == nullptr &&
                   "Child must be detached before appending");

            child->parent_ = this;
            YGNodeInsertChild(value_, child->value_, YGNodeGetChildCount(value_));
            childrens_.emplace_back(std::move(child));
        }
        void insert(std::unique_ptr<Node> child, size_t index)
        {
            if (child == nullptr || index > childrens_.size())
                return;
            assert(child->parent_ == nullptr &&
                   "Child must be detached before appending");
            child->parent_ = this;
            YGNodeInsertChild(value_, child->value_, index);
            childrens_.insert(childrens_.begin() + static_cast<std::int64_t>(index),
                              std::move(child));
        }
        void emplace_back(Node child) // NOLINT
        {
            emplace_back(std::make_unique<Node>(std::move(child)));
        }

        constexpr void release() noexcept
        {
            if (value_ != nullptr)
            {
                // 1. 主动销毁所有子节点（触发子节点的析构 → 从当前 Yoga 节点移除自己）
                childrens_.clear();
                // 2. 从父 Yoga 节点中移除自己（如果有父节点）
                if (parent_ != nullptr)
                {
                    assert(parent_->value_ != nullptr);
                    YGNodeRemoveChild(parent_->value_, value_);
                    parent_ = nullptr;
                }
                // 3. 释放当前 Yoga 节点
                YGNodeFree(std::exchange(value_, {}));
            }
        }

        void applyStyle() const noexcept
        {
            style_.apply(value_);
        }
        void update() const noexcept
        {
            applyStyle();
        }

        Node(const Node &) = delete;
        Node &operator=(const Node &) = delete;

        template <typename... Children>
            requires((std::same_as<Children, std::unique_ptr<Node>> ||
                      std::same_as<Children, Node>) &&
                     ...)
        Node(std::string id, Config config, Style style, Children... children)
            : id_{std::move(id)}, config_{std::move(config)},
              value_(YGNodeNewWithConfig(*config_)), style_{std::move(style)}
        {
            // 1. 应用当前节点的样式
            applyStyle();
            // 2. 构造父子关系
            (emplace_back(std::move(children)), ...);
        }
        template <typename... Children>
            requires((std::same_as<Children, std::unique_ptr<Node>> ||
                      std::same_as<Children, Node>) &&
                     ...)
        Node(std::string id, Style style, Children... children)
            : id_{std::move(id)}, value_(YGNodeNew()), style_{std::move(style)}
        {
            // 1. 应用当前节点的样式
            applyStyle();
            // 2. 构造父子关系
            (emplace_back(std::move(children)), ...);
        }

        ~Node() noexcept
        {
            release();
        }

        std::unique_ptr<Node> removeChild(size_t index)
        {
            if (index >= childrens_.size())
                return nullptr;
            return disconenctChildPtr(childrens_.begin() +
                                      static_cast<std::int64_t>(index));
        }

        std::unique_ptr<Node> removeChild(const std::unique_ptr<Node> &item)
        {
            if (item == nullptr)
                return nullptr;
            if (auto it = std::ranges::find_if(
                    childrens_, [&](const auto &cur) { return cur.get() == item.get(); });
                it != childrens_.end())
            {
                return disconenctChildPtr(it);
            }
            return nullptr;
        }

        [[nodiscard]] auto &refConfig() noexcept
        {
            return config_;
        }
        [[nodiscard]] const std::string &id() const noexcept
        {
            return id_;
        }
        void calculateLayout(layout_size size)
        {
            YGNodeCalculateLayout(value_, size.available_width, size.available_height,
                                  *style_.direction);
        }

        Style &refStyle() noexcept
        {
            return style_;
        }

        [[nodiscard]] auto &childrens() const noexcept
        {
            return childrens_;
        }
        [[nodiscard]] auto &childrens() noexcept
        {
            return childrens_;
        }
        [[nodiscard]] const Node *parent() const noexcept
        {
            return parent_;
        }
        [[nodiscard]] const Node *root() const noexcept
        {
            Node *cur = this->parent_;
            while (cur->parent_ != nullptr)
                cur = cur->parent_;
            return cur;
        }
    };
}; // namespace mcs::vulkan::yoga