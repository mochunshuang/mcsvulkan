#include "head.hpp"
#include <any>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <utility>

using mcs::vulkan::ecs::gen_soa_struct;
using mcs::vulkan::meta::make_aggregate;
using mcs::vulkan::meta::static_string;

#include <iostream>
#include <exception>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// ====================== 全局基础类型：Entity 与 RefAny ======================
struct Entity
{
    using release_type = std::move_only_function<void(uint32_t) noexcept>;
    static constexpr auto invalid = ~0u;
    uint32_t type_id = invalid;
    uint32_t entity_id = invalid;
    release_type release;

    constexpr bool valid() const noexcept
    {
        return type_id != invalid;
    }
    constexpr Entity(uint32_t type_id, uint32_t entity_id, release_type release)
        : type_id{type_id}, entity_id{entity_id}, release{std::move(release)}
    {
    }
    Entity(const Entity &) = delete;
    Entity &operator=(const Entity &) = delete;
    constexpr Entity(Entity &&o) noexcept
        : type_id(std::exchange(o.type_id, invalid)),
          entity_id(std::exchange(o.entity_id, invalid)), release(std::move(o.release))
    {
    }
    constexpr Entity &operator=(Entity &&o) noexcept
    {
        if (this != &o)
        {
            if (type_id != invalid)
                release(entity_id);
            type_id = std::exchange(o.type_id, invalid);
            entity_id = std::exchange(o.entity_id, invalid);
            release = std::move(o.release);
        }
        return *this;
    }
    constexpr ~Entity() noexcept
    {
        if (type_id != invalid)
        {
            release(entity_id);
            type_id = invalid;
            entity_id = invalid;
        }
    }
};

class RefAny
{
    void *ptr_ = nullptr;
    std::type_index type_ = typeid(void);

  public:
    constexpr RefAny() = default;
    template <typename T>
    constexpr RefAny(T &value) noexcept
        : ptr_(static_cast<void *>(std::addressof(value))), type_(typeid(T))
    {
    }

    constexpr bool has_value() const noexcept
    {
        return ptr_ != nullptr;
    }
    constexpr void *raw_address() const noexcept
    {
        return ptr_;
    }

    template <typename T>
    constexpr friend T &any_cast(RefAny &any)
    {
        if (any.type_ != typeid(T))
            throw std::bad_cast();
        return *static_cast<T *>(any.ptr_);
    }
    template <typename T>
    constexpr friend const T &any_cast(const RefAny &any)
    {
        if (any.type_ != typeid(T))
            throw std::bad_cast();
        return *static_cast<const T *>(any.ptr_);
    }

    template <typename T>
    constexpr auto &cast_to(this auto &&self)
    {
        return any_cast<T>(self);
    }

    template <typename T>
    constexpr auto value_or(this auto &&self, T value) noexcept
        requires(requires { *(any_cast<T>(self)); })
    {
        try
        {
            auto &ref = any_cast<T>(self);
            return ref ? *ref : value;
        }
        catch (...)
        {
        }
        return value;
    }

    template <typename T>
    constexpr T miss_return(this auto &&self, T defaultVal) noexcept
    {
        if (!self.has_value())
            return defaultVal;
        return any_cast<T>(self);
    }
};

// ====================== 全局辅助模板 ======================
template <static_string name>
static constexpr auto do_get_member(auto &soaCtx, const Entity &ref)
{
    return soaCtx.get_member.template operator()<name>(soaCtx, ref);
}

// ====================== Trait 基类（全局）======================
template <static_string Name>
struct TraitBase
{
    static constexpr auto name = Name;
    template <typename SoaCtx>
    static consteval uint32_t type_id(SoaCtx &)
    {
        return std::remove_cvref_t<SoaCtx>::find_name(name);
    }
    template <typename SoaCtx>
    constexpr static auto release(SoaCtx &soaCtx)
        -> std::move_only_function<void(uint32_t) noexcept>
    {
        return [&soaCtx](uint32_t entity_id) noexcept {
            constexpr auto members = std::remove_cvref_t<decltype(soaCtx)>::members;
            constexpr auto I = type_id(soaCtx);
            return soaCtx.[:members[I]:].release_entity(entity_id);
        };
    }
};

// ====================== UI 布局核心（命名空间）======================
namespace ui
{

    // 基础几何类型
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

    // 方向枚举
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

    // [NEW] FlexFit 枚举，对应 Flutter 的 FlexFit
    enum class FlexFit
    {
        tight, // 强制填满分配空间（Expanded 默认行为）
        loose  // 允许子组件小于分配空间（Flexible 行为）
    };

    struct BoxGeometry
    {
        float x = 0, y = 0, w = 0, h = 0;
    };

    // 扁平树节点（使用全局 Entity）
    struct FlatNode
    {
        Entity ref; // 全局 Entity，ui 内可直接访问
        int parent = -1;
        int firstChild = -1;
        int nextSibling = -1;
        BoxGeometry geometry;
        std::string name;
        float baseline = 0;
    };

    // 树管理器
    class FlatLayoutTree
    {
      public:
        std::vector<FlatNode> nodes;

        void reserve(size_t n)
        {
            nodes.reserve(n);
        }
        const auto &getNode(size_t index) noexcept
        {
            return nodes[index];
        }

        int addNode(std::string name, Entity ref, int parentIdx)
        {
            int idx = static_cast<int>(nodes.size());
            nodes.push_back({std::move(ref), parentIdx, -1, -1, {}, std::move(name), 0});
            if (parentIdx != -1)
            {
                FlatNode &p = nodes[parentIdx];
                if (p.firstChild == -1)
                    p.firstChild = idx;
                else
                {
                    int sib = p.firstChild;
                    while (nodes[sib].nextSibling != -1)
                        sib = nodes[sib].nextSibling;
                    nodes[sib].nextSibling = idx;
                }
            }
            return idx;
        }

        void clear()
        {
            nodes.clear();
        }

        template <typename F>
        void forEachChild(int idx, F &&f) const
        {
            for (int c = nodes[idx].firstChild; c != -1; c = nodes[c].nextSibling)
                f(c);
        }
    };

    // 主轴/交叉轴工具函数
    enum class AxisDirection
    {
        right,
        left,
        down,
        up
    };

    inline AxisDirection axisDirectionForFlex(bool isRow, TextDirection td,
                                              VerticalDirection vd)
    {
        if (isRow)
            return (td == TextDirection::ltr) ? AxisDirection::right
                                              : AxisDirection::left;
        else
            return (vd == VerticalDirection::down) ? AxisDirection::down
                                                   : AxisDirection::up;
    }

    inline bool isAxisForward(AxisDirection dir)
    {
        return dir == AxisDirection::right || dir == AxisDirection::down;
    }

    inline float mainAxisSizeFromConstraints(const Constraints &bc, bool isRow)
    {
        return isRow ? bc.maxW : bc.maxH;
    }
    inline float crossAxisSizeFromConstraints(const Constraints &bc, bool isRow)
    {
        return isRow ? bc.maxH : bc.maxW;
    }

    inline float mainAxisPaddingStart(const EdgeInsets &pad, AxisDirection dir)
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
    inline float mainAxisPaddingEnd(const EdgeInsets &pad, AxisDirection dir)
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
    inline float crossAxisPaddingStart(const EdgeInsets &pad, AxisDirection dir)
    {
        if (dir == AxisDirection::right || dir == AxisDirection::left)
            return pad.top;
        else
            return pad.left;
    }
    inline float crossAxisPaddingEnd(const EdgeInsets &pad, AxisDirection dir)
    {
        if (dir == AxisDirection::right || dir == AxisDirection::left)
            return pad.bottom;
        else
            return pad.right;
    }

    inline float childMainAxisLength(const FlatNode &child, bool isRow)
    {
        return isRow ? child.geometry.w : child.geometry.h;
    }
    inline float childCrossAxisLength(const FlatNode &child, bool isRow)
    {
        return isRow ? child.geometry.h : child.geometry.w;
    }

    inline void setChildMainAxisPosition(FlatNode &child, AxisDirection dir, float pos,
                                         float childMainLen, float parentMainSize,
                                         float padStart, float padEnd)
    {
        float physical = isAxisForward(dir)
                             ? padStart + pos
                             : parentMainSize - padEnd - pos - childMainLen;
        if (dir == AxisDirection::right || dir == AxisDirection::left)
            child.geometry.x = physical;
        else
            child.geometry.y = physical;
    }
    inline void setChildCrossAxisPosition(FlatNode &child, AxisDirection dir, float pos,
                                          float /*childCrossLen*/,
                                          float /*parentCrossSize*/, float padCrossStart,
                                          float /*padCrossEnd*/)
    {
        if (dir == AxisDirection::right || dir == AxisDirection::left)
            child.geometry.y = padCrossStart + pos;
        else
            child.geometry.x = padCrossStart + pos;
    }

    inline bool mainAxisIsBounded(const Constraints &bc, bool isRow)
    {
        return isRow ? bc.hasBoundedWidth() : bc.hasBoundedHeight();
    }
    inline bool crossAxisIsBounded(const Constraints &bc, bool isRow)
    {
        return isRow ? bc.hasBoundedHeight() : bc.hasBoundedWidth();
    }

    inline Constraints makeFlexAxisConstraints(bool isRow, float minMain, float maxMain,
                                               float minCross, float maxCross)
    {
        if (isRow)
            return {minMain, maxMain, minCross, maxCross};
        else
            return {minCross, maxCross, minMain, maxMain};
    }

    // 布局辅助函数
    inline Constraints makeLooseConstraints(const Constraints &c)
    {
        return {0.0f, c.maxW, 0.0f, c.maxH};
    }

    inline Size childFullSize(auto &soaCtx, const FlatNode &child)
    {
        EdgeInsets m =
            do_get_member<"margin">(soaCtx, child.ref).miss_return(EdgeInsets{});
        return {child.geometry.w + m.horizontal(), child.geometry.h + m.vertical()};
    }

    inline Offset alignmentOffset(const Alignment &align, float extraW, float extraH,
                                  const EdgeInsets &pad, const EdgeInsets &margin,
                                  const EdgeInsets &border)
    {
        return {border.left + pad.left + margin.left + extraW * (align.x + 1.0f) / 2.0f,
                border.top + pad.top + margin.top + extraH * (align.y + 1.0f) / 2.0f};
    }

    inline void positionChildByAlignment(auto &soaCtx, FlatNode &child,
                                         const Constraints &containerConstraints,
                                         const EdgeInsets &padding,
                                         const std::optional<Alignment> &align,
                                         const EdgeInsets &border)
    {
        EdgeInsets childMargin =
            do_get_member<"margin">(soaCtx, child.ref).miss_return(EdgeInsets{});
        Size childFull = childFullSize(soaCtx, child);
        float contentW =
            containerConstraints.maxW - border.horizontal() - padding.horizontal();
        float contentH =
            containerConstraints.maxH - border.vertical() - padding.vertical();
        float extraW = std::max(0.0f, contentW - childFull.width);
        float extraH = std::max(0.0f, contentH - childFull.height);
        Alignment al = align.value_or(Align::topLeft);
        Offset offset = alignmentOffset(al, extraW, extraH, padding, childMargin, border);
        child.geometry.x = offset.x;
        child.geometry.y = offset.y;
    }

    // ====================== FlexChildInfo ======================
    struct FlexChildInfo
    {
        int nodeIdx = -1;
        float flex = 0;
        FlexFit flexFit = FlexFit::tight; // [NEW] 默认为 tight
        EdgeInsets margin;

        float marginMain(bool isRow) const
        {
            return isRow ? margin.horizontal() : margin.vertical();
        }
        float marginCross(bool isRow) const
        {
            return isRow ? margin.vertical() : margin.horizontal();
        }
    };

