// 原理: https://docs.flutter.dev/ui/layout/constraints
//      https://docs.flutter.dev/ui/layout#overview
// 概念：约束向下传递，尺寸向上传递，父节点决定子节点位置。
// 本实现将 Flutter 布局原则映射为 C++ 代码，所有行为均与官方一致。
// 使用 Builder 风格 API，构造函数使用初始化列表。

#include <algorithm>
#include <cassert>
#include <meta>
#include <format>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <memory_resource> // pmr 内存资源
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

// ============================================================================
// static_string 定义（编译期驻留字符串）
// ============================================================================

namespace mcs::vulkan::meta
{
    struct static_string
    {
        const char *value{}; // NOLINT

        static_string() = default;
        consteval explicit static_string(std::string_view view)
            : value{std::define_static_string(view)}
        {
        }
        consteval explicit static_string(const char *value) noexcept
            : static_string{std::string_view{value}}
        {
        }
        template <size_t N>
        consteval static_string(const char (&str)[N]) noexcept // NOLINT
            : static_string{std::string_view{str, N - 1}}
        {
        }
        [[nodiscard]] constexpr std::string_view view() const noexcept
        {
            return std::string_view{value};
        }
        constexpr bool operator==(const static_string &o) const noexcept
        {
            if (value == o.value)
                return true;
            return view() == o.view();
        }
        constexpr bool operator==(const std::string_view &o) const noexcept
        {
            return view() == o;
        }
        template <size_t N>
        consteval bool operator==(const char (&str)[N]) const noexcept // NOLINT
        {
            return view() == std::string_view{str, N - 1};
        }

        constexpr bool operator<(const static_string &o) const noexcept
        {
            return view() < o.view();
        }

        friend std::ostream &operator<<(std::ostream &os, const static_string &s)
        {
            return os << s.view();
        }
    };
}; // namespace mcs::vulkan::meta

// 引入到全局，方便使用
using static_string = mcs::vulkan::meta::static_string;
// ============================================================================
// 哈希与相等函数对象（支持透明查找和指针快速路径）
// ============================================================================

struct StaticStringHash
{
    using is_transparent = void; // 启用异构查找

    std::size_t operator()(const static_string &s) const noexcept
    {
        return std::hash<std::string_view>{}(s.view());
    }
    std::size_t operator()(std::string_view sv) const noexcept
    {
        return std::hash<std::string_view>{}(sv);
    }
};

struct StaticStringEqual
{
    using is_transparent = void; // 启用异构查找

    bool operator()(const static_string &a, const static_string &b) const noexcept
    {
        if (a.value == b.value) // 指针相等快速路径
            return true;
        return a.view() == b.view();
    }
    bool operator()(const static_string &a, std::string_view b) const noexcept
    {
        return a.view() == b;
    }
    bool operator()(std::string_view a, const static_string &b) const noexcept
    {
        return a == b.view();
    }
};

namespace std
{
    template <>
    struct formatter<mcs::vulkan::meta::static_string> : formatter<string_view>
    {
        auto format(const mcs::vulkan::meta::static_string &s, format_context &ctx) const
        {
            return formatter<string_view>::format(s.view(), ctx);
        }
    };
} // namespace std

// ============================================================================
// 全局内存池资源（供所有 pmr 分配使用）
// ============================================================================
inline std::pmr::synchronized_pool_resource globalMemoryPool;
inline std::pmr::memory_resource *nodeResource = &globalMemoryPool;

// ============================================================================
// pmr 唯一所有权指针封装（类型擦除删除器）
// ============================================================================

struct PmrDeleter
{
    using destroy_fn = void (*)(void *, std::pmr::memory_resource *);
    destroy_fn destroy{nullptr};
    std::pmr::memory_resource *resource{nodeResource};

    PmrDeleter() = default;
    PmrDeleter(destroy_fn fn, std::pmr::memory_resource *r) : destroy(fn), resource(r) {}

    void operator()(void *p) const noexcept
    {
        if (p && destroy)
        {
            destroy(p, resource);
        }
    }
};

template <typename T>
PmrDeleter make_pmr_deleter(std::pmr::memory_resource *r)
{
    return PmrDeleter{
        [](void *p, std::pmr::memory_resource *r) {
            auto typed = static_cast<T *>(p);
            std::pmr::polymorphic_allocator<T> alloc{r};
            std::allocator_traits<std::pmr::polymorphic_allocator<T>>::destroy(alloc,
                                                                               typed);
            alloc.deallocate(typed, 1);
        },
        r};
}

template <typename T>
using pmr_unique_ptr = std::unique_ptr<T, PmrDeleter>;

template <typename T, typename... Args>
pmr_unique_ptr<T> make_pmr_unique(std::pmr::memory_resource *r, Args &&...args)
{
    std::pmr::polymorphic_allocator<T> alloc{r};
    T *p = alloc.allocate(1);

    try
    {
        std::allocator_traits<std::pmr::polymorphic_allocator<T>>::construct(
            alloc, p, std::forward<Args>(args)...);
    }
    catch (...)
    {
        alloc.deallocate(p, 1);
        throw;
    }

    return pmr_unique_ptr<T>(p, make_pmr_deleter<T>(r));
}

// ============================================================================
// 基础几何与布局类型
// ============================================================================

struct Size
{
    float width, height;
};

struct Offset
{
    float x, y;
};

struct EdgeInsets
{
    float left = 0, top = 0, right = 0, bottom = 0;
    constexpr float horizontal() const
    {
        return left + right;
    }
    constexpr float vertical() const
    {
        return top + bottom;
    }
    static constexpr EdgeInsets all(float v)
    {
        return {v, v, v, v};
    }
};

