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
// A. 约束链综合
// =========================================================================
namespace test_A
{
    // A1. SizedBox(tight) > ConstrainedBox > OverflowBox
    void testA1()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(200).height(100).child(
                ConstrainedBox("cb")
                    .constraints(BoxConstraints{100.0, BoxConstraints::inf, 50.0,
                                                BoxConstraints::inf})
                    .child(OverflowBox("ob")
                               .minWidth(0)
                               .maxWidth(BoxConstraints::inf)
                               .minHeight(0)
                               .maxHeight(BoxConstraints::inf)
                               .child(SizedBox("c").width(80).height(60))))))
            .layout();

        auto *cb = screen.findByKey("cb");
        auto *ob = screen.findByKey("ob");
        auto *c = screen.findByKey("c");
        assert(cb && ob && c);

        expectSize(cb, 200, 100);
        expectSize(ob, 200, 100);
        expectSize(c, 80, 60);
        expectOffset(c, ob, 60, 20);
        expectTopLeft(cb, 300, 250);
        expectTopLeft(c, 360, 270);
    }

    // A2. SizedBox(tight) > Align(wf/hf 0.5) > child
    void testA2()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(200).height(100).child(
                Align("al").widthFactor(0.5).heightFactor(0.5).child(
                    SizedBox("c").width(80).height(60)))))
            .layout();

        auto *al = screen.findByKey("al");
        auto *c = screen.findByKey("c");
        assert(al && c);

        expectSize(al, 200, 100);
        expectSize(c, 80, 60);
        expectOffset(c, al, 60, 20);
    }

    // A3. FractionallySizedBox > AspectRatio > Container
    void testA3()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(400).height(300).child(
                FractionallySizedBox("fsb").widthFactor(0.5).heightFactor(0.5).child(
                    AspectRatio("ar", 2.0).child(Container("c"))))))
            .layout();

        auto *fsb = screen.findByKey("fsb");
        auto *ar = screen.findByKey("ar");
        auto *c = screen.findByKey("c");
        assert(fsb && ar && c);

        expectSize(fsb, 400, 300);
        expectSize(ar, 200, 150);
        expectSize(c, 200, 150);
        expectOffset(ar, fsb, 100, 75);
        expectOffset(c, ar, 0, 0);
        expectTopLeft(fsb, 200, 150);
        expectTopLeft(ar, 300, 225);
    }

    // A4. IntrinsicWidth > Padding > SizedBox
    // ❌ 跳过：IntrinsicWidth 未实现（需要基类 computeMaxIntrinsicWidth 虚函数）
} // namespace test_A

// =========================================================================
// B. 流式嵌套综合
// =========================================================================
namespace test_B
{
    // B1. Row > Expanded > Column.stretch > (SizedBox + Expanded)
    void testB1()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(
                SizedBox("box").width(400).height(200).child(Row("row").addChild(
                    Expanded().child(Column("col")
                                         .crossAxisAlignment(CrossAxisAlignment::stretch)
                                         .addChild(SizedBox("a").height(40))
                                         .addChild(Expanded().child(SizedBox("b"))))))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *col = screen.findByKey("col");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(row && col && a && b);

        expectSize(row, 400, 200);
        expectSize(col, 400, 200);
        expectSize(a, 400, 40);
        expectSize(b, 400, 160);
        expectOffset(col, row, 0, 0);
        expectOffset(a, col, 0, 0);
        expectOffset(b, col, 0, 40);
        expectTopLeft(row, 200, 200);
        expectTopLeft(b, 200, 240);
    }

