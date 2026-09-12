#pragma once

#include <algorithm>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

template <typename T>
concept constraints = requires(const T &t) {
    { t.isTight() } noexcept -> std::same_as<bool>;
    { t.isNormalized() } noexcept -> std::same_as<bool>;
};

// NOLINTBEGIN
// clang-format off
struct Size
{
    double width, height;
};
struct Offset
{
    double x, y;
};
enum class Axis               : std::uint8_t { horizontal, vertical };
enum class FlexFit            : std::uint8_t { tight, loose };
enum class MainAxisSize       : std::uint8_t { min, max };
enum class MainAxisAlignment  : std::uint8_t { start, end, center, spaceBetween, spaceAround, spaceEvenly };
enum class CrossAxisAlignment : std::uint8_t { start, end, center, stretch, baseline };
enum class TextDirection      : std::uint8_t { rtl, ltr };
enum class VerticalDirection  : std::uint8_t { up, down };
enum class TextBaseline       : std::uint8_t { alphabetic, ideographic };

struct EdgeInsetsGeometry
{
    static constexpr auto inf = std::numeric_limits<double>::infinity();
    double left,right,top,bottom,start,end;
    
    static constexpr auto fromLRSETB(double left,double right,double start,double end,double top,double bottom) noexcept
    { return EdgeInsetsGeometry{.left=left,.right=right,.top=top,.bottom=bottom,.start=start,.end=end}; }
    static constexpr auto fromLTRB(double left,double top,double right,double bottom) noexcept 
    { return EdgeInsetsGeometry{.left=left,.right=right,.top=top,.bottom=bottom,.start=0,.end=0}; }
     static constexpr auto fromSTEB(double start,double top,double end,double bottom)noexcept
    { return EdgeInsetsGeometry{.left=0,.right=0,.top=top,.bottom=bottom,.start=start,.end=end}; }

    // api
    [[nodiscard]] constexpr auto horizontal()const noexcept { return left + right + start + end; }
    [[nodiscard]] constexpr auto vertical()const noexcept { return top + bottom; }
    // The total offset in the given direction.
    [[nodiscard]] constexpr double along(Axis axis) const noexcept
    {
        switch (axis) 
        {
            case  Axis::horizontal : return horizontal();
            case  Axis::vertical   : return vertical();
        };
    }
    [[nodiscard]] constexpr Size collapsedSize() const noexcept { return {horizontal(), vertical()}; }
    [[nodiscard]] constexpr bool isNonNegative() const noexcept { return left >= 0 && right >= 0 && top >= 0 && bottom >= 0 && start>=0 && end >= 0; }
};
struct EdgeInsets : EdgeInsetsGeometry
{
    static constexpr auto all(double value) noexcept
    {
        return EdgeInsetsGeometry::fromLTRB(value,value,value,value);
    }
};


struct Alignment
{
    double x, y;
    struct _Initializer{float x, y; constexpr operator Alignment() const noexcept { return {x, y};}};
    constexpr static _Initializer topLeft{-1, -1}, topCenter{0, -1}, topRight{1, -1};
    constexpr static _Initializer centerLeft{-1, 0}, center{0, 0}, centerRight{1, 0};
    constexpr static _Initializer bottomLeft{-1, 1}, bottomCenter{0, 1}, bottomRight{1, 1};
};


class BoxConstraints
{
  public:
    static constexpr auto inf = std::numeric_limits<double>::infinity();
    constexpr explicit BoxConstraints(double minWidth = 0, double maxWidth = inf,
                                      double minHeight = 0, double maxHeight = inf)
        : minWidth_{minWidth}, maxWidth_{maxWidth}, minHeight_{minHeight},
          maxHeight_{maxHeight}
    {
    }

    [[nodiscard]] constexpr double minWidth() const noexcept { return minWidth_; }
    [[nodiscard]] constexpr double maxWidth() const noexcept { return maxWidth_; }
    [[nodiscard]] constexpr double minHeight() const noexcept { return minHeight_; }
    [[nodiscard]] constexpr double maxHeight() const noexcept { return maxHeight_; }

    // constraints  concept
    [[nodiscard]] constexpr bool hasTightWidth() const noexcept { return minWidth_ >= maxWidth_; }
    [[nodiscard]] constexpr bool hasTightHeight() const noexcept { return minHeight_ >= maxHeight_; }
    [[nodiscard]] constexpr bool isTight() const noexcept { return hasTightWidth() && hasTightHeight(); }
    [[nodiscard]] constexpr bool isNormalized() const noexcept
    { return minWidth_ >= 0.0 && minWidth_ <= maxWidth_ && minHeight_ >= 0.0 && minHeight_ <= maxHeight_; }
    [[nodiscard]] constexpr bool hasBoundedWidth() const noexcept { return maxWidth_ < inf; }
    [[nodiscard]] constexpr bool hasBoundedHeight() const noexcept { return maxHeight_ < inf; }
    [[nodiscard]] constexpr bool hasInfiniteWidth() const noexcept { return minWidth_ >= inf; }
    [[nodiscard]] constexpr bool hasInfiniteHeight() const noexcept { return minHeight_ >= inf; }

    [[nodiscard]] constexpr bool isSatisfiedBy(Size size) const noexcept 
    {     
        return  (minWidth_ <= size.width) && (size.width <= maxWidth_) &&
                (minHeight_ <= size.height) && (size.height <= maxHeight_);
    }
    [[nodiscard]] constexpr auto normalize()const noexcept  
    {
        if (isNormalized())
            return *this;
        auto minWidth = minWidth_ >= 0.0 ? minWidth_ : 0.0;
        auto minHeight = minHeight_ >= 0.0 ? minHeight_ : 0.0;
        return BoxConstraints(
                minWidth,
                minWidth > maxWidth_ ? minWidth : maxWidth_,
                minHeight,
                minHeight > maxHeight_ ? minHeight : maxHeight_);
    }

    // 语义相关 API
    static constexpr auto tight(Size size) noexcept
    { return BoxConstraints{size.width,size.width,size.height,size.height}; }
    struct Optional {std::optional<double> width,height;};
    // null → 宽松;非 null 值 -> 紧
    static constexpr auto tightFor(Optional op) noexcept
    {
        auto [width,height] = op;
        return BoxConstraints{width.value_or(0.0),width.value_or(inf),height.value_or(0.0),height.value_or(inf)};
    }
    struct Finite {double width = inf, height = inf;};
    // infinity → 宽松; 有限数值 → 紧
    static constexpr auto tightForFinite(Finite op) noexcept
    {
        auto [width,height] = op;
        return BoxConstraints{width != inf ? width : 0.0,width != inf ? width : inf,height != inf ? height : 0.0,height != inf ? height : inf};
    }

    static constexpr auto loose(Size size) noexcept
    { return BoxConstraints{0,size.width,0,size.height}; }
    // expand 默认是无限大紧约束; 不设置维度，则该维度无限大
    static constexpr auto expand(Optional op = {}) noexcept
    {
        auto [width,height] = op;
        return BoxConstraints{width.value_or(inf),width.value_or(inf),height.value_or(inf),height.value_or(inf)};
    }

    // 成员API
    struct OptionalConstraints {std::optional<double> minWidth,maxWidth,minHeight,maxHeight;};
    // 无维度定义，则复制当前维度
    constexpr auto copyWith(OptionalConstraints c) const noexcept
    {
        auto [minWidth,maxWidth,minHeight,maxHeight] =c;
        return BoxConstraints{minWidth.value_or(minWidth_),maxWidth.value_or(maxWidth_),minHeight.value_or(minHeight_),maxHeight.value_or(maxHeight_)};
    }
    // 根据给定的边距（内边距）缩小当前的盒子约束，返回一个更紧的新约束，表示在扣除这些边距后，子组件可用的空间范围
    constexpr auto deflate(EdgeInsetsGeometry edges) const noexcept
    {
        double horizontal = edges.horizontal();
        double vertical = edges.vertical();
        double deflatedMinWidth = std::max(0.0, minWidth_ - horizontal);
        double deflatedMinHeight =  std::max(0.0, minHeight_ - vertical);
        return BoxConstraints{
        deflatedMinWidth,
        std::max(deflatedMinWidth, maxWidth_ - horizontal),
        deflatedMinHeight,
        std::max(deflatedMinHeight, maxHeight_ - vertical)};
    }
    constexpr auto enforce(BoxConstraints constraints) const noexcept
    {
        return BoxConstraints{
            std::clamp(minWidth_, constraints.minWidth_, constraints.maxWidth_),
            std::clamp(maxWidth_, constraints.minWidth_, constraints.maxWidth_),
            std::clamp(minHeight_, constraints.minHeight_, constraints.maxHeight_),
            std::clamp(maxHeight_, constraints.minHeight_, constraints.maxHeight_)};
    }
    constexpr auto tighten(Optional op) const noexcept
    {
        auto [ width,height] = op;
        return BoxConstraints{
            !width.has_value() ? minWidth_ : std::clamp(*width, minWidth_, maxWidth_),
            !width.has_value() ? maxWidth_ : std::clamp(*width, minWidth_, maxWidth_),
            !height.has_value() ? minHeight_ : std::clamp(*height, minHeight_, maxHeight_),
            !height.has_value() ? maxHeight_ : std::clamp(*height, minHeight_, maxHeight_)};
    }

    [[nodiscard]] constexpr auto loosen() const noexcept
    { return BoxConstraints{0.0, maxWidth_, 0.0, maxHeight_}; }

    [[nodiscard]] constexpr auto flipped() const noexcept
    { return BoxConstraints{minHeight_, maxHeight_, minWidth_, maxWidth_}; }
    
    constexpr auto constrainWidth(double width = inf ) const noexcept { return std::clamp(width, minWidth_, maxWidth_); } 
    constexpr auto constrainHeight(double height = inf ) const noexcept { return std::clamp(height, minHeight_, maxHeight_); } 
    constexpr Size constrain(Size size) const noexcept { return {constrainWidth(size.width), constrainHeight(size.height)}; }
    constexpr Size constrainDimensions(double width, double height) const noexcept { return constrain(Size{width,height}); }

    // The biggest size that satisfies the constraints.
    constexpr Size biggest() const noexcept { return Size(constrainWidth(), constrainHeight());}
    // The smallest size that satisfies the constraints.
    constexpr Size smallest() const noexcept { return Size(constrainWidth(0), constrainHeight(0));}

    [[nodiscard]] constexpr bool operator==(const BoxConstraints &) const noexcept = default;

    [[nodiscard]] std::string toString() const
    {
        auto fmt1 = [](double v) {
            if (std::isinf(v)) return std::string("inf");
            return std::to_string(v);
        };
        return "BoxConstraints(w=[" + fmt1(minWidth_) + "," + fmt1(maxWidth_) + "]"
            ", h=[" + fmt1(minHeight_) + "," + fmt1(maxHeight_) + "])";
    }
  private:
    double minWidth_;
    double maxWidth_;
    double minHeight_;
    double maxHeight_;
};

static_assert(constraints<BoxConstraints>, "should satisfy");


// 在 Flutter 中，尺寸（w, h）先计算，位置（x, y）后设置
struct RenderGeometry
{
    Size size;
    Offset offset;
};
struct RenderElement
{
};
struct RenderObject
{
    virtual ~RenderObject() = default;
    std::vector<std::unique_ptr<RenderElement>> elements;
};
struct RenderBox
{
  RenderGeometry layoutGeometry;
  void render()
  {

  }
};
struct BuildContext
{

};

// 自上而下传递约束（Constraints）
// 自下而上汇报尺寸（Size）
struct Widget
{ 
    std::string key{};
    Size size{};
    Offset offset{};
    virtual constexpr void layout(BoxConstraints c) = 0;
    virtual constexpr void updateOffset(Offset offset) noexcept = 0;
    virtual constexpr std::vector<Widget*> children() = 0;
    virtual ~Widget()=default;
    constexpr auto& Self() noexcept { return *this; }

     // 前序遍历：先自己，再按顺序遍历每个子节点
    template <typename Fn>
    constexpr void visit(Fn &&fn)
    {
        fn(*this);
        for (auto *child : children())
            if (child) child->visit(std::forward<Fn>(fn));
    }

    // 前序查找第一个满足谓词的节点，找不到返回 nullptr
    template <typename Pred>
    constexpr Widget *findIf(Pred &&pred)
    {
        if (pred(*this)) return this;
        for (auto *child : children())
            if (child)
                if (auto *found = child->findIf(std::forward<Pred>(pred)))
                    return found;
        return nullptr;
    }

    // 按 key 查找
    [[nodiscard]] constexpr Widget *findByKey(std::string_view k)
    {
        return findIf([k](const Widget &w) { return w.key == k; });
    }
};

template <typename B>
concept WidgetBuild = requires(B &&b) {
    static_cast<std::unique_ptr<Widget>>(std::forward<B>(b));
};
struct ScreenWidget
{
    constexpr void layout()
    {
        if(auto *ptr = root_.get())
        {
            ptr->layout(BoxConstraints{0,size_.width,0,size_.height});
            ptr->updateOffset({0,0});
        }
    }
    constexpr void update(double width,double height)
    {
        size_ = {width,height};
        layout();

    }
    ScreenWidget()=default;
    constexpr ScreenWidget(Size size,std::unique_ptr<Widget> root) noexcept : size_(size) , root_(std::move(root)) {}
    template <WidgetBuild Build>
    constexpr ScreenWidget(Size size,Build &&b) noexcept : ScreenWidget(size,static_cast<std::unique_ptr<Widget>>(std::forward<Build>(b))) {}
    
    constexpr auto& size(Size size) noexcept 
    {
        size_ = size;
        return *this;
    }
    constexpr auto& size(double width, double height) noexcept { return size({width,height});}
    
    template <WidgetBuild Build>
    constexpr auto &root(Build &&b) noexcept 
    { 
        root_ = static_cast<std::unique_ptr<Widget>>(std::forward<Build>(b));
        return *this;
    }

    // root 访问器：给外部拿根节点（需要时可加 const 重载）
    [[nodiscard]] constexpr Widget *root() noexcept { return root_.get(); }
     // 从根节点开始转发
    template <typename Fn>
    constexpr void visit(Fn &&fn) { if (root_) root_->visit(std::forward<Fn>(fn)); }

    template <typename Pred>
    constexpr Widget *findIf(Pred &&pred)
    { return root_ ? root_->findIf(std::forward<Pred>(pred)) : nullptr; }

    [[nodiscard]] constexpr Widget *findByKey(std::string_view k)
    { return root_ ? root_->findByKey(k) : nullptr; }

private:
    Size size_;
    std::unique_ptr<Widget> root_;
};

// clang-format on
// NOLINTEND