    // ====================== collectFlexChildren ======================
    inline std::vector<FlexChildInfo> collectFlexChildren(auto &soaCtx,
                                                          FlatLayoutTree &tree,
                                                          int parentIdx, bool isRow,
                                                          float &outTotalFlex)
    {
        std::vector<FlexChildInfo> infos;
        outTotalFlex = 0.0f;
        constexpr auto expandedTypeId =
            std::decay_t<decltype(soaCtx)>::find_name("expandeds");
        static_assert(expandedTypeId != ~0u);

        for (int c = tree.nodes[parentIdx].firstChild; c != -1;
             c = tree.nodes[c].nextSibling)
        {
            FlatNode &child = tree.nodes[c];
            if (size_t(child.ref.type_id) == expandedTypeId)
            {
                if (child.firstChild == -1)
                    throw std::logic_error("Expanded/Flexible widget has no child.");
                int realIdx = child.firstChild;
                auto [flexVal, flexFitVal] = // [FIX] 读取 flexFit
                    soaCtx.expandeds.template view_entity<"flex", "flexFit">(
                        0, child.ref.entity_id);
                EdgeInsets m = do_get_member<"margin">(soaCtx, tree.nodes[realIdx].ref)
                                   .miss_return(EdgeInsets{});
                infos.push_back({realIdx, (float)flexVal, flexFitVal, m});
                outTotalFlex += flexVal;
            }
            else
            {
                EdgeInsets m =
                    do_get_member<"margin">(soaCtx, child.ref).miss_return(EdgeInsets{});
                infos.push_back({c, 0.0f, FlexFit::tight, m});
            }
        }
        return infos;
    }

    // ====================== layoutChildrenNaturally ======================
    inline std::vector<float> layoutChildrenNaturally(
        auto &soaCtx, FlatLayoutTree &tree, const std::vector<FlexChildInfo> &infos,
        bool isRow, bool /*isBoundedMain*/, float /*innerMain*/, float innerCross,
        CrossAxisAlignment crossAlign)
    {
        std::vector<float> naturalMain;
        naturalMain.reserve(infos.size());
        for (const auto &info : infos)
        {
            // 弹性子组件用 0 主轴最大约束（为了测量交叉轴）
            float childMainMax = (info.flex > 0) ? 0.0f : Constraints::inf;
            float childCrossMax = innerCross;
            float childCrossMin =
                (crossAlign == CrossAxisAlignment::stretch) ? childCrossMax : 0.0f;
            Constraints childBC = makeFlexAxisConstraints(isRow, 0.0f, childMainMax,
                                                          childCrossMin, childCrossMax);
            // 非弹性子组件才 deflate margin（弹性子组件后续布局会处理）
            if (info.flex == 0)
                childBC = childBC.deflate(info.margin);
            soaCtx.layout(soaCtx, tree, info.nodeIdx, childBC);
            naturalMain.push_back(childMainAxisLength(tree.nodes[info.nodeIdx], isRow));
        }
        return naturalMain;
    }

    // ====================== distributeFlexSpace ======================
    inline void distributeFlexSpace(auto &soaCtx, FlatLayoutTree &tree,
                                    const std::vector<FlexChildInfo> &infos,
                                    std::vector<float> &naturalMain, bool isRow,
                                    float freeMain, float totalFlex, float innerCross,
                                    CrossAxisAlignment crossAlign)
    {
        if (totalFlex <= 0 || freeMain <= 0)
            return;
        float flexUnit = freeMain / totalFlex;
        for (size_t i = 0; i < infos.size(); ++i)
        {
            if (infos[i].flex > 0)
            {
                float allocated = infos[i].flex * flexUnit;
                float childCrossMax = innerCross;
                float childCrossMin =
                    (crossAlign == CrossAxisAlignment::stretch) ? childCrossMax : 0.0f;

                // [FIX] 根据 flexFit 选择约束类型
                float minMain = (infos[i].flexFit == FlexFit::loose) ? 0.0f : allocated;
                float maxMain = allocated;

                Constraints flexBC = makeFlexAxisConstraints(
                    isRow, minMain, maxMain, childCrossMin, childCrossMax);
                // 扣除 margin
                flexBC = flexBC.deflate(infos[i].margin);
                soaCtx.layout(soaCtx, tree, infos[i].nodeIdx, flexBC);
                naturalMain[i] = childMainAxisLength(tree.nodes[infos[i].nodeIdx], isRow);
            }
        }
    }

    // computeFinalMainSize 不变
    inline float computeFinalMainSize(bool isBoundedMain, float totalNatural,
                                      float innerMain, MainAxisSize axisSize)
    {
        if (!isBoundedMain)
            return totalNatural;
        if (axisSize == MainAxisSize::min)
            return std::min(totalNatural, innerMain);
        return innerMain;
    }

    // computeMainGapAndStart 不变
    inline void computeMainGapAndStart(size_t childCount, float extraMain,
                                       MainAxisAlignment mainAlign, float &outMainGap,
                                       float &outMainStartOff)
    {
        outMainGap = 0.0f;
        outMainStartOff = 0.0f;
        if (childCount == 1)
        {
            if (mainAlign == MainAxisAlignment::spaceAround ||
                mainAlign == MainAxisAlignment::spaceEvenly)
                outMainStartOff = extraMain / 2.0f;
        }
        else if (childCount > 1)
        {
            switch (mainAlign)
            {
            case MainAxisAlignment::start:
                outMainStartOff = 0.0f;
                break;
            case MainAxisAlignment::end:
                outMainStartOff = extraMain;
                break;
            case MainAxisAlignment::center:
                outMainStartOff = extraMain / 2.0f;
                break;
            case MainAxisAlignment::spaceBetween:
                outMainGap = extraMain / (childCount - 1);
                break;
            case MainAxisAlignment::spaceAround:
                outMainGap = extraMain / childCount;
                outMainStartOff = outMainGap / 2.0f;
                break;
            case MainAxisAlignment::spaceEvenly:
                outMainGap = extraMain / (childCount + 1);
                outMainStartOff = outMainGap;
                break;
            }
        }
    }

    // ====================== positionChildrenInFlex ======================
    inline void positionChildrenInFlex(
        auto &soaCtx, FlatLayoutTree &tree, const std::vector<FlexChildInfo> &infos,
        const std::vector<float> &naturalMain, AxisDirection dir, bool isRow,
        float /*innerMain*/, float innerCross, float padMainStart, float padMainEnd,
        float padCrossStart, float padCrossEnd, CrossAxisAlignment crossAlign,
        float mainGap, float mainStartOff, float parentMainSize, float parentCrossSize)
    {
        float mainPos = mainStartOff;
        float maxBaseline = 0;
        if (crossAlign == CrossAxisAlignment::baseline && isRow)
        {
            for (const auto &info : infos)
                maxBaseline = std::max(maxBaseline, tree.nodes[info.nodeIdx].baseline);
        }

        for (size_t i = 0; i < infos.size(); ++i)
        {
            FlatNode &child = tree.nodes[infos[i].nodeIdx];
            const EdgeInsets &childMargin = infos[i].margin;
            float childMainLen = naturalMain[i];
            float childCrossLen = childCrossAxisLength(child, isRow);

            float leadingMargin = isRow ? childMargin.left : childMargin.top;
            float crossLeadingMargin = isRow ? childMargin.top : childMargin.left;

            // 主轴位置 = 分配位置 + leading margin
            setChildMainAxisPosition(child, dir, mainPos + leadingMargin, childMainLen,
                                     parentMainSize, padMainStart, padMainEnd);

            float crossStart = 0;
            float crossExtra = std::max(
                0.0f, innerCross - (childCrossLen + infos[i].marginCross(isRow)));
            switch (crossAlign)
            {
            case CrossAxisAlignment::start:
                crossStart = 0;
                break;
            case CrossAxisAlignment::end:
                crossStart = crossExtra;
                break;
            case CrossAxisAlignment::center:
                crossStart = crossExtra / 2.0f;
                break;
            case CrossAxisAlignment::stretch:
                crossStart = 0;
                break;
            case CrossAxisAlignment::baseline:
                crossStart = isRow ? maxBaseline - child.baseline : 0;
                break;
            }
            // 交叉轴位置 = 对齐位置 + cross leading margin
            setChildCrossAxisPosition(child, dir, crossStart + crossLeadingMargin,
                                      childCrossLen, parentCrossSize, padCrossStart,
                                      padCrossEnd);

            mainPos += naturalMain[i] + infos[i].marginMain(isRow) + mainGap;
        }
    }

    // ====================== layoutFlexImpl (关键修正) ======================
    template <bool isRow>
    void layoutFlexImpl(auto &soaCtx, FlatLayoutTree &tree, int nodeIdx,
                        Constraints borderBC, const EdgeInsets &pad, AxisDirection dir,
                        CrossAxisAlignment crossAlign, MainAxisSize axisSize,
                        MainAxisAlignment mainAlign, std::optional<float> w,
                        std::optional<float> h)
    {
        FlatNode &node = tree.nodes[nodeIdx];
        bool isBoundedMain = mainAxisIsBounded(borderBC, isRow);
        bool isBoundedCross = crossAxisIsBounded(borderBC, isRow);

        // [FIX] 根据 Flutter 规范：交叉轴必须始终有界，否则直接报错
        if (!isBoundedCross)
            throw std::logic_error("Row/Column '" + node.name +
                                   "' has unbounded cross axis. Cross axis must be "
                                   "bounded in Flutter flex layout.");

        if (!isBoundedMain)
        {
            for (int c = node.firstChild; c != -1; c = tree.nodes[c].nextSibling)
                if (tree.nodes[c].ref.type_id ==
                    (uint32_t)std::remove_cvref_t<decltype(soaCtx)>::find_name(
                        "expandeds"))
                    throw std::logic_error(
                        "Row/Column '" + node.name +
                        "' has unbounded main axis and Expanded/Flexible child.");
        }

        float mainSize = mainAxisSizeFromConstraints(borderBC, isRow);
        float crossSize = crossAxisSizeFromConstraints(borderBC, isRow);
        float padMainStart = mainAxisPaddingStart(pad, dir);
        float padMainEnd = mainAxisPaddingEnd(pad, dir);
        float padCrossStart = crossAxisPaddingStart(pad, dir);
        float padCrossEnd = crossAxisPaddingEnd(pad, dir);
        float innerMain = mainSize - padMainStart - padMainEnd;
        if (!isBoundedMain)
            innerMain = Constraints::inf;
        float innerCross = crossSize - padCrossStart - padCrossEnd;

        float totalFlex = 0;
        auto infos = collectFlexChildren(soaCtx, tree, nodeIdx, isRow, totalFlex);
        auto naturalMain = layoutChildrenNaturally(
            soaCtx, tree, infos, isRow, isBoundedMain, innerMain, innerCross, crossAlign);

        float totalNatural = 0.0f;
        for (size_t i = 0; i < infos.size(); ++i)
        {
            if (infos[i].flex == 0)
                totalNatural += naturalMain[i] + infos[i].marginMain(isRow);
        }

        float finalInnerMain =
            computeFinalMainSize(isBoundedMain, totalNatural, innerMain, axisSize);
        float freeMain = std::max(0.0f, finalInnerMain - totalNatural);
        distributeFlexSpace(soaCtx, tree, infos, naturalMain, isRow, freeMain, totalFlex,
                            innerCross, crossAlign);

        float totalMainUsed = 0.0f;
        for (size_t i = 0; i < infos.size(); ++i)
            totalMainUsed += naturalMain[i] + infos[i].marginMain(isRow);

        float extraMain = std::max(0.0f, finalInnerMain - totalMainUsed);
        float mainGap = 0.0f, mainStartOff = 0.0f;
        computeMainGapAndStart(infos.size(), extraMain, mainAlign, mainGap, mainStartOff);

        positionChildrenInFlex(soaCtx, tree, infos, naturalMain, dir, isRow, innerMain,
                               innerCross, padMainStart, padMainEnd, padCrossStart,
                               padCrossEnd, crossAlign, mainGap, mainStartOff, mainSize,
                               crossSize);

        Size size;
        if (isRow)
        {
            size.width = finalInnerMain + pad.horizontal();
            size.height = innerCross + pad.vertical();
        }
        else
        {
            size.width = innerCross + pad.horizontal();
            size.height = finalInnerMain + pad.vertical();
        }
        if (w)
            size.width = *w;
        if (h)
            size.height = *h;
        size = borderBC.clamp(size);

        node.geometry = {0, 0, size.width, size.height};

        if (!infos.empty())
        {
            FlatNode &first = tree.nodes[infos[0].nodeIdx];
            node.baseline = isRow ? first.baseline + first.geometry.y
                                  : first.baseline + first.geometry.x;
        }
        else
        {
            node.baseline = isRow ? size.height : size.width;
        }
    }

} // namespace ui