    // B2. Column > Expanded > Row > (SizedBox + Expanded)
    void testB2()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(
                SizedBox("box").width(200).height(300).child(Column("col").addChild(
                    Expanded().child(Row("row")
                                         .addChild(SizedBox("a").width(50).height(30))
                                         .addChild(Expanded().child(SizedBox("b"))))))))
            .layout();

        auto *col = screen.findByKey("col");
        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(col && row && a && b);

        expectSize(col, 200, 300);
        expectSize(row, 200, 300);
        expectSize(a, 50, 30);
        expectSize(b, 150, 0);
        expectOffset(a, row, 0, 135);
        expectOffset(b, row, 50, 150);
        expectTopLeft(col, 300, 150);
        expectTopLeft(row, 300, 150);
        expectTopLeft(a, 300, 285);
        expectTopLeft(b, 350, 300);
    }

    // B3. Row > Flexible(loose) + Expanded(tight)
    void testB3()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(400).height(100).child(
                Row("row")
                    .addChild(
                        Flexible().flex(1).child(SizedBox("a").width(100).height(30)))
                    .addChild(
                        Expanded().flex(1).child(SizedBox("b").width(50).height(30))))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(row && a && b);

        expectSize(row, 400, 100);
        expectSize(a, 100, 30);
        expectSize(b, 200, 30);
        expectOffset(a, row, 0, 35);
        expectOffset(b, row, 100, 35);
    }

    // B4. Row > Expanded > Row(min)
    void testB4()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(400).height(100).child(
                Row("outer").addChild(Expanded().child(
                    Row("inner")
                        .mainAxisSize(MainAxisSize::min)
                        .addChild(SizedBox("a").width(30).height(30))
                        .addChild(SizedBox("b").width(40).height(30)))))))
            .layout();

        auto *inner = screen.findByKey("inner");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(inner && a && b);

        expectSize(inner, 400, 30);
        expectSize(a, 30, 30);
        expectSize(b, 40, 30);
        expectOffset(a, inner, 0, 0);
        expectOffset(b, inner, 30, 0);
    }

    // B5. Column > Expanded > Column(min)
    void testB5()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(100).height(400).child(
                Column("outer").addChild(Expanded().child(
                    Column("inner")
                        .mainAxisSize(MainAxisSize::min)
                        .addChild(SizedBox("a").width(30).height(30))
                        .addChild(SizedBox("b").width(40).height(30)))))))
            .layout();

        auto *inner = screen.findByKey("inner");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(inner && a && b);

        expectSize(inner, 40, 400);
        expectSize(a, 30, 30);
        expectSize(b, 40, 30);
        expectOffset(a, inner, 5, 0); // R5：a 宽 30 在 inner 宽 40 中居中
        expectOffset(b, inner, 0, 30);
    }
} // namespace test_B

// =========================================================================
// C. 跨协议综合
// =========================================================================
namespace test_C
{
    // C1. IntrinsicHeight > Row(min)
    // ❌ 跳过：IntrinsicHeight 未实现

    // C2. RotatedBox(qt: 1) > Row(min)
    // ❌ 跳过：全局位置需要 paint transform（旋转矩阵），C++ 引擎只做 layout offset
    //    只测 row 相对 rb 的 layout offset 是可以做的，但用例里的 getTopLeft(a)、
    //    getTopLeft(b) 期望是旋转后的全局坐标，需要 paint transform 支持。

    // C3. Offstage(true) > Row(min)
    void testC3()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Offstage("o").offstage(true).child(
                Row("row")
                    .mainAxisSize(MainAxisSize::min)
                    .addChild(SizedBox("a").width(50).height(30))
                    .addChild(SizedBox("b").width(80).height(40)))))
            .layout();

        auto *o = screen.findByKey("o");
        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(o && row && a && b);

        expectSize(o, 0, 0);
        expectSize(row, 130, 40);
        expectOffset(row, o, 0, 0);
        expectOffset(a, row, 0, 5);
        expectOffset(b, row, 50, 0);
        expectTopLeft(o, 400, 300);
        expectTopLeft(a, 400, 305);
        expectTopLeft(b, 450, 300);
    }

    // C4. SizedOverflowBox > Row(min)
    // ❌ 跳过：SizedOverflowBox 未实现

    // C5. SizedBox(tight) > SizedOverflowBox > child
    // ❌ 跳过：SizedOverflowBox 未实现
} // namespace test_C

// =========================================================================
// D. Table 综合
// =========================================================================
// ❌ 全部跳过：Table 未实现（IntrinsicColumnWidth 需要基类 computeMaxIntrinsicWidth）