/*
在 Flutter 中，只要父级和子级没有同时强制“唯一宽高”（即紧约束），它们确实会互相“协商”并影响对方的大小。

但是，这个“互相”必须严格遵守 Flutter 布局的第一大铁律：

约束（Constraints）向下传递，大小（Size）向上传递，位置由父级决定。

这不是“双向同时谈判”，而是严格的两步流水线。为了让你看透本质，我们分三种情况拆解：

1. 子级能改变父级大小吗？（向上影响）—— 能！
条件：父级没有固定宽高（即父级对子级传递的是松约束，比如 0 ≤ w ≤ ∞）。

过程：

父级说：“孩子，你想多大就多大（松约束）。”
子级说：“那我算出我要 200 宽。”
子级向上返回 size = 200。
父级一看：“哦，孩子是 200，我没有固定宽高，那我就把自己也设为 200 宽吧！”
典型例子：Container 没有设宽高，但它的孩子是一个 Container(width: 200)，那么父级会被撑成 200 宽。

2. 父级能改变子级大小吗？（向下影响）—— 能！
条件：父级有固定宽高或紧约束（比如父级宽 100）。

过程：

父级说：“你必须正好是 100 宽（紧约束）。”
子级就算想当 200，也得乖乖算出 100，向上返回 size = 100。
典型例子：父级是 SizedBox(width: 100)，孩子是 Container(width: 200)，最终孩子被强制变成 100（除非溢出报错）。

3. 为什么不会变成“死循环”？—— 因为你说的“不强制唯一的宽高”是关键
你敏锐地注意到了“如果不强制唯一的宽高”这个前提。如果父子都没有强制唯一宽高（即都是松约束），流程是这样的：

父级先出牌：父级把当前的约束（比如 0-∞）传给子级（方向：向下）。

子级算结果：子级根据自己的 child 算出自己的 size。

父级拿结果：父级读取子级的 size，据此算出自己的 size（方向：向上）。

结束！这里绝对不存在“循环回推”。

为什么不会无限循环？
因为约束（Constraints）和大小（Size）是两种完全不同的对象。父级在把约束给子级之前，就已经确定了自己的约束。父级不会因为子级返回了尺寸，就回过头去“修改”刚才传给子级的约束。它是先定规矩，再量尺寸。

===============================================================================================
多子带来唯一的新概念：“分配剩余空间”
虽然汇总公式变了，但多子布局确实引入了一个单子布局里不存在的问题，那就是 flex（弹性）：

在单子（Container）里：父级给子级传什么约束，子级就按什么算，简单直接。

在多子（Row）里：父级先把总宽度拿出来，减去固定宽度孩子的尺寸，剩下的“剩余空间”怎么分？

这就引出了 RenderFlex 的“两步走”算法：

第一步（无约束测量）：先问所有非 Expanded 的孩子要多大尺寸（layout 一次）。
第二步（分配约束）：算出剩余空间，除以 Expanded 的 flex 权重，算出每个弹性孩子的确切新约束，再让他们重新 layout 一次（第二次测量）。


===============================================================================================
// 只有一个孩子
child.layout(constraints);
size = child.size;  // ★ 公式：父尺寸 = 唯一孩子的尺寸（完全复制）

----------------------------------------------------------------------
// 多个孩子
double totalWidth = 0;
double maxHeight = 0;

// 遍历所有孩子
for (child in children) {
  child.layout(constraints, parentUsesSize: true);
  totalWidth += child.size.width;    // 累加宽度（主轴）
  maxHeight = max(maxHeight, child.size.height); // 取最大高度（纵轴）
}

// ★ 公式：父尺寸 = 子尺寸的累加和 与 最大值 的结合
size = Size(totalWidth, maxHeight);

----------------------------------------------------------------------
// 源码位置：RenderPadding.performLayout (极度简化)
@override
void performLayout() {
  // 1. 先扣除 margin，把缩水后的约束传给 child
  final BoxConstraints innerConstraints = constraints.deflate(_padding); 
  child.layout(innerConstraints, parentUsesSize: true);
  
  // 2. ★ 核心配合公式：最终尺寸 = 孩子的尺寸 + margin 的宽高
  size = constraints.constrain(Size(
    child.size.width + _padding.horizontal,   // 宽 = 孩子宽 + 左margin + 右margin
    child.size.height + _padding.vertical,    // 高 = 孩子高 + 上margin + 下margin
  ));
}

-------------------------------------------------------------------------------------
// 伪代码：单子父级的 performLayout
void performLayout() {
  // 1. 把约束缩水后传给唯一的孩子
  child.layout(innerConstraints);
  
  // 2. 算出自己的总尺寸（孩子尺寸 + 边距）
  size = ...;
  
  // 3. ★ 直接计算这一个孩子的 x,y（极其简单）
  child.parentData.offset = Offset(padding.left, padding.top);
}
-------------------------------------------------------------------------------------
// 伪代码：Row 父级的 performLayout（极度简化）
double currentX = 0;
for (child in children) {
  // ★ 这才是多子布局真正的核心难点：
  // 根据 MainAxisAlignment（居中、靠左、SpaceBetween）计算出当前孩子的起始 x
  // 如果是 spaceBetween，还要先算出总空白间隔，再逐个累加
  
  child.parentData.offset = Offset(currentX, 0); // 决定 Y 轴对齐（顶部、居中、拉伸）
  currentX += child.size.width; // 累加，指向下一个兄弟的起始位置
}
*/
struct ContainerWidget : Widget
{

    ContainerWidget() = default; // NOLINTBEGIN
    constexpr ContainerWidget(std::string key, std::optional<BoxConstraints> constraints,
                              std::optional<Alignment> alignment,
                              std::optional<EdgeInsetsGeometry> margin,
                              std::optional<EdgeInsetsGeometry> border,
                              std::optional<EdgeInsetsGeometry> padding,
                              std::unique_ptr<Widget> child) noexcept
        : constraints_{constraints}, alignment_{alignment}, margin_{margin},
          border_{border}, padding_{padding}, child_{std::move(child)} // NOLINTEND
    {
        Self().key = std::move(key);
        assert(not constraints_.has_value() || (*constraints_).isNormalized());
        assert(not margin_.has_value() || (*margin_).isNonNegative());
        assert(not border_.has_value() || (*border_).isNonNegative());
        assert(not padding_.has_value() || (*padding_).isNonNegative());
    }

    [[nodiscard]] constexpr auto additionalWidth() const noexcept
    {
        return margin_.value_or({}).horizontal() + border_.value_or({}).horizontal() +
               padding_.value_or({}).horizontal();
    }
    [[nodiscard]] constexpr auto additionalHeight() const noexcept
    {
        return margin_.value_or({}).vertical() + border_.value_or({}).vertical() +
               padding_.value_or({}).vertical();
    }
    constexpr void layout(BoxConstraints c) override
    {
        const auto margin = margin_.value_or({});
        const auto border = border_.value_or({});
        const auto padding = padding_.value_or({});

        // ── 1. 先扣 margin，得到 ConstrainedBox 的可用区
        BoxConstraints afterMargin = c.deflate(margin);

        // ── 2. 应用用户 constraints_（若没有就是继承父约束）
        BoxConstraints inner =
            constraints_.has_value() ? (*constraints_).enforce(afterMargin) : afterMargin;

        // ── 3. 再扣 padding + border，得到 child 真正的可用区
        const EdgeInsetsGeometry inset = EdgeInsetsGeometry::fromLTRB(
            border.left + padding.left, border.top + padding.top,
            border.right + padding.right, border.bottom + padding.bottom);
        BoxConstraints childConstraints = inner.deflate(inset);

        if (child_)
        {
            Widget &childRef = *child_;

            // ── 4. 有 alignment → 给孩子松约束，让它算自己的尺寸
            //      无 alignment → 直接把 childConstraints 丢给它
            if (alignment_.has_value())
            {
                childRef.layout(BoxConstraints{0.0, childConstraints.maxWidth(), 0.0,
                                               childConstraints.maxHeight()});
            }
            else
            {
                childRef.layout(childConstraints);
            }

            // ── 5. 内容区尺寸：
            //      有 alignment：Align 展开到内容区的 max（tight 时即 tight 值）
            //      无 alignment：就是孩子尺寸
            Size contentSize =
                alignment_.has_value() ? childConstraints.biggest() : childRef.size;

            // Padding(padding).size = inner.constrain(child.size + padding + border)
            Size paddingBoxSize = inner.constrain(Size{
                .width = contentSize.width + inset.horizontal(),
                .height = contentSize.height + inset.vertical(),
            });

            // ── 6. 父自身 = 内容 + padding + border + margin
            // Padding(margin).size = c.constrain(paddingBox.size + margin)
            Self().size = c.constrain(Size{
                .width = paddingBoxSize.width + margin.horizontal(),
                .height = paddingBoxSize.height + margin.vertical(),
            });
        }
        else
        {
            // 无 child：只有 margin 计入自身尺寸，padding/border 不计
            BoxConstraints effective = constraints_.has_value()
                                           ? (*constraints_).enforce(afterMargin)
                                           : afterMargin;

            double w = 0.0, h = 0.0;
            if (constraints_.has_value() && constraints_->isTight())
            {
                w = effective.maxWidth();
                h = effective.maxHeight();
            }
            else
            {
                w = effective.hasBoundedWidth() ? effective.maxWidth() : 0.0;
                h = effective.hasBoundedHeight() ? effective.maxHeight() : 0.0;
            }
            Self().size = {
                .width = w + margin.horizontal(),
                .height = h + margin.vertical(),
            };
        }
    }

    constexpr void updateOffset(Offset offset) noexcept override
    {
        Self().offset = offset;
        if (!child_)
            return;

        Widget &childRef = *child_;

        const auto margin = margin_.value_or({});
        const auto border = border_.value_or({});
        const auto padding = padding_.value_or({});

        // 孩子左边缘 = 父偏移 + margin + border + padding
        double dx = offset.x + margin.left + border.left + padding.left;
        double dy = offset.y + margin.top + border.top + padding.top;

        // 有 alignment：再叠加内容区内的对齐偏移
        if (alignment_.has_value())
        {
            // 内容区尺寸 = 父自身 - padding - border - margin
            // 但更简单：直接用 layout 里算过的内容区尺寸反推
            // 这里从对齐公式出发，需要 free = contentSize - child.size
            // 而 contentSize = Self().size - inset - margin
            double contentW = Self().size.width - margin.horizontal() -
                              border.horizontal() - padding.horizontal();
            double contentH = Self().size.height - margin.vertical() - border.vertical() -
                              padding.vertical();

            double freeW = contentW - childRef.size.width;
            double freeH = contentH - childRef.size.height;

            dx += freeW * (alignment_->x + 1.0) * 0.5;
            dy += freeH * (alignment_->y + 1.0) * 0.5;
        }

        childRef.updateOffset({dx, dy});
    }
    constexpr std::vector<Widget *> children() override
    {
        return child_ ? std::vector<Widget *>{child_.get()} : std::vector<Widget *>{};
    }

  private:
    std::optional<BoxConstraints> constraints_;
    std::optional<Alignment> alignment_;
    std::optional<EdgeInsetsGeometry> margin_;
    std::optional<EdgeInsetsGeometry> border_;
    std::optional<EdgeInsetsGeometry> padding_;
    std::unique_ptr<Widget> child_;
};
struct ContainerBuild
{
    constexpr explicit ContainerBuild(std::string key) noexcept : key_(std::move(key)) {}
    constexpr explicit operator std::unique_ptr<Widget>()
    {
        return std::make_unique<ContainerWidget>(
            std::move(key_),
            width_.has_value() || height_.has_value()
                ? std::optional<BoxConstraints>{BoxConstraints::tightFor(
                      {.width = width_, .height = height_})}
                : std::nullopt,
            alignment_, margin_, border_, padding_, std::move(child_));
    }
    constexpr auto &key(std::string key) noexcept
    {
        key_ = std::move(key);
        return *this;
    }
    constexpr auto &width(double width) noexcept
    {
        width_ = width;
        return *this;
    }
    constexpr auto &height(double height) noexcept
    {
        height_ = height;
        return *this;
    }
    constexpr auto &margin(EdgeInsetsGeometry margin) noexcept
    {
        margin_ = margin;
        return *this;
    }
    constexpr auto &alignment(Alignment alignment) noexcept
    {
        alignment_ = alignment;
        return *this;
    }
    constexpr auto &border(EdgeInsetsGeometry border) noexcept
    {
        border_ = border;
        return *this;
    }
    constexpr auto &padding(EdgeInsetsGeometry padding) noexcept
    {
        padding_ = padding;
        return *this;
    }
    constexpr auto &child(std::unique_ptr<Widget> child) noexcept
    {
        child_ = std::move(child);
        return *this;
    }
    constexpr auto &child(WidgetBuild auto &&b) noexcept
    {
        return child(static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
    }

  private:
    std::string key_;
    std::optional<double> width_;
    std::optional<double> height_;
    std::optional<Alignment> alignment_;
    std::optional<EdgeInsetsGeometry> margin_;
    std::optional<EdgeInsetsGeometry> border_;
    std::optional<EdgeInsetsGeometry> padding_;
    std::unique_ptr<Widget> child_;
};
static constexpr auto Container(std::string key = {}) noexcept // NOLINT
{
    return ContainerBuild{std::move(key)};
}

struct AlignWidget : Widget
{
    // NOLINTBEGIN
    AlignWidget(std::string key, Alignment alignment, std::optional<double> widthFactor,
                std::optional<double> heightFactor,
                std::unique_ptr<Widget> child) noexcept // NOLINTEND
        : alignment_{alignment}, widthFactor_{widthFactor}, heightFactor_{heightFactor},
          child_{std::move(child)}
    {
        Self().key = std::move(key);
        assert(not widthFactor_.has_value() || *widthFactor_ >= 0);
        assert(not heightFactor_.has_value() || *heightFactor_ >= 0);
    }
    void layout(BoxConstraints c) override
    {
        const BoxConstraints constraints = c.normalize();

        if (child_)
        {
            Widget &childRef = *child_;

            // Align 给孩子松约束：最小值为 0，最大值保持不变。
            const BoxConstraints childConstraints = constraints.loosen();

            childRef.layout(childConstraints);

            const Size childSize = childRef.size;

            const double width = widthFactor_.has_value()
                                     ? childSize.width * (*widthFactor_)
                                     : constraints.maxWidth();

            const double height = heightFactor_.has_value()
                                      ? childSize.height * (*heightFactor_)
                                      : constraints.maxHeight();

            Self().size = constraints.constrain(Size{width, height});
        }
        else
        {
            const double width = widthFactor_.has_value()
                                     ? constraints.minWidth() * (*widthFactor_)
                                     : constraints.maxWidth();

            const double height = heightFactor_.has_value()
                                      ? constraints.minHeight() * (*heightFactor_)
                                      : constraints.maxHeight();

            Self().size = constraints.constrain(Size{width, height});
        }
    }