using namespace ui;

// ====================== Container Trait（重写以符合 Flutter 规范）======================
struct ContainerStyleTrait : TraitBase<"containers">
{
    static constexpr bool has_single_child = true;

    template <typename SoaCtx>
    static Entity make(SoaCtx &soaCtx, std::optional<float> width = std::nullopt,
                       std::optional<float> height = std::nullopt, EdgeInsets margin = {},
                       EdgeInsets padding = {}, EdgeInsets border = {},
                       std::optional<Alignment> alignment = std::nullopt,
                       std::optional<Constraints> constraints = std::nullopt)
    {
        uint32_t idx = soaCtx.containers.new_entity(width, height, margin, padding,
                                                    border, alignment, constraints);
        return {type_id(soaCtx), idx, release(soaCtx)};
    }

    // [FIX] 完全按照 Flutter Container 布局算法重写
    static void layout(auto &soaCtx, auto &selfSoa, FlatLayoutTree &tree, int nodeIdx,
                       Constraints borderBC, const EdgeInsets &pad)
    {
        FlatNode &node = tree.nodes[nodeIdx];
        auto [border, ownConstraints, width, height, alignment] =
            selfSoa.template view_entity<"border", "constraints", "width", "height",
                                         "alignment">(0, node.ref.entity_id);

        if (ownConstraints)
            borderBC = borderBC.intersect(*ownConstraints);

        // 1. 无子组件：尽可能大（或显式尺寸）
        if (node.firstChild == -1)
        {
            float w = width.has_value()
                          ? *width
                          : (borderBC.hasBoundedWidth() ? borderBC.maxW : 0.0f);
            float h = height.has_value()
                          ? *height
                          : (borderBC.hasBoundedHeight() ? borderBC.maxH : 0.0f);
            Size size = borderBC.clamp({w, h});
            node.geometry = {0, 0, size.width, size.height};
            node.baseline = size.height;
            return;
        }

        // 2. 有子组件：必须唯一
        int childIdx = node.firstChild;
        if (tree.nodes[childIdx].nextSibling != -1)
            throw std::logic_error("Container '" + node.name +
                                   "' must have exactly one child.");

        FlatNode &child = tree.nodes[childIdx];
        EdgeInsets childMargin =
            do_get_member<"margin">(soaCtx, child.ref).miss_return(EdgeInsets{});

        // 计算内部约束（去除 border 和 container 自己的 padding）
        Constraints innerBC = borderBC.deflate(border).deflate(pad);

        if (alignment.has_value())
        {
            // --- 有 alignment：容器尺寸优先看显式宽高，否则尝试填满父约束（有界时），最后被子组件撑开 ---

            // 1. 先布局子组件，使用 loose 约束（内部扣除 margin）
            Constraints childBC = makeLooseConstraints(innerBC);
            childBC = childBC.deflate(childMargin);
            soaCtx.layout(soaCtx, tree, childIdx, childBC);

            Size childFull = childFullSize(soaCtx, child);

            // 2. 确定容器最终尺寸
            // 使用 innerBC（已扣除 padding 和 border），而非 borderBC
            float containerW =
                width.has_value()
                    ? *width
                    : (innerBC.hasBoundedWidth()
                           ? innerBC.maxW
                           : childFull.width + border.horizontal() + pad.horizontal());
            float containerH =
                height.has_value()
                    ? *height
                    : (innerBC.hasBoundedHeight()
                           ? innerBC.maxH
                           : childFull.height + border.vertical() + pad.vertical());

            Size containerSize = borderBC.clamp({containerW, containerH});

            // 3. 根据 alignment 定位子组件（使用容器的实际尺寸作为对齐参考空间）
            Constraints containerAsConstraints{0, containerSize.width, 0,
                                               containerSize.height};
            positionChildByAlignment(soaCtx, child, containerAsConstraints, pad,
                                     alignment, border);

            // 4. 设置当前节点几何
            node.geometry = {0, 0, containerSize.width, containerSize.height};
            node.baseline = child.baseline + child.geometry.y;
        }
        else
        {
            // --- 无 alignment：容器尺寸由子组件决定，子组件接收 loose 约束 ---
            Constraints childBC = makeLooseConstraints(innerBC).deflate(childMargin);
            soaCtx.layout(soaCtx, tree, childIdx, childBC);

            Size childFull = childFullSize(soaCtx, child);
            float containerW =
                width.value_or(childFull.width + border.horizontal() + pad.horizontal());
            float containerH =
                height.value_or(childFull.height + border.vertical() + pad.vertical());
            Size containerSize = borderBC.clamp({containerW, containerH});

            // 子组件定位到左上角（默认无 alignment 行为）
            child.geometry.x = border.left + pad.left + childMargin.left;
            child.geometry.y = border.top + pad.top + childMargin.top;

            node.geometry = {0, 0, containerSize.width, containerSize.height};
            node.baseline = child.baseline + child.geometry.y;
        }
    }
};
using ContainerStyleObject =
    gen_soa_struct<ContainerStyleTrait, {"width", ^^std::optional<float>},
                   {"height", ^^std::optional<float>}, {"margin", ^^EdgeInsets},
                   {"padding", ^^EdgeInsets}, {"border", ^^EdgeInsets},
                   {"alignment", ^^std::optional<Alignment>},
                   {"constraints", ^^std::optional<Constraints>}>;

// ====================== Row / Column / Expanded / Text Trait ======================
struct RowStyleTrait : TraitBase<"rows">
{
    static void layout(auto &soaCtx, auto &selfSoa, FlatLayoutTree &tree, int nodeIdx,
                       Constraints borderBC, const EdgeInsets &pad)
    {
        auto &ref = tree.nodes[nodeIdx].ref;
        auto [td, crossAlign, axisSize, mainAlign, w, h] =
            selfSoa.template view_entity<"textDirection", "crossAlign", "mainAxisSize",
                                         "mainAlign", "width", "height">(0,
                                                                         ref.entity_id);
        AxisDirection dir = axisDirectionForFlex(true, td, VerticalDirection::down);
        layoutFlexImpl<true>(soaCtx, tree, nodeIdx, borderBC, pad, dir, crossAlign,
                             axisSize, mainAlign, w, h);
    }

    static Entity make(auto &soaCtx, std::optional<float> width = std::nullopt,
                       std::optional<float> height = std::nullopt, EdgeInsets margin = {},
                       EdgeInsets padding = {},
                       MainAxisAlignment mainAlign = MainAxisAlignment::start,
                       CrossAxisAlignment crossAlign = CrossAxisAlignment::start,
                       MainAxisSize mainAxisSize = MainAxisSize::max,
                       TextDirection textDirection = TextDirection::ltr)
    {
        uint32_t idx = soaCtx.rows.new_entity(width, height, margin, padding, mainAlign,
                                              crossAlign, mainAxisSize, textDirection);
        return {type_id(soaCtx), idx, release(soaCtx)};
    }
};
using RowStyleObject =
    gen_soa_struct<RowStyleTrait, {"width", ^^std::optional<float>},
                   {"height", ^^std::optional<float>}, {"margin", ^^EdgeInsets},
                   {"padding", ^^EdgeInsets}, {"mainAlign", ^^MainAxisAlignment},
                   {"crossAlign", ^^CrossAxisAlignment}, {"mainAxisSize", ^^MainAxisSize},
                   {"textDirection", ^^TextDirection}>;

struct ColumnStyleTrait : TraitBase<"columns">
{
    static void layout(auto &soaCtx, auto &selfSoa, FlatLayoutTree &tree, int nodeIdx,
                       Constraints borderBC, const EdgeInsets &pad)
    {
        auto &ref = tree.nodes[nodeIdx].ref;
        auto [vd, crossAlign, axisSize, mainAlign, w, h] =
            selfSoa.template view_entity<"verticalDirection", "crossAlign",
                                         "mainAxisSize", "mainAlign", "width", "height">(
                0, ref.entity_id);
        AxisDirection dir = axisDirectionForFlex(false, TextDirection::ltr, vd);
        layoutFlexImpl<false>(soaCtx, tree, nodeIdx, borderBC, pad, dir, crossAlign,
                              axisSize, mainAlign, w, h);
    }
    static Entity make(auto &soaCtx, std::optional<float> width = std::nullopt,
                       std::optional<float> height = std::nullopt, EdgeInsets margin = {},
                       EdgeInsets padding = {},
                       MainAxisAlignment mainAlign = MainAxisAlignment::start,
                       CrossAxisAlignment crossAlign = CrossAxisAlignment::start,
                       MainAxisSize mainAxisSize = MainAxisSize::max,
                       VerticalDirection verticalDirection = VerticalDirection::down)
    {
        uint32_t idx =
            soaCtx.columns.new_entity(width, height, margin, padding, mainAlign,
                                      crossAlign, mainAxisSize, verticalDirection);
        return {type_id(soaCtx), idx, release(soaCtx)};
    }
};
using ColumnStyleObject =
    gen_soa_struct<ColumnStyleTrait, {"width", ^^std::optional<float>},
                   {"height", ^^std::optional<float>}, {"margin", ^^EdgeInsets},
                   {"padding", ^^EdgeInsets}, {"mainAlign", ^^MainAxisAlignment},
                   {"crossAlign", ^^CrossAxisAlignment}, {"mainAxisSize", ^^MainAxisSize},
                   {"verticalDirection", ^^VerticalDirection}>;

// [FIX] Expanded 扩展为支持 FlexFit（可表达 Expanded 和 Flexible）
struct ExpandedStyleTrait : TraitBase<"expandeds">
{
    static constexpr bool has_single_child = true; // 强制单子节点

    static Entity make(auto &soaCtx, int flex = 1,
                       FlexFit fit = FlexFit::tight) // [NEW] 增加 flexFit 参数
    {
        uint32_t idx = soaCtx.expandeds.new_entity(flex, fit);
        return {type_id(soaCtx), idx, release(soaCtx)};
    }
};
using ExpandedStyleObject =
    gen_soa_struct<ExpandedStyleTrait, {"flex", ^^int},
                   {"flexFit", ^^FlexFit}>; // [NEW] 添加 flexFit 字段

struct TextStyleTrait : TraitBase<"texts">
{
    static Entity make(auto &soaCtx, std::optional<float> w = std::nullopt,
                       std::optional<float> h = std::nullopt, EdgeInsets m = {},
                       EdgeInsets p = {})
    {
        uint32_t idx = soaCtx.texts.new_entity(w, h, m, p);
        return {type_id(soaCtx), idx, release(soaCtx)};
    }

