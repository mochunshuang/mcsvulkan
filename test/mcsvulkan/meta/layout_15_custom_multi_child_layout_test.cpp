// NOLINTBEGIN
#include <cassert>
#include <cmath>
#include <iostream>
#include <exception>

#include "layout_engine.hpp"

// =========================================================================
// 辅助
// =========================================================================
bool nearlyEqual(double a, double b)
{
    if (std::isinf(a) || std::isinf(b))
        return a == b;
    return std::abs(a - b) < 1e-9;
}
void expectSize(const Widget *w, double width, double height)
{
    assert(w);
    assert(nearlyEqual(w->size.width, width));
    assert(nearlyEqual(w->size.height, height));
}
void expectOffset(const Widget *child, const Widget *parent, double dx, double dy)
{
    assert(child && parent);
    assert(nearlyEqual(child->offset.x - parent->offset.x, dx));
    assert(nearlyEqual(child->offset.y - parent->offset.y, dy));
}
void expectTopLeft(const Widget *w, double x, double y)
{
    assert(w);
    assert(nearlyEqual(w->offset.x, x));
    assert(nearlyEqual(w->offset.y, y));
}
template <typename Fn>
void runStage(const char *name, Fn &&fn)
{
    std::cout << "[ RUN  ] " << name << std::endl;
    fn();
    std::cout << "[ PASS ] " << name << std::endl;
}

constexpr auto W = 800.0;
constexpr auto H = 600.0;

// =========================================================================
// Delegates
// =========================================================================
struct FixedSizeDelegate : MultiChildLayoutDelegate
{
    Size fixedSize;
    explicit FixedSizeDelegate(Size s) : fixedSize{s} {}

    Size getSize(BoxConstraints c) const override
    {
        return c.constrain(fixedSize);
    }

    void performLayout(Size size, MultiChildLayoutContext &ctx) const override
    {
        for (auto *slot : {"a", "b", "c"})
            if (ctx.hasChild(slot))
            {
                ctx.layoutChild(slot, BoxConstraints::loose(size));
                ctx.positionChild(slot, {0.0, 0.0});
            }
    }
};

struct HorizontalDelegate : MultiChildLayoutDelegate
{
    void performLayout(Size size, MultiChildLayoutContext &ctx) const override
    {
        double x = 0.0;
        for (auto *slot : {"a", "b", "c"})
            if (ctx.hasChild(slot))
            {
                const Size s = ctx.layoutChild(slot, BoxConstraints::loose(size));
                ctx.positionChild(slot, {x, 0.0});
                x += s.width;
            }
    }
};

struct FollowTheLeaderDelegate : MultiChildLayoutDelegate
{
    void performLayout(Size size, MultiChildLayoutContext &ctx) const override
    {
        Size leaderSize{0.0, 0.0};
        if (ctx.hasChild("a"))
        {
            leaderSize = ctx.layoutChild("a", BoxConstraints::loose(size));
            ctx.positionChild("a", {0.0, 0.0});
        }
        if (ctx.hasChild("b"))
        {
            ctx.layoutChild("b", BoxConstraints::tight(leaderSize));
            ctx.positionChild(
                "b", {size.width - leaderSize.width, size.height - leaderSize.height});
        }
    }
};

struct FixedLayoutDelegate : MultiChildLayoutDelegate
{
    Size fixedSize;
    BoxConstraints childConstraints;
    Offset offset;

    FixedLayoutDelegate(Size s, BoxConstraints cc, Offset o)
        : fixedSize{s}, childConstraints{cc}, offset{o}
    {
    }

    Size getSize(BoxConstraints c) const override
    {
        return c.constrain(fixedSize);
    }

    void performLayout(Size, MultiChildLayoutContext &ctx) const override
    {
        if (ctx.hasChild("a"))
        {
            ctx.layoutChild("a", childConstraints);
            ctx.positionChild("a", offset);
        }
    }
};