    void updateOffset(Offset offset) noexcept override
    {
        Self().offset = offset;

        if (!child_)
            return;

        Widget &childRef = *child_;

        double freeWidth = Self().size.width - childRef.size.width;
        double freeHeight = Self().size.height - childRef.size.height;

        // Alignment:
        // (-1, -1) => 左上
        // ( 0,  0) => 居中
        // ( 1,  1) => 右下
        double dx = freeWidth * (alignment_.x + 1.0) * 0.5;
        double dy = freeHeight * (alignment_.y + 1.0) * 0.5;

        childRef.updateOffset(Offset{.x = offset.x + dx, .y = offset.y + dy});
    }

    constexpr std::vector<Widget *> children() override
    {
        return child_ ? std::vector<Widget *>{child_.get()} : std::vector<Widget *>{};
    }

  private:
    Alignment alignment_;
    std::optional<double> widthFactor_;
    std::optional<double> heightFactor_;
    std::unique_ptr<Widget> child_;
};
struct AlignBuild
{
    explicit AlignBuild(std::string key) noexcept : key_(std::move(key)) {}
    explicit operator std::unique_ptr<Widget>()
    {
        return std::make_unique<AlignWidget>(std::move(key_), alignment_, widthFactor_,
                                             heightFactor_, std::move(child_));
    }
    constexpr auto &key(std::string key) noexcept
    {
        key_ = std::move(key);
        return *this;
    }
    constexpr AlignBuild &alignment(Alignment alignment) noexcept
    {
        alignment_ = alignment;
        return *this;
    }
    constexpr auto &widthFactor(double widthFactor) noexcept
    {
        widthFactor_ = widthFactor;
        return *this;
    }
    constexpr auto &heightFactor(double heightFactor) noexcept
    {
        heightFactor_ = heightFactor;
        return *this;
    }
    constexpr auto &child(std::unique_ptr<Widget> child) noexcept
    {
        child_ = std::move(child);
        return *this;
    }
    constexpr auto &child(WidgetBuild auto &&b) noexcept
    {
        return child(static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
    }

  private:
    std::string key_;
    Alignment alignment_ = Alignment::center;
    std::optional<double> widthFactor_;
    std::optional<double> heightFactor_;
    std::unique_ptr<Widget> child_;
};
static constexpr auto Align(std::string key = {}) noexcept // NOLINT
{
    return AlignBuild{std::move(key)};
}

struct CenterWidget : AlignWidget
{
};
struct CenterBuild : AlignBuild
{
    using AlignBuild::AlignBuild;
    constexpr AlignBuild &alignment(Alignment alignment) noexcept = delete;
};
static constexpr auto Center(std::string key = {}) noexcept // NOLINT
{
    return CenterBuild{std::move(key)};
}

struct SizedBoxWidget : Widget
{
    // NOLINTBEGIN
    constexpr SizedBoxWidget(std::string key, std::optional<double> width,
                             std::optional<double> height,
                             std::unique_ptr<Widget> child) noexcept
        : width_{width}, height_{height}, child_{std::move(child)} // NOLINTEND
    {
        Self().key = std::move(key);
        assert(not width_.has_value() || *width_ >= 0);
        assert(not height_.has_value() || *height_ >= 0);
    }
    constexpr void layout(BoxConstraints c) override
    {
        // Flutter: RenderConstrainedBox(additionalConstraints: tightFor(width, height))
        BoxConstraints effective =
            BoxConstraints::tightFor({.width = width_, .height = height_}).enforce(c);

        if (child_)
        {
            Widget &childRef = *child_;
            childRef.layout(effective);
            // 自身尺寸 = 孩子的尺寸
            Self().size = childRef.size;
        }
        else
        {
            // Flutter: size = effective.constrain(Size.zero)
            Self().size = effective.smallest();
        }
    }
    constexpr void updateOffset(Offset offset) noexcept override
    {
        Self().offset = offset;
        if (child_)
        {
            // SizedBox 无 margin/padding，孩子与自身同原点
            Widget &childRef = *child_;
            childRef.updateOffset(offset);
        }
    }
    constexpr std::vector<Widget *> children() override
    {
        return child_ ? std::vector<Widget *>{child_.get()} : std::vector<Widget *>{};
    }

  private:
    std::optional<double> width_;
    std::optional<double> height_;
    std::unique_ptr<Widget> child_;
};
struct SizedBoxBuild
{
    explicit SizedBoxBuild(std::string key) noexcept : key_(std::move(key)) {}

    explicit operator std::unique_ptr<Widget>()
    {
        return std::make_unique<SizedBoxWidget>(std::move(key_), width_, height_,
                                                std::move(child_));
    }

    constexpr auto &key(std::string key) noexcept
    {
        key_ = std::move(key);
        return *this;
    }
    constexpr auto &width(double width) noexcept
    {
        width_ = width;
        return *this;
    }
    constexpr auto &height(double height) noexcept
    {
        height_ = height;
        return *this;
    }
    constexpr auto &child(std::unique_ptr<Widget> child) noexcept
    {
        child_ = std::move(child);
        return *this;
    }
    constexpr auto &child(WidgetBuild auto &&b) noexcept
    {
        return child(static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
    }

  private:
    std::string key_;
    std::optional<double> width_;
    std::optional<double> height_;
    std::unique_ptr<Widget> child_;
};
static constexpr auto SizedBox(std::string key = {}) noexcept // NOLINT
{
    return SizedBoxBuild{std::move(key)};
}

struct PaddingWidget : Widget
{
    constexpr PaddingWidget(std::string key, EdgeInsetsGeometry padding,
                            std::unique_ptr<Widget> child) noexcept
        : padding_{padding}, child_{std::move(child)}
    {
        Self().key = std::move(key);
        assert(padding_.isNonNegative());
    }

    constexpr void layout(BoxConstraints c) override
    {
        if (child_)
        {
            Widget &childRef = *child_;

            // 1. 缩水约束：c.deflate(padding) 给孩子
            childRef.layout(c.deflate(padding_));

            // 2. 自身尺寸 = 孩子尺寸 + padding，再被父约束夹住
            Self().size = c.constrain(Size{
                .width = childRef.size.width + padding_.horizontal(),
                .height = childRef.size.height + padding_.vertical(),
            });
        }
        else
        {
            Self().size = c.constrain(Size{
                .width = padding_.horizontal(),
                .height = padding_.vertical(),
            });
        }
    }

    constexpr void updateOffset(Offset offset) noexcept override
    {
        Self().offset = offset;
        if (child_)
        {
            Widget &childRef = *child_;
            // padding 就是孩子的内缩量（LTR 情况下 start=left, end=right）
            childRef.updateOffset(Offset{
                .x = offset.x + padding_.left,
                .y = offset.y + padding_.top,
            });
        }
    }

    constexpr std::vector<Widget *> children() override
    {
        return child_ ? std::vector<Widget *>{child_.get()} : std::vector<Widget *>{};
    }

  private:
    EdgeInsetsGeometry padding_;
    std::unique_ptr<Widget> child_;
};
struct PaddingBuild
{
    constexpr explicit PaddingBuild(std::string key) noexcept : key_(std::move(key)) {}

    explicit operator std::unique_ptr<Widget>()
    {
        if (not padding_.has_value())
            throw std::logic_error{"PaddingBuild required padding value"};
        return std::make_unique<PaddingWidget>(std::move(key_), *padding_,
                                               std::move(child_));
    }

    constexpr auto &key(std::string key) noexcept
    {
        key_ = std::move(key);
        return *this;
    }
    constexpr auto &padding(EdgeInsetsGeometry padding) noexcept
    {
        padding_ = padding;
        return *this;
    }
    constexpr auto &child(std::unique_ptr<Widget> child) noexcept
    {
        child_ = std::move(child);
        return *this;
    }
    constexpr auto &child(WidgetBuild auto &&b) noexcept
    {
        return child(static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
    }

  private:
    std::string key_;
    std::optional<EdgeInsetsGeometry> padding_;
    std::unique_ptr<Widget> child_;
};
static constexpr auto Padding(std::string key = {}) noexcept // NOLINT
{
    return PaddingBuild{std::move(key)};
}

struct ConstrainedBoxWidget : Widget
{
    constexpr ConstrainedBoxWidget(std::string key, BoxConstraints constraints,
                                   std::unique_ptr<Widget> child) noexcept
        : constraints_{constraints}, child_{std::move(child)}
    {
        Self().key = std::move(key);
        assert(constraints_.isNormalized());
    }

    constexpr void layout(BoxConstraints c) override
    {
        // Flutter: RenderConstrainedBox(additionalConstraints: constraints_)
        BoxConstraints effective = constraints_.enforce(c);

        if (child_)
        {
            Widget &childRef = *child_;
            childRef.layout(effective);
            // 自身尺寸 = 孩子的尺寸
            Self().size = childRef.size;
        }
        else
        {
            // Flutter: size = effective.constrain(Size.zero)
            Self().size = effective.smallest();
        }
    }

    constexpr void updateOffset(Offset offset) noexcept override
    {
        Self().offset = offset;
        if (child_)
        {
            // 无 margin/padding，孩子与自身同原点
            child_->updateOffset(offset);
        }
    }

    constexpr std::vector<Widget *> children() override
    {
        return child_ ? std::vector<Widget *>{child_.get()} : std::vector<Widget *>{};
    }

  private:
    BoxConstraints constraints_; // The additional constraints to impose on the child.
    std::unique_ptr<Widget> child_;
};
struct ConstrainedBoxBuild
{
    constexpr explicit ConstrainedBoxBuild(std::string key) noexcept
        : key_(std::move(key))
    {
    }

    explicit operator std::unique_ptr<Widget>()
    {
        if (not constraints_.has_value())
            throw std::logic_error{"ConstrainedBoxWidget required constraints value"};
        return std::make_unique<ConstrainedBoxWidget>(std::move(key_), *constraints_,
                                                      std::move(child_));
    }

    constexpr auto &key(std::string key) noexcept
    {
        key_ = std::move(key);
        return *this;
    }
    constexpr auto &constraints(BoxConstraints constraints) noexcept
    {
        constraints_ = constraints;
        return *this;
    }
    constexpr auto &child(std::unique_ptr<Widget> child) noexcept
    {
        child_ = std::move(child);
        return *this;
    }
    constexpr auto &child(WidgetBuild auto &&b) noexcept
    {
        return child(static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
    }

  private:
    std::string key_;
    std::optional<BoxConstraints> constraints_;
    std::unique_ptr<Widget> child_;
};
static constexpr auto ConstrainedBox(std::string key = {}) noexcept // NOLINT
{
    return ConstrainedBoxBuild{std::move(key)};
}

enum class OverflowBoxFit : std::uint8_t
{
    max,          // 自身 = 父约束允许的最大尺寸（默认）
    deferToChild, // 自身 = 父约束对 child.size 夹取后的尺寸
};
struct OverflowBoxWidget : Widget
{
    // NOLINTBEGIN
    constexpr OverflowBoxWidget(std::string key, Alignment alignment,
                                std::optional<double> minWidth,
                                std::optional<double> maxWidth,
                                std::optional<double> minHeight,
                                std::optional<double> maxHeight, OverflowBoxFit fit,
                                std::unique_ptr<Widget> child) noexcept // NOLINTEND
        : alignment_{alignment}, minWidth_{minWidth}, maxWidth_{maxWidth},
          minHeight_{minHeight}, maxHeight_{maxHeight}, fit_{fit},
          child_{std::move(child)}
    {
        Self().key = std::move(key);
    }

    constexpr void layout(BoxConstraints c) override
    {
        // 1) 用「显式覆盖值 + 从父约束继承」组合出 child 的约束
        const BoxConstraints inner = innerConstraints(c);

        if (child_)
        {
            Widget &childRef = *child_;

            // 2) child 用 inner 布局（可能溢出父约束，这正是 OverflowBox 的用意）
            childRef.layout(inner);

            // 3) 自身尺寸：deferToChild → 夹取 child.size；max → 父允许的最大
            Self().size = (fit_ == OverflowBoxFit::deferToChild)
                              ? c.constrain(childRef.size)
                              : c.biggest();
        }
        else
        {
            // 无 child：仍按 fit 决定自身尺寸
            Self().size = (fit_ == OverflowBoxFit::deferToChild)
                              ? c.constrain(inner.smallest())
                              : c.biggest();
        }
    }

    constexpr void updateOffset(Offset offset) noexcept override
    {
        Self().offset = offset;
        if (!child_)
            return;

        Widget &childRef = *child_;

        // child 可以大于 / 小于自身，因此 freeW / freeH 可能为负（溢出时）
        const double freeW = Self().size.width - childRef.size.width;
        const double freeH = Self().size.height - childRef.size.height;

        const double dx = offset.x + freeW * (alignment_.x + 1.0) * 0.5;
        const double dy = offset.y + freeH * (alignment_.y + 1.0) * 0.5;

        childRef.updateOffset({dx, dy});
    }

    constexpr std::vector<Widget *> children() override
    {
        return child_ ? std::vector<Widget *>{child_.get()} : std::vector<Widget *>{};
    }

  private:
    [[nodiscard]] constexpr BoxConstraints innerConstraints(
        BoxConstraints c) const noexcept
    {
        // 未提供的分量 → 沿用父约束；提供的分量 → 覆盖
        return BoxConstraints{
            minWidth_.value_or(c.minWidth()),
            maxWidth_.value_or(c.maxWidth()),
            minHeight_.value_or(c.minHeight()),
            maxHeight_.value_or(c.maxHeight()),
        };
    }

    Alignment alignment_;
    std::optional<double> minWidth_;
    std::optional<double> maxWidth_;
    std::optional<double> minHeight_;
    std::optional<double> maxHeight_;
    OverflowBoxFit fit_;
    std::unique_ptr<Widget> child_;
};
struct OverflowBoxBuild
{
    constexpr explicit OverflowBoxBuild(std::string key) noexcept : key_(std::move(key))
    {
    }

    explicit operator std::unique_ptr<Widget>()
    {
        return std::make_unique<OverflowBoxWidget>(std::move(key_), alignment_, minWidth_,
                                                   maxWidth_, minHeight_, maxHeight_,
                                                   fit_, std::move(child_));
    }