    static void layout(auto &soaCtx, auto &selfSoa, FlatLayoutTree &tree, int nodeIdx,
                       Constraints borderBC, const EdgeInsets & /*pad*/)
    {
        FlatNode &node = tree.nodes[nodeIdx];
        if (node.firstChild != -1)
            throw std::logic_error("Text widget '" + node.name +
                                   "' cannot have children.");

        auto [width, height] =
            selfSoa.template view_entity<"width", "height">(0, node.ref.entity_id);
        float w = width.value_or(80.0f);
        float h = height.value_or(40.0f);
        Size size = borderBC.clamp({w, h});
        node.geometry = {0, 0, size.width, size.height};
        node.baseline = size.height * 0.8f; // 测试用，实际需真实字体度量
    }
};
using TextStyleObject =
    gen_soa_struct<TextStyleTrait, {"width", ^^std::optional<float>},
                   {"height", ^^std::optional<float>}, {"margin", ^^EdgeInsets},
                   {"padding", ^^EdgeInsets}>;

// ====================== SOA 上下文初始化 ======================
constexpr auto initSoaData()
{
    ContainerStyleObject containers{100};
    RowStyleObject rows{100};
    ColumnStyleObject columns{100};
    ExpandedStyleObject expandeds{100};
    TextStyleObject texts{100};

    constexpr auto get_member =
        []<static_string member_name>(auto &soaCtx, const Entity &ref) -> RefAny {
        constexpr auto members = std::remove_cvref_t<decltype(soaCtx)>::members;
        template for (constexpr auto I : std::ranges::views::indices(members.size()))
        {
            using MemberType = std::remove_cvref_t<decltype(soaCtx.[:members[I]:])>;
            if constexpr (requires { typename MemberType::trait_type; })
            {
                if constexpr (MemberType::has_member(member_name))
                {
                    if (I == (size_t)ref.type_id)
                    {
                        auto [m] = soaCtx.[:members[I]:]
                            .template view_entity<member_name>(0, ref.entity_id);
                        using Ref = decltype(m);
                        static_assert(std::is_reference_v<Ref>, "should be reference");
                        return RefAny{m};
                    }
                }
            }
        }
        return RefAny{};
    };

    constexpr auto layout = [](auto &soaCtx, FlatLayoutTree &tree, int nodeIdx,
                               Constraints constraints) {
        FlatNode &node = tree.nodes[nodeIdx];
        const auto &ref = node.ref;
        if (ref.type_id == ExpandedStyleTrait::type_id(soaCtx))
            throw std::logic_error("Expanded/Flexible widget must be placed directly "
                                   "inside Row/Column/Flex.");

        Constraints borderBC = constraints;
        if (auto w = do_get_member<"width">(soaCtx, ref).value_or(std::optional<float>{}))
            borderBC = borderBC.applyFixedWidth(*w);
        if (auto h =
                do_get_member<"height">(soaCtx, ref).value_or(std::optional<float>{}))
            borderBC = borderBC.applyFixedHeight(*h);
        if (auto ownConstraints = do_get_member<"constraints">(soaCtx, ref)
                                      .value_or(std::optional<Constraints>{}))
            borderBC = borderBC.intersect(*ownConstraints);

        EdgeInsets padding =
            do_get_member<"padding">(soaCtx, ref).miss_return(EdgeInsets{});
        bool dispatched = false;
        constexpr auto members = std::remove_cvref_t<decltype(soaCtx)>::members;
        template for (constexpr auto I : std::ranges::views::indices(members.size()))
        {
            using MemberType = std::remove_cvref_t<decltype(soaCtx.[:members[I]:])>;
            if constexpr (requires {
                              typename MemberType::trait_type;
                              MemberType::trait_type::layout(
                                  soaCtx, soaCtx.[:members[I]:], tree, nodeIdx, borderBC,
                                                               padding);
                          })
            {
                if (size_t(ref.type_id) == I)
                {
                    MemberType::trait_type::layout(
                        soaCtx, soaCtx.[:members[I]:], tree, nodeIdx, borderBC, padding);
                    dispatched = true;
                }
            }
        }
        if (!dispatched)
            throw std::logic_error("Unhandled WidgetKind in layout");

        Size finalSize{node.geometry.w, node.geometry.h};
        if (finalSize.width > constraints.maxW + 1e-6f ||
            finalSize.height > constraints.maxH + 1e-6f)
        {
            std::cerr << "WARNING: Widget '" << node.name << "' overflows parent by ("
                      << finalSize.width - constraints.maxW << ", "
                      << finalSize.height - constraints.maxH << ")\n";
        }
    };

    return make_aggregate<"soaData", ContainerStyleObject::trait_type::name,
                          RowStyleObject::trait_type::name,
                          ColumnStyleObject::trait_type::name,
                          ExpandedStyleObject::trait_type::name,
                          TextStyleObject::trait_type::name, "get_member", "layout">(
        std::move(containers), std::move(rows), std::move(columns), std::move(expandeds),
        std::move(texts), std::move(get_member), std::move(layout));
}

// ====================== 测试辅助 ======================
bool approx(float a, float b, float eps = 1e-3f)
{
    return std::abs(a - b) < eps;
}

bool verifyGeometry(const FlatNode &node, float x, float y, float w, float h)
{
    return approx(node.geometry.x, x) && approx(node.geometry.y, y) &&
           approx(node.geometry.w, w) && approx(node.geometry.h, h);
}

bool checkGeometry(const FlatNode &node, float x, float y, float w, float h,
                   const char *label)
{
    if (!verifyGeometry(node, x, y, w, h))
    {
        std::cerr << "FAIL: " << label << " expected (" << x << "," << y << "," << w
                  << "," << h << ") got (" << node.geometry.x << "," << node.geometry.y
                  << "," << node.geometry.w << "," << node.geometry.h << ")\n";
        return false;
    }
    return true;
}

bool tryLayout(auto &soaCtx, FlatLayoutTree &tree, int rootIdx, Constraints c,
               const std::string &expectedError = "")
{
    try
    {
        c = c.deflate(do_get_member<"margin">(soaCtx, tree.nodes[rootIdx].ref)
                          .miss_return(EdgeInsets{}));
        soaCtx.layout(soaCtx, tree, rootIdx, c);
        if (!expectedError.empty())
        {
            std::cerr << "FAIL: expected exception but none thrown\n";
            return false;
        }
        return true;
    }
    catch (const std::logic_error &e)
    {
        if (expectedError.empty())
        {
            std::cerr << "FAIL: unexpected exception: " << e.what() << "\n";
            return false;
        }
        if (std::string(e.what()).find(expectedError) == std::string::npos)
        {
            std::cerr << "FAIL: expected '" << expectedError << "' but got '" << e.what()
                      << "'\n";
            return false;
        }
        return true;
    }
}

// ====================== 测试用例（适配新行为）======================
bool test_basic_container_fixed()
{
    auto soaCtx = initSoaData();
    FlatLayoutTree tree;
    tree.reserve(10);
    int root = tree.addNode(
        "root", ContainerStyleTrait::make(soaCtx, 100, 100, {}, {}, {}, Align::topLeft),
        -1);
    int child = tree.addNode(
        "child", ContainerStyleTrait::make(soaCtx, 50, 50, {5, 5, 5, 5}), root);
    if (!tryLayout(soaCtx, tree, root, {0, 800, 0, 600}))
        return false;
    return checkGeometry(tree.nodes[root], 0, 0, 100, 100, "root") &&
           checkGeometry(tree.nodes[child], 5, 5, 50, 50, "child");
}

bool test_row_fixed_children()
{
    auto soaCtx = initSoaData();
    FlatLayoutTree tree;
    tree.reserve(10);
    int row = tree.addNode("row", RowStyleTrait::make(soaCtx, 300, 100), -1);
    int c1 = tree.addNode("c1", ContainerStyleTrait::make(soaCtx, 50, 30), row);
    if (!tryLayout(soaCtx, tree, row, {0, 500, 0, 200}))
        return false;
    return checkGeometry(tree.nodes[row], 0, 0, 300, 100, "row") &&
           checkGeometry(tree.nodes[c1], 0, 0, 50, 30, "c1");
}

bool test_expanded_in_row()
{
    auto soaCtx = initSoaData();
    FlatLayoutTree tree;
    tree.reserve(10);
    int row = tree.addNode("row", RowStyleTrait::make(soaCtx, 300, 100), -1);
    int c1 = tree.addNode("c1", ContainerStyleTrait::make(soaCtx, 50, 30), row);
    int exp = tree.addNode("exp", ExpandedStyleTrait::make(soaCtx, 2), row);
    int c2 = tree.addNode("c2", ContainerStyleTrait::make(soaCtx, std::nullopt, 20), exp);
    int c3 = tree.addNode("c3", ContainerStyleTrait::make(soaCtx, 70, 40), row);
    if (!tryLayout(soaCtx, tree, row, {0, 300, 0, 100}))
        return false;
    return checkGeometry(tree.nodes[c1], 0, 0, 50, 30, "c1") &&
           checkGeometry(tree.nodes[c2], 50, 0, 180, 20, "c2 expanded") &&
           checkGeometry(tree.nodes[c3], 230, 0, 70, 40, "c3");
}

bool test_stretch_overrides_height()
{
    auto soaCtx = initSoaData();
    FlatLayoutTree tree;
    tree.reserve(10);
    int row = tree.addNode("row",
                           RowStyleTrait::make(soaCtx, 300, 100, {}, {},
                                               MainAxisAlignment::start,
                                               CrossAxisAlignment::stretch),
                           -1);
    int c1 = tree.addNode("c1", ContainerStyleTrait::make(soaCtx, 50, std::nullopt), row);
    int c2 = tree.addNode("c2", ContainerStyleTrait::make(soaCtx, 70, 40), row);
    if (!tryLayout(soaCtx, tree, row, {0, 300, 0, 100}))
        return false;
    return checkGeometry(tree.nodes[c1], 0, 0, 50, 100, "c1 stretched") &&
           checkGeometry(tree.nodes[c2], 50, 0, 70, 100, "c2 forced");
}

bool test_column_min_shrink()
{
    auto soaCtx = initSoaData();
    FlatLayoutTree tree;
    tree.reserve(10);
    int col =
        tree.addNode("col",
                     ColumnStyleTrait::make(soaCtx, 200, std::nullopt, {}, {},
                                            MainAxisAlignment::start,
                                            CrossAxisAlignment::start, MainAxisSize::min),
                     -1);
    tree.addNode("c1", ContainerStyleTrait::make(soaCtx, 100, 30), col);
    tree.addNode("c2", ContainerStyleTrait::make(soaCtx, 80, 20), col);
    if (!tryLayout(soaCtx, tree, col, {0, 200, 0, Constraints::inf}))
        return false;
    return approx(tree.nodes[col].geometry.h, 50);
}

bool test_rtl_row()
{
    auto soaCtx = initSoaData();
    FlatLayoutTree tree;
    tree.reserve(10);
    int row = tree.addNode("row",
                           RowStyleTrait::make(soaCtx, 300, 100, {}, {},
                                               MainAxisAlignment::start,
                                               CrossAxisAlignment::start,
                                               MainAxisSize::max, TextDirection::rtl),
                           -1);
    int c1 = tree.addNode("c1", ContainerStyleTrait::make(soaCtx, 50, 30), row);
    int c2 = tree.addNode("c2", ContainerStyleTrait::make(soaCtx, 70, 40), row);
    if (!tryLayout(soaCtx, tree, row, {0, 300, 0, 100}))
        return false;
    return checkGeometry(tree.nodes[c1], 250, 0, 50, 30, "c1 right") &&
           checkGeometry(tree.nodes[c2], 180, 0, 70, 40, "c2 left");
}

bool test_up_column()
{
    auto soaCtx = initSoaData();
    FlatLayoutTree tree;
    tree.reserve(10);
    int col = tree.addNode(
        "col",
        ColumnStyleTrait::make(soaCtx, 100, 200, {}, {}, MainAxisAlignment::start,
                               CrossAxisAlignment::start, MainAxisSize::max,
                               VerticalDirection::up),
        -1);
    int c1 = tree.addNode("c1", ContainerStyleTrait::make(soaCtx, 50, 30), col);
    int c2 = tree.addNode("c2", ContainerStyleTrait::make(soaCtx, 50, 40), col);
    if (!tryLayout(soaCtx, tree, col, {0, 100, 0, 200}))
        return false;
    return checkGeometry(tree.nodes[c1], 0, 170, 50, 30, "c1 bottom") &&
           checkGeometry(tree.nodes[c2], 0, 130, 50, 40, "c2 above");
}

