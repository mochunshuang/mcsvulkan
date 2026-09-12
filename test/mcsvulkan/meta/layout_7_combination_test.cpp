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

// =========================================================================
// 通用常量
// =========================================================================
constexpr auto W = 800.0;
constexpr auto H = 600.0;

// =========================================================================
// 1. Row > SizedBox + Column(min, start)
//    Row 400x200，a 100x100，Column(min, start) 80x90
// =========================================================================
void test_Row_Column()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(SizedBox("box").width(400).height(200).child(
            Row("row")
                .addChild(SizedBox("a").width(100).height(100))
                .addChild(Column("col")
                              .mainAxisSize(MainAxisSize::min)
                              .crossAxisAlignment(CrossAxisAlignment::start)
                              .addChild(SizedBox("b1").width(80).height(30))
                              .addChild(SizedBox("b2").width(60).height(40))
                              .addChild(SizedBox("b3").width(40).height(20))))))
        .layout();

    auto *row = screen.findByKey("row");
    auto *a = screen.findByKey("a");
    auto *col = screen.findByKey("col");
    auto *b1 = screen.findByKey("b1");
    auto *b2 = screen.findByKey("b2");
    auto *b3 = screen.findByKey("b3");
    assert(row && a && col && b1 && b2 && b3);

    expectSize(row, 400, 200);
    expectSize(a, 100, 100);
    expectSize(col, 80, 90);
    expectSize(b1, 80, 30);
    expectSize(b2, 60, 40);
    expectSize(b3, 40, 20);

    // Row 交叉轴 center
    expectOffset(a, row, 0, 50);
    expectOffset(col, row, 100, 55);

    // Column crossAxisAlignment.start
    expectOffset(b1, col, 0, 0);
    expectOffset(b2, col, 0, 30);
    expectOffset(b3, col, 0, 70);

    // 绝对坐标
    expectTopLeft(row, 200, 200);
    expectTopLeft(a, 200, 250);
    expectTopLeft(col, 300, 255);
    expectTopLeft(b3, 300, 325);
}

// =========================================================================
// 2. Column > SizedBox + Row(min)
//    Column 200x400，a 100x80，Row(min) 110x40
// =========================================================================
void test_Column_Row()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(SizedBox("box").width(200).height(400).child(
            Column("col")
                .addChild(SizedBox("a").width(100).height(80))
                .addChild(Row("row")
                              .mainAxisSize(MainAxisSize::min)
                              .addChild(SizedBox("b1").width(50).height(30))
                              .addChild(SizedBox("b2").width(60).height(40))))))
        .layout();

    auto *col = screen.findByKey("col");
    auto *row = screen.findByKey("row");
    auto *a = screen.findByKey("a");
    auto *b1 = screen.findByKey("b1");
    auto *b2 = screen.findByKey("b2");
    assert(col && row && a && b1 && b2);

    expectSize(col, 200, 400);
    expectSize(row, 110, 40);
    expectSize(a, 100, 80);

    // Column crossAxisAlignment.center
    expectOffset(a, col, 50, 0);
    expectOffset(row, col, 45, 80);

    // Row crossAxisAlignment.center
    expectOffset(b1, row, 0, 5);
    expectOffset(b2, row, 50, 0);

    expectTopLeft(col, 300, 100);
    expectTopLeft(a, 350, 100);
    expectTopLeft(row, 345, 180);
    expectTopLeft(b2, 395, 180);
}

// =========================================================================
// 3. Row > SizedBox + Expanded(Column.stretch)
//    Row 400x200，a 80x100，Expanded -> Column tight 320x200
// =========================================================================
void test_Row_Expanded_Column_Stretch()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(SizedBox("box").width(400).height(200).child(
            Row("row")
                .addChild(SizedBox("a").width(80).height(100))
                .addChild(
                    Expanded().child(Column("col")
                                         .crossAxisAlignment(CrossAxisAlignment::stretch)
                                         .addChild(SizedBox("c1").height(30))
                                         .addChild(Expanded().child(SizedBox("c2")))
                                         .addChild(SizedBox("c3").height(30)))))))
        .layout();

    auto *row = screen.findByKey("row");
    auto *a = screen.findByKey("a");
    auto *col = screen.findByKey("col");
    auto *c1 = screen.findByKey("c1");
    auto *c2 = screen.findByKey("c2");
    auto *c3 = screen.findByKey("c3");
    assert(row && a && col && c1 && c2 && c3);

    expectSize(row, 400, 200);
    expectSize(a, 80, 100);
    expectSize(col, 320, 200);
    expectSize(c1, 320, 30);
    expectSize(c2, 320, 140);
    expectSize(c3, 320, 30);

    expectOffset(a, row, 0, 50);
    expectOffset(col, row, 80, 0);

    expectOffset(c1, col, 0, 0);
    expectOffset(c2, col, 0, 30);
    expectOffset(c3, col, 0, 170);

    expectTopLeft(row, 200, 200);
    expectTopLeft(col, 280, 200);
    expectTopLeft(c3, 280, 370);
}