// =========================================================================
// E. 边界排查
// =========================================================================
namespace test_E
{
    // E1. 三层 Padding 累积
    void testE1()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(
                Padding("p1")
                    .padding(EdgeInsets::all(10))
                    .child(Padding("p2")
                               .padding(EdgeInsets::all(20))
                               .child(Padding("p3")
                                          .padding(EdgeInsets::all(30))
                                          .child(SizedBox("c").width(50).height(50))))))
            .layout();

        auto *p1 = screen.findByKey("p1");
        auto *p2 = screen.findByKey("p2");
        auto *p3 = screen.findByKey("p3");
        auto *c = screen.findByKey("c");
        assert(p1 && p2 && p3 && c);

        expectSize(p1, 170, 170);
        expectSize(p2, 150, 150);
        expectSize(p3, 110, 110);
        expectSize(c, 50, 50);
        expectOffset(p2, p1, 10, 10);
        expectOffset(p3, p2, 20, 20);
        expectOffset(c, p3, 30, 30);
        expectTopLeft(p1, 315, 215);
        expectTopLeft(c, 375, 275);
    }

    // E2. 多级 Expanded + 固定子节点
    void testE2()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(400).height(100).child(
                Row("row")
                    .addChild(Expanded().child(
                        Row("inner")
                            .crossAxisAlignment(CrossAxisAlignment::stretch)
                            .addChild(Expanded().child(SizedBox("a")))))
                    .addChild(SizedBox("b").width(100).height(50)))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *inner = screen.findByKey("inner");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(row && inner && a && b);

        expectSize(row, 400, 100);
        expectSize(inner, 300, 100);
        expectSize(a, 300, 100);
        expectSize(b, 100, 50);
        expectOffset(inner, row, 0, 0);
        expectOffset(a, inner, 0, 0);
        expectOffset(b, row, 300, 25);
        expectTopLeft(row, 200, 250);
        expectTopLeft(a, 200, 250);
        expectTopLeft(b, 500, 275);
    }

    // E3. 两层 IntrinsicWidth 嵌套
    // ❌ 跳过：IntrinsicWidth 未实现

    // E4. 零尺寸 child 在 Expanded 中传播
    void testE4()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(200).height(100).child(
                Row("row").addChild(Expanded().child(SizedBox("c").width(0).height(0))))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *c = screen.findByKey("c");
        assert(row && c);

        expectSize(c, 200, 0);
        expectOffset(c, row, 0, 50);
        expectTopLeft(row, 300, 250);
        expectTopLeft(c, 300, 300);
    }
} // namespace test_E