struct Alignment
{
    float x, y;
};
namespace Align
{
    constexpr Alignment topLeft{-1, -1}, topCenter{0, -1}, topRight{1, -1};
    constexpr Alignment centerLeft{-1, 0}, center{0, 0}, centerRight{1, 0};
    constexpr Alignment bottomLeft{-1, 1}, bottomCenter{0, 1}, bottomRight{1, 1};
} // namespace Align

// ============================================================================
// 约束（Constraints）
// ============================================================================

struct Constraints
{
    static constexpr float inf = std::numeric_limits<float>::infinity();
    float minW = 0, maxW = inf, minH = 0, maxH = inf;

    constexpr bool hasBoundedWidth() const noexcept
    {
        return maxW < inf;
    }
    constexpr bool hasBoundedHeight() const noexcept
    {
        return maxH < inf;
    }
    constexpr bool hasUnboundedWidth() const noexcept
    {
        return maxW >= inf;
    }
    constexpr bool hasUnboundedHeight() const noexcept
    {
        return maxH >= inf;
    }

    constexpr Constraints deflate(const EdgeInsets &e) const noexcept
    {
        float h = e.horizontal(), v = e.vertical();
        return {std::max(0.0f, minW - h), std::max(0.0f, maxW - h),
                std::max(0.0f, minH - v), std::max(0.0f, maxH - v)};
    }

    constexpr Constraints intersect(const Constraints &parent) const noexcept
    {
        float newMinW = std::max(parent.minW, minW);
        float newMaxW = std::min(parent.maxW, maxW);
        float newMinH = std::max(parent.minH, minH);
        float newMaxH = std::min(parent.maxH, maxH);
        if (newMinW > newMaxW)
            newMinW = newMaxW = std::clamp(minW, parent.minW, parent.maxW);
        if (newMinH > newMaxH)
            newMinH = newMaxH = std::clamp(minH, parent.minH, parent.maxH);
        return {newMinW, newMaxW, newMinH, newMaxH};
    }

    constexpr Constraints applyFixedWidth(float w) const noexcept
    {
        return Constraints{w, w, minH, maxH}.intersect(*this);
    }
    constexpr Constraints applyFixedHeight(float h) const noexcept
    {
        return Constraints{minW, maxW, h, h}.intersect(*this);
    }

    constexpr Size clamp(const Size &s) const noexcept
    {
        return {std::clamp(s.width, minW, maxW), std::clamp(s.height, minH, maxH)};
    }
};

// ============================================================================
// 方向与枚举
// ============================================================================

enum class TextDirection
{
    ltr,
    rtl
};
enum class VerticalDirection
{
    down,
    up
};
enum class MainAxisAlignment
{
    start,
    end,
    center,
    spaceBetween,
    spaceAround,
    spaceEvenly
};
enum class CrossAxisAlignment
{
    start,
    end,
    center,
    stretch,
    baseline
};
enum class MainAxisSize
{
    max,
    min
};
enum class AxisDirection
{
    right,
    left,
    down,
    up
};

// ============================================================================
// 数据挂载基类 IData
// ============================================================================

class IData
{
  public:
    virtual ~IData() = default;
};

// ============================================================================
// 前置声明 Node 并定义 Widget 别名
// ============================================================================

class Node;
using Widget = pmr_unique_ptr<Node>;

// ============================================================================
// 抽象节点基类 Node
// ============================================================================

struct BoxGeometry
{
    float x = 0, y = 0, w = 0, h = 0;
};

class Node
{
  public:
    explicit Node(std::string name, EdgeInsets margin = {}, EdgeInsets padding = {},
                  std::optional<float> fixedWidth = std::nullopt,
                  std::optional<float> fixedHeight = std::nullopt)
        : name(std::move(name)), margin(margin), padding(padding), fixedWidth(fixedWidth),
          fixedHeight(fixedHeight)
    {
    }

    virtual ~Node() = default;

    BoxGeometry geometry;
    std::string name;
    float baseline = 0.0f;
    EdgeInsets margin;
    EdgeInsets padding;
    std::optional<float> fixedWidth;
    std::optional<float> fixedHeight;
    std::vector<Widget> children;
    Node *parent = nullptr; // 父节点裸指针，不拥有所有权
    bool dirty = false;     // 脏标记，用于测试遍历是否覆盖

    // ---------- 数据挂载 API（使用 static_string 优化） ----------
    // 添加数据，返回原始指针以便后续修改
    template <typename T, typename... Args>
    T *addData(static_string key, Args &&...args)
    {
        static_assert(std::is_base_of_v<IData, T>, "T must derive from IData");
        auto ptr = make_pmr_unique<T>(nodeResource, std::forward<Args>(args)...);
        T *raw = ptr.get();
        dataMap_[key].push_back(std::move(ptr));
        return raw;
    }

    // 获取数据，如果类型不匹配或不存在则返回 nullptr
    template <typename T>
    T *getData(static_string key, std::size_t index = 0)
    {
        static_assert(std::is_base_of_v<IData, T>, "T must derive from IData");
        auto it = dataMap_.find(key);
        if (it == dataMap_.end() || index >= it->second.size())
            return nullptr;
        return dynamic_cast<T *>(it->second[index].get());
    }

    // 获取指定键下数据的数量
    std::size_t getDataCount(static_string key) const
    {
        auto it = dataMap_.find(key);
        return (it == dataMap_.end()) ? 0 : it->second.size();
    }

    // 移除指定键的某个数据，若成功则返回 true
    bool removeData(static_string key, std::size_t index = 0)
    {
        auto it = dataMap_.find(key);
        if (it == dataMap_.end() || index >= it->second.size())
            return false;
        it->second.erase(it->second.begin() + index);
        if (it->second.empty())
            dataMap_.erase(it);
        return true;
    }