// =========================================================================
// 4. Wrap > SizedBox + Row(min) + SizedBox（换行）
//    Wrap 200x200，a(80x50) + Row(min,80x20) + c(90x40) -> c 换行
// =========================================================================
void test_Wrap_Row_Wrap()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(SizedBox("box").width(200).height(200).child(
            Wrap("wrap")
                .addChild(SizedBox("a").width(80).height(50))
                .addChild(Row("row")
                              .mainAxisSize(MainAxisSize::min)
                              .addChild(SizedBox("b1").width(40).height(20))
                              .addChild(SizedBox("b2").width(40).height(20)))
                .addChild(SizedBox("c").width(90).height(40)))))
        .layout();

    auto *wrap = screen.findByKey("wrap");
    auto *a = screen.findByKey("a");
    auto *row = screen.findByKey("row");
    auto *b1 = screen.findByKey("b1");
    auto *b2 = screen.findByKey("b2");
    auto *c = screen.findByKey("c");
    assert(wrap && a && row && b1 && b2 && c);

    expectSize(wrap, 200, 200);
    expectSize(a, 80, 50);
    expectSize(row, 80, 20);
    expectSize(c, 90, 40);

    // 第一行 a + row；c 换行到第二行
    expectOffset(a, wrap, 0, 0);
    expectOffset(row, wrap, 80, 0);
    expectOffset(c, wrap, 0, 50);

    expectOffset(b1, row, 0, 0);
    expectOffset(b2, row, 40, 0);

    expectTopLeft(wrap, 300, 200);
    expectTopLeft(c, 300, 250);
}

// =========================================================================
// 5. Row > SizedBox + Expanded(Wrap)
//    Row 400x200，a 80x100，Expanded -> Wrap tight 320 x loose -> 320x100
// =========================================================================
void test_Row_Expanded_Wrap()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(SizedBox("box").width(400).height(200).child(
            Row("row")
                .addChild(SizedBox("a").width(80).height(100))
                .addChild(Expanded().child(
                    Wrap("wrap")
                        .addChild(SizedBox("w1").width(100).height(50))
                        .addChild(SizedBox("w2").width(100).height(50))
                        .addChild(SizedBox("w3").width(100).height(50))
                        .addChild(SizedBox("w4").width(100).height(50)))))))
        .layout();

    auto *row = screen.findByKey("row");
    auto *a = screen.findByKey("a");
    auto *wrap = screen.findByKey("wrap");
    auto *w1 = screen.findByKey("w1");
    auto *w2 = screen.findByKey("w2");
    auto *w3 = screen.findByKey("w3");
    auto *w4 = screen.findByKey("w4");
    assert(row && a && wrap && w1 && w2 && w3 && w4);

    expectSize(row, 400, 200);
    expectSize(a, 80, 100);
    expectSize(wrap, 320, 100);

    expectOffset(w1, wrap, 0, 0);
    expectOffset(w2, wrap, 100, 0);
    expectOffset(w3, wrap, 200, 0);
    expectOffset(w4, wrap, 0, 50);

    expectOffset(a, row, 0, 50);
    expectOffset(wrap, row, 80, 50);

    expectTopLeft(row, 200, 200);
    expectTopLeft(wrap, 280, 250);
    expectTopLeft(w3, 480, 250);
    expectTopLeft(w4, 280, 300);
}

