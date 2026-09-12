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
// 1. Stack 自身 size
// =========================================================================
void test_tightWithPositioned()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(SizedBox("box").width(200).height(100).child(
            Stack("stack").addChild(Positioned().left(10).top(10).child(
                SizedBox("a").width(50).height(30))))))
        .layout();

    auto *stack = screen.findByKey("stack");
    auto *a = screen.findByKey("a");
    assert(stack && a);
    expectSize(stack, 200, 100);
    expectSize(a, 50, 30);
    expectOffset(a, stack, 10, 10);
    expectTopLeft(stack, 300, 250);
    expectTopLeft(a, 310, 260);
}

void test_looseNoPositioned()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(Stack("stack").addChild(
            Positioned().left(10).top(10).child(SizedBox("a").width(50).height(30)))))
        .layout();

    auto *stack = screen.findByKey("stack");
    auto *a = screen.findByKey("a");
    assert(stack && a);
    expectSize(stack, 800, 600);
    expectOffset(a, stack, 10, 10);
    expectTopLeft(stack, 0, 0);
}

void test_looseWithNonPositioned()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(Stack("stack").addChild(SizedBox("a").width(50).height(30))))
        .layout();

    auto *stack = screen.findByKey("stack");
    auto *a = screen.findByKey("a");
    assert(stack && a);
    expectSize(stack, 50, 30);
    expectOffset(a, stack, 0, 0);
    expectTopLeft(stack, 375, 285);
}

void test_looseMultipleNonPositioned()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(Stack("stack")
                                 .addChild(SizedBox("a").width(50).height(30))
                                 .addChild(SizedBox("b").width(80).height(20))))
        .layout();

    auto *stack = screen.findByKey("stack");
    auto *a = screen.findByKey("a");
    auto *b = screen.findByKey("b");
    assert(stack && a && b);
    expectSize(stack, 80, 30);
    expectSize(a, 50, 30);
    expectSize(b, 80, 20);
    expectOffset(a, stack, 0, 0);
    expectOffset(b, stack, 0, 0);
    expectTopLeft(stack, 360, 285);
}

void test_expand()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(Stack("stack")
                                 .fit(StackFit::expand)
                                 .addChild(SizedBox("a").width(50).height(30))))
        .layout();

    auto *stack = screen.findByKey("stack");
    auto *a = screen.findByKey("a");
    assert(stack && a);
    expectSize(stack, 800, 600);
    expectSize(a, 800, 600);
    expectOffset(a, stack, 0, 0);
}

void test_tightLoose()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(SizedBox("box").width(200).height(100).child(
            Stack("stack")
                .fit(StackFit::loose)
                .addChild(SizedBox("a").width(50).height(30)))))
        .layout();

    auto *stack = screen.findByKey("stack");
    auto *a = screen.findByKey("a");
    assert(stack && a);
    expectSize(stack, 200, 100);
    expectSize(a, 50, 30);
    expectOffset(a, stack, 0, 0);
}

void test_tightPassthrough()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(SizedBox("box").width(200).height(100).child(
            Stack("stack")
                .fit(StackFit::passthrough)
                .addChild(SizedBox("a").width(50).height(30)))))
        .layout();

    auto *stack = screen.findByKey("stack");
    auto *a = screen.findByKey("a");
    assert(stack && a);
    expectSize(stack, 200, 100);
    expectSize(a, 200, 100); // passthrough 收到 tight 约束
    expectOffset(a, stack, 0, 0);
}

void test_emptyTight()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(
            Center().child(SizedBox("box").width(200).height(100).child(Stack("stack"))))
        .layout();

    auto *stack = screen.findByKey("stack");
    assert(stack);
    expectSize(stack, 200, 100);
    expectTopLeft(stack, 300, 250);
}

void test_emptyLoose()
{
    ScreenWidget screen{};
    screen.size(W, H).root(Center().child(Stack("stack"))).layout();

    auto *stack = screen.findByKey("stack");
    assert(stack);
    expectSize(stack, 800, 600);
    expectTopLeft(stack, 0, 0);
}

// =========================================================================
// 2. 非定位子节点的 alignment
// =========================================================================
void test_alignmentCenter()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(SizedBox("box").width(200).height(100).child(
            Stack("stack")
                .alignment(Alignment::center)
                .addChild(SizedBox("a").width(50).height(30)))))
        .layout();

    auto *stack = screen.findByKey("stack");
    auto *a = screen.findByKey("a");
    assert(stack && a);
    expectOffset(a, stack, 75, 35);
}