    // 布局入口
    void layout(Constraints constraints)
    {
        if (fixedWidth)
            constraints = constraints.applyFixedWidth(*fixedWidth);
        if (fixedHeight)
            constraints = constraints.applyFixedHeight(*fixedHeight);
        performLayout(constraints);
        Size finalSize{geometry.w, geometry.h};
        if (finalSize.width > constraints.maxW + 1e-6f ||
            finalSize.height > constraints.maxH + 1e-6f)
        {
            std::cerr << "WARNING: Widget '" << name << "' overflows parent by ("
                      << finalSize.width - constraints.maxW << ", "
                      << finalSize.height - constraints.maxH << ")\n";
        }
    }

    Size size() const
    {
        return {geometry.w, geometry.h};
    }
    Offset position() const
    {
        return {geometry.x, geometry.y};
    }
    BoxGeometry getGeometry() const
    {
        return geometry;
    }

    // ==========================================================================
    // 节点移动 API（安全操作，维护 parent 指针）
    // ==========================================================================

    // 移除指定子节点，返回其 unique_ptr（所有权转移给调用者），parent 置空
    Widget removeChild(Node *child)
    {
        auto it = std::ranges::find_if(
            children, [child](const Widget &w) { return w.get() == child; });
        if (it == children.end())
            return nullptr;

        auto detached = std::move(*it);
        children.erase(it);
        detached->parent = nullptr;
        return detached;
    }

    // 添加子节点（接收 unique_ptr），设置 parent 并加入 children
    void addChild(Widget child)
    {
        if (!child)
            return;
        child->parent = this;
        children.push_back(std::move(child));
    }

    // 将已存在的子节点移动到新的父节点下
    static void moveNode(Node *child, Node *newParent)
    {
        if (!child || !newParent)
            return;
        Node *oldParent = child->parent;
        if (oldParent == newParent)
            return;

        Widget detached;
        if (oldParent)
        {
            detached = oldParent->removeChild(child);
        }
        else
        {
            return; // 没有父节点，无法取得所有权
        }

        if (detached)
        {
            newParent->addChild(std::move(detached));
        }
    }

  private:
    std::unordered_map<static_string, std::vector<pmr_unique_ptr<IData>>,
                       StaticStringHash, StaticStringEqual>
        dataMap_;

  protected:
    virtual void performLayout(Constraints borderBC) = 0;
    Size childFullSize(const Node &child) const
    {
        return {child.geometry.w + child.margin.horizontal(),
                child.geometry.h + child.margin.vertical()};
    }
    static Constraints makeLooseConstraints(const Constraints &c)
    {
        return {0.0f, c.maxW, 0.0f, c.maxH};
    }
};

// ============================================================================
// Expanded 节点
// ============================================================================

class ExpandedNode : public Node
{
  public:
    explicit ExpandedNode(int flex = 1, std::string name = "expanded")
        : Node(std::move(name)), flex(flex)
    {
    }
    int flex = 1;

  protected:
    void performLayout(Constraints) override
    {
        throw std::logic_error("Expanded must be direct child of Row/Column/Flex.");
    }
};

// ============================================================================
// Container 节点
// ============================================================================

class ContainerNode : public Node
{
  public:
    explicit ContainerNode(std::string name, std::optional<float> width = std::nullopt,
                           std::optional<float> height = std::nullopt,
                           EdgeInsets margin = {}, EdgeInsets padding = {},
                           EdgeInsets border = {},
                           std::optional<Alignment> alignment = std::nullopt,
                           std::optional<Constraints> constraints = std::nullopt)
        : Node(std::move(name), margin, padding, width, height), alignment(alignment),
          ownConstraints(constraints), border(border)
    {
    }

    std::optional<Alignment> alignment;
    std::optional<Constraints> ownConstraints;
    EdgeInsets border;

  protected:
    void performLayout(Constraints borderBC) override
    {
        if (ownConstraints)
            borderBC = borderBC.intersect(*ownConstraints);
        if (children.empty())
        {
            float w = fixedWidth.value_or(0.0f);
            float h = fixedHeight.value_or(0.0f);
            Size size = borderBC.clamp({w, h});
            geometry = BoxGeometry{0, 0, size.width, size.height};
            baseline = size.height;
            return;
        }
        if (children.size() != 1)
            throw std::logic_error("Container '" + name +
                                   "' must have exactly one child.");
        Node &child = *children[0];
        Constraints innerBC = borderBC.deflate(border).deflate(padding);
        Constraints childBC = alignment ? Node::makeLooseConstraints(innerBC) : innerBC;
        childBC = childBC.deflate(child.margin);
        child.layout(childBC);
        Size childFull = childFullSize(child);
        float baseW = childFull.width + padding.horizontal() + border.horizontal();
        float baseH = childFull.height + padding.vertical() + border.vertical();
        float containerW = fixedWidth.value_or(baseW);
        float containerH = fixedHeight.value_or(baseH);
        Size containerSize = borderBC.clamp({containerW, containerH});
        Constraints containerAsConstraints{0, containerSize.width, 0,
                                           containerSize.height};
        positionChildByAlignment(child, containerAsConstraints, padding, alignment,
                                 border);
        geometry = BoxGeometry{0, 0, containerSize.width, containerSize.height};
        baseline = child.baseline + child.geometry.y;
    }

  private:
    void positionChildByAlignment(Node &child, const Constraints &containerConstraints,
                                  const EdgeInsets &padding,
                                  const std::optional<Alignment> &align,
                                  const EdgeInsets &border)
    {
        Size childFull = childFullSize(child);
        float contentW =
            containerConstraints.maxW - border.horizontal() - padding.horizontal();
        float contentH =
            containerConstraints.maxH - border.vertical() - padding.vertical();
        float extraW = std::max(0.0f, contentW - childFull.width);
        float extraH = std::max(0.0f, contentH - childFull.height);
        Alignment al = align.value_or(Align::topLeft);
        Offset offset = {border.left + padding.left + child.margin.left +
                             extraW * (al.x + 1.0f) / 2.0f,
                         border.top + padding.top + child.margin.top +
                             extraH * (al.y + 1.0f) / 2.0f};
        child.geometry.x = offset.x;
        child.geometry.y = offset.y;
    }
};