struct NoPositionDelegate : MultiChildLayoutDelegate
{
    void performLayout(Size size, MultiChildLayoutContext &ctx) const override
    {
        if (ctx.hasChild("a"))
            ctx.layoutChild("a", BoxConstraints::loose(size));
        // 故意不调 positionChild
    }
};

// =========================================================================
// 1. 单 child 基础
// =========================================================================
namespace test_Single
{
    void testFixedSizeSingle()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(
                CustomMultiChildLayout("cml")
                    .delegate(std::make_unique<FixedSizeDelegate>(Size{200, 100}))
                    .addChild(LayoutId("a").child(SizedBox("a").width(50).height(30)))))
            .layout();

        auto *cml = screen.findByKey("cml");
        auto *a = screen.findByKey("a");
        assert(cml && a);

        expectSize(cml, 200, 100);
        expectSize(a, 50, 30);
        expectOffset(a, cml, 0, 0);
        expectTopLeft(cml, 300, 250);
        expectTopLeft(a, 300, 250);
    }

    void testGetSizeClamped()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(
                CustomMultiChildLayout("cml")
                    .delegate(std::make_unique<FixedSizeDelegate>(Size{1000, 700}))
                    .addChild(LayoutId("a").child(SizedBox("a").width(50).height(30)))))
            .layout();

        auto *cml = screen.findByKey("cml");
        auto *a = screen.findByKey("a");
        assert(cml && a);

        expectSize(cml, 800, 600);
        expectSize(a, 50, 30);
        expectOffset(a, cml, 0, 0);
        expectTopLeft(cml, 0, 0);
    }

    void testNoChild()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(CustomMultiChildLayout("cml").delegate(
                std::make_unique<FixedSizeDelegate>(Size{200, 100}))))
            .layout();

        auto *cml = screen.findByKey("cml");
        assert(cml);
        expectSize(cml, 200, 100);
        expectTopLeft(cml, 300, 250);
    }

    void testHasChildFalseSkipped()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(
                CustomMultiChildLayout("cml")
                    .delegate(std::make_unique<FixedSizeDelegate>(Size{200, 100}))
                    .addChild(LayoutId("b").child( // 只有 b，没有 a/c
                        SizedBox("b").width(50).height(30)))))
            .layout();

        auto *cml = screen.findByKey("cml");
        auto *b = screen.findByKey("b");
        assert(cml && b);

        expectSize(cml, 200, 100);
        expectSize(b, 50, 30);
        expectOffset(b, cml, 0, 0);
    }

    void testNoPositionKeepsZero()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(
                CustomMultiChildLayout("cml")
                    .delegate(std::make_unique<NoPositionDelegate>())
                    .addChild(LayoutId("a").child(SizedBox("a").width(50).height(30)))))
            .layout();

        auto *cml = screen.findByKey("cml");
        auto *a = screen.findByKey("a");
        assert(cml && a);

        expectSize(cml, 800, 600);
        expectOffset(a, cml, 0, 0);
    }
} // namespace test_Single

// =========================================================================
// 2. 多 child 水平排列
// =========================================================================
namespace test_Horizontal
{
    void testTwoChildren()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(
                CustomMultiChildLayout("cml")
                    .delegate(std::make_unique<HorizontalDelegate>())
                    .addChild(LayoutId("a").child(SizedBox("a").width(50).height(30)))
                    .addChild(LayoutId("b").child(SizedBox("b").width(70).height(40)))))
            .layout();

        auto *cml = screen.findByKey("cml");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(cml && a && b);

        expectSize(cml, 800, 600);
        expectSize(a, 50, 30);
        expectSize(b, 70, 40);
        expectOffset(a, cml, 0, 0);
        expectOffset(b, cml, 50, 0);
        expectTopLeft(cml, 0, 0);
    }

    void testThreeChildren()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(
                CustomMultiChildLayout("cml")
                    .delegate(std::make_unique<HorizontalDelegate>())
                    .addChild(LayoutId("a").child(SizedBox("a").width(50).height(30)))
                    .addChild(LayoutId("b").child(SizedBox("b").width(70).height(40)))
                    .addChild(LayoutId("c").child(SizedBox("c").width(60).height(20)))))
            .layout();

        auto *cml = screen.findByKey("cml");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        auto *c = screen.findByKey("c");
        assert(cml && a && b && c);

        expectOffset(a, cml, 0, 0);
        expectOffset(b, cml, 50, 0);
        expectOffset(c, cml, 120, 0);
    }

    void testLooseChildExpand()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(CustomMultiChildLayout("cml")
                                     .delegate(std::make_unique<HorizontalDelegate>())
                                     .addChild(LayoutId("a").child(Container("a")))))
            .layout();

        auto *cml = screen.findByKey("cml");
        auto *a = screen.findByKey("a");
        assert(cml && a);

        // loose(800x600)，无尺寸 Container 展开到 800x600
        expectSize(a, 800, 600);
        expectOffset(a, cml, 0, 0);
    }
} // namespace test_Horizontal