bool test_baseline_alignment()
{
    auto soaCtx = initSoaData();
    FlatLayoutTree tree;
    tree.reserve(10);
    int row = tree.addNode("row",
                           RowStyleTrait::make(soaCtx, 300, 100, {}, {},
                                               MainAxisAlignment::start,
                                               CrossAxisAlignment::baseline),
                           -1);
    int t1 = tree.addNode("t1", TextStyleTrait::make(soaCtx, std::nullopt, 40), row);
    int t2 = tree.addNode("t2", TextStyleTrait::make(soaCtx, std::nullopt, 20), row);
    if (!tryLayout(soaCtx, tree, row, {0, 300, 0, 100}))
        return false;
    return checkGeometry(tree.nodes[t1], 0, 0, 80, 40, "t1 y=0") &&
           checkGeometry(tree.nodes[t2], 80, 16, 80, 20, "t2 y=16");
}

bool test_expanded_outside_flex_throws()
{
    auto soaCtx = initSoaData();
    FlatLayoutTree tree;
    tree.reserve(10);
    int cont = tree.addNode("cont", ContainerStyleTrait::make(soaCtx, 200, 200), -1);
    int exp = tree.addNode("exp", ExpandedStyleTrait::make(soaCtx), cont);
    tree.addNode("inner", ContainerStyleTrait::make(soaCtx, 50, 50), exp);
    return tryLayout(soaCtx, tree, cont, {0, 200, 0, 200}, "Expanded");
}

bool test_nested_expanded()
{
    auto soaCtx = initSoaData();
    FlatLayoutTree tree;
    tree.reserve(20);
    int row = tree.addNode("row", RowStyleTrait::make(soaCtx, 300, 200), -1);
    int fixed = tree.addNode("fixed", ContainerStyleTrait::make(soaCtx, 50, 50), row);
    int expRow = tree.addNode("expRow", ExpandedStyleTrait::make(soaCtx, 1), row);
    int col = tree.addNode("col",
                           ColumnStyleTrait::make(soaCtx, std::nullopt, std::nullopt, {},
                                                  {}, MainAxisAlignment::start,
                                                  CrossAxisAlignment::stretch),
                           expRow);
    int colFixed =
        tree.addNode("colFixed", ContainerStyleTrait::make(soaCtx, 80, 30), col);
    int expCol = tree.addNode("expCol", ExpandedStyleTrait::make(soaCtx, 1), col);
    int inner = tree.addNode(
        "inner", ContainerStyleTrait::make(soaCtx, std::nullopt, std::nullopt), expCol);
    if (!tryLayout(soaCtx, tree, row, {0, 300, 0, 200}))
        return false;
    return checkGeometry(tree.nodes[fixed], 0, 0, 50, 50, "fixed") &&
           checkGeometry(tree.nodes[colFixed], 0, 0, 250, 30, "colFixed") &&
           checkGeometry(tree.nodes[inner], 0, 30, 250, 170, "inner expanded");
}

// ====================== DSL 辅助（全局）======================
constexpr void pre_single_child_check(auto &soaCtx, const FlatNode &node)
{
    constexpr auto members = std::remove_cvref_t<decltype(soaCtx)>::members;
    template for (constexpr auto I : std::ranges::views::indices(members.size()))
    {
        using MemberType = std::remove_cvref_t<decltype(soaCtx.[:members[I]:])>;
        if constexpr (requires {
                          typename MemberType::trait_type;
                          MemberType::trait_type::has_single_child;
                      })
        {
            if constexpr (MemberType::trait_type::has_single_child)
            {
                if (node.ref.type_id == I && node.firstChild != -1)
                    throw std::logic_error("this type'" + node.name +
                                           "' can only have one child.");
            }
        }
    }
}
// dsl_container 增加 border 和 constraints 参数
inline int dsl_container(auto &soaCtx, FlatLayoutTree &tree, int parentIdx,
                         const std::string &name, std::optional<float> w = {},
                         std::optional<float> h = {}, EdgeInsets margin = {},
                         EdgeInsets padding = {}, EdgeInsets border = {},
                         std::optional<Alignment> align = {},
                         std::optional<Constraints> constraints = {})
{
    if (parentIdx != -1)
        pre_single_child_check(soaCtx, tree.getNode(parentIdx));
    auto entity = ContainerStyleTrait::make(soaCtx, w, h, margin, padding, border, align,
                                            constraints);
    return tree.addNode(name, std::move(entity), parentIdx);
}

// dsl_row 增加 axisSize 和 textDir 参数
inline int dsl_row(auto &soaCtx, FlatLayoutTree &tree, int parentIdx,
                   const std::string &name, std::optional<float> w = {},
                   std::optional<float> h = {},
                   MainAxisAlignment ma = MainAxisAlignment::start,
                   CrossAxisAlignment ca = CrossAxisAlignment::start,
                   EdgeInsets margin = {}, EdgeInsets padding = {},
                   MainAxisSize axisSize = MainAxisSize::max,
                   TextDirection textDir = TextDirection::ltr)
{
    if (parentIdx != -1)
        pre_single_child_check(soaCtx, tree.getNode(parentIdx));
    auto entity =
        RowStyleTrait::make(soaCtx, w, h, margin, padding, ma, ca, axisSize, textDir);
    return tree.addNode(name, std::move(entity), parentIdx);
}

// dsl_column 增加 axisSize 和 vertDir 参数
inline int dsl_column(auto &soaCtx, FlatLayoutTree &tree, int parentIdx,
                      const std::string &name, std::optional<float> w = {},
                      std::optional<float> h = {},
                      MainAxisAlignment ma = MainAxisAlignment::start,
                      CrossAxisAlignment ca = CrossAxisAlignment::start,
                      EdgeInsets margin = {}, EdgeInsets padding = {},
                      MainAxisSize axisSize = MainAxisSize::max,
                      VerticalDirection vertDir = VerticalDirection::down)
{
    if (parentIdx != -1)
        pre_single_child_check(soaCtx, tree.getNode(parentIdx));
    auto entity =
        ColumnStyleTrait::make(soaCtx, w, h, margin, padding, ma, ca, axisSize, vertDir);
    return tree.addNode(name, std::move(entity), parentIdx);
}

inline int dsl_expanded(auto &soaCtx, FlatLayoutTree &tree, int parentIdx,
                        const std::string &name, int flex = 1,
                        FlexFit fit = FlexFit::tight)
{
    if (parentIdx != -1)
        pre_single_child_check(soaCtx, tree.getNode(parentIdx));
    auto entity = ExpandedStyleTrait::make(soaCtx, flex, fit);
    return tree.addNode(name, std::move(entity), parentIdx);
}

inline int dsl_text(auto &soaCtx, FlatLayoutTree &tree, int parentIdx,
                    const std::string &name, std::optional<float> w = {},
                    std::optional<float> h = {}, EdgeInsets margin = {},
                    EdgeInsets padding = {})
{
    if (parentIdx != -1)
        pre_single_child_check(soaCtx, tree.getNode(parentIdx));
    auto entity = TextStyleTrait::make(soaCtx, w, h, margin, padding);
    return tree.addNode(name, std::move(entity), parentIdx);
}

inline void dsl_layout(auto &soaCtx, FlatLayoutTree &tree, Constraints c)
{
    if (!tree.nodes.empty())
        soaCtx.layout(soaCtx, tree, 0, c);
}

// ====================== 新增测试用例 ======================
bool test_dsl_simple_layout()
{
    auto soaCtx = initSoaData();
    FlatLayoutTree tree;
    tree.reserve(10);

    int rowIdx = dsl_row(soaCtx, tree, -1, "mainRow", 300, 100);
    dsl_container(soaCtx, tree, rowIdx, "c1", 50, 30);
    dsl_container(soaCtx, tree, rowIdx, "c2", 70, 40);

    Constraints c = {0, 500, 0, 200};
    soaCtx.layout(soaCtx, tree, 0, c);

    bool ok = true;
    ok &= checkGeometry(tree.nodes[0], 0, 0, 300, 100, "row");
    ok &= checkGeometry(tree.nodes[1], 0, 0, 50, 30, "c1");
    ok &= checkGeometry(tree.nodes[2], 50, 0, 70, 40, "c2");
    return ok;
}

struct AppState
{
    int count = 0;
    bool showExtra = false;
};

void buildApp(auto &soaCtx, FlatLayoutTree &tree, const AppState &state)
{
    tree.reserve(20);
    int root = dsl_container(soaCtx, tree, -1, "app", 400, 300);
    int mainColumn =
        dsl_column(soaCtx, tree, root, "mainColumn", std::nullopt, std::nullopt);
    int row = dsl_row(soaCtx, tree, mainColumn, "header", 400, 50);
    dsl_text(soaCtx, tree, row, "title", std::nullopt, 30);
    if (state.showExtra)
        dsl_text(soaCtx, tree, row, "extra", std::nullopt, 20);
    dsl_text(soaCtx, tree, mainColumn, "count_label", std::nullopt, 40);
}

bool test_reactive_update()
{
    auto soaCtx = initSoaData();
    Constraints screen = {0, 400, 0, 300};
    AppState state;

    {
        FlatLayoutTree tree;
        buildApp(soaCtx, tree, state);
        soaCtx.layout(soaCtx, tree, 0, screen);
        bool hasTitle = false, hasExtra = false;
        for (auto &node : tree.nodes)
        {
            if (node.name == "title")
                hasTitle = true;
            if (node.name == "extra")
                hasExtra = true;
        }
        if (!hasTitle || hasExtra)
            return false;
    }

    state.showExtra = true;
    {
        FlatLayoutTree tree;
        buildApp(soaCtx, tree, state);
        soaCtx.layout(soaCtx, tree, 0, screen);
        bool hasExtra = false;
        for (auto &node : tree.nodes)
            if (node.name == "extra")
                hasExtra = true;
        if (!hasExtra)
            return false;
    }
    return true;
}

bool test_dynamic_add_remove()
{
    auto soaCtx = initSoaData();
    Constraints c = {0, 200, 0, 200};

    {
        FlatLayoutTree tree;
        tree.reserve(10);
        int col = dsl_column(soaCtx, tree, -1, "col", 200, 200);
        dsl_container(soaCtx, tree, col, "child1", 100, 50);
        soaCtx.layout(soaCtx, tree, 0, c);
        bool ok = false;
        for (auto &node : tree.nodes)
            if (node.name == "child1")
                ok = true;
        if (!ok)
            return false;
    }

    {
        FlatLayoutTree tree;
        tree.reserve(10);
        int col = dsl_column(soaCtx, tree, -1, "col", 200, 200);
        dsl_container(soaCtx, tree, col, "child2", 80, 60);
        soaCtx.layout(soaCtx, tree, 0, c);
        bool hasChild2 = false, hasChild1 = false;
        for (auto &node : tree.nodes)
        {
            if (node.name == "child2")
                hasChild2 = true;
            if (node.name == "child1")
                hasChild1 = true;
        }
        if (!hasChild2 || hasChild1)
            return false;
    }
    return true;
}