    constexpr auto &key(std::string key) noexcept
    {
        key_ = std::move(key);
        return *this;
    }
    constexpr auto &alignment(Alignment alignment) noexcept
    {
        alignment_ = alignment;
        return *this;
    }
    constexpr auto &minWidth(double v) noexcept
    {
        minWidth_ = v;
        return *this;
    }
    constexpr auto &maxWidth(double v) noexcept
    {
        maxWidth_ = v;
        return *this;
    }
    constexpr auto &minHeight(double v) noexcept
    {
        minHeight_ = v;
        return *this;
    }
    constexpr auto &maxHeight(double v) noexcept
    {
        maxHeight_ = v;
        return *this;
    }
    constexpr auto &fit(OverflowBoxFit f) noexcept
    {
        fit_ = f;
        return *this;
    }
    constexpr auto &child(std::unique_ptr<Widget> child) noexcept
    {
        child_ = std::move(child);
        return *this;
    }
    constexpr auto &child(WidgetBuild auto &&b) noexcept
    {
        return child(static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
    }

  private:
    std::string key_;
    Alignment alignment_ = Alignment::center;
    std::optional<double> minWidth_;
    std::optional<double> maxWidth_;
    std::optional<double> minHeight_;
    std::optional<double> maxHeight_;
    OverflowBoxFit fit_ = OverflowBoxFit::max;
    std::unique_ptr<Widget> child_;
};
static constexpr auto OverflowBox(std::string key = {}) noexcept // NOLINT
{
    return OverflowBoxBuild{std::move(key)};
}

// =========================================================================
// UnconstrainedBox
//
// 语义（对应 Flutter 里的 ConstraintsTransformBox）：
//   constrainedAxis == null
//       两轴都放开  → child 约束 = [0, inf] x [0, inf]
//   constrainedAxis == horizontal
//       保留宽度约束，高度放开 → child 约束 = [c.minW, c.maxW] x [0, inf]
//   constrainedAxis == vertical
//       保留高度约束，宽度放开 → child 约束 = [0, inf] x [c.minH, c.maxH]
//
// 自身尺寸 = 父约束夹取 child.size；child 相对自身按 alignment 对齐。
// 若 child 溢出，对应方向会出现负 free 值（此处不报警，交由上层或调试期处理）。
// =========================================================================
struct UnconstrainedBoxWidget : Widget
{
    // NOLINTBEGIN
    constexpr UnconstrainedBoxWidget(std::string key, Alignment alignment,
                                     std::optional<Axis> constrainedAxis,
                                     std::unique_ptr<Widget> child) noexcept // NOLINTEND
        : alignment_{alignment}, constrainedAxis_{constrainedAxis},
          child_{std::move(child)}
    {
        Self().key = std::move(key);
    }

    constexpr void layout(BoxConstraints c) override
    {
        const BoxConstraints inner = transform(c);

        if (child_)
        {
            Widget &childRef = *child_;

            // child 用放开后的约束布局
            childRef.layout(inner);

            // 自身 = 父约束夹取 child.size（可能因为 child 溢出而被夹住）
            Self().size = c.constrain(childRef.size);
        }
        else
        {
            // 无 child：取父约束下的最小值
            Self().size = c.smallest();
        }
    }

    constexpr void updateOffset(Offset offset) noexcept override
    {
        Self().offset = offset;
        if (!child_)
            return;

        Widget &childRef = *child_;

        // child 可能比自身大（溢出），freeW / freeH 允许为负
        const double freeW = Self().size.width - childRef.size.width;
        const double freeH = Self().size.height - childRef.size.height;

        const double dx = offset.x + freeW * (alignment_.x + 1.0) * 0.5;
        const double dy = offset.y + freeH * (alignment_.y + 1.0) * 0.5;

        childRef.updateOffset({dx, dy});
    }

    constexpr std::vector<Widget *> children() override
    {
        return child_ ? std::vector<Widget *>{child_.get()} : std::vector<Widget *>{};
    }

  private:
    // 三种约束变换（对应 Flutter 的 BoxConstraintsTransform）
    [[nodiscard]] constexpr BoxConstraints transform(BoxConstraints c) const noexcept
    {
        constexpr auto inf = std::numeric_limits<double>::infinity();

        if (!constrainedAxis_.has_value())
        {
            // unconstrained: 两轴都放开
            return BoxConstraints{0.0, inf, 0.0, inf};
        }

        if (*constrainedAxis_ == Axis::horizontal)
        {
            // heightUnconstrained: 保留宽度，高度放开
            return BoxConstraints{c.minWidth(), c.maxWidth(), 0.0, inf};
        }

        // Axis::vertical → widthUnconstrained: 保留高度，宽度放开
        return BoxConstraints{0.0, inf, c.minHeight(), c.maxHeight()};
    }

    Alignment alignment_;
    std::optional<Axis> constrainedAxis_;
    std::unique_ptr<Widget> child_;
};
struct UnconstrainedBoxBuild
{
    constexpr explicit UnconstrainedBoxBuild(std::string key) noexcept
        : key_(std::move(key))
    {
    }

    explicit operator std::unique_ptr<Widget>()
    {
        return std::make_unique<UnconstrainedBoxWidget>(
            std::move(key_), alignment_, constrainedAxis_, std::move(child_));
    }

    constexpr auto &key(std::string key) noexcept
    {
        key_ = std::move(key);
        return *this;
    }

    constexpr auto &alignment(Alignment alignment) noexcept
    {
        alignment_ = alignment;
        return *this;
    }

    constexpr auto &constrainedAxis(Axis axis) noexcept
    {
        constrainedAxis_ = axis;
        return *this;
    }
    constexpr auto &clearConstrainedAxis() noexcept
    {
        constrainedAxis_.reset();
        return *this;
    }

    constexpr auto &child(std::unique_ptr<Widget> child) noexcept
    {
        child_ = std::move(child);
        return *this;
    }
    constexpr auto &child(WidgetBuild auto &&b) noexcept
    {
        return child(static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
    }

  private:
    std::string key_;
    Alignment alignment_ = Alignment::center;
    std::optional<Axis> constrainedAxis_;
    std::unique_ptr<Widget> child_;
};
static constexpr auto UnconstrainedBox(std::string key = {}) noexcept // NOLINT
{
    return UnconstrainedBoxBuild{std::move(key)};
}

struct LimitedBoxWidget : Widget
{
    // NOLINTBEGIN
    constexpr LimitedBoxWidget(std::string key, double maxWidth, double maxHeight,
                               std::unique_ptr<Widget> child) noexcept // NOLINTEND
        : maxWidth_{maxWidth}, maxHeight_{maxHeight}, child_{std::move(child)}
    {
        Self().key = std::move(key);
        assert(maxWidth_ >= 0.0);
        assert(maxHeight_ >= 0.0);
    }

    constexpr void layout(BoxConstraints c) override
    {
        const BoxConstraints inner = innerConstraints(c);

        if (child_)
        {
            Widget &childRef = *child_;
            childRef.layout(inner);
            Self().size = c.constrain(childRef.size);
        }
        else
        {
            Self().size = c.constrain(inner.smallest());
        }
    }

    constexpr void updateOffset(Offset offset) noexcept override
    {
        Self().offset = offset;
        if (child_)
            child_->updateOffset(offset);
    }

    constexpr std::vector<Widget *> children() override
    {
        return child_ ? std::vector<Widget *>{child_.get()} : std::vector<Widget *>{};
    }

  private:
    [[nodiscard]] constexpr BoxConstraints innerConstraints(
        BoxConstraints c) const noexcept
    {
        constexpr auto inf = std::numeric_limits<double>::infinity();

        return BoxConstraints{
            c.minWidth(),
            c.maxWidth() == inf ? maxWidth_ : c.maxWidth(),
            c.minHeight(),
            c.maxHeight() == inf ? maxHeight_ : c.maxHeight(),
        };
    }

    double maxWidth_;
    double maxHeight_;
    std::unique_ptr<Widget> child_;
};
struct LimitedBoxBuild
{
    constexpr explicit LimitedBoxBuild(std::string key) noexcept : key_(std::move(key)) {}

    explicit operator std::unique_ptr<Widget>()
    {
        return std::make_unique<LimitedBoxWidget>(std::move(key_), maxWidth_, maxHeight_,
                                                  std::move(child_));
    }

    constexpr auto &key(std::string key) noexcept
    {
        key_ = std::move(key);
        return *this;
    }

    constexpr auto &maxWidth(double maxWidth) noexcept
    {
        maxWidth_ = maxWidth;
        return *this;
    }

    constexpr auto &maxHeight(double maxHeight) noexcept
    {
        maxHeight_ = maxHeight;
        return *this;
    }

    constexpr auto &child(std::unique_ptr<Widget> child) noexcept
    {
        child_ = std::move(child);
        return *this;
    }

    constexpr auto &child(WidgetBuild auto &&b) noexcept
    {
        return child(static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
    }

  private:
    static constexpr auto inf = std::numeric_limits<double>::infinity();

    std::string key_;
    double maxWidth_{inf};
    double maxHeight_{inf};
    std::unique_ptr<Widget> child_;
};
static constexpr auto LimitedBox(std::string key = {}) noexcept // NOLINT
{
    return LimitedBoxBuild{std::move(key)};
}

// =========================================================================
// AspectRatio
//
// 语义（对应 Flutter RenderAspectRatio）：
//   aspectRatio = width / height，必须 > 0。
//
//   1. 先把宽度设成 maxWidth；若宽度无界，则取 height * aspectRatio
//      （height = maxHeight）或者 0（两轴都无界）。
//   2. 由 width 推出 height = width / aspectRatio。
//   3. 依次做 4 步“夹取修正”，保证 height ∈ [minH, maxH] 且 width ∈ [minW, maxW]：
//        · height > maxHeight  → 用 maxHeight 反推 width
//        · width  < minWidth   → 用 minWidth  反推 height
//        · height < minHeight  → 用 minHeight 反推 width
//   4. size = c.constrain(Size(width, height))
//   5. child 用 BoxConstraints::tight(size) 布局（保证子级不会改变比例）
//
//   无 child 时：size = c.smallest()。
// =========================================================================
struct AspectRatioWidget : Widget
{
    // NOLINTBEGIN
    constexpr AspectRatioWidget(std::string key, double aspectRatio,
                                std::unique_ptr<Widget> child) noexcept // NOLINTEND
        : aspectRatio_{aspectRatio}, child_{std::move(child)}
    {
        Self().key = std::move(key);
        assert(aspectRatio_ > 0.0);
    }

    constexpr void layout(BoxConstraints c) override
    {
        if (!child_)
        {
            Self().size = c.smallest();
            return;
        }

        // ── 1. 决定初始宽度
        double width = c.maxWidth();
        if (!c.hasBoundedWidth())
        {
            width = c.hasBoundedHeight() ? c.maxHeight() * aspectRatio_ : 0.0;
        }

        // ── 2. 由宽度反推高度
        double height = width / aspectRatio_;

        // ── 3. 四步夹取修正（顺序与 Flutter 完全一致）
        if (height > c.maxHeight())
        {
            height = c.maxHeight();
            width = height * aspectRatio_;
        }
        if (width < c.minWidth())
        {
            width = c.minWidth();
            height = width / aspectRatio_;
        }
        if (height < c.minHeight())
        {
            height = c.minHeight();
            width = height * aspectRatio_;
        }

        // ── 4. 自身尺寸 = 约束夹取后的 (w, h)
        Self().size = c.constrain(Size{width, height});

        // ── 5. child 用 tight 约束布局（保证 child 内部不改变比例）
        child_->layout(BoxConstraints::tight(Self().size));
    }

    constexpr void updateOffset(Offset offset) noexcept override
    {
        Self().offset = offset;
        if (child_)
        {
            // AspectRatio 无 margin/padding，child 与自身同原点
            child_->updateOffset(offset);
        }
    }

    constexpr std::vector<Widget *> children() override
    {
        return child_ ? std::vector<Widget *>{child_.get()} : std::vector<Widget *>{};
    }

  private:
    double aspectRatio_;
    std::unique_ptr<Widget> child_;
};
struct AspectRatioBuild
{
    constexpr explicit AspectRatioBuild(std::string key, double aspectRatio) noexcept
        : key_(std::move(key)), aspectRatio_{aspectRatio}
    {
    }

    explicit operator std::unique_ptr<Widget>()
    {
        if (!(aspectRatio_ > 0.0))
            throw std::logic_error{"AspectRatio requires aspectRatio > 0"};
        return std::make_unique<AspectRatioWidget>(std::move(key_), aspectRatio_,
                                                   std::move(child_));
    }

    constexpr auto &key(std::string key) noexcept
    {
        key_ = std::move(key);
        return *this;
    }

    constexpr auto &aspectRatio(double aspectRatio) noexcept
    {
        aspectRatio_ = aspectRatio;
        return *this;
    }

    constexpr auto &child(std::unique_ptr<Widget> child) noexcept
    {
        child_ = std::move(child);
        return *this;
    }
    constexpr auto &child(WidgetBuild auto &&b) noexcept
    {
        return child(static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
    }

  private:
    std::string key_;
    double aspectRatio_;
    std::unique_ptr<Widget> child_;
};
static constexpr auto AspectRatio(std::string key, double aspectRatio) noexcept // NOLINT
{
    return AspectRatioBuild{std::move(key), aspectRatio};
}

// =========================================================================
// FractionallySizedBox
//
// 语义（对应 Flutter RenderFractionallySizedOverflowBox）：
//   widthFactor  != null → child 收到严格宽度约束 = maxWidth * widthFactor
//   widthFactor  == null → child 原样继承父的宽度约束
//   heightFactor != null → child 收到严格高度约束 = maxHeight * heightFactor
//   heightFactor == null → child 原样继承父的高度约束
//
//   自身尺寸 = c.constrain(child.size)（可能比 child 小，从而产生溢出）
//   child 相对自身按 alignment 对齐
// =========================================================================
struct FractionallySizedBoxWidget : Widget
{
    // NOLINTBEGIN
    constexpr FractionallySizedBoxWidget(
        std::string key, Alignment alignment, std::optional<double> widthFactor,
        std::optional<double> heightFactor,
        std::unique_ptr<Widget> child) noexcept // NOLINTEND
        : alignment_{alignment}, widthFactor_{widthFactor}, heightFactor_{heightFactor},
          child_{std::move(child)}
    {
        Self().key = std::move(key);
        assert(!widthFactor_.has_value() || *widthFactor_ >= 0.0);
        assert(!heightFactor_.has_value() || *heightFactor_ >= 0.0);
    }

    constexpr void layout(BoxConstraints c) override
    {
        const BoxConstraints inner = innerConstraints(c);

        if (child_)
        {
            Widget &childRef = *child_;

            // 1. child 用「按比例缩放后的约束」布局
            childRef.layout(inner);

            // 2. 自身尺寸 = 父约束夹取 child.size
            //    （若 widthFactor > 1，child 溢出，自身被夹回 maxWidth）
            Self().size = c.constrain(childRef.size);
        }
        else
        {
            // 3. 无 child：取缩放后约束下的最小尺寸（与 Flutter 一致）
            Self().size = c.constrain(inner.smallest());
        }
    }