// =========================================================================
// F. 模糊点补充
// =========================================================================
namespace test_F
{
    // F1a. Container 无 child、只有 padding，loose 父约束
    void testF1a()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Container("c").padding(EdgeInsets::all(20))))
            .layout();

        auto *c = screen.findByKey("c");
        assert(c);
        expectSize(c, 800, 600);
        expectTopLeft(c, 0, 0);
    }

    // F1b. Container 无 child、只有 padding，tight 200x100
    void testF1b()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(200).height(100).child(
                Container("c").padding(EdgeInsets::all(20)))))
            .layout();

        auto *c = screen.findByKey("c");
        assert(c);
        expectSize(c, 200, 100);
        expectTopLeft(c, 300, 250);
    }

    // F2. Flexible(loose) 自然尺寸 == 分配空间
    void testF2()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(200).height(100).child(
                Row("row")
                    .addChild(Flexible().child(SizedBox("a").width(100).height(30)))
                    .addChild(Flexible().child(SizedBox("b").width(100).height(30))))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(row && a && b);

        expectSize(a, 100, 30);
        expectSize(b, 100, 30);
        expectOffset(a, row, 0, 35);
        expectOffset(b, row, 100, 35);
    }

    // F3. Spacer 无剩余空间
    void testF3()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(200).height(100).child(
                Row("row")
                    .addChild(SizedBox("a").width(100).height(30))
                    .addChild(Spacer("s"))
                    .addChild(SizedBox("b").width(100).height(30)))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *s = screen.findByKey("s");
        assert(row && s);

        expectSize(s, 0, 0);
        expectOffset(s, row, 100, 50);
    }

    // F4. Wrap 两行长度不同，alignment.center
    void testF4()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(150).height(200).child(
                Wrap("wrap")
                    .alignment(WrapAlignment::center)
                    .addChild(SizedBox("a").width(60).height(30))
                    .addChild(SizedBox("b").width(60).height(30))
                    .addChild(SizedBox("c").width(60).height(30)))))
            .layout();

        auto *wrap = screen.findByKey("wrap");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        auto *c = screen.findByKey("c");
        assert(wrap && a && b && c);

        expectOffset(a, wrap, 15, 0);
        expectOffset(b, wrap, 75, 0);
        expectOffset(c, wrap, 45, 30);
    }

    // F5. Stack 只有 positioned 子节点，loose 父约束
    void testF5()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Stack("stack").addChild(
                Positioned().left(10).top(20).width(50).height(30).child(SizedBox("a")))))
            .layout();

        auto *stack = screen.findByKey("stack");
        auto *a = screen.findByKey("a");
        assert(stack && a);

        expectSize(stack, 800, 600);
        expectSize(a, 50, 30);
        expectOffset(a, stack, 10, 20);
        expectTopLeft(stack, 0, 0);
    }

    // F6. Stack 只有 positioned，tight 200x100
    void testF6()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(
                SizedBox("box").width(200).height(100).child(Stack("stack").addChild(
                    Positioned().right(10).bottom(20).width(50).height(30).child(
                        SizedBox("a"))))))
            .layout();

        auto *stack = screen.findByKey("stack");
        auto *a = screen.findByKey("a");
        assert(stack && a);

        expectSize(stack, 200, 100);
        expectOffset(a, stack, 140, 50);
    }

    // F7. Offstage(true) > SizedBox(tight) > Row > Expanded
    void testF7()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Offstage("o").offstage(true).child(
                SizedBox("box").width(300).height(100).child(
                    Row("row")
                        .addChild(SizedBox("a").width(50).height(30))
                        .addChild(Expanded().child(SizedBox("oc").height(30)))
                        .addChild(SizedBox("b").width(50).height(30))))))
            .layout();

        auto *o = screen.findByKey("o");
        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *oc = screen.findByKey("oc");
        auto *b = screen.findByKey("b");
        assert(o && row && a && oc && b);

        expectSize(o, 0, 0);
        expectSize(row, 300, 100);
        expectSize(a, 50, 30);
        expectSize(oc, 200, 30);
        expectSize(b, 50, 30);
        expectOffset(row, o, 0, 0);
        expectOffset(a, row, 0, 35);
        expectOffset(oc, row, 50, 35);
        expectOffset(b, row, 250, 35);
    }

    // F8a. RotatedBox quarterTurns: -1（等价 3）
    void testF8a()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(RotatedBox("rb").quarterTurns(-1).child(
                SizedBox("c").width(100).height(50))))
            .layout();

        auto *rb = screen.findByKey("rb");
        auto *c = screen.findByKey("c");
        assert(rb && c);

        expectSize(rb, 50, 100);
        expectOffset(c, rb, 0, 100);
    }

    // F8b. RotatedBox quarterTurns: 4（等价 0）
    void testF8b()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(RotatedBox("rb").quarterTurns(4).child(
                SizedBox("c").width(100).height(50))))
            .layout();

        auto *rb = screen.findByKey("rb");
        auto *c = screen.findByKey("c");
        assert(rb && c);

        expectSize(rb, 100, 50);
        expectOffset(c, rb, 0, 0);
    }

    // F8c. RotatedBox quarterTurns: 5（等价 1）
    void testF8c()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(RotatedBox("rb").quarterTurns(5).child(
                SizedBox("c").width(100).height(50))))
            .layout();

        auto *rb = screen.findByKey("rb");
        auto *c = screen.findByKey("c");
        assert(rb && c);

        expectSize(rb, 50, 100);
        expectOffset(c, rb, 50, 0);
    }

    // F9. CustomSingleChildLayout 负 offset
    struct NegativeOffsetDelegate : SingleChildLayoutDelegate
    {
        Size getSize(BoxConstraints c) const override
        {
            return c.constrain(Size{200, 100});
        }
        BoxConstraints getConstraintsForChild(BoxConstraints) const override
        {
            return BoxConstraints{};
        }
        Offset getPositionForChild(Size, Size) const override
        {
            return {-20.0, -10.0};
        }
    };

    void testF9()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(CustomSingleChildLayout("cl")
                                     .delegate(std::make_unique<NegativeOffsetDelegate>())
                                     .child(SizedBox("c").width(50).height(30))))
            .layout();

        auto *cl = screen.findByKey("cl");
        auto *c = screen.findByKey("c");
        assert(cl && c);

        expectSize(cl, 200, 100);
        expectOffset(c, cl, -20, -10);
    }

    // F10. CustomMultiChildLayout 两个 child 同 offset
    struct SameOffsetDelegate : MultiChildLayoutDelegate
    {
        Size getSize(BoxConstraints c) const override
        {
            return c.constrain(Size{200, 100});
        }
        void performLayout(Size size, MultiChildLayoutContext &ctx) const override
        {
            for (auto *slot : {"a", "b"})
                if (ctx.hasChild(slot))
                {
                    ctx.layoutChild(slot, BoxConstraints::loose(size));
                    ctx.positionChild(slot, {0.0, 0.0});
                }
        }
    };

    void testF10()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(
                CustomMultiChildLayout("cml")
                    .delegate(std::make_unique<SameOffsetDelegate>())
                    .addChild(LayoutId("a").child(SizedBox("a").width(30).height(20)))
                    .addChild(LayoutId("b").child(SizedBox("b").width(40).height(25)))))
            .layout();

        auto *cml = screen.findByKey("cml");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(cml && a && b);

        expectSize(cml, 200, 100);
        expectOffset(a, cml, 0, 0);
        expectOffset(b, cml, 0, 0);
    }

    // F11. Align 无 child, tight 200x100, wf/hf 0.5
    void testF11()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(200).height(100).child(
                Align("al").widthFactor(0.5).heightFactor(0.5))))
            .layout();

        auto *al = screen.findByKey("al");
        assert(al);
        expectSize(al, 200, 100);
    }

    // F12. ConstrainedBox(loose 0..50 x 0..30) > child(100x80)
    void testF12()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(ConstrainedBox("cb")
                                     .constraints(BoxConstraints{0.0, 50.0, 0.0, 30.0})
                                     .child(SizedBox("c").width(100).height(80))))
            .layout();

        auto *cb = screen.findByKey("cb");
        auto *c = screen.findByKey("c");
        assert(cb && c);

        expectSize(cb, 50, 30);
        expectSize(c, 50, 30);
        expectOffset(c, cb, 0, 0);
    }

    // F13. UnconstrainedBox(constrainedAxis: horizontal)
    void testF13()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(100).height(100).child(
                UnconstrainedBox("ub")
                    .constrainedAxis(Axis::horizontal)
                    .child(SizedBox("c").width(200).height(50)))))
            .layout();

        auto *ub = screen.findByKey("ub");
        auto *c = screen.findByKey("c");
        assert(ub && c);

        expectSize(ub, 100, 100);
        expectSize(c, 100, 50);
        expectOffset(c, ub, 0, 25);
    }
} // namespace test_F

