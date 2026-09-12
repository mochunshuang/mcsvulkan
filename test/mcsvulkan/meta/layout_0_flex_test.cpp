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
// Flex 测试组
//
// 对应 Dart 测试文件里的 8 组：
//   1. 方向 + mainAxisSize 基础
//   2. 交叉轴 = 子节点最大值
//   3. horizontal + max 下的 mainAxisAlignment
//   4. vertical   + max 下的 mainAxisAlignment
//   5. horizontal + min 下的 crossAxisAlignment
//   6. vertical   + min 下的 crossAxisAlignment
//   7. textDirection / verticalDirection
//   8. 边界：空 children、零尺寸子节点
// =========================================================================
namespace test_Flex
{
    constexpr auto W = 800.0;
    constexpr auto H = 600.0;

    // ---------------------------------------------------------------
    // 1. 方向 + mainAxisSize
    // ---------------------------------------------------------------

    // Dart: 'Flex: horizontal, min, single 50x30 child'
    void testHorizontalMinSingle()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Flex("flex")
                                     .direction(Axis::horizontal)
                                     .mainAxisSize(MainAxisSize::min)
                                     .addChild(SizedBox("a").width(50).height(30))))
            .layout();

        auto *flex = screen.findByKey("flex");
        auto *a = screen.findByKey("a");
        assert(flex && a);

        expectSize(flex, 50, 30);
        expectSize(a, 50, 30);
        expectOffset(a, flex, 0, 0);
        expectTopLeft(flex, 375, 285);
        expectTopLeft(a, 375, 285);
    }

    // Dart: 'Flex: horizontal, max, single 50x30 child'
    void testHorizontalMaxSingle()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Flex("flex")
                                     .direction(Axis::horizontal)
                                     .mainAxisSize(MainAxisSize::max)
                                     .addChild(SizedBox("a").width(50).height(30))))
            .layout();

        auto *flex = screen.findByKey("flex");
        auto *a = screen.findByKey("a");
        assert(flex && a);

        expectSize(flex, 800, 30);
        expectSize(a, 50, 30);
        expectOffset(a, flex, 0, 0);
        expectTopLeft(flex, 0, 285);
        expectTopLeft(a, 0, 285);
    }

    // Dart: 'Flex: vertical, min, single 50x30 child'
    void testVerticalMinSingle()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Flex("flex")
                                     .direction(Axis::vertical)
                                     .mainAxisSize(MainAxisSize::min)
                                     .addChild(SizedBox("a").width(50).height(30))))
            .layout();

        auto *flex = screen.findByKey("flex");
        auto *a = screen.findByKey("a");
        assert(flex && a);

        expectSize(flex, 50, 30);
        expectSize(a, 50, 30);
        expectOffset(a, flex, 0, 0);
        expectTopLeft(flex, 375, 285);
    }

    // Dart: 'Flex: vertical, max, single 50x30 child'
    void testVerticalMaxSingle()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Flex("flex")
                                     .direction(Axis::vertical)
                                     .mainAxisSize(MainAxisSize::max)
                                     .addChild(SizedBox("a").width(50).height(30))))
            .layout();

        auto *flex = screen.findByKey("flex");
        auto *a = screen.findByKey("a");
        assert(flex && a);

        expectSize(flex, 50, 600);
        expectSize(a, 50, 30);
        expectOffset(a, flex, 0, 0);
        expectTopLeft(flex, 375, 0);
        expectTopLeft(a, 375, 0);
    }

    // ---------------------------------------------------------------
    // 2. 交叉轴尺寸 = 子节点最大值
    // ---------------------------------------------------------------

    // Dart: 'Flex: horizontal, min, cross size = max child height'
    void testHorizontalMinCrossMax()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Flex("flex")
                                     .direction(Axis::horizontal)
                                     .mainAxisSize(MainAxisSize::min)
                                     .addChild(SizedBox("a").width(50).height(30))
                                     .addChild(SizedBox("b").width(70).height(40))))
            .layout();

        auto *flex = screen.findByKey("flex");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(flex && a && b);

        expectSize(flex, 120, 40);
        expectSize(a, 50, 30);
        expectSize(b, 70, 40);
        expectOffset(a, flex, 0, 5);
        expectOffset(b, flex, 50, 0);
        expectTopLeft(flex, 340, 280);
        expectTopLeft(a, 340, 285);
        expectTopLeft(b, 390, 280);
    }

    // Dart: 'Flex: vertical, min, cross size = max child width'
    void testVerticalMinCrossMax()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Flex("flex")
                                     .direction(Axis::vertical)
                                     .mainAxisSize(MainAxisSize::min)
                                     .addChild(SizedBox("a").width(30).height(50))
                                     .addChild(SizedBox("b").width(40).height(70))))
            .layout();

        auto *flex = screen.findByKey("flex");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(flex && a && b);

        expectSize(flex, 40, 120);
        expectSize(a, 30, 50);
        expectSize(b, 40, 70);
        expectOffset(a, flex, 5, 0);
        expectOffset(b, flex, 0, 50);
        expectTopLeft(flex, 380, 240);
        expectTopLeft(a, 385, 240);
        expectTopLeft(b, 380, 290);
    }

    // ---------------------------------------------------------------
    // 3. horizontal + max 下的 mainAxisAlignment
    //    子节点：50x30 + 70x40，总主轴 120，剩余 680
    // ---------------------------------------------------------------

    // Dart: 'Flex: horizontal, max, mainAxisAlignment.start'
    void testHorizontalMaxStart()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Flex("flex")
                                     .direction(Axis::horizontal)
                                     .mainAxisSize(MainAxisSize::max)
                                     .mainAxisAlignment(MainAxisAlignment::start)
                                     .addChild(SizedBox("a").width(50).height(30))
                                     .addChild(SizedBox("b").width(70).height(40))))
            .layout();

        auto *flex = screen.findByKey("flex");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(flex && a && b);

        expectSize(flex, 800, 40);
        expectOffset(a, flex, 0, 5);
        expectOffset(b, flex, 50, 0);
        expectTopLeft(flex, 0, 280);
        expectTopLeft(a, 0, 285);
        expectTopLeft(b, 50, 280);
    }

    // Dart: 'Flex: horizontal, max, mainAxisAlignment.center'
    void testHorizontalMaxCenter()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Flex("flex")
                                     .direction(Axis::horizontal)
                                     .mainAxisSize(MainAxisSize::max)
                                     .mainAxisAlignment(MainAxisAlignment::center)
                                     .addChild(SizedBox("a").width(50).height(30))
                                     .addChild(SizedBox("b").width(70).height(40))))
            .layout();

        auto *flex = screen.findByKey("flex");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(flex && a && b);

        expectSize(flex, 800, 40);
        expectOffset(a, flex, 340, 5);
        expectOffset(b, flex, 390, 0);
    }

    // Dart: 'Flex: horizontal, max, mainAxisAlignment.end'
    void testHorizontalMaxEnd()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Flex("flex")
                                     .direction(Axis::horizontal)
                                     .mainAxisSize(MainAxisSize::max)
                                     .mainAxisAlignment(MainAxisAlignment::end)
                                     .addChild(SizedBox("a").width(50).height(30))
                                     .addChild(SizedBox("b").width(70).height(40))))
            .layout();

        auto *flex = screen.findByKey("flex");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(flex && a && b);

        expectSize(flex, 800, 40);
        expectOffset(a, flex, 680, 5);
        expectOffset(b, flex, 730, 0);
    }

    // Dart: 'Flex: horizontal, max, mainAxisAlignment.spaceBetween'
    void testHorizontalMaxSpaceBetween()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Flex("flex")
                                     .direction(Axis::horizontal)
                                     .mainAxisSize(MainAxisSize::max)
                                     .mainAxisAlignment(MainAxisAlignment::spaceBetween)
                                     .addChild(SizedBox("a").width(50).height(30))
                                     .addChild(SizedBox("b").width(70).height(40))))
            .layout();

        auto *flex = screen.findByKey("flex");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(flex && a && b);

        expectSize(flex, 800, 40);
        expectOffset(a, flex, 0, 5);
        expectOffset(b, flex, 730, 0);
    }

    // Dart: 'Flex: horizontal, max, mainAxisAlignment.spaceAround'
    void testHorizontalMaxSpaceAround()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Flex("flex")
                                     .direction(Axis::horizontal)
                                     .mainAxisSize(MainAxisSize::max)
                                     .mainAxisAlignment(MainAxisAlignment::spaceAround)
                                     .addChild(SizedBox("a").width(50).height(30))
                                     .addChild(SizedBox("b").width(70).height(40))))
            .layout();

        auto *flex = screen.findByKey("flex");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(flex && a && b);

        expectSize(flex, 800, 40);
        expectOffset(a, flex, 170, 5);
        expectOffset(b, flex, 560, 0);
    }

    // Dart: 'Flex: horizontal, max, mainAxisAlignment.spaceEvenly'
    // 注意：子节点是 100x30 + 100x40
    void testHorizontalMaxSpaceEvenly()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Flex("flex")
                                     .direction(Axis::horizontal)
                                     .mainAxisSize(MainAxisSize::max)
                                     .mainAxisAlignment(MainAxisAlignment::spaceEvenly)
                                     .addChild(SizedBox("a").width(100).height(30))
                                     .addChild(SizedBox("b").width(100).height(40))))
            .layout();

        auto *flex = screen.findByKey("flex");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(flex && a && b);

        expectSize(flex, 800, 40);
        expectOffset(a, flex, 200, 5);
        expectOffset(b, flex, 500, 0);
    }

    // ---------------------------------------------------------------
    // 4. vertical + max 下的 mainAxisAlignment
    //    子节点：30x50 + 40x70，总主轴 120，剩余 480
    // ---------------------------------------------------------------

    // Dart: 'Flex: vertical, max, mainAxisAlignment.start'
    void testVerticalMaxStart()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Flex("flex")
                                     .direction(Axis::vertical)
                                     .mainAxisSize(MainAxisSize::max)
                                     .mainAxisAlignment(MainAxisAlignment::start)
                                     .addChild(SizedBox("a").width(30).height(50))
                                     .addChild(SizedBox("b").width(40).height(70))))
            .layout();

        auto *flex = screen.findByKey("flex");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(flex && a && b);

        expectSize(flex, 40, 600);
        expectOffset(a, flex, 5, 0);
        expectOffset(b, flex, 0, 50);
        expectTopLeft(flex, 380, 0);
    }

    // Dart: 'Flex: vertical, max, mainAxisAlignment.center'
    void testVerticalMaxCenter()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Flex("flex")
                                     .direction(Axis::vertical)
                                     .mainAxisSize(MainAxisSize::max)
                                     .mainAxisAlignment(MainAxisAlignment::center)
                                     .addChild(SizedBox("a").width(30).height(50))
                                     .addChild(SizedBox("b").width(40).height(70))))
            .layout();

        auto *flex = screen.findByKey("flex");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(flex && a && b);

        expectSize(flex, 40, 600);
        expectOffset(a, flex, 5, 240);
        expectOffset(b, flex, 0, 290);
    }

    // Dart: 'Flex: vertical, max, mainAxisAlignment.end'
    void testVerticalMaxEnd()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Flex("flex")
                                     .direction(Axis::vertical)
                                     .mainAxisSize(MainAxisSize::max)
                                     .mainAxisAlignment(MainAxisAlignment::end)
                                     .addChild(SizedBox("a").width(30).height(50))
                                     .addChild(SizedBox("b").width(40).height(70))))
            .layout();

        auto *flex = screen.findByKey("flex");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(flex && a && b);

        expectSize(flex, 40, 600);
        expectOffset(a, flex, 5, 480);
        expectOffset(b, flex, 0, 530);
    }

    // Dart: 'Flex: vertical, max, mainAxisAlignment.spaceBetween'
    void testVerticalMaxSpaceBetween()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Flex("flex")
                                     .direction(Axis::vertical)
                                     .mainAxisSize(MainAxisSize::max)
                                     .mainAxisAlignment(MainAxisAlignment::spaceBetween)
                                     .addChild(SizedBox("a").width(30).height(50))
                                     .addChild(SizedBox("b").width(40).height(70))))
            .layout();

        auto *flex = screen.findByKey("flex");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(flex && a && b);

        expectSize(flex, 40, 600);
        expectOffset(a, flex, 5, 0);
        expectOffset(b, flex, 0, 530);
    }

    // ---------------------------------------------------------------
    // 5. horizontal + min 下的 crossAxisAlignment
    //    子节点：50x30 + 70x40，交叉轴最大 40
    // ---------------------------------------------------------------

    // Dart: 'Flex: horizontal, min, crossAxisAlignment.start'
    void testHorizontalMinCrossStart()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Flex("flex")
                                     .direction(Axis::horizontal)
                                     .mainAxisSize(MainAxisSize::min)
                                     .crossAxisAlignment(CrossAxisAlignment::start)
                                     .addChild(SizedBox("a").width(50).height(30))
                                     .addChild(SizedBox("b").width(70).height(40))))
            .layout();

        auto *flex = screen.findByKey("flex");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(flex && a && b);

        expectSize(flex, 120, 40);
        expectOffset(a, flex, 0, 0);
        expectOffset(b, flex, 50, 0);
    }

    // Dart: 'Flex: horizontal, min, crossAxisAlignment.center'
    void testHorizontalMinCrossCenter()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Flex("flex")
                                     .direction(Axis::horizontal)
                                     .mainAxisSize(MainAxisSize::min)
                                     .crossAxisAlignment(CrossAxisAlignment::center)
                                     .addChild(SizedBox("a").width(50).height(30))
                                     .addChild(SizedBox("b").width(70).height(40))))
            .layout();

        auto *flex = screen.findByKey("flex");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(flex && a && b);

        expectSize(flex, 120, 40);
        expectOffset(a, flex, 0, 5);
        expectOffset(b, flex, 50, 0);
    }

    // Dart: 'Flex: horizontal, min, crossAxisAlignment.end'
    void testHorizontalMinCrossEnd()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Flex("flex")
                                     .direction(Axis::horizontal)
                                     .mainAxisSize(MainAxisSize::min)
                                     .crossAxisAlignment(CrossAxisAlignment::end)
                                     .addChild(SizedBox("a").width(50).height(30))
                                     .addChild(SizedBox("b").width(70).height(40))))
            .layout();

        auto *flex = screen.findByKey("flex");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(flex && a && b);

        expectSize(flex, 120, 40);
        expectOffset(a, flex, 0, 10);
        expectOffset(b, flex, 50, 0);
    }

    // Dart: 'Flex: horizontal, min, crossAxisAlignment.stretch'
    void testHorizontalMinCrossStretch()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Flex("flex")
                                     .direction(Axis::horizontal)
                                     .mainAxisSize(MainAxisSize::min)
                                     .crossAxisAlignment(CrossAxisAlignment::stretch)
                                     .addChild(SizedBox("a").width(50).height(30))
                                     .addChild(SizedBox("b").width(70).height(40))))
            .layout();

        auto *flex = screen.findByKey("flex");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(flex && a && b);

        expectSize(flex, 120, 600);
        expectSize(a, 50, 600);
        expectSize(b, 70, 600);
        expectOffset(a, flex, 0, 0);
        expectOffset(b, flex, 50, 0);
        expectTopLeft(flex, 340, 0);
    }

    // ---------------------------------------------------------------
    // 6. vertical + min 下的 crossAxisAlignment
    //    子节点：30x50 + 40x70，交叉轴最大 40
    // ---------------------------------------------------------------

    // Dart: 'Flex: vertical, min, crossAxisAlignment.start'
    void testVerticalMinCrossStart()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Flex("flex")
                                     .direction(Axis::vertical)
                                     .mainAxisSize(MainAxisSize::min)
                                     .crossAxisAlignment(CrossAxisAlignment::start)
                                     .addChild(SizedBox("a").width(30).height(50))
                                     .addChild(SizedBox("b").width(40).height(70))))
            .layout();

        auto *flex = screen.findByKey("flex");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(flex && a && b);

        expectSize(flex, 40, 120);
        expectOffset(a, flex, 0, 0);
        expectOffset(b, flex, 0, 50);
    }

    // Dart: 'Flex: vertical, min, crossAxisAlignment.end'
    void testVerticalMinCrossEnd()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Flex("flex")
                                     .direction(Axis::vertical)
                                     .mainAxisSize(MainAxisSize::min)
                                     .crossAxisAlignment(CrossAxisAlignment::end)
                                     .addChild(SizedBox("a").width(30).height(50))
                                     .addChild(SizedBox("b").width(40).height(70))))
            .layout();

        auto *flex = screen.findByKey("flex");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(flex && a && b);

        expectSize(flex, 40, 120);
        expectOffset(a, flex, 10, 0);
        expectOffset(b, flex, 0, 50);
    }

    // Dart: 'Flex: vertical, min, crossAxisAlignment.stretch'
    void testVerticalMinCrossStretch()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Flex("flex")
                                     .direction(Axis::vertical)
                                     .mainAxisSize(MainAxisSize::min)
                                     .crossAxisAlignment(CrossAxisAlignment::stretch)
                                     .addChild(SizedBox("a").width(30).height(50))
                                     .addChild(SizedBox("b").width(40).height(70))))
            .layout();

        auto *flex = screen.findByKey("flex");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(flex && a && b);

        expectSize(flex, 800, 120);
        expectSize(a, 800, 50);
        expectSize(b, 800, 70);
        expectOffset(a, flex, 0, 0);
        expectOffset(b, flex, 0, 50);
        expectTopLeft(flex, 0, 240);
    }

    // ---------------------------------------------------------------
    // 7. textDirection / verticalDirection
    // ---------------------------------------------------------------

    // Dart: 'Flex: horizontal, rtl, mainAxisAlignment.start'
    void testHorizontalRtl()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Flex("flex")
                                     .direction(Axis::horizontal)
                                     .textDirection(TextDirection::rtl)
                                     .mainAxisSize(MainAxisSize::max)
                                     .mainAxisAlignment(MainAxisAlignment::start)
                                     .addChild(SizedBox("a").width(50).height(30))))
            .layout();

        auto *flex = screen.findByKey("flex");
        auto *a = screen.findByKey("a");
        assert(flex && a);

        expectSize(flex, 800, 30);
        expectOffset(a, flex, 750, 0);
        expectTopLeft(a, 750, 285);
    }

    // Dart: 'Flex: vertical, up, mainAxisAlignment.start'
    void testVerticalUp()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Flex("flex")
                                     .direction(Axis::vertical)
                                     .verticalDirection(VerticalDirection::up)
                                     .mainAxisSize(MainAxisSize::max)
                                     .mainAxisAlignment(MainAxisAlignment::start)
                                     .addChild(SizedBox("a").width(30).height(50))))
            .layout();

        auto *flex = screen.findByKey("flex");
        auto *a = screen.findByKey("a");
        assert(flex && a);

        expectSize(flex, 30, 600);
        expectOffset(a, flex, 0, 550);
        expectTopLeft(a, 385, 550);
    }

    // ---------------------------------------------------------------
    // 8. 边界情况
    // ---------------------------------------------------------------

    // Dart: 'Flex: empty children, min'
    void testEmptyChildrenMin()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(
                Flex("flex").direction(Axis::horizontal).mainAxisSize(MainAxisSize::min)))
            .layout();

        auto *flex = screen.findByKey("flex");
        assert(flex);

        expectSize(flex, 0, 0);
        expectTopLeft(flex, 400, 300);
    }

    // Dart: 'Flex: zero-size child'
    void testZeroSizeChild()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Flex("flex")
                                     .direction(Axis::horizontal)
                                     .mainAxisSize(MainAxisSize::max)
                                     .addChild(SizedBox("a").width(0).height(0))))
            .layout();

        auto *flex = screen.findByKey("flex");
        auto *a = screen.findByKey("a");
        assert(flex && a);

        expectSize(flex, 800, 0);
        expectSize(a, 0, 0);
        expectOffset(a, flex, 0, 0);
        expectTopLeft(flex, 0, 300);
        expectTopLeft(a, 0, 300);
    }

} // namespace test_Flex