    constexpr void updateOffset(Offset offset) noexcept override
    {
        Self().offset = offset;
        if (!child_)
            return;

        Widget &childRef = *child_;

        // free 值可能为负 → child 溢出时按 alignment 反向偏移（与 Flutter 溢出行为一致）
        const double freeW = Self().size.width - childRef.size.width;
        const double freeH = Self().size.height - childRef.size.height;

        const double dx = offset.x + freeW * (alignment_.x + 1.0) * 0.5;
        const double dy = offset.y + freeH * (alignment_.y + 1.0) * 0.5;

        childRef.updateOffset({dx, dy});
    }

    constexpr std::vector<Widget *> children() override
    {
        return child_ ? std::vector<Widget *>{child_.get()} : std::vector<Widget *>{};
    }

  private:
    // 对应 Flutter 的 RenderFractionallySizedOverflowBox._getInnerConstraints
    [[nodiscard]] constexpr BoxConstraints innerConstraints(
        BoxConstraints c) const noexcept
    {
        double minWidth = c.minWidth();
        double maxWidth = c.maxWidth();
        if (widthFactor_.has_value())
        {
            const double width = maxWidth * (*widthFactor_);
            minWidth = width;
            maxWidth = width;
        }

        double minHeight = c.minHeight();
        double maxHeight = c.maxHeight();
        if (heightFactor_.has_value())
        {
            const double height = maxHeight * (*heightFactor_);
            minHeight = height;
            maxHeight = height;
        }

        return BoxConstraints{minWidth, maxWidth, minHeight, maxHeight};
    }

    Alignment alignment_;
    std::optional<double> widthFactor_;
    std::optional<double> heightFactor_;
    std::unique_ptr<Widget> child_;
};

struct FractionallySizedBoxBuild
{
    constexpr explicit FractionallySizedBoxBuild(std::string key) noexcept
        : key_(std::move(key))
    {
    }

    explicit operator std::unique_ptr<Widget>()
    {
        return std::make_unique<FractionallySizedBoxWidget>(
            std::move(key_), alignment_, widthFactor_, heightFactor_, std::move(child_));
    }

    constexpr auto &key(std::string key) noexcept
    {
        key_ = std::move(key);
        return *this;
    }

    constexpr auto &alignment(Alignment alignment) noexcept
    {
        alignment_ = alignment;
        return *this;
    }

    constexpr auto &widthFactor(double factor) noexcept
    {
        widthFactor_ = factor;
        return *this;
    }

    constexpr auto &heightFactor(double factor) noexcept
    {
        heightFactor_ = factor;
        return *this;
    }

    constexpr auto &clearWidthFactor() noexcept
    {
        widthFactor_.reset();
        return *this;
    }

    constexpr auto &clearHeightFactor() noexcept
    {
        heightFactor_.reset();
        return *this;
    }

    constexpr auto &child(std::unique_ptr<Widget> child) noexcept
    {
        child_ = std::move(child);
        return *this;
    }
    constexpr auto &child(WidgetBuild auto &&b) noexcept
    {
        return child(static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
    }

  private:
    std::string key_;
    Alignment alignment_ = Alignment::center;
    std::optional<double> widthFactor_;
    std::optional<double> heightFactor_;
    std::unique_ptr<Widget> child_;
};

static constexpr auto FractionallySizedBox(std::string key = {}) noexcept // NOLINT
{
    return FractionallySizedBoxBuild{std::move(key)};
}

// ===============================================================================================
// flex 模式 [start]
// ===============================================================================================
// =========================================================================
// FlexInfo —— Flex 对每个子节点的记账信息
// 与 children_ 一一对应，仅存在于 Flex 内部，不进 Widget 抽象
// =========================================================================
struct FlexInfo
{
    int flex = 0;                 // 0 表示非弹性
    FlexFit fit = FlexFit::tight; // flex > 0 时才有意义
};

//NOTE: Spacer 引入 takeChild。 这是部分flex 语义的。因为这些标签是有关联的。因为CPP和dart是不一样的
// =========================================================================
// FlexChildBuilder concept：任何能向 Flex 贡献 (child, flex, fit) 的构建器
// =========================================================================
template <typename B>
concept FlexChildBuilder = requires(B &b) {
    { b.flexValue() } -> std::convertible_to<int>;
    { b.fitValue() } -> std::convertible_to<FlexFit>;
    { b.takeChild() } -> std::convertible_to<std::unique_ptr<Widget>>;
};

// =========================================================================
// FlexibleBuild —— 构建期 DSL，运行时不是 Widget
// 通过 FlexBuild::addChild(FlexibleBuild&) 被解包为 (child, flex, fit)
// =========================================================================
struct FlexibleBuild
{
    constexpr explicit FlexibleBuild(std::string key = {}) noexcept : key_(std::move(key))
    {
    }

    constexpr auto &key(std::string k) noexcept
    {
        key_ = std::move(k);
        return *this;
    }
    constexpr auto &flex(int f) noexcept
    {
        flex_ = f;
        return *this;
    }
    constexpr auto &fit(FlexFit f) noexcept
    {
        fit_ = f;
        return *this;
    }

    constexpr auto &child(std::unique_ptr<Widget> c) noexcept
    {
        child_ = std::move(c);
        return *this;
    }
    constexpr auto &child(WidgetBuild auto &&b) noexcept
    {
        return child(static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
    }

    // 供 FlexBuild 使用
    [[nodiscard]] constexpr int flexValue() const noexcept
    {
        return flex_;
    }
    [[nodiscard]] constexpr FlexFit fitValue() const noexcept
    {
        return fit_;
    }
    [[nodiscard]] std::unique_ptr<Widget> takeChild() noexcept
    {
        return std::move(child_);
    }

  private:
    std::string key_;
    int flex_ = 1;
    FlexFit fit_ = FlexFit::loose; // Flutter 默认 loose
    std::unique_ptr<Widget> child_;
};

struct FlexWidget : Widget
{
    constexpr FlexWidget(std::string key, Axis direction,
                         MainAxisAlignment mainAxisAlignment, MainAxisSize mainAxisSize,
                         CrossAxisAlignment crossAxisAlignment,
                         std::optional<TextDirection> textDirection,
                         VerticalDirection verticalDirection,
                         std::optional<TextBaseline> textBaseline,
                         std::vector<std::unique_ptr<Widget>> children,
                         std::vector<FlexInfo> flexes) noexcept
        : direction_{direction}, mainAxisAlignment_{mainAxisAlignment},
          mainAxisSize_{mainAxisSize}, crossAxisAlignment_{crossAxisAlignment},
          textDirection_{textDirection}, verticalDirection_{verticalDirection},
          textBaseline_{textBaseline}, children_{std::move(children)},
          flexes_{std::move(flexes)}
    {
        Self().key = std::move(key);
        assert(crossAxisAlignment_ != CrossAxisAlignment::baseline ||
               textBaseline_.has_value());
        assert(children_.size() == flexes_.size());
    }

    constexpr void layout(BoxConstraints c) override
    {
        constexpr auto inf = std::numeric_limits<double>::infinity();
        const bool h = direction_ == Axis::horizontal;
        const double maxMain = h ? c.maxWidth() : c.maxHeight();
        const double maxCross = h ? c.maxHeight() : c.maxWidth();
        const bool canFlex = std::isfinite(maxMain);
        const bool stretch = crossAxisAlignment_ == CrossAxisAlignment::stretch;

        // ── 空 children 边界 ────────────────────────────────────────
        if (children_.empty())
        {
            const double main =
                (mainAxisSize_ == MainAxisSize::max && canFlex) ? maxMain : 0.0;
            Self().size = c.constrain(h ? Size{main, 0.0} : Size{0.0, main});
            return;
        }

        // ── 第 1 遍：所有非弹性子节点用「主轴无界」约束 layout ──────
        int totalFlex = 0;
        double allocated = 0.0;
        double maxChildCross = 0.0;
        size_t lastFlexIdx = SIZE_MAX;

        for (size_t i = 0; i < children_.size(); ++i)
        {
            if (flexes_[i].flex > 0)
            {
                totalFlex += flexes_[i].flex;
                lastFlexIdx = i;
            }
            else
            {
                const BoxConstraints cc =
                    h ? BoxConstraints{0.0, inf, stretch ? maxCross : 0.0, maxCross}
                      : BoxConstraints{stretch ? maxCross : 0.0, maxCross, 0.0, inf};
                children_[i]->layout(cc);
                allocated += h ? children_[i]->size.width : children_[i]->size.height;
                const double cs =
                    h ? children_[i]->size.height : children_[i]->size.width;
                if (cs > maxChildCross)
                    maxChildCross = cs;
            }
        }

        // ── 第 2 遍：把剩余主轴按 flex 分配，tight/loose 决定 min ───
        const double freeSpace = std::max(0.0, (canFlex ? maxMain : 0.0) - allocated);
        double allocatedFlexSpace = 0.0;

        if (totalFlex > 0)
        {
            const double spacePerFlex = canFlex ? (freeSpace / totalFlex) : inf;

            for (size_t i = 0; i < children_.size(); ++i)
            {
                if (flexes_[i].flex <= 0)
                    continue;

                const double maxChildExtent = !canFlex ? inf
                                              : (i == lastFlexIdx)
                                                  ? (freeSpace - allocatedFlexSpace)
                                                  : (spacePerFlex * flexes_[i].flex);

                const double minChildExtent =
                    (flexes_[i].fit == FlexFit::tight) ? maxChildExtent : 0.0;

                const double minCrossSize = stretch ? maxCross : 0.0;
                const BoxConstraints cc =
                    h ? BoxConstraints{minChildExtent, maxChildExtent, minCrossSize,
                                       maxCross}
                      : BoxConstraints{minCrossSize, maxCross, minChildExtent,
                                       maxChildExtent};
                children_[i]->layout(cc);

                allocated += h ? children_[i]->size.width : children_[i]->size.height;
                allocatedFlexSpace += maxChildExtent;
                const double cs =
                    h ? children_[i]->size.height : children_[i]->size.width;
                if (cs > maxChildCross)
                    maxChildCross = cs;
            }
        }

        // ── 自身尺寸 ────────────────────────────────────────────────
        const double idealMain =
            (mainAxisSize_ == MainAxisSize::max && canFlex) ? maxMain : allocated;
        const double idealCross = stretch ? maxCross : maxChildCross;

        Self().size =
            c.constrain(h ? Size{idealMain, idealCross} : Size{idealCross, idealMain});
    }

    constexpr void updateOffset(Offset offset) noexcept override
    {
        Self().offset = offset;
        if (children_.empty())
            return;

        const bool h = direction_ == Axis::horizontal;
        const double selfMain = h ? Self().size.width : Self().size.height;
        const double selfCross = h ? Self().size.height : Self().size.width;

        double allocated = 0.0;
        for (auto &child : children_)
            allocated += h ? child->size.width : child->size.height;

        const double remaining = std::max(0.0, selfMain - allocated);
        const auto n = static_cast<double>(children_.size());

        double lead = 0.0, between = 0.0;
        switch (mainAxisAlignment_)
        {
        case MainAxisAlignment::start:
            break;
        case MainAxisAlignment::end:
            lead = remaining;
            break;
        case MainAxisAlignment::center:
            lead = remaining / 2.0;
            break;
        case MainAxisAlignment::spaceBetween:
            between = (n > 1.0) ? remaining / (n - 1.0) : 0.0;
            break;
        case MainAxisAlignment::spaceAround:
            between = remaining / n;
            lead = between / 2.0;
            break;
        case MainAxisAlignment::spaceEvenly:
            between = remaining / (n + 1.0);
            lead = between;
            break;
        }

        const bool startTopLeftMain =
            h ? (textDirection_.value_or(TextDirection::ltr) == TextDirection::ltr)
              : (verticalDirection_ == VerticalDirection::down);
        const bool startTopLeftCross =
            h ? (verticalDirection_ == VerticalDirection::down)
              : (textDirection_.value_or(TextDirection::ltr) == TextDirection::ltr);
        const bool flipMain = !startTopLeftMain;
        const bool flipCross = !startTopLeftCross;

        double cursor = lead;
        for (auto &child : children_)
        {
            const double childMain = h ? child->size.width : child->size.height;
            const double childCross = h ? child->size.height : child->size.width;

            const double lm = cursor;
            double lc = 0.0;
            switch (crossAxisAlignment_)
            {
            case CrossAxisAlignment::start:
            case CrossAxisAlignment::baseline:
            case CrossAxisAlignment::stretch:
                lc = 0.0;
                break;
            case CrossAxisAlignment::end:
                lc = selfCross - childCross;
                break;
            case CrossAxisAlignment::center:
                lc = (selfCross - childCross) / 2.0;
                break;
            }

            const double am = flipMain ? selfMain - lm - childMain : lm;
            const double ac = flipCross ? selfCross - lc - childCross : lc;

            double dx, dy;
            if (h)
            {
                dx = offset.x + am;
                dy = offset.y + ac;
            }
            else
            {
                dx = offset.x + ac;
                dy = offset.y + am;
            }

            child->updateOffset({dx, dy});
            cursor += childMain + between;
        }
    }

    constexpr std::vector<Widget *> children() override
    {
        std::vector<Widget *> out;
        out.reserve(children_.size());
        for (auto &p : children_)
            if (p)
                out.push_back(p.get());
        return out;
    }

  private:
    Axis direction_;
    MainAxisAlignment mainAxisAlignment_;
    MainAxisSize mainAxisSize_;
    CrossAxisAlignment crossAxisAlignment_;
    std::optional<TextDirection> textDirection_;
    VerticalDirection verticalDirection_;
    std::optional<TextBaseline> textBaseline_;
    std::vector<std::unique_ptr<Widget>> children_;
    std::vector<FlexInfo> flexes_; // 与 children_ 一一对应
};
struct FlexBuild
{
    constexpr FlexBuild(std::string key) noexcept : key_(std::move(key)) {}

    constexpr explicit operator std::unique_ptr<Widget>()
    {
        if (!direction_)
            throw std::logic_error{"Flex requires direction value"};
        return std::make_unique<FlexWidget>(
            std::move(key_), *direction_, mainAxisAlignment_, mainAxisSize_,
            crossAxisAlignment_, textDirection_, verticalDirection_, textBaseline_,
            std::move(children_), std::move(flexes_));
    }

    constexpr auto &key(std::string key) noexcept
    {
        key_ = std::move(key);
        return *this;
    }
    constexpr auto &direction(Axis v) noexcept
    {
        direction_ = v;
        return *this;
    }
    constexpr auto &mainAxisAlignment(MainAxisAlignment v) noexcept
    {
        mainAxisAlignment_ = v;
        return *this;
    }
    constexpr auto &mainAxisSize(MainAxisSize v) noexcept
    {
        mainAxisSize_ = v;
        return *this;
    }
    constexpr auto &crossAxisAlignment(CrossAxisAlignment v) noexcept
    {
        crossAxisAlignment_ = v;
        return *this;
    }
    constexpr auto &textDirection(TextDirection v) noexcept
    {
        textDirection_ = v;
        return *this;
    }
    constexpr auto &clearTextDirection() noexcept
    {
        textDirection_.reset();
        return *this;
    }
    constexpr auto &verticalDirection(VerticalDirection v) noexcept
    {
        verticalDirection_ = v;
        return *this;
    }
    constexpr auto &textBaseline(TextBaseline v) noexcept
    {
        textBaseline_ = v;
        return *this;
    }