// =========================================================================
// main
// =========================================================================
void test_layout18_api()
{
    using namespace test_A;
    runStage("A1. SizedBox(tight) > ConstrainedBox > OverflowBox", testA1);
    runStage("A2. SizedBox(tight) > Align(wf/hf 0.5) > child", testA2);
    runStage("A3. FractionallySizedBox > AspectRatio > Container", testA3);
    std::cout << "[ SKIP ] A4. IntrinsicWidth > Padding > SizedBox  (未实现)"
              << std::endl;

    using namespace test_B;
    runStage("B1. Row > Expanded > Column.stretch", testB1);
    runStage("B2. Column > Expanded > Row", testB2);
    runStage("B3. Row > Flexible(loose) + Expanded(tight)", testB3);
    runStage("B4. Row > Expanded > Row(min)", testB4);
    runStage("B5. Column > Expanded > Column(min)", testB5);

    using namespace test_C;
    std::cout << "[ SKIP ] C1. IntrinsicHeight > Row(min)                    (未实现)"
              << std::endl;
    std::cout << "[ SKIP ] C2. RotatedBox(qt:1) > Row(min)                   (需 paint "
                 "transform)"
              << std::endl;
    runStage("C3. Offstage(true) > Row(min)", testC3);
    std::cout << "[ SKIP ] C4. SizedOverflowBox > Row(min)                   (未实现)"
              << std::endl;
    std::cout << "[ SKIP ] C5. SizedBox(tight) > SizedOverflowBox > child    (未实现)"
              << std::endl;

    std::cout << "[ SKIP ] D1~D6. Table 全部用例                              (未实现)"
              << std::endl;

    using namespace test_E;
    runStage("E1. 三层 Padding 累积", testE1);
    runStage("E2. 多级 Expanded + 固定子节点", testE2);
    std::cout << "[ SKIP ] E3. 两层 IntrinsicWidth 嵌套                      (未实现)"
              << std::endl;
    runStage("E4. 零尺寸 child 在 Expanded 中传播", testE4);

    using namespace test_F;
    runStage("F1a. Container 无 child, loose 父约束", testF1a);
    runStage("F1b. Container 无 child, tight 200x100", testF1b);
    runStage("F2. Flexible(loose) 自然尺寸 == 分配空间", testF2);
    runStage("F3. Spacer 无剩余空间", testF3);
    runStage("F4. Wrap 两行长度不同, alignment.center", testF4);
    runStage("F5. Stack 只有 positioned, loose 父约束", testF5);
    runStage("F6. Stack 只有 positioned, tight 200x100", testF6);
    runStage("F7. Offstage(true) > SizedBox > Row > Expanded", testF7);
    runStage("F8a. RotatedBox quarterTurns: -1", testF8a);
    runStage("F8b. RotatedBox quarterTurns: 4", testF8b);
    runStage("F8c. RotatedBox quarterTurns: 5", testF8c);
    runStage("F9. CustomSingleChildLayout 负 offset", testF9);
    runStage("F10. CustomMultiChildLayout 两个 child 同 offset", testF10);
    runStage("F11. Align 无 child, tight 200x100, wf/hf 0.5", testF11);
    runStage("F12. ConstrainedBox(loose) > child", testF12);
    runStage("F13. UnconstrainedBox(constrainedAxis: horizontal)", testF13);
}

int main()
try
{
    test_layout18_api();
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