bool test_resource_cleanup()
{
    {
        auto soaCtx = initSoaData();
        {
            FlatLayoutTree tree;
            tree.reserve(10);
            int root = dsl_container(soaCtx, tree, -1, "root", 200, 200);
            int col = dsl_column(soaCtx, tree, root, "col", std::nullopt, std::nullopt);
            dsl_container(soaCtx, tree, col, "c1", 50, 50);
            dsl_text(soaCtx, tree, col, "t1", std::nullopt, 30);
            soaCtx.layout(soaCtx, tree, 0, {0, 200, 0, 200});
        }
    }

    auto soaCtx2 = initSoaData();
    bool ok = true;
    ok &= (soaCtx2.containers.size() == 0);
    ok &= (soaCtx2.rows.size() == 0);
    ok &= (soaCtx2.columns.size() == 0);
    ok &= (soaCtx2.expandeds.size() == 0);
    ok &= (soaCtx2.texts.size() == 0);
    return ok;
}

bool test_dsl_nested_expanded()
{
    auto soaCtx = initSoaData();
    FlatLayoutTree tree;
    tree.reserve(20);

    int row = dsl_row(soaCtx, tree, -1, "row", 300, 200);
    dsl_container(soaCtx, tree, row, "fixed", 50, 50);
    int expRow = dsl_expanded(soaCtx, tree, row, "expRow", 1);
    int col = dsl_column(soaCtx, tree, expRow, "col", std::nullopt, std::nullopt,
                         MainAxisAlignment::start, CrossAxisAlignment::stretch);
    dsl_container(soaCtx, tree, col, "colFixed", 80, 30);
    int expCol = dsl_expanded(soaCtx, tree, col, "expCol", 1);
    dsl_container(soaCtx, tree, expCol, "inner", std::nullopt, std::nullopt);

    Constraints c = {0, 300, 0, 200};
    soaCtx.layout(soaCtx, tree, 0, c);

    auto findNode = [&](const std::string &name) -> const FlatNode * {
        for (auto &n : tree.nodes)
            if (n.name == name)
                return &n;
        return nullptr;
    };
    const FlatNode *colFixed = findNode("colFixed");
    const FlatNode *inner = findNode("inner");
    bool ok = true;
    ok &= (colFixed != nullptr);
    ok &= (inner != nullptr);
    if (colFixed)
        ok &= checkGeometry(*colFixed, 0, 0, 250, 30, "colFixed");
    if (inner)
        ok &= checkGeometry(*inner, 0, 30, 250, 170, "inner expanded");
    return ok;
}

bool test_container_single_child_constraint()
{
    auto soaCtx = initSoaData();
    FlatLayoutTree tree;
    tree.reserve(10);
    int root = dsl_container(soaCtx, tree, -1, "root", 200, 200);
    dsl_text(soaCtx, tree, root, "first_child", 30, 30);
    bool caught = false;
    try
    {
        dsl_text(soaCtx, tree, root, "second_child", 30, 30);
    }
    catch (const std::logic_error &e)
    {
        caught = true;
    }
    return caught;
}

// ====================== 通用声明式 UIBuilder（新增） ======================
template <typename SoaCtx>
class UIBuilder
{
    SoaCtx &soaCtx_;
    FlatLayoutTree &tree_;
    int currentParent_ = -1;
    std::vector<int> parentStack_;

    void pushParent(int idx)
    {
        parentStack_.push_back(currentParent_);
        currentParent_ = idx;
    }
    void popParent()
    {
        currentParent_ = parentStack_.back();
        parentStack_.pop_back();
    }

    int addNode(const std::string &name, Entity entity)
    {
        if (currentParent_ != -1)
            pre_single_child_check(soaCtx_, tree_.getNode(currentParent_));
        return tree_.addNode(name, std::move(entity), currentParent_);
    }

  public:
    UIBuilder(SoaCtx &ctx, FlatLayoutTree &tree) : soaCtx_{ctx}, tree_{tree} {}

    int lastNodeIdx() const noexcept
    {
        return static_cast<int>(tree_.nodes.size() - 1);
    }

    // 叶子版本（无子节点）
    template <typename TraitType, typename... Args>
        requires(
            sizeof...(Args) == 0 ||
            !std::invocable<std::decay_t<std::tuple_element_t<0, std::tuple<Args...>>>,
                            UIBuilder &>)
    UIBuilder &Add(const std::string &name, Args &&...args)
    {
        auto entity = TraitType::make(soaCtx_, std::forward<Args>(args)...);
        addNode(name, std::move(entity));
        return *this;
    }

    // 容器版本（有子节点）
    template <typename TraitType, typename BuildFn, typename... Args>
        requires std::invocable<std::decay_t<BuildFn>, UIBuilder &>
    UIBuilder &Add(const std::string &name, BuildFn &&buildFn, Args &&...args)
    {
        int idx = Add<TraitType>(name, std::forward<Args>(args)...).lastNodeIdx();
        pushParent(idx);
        std::invoke(std::forward<BuildFn>(buildFn), *this);
        popParent();
        return *this;
    }

    template <typename TraitType = ContainerStyleTrait, typename BuildFn,
              typename... Args>
    void Root(const std::string &name, BuildFn &&buildFn, Args &&...args)
    {
        currentParent_ = -1;
        int idx = Add<TraitType>(name, std::forward<Args>(args)...).lastNodeIdx();
        pushParent(idx);
        std::invoke(std::forward<BuildFn>(buildFn), *this);
        popParent();
    }

    UIBuilder &If(bool condition, std::function<void(UIBuilder &)> build)
    {
        if (condition)
            build(*this);
        return *this;
    }
};

// ====================== 新版通用 Builder 测试 ======================

bool test_generic_builder_simple()
{
    auto soaCtx = initSoaData();
    FlatLayoutTree tree;
    UIBuilder b(soaCtx, tree);

    b.Root<RowStyleTrait>(
        "mainRow",
        [](auto &b) {
            b.template Add<ContainerStyleTrait>("c1", 50, 30);
            b.template Add<ContainerStyleTrait>("c2", 70, 40);
        },
        300, 100);

    soaCtx.layout(soaCtx, tree, 0, {0, 500, 0, 200});

    return checkGeometry(tree.nodes[0], 0, 0, 300, 100, "row") &&
           checkGeometry(tree.nodes[1], 0, 0, 50, 30, "c1") &&
           checkGeometry(tree.nodes[2], 50, 0, 70, 40, "c2");
}

bool test_generic_builder_lambda()
{
    auto soaCtx = initSoaData();
    FlatLayoutTree tree;
    UIBuilder b(soaCtx, tree);

    b.Root<ContainerStyleTrait>(
        "app",
        [](auto &b) {
            b.template Add<ColumnStyleTrait>(
                "mainCol",
                [](auto &b) {
                    b.template Add<RowStyleTrait>(
                        "header",
                        [](auto &b) {
                            b.template Add<TextStyleTrait>("title", std::nullopt, 30);
                        },
                        400, 50);
                    b.template Add<TextStyleTrait>("count_label", std::nullopt, 40);
                },
                std::nullopt, std::nullopt);
        },
        400, 300);

    soaCtx.layout(soaCtx, tree, 0, {0, 400, 0, 300});

    bool ok = (tree.nodes.size() == 5);
    auto find = [&](const std::string &name) -> const FlatNode * {
        for (auto &n : tree.nodes)
            if (n.name == name)
                return &n;
        return nullptr;
    };
    ok &= (find("title") != nullptr && find("count_label") != nullptr);
    const FlatNode *header = find("header");
    ok &= (header != nullptr && header->nextSibling == 4);
    return ok;
}

bool test_generic_builder_nested_expanded()
{
    auto soaCtx = initSoaData();
    FlatLayoutTree tree;
    UIBuilder b(soaCtx, tree);

    b.Root<RowStyleTrait>(
        "row",
        [](auto &b) {
            b.template Add<ContainerStyleTrait>("fixed", 50, 50);
            b.template Add<ExpandedStyleTrait>(
                "expRow",
                [](auto &b) {
                    b.template Add<ColumnStyleTrait>(
                        "col",
                        [](auto &b) {
                            b.template Add<ContainerStyleTrait>("colFixed", 80, 30);
                            b.template Add<ExpandedStyleTrait>(
                                "expCol",
                                [](auto &b) {
                                    b.template Add<ContainerStyleTrait>(
                                        "inner", std::nullopt, std::nullopt);
                                },
                                1);
                        },
                        std::nullopt, std::nullopt, EdgeInsets{}, EdgeInsets{},
                        MainAxisAlignment::start, CrossAxisAlignment::stretch);
                },
                1);
        },
        300, 200);

    soaCtx.layout(soaCtx, tree, 0, {0, 300, 0, 200});

    auto findNode = [&](const std::string &name) -> const FlatNode * {
        for (auto &n : tree.nodes)
            if (n.name == name)
                return &n;
        return nullptr;
    };
    const FlatNode *colFixed = findNode("colFixed");
    const FlatNode *inner = findNode("inner");
    bool ok = true;
    ok &= (colFixed != nullptr && inner != nullptr);
    if (colFixed)
        ok &= checkGeometry(*colFixed, 0, 0, 250, 30, "colFixed");
    if (inner)
        ok &= checkGeometry(*inner, 0, 30, 250, 170, "inner expanded");
    return ok;
}

bool test_generic_builder_single_child_constraint()
{
    auto soaCtx = initSoaData();
    FlatLayoutTree tree;
    UIBuilder b(soaCtx, tree);

    bool caught = false;
    try
    {
        b.Root<ContainerStyleTrait>(
            "root",
            [](auto &b) {
                b.template Add<TextStyleTrait>("first", 30, 30);
                b.template Add<TextStyleTrait>("second", 30, 30); // 应抛出异常
            },
            200, 200);
    }
    catch (const std::logic_error &e)
    {
        caught = true;
    }
    return caught;
}

bool test_generic_builder_reactive()
{
    auto soaCtx = initSoaData();
    auto build = [&](bool showExtra) {
        FlatLayoutTree tree;
        UIBuilder b(soaCtx, tree);
        b.Root<ContainerStyleTrait>(
            "app",
            [&](auto &b) {
                b.template Add<ColumnStyleTrait>(
                    "mainCol",
                    [&](auto &b) {
                        b.template Add<RowStyleTrait>(
                            "header",
                            [&](auto &b) {
                                b.template Add<TextStyleTrait>("title", std::nullopt, 30);
                                b.If(showExtra, [](auto &b) {
                                    b.template Add<TextStyleTrait>("extra", std::nullopt,
                                                                   20);
                                });
                            },
                            400, 50);
                        b.template Add<TextStyleTrait>("count_label", std::nullopt, 40);
                    },
                    std::nullopt, std::nullopt);
            },
            400, 300);

        soaCtx.layout(soaCtx, tree, 0, {0, 400, 0, 300});
        return tree;
    };

    {
        auto t = build(false);
        bool hasExtra = false;
        for (auto &n : t.nodes)
            if (n.name == "extra")
                hasExtra = true;
        if (hasExtra)
            return false;
    }
    {
        auto t = build(true);
        bool hasExtra = false;
        for (auto &n : t.nodes)
            if (n.name == "extra")
                hasExtra = true;
        if (!hasExtra)
            return false;
    }
    return true;
}

/// 屏幕几何信息
struct ScreenGeometry
{
    float x, y, w, h;
    float padL, padR, padT, padB;
    float borderL, borderR, borderT, borderB;
};