// =========================================================================
// 3. FollowTheLeader
// =========================================================================
namespace test_FollowTheLeader
{
    void testLeaderDecidesFollower()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(
                CustomMultiChildLayout("cml")
                    .delegate(std::make_unique<FollowTheLeaderDelegate>())
                    .addChild(LayoutId("a").child(SizedBox("a").width(100).height(80)))
                    .addChild(LayoutId("b").child(Container("b")))))
            .layout();

        auto *cml = screen.findByKey("cml");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(cml && a && b);

        expectSize(cml, 800, 600);
        expectSize(a, 100, 80);
        expectSize(b, 100, 80); // 被 tight(leaderSize) 拉成
        expectOffset(a, cml, 0, 0);
        expectOffset(b, cml, 700, 520);
        expectTopLeft(b, 700, 520);
    }

    void testOnlyLeader()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(
                CustomMultiChildLayout("cml")
                    .delegate(std::make_unique<FollowTheLeaderDelegate>())
                    .addChild(LayoutId("a").child(SizedBox("a").width(100).height(80)))))
            .layout();

        auto *cml = screen.findByKey("cml");
        auto *a = screen.findByKey("a");
        assert(cml && a);

        expectSize(a, 100, 80);
        expectOffset(a, cml, 0, 0);
    }
} // namespace test_FollowTheLeader

// =========================================================================
// 4. childConstraints 自定义
// =========================================================================
namespace test_ChildConstraints
{
    void testTight50x30()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(
                CustomMultiChildLayout("cml")
                    .delegate(std::make_unique<FixedLayoutDelegate>(
                        Size{200, 100},
                        BoxConstraints::tightFor({.width = 50.0, .height = 30.0}),
                        Offset{0, 0}))
                    .addChild(LayoutId("a").child(SizedBox("a").width(100).height(80)))))
            .layout();

        auto *cml = screen.findByKey("cml");
        auto *a = screen.findByKey("a");
        assert(cml && a);

        expectSize(cml, 200, 100);
        expectSize(a, 50, 30);
        expectOffset(a, cml, 0, 0);
        expectTopLeft(cml, 300, 250);
        expectTopLeft(a, 300, 250);
    }

    void testChildOverflow()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(
                CustomMultiChildLayout("cml")
                    .delegate(std::make_unique<FixedLayoutDelegate>(
                        Size{100, 100},
                        BoxConstraints::tightFor({.width = 200.0, .height = 200.0}),
                        Offset{0, 0}))
                    .addChild(LayoutId("a").child(Container("a")))))
            .layout();

        auto *cml = screen.findByKey("cml");
        auto *a = screen.findByKey("a");
        assert(cml && a);

        expectSize(cml, 100, 100);
        expectSize(a, 200, 200);
        expectOffset(a, cml, 0, 0);
    }

    void testOffsetCustom()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(
                CustomMultiChildLayout("cml")
                    .delegate(std::make_unique<FixedLayoutDelegate>(
                        Size{200, 100}, BoxConstraints{}, Offset{20, 30}))
                    .addChild(LayoutId("a").child(SizedBox("a").width(50).height(30)))))
            .layout();

        auto *cml = screen.findByKey("cml");
        auto *a = screen.findByKey("a");
        assert(cml && a);

        expectSize(cml, 200, 100);
        expectSize(a, 50, 30);
        expectOffset(a, cml, 20, 30);
        expectTopLeft(cml, 300, 250);
        expectTopLeft(a, 320, 280);
    }
} // namespace test_ChildConstraints

