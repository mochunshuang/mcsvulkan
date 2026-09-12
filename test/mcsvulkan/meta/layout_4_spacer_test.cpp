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
// 1. Row 中的 Spacer
// =========================================================================
namespace test_Row_Spacer
{
    constexpr auto W = 800.0, H = 600.0;

    // 1.1 SizedBox + Spacer + SizedBox：Spacer 吃掉剩余
    void testBasic()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(300).height(100).child(
                Row("row")
                    .addChild(SizedBox("a").width(50).height(30))
                    .addChild(Spacer("s"))
                    .addChild(SizedBox("b").width(50).height(30)))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *s = screen.findByKey("s");
        auto *b = screen.findByKey("b");
        assert(row && a && s && b);

        expectSize(row, 300, 100);
        expectSize(a, 50, 30);
        expectSize(s, 200, 0);
        expectSize(b, 50, 30);

        expectOffset(a, row, 0, 35);
        expectOffset(s, row, 50, 50);
        expectOffset(b, row, 250, 35);
        expectTopLeft(row, 250, 250);
    }

    // 1.2 两个 Spacer flex 1:1：均分剩余
    void testTwoEqual()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(300).height(100).child(
                Row("row")
                    .addChild(Spacer("s1"))
                    .addChild(SizedBox("a").width(50).height(30))
                    .addChild(Spacer("s2")))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *s1 = screen.findByKey("s1");
        auto *s2 = screen.findByKey("s2");
        auto *a = screen.findByKey("a");
        assert(row && s1 && s2 && a);

        expectSize(s1, 125, 0);
        expectSize(s2, 125, 0);
        expectSize(a, 50, 30);
        expectOffset(s1, row, 0, 50);
        expectOffset(a, row, 125, 35);
        expectOffset(s2, row, 175, 50);
    }

    // 1.3 两个 Spacer flex 1:2：按比例分配
    void testTwoRatio()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(300).height(100).child(
                Row("row")
                    .addChild(Spacer("s1").flex(1))
                    .addChild(SizedBox("a").width(60).height(30))
                    .addChild(Spacer("s2").flex(2)))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *s1 = screen.findByKey("s1");
        auto *s2 = screen.findByKey("s2");
        auto *a = screen.findByKey("a");
        assert(row && s1 && s2 && a);

        expectSize(s1, 80, 0);
        expectSize(s2, 160, 0);
        expectOffset(s1, row, 0, 50);
        expectOffset(a, row, 80, 35);
        expectOffset(s2, row, 140, 50);
    }

    // 1.4 三个 SizedBox + 两个 Spacer
    void testThreeFixedTwoSpacers()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(450).height(100).child(
                Row("row")
                    .addChild(SizedBox("a").width(50).height(30))
                    .addChild(Spacer("s1").flex(1))
                    .addChild(SizedBox("b").width(50).height(30))
                    .addChild(Spacer("s2").flex(2))
                    .addChild(SizedBox("c").width(50).height(30)))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        auto *c = screen.findByKey("c");
        auto *s1 = screen.findByKey("s1");
        auto *s2 = screen.findByKey("s2");
        assert(row && a && b && c && s1 && s2);

        expectSize(a, 50, 30);
        expectSize(b, 50, 30);
        expectSize(c, 50, 30);
        expectSize(s1, 100, 0);
        expectSize(s2, 200, 0);

        expectOffset(a, row, 0, 35);
        expectOffset(s1, row, 50, 50);
        expectOffset(b, row, 150, 35);
        expectOffset(s2, row, 200, 50);
        expectOffset(c, row, 400, 35);
        expectTopLeft(row, 175, 250);
    }

    // 1.5 Spacer 与 mainAxisAlignment.spaceBetween：Spacer 先吃掉全部剩余
    void testWithSpaceBetween()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(300).height(100).child(
                Row("row")
                    .mainAxisAlignment(MainAxisAlignment::spaceBetween)
                    .addChild(SizedBox("a").width(50).height(30))
                    .addChild(Spacer("s"))
                    .addChild(SizedBox("b").width(50).height(30)))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *s = screen.findByKey("s");
        auto *b = screen.findByKey("b");
        assert(row && a && s && b);

        expectSize(s, 200, 0);
        expectOffset(a, row, 0, 35);
        expectOffset(s, row, 50, 50);
        expectOffset(b, row, 250, 35);
    }

    // 1.6 crossAxisAlignment.stretch：Spacer 交叉轴拉满
    void testStretch()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(300).height(100).child(
                Row("row")
                    .crossAxisAlignment(CrossAxisAlignment::stretch)
                    .addChild(SizedBox("a").width(50).height(30))
                    .addChild(Spacer("s"))
                    .addChild(SizedBox("b").width(50).height(30)))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *s = screen.findByKey("s");
        auto *b = screen.findByKey("b");
        assert(row && a && s && b);

        expectSize(a, 50, 100);
        expectSize(s, 200, 100);
        expectSize(b, 50, 100);
        expectOffset(a, row, 0, 0);
        expectOffset(s, row, 50, 0);
        expectOffset(b, row, 250, 0);
    }
} // namespace test_Row_Spacer