// ============================================================================
// Flex 节点基类（Row 和 Column 共用）
// ============================================================================

template <bool isRow_>
class FlexNode : public Node
{
  public:
    MainAxisAlignment mainAlign = MainAxisAlignment::start;
    CrossAxisAlignment crossAlign = CrossAxisAlignment::start;
    MainAxisSize mainAxisSize = MainAxisSize::max;
    TextDirection textDirection = TextDirection::ltr;
    VerticalDirection verticalDirection = VerticalDirection::down;

  protected:
    FlexNode(std::string name, std::optional<float> fixedWidth,
             std::optional<float> fixedHeight, EdgeInsets margin, EdgeInsets padding,
             MainAxisAlignment mainAlign, CrossAxisAlignment crossAlign,
             MainAxisSize mainAxisSize, TextDirection textDirection = TextDirection::ltr,
             VerticalDirection verticalDirection = VerticalDirection::down)
        : Node(std::move(name), margin, padding, fixedWidth, fixedHeight),
          mainAlign(mainAlign), crossAlign(crossAlign), mainAxisSize(mainAxisSize),
          textDirection(textDirection), verticalDirection(verticalDirection)
    {
    }

    void performLayout(Constraints borderBC) override
    {
        AxisDirection dir = axisDirection();
        bool isBoundedMain =
            isRow_ ? borderBC.hasBoundedWidth() : borderBC.hasBoundedHeight();
        bool isBoundedCross =
            isRow_ ? borderBC.hasBoundedHeight() : borderBC.hasBoundedWidth();
        if (!isBoundedCross)
            throw std::logic_error("Row/Column '" + name + "' has unbounded cross axis.");
        if (!isBoundedMain)
        {
            for (const auto &child : children)
                if (dynamic_cast<ExpandedNode *>(child.get()) != nullptr)
                    throw std::logic_error(
                        "Row/Column '" + name +
                        "' has unbounded main axis and Expanded child.");
        }

        float mainSize = mainAxisSizeFromConstraints(borderBC);
        float crossSize = crossAxisSizeFromConstraints(borderBC);
        float padMainStart = mainAxisPaddingStart(padding, dir);
        float padMainEnd = mainAxisPaddingEnd(padding, dir);
        float padCrossStart = crossAxisPaddingStart(padding, dir);
        float padCrossEnd = crossAxisPaddingEnd(padding, dir);
        float innerMain = mainSize - padMainStart - padMainEnd;
        if (!isBoundedMain)
            innerMain = Constraints::inf;
        float innerCross = crossSize - padCrossStart - padCrossEnd;

        struct FlexChildInfo
        {
            Node *node;
            float flex;
            float marginMain;
            float marginCross;
        };
        std::vector<FlexChildInfo> infos;
        float totalFlex = 0;
        for (auto &childPtr : children)
        {
            Node *child = childPtr.get();
            if (auto expanded = dynamic_cast<ExpandedNode *>(child))
            {
                if (expanded->children.empty())
                    throw std::logic_error("Expanded widget has no child.");
                Node *real = expanded->children[0].get();
                float flexVal = expanded->flex;
                EdgeInsets m = real->margin;
                float mM = isRow_ ? m.horizontal() : m.vertical();
                float mC = isRow_ ? m.vertical() : m.horizontal();
                infos.push_back({real, flexVal, mM, mC});
                totalFlex += flexVal;
            }
            else
            {
                EdgeInsets m = child->margin;
                float mM = isRow_ ? m.horizontal() : m.vertical();
                float mC = isRow_ ? m.vertical() : m.horizontal();
                infos.push_back({child, 0.0f, mM, mC});
            }
        }

        std::vector<float> naturalMain;
        naturalMain.reserve(infos.size());
        for (const auto &info : infos)
        {
            float childMainMax =
                (info.flex > 0) ? 0.0f : (isBoundedMain ? innerMain : Constraints::inf);
            float childCrossMax = innerCross;
            float childCrossMin =
                (crossAlign == CrossAxisAlignment::stretch) ? childCrossMax : 0.0f;
            Constraints childBC = makeFlexAxisConstraints(isRow_, 0.0f, childMainMax,
                                                          childCrossMin, childCrossMax);
            if (info.flex == 0)
            {
                EdgeInsets deflateMargin =
                    isRow_ ? EdgeInsets{info.marginMain, info.marginCross, 0, 0}
                           : EdgeInsets{info.marginCross, info.marginMain, 0, 0};
                childBC = childBC.deflate(deflateMargin);
            }
            info.node->layout(childBC);
            naturalMain.push_back(isRow_ ? info.node->geometry.w : info.node->geometry.h);
        }

        float totalNatural = 0;
        for (size_t i = 0; i < infos.size(); ++i)
            totalNatural += naturalMain[i] + infos[i].marginMain;

        float finalInnerMain;
        if (!isBoundedMain)
            finalInnerMain = totalNatural;
        else if (mainAxisSize == MainAxisSize::min)
            finalInnerMain = std::min(totalNatural, innerMain);
        else
            finalInnerMain = innerMain;

        float freeMain = std::max(0.0f, finalInnerMain - totalNatural);
        if (totalFlex > 0 && freeMain > 0)
        {
            float flexUnit = freeMain / totalFlex;
            for (size_t i = 0; i < infos.size(); ++i)
            {
                if (infos[i].flex > 0)
                {
                    float allocated = infos[i].flex * flexUnit;
                    float childMainSize = std::max(0.0f, allocated - infos[i].marginMain);
                    float childCrossMax = innerCross;
                    float childCrossMin = (crossAlign == CrossAxisAlignment::stretch)
                                              ? childCrossMax
                                              : 0.0f;
                    Constraints tightBC =
                        makeFlexAxisConstraints(isRow_, childMainSize, childMainSize,
                                                childCrossMin, childCrossMax);
                    infos[i].node->layout(tightBC);
                    naturalMain[i] =
                        isRow_ ? infos[i].node->geometry.w : infos[i].node->geometry.h;
                }
            }
        }

        float totalMainUsed = 0;
        for (size_t i = 0; i < infos.size(); ++i)
            totalMainUsed += naturalMain[i] + infos[i].marginMain;

        float extraMain = std::max(0.0f, finalInnerMain - totalMainUsed);
        float mainGap = 0, mainStartOff = 0;
        size_t childCount = infos.size();
        if (childCount == 1)
        {
            if (mainAlign == MainAxisAlignment::spaceAround ||
                mainAlign == MainAxisAlignment::spaceEvenly)
                mainStartOff = extraMain / 2;
        }
        else if (childCount > 1)
        {
            switch (mainAlign)
            {
            case MainAxisAlignment::start:
                mainStartOff = 0;
                break;
            case MainAxisAlignment::end:
                mainStartOff = extraMain;
                break;
            case MainAxisAlignment::center:
                mainStartOff = extraMain / 2;
                break;
            case MainAxisAlignment::spaceBetween:
                mainGap = extraMain / (childCount - 1);
                break;
            case MainAxisAlignment::spaceAround:
                mainGap = extraMain / childCount;
                mainStartOff = mainGap / 2;
                break;
            case MainAxisAlignment::spaceEvenly:
                mainGap = extraMain / (childCount + 1);
                mainStartOff = mainGap;
                break;
            }
        }

        float mainPos = mainStartOff;
        float maxBaseline = 0;
        if (crossAlign == CrossAxisAlignment::baseline && isRow_)
        {
            for (const auto &info : infos)
                maxBaseline = std::max(maxBaseline, info.node->baseline);
        }

        for (size_t i = 0; i < infos.size(); ++i)
        {
            Node &child = *infos[i].node;
            float childMainLen = naturalMain[i];
            float childCrossLen = isRow_ ? child.geometry.h : child.geometry.w;
            float leadingMargin = isRow_ ? child.margin.left : child.margin.top;
            setChildMainAxisPosition(child, dir, mainPos + leadingMargin, childMainLen,
                                     mainSize, padMainStart, padMainEnd);
            float crossExtra =
                std::max(0.0f, innerCross - (childCrossLen + infos[i].marginCross));
            float crossStart = 0;
            switch (crossAlign)
            {
            case CrossAxisAlignment::start:
                crossStart = 0;
                break;
            case CrossAxisAlignment::end:
                crossStart = crossExtra;
                break;
            case CrossAxisAlignment::center:
                crossStart = crossExtra / 2;
                break;
            case CrossAxisAlignment::stretch:
                crossStart = 0;
                break;
            case CrossAxisAlignment::baseline:
                if (isRow_)
                    crossStart = maxBaseline - child.baseline;
                break;
            }
            float crossLeadingMargin = isRow_ ? child.margin.top : child.margin.left;
            setChildCrossAxisPosition(child, dir, crossStart + crossLeadingMargin,
                                      childCrossLen, crossSize, padCrossStart,
                                      padCrossEnd);
            mainPos += childMainLen + infos[i].marginMain + mainGap;
        }

        Size size;
        if (isRow_)
        {
            size.width = finalInnerMain + padding.horizontal();
            if (crossAlign == CrossAxisAlignment::stretch)
                size.height = innerCross + padding.vertical();
            else
            {
                float maxChildHeight = 0;
                for (const auto &info : infos)
                    maxChildHeight = std::max(maxChildHeight, info.node->geometry.h);
                size.height = maxChildHeight + padding.vertical();
            }
        }
        else
        {
            if (crossAlign == CrossAxisAlignment::stretch)
                size.width = innerCross + padding.horizontal();
            else
            {
                float maxChildWidth = 0;
                for (const auto &info : infos)
                    maxChildWidth = std::max(maxChildWidth, info.node->geometry.w);
                size.width = maxChildWidth + padding.horizontal();
            }
            size.height = finalInnerMain + padding.vertical();
        }
        if (fixedWidth)
            size.width = *fixedWidth;
        if (fixedHeight)
            size.height = *fixedHeight;
        size = borderBC.clamp(size);
        geometry = BoxGeometry{0, 0, size.width, size.height};
        if (!infos.empty())
        {
            Node &first = *infos[0].node;
            baseline = isRow_ ? first.baseline + first.geometry.y
                              : first.baseline + first.geometry.x;
        }
        else
        {
            baseline = isRow_ ? size.height : size.width;
        }
    }