// =========================================================================
// 5. tight 父约束
// =========================================================================
namespace test_Tight
{
    void testGetSizeClamp()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(200).height(100).child(
                CustomMultiChildLayout("cml")
                    .delegate(std::make_unique<FixedSizeDelegate>(Size{100, 50}))
                    .addChild(LayoutId("a").child(SizedBox("a").width(50).height(30))))))
            .layout();

        auto *cml = screen.findByKey("cml");
        auto *a = screen.findByKey("a");
        assert(cml && a);

        expectSize(cml, 200, 100);
        expectSize(a, 50, 30);
        expectOffset(a, cml, 0, 0);
        expectTopLeft(cml, 300, 250);
    }

    void testDelegateReturnsTight()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(200).height(100).child(
                CustomMultiChildLayout("cml")
                    .delegate(std::make_unique<FixedSizeDelegate>(Size{200, 100}))
                    .addChild(LayoutId("a").child(SizedBox("a").width(50).height(30))))))
            .layout();

        auto *cml = screen.findByKey("cml");
        auto *a = screen.findByKey("a");
        assert(cml && a);

        expectSize(cml, 200, 100);
        expectSize(a, 50, 30);
        expectOffset(a, cml, 0, 0);
    }
} // namespace test_Tight

// =========================================================================
// 6. 边界：零尺寸 child
// =========================================================================
namespace test_Edge
{
    void testZeroSizeChild()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(
                CustomMultiChildLayout("cml")
                    .delegate(std::make_unique<FixedSizeDelegate>(Size{200, 100}))
                    .addChild(LayoutId("a").child(SizedBox("a").width(0).height(0)))))
            .layout();

        auto *cml = screen.findByKey("cml");
        auto *a = screen.findByKey("a");
        assert(cml && a);

        expectSize(a, 0, 0);
        expectOffset(a, cml, 0, 0);
    }
} // namespace test_Edge

// =========================================================================
// main
// =========================================================================
void test_CustomMultiChildLayout_api()
{
    using namespace test_Single;
    runStage("Single: fixed size 200x100", testFixedSizeSingle);
    runStage("Single: getSize clamped", testGetSizeClamped);
    runStage("Single: no child", testNoChild);
    runStage("Single: hasChild false skipped", testHasChildFalseSkipped);
    runStage("Single: no positionChild → (0,0)", testNoPositionKeepsZero);

    using namespace test_Horizontal;
    runStage("Horizontal: two children", testTwoChildren);
    runStage("Horizontal: three children", testThreeChildren);
    runStage("Horizontal: loose child expand", testLooseChildExpand);

    using namespace test_FollowTheLeader;
    runStage("FollowTheLeader: leader+follower", testLeaderDecidesFollower);
    runStage("FollowTheLeader: only leader", testOnlyLeader);

    using namespace test_ChildConstraints;
    runStage("ChildConstraints: tight 50x30", testTight50x30);
    runStage("ChildConstraints: child overflow", testChildOverflow);
    runStage("ChildConstraints: offset (20,30)", testOffsetCustom);

    using namespace test_Tight;
    runStage("Tight: getSize clamp", testGetSizeClamp);
    runStage("Tight: delegate returns tight", testDelegateReturnsTight);

    using namespace test_Edge;
    runStage("Edge: zero-size child", testZeroSizeChild);
}

int main()
try
{
    test_CustomMultiChildLayout_api();
    std::cout << "main done\n";
    return 0;
}
catch (const std::exception &e)
{
    std::cerr << "Exception: " << e.what() << '\n';
    return 1;
}
catch (...)
{
    std::cerr << "Unknown exception\n";
    return 1;
} // NOLINTEND