    // =====================================================================
    // ── FlexBuild::addChild 三个重载 ─────────────────────────────────
    // (1) 普通子 Widget：flex = 0
    constexpr auto &addChild(std::unique_ptr<Widget> child) noexcept
    {
        children_.push_back(std::move(child));
        flexes_.push_back(FlexInfo{.flex = 0, .fit = FlexFit::tight});
        return *this;
    }
    // (2) WidgetBuild 概念（如 SizedBoxBuild / PaddingBuild …）：转成 Widget 走 (1)
    constexpr auto &addChild(WidgetBuild auto &&b) noexcept
    {
        return addChild(
            static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
    }
    // (3) FlexibleBuild / ExpandedBuild / SpacerBuild：解包为 (child, flex, fit)
    template <FlexChildBuilder B>
    constexpr auto &addChild(B &&fb) noexcept
    {
        children_.push_back(fb.takeChild());
        flexes_.push_back(FlexInfo{fb.flexValue(), fb.fitValue()});
        return *this;
    }
    // =====================================================================

    // ── Flexible DSL：解包为 (child, flex, fit) ──────────────────
    constexpr auto &addChild(FlexibleBuild &fb) noexcept
    {
        children_.push_back(fb.takeChild());
        flexes_.push_back(FlexInfo{fb.flexValue(), fb.fitValue()});
        return *this;
    }

  private:
    std::string key_;
    std::optional<Axis> direction_;
    MainAxisAlignment mainAxisAlignment_ = MainAxisAlignment::start;
    MainAxisSize mainAxisSize_ = MainAxisSize::max;
    CrossAxisAlignment crossAxisAlignment_ = CrossAxisAlignment::center;
    std::optional<TextDirection> textDirection_;
    VerticalDirection verticalDirection_ = VerticalDirection::down;
    std::optional<TextBaseline> textBaseline_;
    std::vector<std::unique_ptr<Widget>> children_;
    std::vector<FlexInfo> flexes_;
};
static constexpr auto Flex(std::string key = {}) noexcept // NOLINT
{
    return FlexBuild{std::move(key)};
}

// ===============================================================================================
// Row / Column —— Flex 的语法糖
//
// 语义（对应 Dart 的 Row / Column）：
//   Row    = Flex(direction: Axis::horizontal)
//   Column = Flex(direction: Axis::vertical)
//
// 方向是固定值，调用方不可覆盖；其余字段（mainAxisAlignment / mainAxisSize /
// crossAxisAlignment / textDirection / verticalDirection / textBaseline / children）
// 全部继承自 FlexBuild。
// ===============================================================================================
struct RowBuild : FlexBuild
{
    constexpr RowBuild(std::string key) noexcept : FlexBuild(std::move(key))
    {
        FlexBuild::direction(Axis::horizontal);
    }
    // Row 方向固定为 horizontal：删掉继承来的 direction() 以禁止覆盖
    FlexBuild &direction(Axis) noexcept = delete;
};
struct ColumnBuild : FlexBuild
{
    constexpr ColumnBuild(std::string key) noexcept : FlexBuild(std::move(key))
    {
        FlexBuild::direction(Axis::vertical);
    }
    // Column 方向固定为 vertical：删掉继承来的 direction() 以禁止覆盖
    FlexBuild &direction(Axis) noexcept = delete;
};
static constexpr auto Row(std::string key = {}) noexcept // NOLINT
{
    return RowBuild{std::move(key)};
}
static constexpr auto Column(std::string key = {}) noexcept // NOLINT
{
    return ColumnBuild{std::move(key)};
}

//NOTE: ParentDataWidget<FlexParentData> 的解决是，引入 FlexibleBuild 重构 flex. 当前和flutter的有点不一样了。但是 FlexInfo 将传递方便很多。影响访问是明确的
static constexpr auto Flexible(std::string key = {}) noexcept // NOLINT
{
    return FlexibleBuild{std::move(key)};
}

// =========================================================================
// ExpandedBuild —— Flexible(fit: FlexFit.tight) 的语法糖
// 复用 FlexibleBuild 的全部字段与解包逻辑，只把 fit 锁死为 tight
// =========================================================================
struct ExpandedBuild : FlexibleBuild
{
    constexpr explicit ExpandedBuild(std::string key = {}) noexcept
        : FlexibleBuild(std::move(key))
    {
        FlexibleBuild::fit(FlexFit::tight);
    }

    // 禁止外部覆盖 fit：Expanded 的语义就是 tight
    FlexibleBuild &fit(FlexFit) noexcept = delete;
};
static constexpr auto Expanded(std::string key = {}) noexcept // NOLINT
{
    return ExpandedBuild{std::move(key)};
}

// NOTE: Spacer 生成一个携带 key 的 0×0 SizedBoxWidget，同时把 (flex, tight) 交给 Flex 记账。
// NOTE: 唯一的改动是把 FlexBuild::addChild(FlexibleBuild&) 泛化成 concept 重载，让 Spacer 也能走同一条路
/*
class Spacer extends StatelessWidget {
  const Spacer({super.key, this.flex = 1}) : assert(flex > 0);
  final int flex;
  Widget build(BuildContext context) => Expanded(
    flex: flex,
    child: const SizedBox.shrink(),   // 0×0 空盒
  );
}
关键点：
    Spacer 本身是 StatelessWidget，其 key 传给它自己；子树里是 Expanded(tight) → SizedBox.shrink()。

    find.byKey('s') 命中的是 Spacer，renderObject 回溯到它子树里的 RenderConstrainedBox，即 SizedBox.shrink() 的 renderObject。

    所以 Spacer 在运行时树里必须真实存在一个 Widget（不同于 Flexible），能被 find，能被 getSize 拿到尺寸。

    尺寸行为 = SizedBox(0,0) 在 tight 约束下被撑成 (分配的主轴 × 0 交叉轴)。
*/
// =========================================================================
// SpacerBuild —— Spacer 的等价物
// 运行时树里生成一个真实存在的、携带 key 的 0×0 SizedBoxWidget；
// 同时以 (flex, FlexFit::tight) 参与 Flex 的记账。
// =========================================================================
struct SpacerBuild
{
    constexpr explicit SpacerBuild(std::string key = {}) noexcept : key_(std::move(key))
    {
        assert(flex_ > 0); // Dart: assert(flex > 0)
    }

    constexpr auto &key(std::string k) noexcept
    {
        key_ = std::move(k);
        return *this;
    }

    constexpr auto &flex(int f) noexcept
    {
        assert(f > 0); // Dart: assert(flex > 0)
        flex_ = f;
        return *this;
    }

    // ── 供 FlexBuild 解包（与 FlexibleBuild 相同接口）──────────────
    [[nodiscard]] constexpr int flexValue() const noexcept
    {
        return flex_;
    }
    [[nodiscard]] constexpr FlexFit fitValue() const noexcept
    {
        return FlexFit::tight;
    }

    [[nodiscard]] std::unique_ptr<Widget> takeChild() noexcept
    {
        // 0×0 空盒子，key 由 Spacer 提供
        return std::make_unique<SizedBoxWidget>(std::move(key_), std::nullopt,
                                                std::nullopt, nullptr);
    }

  private:
    std::string key_;
    int flex_ = 1;
};
static constexpr auto Spacer(std::string key = {}) noexcept // NOLINT
{
    return SpacerBuild{std::move(key)};
}

enum class WrapAlignment : std::uint8_t
{
    start,
    end,
    center,
    spaceBetween,
    spaceAround,
    spaceEvenly
};
enum class WrapCrossAlignment : std::uint8_t
{
    start,
    end,
    center
};
struct WrapWidget : Widget
{
    // NOLINTBEGIN
    constexpr WrapWidget(std::string key, Axis direction, WrapAlignment alignment,
                         double spacing, WrapAlignment runAlignment, double runSpacing,
                         WrapCrossAlignment crossAxisAlignment,
                         std::optional<TextDirection> textDirection,
                         VerticalDirection verticalDirection,
                         std::vector<std::unique_ptr<Widget>> children) noexcept
        : direction_{direction}, alignment_{alignment}, spacing_{spacing},
          runAlignment_{runAlignment}, runSpacing_{runSpacing},
          crossAxisAlignment_{crossAxisAlignment}, textDirection_{textDirection},
          verticalDirection_{verticalDirection},
          children_{std::move(children)} // NOLINTEND
    {
        Self().key = std::move(key);
    }

    constexpr void layout(BoxConstraints c) override
    {
        constexpr auto inf = std::numeric_limits<double>::infinity();
        const bool h = direction_ == Axis::horizontal;
        const double maxMain = h ? c.maxWidth() : c.maxHeight();

        // ── 空 children：返回 constraints.smallest ────────────────
        if (children_.empty())
        {
            Self().size = c.smallest();
            runs_.clear();
            return;
        }

        // ── flip 判定（严格按 RenderWrap.performLayout）────────────
        flipMainAxis_ =
            h ? (textDirection_.value_or(TextDirection::ltr) == TextDirection::rtl)
              : (verticalDirection_ == VerticalDirection::up);
        flipCrossAxis_ =
            h ? (verticalDirection_ == VerticalDirection::up)
              : (textDirection_.value_or(TextDirection::ltr) == TextDirection::rtl);

        // ── 子节点约束：只约束主轴 max，其他无界 ──────────────────
        const BoxConstraints childConstraints =
            h ? BoxConstraints{0.0, maxMain, 0.0, inf}
              : BoxConstraints{0.0, inf, 0.0, maxMain};

        // ── 阶段 1：逐个子节点布局并分组为 runs ───────────────────
        runs_.clear();
        double runMain = 0.0;
        double runCross = 0.0;
        size_t runStart = 0;
        int childCount = 0;
        double mainAxisExtent = 0.0;
        double crossAxisExtent = 0.0;

        for (size_t i = 0; i < children_.size(); ++i)
        {
            auto &child = children_[i];
            child->layout(childConstraints);

            const double childMain = h ? child->size.width : child->size.height;
            const double childCross = h ? child->size.height : child->size.width;

            // 换行判定：runMain + spacing + childMain > mainAxisLimit
            if (childCount > 0 && runMain + spacing_ + childMain > maxMain)
            {
                mainAxisExtent = std::max(mainAxisExtent, runMain);
                crossAxisExtent += runCross;
                if (!runs_.empty())
                    crossAxisExtent += runSpacing_;
                runs_.push_back(RunInfo{runMain, runCross, runStart,
                                        static_cast<size_t>(childCount)});
                runMain = 0.0;
                runCross = 0.0;
                childCount = 0;
                runStart = i;
            }

            runMain += childMain;
            if (childCount > 0)
                runMain += spacing_;
            runCross = std::max(runCross, childCross);
            ++childCount;
        }
        // flush 最后一个 run
        if (childCount > 0)
        {
            mainAxisExtent = std::max(mainAxisExtent, runMain);
            crossAxisExtent += runCross;
            if (!runs_.empty())
                crossAxisExtent += runSpacing_;
            runs_.push_back(
                RunInfo{runMain, runCross, runStart, static_cast<size_t>(childCount)});
        }

        // ── 阶段 2：自身尺寸 ──────────────────────────────────────
        Self().size = c.constrain(h ? Size{mainAxisExtent, crossAxisExtent}
                                    : Size{crossAxisExtent, mainAxisExtent});

        // ── 缓存给 updateOffset 用 ────────────────────────────────
        containerMainExt_ = h ? Self().size.width : Self().size.height;
        containerCrossExt_ = h ? Self().size.height : Self().size.width;
        totalMainExt_ = mainAxisExtent;
        totalCrossExt_ = crossAxisExtent;
    }

    constexpr void updateOffset(Offset offset) noexcept override
    {
        Self().offset = offset;
        if (children_.empty() || runs_.empty())
            return;

        const bool h = direction_ == Axis::horizontal;

        // ── runs 在交叉轴对齐 ────────────────────────────────────
        const double crossAxisFreeSpace =
            std::max(0.0, containerCrossExt_ - totalCrossExt_);
        const int runCount = static_cast<int>(runs_.size());

        double runLeadingSpace = 0.0;
        double runBetweenSpace = 0.0;
        switch (runAlignment_)
        {
        case WrapAlignment::start:
            break;
        case WrapAlignment::end:
            runLeadingSpace = crossAxisFreeSpace;
            break;
        case WrapAlignment::center:
            runLeadingSpace = crossAxisFreeSpace / 2.0;
            break;
        case WrapAlignment::spaceBetween:
            runBetweenSpace = runCount > 1 ? crossAxisFreeSpace / (runCount - 1) : 0.0;
            break;
        case WrapAlignment::spaceAround:
            runBetweenSpace = crossAxisFreeSpace / runCount;
            runLeadingSpace = runBetweenSpace / 2.0;
            break;
        case WrapAlignment::spaceEvenly:
            runBetweenSpace = crossAxisFreeSpace / (runCount + 1);
            runLeadingSpace = runBetweenSpace;
            break;
        }
        runBetweenSpace += runSpacing_; // ← 基础 runSpacing

        double crossAxisOffset =
            flipCrossAxis_ ? containerCrossExt_ - runLeadingSpace : runLeadingSpace;

        for (int i = 0; i < runCount; ++i)
        {
            const auto &run = runs_[i];
            const double runMain = run.mainSize;
            const double runCross = run.crossSize;
            const int cnt = static_cast<int>(run.childCount);

            // ── run 内主轴对齐 ────────────────────────────────────
            const double mainAxisFreeSpace = std::max(0.0, containerMainExt_ - runMain);

            double childLeadingSpace = 0.0;
            double childBetweenSpace = 0.0;
            switch (alignment_)
            {
            case WrapAlignment::start:
                break;
            case WrapAlignment::end:
                childLeadingSpace = mainAxisFreeSpace;
                break;
            case WrapAlignment::center:
                childLeadingSpace = mainAxisFreeSpace / 2.0;
                break;
            case WrapAlignment::spaceBetween:
                childBetweenSpace = cnt > 1 ? mainAxisFreeSpace / (cnt - 1) : 0.0;
                break;
            case WrapAlignment::spaceAround:
                childBetweenSpace = mainAxisFreeSpace / cnt;
                childLeadingSpace = childBetweenSpace / 2.0;
                break;
            case WrapAlignment::spaceEvenly:
                childBetweenSpace = mainAxisFreeSpace / (cnt + 1);
                childLeadingSpace = childBetweenSpace;
                break;
            }
            childBetweenSpace += spacing_; // ← 基础 spacing

            double childMainPosition =
                flipMainAxis_ ? containerMainExt_ - childLeadingSpace : childLeadingSpace;

            // flipCrossAxis 时，run 的交叉轴起点先向下/向上移动一个 runCross
            if (flipCrossAxis_)
                crossAxisOffset -= runCross;

            for (size_t k = 0; k < run.childCount; ++k)
            {
                auto &child = children_[run.startIdx + k];
                const double childMain = h ? child->size.width : child->size.height;
                const double childCross = h ? child->size.height : child->size.width;

                // ── run 内交叉轴对齐（考虑 flipCrossAxis）────────
                const double freeSpace = runCross - childCross;
                double childCrossOffset = 0.0;
                switch (crossAxisAlignment_)
                {
                case WrapCrossAlignment::start:
                    childCrossOffset = flipCrossAxis_ ? freeSpace : 0.0;
                    break;
                case WrapCrossAlignment::end:
                    childCrossOffset = flipCrossAxis_ ? 0.0 : freeSpace;
                    break;
                case WrapCrossAlignment::center:
                    childCrossOffset = freeSpace / 2.0;
                    break;
                }

                // flipMainAxis 时，主轴位置先回退一个 childMain
                if (flipMainAxis_)
                    childMainPosition -= childMain;

                const double mPos = childMainPosition;
                const double cPos = crossAxisOffset + childCrossOffset;

                double dx, dy;
                if (h)
                {
                    dx = offset.x + mPos;
                    dy = offset.y + cPos;
                }
                else
                {
                    dx = offset.x + cPos;
                    dy = offset.y + mPos;
                }

                child->updateOffset({dx, dy});

                if (flipMainAxis_)
                    childMainPosition -= childBetweenSpace;
                else
                    childMainPosition += childMain + childBetweenSpace;
            }

            // ── 进入下一个 run ───────────────────────────────────
            if (flipCrossAxis_)
                crossAxisOffset -= runBetweenSpace;
            else
                crossAxisOffset += runCross + runBetweenSpace;
        }
    }