void test_Flex_api()
{
    using namespace test_Flex;

    // 1. 方向 + mainAxisSize
    runStage("Flex: h min single", testHorizontalMinSingle);
    runStage("Flex: h max single", testHorizontalMaxSingle);
    runStage("Flex: v min single", testVerticalMinSingle);
    runStage("Flex: v max single", testVerticalMaxSingle);

    // 2. 交叉轴 = 子节点最大值
    runStage("Flex: h min cross=max", testHorizontalMinCrossMax);
    runStage("Flex: v min cross=max", testVerticalMinCrossMax);

    // 3. horizontal + max 对齐
    runStage("Flex: h max start", testHorizontalMaxStart);
    runStage("Flex: h max center", testHorizontalMaxCenter);
    runStage("Flex: h max end", testHorizontalMaxEnd);
    runStage("Flex: h max spaceBetween", testHorizontalMaxSpaceBetween);
    runStage("Flex: h max spaceAround", testHorizontalMaxSpaceAround);
    runStage("Flex: h max spaceEvenly", testHorizontalMaxSpaceEvenly);

    // 4. vertical + max 对齐
    runStage("Flex: v max start", testVerticalMaxStart);
    runStage("Flex: v max center", testVerticalMaxCenter);
    runStage("Flex: v max end", testVerticalMaxEnd);
    runStage("Flex: v max spaceBetween", testVerticalMaxSpaceBetween);

    // 5. horizontal + min crossAxisAlignment
    runStage("Flex: h min cross start", testHorizontalMinCrossStart);
    runStage("Flex: h min cross center", testHorizontalMinCrossCenter);
    runStage("Flex: h min cross end", testHorizontalMinCrossEnd);
    runStage("Flex: h min cross stretch", testHorizontalMinCrossStretch);

    // 6. vertical + min crossAxisAlignment
    runStage("Flex: v min cross start", testVerticalMinCrossStart);
    runStage("Flex: v min cross end", testVerticalMinCrossEnd);
    runStage("Flex: v min cross stretch", testVerticalMinCrossStretch);

    // 7. textDirection / verticalDirection
    runStage("Flex: h rtl", testHorizontalRtl);
    runStage("Flex: v up", testVerticalUp);

    // 8. 边界
    runStage("Flex: empty children min", testEmptyChildrenMin);
    runStage("Flex: zero-size child", testZeroSizeChild);
}

int main()
try
{
    test_Flex_api();

    std::cout << "\n========== Flex all pass ==========\n\n";
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