  private:
    AxisDirection axisDirection() const
    {
        if (isRow_)
            return (textDirection == TextDirection::ltr) ? AxisDirection::right
                                                         : AxisDirection::left;
        else
            return (verticalDirection == VerticalDirection::down) ? AxisDirection::down
                                                                  : AxisDirection::up;
    }
    bool isAxisForward(AxisDirection dir) const
    {
        return dir == AxisDirection::right || dir == AxisDirection::down;
    }
    float mainAxisSizeFromConstraints(const Constraints &bc) const
    {
        return isRow_ ? bc.maxW : bc.maxH;
    }
    float crossAxisSizeFromConstraints(const Constraints &bc) const
    {
        return isRow_ ? bc.maxH : bc.maxW;
    }
    float mainAxisPaddingStart(const EdgeInsets &pad, AxisDirection dir) const
    {
        switch (dir)
        {
        case AxisDirection::right:
            return pad.left;
        case AxisDirection::left:
            return pad.right;
        case AxisDirection::down:
            return pad.top;
        case AxisDirection::up:
            return pad.bottom;
        }
        return 0;
    }
    float mainAxisPaddingEnd(const EdgeInsets &pad, AxisDirection dir) const
    {
        switch (dir)
        {
        case AxisDirection::right:
            return pad.right;
        case AxisDirection::left:
            return pad.left;
        case AxisDirection::down:
            return pad.bottom;
        case AxisDirection::up:
            return pad.top;
        }
        return 0;
    }
    float crossAxisPaddingStart(const EdgeInsets &pad, AxisDirection dir) const
    {
        return (dir == AxisDirection::right || dir == AxisDirection::left) ? pad.top
                                                                           : pad.left;
    }
    float crossAxisPaddingEnd(const EdgeInsets &pad, AxisDirection dir) const
    {
        return (dir == AxisDirection::right || dir == AxisDirection::left) ? pad.bottom
                                                                           : pad.right;
    }
    Constraints makeFlexAxisConstraints(bool isRow, float minMain, float maxMain,
                                        float minCross, float maxCross) const
    {
        return isRow ? Constraints{minMain, maxMain, minCross, maxCross}
                     : Constraints{minCross, maxCross, minMain, maxMain};
    }
    void setChildMainAxisPosition(Node &child, AxisDirection dir, float pos,
                                  float childMainLen, float parentMainSize,
                                  float padStart, float padEnd)
    {
        float physical;
        if (isAxisForward(dir))
            physical = padStart + pos;
        else
            physical = parentMainSize - padEnd - pos - childMainLen;
        if (dir == AxisDirection::right || dir == AxisDirection::left)
            child.geometry.x = physical;
        else
            child.geometry.y = physical;
    }
    void setChildCrossAxisPosition(Node &child, AxisDirection dir, float pos,
                                   float childCrossLen, float parentCrossSize,
                                   float padCrossStart, float padCrossEnd)
    {
        (void)childCrossLen;
        (void)parentCrossSize;
        (void)padCrossEnd;
        if (dir == AxisDirection::right || dir == AxisDirection::left)
            child.geometry.y = padCrossStart + pos;
        else
            child.geometry.x = padCrossStart + pos;
    }
};

