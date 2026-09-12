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
// Delegates（对照 Dart 里的 4 个自定义 delegate）
// =========================================================================
struct FixedSizeDelegate : SingleChildLayoutDelegate
{
    Size fixedSize;
    explicit FixedSizeDelegate(Size s) : fixedSize{s} {}

    Size getSize(BoxConstraints c) const override
    {
        return c.constrain(fixedSize);
    }
    // 其余用默认：childConstraints = 原样, position = (0,0)
};

struct FixedChildConstraintsDelegate : SingleChildLayoutDelegate
{
    BoxConstraints childConstraints;
    explicit FixedChildConstraintsDelegate(BoxConstraints cc) : childConstraints{cc} {}

    BoxConstraints getConstraintsForChild(BoxConstraints) const override
    {
        return childConstraints;
    }
};

struct FixedPositionDelegate : SingleChildLayoutDelegate
{
    Offset offset;
    explicit FixedPositionDelegate(Offset o) : offset{o} {}

    Offset getPositionForChild(Size, Size) const override
    {
        return offset;
    }
};

struct CombinedDelegate : SingleChildLayoutDelegate
{
    Size fixedSize;
    BoxConstraints childConstraints;
    Offset offset;

    CombinedDelegate(Size s, BoxConstraints cc, Offset o)
        : fixedSize{s}, childConstraints{cc}, offset{o}
    {
    }

    Size getSize(BoxConstraints c) const override
    {
        return c.constrain(fixedSize);
    }

    BoxConstraints getConstraintsForChild(BoxConstraints) const override
    {
        return childConstraints;
    }

    Offset getPositionForChild(Size, Size) const override
    {
        return offset;
    }
};

struct BottomRightDelegate : SingleChildLayoutDelegate
{
    Offset getPositionForChild(Size size, Size childSize) const override
    {
        return {size.width - childSize.width, size.height - childSize.height};
    }
};

// =========================================================================
// 1. 默认 delegate（通过 FixedSizeDelegate 只覆盖 getSize）
// =========================================================================
namespace test_Default
{
    void testLooseParent()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(
                CustomSingleChildLayout("cl")
                    .delegate(std::make_unique<FixedSizeDelegate>(Size{800, 600}))
                    .child(SizedBox("c").width(100).height(80))))
            .layout();

        auto *cl = screen.findByKey("cl");
        auto *c = screen.findByKey("c");
        assert(cl && c);

        expectSize(cl, 800, 600);
        expectSize(c, 100, 80);
        expectOffset(c, cl, 0, 0);
        expectTopLeft(cl, 0, 0);
        expectTopLeft(c, 0, 0);
    }

    void testNoChild()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(CustomSingleChildLayout("cl").delegate(
                std::make_unique<FixedSizeDelegate>(Size{200, 100}))))
            .layout();

        auto *cl = screen.findByKey("cl");
        assert(cl);
        expectSize(cl, 200, 100);
        expectTopLeft(cl, 300, 250);
    }
} // namespace test_Default

// =========================================================================
// 2. getSize 自定义
// =========================================================================
namespace test_GetSize
{
    void testFixed200x100()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(
                CustomSingleChildLayout("cl")
                    .delegate(std::make_unique<FixedSizeDelegate>(Size{200, 100}))
                    .child(SizedBox("c").width(50).height(30))))
            .layout();

        auto *cl = screen.findByKey("cl");
        auto *c = screen.findByKey("c");
        assert(cl && c);

        expectSize(cl, 200, 100);
        expectSize(c, 50, 30);
        expectOffset(c, cl, 0, 0);
        expectTopLeft(cl, 300, 250);
        expectTopLeft(c, 300, 250);
    }

    void testClamp1000x700()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(
                CustomSingleChildLayout("cl")
                    .delegate(std::make_unique<FixedSizeDelegate>(Size{1000, 700}))
                    .child(SizedBox("c").width(50).height(30))))
            .layout();

        auto *cl = screen.findByKey("cl");
        auto *c = screen.findByKey("c");
        assert(cl && c);

        expectSize(cl, 800, 600);
        expectSize(c, 50, 30);
        expectTopLeft(cl, 0, 0);
    }
} // namespace test_GetSize

// =========================================================================
// 3. getConstraintsForChild 自定义
// =========================================================================
namespace test_ChildConstraints
{
    void testTight150x50()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(
                CustomSingleChildLayout("cl")
                    .delegate(std::make_unique<FixedChildConstraintsDelegate>(
                        BoxConstraints::tightFor({.width = 150.0, .height = 50.0})))
                    .child(SizedBox("c").width(100).height(80))))
            .layout();

        auto *cl = screen.findByKey("cl");
        auto *c = screen.findByKey("c");
        assert(cl && c);

        expectSize(cl, 800, 600); // 默认 getSize = biggest
        expectSize(c, 150, 50);   // tight 约束
        expectOffset(c, cl, 0, 0);
        expectTopLeft(cl, 0, 0);
        expectTopLeft(c, 0, 0);
    }

    void testLoose200x100()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(
                CustomSingleChildLayout("cl")
                    .delegate(std::make_unique<FixedChildConstraintsDelegate>(
                        BoxConstraints{0.0, 200.0, 0.0, 100.0}))
                    .child(SizedBox("c").width(100).height(80))))
            .layout();

        auto *cl = screen.findByKey("cl");
        auto *c = screen.findByKey("c");
        assert(cl && c);

        expectSize(cl, 800, 600);
        expectSize(c, 100, 80);
        expectOffset(c, cl, 0, 0);
    }

    void testChildOverflow()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(
                CustomSingleChildLayout("cl")
                    .delegate(std::make_unique<CombinedDelegate>(
                        Size{100, 100},
                        BoxConstraints::tightFor({.width = 200.0, .height = 200.0}),
                        Offset{0, 0}))
                    .child(SizedBox("c").width(50).height(50))))
            .layout();

        auto *cl = screen.findByKey("cl");
        auto *c = screen.findByKey("c");
        assert(cl && c);

        expectSize(cl, 100, 100);
        expectSize(c, 200, 200);
        expectOffset(c, cl, 0, 0);
        expectTopLeft(cl, 350, 250);
        expectTopLeft(c, 350, 250);
    }
} // namespace test_ChildConstraints