static std::vector<ScreenGeometry> extractScreenGeometries(auto &soaCtx,
                                                           const FlatLayoutTree &tree,
                                                           int rootIdx)
{
    std::vector<ScreenGeometry> geos;
    auto dfs = [&](this auto &self, int nodeIdx, float accX, float accY) -> void {
        const FlatNode &node = tree.nodes[nodeIdx];
        float x = accX + node.geometry.x;
        float y = accY + node.geometry.y;
        float w = node.geometry.w;
        float h = node.geometry.h;

        // 直接使用 Trait 的 type_id，与 make 时一致
        if (node.ref.type_id == ContainerStyleTrait::type_id(soaCtx))
        {
            // 注意：view_entity 第一个参数是 field_count（单字段为 0）
            auto [pad, border] =
                soaCtx.containers.template view_entity<"padding", "border">(
                    0, node.ref.entity_id);
            geos.push_back({x, y, w, h, pad.left, pad.right, pad.top, pad.bottom,
                            border.left, border.right, border.top, border.bottom});
        }

        for (int c = node.firstChild; c != -1; c = tree.nodes[c].nextSibling)
            self(c, x, y);
    };
    dfs(rootIdx, 0.0f, 0.0f);
    return geos;
}
bool test_extract_screen_geometries()
{
    auto soaCtx = initSoaData();
    FlatLayoutTree tree2;
    tree2.reserve(10);
    int root2 = tree2.addNode(
        "root", ContainerStyleTrait::make(soaCtx, 100, 100, {}, {}, {}, std::nullopt),
        -1);
    int child2 = tree2.addNode(
        "child",
        ContainerStyleTrait::make(soaCtx, 30, 30, {}, EdgeInsets{5, 5, 5, 5},
                                  EdgeInsets{2, 2, 2, 2}, std::nullopt),
        root2);

    Constraints screen{0, 500, 0, 500};
    tryLayout(soaCtx, tree2, root2, screen);

    auto geos = extractScreenGeometries(soaCtx, tree2, root2);
    if (geos.size() != 2)
    {
        std::cerr << "FAIL: expected 2 geometries, got " << geos.size() << "\n";
        return false;
    }

    const auto &rootGeo = geos[0];
    if (!approx(rootGeo.x, 0) || !approx(rootGeo.y, 0) || !approx(rootGeo.w, 100) ||
        !approx(rootGeo.h, 100))
    {
        std::cerr << "FAIL: root geometry mismatch\n";
        return false;
    }

    const auto &childGeo = geos[1];
    if (!approx(childGeo.x, 0) || !approx(childGeo.y, 0) || !approx(childGeo.w, 30) ||
        !approx(childGeo.h, 30))
        return false;

    if (!approx(childGeo.padL, 5) || !approx(childGeo.padR, 5) ||
        !approx(childGeo.padT, 5) || !approx(childGeo.padB, 5) ||
        !approx(childGeo.borderL, 2) || !approx(childGeo.borderR, 2) ||
        !approx(childGeo.borderT, 2) || !approx(childGeo.borderB, 2))
        return false;

    return true;
}

// ====================== 新增修复验证测试 ======================

bool test_flex_margin_no_double_offset()
{
    auto soaCtx = initSoaData();
    FlatLayoutTree tree;
    tree.reserve(10);

    int row = tree.addNode("row", RowStyleTrait::make(soaCtx, 300, 100), -1);
    int c1 =
        tree.addNode("c1", ContainerStyleTrait::make(soaCtx, 50, 30, {10, 5, 0, 0}), row);
    int c2 =
        tree.addNode("c2", ContainerStyleTrait::make(soaCtx, 70, 40, {0, 8, 0, 0}), row);

    if (!tryLayout(soaCtx, tree, row, {0, 300, 0, 100}))
        return false;

    return checkGeometry(tree.nodes[c1], 10, 5, 50, 30, "c1 with margin") &&
           checkGeometry(tree.nodes[c2], 60, 8, 70, 40, "c2 with margin");
}

bool test_expanded_with_margin()
{
    auto soaCtx = initSoaData();
    FlatLayoutTree tree;
    tree.reserve(10);

    int row = tree.addNode("row", RowStyleTrait::make(soaCtx, 300, 100), -1);
    int c1 =
        tree.addNode("c1", ContainerStyleTrait::make(soaCtx, 50, 30, {5, 0, 0, 0}), row);
    int exp = tree.addNode("exp", ExpandedStyleTrait::make(soaCtx, 1), row);
    int c2 = tree.addNode(
        "c2", ContainerStyleTrait::make(soaCtx, std::nullopt, 40, {3, 0, 0, 0}), exp);

    if (!tryLayout(soaCtx, tree, row, {0, 300, 0, 100}))
        return false;

    return checkGeometry(tree.nodes[c1], 5, 0, 50, 30, "c1") &&
           checkGeometry(tree.nodes[c2], 58, 0, 242, 40, "c2 expanded with margin");
}

bool test_baseline_with_margin()
{
    auto soaCtx = initSoaData();
    FlatLayoutTree tree;
    tree.reserve(10);

    int row = tree.addNode("row",
                           RowStyleTrait::make(soaCtx, 300, 100, {}, {},
                                               MainAxisAlignment::start,
                                               CrossAxisAlignment::baseline),
                           -1);
    int t1 = tree.addNode(
        "t1", TextStyleTrait::make(soaCtx, std::nullopt, 40, {0, 5, 0, 0}), row);
    int t2 = tree.addNode("t2", TextStyleTrait::make(soaCtx, std::nullopt, 20), row);
    if (!tryLayout(soaCtx, tree, row, {0, 300, 0, 100}))
        return false;

    return checkGeometry(tree.nodes[t1], 0, 5, 80, 40, "t1 baseline + margin") &&
           checkGeometry(tree.nodes[t2], 80, 16, 80, 20, "t2 baseline");
}

bool test_container_baseline_with_margin()
{
    auto soaCtx = initSoaData();
    FlatLayoutTree tree;
    tree.reserve(10);

    int root = tree.addNode("root", ContainerStyleTrait::make(soaCtx, 200, 200), -1);
    int child = tree.addNode(
        "child", TextStyleTrait::make(soaCtx, std::nullopt, 40, {0, 10, 0, 0}), root);

    std::cerr << "[TEST] Before layout:\n";
    std::cerr << "  child.ref.type_id=" << tree.nodes[child].ref.type_id
              << " entity_id=" << tree.nodes[child].ref.entity_id << "\n";

    if (!tryLayout(soaCtx, tree, root, {0, 200, 0, 200}))
        return false;

    const FlatNode &rootNode = tree.nodes[root];
    const FlatNode &childNode = tree.nodes[child];

    // 重新读取 child 的 margin 和 baseline，验证布局后的状态
    auto marginRef = do_get_member<"margin">(soaCtx, childNode.ref);
    EdgeInsets childMargin = marginRef.miss_return(EdgeInsets{});
    std::cerr << "[TEST] After layout:\n";
    std::cerr << "  child.geometry = (" << childNode.geometry.x << ","
              << childNode.geometry.y << "," << childNode.geometry.w << ","
              << childNode.geometry.h << ")\n";
    std::cerr << "  child.baseline = " << childNode.baseline << "\n";
    std::cerr << "  child margin (from SOA) = {l=" << childMargin.left
              << ", t=" << childMargin.top << ", r=" << childMargin.right
              << ", b=" << childMargin.bottom << "}\n";
    std::cerr << "  root.baseline = " << rootNode.baseline << "\n";
    std::cerr << "  expected root.baseline = child.baseline + child.geometry.y = "
              << (childNode.baseline + childNode.geometry.y) << "\n";

    // 如果 child.geometry.y 不是 10，则可能 Container 布局中读取 margin 失败
    if (!approx(childNode.geometry.y, 10.0f))
    {
        std::cerr << "  => child.geometry.y is wrong! Should be 10 (top margin).\n";
    }
    if (!approx(childNode.baseline, 32.0f))
    {
        std::cerr << "  => child.baseline is wrong! Should be 32 (40 * 0.8).\n";
    }

    return approx(rootNode.baseline, 42.0f);
}
// [NEW] 测试 Container 有 alignment 时填满父约束
bool test_container_alignment_fills_parent()
{
    auto soaCtx = initSoaData();
    FlatLayoutTree tree;
    tree.reserve(10);

    // Container 无尺寸，有 alignment，父约束有界 => 应填满父约束
    int root = tree.addNode("root",
                            ContainerStyleTrait::make(soaCtx, std::nullopt, std::nullopt,
                                                      {}, {}, {}, Align::center),
                            -1);
    int child = tree.addNode("child", ContainerStyleTrait::make(soaCtx, 50, 30), root);

    Constraints parentC = {0, 200, 0, 200};
    if (!tryLayout(soaCtx, tree, root, parentC))
        return false;

    return checkGeometry(tree.nodes[root], 0, 0, 200, 200, "root fills parent") &&
           checkGeometry(tree.nodes[child], 75, 85, 50, 30, "child centered");
}

// [NEW] 测试 Flexible (loose) 行为
bool test_flexible_loose()
{
    auto soaCtx = initSoaData();
    FlatLayoutTree tree;
    tree.reserve(10);

    int row = tree.addNode("row", RowStyleTrait::make(soaCtx, 300, 100), -1);
    // 固定宽度子组件
    tree.addNode("c1", ContainerStyleTrait::make(soaCtx, 50, 30), row);
    // Flexible (loose) 子组件，内部 Container 宽度 50，不应填满剩余空间
    int flexible =
        tree.addNode("flex", ExpandedStyleTrait::make(soaCtx, 1, FlexFit::loose), row);
    tree.addNode("c2", ContainerStyleTrait::make(soaCtx, 50, 20), flexible);
    // 另一个固定子组件
    tree.addNode("c3", ContainerStyleTrait::make(soaCtx, 70, 40), row);

    if (!tryLayout(soaCtx, tree, row, {0, 300, 0, 100}))
        return false;

    // c2 应该保持 50 宽度，而不是填满弹性空间
    return checkGeometry(tree.nodes[1], 0, 0, 50, 30, "c1") &&
           checkGeometry(tree.nodes[3], 50, 0, 50, 20, "c2 flexible loose") &&
           checkGeometry(tree.nodes[4], 100, 0, 70, 40, "c3");
}

// [NEW] 测试交叉轴无界时 Flex 抛出异常
bool test_flex_unbounded_cross_throws()
{
    auto soaCtx = initSoaData();
    FlatLayoutTree tree;
    tree.reserve(10);

    // Row 未指定高度，父约束高度无界 => 交叉轴无界，应抛出
    int row = tree.addNode("row", RowStyleTrait::make(soaCtx, 300, std::nullopt), -1);
    tree.addNode("c1", ContainerStyleTrait::make(soaCtx, 50, 30), row);

    return tryLayout(soaCtx, tree, row, {0, 300, 0, Constraints::inf},
                     "unbounded cross axis");
}
bool test_complex_nested_flex()
{
    auto soaCtx = initSoaData();
    FlatLayoutTree tree;
    tree.reserve(20);

    int root = dsl_container(soaCtx, tree, -1, "root");
    int col = dsl_column(soaCtx, tree, root, "col", std::nullopt, std::nullopt,
                         MainAxisAlignment::start, CrossAxisAlignment::stretch, {}, {},
                         MainAxisSize::min);
    int row =
        dsl_row(soaCtx, tree, col, "row", std::nullopt, 60, MainAxisAlignment::start,
                CrossAxisAlignment::start, EdgeInsets{}, EdgeInsets{5, 10, 5, 10});
    dsl_container(soaCtx, tree, row, "fixed", 50, 30);
    int exp = dsl_expanded(soaCtx, tree, row, "exp", 1);
    int innerCont = dsl_container(soaCtx, tree, exp, "innerCont", std::nullopt,
                                  std::nullopt, {}, {}, {}, Align::center);
    dsl_text(soaCtx, tree, innerCont, "innerText", std::nullopt, 24);
    dsl_text(soaCtx, tree, col, "bottomText", std::nullopt, 30, {0, 15, 0, 0});

    Constraints screen{0, 500, 0, 500};
    dsl_layout(soaCtx, tree, screen);

    auto find = [&](const std::string &name) -> const FlatNode & {
        for (auto &n : tree.nodes)
            if (n.name == name)
                return n;
        throw std::runtime_error("node not found");
    };

    bool ok = true;
    ok &= checkGeometry(tree.nodes[root], 0, 0, 500, 105, "root");
    ok &= checkGeometry(tree.nodes[col], 0, 0, 500, 105, "col");
    ok &= checkGeometry(find("row"), 0, 0, 500, 60, "row");
    ok &= checkGeometry(find("fixed"), 5, 10, 50, 30, "fixed");
    // innerCont 在 flex 空间内，且 alignment center 使其垂直居中
    ok &= checkGeometry(find("innerCont"), 55, 10, 440, 40, "innerCont");
    ok &= checkGeometry(find("innerText"), 180, 8, 80, 24, "innerText centered");
    ok &= checkGeometry(find("bottomText"), 0, 75, 500, 30, "bottomText");
    return ok;
}