void test_alignmentBottomRight()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(SizedBox("box").width(200).height(100).child(
            Stack("stack")
                .alignment(Alignment::bottomRight)
                .addChild(SizedBox("a").width(50).height(30)))))
        .layout();

    auto *stack = screen.findByKey("stack");
    auto *a = screen.findByKey("a");
    assert(stack && a);
    expectOffset(a, stack, 150, 70);
}

void test_alignmentTopRight()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(SizedBox("box").width(200).height(100).child(
            Stack("stack")
                .alignment(Alignment::topRight)
                .addChild(SizedBox("a").width(50).height(30)))))
        .layout();

    auto *stack = screen.findByKey("stack");
    auto *a = screen.findByKey("a");
    assert(stack && a);
    expectOffset(a, stack, 150, 0);
}

// =========================================================================
// 3. Positioned 基本定位
// =========================================================================
void test_positionedLeftTop()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(SizedBox("box").width(200).height(100).child(
            Stack("stack").addChild(Positioned().left(20).top(10).child(
                SizedBox("a").width(50).height(30))))))
        .layout();

    auto *stack = screen.findByKey("stack");
    auto *a = screen.findByKey("a");
    assert(stack && a);
    expectSize(a, 50, 30);
    expectOffset(a, stack, 20, 10);
    expectTopLeft(a, 320, 260);
}

void test_positionedRightTop()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(SizedBox("box").width(200).height(100).child(
            Stack("stack").addChild(Positioned().right(20).top(10).child(
                SizedBox("a").width(50).height(30))))))
        .layout();

    auto *stack = screen.findByKey("stack");
    auto *a = screen.findByKey("a");
    assert(stack && a);
    expectSize(a, 50, 30);
    expectOffset(a, stack, 130, 10);
}

void test_positionedLeftBottom()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(SizedBox("box").width(200).height(100).child(
            Stack("stack").addChild(Positioned().left(20).bottom(10).child(
                SizedBox("a").width(50).height(30))))))
        .layout();

    auto *stack = screen.findByKey("stack");
    auto *a = screen.findByKey("a");
    assert(stack && a);
    expectOffset(a, stack, 20, 60);
}

void test_positionedRightBottom()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(SizedBox("box").width(200).height(100).child(
            Stack("stack").addChild(Positioned().right(20).bottom(10).child(
                SizedBox("a").width(50).height(30))))))
        .layout();

    auto *stack = screen.findByKey("stack");
    auto *a = screen.findByKey("a");
    assert(stack && a);
    expectOffset(a, stack, 130, 60);
}

// =========================================================================
// 4. Positioned 尺寸推导
// =========================================================================
void test_positionedLTRBDerive()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(
            SizedBox("box").width(200).height(100).child(Stack("stack").addChild(
                Positioned().left(20).right(30).top(10).bottom(20).child(
                    SizedBox("a").width(50).height(30))))))
        .layout();

    auto *stack = screen.findByKey("stack");
    auto *a = screen.findByKey("a");
    assert(stack && a);
    expectSize(a, 150, 70); // 200-20-30, 100-10-20
    expectOffset(a, stack, 20, 10);
}

void test_positionedLeftWidth()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(SizedBox("box").width(200).height(100).child(
            Stack("stack").addChild(Positioned().left(20).top(10).width(80).child(
                SizedBox("a").width(50).height(30))))))
        .layout();

    auto *stack = screen.findByKey("stack");
    auto *a = screen.findByKey("a");
    assert(stack && a);
    expectSize(a, 80, 30);
    expectOffset(a, stack, 20, 10);
}

void test_positionedTopHeight()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(SizedBox("box").width(200).height(100).child(
            Stack("stack").addChild(Positioned().left(20).top(10).height(40).child(
                SizedBox("a").width(50).height(30))))))
        .layout();

    auto *stack = screen.findByKey("stack");
    auto *a = screen.findByKey("a");
    assert(stack && a);
    expectSize(a, 50, 40);
    expectOffset(a, stack, 20, 10);
}

void test_positionedWidthHeight()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(
            SizedBox("box").width(200).height(100).child(Stack("stack").addChild(
                Positioned().left(20).top(10).width(80).height(40).child(
                    SizedBox("a").width(50).height(30))))))
        .layout();

    auto *stack = screen.findByKey("stack");
    auto *a = screen.findByKey("a");
    assert(stack && a);
    expectSize(a, 80, 40);
    expectOffset(a, stack, 20, 10);
}

void test_positionedLROnlyAlignmentV()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(SizedBox("box").width(200).height(100).child(
            Stack("stack").addChild(Positioned().left(20).right(30).child(
                SizedBox("a").width(50).height(30))))))
        .layout();

    auto *stack = screen.findByKey("stack");
    auto *a = screen.findByKey("a");
    assert(stack && a);
    expectSize(a, 150, 30);
    expectOffset(a, stack, 20, 0); // 垂直方向 alignment topStart
}