// ============================================================================
// Row 和 Column 具体节点
// ============================================================================

class RowNode : public FlexNode<true>
{
  public:
    explicit RowNode(std::string name, std::optional<float> width = std::nullopt,
                     std::optional<float> height = std::nullopt,
                     MainAxisAlignment ma = MainAxisAlignment::start,
                     CrossAxisAlignment ca = CrossAxisAlignment::start,
                     MainAxisSize ms = MainAxisSize::max, EdgeInsets m = {},
                     EdgeInsets p = {}, TextDirection td = TextDirection::ltr)
        : FlexNode<true>(std::move(name), width, height, m, p, ma, ca, ms, td)
    {
    }
};

class ColumnNode : public FlexNode<false>
{
  public:
    explicit ColumnNode(std::string name, std::optional<float> width = std::nullopt,
                        std::optional<float> height = std::nullopt,
                        MainAxisAlignment ma = MainAxisAlignment::start,
                        CrossAxisAlignment ca = CrossAxisAlignment::start,
                        MainAxisSize ms = MainAxisSize::max, EdgeInsets m = {},
                        EdgeInsets p = {}, VerticalDirection vd = VerticalDirection::down)
        : FlexNode<false>(std::move(name), width, height, m, p, ma, ca, ms,
                          TextDirection::ltr, vd)
    {
    }
};

// ============================================================================
// Builder 类
// ============================================================================

// 辅助函数：将任意类型转换为 Widget（支持 Widget 和 Builder）
template <typename T>
Widget toWidget(T &&arg)
{
    if constexpr (std::is_convertible_v<T, Widget>)
    {
        return std::forward<T>(arg);
    }
    else
    {
        return std::forward<T>(arg).build();
    }
}

// 通用数据挂载方法，所有 Builder 继承使用
template <typename Derived>
class DataMixin
{
  public:
    template <typename T, typename... Args>
    Derived &data(static_string key, Args &&...args)
    {
        static_cast<Derived *>(this)->node->template addData<T>(
            key, std::forward<Args>(args)...);
        return *static_cast<Derived *>(this);
    }
};

class ContainerBuilder : public DataMixin<ContainerBuilder>
{
    pmr_unique_ptr<ContainerNode> node;

  public:
    explicit ContainerBuilder(std::string name)
        : node(make_pmr_unique<ContainerNode>(nodeResource, std::move(name)))
    {
    }
    ContainerBuilder &width(float w)
    {
        node->fixedWidth = w;
        return *this;
    }
    ContainerBuilder &height(float h)
    {
        node->fixedHeight = h;
        return *this;
    }
    ContainerBuilder &margin(EdgeInsets m)
    {
        node->margin = m;
        return *this;
    }
    ContainerBuilder &padding(EdgeInsets p)
    {
        node->padding = p;
        return *this;
    }
    ContainerBuilder &border(EdgeInsets b)
    {
        node->border = b;
        return *this;
    }
    ContainerBuilder &alignment(Alignment a)
    {
        node->alignment = a;
        return *this;
    }
    ContainerBuilder &constraints(Constraints c)
    {
        node->ownConstraints = c;
        return *this;
    }
    template <typename T>
    ContainerBuilder &child(T &&child)
    {
        Widget childWidget = toWidget(std::forward<T>(child));
        node->addChild(std::move(childWidget));
        return *this;
    }
    Widget build()
    {
        assert(node != nullptr);
        return std::move(node);
    }

    template <typename>
    friend class DataMixin; // 允许访问私有成员 node
};

