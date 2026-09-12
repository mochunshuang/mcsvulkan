#include <cassert>
#include <cmath>
#include <iostream>
#include <exception>

#include "layout_engine.hpp"

// NOLINTBEGIN
// =========================================================================
// 辅助函数
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

// =========================================================================
// 10. Align / Center widthFactor / heightFactor
//
// 算法（RenderPositionedBox）：
//   shrinkWrapWidth  = widthFactor != null || constraints.maxWidth  == inf
//   shrinkWrapHeight = heightFactor != null || constraints.maxHeight == inf
//   size = constraints.constrain(Size(
//              shrinkWrapWidth  ? child.width  * (widthFactor  ?? 1) : inf,
//              shrinkWrapHeight ? child.height * (heightFactor ?? 1) : inf))
//   offset = alignment.alongOffset(size - child.size)
//
// child 统一 100x80，父约束 loose 0..800 x 0..600。
// =========================================================================
namespace test_Align_Factor
{
    static constexpr auto Width = 800;
    static constexpr auto Height = 600;

    // ---------------------------------------------------------------
    // 1. Align 无 factor（对照）
    // ---------------------------------------------------------------
    void testAlignNoFactor()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(Align("al").child(SizedBox("c").width(100).height(80))))
            .layout();

        auto *al = screen.findByKey("al");
        auto *c = screen.findByKey("c");
        assert(al && c);

        expectSize(al, 800, 600);
        expectSize(c, 100, 80);
        expectOffset(c, al, 350, 260);
        expectTopLeft(al, 0, 0);
        expectTopLeft(c, 350, 260);
    }

    void testAlignTopLeft()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(Align("al")
                                     .alignment(Alignment::topLeft)
                                     .child(SizedBox("c").width(100).height(80))))
            .layout();

        auto *al = screen.findByKey("al");
        auto *c = screen.findByKey("c");
        assert(al && c);

        expectSize(al, 800, 600);
        expectOffset(c, al, 0, 0);
    }

    void testAlignBottomRight()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(Align("al")
                                     .alignment(Alignment::bottomRight)
                                     .child(SizedBox("c").width(100).height(80))))
            .layout();

        auto *al = screen.findByKey("al");
        auto *c = screen.findByKey("c");
        assert(al && c);

        expectSize(al, 800, 600);
        expectOffset(c, al, 700, 520);
    }

    // ---------------------------------------------------------------
    // 2. Align widthFactor
    // ---------------------------------------------------------------
    void testWidthFactorHalf()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(
                Align("al").widthFactor(0.5).child(SizedBox("c").width(100).height(80))))
            .layout();

        auto *al = screen.findByKey("al");
        auto *c = screen.findByKey("c");
        assert(al && c);

        // size = (100*0.5, 600) = (50, 600)
        expectSize(al, 50, 600);
        // offset = center.alongOffset((-50, 520)) = (-25, 260)
        expectOffset(c, al, -25, 260);
        expectTopLeft(al, 375, 0);
        expectTopLeft(c, 350, 260);
    }

    void testWidthFactorTwo()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(
                Align("al").widthFactor(2.0).child(SizedBox("c").width(100).height(80))))
            .layout();

        auto *al = screen.findByKey("al");
        auto *c = screen.findByKey("c");
        assert(al && c);

        expectSize(al, 200, 600);
        expectOffset(c, al, 50, 260);
    }

    void testWidthFactorZero()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(
                Align("al").widthFactor(0.0).child(SizedBox("c").width(100).height(80))))
            .layout();

        auto *al = screen.findByKey("al");
        auto *c = screen.findByKey("c");
        assert(al && c);

        expectSize(al, 0, 600);
        expectOffset(c, al, -50, 260);
    }

    // ---------------------------------------------------------------
    // 3. Align heightFactor
    // ---------------------------------------------------------------
    void testHeightFactorHalf()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(
                Align("al").heightFactor(0.5).child(SizedBox("c").width(100).height(80))))
            .layout();

        auto *al = screen.findByKey("al");
        auto *c = screen.findByKey("c");
        assert(al && c);

        expectSize(al, 800, 40);
        expectOffset(c, al, 350, -20);
        expectTopLeft(al, 0, 280);
        expectTopLeft(c, 350, 260);
    }

    void testHeightFactorTwo()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(
                Align("al").heightFactor(2.0).child(SizedBox("c").width(100).height(80))))
            .layout();

        auto *al = screen.findByKey("al");
        auto *c = screen.findByKey("c");
        assert(al && c);

        expectSize(al, 800, 160);
        expectOffset(c, al, 350, 40);
    }

    // ---------------------------------------------------------------
    // 4. Align widthFactor + heightFactor
    // ---------------------------------------------------------------
    void testBothFactorHalf()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(Align("al").widthFactor(0.5).heightFactor(0.5).child(
                SizedBox("c").width(100).height(80))))
            .layout();

        auto *al = screen.findByKey("al");
        auto *c = screen.findByKey("c");
        assert(al && c);

        expectSize(al, 50, 40);
        expectOffset(c, al, -25, -20);
        expectTopLeft(al, 375, 280);
        expectTopLeft(c, 350, 260);
    }

    void testWidthHalfHeightTwo()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(Align("al").widthFactor(0.5).heightFactor(2.0).child(
                SizedBox("c").width(100).height(80))))
            .layout();

        auto *al = screen.findByKey("al");
        auto *c = screen.findByKey("c");
        assert(al && c);

        expectSize(al, 50, 160);
        expectOffset(c, al, -25, 40);
    }

    // ---------------------------------------------------------------
    // 5. Align alignment + factor 组合
    //    wf:0.5 -> size (50, 600)，size-child = (-50, 520)，center=(-25, 260)
    //    hf:0.5 -> size (800, 40)，size-child = (700, -40)，center=(350, -20)
    // ---------------------------------------------------------------
    void testWidthHalfAlignmentTopLeft()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(Align("al")
                                     .widthFactor(0.5)
                                     .alignment(Alignment::topLeft)
                                     .child(SizedBox("c").width(100).height(80))))
            .layout();

        auto *al = screen.findByKey("al");
        auto *c = screen.findByKey("c");
        assert(al && c);

        expectSize(al, 50, 600);
        expectOffset(c, al, 0, 0);
        expectTopLeft(al, 375, 0);
        expectTopLeft(c, 375, 0);
    }

    void testWidthHalfAlignmentBottomRight()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(Align("al")
                                     .widthFactor(0.5)
                                     .alignment(Alignment::bottomRight)
                                     .child(SizedBox("c").width(100).height(80))))
            .layout();

        auto *al = screen.findByKey("al");
        auto *c = screen.findByKey("c");
        assert(al && c);

        expectSize(al, 50, 600);
        expectOffset(c, al, -50, 520);
        expectTopLeft(c, 325, 520);
    }

    void testHeightHalfAlignmentTopRight()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(Align("al")
                                     .heightFactor(0.5)
                                     .alignment(Alignment::topRight)
                                     .child(SizedBox("c").width(100).height(80))))
            .layout();

        auto *al = screen.findByKey("al");
        auto *c = screen.findByKey("c");
        assert(al && c);

        expectSize(al, 800, 40);
        expectOffset(c, al, 700, 0);
        expectTopLeft(al, 0, 280);
        expectTopLeft(c, 700, 280);
    }

    void testWidthHalfAlignmentCenterLeft()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(Align("al")
                                     .widthFactor(0.5)
                                     .alignment(Alignment::centerLeft)
                                     .child(SizedBox("c").width(100).height(80))))
            .layout();

        auto *al = screen.findByKey("al");
        auto *c = screen.findByKey("c");
        assert(al && c);

        expectSize(al, 50, 600);
        expectOffset(c, al, 0, 260);
    }

    // ---------------------------------------------------------------
    // 6. Align 在 tight 父约束下 factor 被 clamp
    // ---------------------------------------------------------------
    void testTightParentBothFactorHalf()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(SizedBox("s").width(200).height(100).child(
                Align("al").widthFactor(0.5).heightFactor(0.5).child(
                    SizedBox("c").width(100).height(80)))))
            .layout();

        auto *al = screen.findByKey("al");
        auto *c = screen.findByKey("c");
        assert(al && c);

        // tight 200x100 -> size 被 clamp 回 (200, 100)
        expectSize(al, 200, 100);
        // offset = center.alongOffset((100, 20)) = (50, 10)
        expectOffset(c, al, 50, 10);
        expectTopLeft(al, 300, 250);
        expectTopLeft(c, 350, 260);
    }

    void testTightParentBothFactorTwo()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(SizedBox("s").width(200).height(100).child(
                Align("al").widthFactor(2.0).heightFactor(2.0).child(
                    SizedBox("c").width(100).height(80)))))
            .layout();

        auto *al = screen.findByKey("al");
        auto *c = screen.findByKey("c");
        assert(al && c);

        expectSize(al, 200, 100);
        expectOffset(c, al, 50, 10);
    }

    void testTightParentTopLeft()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(SizedBox("s").width(200).height(100).child(
                Align("al")
                    .alignment(Alignment::topLeft)
                    .child(SizedBox("c").width(100).height(80)))))
            .layout();

        auto *al = screen.findByKey("al");
        auto *c = screen.findByKey("c");
        assert(al && c);

        expectSize(al, 200, 100);
        expectOffset(c, al, 0, 0);
    }

    // ---------------------------------------------------------------
    // 7. Align 无 child
    //    shrinkWrap 维度 = 0；另一维 = constraints.max
    // ---------------------------------------------------------------
    void testNoChildNoFactor()
    {
        ScreenWidget screen{};
        screen.size(Width, Height).root(Center().child(Align("al"))).layout();

        auto *al = screen.findByKey("al");
        assert(al);

        expectSize(al, 800, 600);
        expectTopLeft(al, 0, 0);
    }

    void testNoChildWidthFactorHalf()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(Align("al").widthFactor(0.5)))
            .layout();

        auto *al = screen.findByKey("al");
        assert(al);

        expectSize(al, 0, 600);
        expectTopLeft(al, 400, 0);
    }

    void testNoChildHeightFactorHalf()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(Align("al").heightFactor(0.5)))
            .layout();

        auto *al = screen.findByKey("al");
        assert(al);

        expectSize(al, 800, 0);
        expectTopLeft(al, 0, 300);
    }

    void testNoChildBothFactorHalf()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(Align("al").widthFactor(0.5).heightFactor(0.5)))
            .layout();

        auto *al = screen.findByKey("al");
        assert(al);

        expectSize(al, 0, 0);
        expectTopLeft(al, 400, 300);
    }

    // ---------------------------------------------------------------
    // 8. Center（等价于 Align(alignment: center)）
    // ---------------------------------------------------------------
    void testCenterNoFactor()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(
                Center("c").child(SizedBox("inner").width(100).height(80))))
            .layout();

        auto *c = screen.findByKey("c");
        auto *inner = screen.findByKey("inner");
        assert(c && inner);

        expectSize(c, 800, 600);
        expectOffset(inner, c, 350, 260);
        expectTopLeft(c, 0, 0);
    }

    void testCenterWidthFactorHalf()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(Center("c").widthFactor(0.5).child(
                SizedBox("inner").width(100).height(80))))
            .layout();

        auto *c = screen.findByKey("c");
        auto *inner = screen.findByKey("inner");
        assert(c && inner);

        expectSize(c, 50, 600);
        expectOffset(inner, c, -25, 260);
        expectTopLeft(c, 375, 0);
    }

    void testCenterHeightFactorHalf()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(Center("c").heightFactor(0.5).child(
                SizedBox("inner").width(100).height(80))))
            .layout();

        auto *c = screen.findByKey("c");
        auto *inner = screen.findByKey("inner");
        assert(c && inner);

        expectSize(c, 800, 40);
        expectOffset(inner, c, 350, -20);
    }

    void testCenterBothFactorHalf()
    {
        ScreenWidget screen{};
        screen.size(Width, Height)
            .root(Center().child(Center("c").widthFactor(0.5).heightFactor(0.5).child(
                SizedBox("inner").width(100).height(80))))
            .layout();

        auto *c = screen.findByKey("c");
        auto *inner = screen.findByKey("inner");
        assert(c && inner);

        expectSize(c, 50, 40);
        expectOffset(inner, c, -25, -20);
        expectTopLeft(c, 375, 280);
        expectTopLeft(inner, 350, 260);
    }
} // namespace test_Align_Factor