void test_positionedWidthOnlyAlignmentH()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(SizedBox("box").width(200).height(100).child(
            Stack("stack")
                .alignment(Alignment::center)
                .addChild(Positioned().width(80).top(10).child(
                    SizedBox("a").width(50).height(30))))))
        .layout();

    auto *stack = screen.findByKey("stack");
    auto *a = screen.findByKey("a");
    assert(stack && a);
    expectSize(a, 80, 30);
    expectOffset(a, stack, 60, 10); // (200-80)/2 = 60
}

// =========================================================================
// 5. Positioned.fill
// =========================================================================
void test_positionedFillDefault()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(
            SizedBox("box").width(200).height(100).child(Stack("stack").addChild(
                Positioned().fill().child(SizedBox("a").width(50).height(30))))))
        .layout();

    auto *stack = screen.findByKey("stack");
    auto *a = screen.findByKey("a");
    assert(stack && a);
    expectSize(a, 200, 100);
    expectOffset(a, stack, 0, 0);
}

void test_positionedFillCustom()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(
            SizedBox("box").width(200).height(100).child(Stack("stack").addChild(
                Positioned().left(10).top(20).right(30).bottom(40).child(
                    SizedBox("a").width(50).height(30))))))
        .layout();

    auto *stack = screen.findByKey("stack");
    auto *a = screen.findByKey("a");
    assert(stack && a);
    expectSize(a, 160, 40);
    expectOffset(a, stack, 10, 20);
}

// =========================================================================
// 6. Positioned 单边未指定
// =========================================================================
void test_topOnlyDefault()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(
            SizedBox("box").width(200).height(100).child(Stack("stack").addChild(
                Positioned().top(10).child(SizedBox("a").width(50).height(30))))))
        .layout();

    auto *stack = screen.findByKey("stack");
    auto *a = screen.findByKey("a");
    assert(stack && a);
    expectOffset(a, stack, 0, 10);
}

void test_topOnlyCenter()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(SizedBox("box").width(200).height(100).child(
            Stack("stack")
                .alignment(Alignment::center)
                .addChild(
                    Positioned().top(10).child(SizedBox("a").width(50).height(30))))))
        .layout();

    auto *stack = screen.findByKey("stack");
    auto *a = screen.findByKey("a");
    assert(stack && a);
    expectOffset(a, stack, 75, 10); // (200-50)/2 = 75
}

void test_leftOnlyDefault()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(
            SizedBox("box").width(200).height(100).child(Stack("stack").addChild(
                Positioned().left(20).child(SizedBox("a").width(50).height(30))))))
        .layout();

    auto *stack = screen.findByKey("stack");
    auto *a = screen.findByKey("a");
    assert(stack && a);
    expectOffset(a, stack, 20, 0);
}

void test_leftOnlyCenter()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(SizedBox("box").width(200).height(100).child(
            Stack("stack")
                .alignment(Alignment::center)
                .addChild(
                    Positioned().left(20).child(SizedBox("a").width(50).height(30))))))
        .layout();

    auto *stack = screen.findByKey("stack");
    auto *a = screen.findByKey("a");
    assert(stack && a);
    expectOffset(a, stack, 20, 35); // (100-30)/2 = 35
}

// =========================================================================
// 7. Positioned 构造函数变体
// =========================================================================
void test_positionedFromRect()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(SizedBox("box").width(200).height(100).child(
            Stack("stack").addChild(Positioned()
                                        .fromRect(10, 20, 50, 30)
                                        .child(SizedBox("a").width(80).height(80))))))
        .layout();

    auto *stack = screen.findByKey("stack");
    auto *a = screen.findByKey("a");
    assert(stack && a);
    expectSize(a, 50, 30);
    expectOffset(a, stack, 10, 20);
}

void test_positionedFromRelativeRect()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(SizedBox("box").width(200).height(100).child(
            Stack("stack").addChild(Positioned()
                                        .fromLTRB(10, 20, 30, 40)
                                        .child(SizedBox("a").width(50).height(30))))))
        .layout();

    auto *stack = screen.findByKey("stack");
    auto *a = screen.findByKey("a");
    assert(stack && a);
    expectSize(a, 160, 40);
    expectOffset(a, stack, 10, 20);
}