    constexpr std::vector<Widget *> children() override
    {
        std::vector<Widget *> out;
        out.reserve(children_.size());
        for (auto &p : children_)
            if (p)
                out.push_back(p.get());
        return out;
    }

  private:
    struct RunInfo
    {
        double mainSize;   // 该 run 的主轴尺寸（含 run 内 spacing）
        double crossSize;  // 该 run 的交叉轴尺寸
        size_t startIdx;   // children_ 中的起始下标
        size_t childCount; // 该 run 中子节点数
    };

    Axis direction_;
    WrapAlignment alignment_;
    double spacing_;
    WrapAlignment runAlignment_;
    double runSpacing_;
    WrapCrossAlignment crossAxisAlignment_;
    std::optional<TextDirection> textDirection_;
    VerticalDirection verticalDirection_;
    std::vector<std::unique_ptr<Widget>> children_;

    // layout 阶段填充；updateOffset 阶段读取
    std::vector<RunInfo> runs_;
    bool flipMainAxis_ = false;
    bool flipCrossAxis_ = false;
    double containerMainExt_ = 0.0;  // 自身尺寸在主轴的投影（constrain 后）
    double containerCrossExt_ = 0.0; // 自身尺寸在交叉轴的投影（constrain 后）
    double totalMainExt_ = 0.0;      // 所有 run 累加主轴尺寸
    double totalCrossExt_ = 0.0;     // 所有 run 累加交叉轴尺寸（含 runSpacing）
};
struct WrapBuild
{
    constexpr explicit WrapBuild(std::string key = {}) noexcept : key_(std::move(key)) {}

    constexpr auto &key(std::string k) noexcept
    {
        key_ = std::move(k);
        return *this;
    }
    constexpr auto &direction(Axis v) noexcept
    {
        direction_ = v;
        return *this;
    }
    constexpr auto &alignment(WrapAlignment v) noexcept
    {
        alignment_ = v;
        return *this;
    }
    constexpr auto &spacing(double v) noexcept
    {
        spacing_ = v;
        return *this;
    }
    constexpr auto &runAlignment(WrapAlignment v) noexcept
    {
        runAlignment_ = v;
        return *this;
    }
    constexpr auto &runSpacing(double v) noexcept
    {
        runSpacing_ = v;
        return *this;
    }
    constexpr auto &crossAxisAlignment(WrapCrossAlignment v) noexcept
    {
        crossAxisAlignment_ = v;
        return *this;
    }
    constexpr auto &textDirection(TextDirection v) noexcept
    {
        textDirection_ = v;
        return *this;
    }
    constexpr auto &clearTextDirection() noexcept
    {
        textDirection_.reset();
        return *this;
    }
    constexpr auto &verticalDirection(VerticalDirection v) noexcept
    {
        verticalDirection_ = v;
        return *this;
    }

    constexpr auto &addChild(std::unique_ptr<Widget> child) noexcept
    {
        children_.push_back(std::move(child));
        return *this;
    }
    constexpr auto &addChild(WidgetBuild auto &&b) noexcept
    {
        return addChild(
            static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
    }

    constexpr explicit operator std::unique_ptr<Widget>()
    {
        return std::make_unique<WrapWidget>(std::move(key_), direction_, alignment_,
                                            spacing_, runAlignment_, runSpacing_,
                                            crossAxisAlignment_, textDirection_,
                                            verticalDirection_, std::move(children_));
    }

  private:
    std::string key_;
    Axis direction_ = Axis::horizontal;
    WrapAlignment alignment_ = WrapAlignment::start;
    double spacing_ = 0.0;
    WrapAlignment runAlignment_ = WrapAlignment::start;
    double runSpacing_ = 0.0;
    WrapCrossAlignment crossAxisAlignment_ = WrapCrossAlignment::start;
    std::optional<TextDirection> textDirection_;
    VerticalDirection verticalDirection_ = VerticalDirection::down;
    std::vector<std::unique_ptr<Widget>> children_;
};
static constexpr auto Wrap(std::string key = {}) noexcept // NOLINT
{
    return WrapBuild{std::move(key)};
}

/*
你的模型：
    layout       → 每个 widget 的 (W, H) + Offset 全部确定
    每帧 update  → 把 Model 矩阵上传 shader
    render       → 画像素
Flow 的模型：
    layout       → 只确定 (W, H)，Offset 待定
    paint 阶段   → 回调 delegate，现场算 Matrix4
    render       → 画像素
NOTE: 我不需要，我会将这些信息和renderobject 绑定，更新model矩阵更直接
*/

// =========================================================================
// StackFit
// =========================================================================
enum class StackFit : std::uint8_t
{
    loose,
    expand,
    passthrough
};
// =========================================================================
// StackChildInfo —— Stack 对每个子节点的记账（与 children_ 平行）
// 未设置任何属性 = 非定位；任一非 null = 定位
// =========================================================================
struct StackChildInfo
{
    std::optional<double> left, top, right, bottom, width, height;

    [[nodiscard]] bool isPositioned() const noexcept
    {
        return left.has_value() || top.has_value() || right.has_value() ||
               bottom.has_value() || width.has_value() || height.has_value();
    }
};
// =========================================================================
// PositionedBuild —— Stack 的定位 DSL（构建期，运行时不是 Widget）
// =========================================================================
struct PositionedBuild
{
    std::optional<double> left_, top_, right_, bottom_, width_, height_;
    std::unique_ptr<Widget> child_;

    auto &left(double v) noexcept
    {
        left_ = v;
        return *this;
    }
    auto &top(double v) noexcept
    {
        top_ = v;
        return *this;
    }
    auto &right(double v) noexcept
    {
        right_ = v;
        return *this;
    }
    auto &bottom(double v) noexcept
    {
        bottom_ = v;
        return *this;
    }
    auto &width(double v) noexcept
    {
        width_ = v;
        return *this;
    }
    auto &height(double v) noexcept
    {
        height_ = v;
        return *this;
    }

    // 便捷：四边都为 0（对应 Positioned.fill）
    auto &fill() noexcept
    {
        left_ = 0;
        top_ = 0;
        right_ = 0;
        bottom_ = 0;
        return *this;
    }

    // 便捷：fromRect(left, top, width, height)
    auto &fromRect(double l, double t, double w, double h) noexcept
    {
        left_ = l;
        top_ = t;
        width_ = w;
        height_ = h;
        return *this;
    }

    // 便捷：fromRelativeRect(left, top, right, bottom)
    auto &fromLTRB(double l, double t, double r, double b) noexcept
    {
        left_ = l;
        top_ = t;
        right_ = r;
        bottom_ = b;
        return *this;
    }

    // 便捷：directional（把 start/end 按 textDirection 解析为 left/right）
    auto &directional(TextDirection dir, double start, double top,
                      std::optional<double> end = std::nullopt,
                      std::optional<double> bottom = std::nullopt) noexcept
    {
        if (dir == TextDirection::ltr)
        {
            left_ = start;
            if (end)
                right_ = *end;
        }
        else
        {
            right_ = start;
            if (end)
                left_ = *end;
        }
        top_ = top;
        if (bottom)
            bottom_ = *bottom;
        return *this;
    }

    auto &child(std::unique_ptr<Widget> c) noexcept
    {
        child_ = std::move(c);
        return *this;
    }
    auto &child(WidgetBuild auto &&b) noexcept
    {
        return child(static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
    }

    [[nodiscard]] StackChildInfo info() const noexcept
    {
        return StackChildInfo{left_, top_, right_, bottom_, width_, height_};
    }

    [[nodiscard]] std::unique_ptr<Widget> takeChild() noexcept
    {
        return std::move(child_);
    }
};
static constexpr auto Positioned() noexcept // NOLINT
{
    return PositionedBuild{};
}
// =========================================================================
// StackWidget
// =========================================================================
struct StackWidget : Widget
{
    StackWidget(std::string key, Alignment alignment, StackFit fit,
                std::vector<std::unique_ptr<Widget>> children,
                std::vector<StackChildInfo> infos) noexcept
        : alignment_{alignment}, fit_{fit}, children_{std::move(children)},
          infos_{std::move(infos)}
    {
        Self().key = std::move(key);
        assert(children_.size() == infos_.size());
    }

    void layout(BoxConstraints c) override
    {
        // ── 空 children ─────────────────────────────────────────
        if (children_.empty())
        {
            const Size big = c.biggest();
            const bool finite = std::isfinite(big.width) && std::isfinite(big.height);
            Self().size = finite ? big : c.smallest();
            return;
        }

        // ── 非定位子节点的约束（按 fit 变换）────────────────────
        BoxConstraints npc;
        switch (fit_)
        {
        case StackFit::loose:
            npc = c.loosen();
            break;
        case StackFit::expand:
            npc = BoxConstraints::tight(c.biggest());
            break;
        case StackFit::passthrough:
            npc = c;
            break;
        }

        // ── 布局非定位子节点，累加最大尺寸 ─────────────────────
        bool hasNonPositioned = false;
        double width = c.minWidth();
        double height = c.minHeight();

        for (size_t i = 0; i < children_.size(); ++i)
        {
            if (!infos_[i].isPositioned())
            {
                hasNonPositioned = true;
                children_[i]->layout(npc);
                width = std::max(width, children_[i]->size.width);
                height = std::max(height, children_[i]->size.height);
            }
        }

        // ── Stack 尺寸 ─────────────────────────────────────────
        Self().size = hasNonPositioned ? Size{width, height} : c.biggest();

        // ── 布局定位子节点 ─────────────────────────────────────
        for (size_t i = 0; i < children_.size(); ++i)
        {
            if (infos_[i].isPositioned())
                layoutPositionedChild(*children_[i], infos_[i]);
        }
    }

    constexpr void updateOffset(Offset offset) noexcept override
    {
        Self().offset = offset;

        for (size_t i = 0; i < children_.size(); ++i)
        {
            auto &child = children_[i];
            auto &info = infos_[i];

            double x, y;
            if (!info.isPositioned())
            {
                const double freeW = Self().size.width - child->size.width;
                const double freeH = Self().size.height - child->size.height;
                x = freeW * (alignment_.x + 1.0) / 2.0;
                y = freeH * (alignment_.y + 1.0) / 2.0;
            }
            else
            {
                // 水平
                if (info.left)
                    x = *info.left;
                else if (info.right)
                    x = Self().size.width - *info.right - child->size.width;
                else
                {
                    const double freeW = Self().size.width - child->size.width;
                    x = freeW * (alignment_.x + 1.0) / 2.0;
                }
                // 垂直
                if (info.top)
                    y = *info.top;
                else if (info.bottom)
                    y = Self().size.height - *info.bottom - child->size.height;
                else
                {
                    const double freeH = Self().size.height - child->size.height;
                    y = freeH * (alignment_.y + 1.0) / 2.0;
                }
            }

            child->updateOffset({offset.x + x, offset.y + y});
        }
    }

    std::vector<Widget *> children() override
    {
        std::vector<Widget *> out;
        out.reserve(children_.size());
        for (auto &p : children_)
            if (p)
                out.push_back(p.get());
        return out;
    }

  private:
    // 对应 RenderStack.layoutPositionedChild
    void layoutPositionedChild(Widget &child, const StackChildInfo &info)
    {
        BoxConstraints cc{0.0, BoxConstraints::inf, 0.0, BoxConstraints::inf};

        if (info.left && info.right)
        {
            const double w = Self().size.width - *info.right - *info.left;
            cc = BoxConstraints{w, w, cc.minHeight(), cc.maxHeight()};
        }
        else if (info.width)
        {
            const double w = *info.width;
            cc = BoxConstraints{w, w, cc.minHeight(), cc.maxHeight()};
        }

        if (info.top && info.bottom)
        {
            const double h = Self().size.height - *info.bottom - *info.top;
            cc = BoxConstraints{cc.minWidth(), cc.maxWidth(), h, h};
        }
        else if (info.height)
        {
            const double h = *info.height;
            cc = BoxConstraints{cc.minWidth(), cc.maxWidth(), h, h};
        }

        child.layout(cc);
    }

    Alignment alignment_ = Alignment::topLeft; // = AlignmentDirectional.topStart (ltr)
    StackFit fit_ = StackFit::loose;
    std::vector<std::unique_ptr<Widget>> children_;
    std::vector<StackChildInfo> infos_;
};
// =========================================================================
// StackBuild / Stack
// =========================================================================
struct StackBuild
{
    explicit StackBuild(std::string key = {}) noexcept : key_(std::move(key)) {}

    auto &key(std::string k) noexcept
    {
        key_ = std::move(k);
        return *this;
    }
    auto &alignment(Alignment a) noexcept
    {
        alignment_ = a;
        return *this;
    }
    auto &fit(StackFit f) noexcept
    {
        fit_ = f;
        return *this;
    }