// =========================================================================
// 2. Column 中的 Spacer
// =========================================================================
namespace test_Column_Spacer
{
    constexpr auto W = 800.0, H = 600.0;

    // 2.1 SizedBox + Spacer + SizedBox
    void testBasic()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(100).height(300).child(
                Column("col")
                    .addChild(SizedBox("a").width(30).height(50))
                    .addChild(Spacer("s"))
                    .addChild(SizedBox("b").width(30).height(50)))))
            .layout();

        auto *col = screen.findByKey("col");
        auto *a = screen.findByKey("a");
        auto *s = screen.findByKey("s");
        auto *b = screen.findByKey("b");
        assert(col && a && s && b);

        expectSize(col, 100, 300);
        expectSize(a, 30, 50);
        expectSize(s, 0, 200);
        expectSize(b, 30, 50);

        expectOffset(a, col, 35, 0);
        expectOffset(s, col, 50, 50);
        expectOffset(b, col, 35, 250);
        expectTopLeft(col, 350, 150);
    }

    // 2.2 两个 Spacer flex 1:2
    void testTwoRatio()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(100).height(300).child(
                Column("col")
                    .addChild(Spacer("s1").flex(1))
                    .addChild(SizedBox("a").width(30).height(60))
                    .addChild(Spacer("s2").flex(2)))))
            .layout();

        auto *col = screen.findByKey("col");
        auto *s1 = screen.findByKey("s1");
        auto *s2 = screen.findByKey("s2");
        auto *a = screen.findByKey("a");
        assert(col && s1 && s2 && a);

        expectSize(s1, 0, 80);
        expectSize(s2, 0, 160);
        expectOffset(s1, col, 50, 0);
        expectOffset(a, col, 35, 80);
        expectOffset(s2, col, 50, 140);
    }

    // 2.3 Spacer 与 verticalDirection.up
    void testVerticalUp()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(100).height(300).child(
                Column("col")
                    .verticalDirection(VerticalDirection::up)
                    .addChild(SizedBox("a").width(30).height(50))
                    .addChild(Spacer("s"))
                    .addChild(SizedBox("b").width(30).height(50)))))
            .layout();

        auto *col = screen.findByKey("col");
        auto *a = screen.findByKey("a");
        auto *s = screen.findByKey("s");
        auto *b = screen.findByKey("b");
        assert(col && a && s && b);

        expectSize(s, 0, 200);
        expectOffset(a, col, 35, 250);
        expectOffset(s, col, 50, 50);
        expectOffset(b, col, 35, 0);
    }
} // namespace test_Column_Spacer

// =========================================================================
// 3. 边界
// =========================================================================
namespace test_Spacer_Edge
{
    constexpr auto W = 800.0, H = 600.0;

    // 3.1 Row 只有一个 Spacer
    void testOnlySpacer()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(300).height(100).child(
                Row("row").addChild(Spacer("s")))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *s = screen.findByKey("s");
        assert(row && s);

        expectSize(s, 300, 0);
        expectOffset(s, row, 0, 50);
    }

    // 3.2 Spacer 与 Expanded 混合
    void testSpacerExpandedMixed()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(300).height(100).child(
                Row("row")
                    .addChild(SizedBox("a").width(60).height(30))
                    .addChild(Spacer("s").flex(1))
                    .addChild(
                        Expanded().flex(1).child(SizedBox("b").width(10).height(30))))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *s = screen.findByKey("s");
        auto *b = screen.findByKey("b");
        assert(row && a && s && b);

        // 剩余 240，flex 1:1 -> 120 / 120
        expectSize(s, 120, 0);
        expectSize(b, 120, 30);
        expectOffset(a, row, 0, 35);
        expectOffset(s, row, 60, 50);
        expectOffset(b, row, 180, 35);
    }
} // namespace test_Spacer_Edge

// =========================================================================
// main
// =========================================================================
void test_Spacer_api()
{
    {
        using namespace test_Row_Spacer;
        runStage("Row Spacer: basic fixed+spacer+fixed", testBasic);
        runStage("Row Spacer: two equal flex 1:1", testTwoEqual);
        runStage("Row Spacer: two flex 1:2", testTwoRatio);
        runStage("Row Spacer: three fixed two spacers", testThreeFixedTwoSpacers);
        runStage("Row Spacer: with spaceBetween", testWithSpaceBetween);
        runStage("Row Spacer: with crossAxis.stretch", testStretch);
    }

    {
        using namespace test_Column_Spacer;
        runStage("Column Spacer: basic fixed+spacer+fixed", testBasic);
        runStage("Column Spacer: two flex 1:2", testTwoRatio);
        runStage("Column Spacer: with verticalDirection.up", testVerticalUp);
    }

    using namespace test_Spacer_Edge;
    runStage("Spacer edge: only spacer in Row", testOnlySpacer);
    runStage("Spacer edge: mixed with Expanded", testSpacerExpandedMixed);
}

int main()
try
{
    test_Spacer_api();
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