bool test_container_complex_decoration()
{
    auto soaCtx = initSoaData();
    FlatLayoutTree tree;
    tree.reserve(10);
    int root = dsl_container(soaCtx, tree, -1, "root", 300, 300, {},
                             EdgeInsets{10, 10, 10, 10}, // 四边均匀 padding
                             EdgeInsets{2, 2, 2, 2},     // 四边均匀 border
                             Align::bottomRight);
    int child = dsl_container(soaCtx, tree, root, "child", 50, 40);
    Constraints c{0, 500, 0, 500};
    dsl_layout(soaCtx, tree, c);
    return checkGeometry(tree.nodes[root], 0, 0, 300, 300, "root") &&
           checkGeometry(tree.nodes[child], 238, 248, 50, 40, "child");
}

bool test_complex_baseline_in_column()
{
    auto soaCtx = initSoaData();
    FlatLayoutTree tree;
    tree.reserve(20);
    int root = dsl_container(soaCtx, tree, -1, "root", 400, 400);
    int col = dsl_column(soaCtx, tree, root, "col", std::nullopt, std::nullopt,
                         MainAxisAlignment::start, CrossAxisAlignment::stretch);
    int row = dsl_row(soaCtx, tree, col, "row", std::nullopt, 50,
                      MainAxisAlignment::start, CrossAxisAlignment::baseline);
    dsl_text(soaCtx, tree, row, "t1", std::nullopt, 30);
    dsl_text(soaCtx, tree, row, "t2", std::nullopt, 50);
    int exp = dsl_expanded(soaCtx, tree, col, "exp", 1);
    int innerCol = dsl_column(soaCtx, tree, exp, "innerCol", std::nullopt, std::nullopt,
                              MainAxisAlignment::start, CrossAxisAlignment::start, {}, {},
                              MainAxisSize::min);
    dsl_text(soaCtx, tree, innerCol, "innerText", std::nullopt, 40);

    Constraints screen{0, 400, 0, 400};
    dsl_layout(soaCtx, tree, screen);

    auto find = [&](const std::string &name) -> const FlatNode & {
        for (auto &n : tree.nodes)
            if (n.name == name)
                return n;
        throw std::runtime_error("node not found");
    };
    return checkGeometry(find("t1"), 0, 16, 80, 30, "t1") &&
           checkGeometry(find("t2"), 80, 0, 80, 50, "t2") &&
           checkGeometry(find("row"), 0, 0, 400, 50, "row") &&
           checkGeometry(find("innerCol"), 0, 50, 400, 350, "innerCol") &&
           checkGeometry(find("innerText"), 0, 0, 80, 40, "innerText");
}
// 测试主轴 spaceEvenly 的间隙
bool test_row_space_evenly()
{
    auto soaCtx = initSoaData();
    FlatLayoutTree tree;
    tree.reserve(10);
    int row = tree.addNode("row",
                           RowStyleTrait::make(soaCtx, 300, 100, {}, {},
                                               MainAxisAlignment::spaceEvenly,
                                               CrossAxisAlignment::start),
                           -1);
    tree.addNode("c1", ContainerStyleTrait::make(soaCtx, 50, 30), row);
    tree.addNode("c2", ContainerStyleTrait::make(soaCtx, 50, 30), row);
    tree.addNode("c3", ContainerStyleTrait::make(soaCtx, 50, 30), row);

    tryLayout(soaCtx, tree, row, {0, 300, 0, 100});

    // spaceEvenly: 间隙 = (300 - 150) / 4 = 37.5
    // 子组件 x 分别为 37.5, 125, 212.5
    return checkGeometry(tree.nodes[1], 37.5, 0, 50, 30, "c1") &&
           checkGeometry(tree.nodes[2], 125, 0, 50, 30, "c2") &&
           checkGeometry(tree.nodes[3], 212.5, 0, 50, 30, "c3");
}

// 测试 Column 交叉轴 end 对齐
bool test_column_cross_end()
{
    auto soaCtx = initSoaData();
    FlatLayoutTree tree;
    tree.reserve(10);
    int col = tree.addNode("col",
                           ColumnStyleTrait::make(soaCtx, 200, 200, {}, {},
                                                  MainAxisAlignment::start,
                                                  CrossAxisAlignment::end),
                           -1);
    tree.addNode("c1", ContainerStyleTrait::make(soaCtx, 50, 50), col);
    tree.addNode("c2", ContainerStyleTrait::make(soaCtx, 80, 40), col);

    tryLayout(soaCtx, tree, col, {0, 200, 0, 200});

    // 交叉轴宽度 200，子组件靠右（end），x 分别为 150 和 120
    return checkGeometry(tree.nodes[1], 150, 0, 50, 50, "c1") &&
           checkGeometry(tree.nodes[2], 120, 50, 80, 40, "c2");
}
// ====================== 测试执行辅助 ======================
struct TestCase
{
    const char *name;
    bool (*func)();
};

// 所有测试用例注册表
static const TestCase allTests[] = {
    {"test_basic_container_fixed", test_basic_container_fixed},
    {"test_row_fixed_children", test_row_fixed_children},
    {"test_expanded_in_row", test_expanded_in_row},
    {"test_stretch_overrides_height", test_stretch_overrides_height},
    {"test_column_min_shrink", test_column_min_shrink},
    {"test_rtl_row", test_rtl_row},
    {"test_up_column", test_up_column},
    {"test_baseline_alignment", test_baseline_alignment},
    {"test_expanded_outside_flex_throws", test_expanded_outside_flex_throws},
    {"test_nested_expanded", test_nested_expanded},
    {"test_dsl_simple_layout", test_dsl_simple_layout},
    {"test_reactive_update", test_reactive_update},
    {"test_dynamic_add_remove", test_dynamic_add_remove},
    {"test_resource_cleanup", test_resource_cleanup},
    {"test_dsl_nested_expanded", test_dsl_nested_expanded},
    {"test_container_single_child_constraint", test_container_single_child_constraint},
    {"test_generic_builder_simple", test_generic_builder_simple},
    {"test_generic_builder_lambda", test_generic_builder_lambda},
    {"test_generic_builder_nested_expanded", test_generic_builder_nested_expanded},
    {"test_generic_builder_single_child_constraint",
     test_generic_builder_single_child_constraint},
    {"test_generic_builder_reactive", test_generic_builder_reactive},
    {"test_extract_screen_geometries", test_extract_screen_geometries},
    {"test_flex_margin_no_double_offset", test_flex_margin_no_double_offset},
    {"test_expanded_with_margin", test_expanded_with_margin},
    {"test_baseline_with_margin", test_baseline_with_margin},
    {"test_container_baseline_with_margin", test_container_baseline_with_margin},
    {"test_container_alignment_fills_parent", test_container_alignment_fills_parent},
    {"test_flexible_loose", test_flexible_loose},
    {"test_flex_unbounded_cross_throws", test_flex_unbounded_cross_throws},
    {"test_complex_nested_flex", test_complex_nested_flex},
    {"test_container_complex_decoration", test_container_complex_decoration},
    {"test_complex_baseline_in_column", test_complex_baseline_in_column},
    {"test_row_space_evenly", test_row_space_evenly},
    {"test_column_cross_end", test_column_cross_end},
};

// ====================== 独立 API 反射测试 ======================
bool test_api_margin_on_text()
{
    auto soaCtx = initSoaData();
    // 创建一个带 margin 的 Text 实体
    EdgeInsets margin{0, 10, 0, 0}; // top=10
    Entity textEntity = TextStyleTrait::make(soaCtx, std::nullopt, 40, margin, {});

    // 用 do_get_member 读取 margin
    RefAny anyMargin = do_get_member<"margin">(soaCtx, textEntity);
    std::cerr << "[API TEST] do_get_member<\"margin\"> on Text: has_value="
              << anyMargin.has_value() << std::endl;
    if (!anyMargin.has_value())
    {
        std::cerr << "FAIL: margin not found via do_get_member\n";
        return false;
    }
    EdgeInsets m = anyMargin.miss_return(EdgeInsets{});
    std::cerr << "  margin = {l=" << m.left << ", t=" << m.top << ", r=" << m.right
              << ", b=" << m.bottom << "}\n";
    if (!approx(m.top, 10.0f) || !approx(m.left, 0.0f))
    {
        std::cerr << "FAIL: margin value mismatch\n";
        return false;
    }

    // 再用 view_entity 直接读 (验证另一种访问路径)
    auto [w, h] =
        soaCtx.texts.template view_entity<"width", "height">(0, textEntity.entity_id);
    // 注意：view_entity 可能需要 (col_idx, entity_id) 或仅 entity_id，按你现有代码的调用方式
    // 你现有的 Text layout 里是 view_entity<"width","height">(0, entity_id)，我们也保持一致
    std::cerr << "  view_entity width=" << (w.has_value() ? *w : 0.0f)
              << " height=" << (h.has_value() ? *h : 0.0f) << std::endl;
    return true;
}

bool test_api_padding_border_on_container()
{
    auto soaCtx = initSoaData();
    EdgeInsets padding{5, 5, 5, 5};
    EdgeInsets border{2, 2, 2, 2};
    Entity contEntity =
        ContainerStyleTrait::make(soaCtx, 30, 30, {}, padding, border, std::nullopt);

    // 用 view_entity 读取 padding/border
    auto [pad, bd] = soaCtx.containers.template view_entity<"padding", "border">(
        0, contEntity.entity_id); // 注意这里用0，与你 layout 中一致
    std::cerr << "[API TEST] container padding = {l=" << pad.left << ", t=" << pad.top
              << ", r=" << pad.right << ", b=" << pad.bottom << "}\n";
    std::cerr << "  border = {l=" << bd.left << ", t=" << bd.top << ", r=" << bd.right
              << ", b=" << bd.bottom << "}\n";
    if (!approx(pad.left, 5) || !approx(pad.top, 5) || !approx(bd.left, 2) ||
        !approx(bd.top, 2))
    {
        std::cerr << "FAIL: padding/border mismatch\n";
        return false;
    }
    return true;
}

int main()
{
    std::cout << "Running API tests...\n";
    bool api1 = test_api_margin_on_text();
    bool api2 = test_api_padding_border_on_container();
    if (!api1 || !api2)
    {
        std::cerr << "API test(s) failed, abort.\n";
        return 1;
    }

    int passed = 0;
    int failed = 0;
    constexpr int total = sizeof(allTests) / sizeof(allTests[0]);

    std::cout << "Running " << total << " tests...\n";
    std::cout << "----------------------------------------\n";

    for (const auto &test : allTests)
    {
        std::cout << "Test: " << test.name << " ... ";
        try
        {
            if (test.func())
            {
                std::cout << "PASSED\n";
                passed++;
            }
            else
            {
                std::cout << "FAILED (returned false)\n";
                failed++;
            }
        }
        catch (const std::exception &e)
        {
            std::cout << "EXCEPTION: " << e.what() << "\n";
            failed++;
        }
        catch (...)
        {
            std::cout << "UNKNOWN EXCEPTION\n";
            failed++;
        }
    }

    std::cout << "----------------------------------------\n";
    std::cout << "Results: " << passed << " passed, " << failed << " failed out of "
              << total << "\n";

    return (failed == 0) ? 0 : 1;
}