class RowBuilder : public DataMixin<RowBuilder>
{
    pmr_unique_ptr<RowNode> node;

  public:
    explicit RowBuilder(std::string name)
        : node(make_pmr_unique<RowNode>(nodeResource, std::move(name)))
    {
    }
    RowBuilder &width(float w)
    {
        node->fixedWidth = w;
        return *this;
    }
    RowBuilder &height(float h)
    {
        node->fixedHeight = h;
        return *this;
    }
    RowBuilder &margin(EdgeInsets m)
    {
        node->margin = m;
        return *this;
    }
    RowBuilder &padding(EdgeInsets p)
    {
        node->padding = p;
        return *this;
    }
    RowBuilder &mainAxisAlignment(MainAxisAlignment ma)
    {
        node->mainAlign = ma;
        return *this;
    }
    RowBuilder &crossAxisAlignment(CrossAxisAlignment ca)
    {
        node->crossAlign = ca;
        return *this;
    }
    RowBuilder &mainAxisSize(MainAxisSize ms)
    {
        node->mainAxisSize = ms;
        return *this;
    }
    RowBuilder &textDirection(TextDirection td)
    {
        node->textDirection = td;
        return *this;
    }

    template <typename... Children>
    RowBuilder &children(Children &&...children)
    {
        (
            [&] {
                Widget childWidget = toWidget(std::forward<Children>(children));
                node->addChild(std::move(childWidget));
            }(),
            ...);
        return *this;
    }

    Widget build()
    {
        assert(node != nullptr);
        return std::move(node);
    }

    template <typename>
    friend class DataMixin;
};

class ColumnBuilder : public DataMixin<ColumnBuilder>
{
    pmr_unique_ptr<ColumnNode> node;

  public:
    explicit ColumnBuilder(std::string name)
        : node(make_pmr_unique<ColumnNode>(nodeResource, std::move(name)))
    {
    }
    ColumnBuilder &width(float w)
    {
        node->fixedWidth = w;
        return *this;
    }
    ColumnBuilder &height(float h)
    {
        node->fixedHeight = h;
        return *this;
    }
    ColumnBuilder &margin(EdgeInsets m)
    {
        node->margin = m;
        return *this;
    }
    ColumnBuilder &padding(EdgeInsets p)
    {
        node->padding = p;
        return *this;
    }
    ColumnBuilder &mainAxisAlignment(MainAxisAlignment ma)
    {
        node->mainAlign = ma;
        return *this;
    }
    ColumnBuilder &crossAxisAlignment(CrossAxisAlignment ca)
    {
        node->crossAlign = ca;
        return *this;
    }
    ColumnBuilder &mainAxisSize(MainAxisSize ms)
    {
        node->mainAxisSize = ms;
        return *this;
    }
    ColumnBuilder &verticalDirection(VerticalDirection vd)
    {
        node->verticalDirection = vd;
        return *this;
    }

    template <typename... Children>
    ColumnBuilder &children(Children &&...children)
    {
        (
            [&] {
                Widget childWidget = toWidget(std::forward<Children>(children));
                node->addChild(std::move(childWidget));
            }(),
            ...);
        return *this;
    }

    Widget build()
    {
        assert(node != nullptr);
        return std::move(node);
    }

    template <typename>
    friend class DataMixin;
};

class ExpandedBuilder : public DataMixin<ExpandedBuilder>
{
    pmr_unique_ptr<ExpandedNode> node;

  public:
    explicit ExpandedBuilder(int flex = 1, std::string name = "expanded")
        : node(make_pmr_unique<ExpandedNode>(nodeResource, flex, std::move(name)))
    {
    }
    ExpandedBuilder &flex(int f)
    {
        node->flex = f;
        return *this;
    }
    template <typename T>
    ExpandedBuilder &child(T &&child)
    {
        Widget childWidget = toWidget(std::forward<T>(child));
        node->addChild(std::move(childWidget));
        return *this;
    }
    Widget build()
    {
        assert(node != nullptr);
        return std::move(node);
    }

    template <typename>
    friend class DataMixin;
};

// ============================================================================
// 顶层入口函数
// ============================================================================

inline ContainerBuilder Container(std::string name)
{
    return ContainerBuilder(std::move(name));
}
inline RowBuilder Row(std::string name)
{
    return RowBuilder(std::move(name));
}
inline ColumnBuilder Column(std::string name)
{
    return ColumnBuilder(std::move(name));
}
inline ExpandedBuilder Expanded(int flex = 1, std::string name = "expanded")
{
    return ExpandedBuilder(flex, std::move(name));
}

// ============================================================================
// 测试：Node 数据挂载功能（使用 Builder 直接挂载）
// ============================================================================

// 测试数据类型定义（包含构造函数）
struct MeshData : IData
{
    int vertexCount;
    std::string modelName;

    MeshData() : vertexCount(0), modelName("") {}
    MeshData(int vc, std::string name) : vertexCount(vc), modelName(std::move(name)) {}
    MeshData(int vc, const char *name) : vertexCount(vc), modelName(name) {}
};

struct PhysicsData : IData
{
    float mass;
    bool isStatic;

    PhysicsData() : mass(0.0f), isStatic(true) {}
    PhysicsData(float m, bool s) : mass(m), isStatic(s) {}
};

// 测试1：Builder 直接挂载并获取单个数据
bool test_builder_add_and_get_single_data()
{
    auto root = Container("root").data<MeshData>("mesh", 100, "Cube").build();

    auto *mesh = root->getData<MeshData>("mesh");
    if (!mesh)
    {
        std::cerr << "FAIL [builder_single_data]: getData returned nullptr\n";
        return false;
    }
    if (mesh->vertexCount != 100 || mesh->modelName != "Cube")
    {
        std::cerr << "FAIL [builder_single_data]: data mismatch\n";
        return false;
    }
    return true;
}