    // 非定位子节点
    auto &addChild(std::unique_ptr<Widget> child) noexcept
    {
        children_.push_back(std::move(child));
        infos_.push_back({}); // 未设置任何属性 → 非定位
        return *this;
    }
    auto &addChild(WidgetBuild auto &&b) noexcept
    {
        return addChild(
            static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
    }

    // 定位子节点
    auto &addChild(PositionedBuild &pb) noexcept
    {
        infos_.push_back(pb.info());
        children_.push_back(pb.takeChild());
        return *this;
    }

    explicit operator std::unique_ptr<Widget>()
    {
        return std::make_unique<StackWidget>(std::move(key_), alignment_, fit_,
                                             std::move(children_), std::move(infos_));
    }

  private:
    std::string key_;
    Alignment alignment_ = Alignment::topLeft;
    StackFit fit_ = StackFit::loose;
    std::vector<std::unique_ptr<Widget>> children_;
    std::vector<StackChildInfo> infos_;
};
static constexpr auto Stack(std::string key = {}) noexcept // NOLINT
{
    return StackBuild{std::move(key)};
}

///NORE: Baseline 是针对文本布局的，现在不需要

struct OffstageWidget : Widget
{
    OffstageWidget(std::string key, bool offstage, std::unique_ptr<Widget> child) noexcept
        : offstage_{offstage}, child_{std::move(child)}
    {
        Self().key = std::move(key);
    }

    void layout(BoxConstraints c) override
    {
        // child 无论 offstage 与否都拿到原约束并 layout
        if (child_)
            child_->layout(c);

        // 自身 size：
        //   offstage=true  → constraints.smallest
        //   offstage=false → child.size（无 child 时也是 smallest）
        if (offstage_ || !child_)
            Self().size = c.smallest();
        else
            Self().size = child_->size;
    }

    constexpr void updateOffset(Offset offset) noexcept override
    {
        Self().offset = offset;
        if (child_)
            child_->updateOffset(offset); // 相对 Offstage 恒为 (0,0)
    }
    std::vector<Widget *> children() override
    {
        return child_ ? std::vector<Widget *>{child_.get()} : std::vector<Widget *>{};
    }

  private:
    bool offstage_;
    std::unique_ptr<Widget> child_;
};
struct OffstageBuild
{
    explicit OffstageBuild(std::string key = {}) noexcept : key_(std::move(key)) {}
    explicit operator std::unique_ptr<Widget>()
    {
        return std::make_unique<OffstageWidget>(std::move(key_), offstage_,
                                                std::move(child_));
    }

    auto &key(std::string k) noexcept
    {
        key_ = std::move(k);
        return *this;
    }
    auto &offstage(bool v) noexcept
    {
        offstage_ = v;
        return *this;
    }

    auto &child(std::unique_ptr<Widget> c) noexcept
    {
        child_ = std::move(c);
        return *this;
    }
    auto &child(WidgetBuild auto &&b) noexcept
    {
        return child(static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
    }

  private:
    std::string key_;
    bool offstage_ = true;
    std::unique_ptr<Widget> child_;
};
static constexpr auto Offstage(std::string key = {}) noexcept // NOLINT
{
    return OffstageBuild{std::move(key)};
}

// NOTE: CustomSingleChildLayout 就是把这个决策权直接暴露给用户的 widget
// =========================================================================
// SingleChildLayoutDelegate —— 用户自定义单子节点布局的接口
// 三个方法全部在 layout 阶段调用，返回值直接决定 W/H + Offset
// =========================================================================
struct SingleChildLayoutDelegate
{
    virtual ~SingleChildLayoutDelegate() = default;

    /// 返回自身想要的尺寸，会被父约束 constrain
    virtual Size getSize(BoxConstraints c) const
    {
        return c.biggest();
    }

    /// 返回传给 child 的约束（不会被框架再处理，delegate 负责合理性）
    virtual BoxConstraints getConstraintsForChild(BoxConstraints c) const
    {
        return c;
    }

    /// 返回 child 相对自身的偏移
    virtual Offset getPositionForChild(Size selfSize, Size childSize) const
    {
        return {0.0, 0.0};
    }
};
struct CustomSingleChildLayoutWidget : Widget
{
    CustomSingleChildLayoutWidget(std::string key,
                                  std::unique_ptr<SingleChildLayoutDelegate> delegate,
                                  std::unique_ptr<Widget> child) noexcept
        : delegate_{std::move(delegate)}, child_{std::move(child)}
    {
        Self().key = std::move(key);
        assert(delegate_);
    }

    void layout(BoxConstraints c) override
    {
        // 1. 自身尺寸 = constrain(delegate.getSize(c))
        Self().size = c.constrain(delegate_->getSize(c));

        // 2. child 用 delegate 给定的约束 layout
        if (child_)
            child_->layout(delegate_->getConstraintsForChild(c));
    }

    void updateOffset(Offset offset) noexcept override
    {
        Self().offset = offset;
        if (!child_)
            return;

        // 3. 位置 = delegate.getPositionForChild(自身 size, child size)
        const Offset p = delegate_->getPositionForChild(Self().size, child_->size);
        child_->updateOffset({offset.x + p.x, offset.y + p.y});
    }

    std::vector<Widget *> children() override
    {
        return child_ ? std::vector<Widget *>{child_.get()} : std::vector<Widget *>{};
    }

  private:
    std::unique_ptr<SingleChildLayoutDelegate> delegate_;
    std::unique_ptr<Widget> child_;
};
struct CustomSingleChildLayoutBuild
{
    explicit CustomSingleChildLayoutBuild(std::string key = {}) noexcept
        : key_(std::move(key))
    {
    }

    auto &key(std::string k) noexcept
    {
        key_ = std::move(k);
        return *this;
    }

    auto &delegate(std::unique_ptr<SingleChildLayoutDelegate> d) noexcept
    {
        delegate_ = std::move(d);
        return *this;
    }

    auto &child(std::unique_ptr<Widget> c) noexcept
    {
        child_ = std::move(c);
        return *this;
    }
    auto &child(WidgetBuild auto &&b) noexcept
    {
        return child(static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
    }

    explicit operator std::unique_ptr<Widget>()
    {
        if (!delegate_)
            throw std::logic_error{"CustomSingleChildLayout requires a delegate"};
        return std::make_unique<CustomSingleChildLayoutWidget>(
            std::move(key_), std::move(delegate_), std::move(child_));
    }

  private:
    std::string key_;
    std::unique_ptr<SingleChildLayoutDelegate> delegate_;
    std::unique_ptr<Widget> child_;
};
static constexpr auto CustomSingleChildLayout(std::string key = {}) noexcept // NOLINT
{
    return CustomSingleChildLayoutBuild{std::move(key)};
}

// =========================================================================
// MultiChildLayoutContext —— delegate.performLayout 里用来操作 children
// hasChild / layoutChild / positionChild 对应 Flutter 的同名 API
// =========================================================================
struct MultiChildLayoutContext
{
    Size size{};
    std::function<bool(const std::string &)> hasChild;
    std::function<Size(const std::string &, BoxConstraints)> layoutChild;
    std::function<void(const std::string &, Offset)> positionChild;
};
// =========================================================================
// MultiChildLayoutDelegate
// =========================================================================
struct MultiChildLayoutDelegate
{
    virtual ~MultiChildLayoutDelegate() = default;

    /// 返回自身想要的尺寸，被父约束 constrain
    virtual Size getSize(BoxConstraints c) const
    {
        return c.biggest();
    }

    /// 在 layout 阶段调用：用 ctx.layoutChild / ctx.positionChild 摆放 children
    virtual void performLayout(Size size, MultiChildLayoutContext &ctx) const = 0;
};
// =========================================================================
// LayoutIdBuild —— 把 (id, child) 绑定起来给 delegate 引用
// 运行时不是 Widget，被 CustomMultiChildLayoutBuild 解包
// =========================================================================
struct LayoutIdBuild
{
    std::string id_;
    std::unique_ptr<Widget> child_;

    explicit LayoutIdBuild(std::string id) : id_{std::move(id)} {}

    auto &child(std::unique_ptr<Widget> c)
    {
        child_ = std::move(c);
        return *this;
    }
    auto &child(WidgetBuild auto &&b)
    {
        return child(static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
    }
};
static auto LayoutId(std::string id) noexcept // NOLINT
{
    return LayoutIdBuild{std::move(id)};
}
struct CustomMultiChildLayoutWidget : Widget
{
    CustomMultiChildLayoutWidget(std::string key,
                                 std::unique_ptr<MultiChildLayoutDelegate> delegate,
                                 std::vector<std::unique_ptr<Widget>> children,
                                 std::vector<std::string> ids) noexcept
        : delegate_{std::move(delegate)}, children_{std::move(children)},
          ids_{std::move(ids)}
    {
        Self().key = std::move(key);
        assert(delegate_);
        assert(children_.size() == ids_.size());
        for (size_t i = 0; i < ids_.size(); ++i)
            idToIndex_[ids_[i]] = i;
        positions_.resize(children_.size(), Offset{0.0, 0.0});
    }

    void layout(BoxConstraints c) override
    {
        // 1. 自身尺寸 = constrain(delegate.getSize(c))
        Self().size = c.constrain(delegate_->getSize(c));

        // 2. 重置所有 child 位置为 (0, 0)
        std::fill(positions_.begin(), positions_.end(), Offset{0.0, 0.0});

        // 3. 构造 context 交给 delegate
        MultiChildLayoutContext ctx;
        ctx.size = Self().size;
        ctx.hasChild = [this](const std::string &id) -> bool {
            return idToIndex_.count(id) > 0;
        };
        ctx.layoutChild = [this](const std::string &id, BoxConstraints cc) -> Size {
            auto it = idToIndex_.find(id);
            if (it == idToIndex_.end())
                return {0.0, 0.0};
            auto &child = children_[it->second];
            child->layout(cc);
            return child->size;
        };
        ctx.positionChild = [this](const std::string &id, Offset o) {
            auto it = idToIndex_.find(id);
            if (it == idToIndex_.end())
                return;
            positions_[it->second] = o;
        };

        delegate_->performLayout(Self().size, ctx);
    }

    void updateOffset(Offset offset) noexcept override
    {
        Self().offset = offset;
        for (size_t i = 0; i < children_.size(); ++i)
            children_[i]->updateOffset({
                offset.x + positions_[i].x,
                offset.y + positions_[i].y,
            });
    }

    std::vector<Widget *> children() override
    {
        std::vector<Widget *> out;
        out.reserve(children_.size());
        for (auto &c : children_)
            if (c)
                out.push_back(c.get());
        return out;
    }

  private:
    std::unique_ptr<MultiChildLayoutDelegate> delegate_;
    std::vector<std::unique_ptr<Widget>> children_;
    std::vector<std::string> ids_;
    std::unordered_map<std::string, size_t> idToIndex_;
    std::vector<Offset> positions_;
};
struct CustomMultiChildLayoutBuild
{
    explicit CustomMultiChildLayoutBuild(std::string key = {}) noexcept
        : key_(std::move(key))
    {
    }

    auto &key(std::string k) noexcept
    {
        key_ = std::move(k);
        return *this;
    }

    auto &delegate(std::unique_ptr<MultiChildLayoutDelegate> d) noexcept
    {
        delegate_ = std::move(d);
        return *this;
    }

    auto &addChild(LayoutIdBuild &lib) noexcept
    {
        ids_.push_back(lib.id_);
        children_.push_back(std::move(lib.child_));
        return *this;
    }

    explicit operator std::unique_ptr<Widget>()
    {
        if (!delegate_)
            throw std::logic_error{"CustomMultiChildLayout requires a delegate"};
        return std::make_unique<CustomMultiChildLayoutWidget>(
            std::move(key_), std::move(delegate_), std::move(children_), std::move(ids_));
    }

  private:
    std::string key_;
    std::unique_ptr<MultiChildLayoutDelegate> delegate_;
    std::vector<std::unique_ptr<Widget>> children_;
    std::vector<std::string> ids_;
};
static auto CustomMultiChildLayout(std::string key = {}) noexcept // NOLINT
{
    return CustomMultiChildLayoutBuild{std::move(key)};
}

struct RotatedBoxWidget : Widget
{
    RotatedBoxWidget(std::string key, int quarterTurns,
                     std::unique_ptr<Widget> child) noexcept
        : quarterTurns_{quarterTurns}, child_{std::move(child)}
    {
        Self().key = std::move(key);
    }

    void layout(BoxConstraints c) override
    {
        if (!child_)
        {
            Self().size = c.smallest();
            return;
        }

        const bool odd = (quarterTurns_ % 2) != 0;

        // 奇数 quarterTurns：约束宽高互换
        const BoxConstraints childC =
            odd ? BoxConstraints{c.minHeight(), c.maxHeight(), c.minWidth(), c.maxWidth()}
                : c;

        child_->layout(childC);

        // 自身 size = constrain(旋转后 childSize)
        Size s = child_->size;
        if (odd)
            std::swap(s.width, s.height);
        Self().size = c.constrain(s);
    }

    void updateOffset(Offset offset) noexcept override
    {
        Self().offset = offset;
        if (!child_)
            return;

        const int q = ((quarterTurns_ % 4) + 4) % 4;
        const Size cs = child_->size; // 未旋转的 child 尺寸

        Offset p;
        switch (q)
        {
        case 0:
            p = {0.0, 0.0};
            break;
        case 1:
            p = {cs.height, 0.0};
            break;
        case 2:
            p = {cs.width, cs.height};
            break;
        default:
            p = {0.0, cs.width};
            break; // q == 3
        }

        child_->updateOffset({offset.x + p.x, offset.y + p.y});
    }

    std::vector<Widget *> children() override
    {
        return child_ ? std::vector<Widget *>{child_.get()} : std::vector<Widget *>{};
    }

  private:
    int quarterTurns_;
    std::unique_ptr<Widget> child_;
};
struct RotatedBoxBuild
{
    explicit RotatedBoxBuild(std::string key = {}) noexcept : key_(std::move(key)) {}

    auto &key(std::string k) noexcept
    {
        key_ = std::move(k);
        return *this;
    }
    auto &quarterTurns(int q) noexcept
    {
        quarterTurns_ = q;
        return *this;
    }

    auto &child(std::unique_ptr<Widget> c) noexcept
    {
        child_ = std::move(c);
        return *this;
    }
    auto &child(WidgetBuild auto &&b) noexcept
    {
        return child(static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
    }

    explicit operator std::unique_ptr<Widget>()
    {
        return std::make_unique<RotatedBoxWidget>(std::move(key_), quarterTurns_,
                                                  std::move(child_));
    }

  private:
    std::string key_;
    int quarterTurns_ = 0;
    std::unique_ptr<Widget> child_;
};
static constexpr auto RotatedBox(std::string key = {}) noexcept // NOLINT
{
    return RotatedBoxBuild{std::move(key)};
}

// ===============================================================================================
// flex 模式 [end]
// ===============================================================================================