void test_Align_Factor_api()
{
    using namespace test_Align_Factor;

    // 1. Align 无 factor（对照）
    runStage("Align no factor", testAlignNoFactor);
    runStage("Align topLeft", testAlignTopLeft);
    runStage("Align bottomRight", testAlignBottomRight);

    // 2. Align widthFactor
    runStage("Align widthFactor 0.5", testWidthFactorHalf);
    runStage("Align widthFactor 2.0", testWidthFactorTwo);
    runStage("Align widthFactor 0", testWidthFactorZero);

    // 3. Align heightFactor
    runStage("Align heightFactor 0.5", testHeightFactorHalf);
    runStage("Align heightFactor 2.0", testHeightFactorTwo);

    // 4. Align widthFactor + heightFactor
    runStage("Align wf 0.5 hf 0.5", testBothFactorHalf);
    runStage("Align wf 0.5 hf 2.0", testWidthHalfHeightTwo);

    // 5. Align alignment + factor
    runStage("Align wf 0.5 topLeft", testWidthHalfAlignmentTopLeft);
    runStage("Align wf 0.5 bottomRight", testWidthHalfAlignmentBottomRight);
    runStage("Align hf 0.5 topRight", testHeightHalfAlignmentTopRight);
    runStage("Align wf 0.5 centerLeft", testWidthHalfAlignmentCenterLeft);

    // 6. Align 在 tight 父约束下
    runStage("Align tight parent wf/hf 0.5", testTightParentBothFactorHalf);
    runStage("Align tight parent wf/hf 2.0", testTightParentBothFactorTwo);
    runStage("Align tight parent topLeft", testTightParentTopLeft);

    // 7. Align 无 child
    runStage("Align no child no factor", testNoChildNoFactor);
    runStage("Align no child wf 0.5", testNoChildWidthFactorHalf);
    runStage("Align no child hf 0.5", testNoChildHeightFactorHalf);
    runStage("Align no child wf/hf 0.5", testNoChildBothFactorHalf);

    // 8. Center
    runStage("Center no factor", testCenterNoFactor);
    runStage("Center wf 0.5", testCenterWidthFactorHalf);
    runStage("Center hf 0.5", testCenterHeightFactorHalf);
    runStage("Center wf/hf 0.5", testCenterBothFactorHalf);
}

int main()
try
{
    test_Align_Factor_api();

    std::cout << "\n========== Group 10 (Align/Center factor) all pass ==========\n\n";

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
}
// NOLINTEND