// =========================================================================
// 4. getPositionForChild 自定义
// =========================================================================
namespace test_Position
{
    void testFixedOffset()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(
                Center().child(CustomSingleChildLayout("cl")
                                   .delegate(std::make_unique<CombinedDelegate>(
                                       Size{200, 100}, BoxConstraints{}, Offset{20, 30}))
                                   .child(SizedBox("c").width(50).height(30))))
            .layout();

        auto *cl = screen.findByKey("cl");
        auto *c = screen.findByKey("c");
        assert(cl && c);

        expectSize(cl, 200, 100);
        expectSize(c, 50, 30);
        expectOffset(c, cl, 20, 30);
        expectTopLeft(cl, 300, 250);
        expectTopLeft(c, 320, 280);
    }

    void testBottomRightLoose()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(CustomSingleChildLayout("cl")
                                     .delegate(std::make_unique<BottomRightDelegate>())
                                     .child(SizedBox("c").width(100).height(80))))
            .layout();

        auto *cl = screen.findByKey("cl");
        auto *c = screen.findByKey("c");
        assert(cl && c);

        expectSize(cl, 800, 600);
        expectSize(c, 100, 80);
        expectOffset(c, cl, 700, 520);
        expectTopLeft(cl, 0, 0);
        expectTopLeft(c, 700, 520);
    }

    void testBottomRightTight()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(
                CustomSingleChildLayout("cl")
                    .delegate(std::make_unique<CombinedDelegate>(
                        Size{200, 100},
                        BoxConstraints::tightFor({.width = 50.0, .height = 30.0}),
                        Offset{150, 70}))
                    .child(SizedBox("c").width(100).height(80))))
            .layout();

        auto *cl = screen.findByKey("cl");
        auto *c = screen.findByKey("c");
        assert(cl && c);

        expectSize(cl, 200, 100);
        expectSize(c, 50, 30);
        expectOffset(c, cl, 150, 70);
    }
} // namespace test_Position

// =========================================================================
// 5. 组合
// =========================================================================
namespace test_Combined
{
    void testAllThree()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(
                CustomSingleChildLayout("cl")
                    .delegate(std::make_unique<CombinedDelegate>(
                        Size{200, 100},
                        BoxConstraints::tightFor({.width = 50.0, .height = 30.0}),
                        Offset{75, 35}))
                    .child(SizedBox("c").width(100).height(80))))
            .layout();

        auto *cl = screen.findByKey("cl");
        auto *c = screen.findByKey("c");
        assert(cl && c);

        expectSize(cl, 200, 100);
        expectSize(c, 50, 30);
        expectOffset(c, cl, 75, 35);
        expectTopLeft(cl, 300, 250);
        expectTopLeft(c, 375, 285);
    }
} // namespace test_Combined

// =========================================================================
// 6. tight 父约束
// =========================================================================
namespace test_Tight
{
    void testDefaultDelegate()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(200).height(100).child(
                CustomSingleChildLayout("cl")
                    .delegate(std::make_unique<FixedSizeDelegate>(Size{200, 100}))
                    .child(SizedBox("c").width(50).height(30)))))
            .layout();

        auto *cl = screen.findByKey("cl");
        auto *c = screen.findByKey("c");
        assert(cl && c);

        expectSize(cl, 200, 100);
        expectSize(c, 200, 100); // childConstraints = 原样 tight
        expectOffset(c, cl, 0, 0);
        expectTopLeft(cl, 300, 250);
    }

    void testGetSizeClamp()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(200).height(100).child(
                CustomSingleChildLayout("cl")
                    .delegate(std::make_unique<FixedSizeDelegate>(Size{100, 50}))
                    .child(SizedBox("c").width(50).height(30)))))
            .layout();

        auto *cl = screen.findByKey("cl");
        auto *c = screen.findByKey("c");
        assert(cl && c);

        expectSize(cl, 200, 100); // constrain(100x50) → tight 200x100
        expectSize(c, 200, 100);
    }
} // namespace test_Tight

// =========================================================================
// main
// =========================================================================
void test_CustomSingleChildLayout_api()
{
    using namespace test_Default;
    runStage("Default: loose parent", testLooseParent);
    runStage("Default: no child", testNoChild);

    using namespace test_GetSize;
    runStage("GetSize: fixed 200x100", testFixed200x100);
    runStage("GetSize: clamp 1000x700", testClamp1000x700);

    using namespace test_ChildConstraints;
    runStage("ChildConstraints: tight 150x50", testTight150x50);
    runStage("ChildConstraints: loose 200x100", testLoose200x100);
    runStage("ChildConstraints: child overflow", testChildOverflow);

    using namespace test_Position;
    runStage("Position: fixed offset", testFixedOffset);
    runStage("Position: bottom right loose", testBottomRightLoose);
    runStage("Position: bottom right tight", testBottomRightTight);

    using namespace test_Combined;
    runStage("Combined: all three", testAllThree);

    using namespace test_Tight;
    runStage("Tight parent: default", testDefaultDelegate);
    runStage("Tight parent: getSize clamp", testGetSizeClamp);
}

int main()
try
{
    test_CustomSingleChildLayout_api();
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