void test_positionedDirectionalLtr()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(SizedBox("box").width(200).height(100).child(
            Stack("stack").addChild(Positioned()
                                        .directional(TextDirection::ltr, 20, 10)
                                        .child(SizedBox("a").width(50).height(30))))))
        .layout();

    auto *stack = screen.findByKey("stack");
    auto *a = screen.findByKey("a");
    assert(stack && a);
    expectOffset(a, stack, 20, 10);
}

void test_positionedDirectionalRtl()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(SizedBox("box").width(200).height(100).child(
            Stack("stack").addChild(Positioned()
                                        .directional(TextDirection::rtl, 20, 10)
                                        .child(SizedBox("a").width(50).height(30))))))
        .layout();

    auto *stack = screen.findByKey("stack");
    auto *a = screen.findByKey("a");
    assert(stack && a);
    // rtl: start -> right = 20 -> x = 200-20-50 = 130
    expectOffset(a, stack, 130, 10);
}

// =========================================================================
// 8. 多个 Positioned / 混合
// =========================================================================
void test_twoPositioned()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(SizedBox("box").width(200).height(100).child(
            Stack("stack")
                .addChild(Positioned().left(10).top(10).child(
                    SizedBox("a").width(50).height(30)))
                .addChild(Positioned().right(10).bottom(10).child(
                    SizedBox("b").width(60).height(40))))))
        .layout();

    auto *stack = screen.findByKey("stack");
    auto *a = screen.findByKey("a");
    auto *b = screen.findByKey("b");
    assert(stack && a && b);
    expectOffset(a, stack, 10, 10);
    expectOffset(b, stack, 130, 50);
}

void test_mixedPositioned()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(SizedBox("box").width(200).height(100).child(
            Stack("stack")
                .alignment(Alignment::center)
                .addChild(SizedBox("a").width(50).height(30))
                .addChild(Positioned().left(10).top(10).child(
                    SizedBox("b").width(60).height(40))))))
        .layout();

    auto *stack = screen.findByKey("stack");
    auto *a = screen.findByKey("a");
    auto *b = screen.findByKey("b");
    assert(stack && a && b);
    expectSize(stack, 200, 100);
    expectOffset(a, stack, 75, 35);
    expectOffset(b, stack, 10, 10);
}

// =========================================================================
// main
// =========================================================================
void test_Stack_api()
{
    runStage("Stack size: tight with positioned", test_tightWithPositioned);
    runStage("Stack size: loose no positioned", test_looseNoPositioned);
    runStage("Stack size: loose with non-positioned", test_looseWithNonPositioned);
    runStage("Stack size: loose multiple non-positioned",
             test_looseMultipleNonPositioned);
    runStage("Stack size: expand", test_expand);
    runStage("Stack size: tight loose", test_tightLoose);
    runStage("Stack size: tight passthrough", test_tightPassthrough);
    runStage("Stack size: empty tight", test_emptyTight);
    runStage("Stack size: empty loose", test_emptyLoose);

    runStage("Stack alignment: center", test_alignmentCenter);
    runStage("Stack alignment: bottomRight", test_alignmentBottomRight);
    runStage("Stack alignment: topRight", test_alignmentTopRight);

    runStage("Positioned: left+top", test_positionedLeftTop);
    runStage("Positioned: right+top", test_positionedRightTop);
    runStage("Positioned: left+bottom", test_positionedLeftBottom);
    runStage("Positioned: right+bottom", test_positionedRightBottom);

    runStage("Positioned size: ltrb derive", test_positionedLTRBDerive);
    runStage("Positioned size: left+width", test_positionedLeftWidth);
    runStage("Positioned size: top+height", test_positionedTopHeight);
    runStage("Positioned size: width+height", test_positionedWidthHeight);
    runStage("Positioned: lr only, alignment v", test_positionedLROnlyAlignmentV);
    runStage("Positioned: width only, alignment h", test_positionedWidthOnlyAlignmentH);

    runStage("Positioned.fill: default", test_positionedFillDefault);
    runStage("Positioned.fill: custom", test_positionedFillCustom);

    runStage("Positioned: top only default", test_topOnlyDefault);
    runStage("Positioned: top only center", test_topOnlyCenter);
    runStage("Positioned: left only default", test_leftOnlyDefault);
    runStage("Positioned: left only center", test_leftOnlyCenter);

    runStage("Positioned.fromRect", test_positionedFromRect);
    runStage("Positioned.fromRelativeRect", test_positionedFromRelativeRect);
    runStage("Positioned.directional ltr", test_positionedDirectionalLtr);
    runStage("Positioned.directional rtl", test_positionedDirectionalRtl);

    runStage("Multiple: two positioned", test_twoPositioned);
    runStage("Multiple: mixed positioned", test_mixedPositioned);
}

int main()
try
{
    test_Stack_api();
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