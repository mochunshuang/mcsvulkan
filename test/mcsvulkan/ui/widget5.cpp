#include "head.hpp"
#include <any>
#include <cassert>
#include <cstddef>
#include <cstdint>
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
// 注意：这些类型必须在 ui 命名空间之前定义，否则 ui 内部无法使用
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

    // Flex 布局支持结构
    struct FlexChildInfo
    {
        int nodeIdx = -1;
        float flex = 0;
        float marginMain = 0;
        float marginCross = 0;
    };

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
                    throw std::logic_error("Expanded widget has no child.");
                int realIdx = child.firstChild;
                auto [flexVal] =
                    soaCtx.expandeds.template view_entity<"flex">(0, child.ref.entity_id);
                EdgeInsets m = do_get_member<"margin">(soaCtx, tree.nodes[realIdx].ref)
                                   .miss_return(EdgeInsets{});
                float mM = isRow ? m.horizontal() : m.vertical();
                float mC = isRow ? m.vertical() : m.horizontal();
                infos.push_back({realIdx, (float)flexVal, mM, mC});
                outTotalFlex += flexVal;
            }
            else
            {
                EdgeInsets m =
                    do_get_member<"margin">(soaCtx, child.ref).miss_return(EdgeInsets{});
                float mM = isRow ? m.horizontal() : m.vertical();
                float mC = isRow ? m.vertical() : m.horizontal();
                infos.push_back({c, 0.0f, mM, mC});
            }
        }
        return infos;
    }

    inline std::vector<float> layoutChildrenNaturally(
        auto &soaCtx, FlatLayoutTree &tree, const std::vector<FlexChildInfo> &infos,
        bool isRow, bool /*isBoundedMain*/, float /*innerMain*/, float innerCross,
        CrossAxisAlignment crossAlign)
    {
        std::vector<float> naturalMain;
        naturalMain.reserve(infos.size());
        for (const auto &info : infos)
        {
            float childMainMax = (info.flex > 0) ? 0.0f : Constraints::inf;
            float childCrossMax = innerCross;
            float childCrossMin =
                (crossAlign == CrossAxisAlignment::stretch) ? childCrossMax : 0.0f;
            Constraints childBC = makeFlexAxisConstraints(isRow, 0.0f, childMainMax,
                                                          childCrossMin, childCrossMax);
            if (info.flex == 0)
                childBC = childBC.deflate({info.marginMain, info.marginCross, 0, 0});
            soaCtx.layout(soaCtx, tree, info.nodeIdx, childBC);
            naturalMain.push_back(childMainAxisLength(tree.nodes[info.nodeIdx], isRow));
        }
        return naturalMain;
    }

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
                Constraints tightBC = makeFlexAxisConstraints(
                    isRow, allocated, allocated, childCrossMin, childCrossMax);
                soaCtx.layout(soaCtx, tree, infos[i].nodeIdx, tightBC);
                naturalMain[i] = childMainAxisLength(tree.nodes[infos[i].nodeIdx], isRow);
            }
        }
    }

    inline float computeFinalMainSize(bool isBoundedMain, float totalNatural,
                                      float innerMain, MainAxisSize axisSize)
    {
        if (!isBoundedMain)
            return totalNatural;
        if (axisSize == MainAxisSize::min)
            return std::min(totalNatural, innerMain);
        return innerMain;
    }

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
            EdgeInsets childMargin =
                do_get_member<"margin">(soaCtx, child.ref).miss_return(EdgeInsets{});
            float childMainLen = naturalMain[i];
            float childCrossLen = childCrossAxisLength(child, isRow);

            float leadingMargin = isRow ? childMargin.left : childMargin.top;
            setChildMainAxisPosition(child, dir, mainPos + leadingMargin, childMainLen,
                                     parentMainSize, padMainStart, padMainEnd);

            float crossStart = 0;
            float crossExtra =
                std::max(0.0f, innerCross - (childCrossLen + infos[i].marginCross));
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
            float crossLeadingMargin = isRow ? childMargin.top : childMargin.left;
            setChildCrossAxisPosition(child, dir, crossStart + crossLeadingMargin,
                                      childCrossLen, parentCrossSize, padCrossStart,
                                      padCrossEnd);
            mainPos += naturalMain[i] + infos[i].marginMain + mainGap;
        }
    }

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
        if (!isBoundedCross)
            throw std::logic_error("Row/Column '" + node.name +
                                   "' has unbounded cross axis.");

        if (!isBoundedMain)
        {
            for (int c = node.firstChild; c != -1; c = tree.nodes[c].nextSibling)
                if (tree.nodes[c].ref.type_id ==
                    (uint32_t)std::remove_cvref_t<decltype(soaCtx)>::find_name(
                        "expandeds"))
                    throw std::logic_error(
                        "Row/Column '" + node.name +
                        "' has unbounded main axis and Expanded child.");
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
            totalNatural += naturalMain[i] + infos[i].marginMain;

        float finalInnerMain =
            computeFinalMainSize(isBoundedMain, totalNatural, innerMain, axisSize);
        float freeMain = std::max(0.0f, finalInnerMain - totalNatural);
        distributeFlexSpace(soaCtx, tree, infos, naturalMain, isRow, freeMain, totalFlex,
                            innerCross, crossAlign);

        float totalMainUsed = 0.0f;
        for (size_t i = 0; i < infos.size(); ++i)
            totalMainUsed += naturalMain[i] + infos[i].marginMain;

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

// 引入 ui 命名空间，方便后续代码直接使用布局组件
using namespace ui;

// ====================== Container Trait ======================
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

    static void layout(auto &soaCtx, auto &selfSoa, FlatLayoutTree &tree, int nodeIdx,
                       Constraints borderBC, const EdgeInsets &pad)
    {
        FlatNode &node = tree.nodes[nodeIdx];
        auto [border, ownConstraints, width, height, align] =
            selfSoa.template view_entity<"border", "constraints", "width", "height",
                                         "alignment">(0, node.ref.entity_id);

        if (ownConstraints)
            borderBC = borderBC.intersect(*ownConstraints);

        if (node.firstChild == -1)
        {
            float w = width.value_or(0.0f);
            float h = height.value_or(0.0f);
            Size size = borderBC.clamp({w, h});
            node.geometry = {0, 0, size.width, size.height};
            node.baseline = size.height;
            return;
        }

        int childIdx = node.firstChild;
        if (tree.nodes[childIdx].nextSibling != -1)
            throw std::logic_error("Container '" + node.name +
                                   "' must have exactly one child.");

        FlatNode &child = tree.nodes[childIdx];
        Constraints innerBC = borderBC.deflate(border).deflate(pad);
        Constraints childBC = align.has_value() ? makeLooseConstraints(innerBC) : innerBC;
        childBC = childBC.deflate(
            do_get_member<"margin">(soaCtx, child.ref).miss_return(EdgeInsets{}));

        soaCtx.layout(soaCtx, tree, childIdx, childBC);

        Size childFull = childFullSize(soaCtx, child);
        float baseW = childFull.width + pad.horizontal() + border.horizontal();
        float baseH = childFull.height + pad.vertical() + border.vertical();
        float containerW = width.value_or(baseW);
        float containerH = height.value_or(baseH);
        Size containerSize = borderBC.clamp({containerW, containerH});

        Constraints containerAsConstraints{0, containerSize.width, 0,
                                           containerSize.height};
        positionChildByAlignment(soaCtx, child, containerAsConstraints, pad, align,
                                 border);

        node.geometry = {0, 0, containerSize.width, containerSize.height};
        node.baseline = child.baseline + child.geometry.y;
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

struct ExpandedStyleTrait : TraitBase<"expandeds">
{
    static Entity make(auto &soaCtx, int flex = 1)
    {
        uint32_t idx = soaCtx.expandeds.new_entity(flex);
        return {type_id(soaCtx), idx, release(soaCtx)};
    }
};
using ExpandedStyleObject = gen_soa_struct<ExpandedStyleTrait, {"flex", ^^int}>;

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
        node.baseline = size.height * 0.8f;
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
            throw std::logic_error(
                "Expanded widget must be placed directly inside Row/Column/Flex.");

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

// ====================== 测试用例（全局）======================
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
    return tryLayout(soaCtx, tree, cont, {0, 200, 0, 200}, "Expanded widget");
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

inline int dsl_container(auto &soaCtx, FlatLayoutTree &tree, int parentIdx,
                         const std::string &name, std::optional<float> w = {},
                         std::optional<float> h = {}, EdgeInsets margin = {},
                         EdgeInsets padding = {}, std::optional<Alignment> align = {})
{
    if (parentIdx != -1)
        pre_single_child_check(soaCtx, tree.getNode(parentIdx));
    auto entity = ContainerStyleTrait::make(soaCtx, w, h, margin, padding, {}, align);
    return tree.addNode(name, std::move(entity), parentIdx);
}

inline int dsl_row(auto &soaCtx, FlatLayoutTree &tree, int parentIdx,
                   const std::string &name, std::optional<float> w = {},
                   std::optional<float> h = {},
                   MainAxisAlignment ma = MainAxisAlignment::start,
                   CrossAxisAlignment ca = CrossAxisAlignment::start,
                   EdgeInsets margin = {}, EdgeInsets padding = {})
{
    if (parentIdx != -1)
        pre_single_child_check(soaCtx, tree.getNode(parentIdx));
    auto entity = RowStyleTrait::make(soaCtx, w, h, margin, padding, ma, ca);
    return tree.addNode(name, std::move(entity), parentIdx);
}

inline int dsl_column(auto &soaCtx, FlatLayoutTree &tree, int parentIdx,
                      const std::string &name, std::optional<float> w = {},
                      std::optional<float> h = {},
                      MainAxisAlignment ma = MainAxisAlignment::start,
                      CrossAxisAlignment ca = CrossAxisAlignment::start,
                      EdgeInsets margin = {}, EdgeInsets padding = {})
{
    if (parentIdx != -1)
        pre_single_child_check(soaCtx, tree.getNode(parentIdx));
    auto entity = ColumnStyleTrait::make(soaCtx, w, h, margin, padding, ma, ca);
    return tree.addNode(name, std::move(entity), parentIdx);
}

inline int dsl_expanded(auto &soaCtx, FlatLayoutTree &tree, int parentIdx,
                        const std::string &name, int flex = 1)
{
    if (parentIdx != -1)
        pre_single_child_check(soaCtx, tree.getNode(parentIdx));
    auto entity = ExpandedStyleTrait::make(soaCtx, flex);
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

// ====================== 运行所有测试 ======================
bool runTests()
{
    bool newTests = test_dsl_simple_layout() && test_reactive_update() &&
                    test_dynamic_add_remove() && test_resource_cleanup() &&
                    test_dsl_nested_expanded() &&
                    test_container_single_child_constraint();

    return newTests && test_basic_container_fixed() && test_row_fixed_children() &&
           test_expanded_in_row() && test_stretch_overrides_height() &&
           test_column_min_shrink() && test_rtl_row() && test_up_column() &&
           test_baseline_alignment() && test_expanded_outside_flex_throws() &&
           test_nested_expanded();
}

int main()
{
    try
    {
        if (runTests())
        {
            std::cout << "All tests passed!\n";
            return 0;
        }
        std::cout << "no passed!\n";
        return 1;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Unhandled exception: " << e.what() << "\n";
        return 1;
    }
}