// 测试2：Builder 挂载多个同类型数据并获取指定索引
bool test_builder_multiple_data_same_key()
{
    auto root = Row("row")
                    .data<PhysicsData>("physics", 1.0f, false)
                    .data<PhysicsData>("physics", 2.5f, true)
                    .build();

    auto *get0 = root->getData<PhysicsData>("physics", 0);
    auto *get1 = root->getData<PhysicsData>("physics", 1);

    if (!get0 || !get1)
    {
        std::cerr
            << "FAIL [builder_multiple_data]: getData returned nullptr for valid index\n";
        return false;
    }
    if (get0->mass != 1.0f || get0->isStatic != false || get1->mass != 2.5f ||
        get1->isStatic != true)
    {
        std::cerr << "FAIL [builder_multiple_data]: data values incorrect\n";
        return false;
    }
    return true;
}

// 测试3：Builder 挂载后使用错误类型获取应返回 nullptr
bool test_builder_wrong_type_returns_null()
{
    auto root = Column("column").data<MeshData>("mesh", 200, "Sphere").build();

    auto *wrong = root->getData<PhysicsData>("mesh");
    if (wrong != nullptr)
    {
        std::cerr << "FAIL [builder_wrong_type]: expected nullptr for wrong type\n";
        return false;
    }
    return true;
}

// 测试4：Builder 挂载后获取不存在的键应返回 nullptr
bool test_builder_nonexistent_key_returns_null()
{
    auto root = Container("root").build();
    auto *data = root->getData<MeshData>("not_there");
    if (data != nullptr)
    {
        std::cerr << "FAIL [builder_nonexistent_key]: expected nullptr\n";
        return false;
    }
    return true;
}

// 测试5：Builder 挂载后修改数据应反映更改
bool test_builder_modify_data_reflects_change()
{
    auto root = Row("row").data<MeshData>("mesh").build();

    auto *mesh = root->getData<MeshData>("mesh");
    if (!mesh)
    {
        std::cerr << "FAIL [builder_modify_data]: getData returned nullptr\n";
        return false;
    }
    mesh->vertexCount = 200;

    auto *again = root->getData<MeshData>("mesh");
    if (again->vertexCount != 200)
    {
        std::cerr << "FAIL [builder_modify_data]: modification not reflected\n";
        return false;
    }
    return true;
}

// 测试6：Builder 挂载后移除数据
bool test_builder_remove_data()
{
    auto root = Container("root")
                    .data<PhysicsData>("physics", 1.0f, false)
                    .data<PhysicsData>("physics", 2.0f, true)
                    .build();

    if (root->getDataCount("physics") != 2)
    {
        std::cerr << "FAIL [builder_remove_data]: initial count incorrect\n";
        return false;
    }

    bool removed = root->removeData("physics", 0);
    if (!removed)
    {
        std::cerr << "FAIL [builder_remove_data]: removeData returned false\n";
        return false;
    }

    if (root->getDataCount("physics") != 1)
    {
        std::cerr << "FAIL [builder_remove_data]: count after removal incorrect\n";
        return false;
    }

    auto *remaining = root->getData<PhysicsData>("physics");
    if (!remaining || remaining->mass != 2.0f)
    {
        std::cerr << "FAIL [builder_remove_data]: remaining data incorrect\n";
        return false;
    }
    return true;
}

// 测试7：Builder 挂载的数据在节点移动后仍然保留
bool test_builder_data_survives_node_move()
{
    auto root = Row("root")
                    .children(Container("childA").data<MeshData>("mesh", 42, "Teapot"),
                              Container("childB"))
                    .build();

    Node *childA = root->children[0].get();
    Node *childB = root->children[1].get();

    Node::moveNode(childA, childB); // 移动 childA 到 childB 下

    if (childA->parent != childB)
    {
        std::cerr << "FAIL [builder_data_move]: parent pointer incorrect after move\n";
        return false;
    }

    auto *mesh = childA->getData<MeshData>("mesh");
    if (!mesh || mesh->vertexCount != 42 || mesh->modelName != "Teapot")
    {
        std::cerr << "FAIL [builder_data_move]: data lost or corrupted after move\n";
        return false;
    }
    return true;
}

// 测试8：Builder 挂载的数据在不同节点之间互不影响
bool test_builder_data_isolation_between_nodes()
{
    auto root = Row("root")
                    .children(Container("child1").data<MeshData>("mesh", 10, "A"),
                              Container("child2").data<MeshData>("mesh", 20, "B"))
                    .build();

    Node *child1 = root->children[0].get();
    Node *child2 = root->children[1].get();

    auto *data1 = child1->getData<MeshData>("mesh");
    auto *data2 = child2->getData<MeshData>("mesh");

    if (!data1 || !data2)
    {
        std::cerr << "FAIL [builder_data_isolation]: data not found\n";
        return false;
    }
    if (data1->vertexCount != 10 || data1->modelName != "A" || data2->vertexCount != 20 ||
        data2->modelName != "B")
    {
        std::cerr << "FAIL [builder_data_isolation]: data values incorrect\n";
        return false;
    }
    return true;
}

// ============================================================================
// 运行所有测试
// ============================================================================

bool runTests()
{
    return test_builder_add_and_get_single_data() &&
           test_builder_multiple_data_same_key() &&
           test_builder_wrong_type_returns_null() &&
           test_builder_nonexistent_key_returns_null() &&
           test_builder_modify_data_reflects_change() && test_builder_remove_data() &&
           test_builder_data_survives_node_move() &&
           test_builder_data_isolation_between_nodes();
}

int main()
{
    if (runTests())
    {
        std::cout << "All builder data attachment tests passed!\n";
        return 0;
    }
    return 1;
}