// =========================================================================
// 6. Flex > SizedBox + Spacer + SizedBox + Expanded + SizedBox + SizedBox
//    Flex 400x100，剩余 160，flex 1:1 -> 80/80
// =========================================================================
void test_Flex_Spacer_Expanded()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(SizedBox("box").width(400).height(100).child(
            Flex("flex")
                .direction(Axis::horizontal)
                .addChild(SizedBox("a").width(60).height(30))
                .addChild(Spacer("s"))
                .addChild(SizedBox("b").width(60).height(30))
                .addChild(Expanded().child(SizedBox("e").width(10).height(10)))
                .addChild(SizedBox("c").width(60).height(30))
                .addChild(SizedBox("d").width(60).height(30)))))
        .layout();

    auto *flex = screen.findByKey("flex");
    auto *a = screen.findByKey("a");
    auto *s = screen.findByKey("s");
    auto *b = screen.findByKey("b");
    auto *e = screen.findByKey("e");
    auto *c = screen.findByKey("c");
    auto *d = screen.findByKey("d");
    assert(flex && a && s && b && e && c && d);

    expectSize(flex, 400, 100);
    expectSize(a, 60, 30);
    expectSize(s, 80, 0);
    expectSize(b, 60, 30);
    expectSize(e, 80, 10);
    expectSize(c, 60, 30);
    expectSize(d, 60, 30);

    expectOffset(a, flex, 0, 35);
    expectOffset(s, flex, 60, 50);
    expectOffset(b, flex, 140, 35);
    expectOffset(e, flex, 200, 45);
    expectOffset(c, flex, 280, 35);
    expectOffset(d, flex, 340, 35);
}

// =========================================================================
// 7. Padding > Row > SizedBox + Expanded(Column) + Padding(SizedBox)
// =========================================================================
void test_Padding_Row_Expanded_Column_Padding()
{
    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(SizedBox("box").width(500).height(300).child(
            Padding("p")
                .padding(EdgeInsetsGeometry::fromLTRB(20, 20, 20, 20))
                .child(
                    Row("row")
                        .addChild(SizedBox("a").width(100).height(100))
                        .addChild(Expanded().child(
                            Column("col")
                                .addChild(SizedBox("c1").width(80).height(40))
                                .addChild(Spacer("sp"))
                                .addChild(SizedBox("c2").width(80).height(40))))
                        .addChild(Padding("p2")
                                      .padding(EdgeInsetsGeometry::fromLTRB(10, 0, 0, 0))
                                      .child(SizedBox("b").width(50).height(50)))))))
        .layout();

    auto *p = screen.findByKey("p");
    auto *row = screen.findByKey("row");
    auto *a = screen.findByKey("a");
    auto *col = screen.findByKey("col");
    auto *c1 = screen.findByKey("c1");
    auto *sp = screen.findByKey("sp");
    auto *c2 = screen.findByKey("c2");
    auto *p2 = screen.findByKey("p2");
    auto *b = screen.findByKey("b");
    assert(p && row && a && col && c1 && sp && c2 && p2 && b);

    expectSize(p, 500, 300);
    expectSize(row, 460, 260);

    expectSize(a, 100, 100);
    expectSize(col, 300, 260);

    expectSize(c1, 80, 40);
    expectSize(sp, 0, 180); // 260 - 40 - 40
    expectSize(c2, 80, 40);

    // Row 内位置
    expectOffset(a, row, 0, 80);
    expectOffset(col, row, 100, 0);
    expectOffset(p2, row, 400, 105);

    // Column crossAxisAlignment.center
    expectOffset(c1, col, 110, 0);
    expectOffset(sp, col, 150, 40);
    expectOffset(c2, col, 110, 220);

    // Padding 内 b
    expectSize(b, 50, 50);
    expectOffset(b, p2, 10, 0);

    // 绝对坐标
    expectTopLeft(p, 150, 150);
    expectTopLeft(row, 170, 170);
    expectTopLeft(a, 170, 250);
    expectTopLeft(col, 270, 170);
    expectTopLeft(c1, 380, 170);
    expectTopLeft(c2, 380, 390);
    expectTopLeft(p2, 570, 275);
    expectTopLeft(b, 580, 275);
}

// =========================================================================
// main
// =========================================================================
void test_Combination_api()
{
    runStage("Row > SizedBox + Column(min, start)", test_Row_Column);
    runStage("Column > SizedBox + Row(min)", test_Column_Row);
    runStage("Row > SizedBox + Expanded(Column.stretch)",
             test_Row_Expanded_Column_Stretch);
    runStage("Wrap > SizedBox + Row(min) + SizedBox", test_Wrap_Row_Wrap);
    runStage("Row > SizedBox + Expanded(Wrap)", test_Row_Expanded_Wrap);
    runStage("Flex > SizedBox + Spacer + SizedBox + Expanded + SizedBox + SizedBox",
             test_Flex_Spacer_Expanded);
    runStage("Padding > Row > SizedBox + Expanded(Column) + Padding(SizedBox)",
             test_Padding_Row_Expanded_Column_Padding);
}

int main()
try
{
    test_Combination_api();
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