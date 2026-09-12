
#include <cassert>
#include <cmath>
#include <iostream>
#include <exception>

#include "layout_engine.hpp"

// NOLINTBEGIN
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
// 1. Row 中的 Flexible
// =========================================================================
namespace test_Row_Flexible
{
    constexpr auto W = 800.0, H = 600.0;

    // 1.1 Flexible(loose): child keeps natural size
    void testLooseKeepsNatural()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(200).height(100).child(
                Row("row")
                    .addChild(SizedBox("a").width(50).height(30))
                    .addChild(Flexible().child(SizedBox("b").width(100).height(30))))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(row && a && b);

        expectSize(row, 200, 100);
        expectSize(a, 50, 30);
        expectSize(b, 100, 30);
        expectOffset(a, row, 0, 35);
        expectOffset(b, row, 50, 35);
        expectTopLeft(row, 300, 250);
        expectTopLeft(a, 300, 285);
        expectTopLeft(b, 350, 285);
    }

    // 1.2 Flexible(tight): child fills allocated space
    void testTightFills()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(200).height(100).child(
                Row("row")
                    .addChild(SizedBox("a").width(50).height(30))
                    .addChild(Flexible()
                                  .fit(FlexFit::tight)
                                  .child(SizedBox("b").width(100).height(30))))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(row && a && b);

        expectSize(row, 200, 100);
        expectSize(a, 50, 30);
        expectSize(b, 150, 30);
        expectOffset(a, row, 0, 35);
        expectOffset(b, row, 50, 35);
    }

    // 1.3 Two Flexible: flex factors divide remaining space
    void testTwoFlex()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(300).height(100).child(
                Row("row")
                    .addChild(SizedBox("a").width(60).height(30))
                    .addChild(
                        Flexible().flex(1).child(SizedBox("b").width(200).height(30)))
                    .addChild(
                        Flexible().flex(2).child(SizedBox("c").width(200).height(30))))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        auto *c = screen.findByKey("c");
        assert(row && a && b && c);

        expectSize(row, 300, 100);
        expectSize(a, 60, 30);
        expectSize(b, 80, 30);
        expectSize(c, 160, 30);
        expectOffset(a, row, 0, 35);
        expectOffset(b, row, 60, 35);
        expectOffset(c, row, 140, 35);
    }

    // 1.4 Mixed tight and loose Flexible
    void testMixedTightLoose()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(300).height(100).child(
                Row("row")
                    .addChild(SizedBox("a").width(60).height(30))
                    .addChild(Flexible()
                                  .flex(1)
                                  .fit(FlexFit::tight)
                                  .child(SizedBox("b").width(200).height(30)))
                    .addChild(Flexible()
                                  .flex(2)
                                  .fit(FlexFit::loose)
                                  .child(SizedBox("c").width(100).height(30))))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        auto *c = screen.findByKey("c");
        assert(row && a && b && c);

        expectSize(row, 300, 100);
        expectSize(a, 60, 30);
        expectSize(b, 80, 30);
        expectSize(c, 100, 30);
        expectOffset(a, row, 0, 35);
        expectOffset(b, row, 60, 35);
        expectOffset(c, row, 140, 35);
    }

    // 1.5 Flexible(loose): child smaller than allocated space
    void testLooseChildSmaller()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(300).height(100).child(
                Row("row")
                    .addChild(SizedBox("a").width(60).height(30))
                    .addChild(Flexible().child(SizedBox("b").width(40).height(30))))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(row && a && b);

        expectSize(row, 300, 100);
        expectSize(a, 60, 30);
        expectSize(b, 40, 30);
        expectOffset(a, row, 0, 35);
        expectOffset(b, row, 60, 35);
    }

    // 1.6 Flexible with flex: 0 is treated as inflexible
    void testFlexZero()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(300).height(100).child(
                Row("row")
                    .addChild(SizedBox("a").width(60).height(30))
                    .addChild(
                        Flexible().flex(0).child(SizedBox("b").width(100).height(30)))
                    .addChild(
                        Flexible().flex(1).child(SizedBox("c").width(200).height(30))))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        auto *c = screen.findByKey("c");
        assert(row && a && b && c);

        expectSize(row, 300, 100);
        expectSize(a, 60, 30);
        expectSize(b, 100, 30);
        expectSize(c, 140, 30);
        expectOffset(a, row, 0, 35);
        expectOffset(b, row, 60, 35);
        expectOffset(c, row, 160, 35);
    }

    // 1.7 Flexible under crossAxisAlignment.stretch
    void testStretch()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(200).height(100).child(
                Row("row")
                    .crossAxisAlignment(CrossAxisAlignment::stretch)
                    .addChild(SizedBox("a").width(50).height(30))
                    .addChild(Flexible().child(SizedBox("b").width(100).height(30))))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(row && a && b);

        expectSize(row, 200, 100);
        expectSize(a, 50, 100);
        expectSize(b, 100, 100);
        expectOffset(a, row, 0, 0);
        expectOffset(b, row, 50, 0);
    }

    // 1.8 Flexible along with mainAxisAlignment.center
    void testCenter()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(300).height(100).child(
                Row("row")
                    .mainAxisAlignment(MainAxisAlignment::center)
                    .addChild(SizedBox("a").width(60).height(30))
                    .addChild(Flexible().child(SizedBox("b").width(40).height(30))))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(row && a && b);

        expectSize(row, 300, 100);
        expectOffset(a, row, 100, 35);
        expectOffset(b, row, 160, 35);
    }
} // namespace test_Row_Flexible

// =========================================================================
// 2. Column 中的 Flexible
// =========================================================================
namespace test_Column_Flexible
{
    constexpr auto W = 800.0, H = 600.0;

    // 2.1 Flexible(loose): child keeps natural height
    void testLooseKeepsNatural()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(100).height(300).child(
                Column("col")
                    .addChild(SizedBox("a").width(30).height(60))
                    .addChild(Flexible().child(SizedBox("b").width(30).height(100))))))
            .layout();

        auto *col = screen.findByKey("col");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(col && a && b);

        expectSize(col, 100, 300);
        expectSize(a, 30, 60);
        expectSize(b, 30, 100);
        expectOffset(a, col, 35, 0);
        expectOffset(b, col, 35, 60);
        expectTopLeft(col, 350, 150);
    }

    // 2.2 Flexible(tight): child fills allocated height
    void testTightFills()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(100).height(300).child(
                Column("col")
                    .addChild(SizedBox("a").width(30).height(60))
                    .addChild(Flexible()
                                  .fit(FlexFit::tight)
                                  .child(SizedBox("b").width(30).height(100))))))
            .layout();

        auto *col = screen.findByKey("col");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(col && a && b);

        expectSize(col, 100, 300);
        expectSize(a, 30, 60);
        expectSize(b, 30, 240);
        expectOffset(a, col, 35, 0);
        expectOffset(b, col, 35, 60);
    }

    // 2.3 Two Flexible in Column
    void testTwoFlex()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(100).height(300).child(
                Column("col")
                    .addChild(SizedBox("a").width(30).height(60))
                    .addChild(
                        Flexible().flex(1).child(SizedBox("b").width(30).height(200)))
                    .addChild(
                        Flexible().flex(2).child(SizedBox("c").width(30).height(200))))))
            .layout();

        auto *col = screen.findByKey("col");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        auto *c = screen.findByKey("c");
        assert(col && a && b && c);

        expectSize(col, 100, 300);
        expectSize(a, 30, 60);
        expectSize(b, 30, 80);
        expectSize(c, 30, 160);
        expectOffset(a, col, 35, 0);
        expectOffset(b, col, 35, 60);
        expectOffset(c, col, 35, 140);
    }
} // namespace test_Column_Flexible

// =========================================================================
// 3. 边界
// =========================================================================
namespace test_Flexible_Edge
{
    constexpr auto W = 800.0, H = 600.0;

    // 3.1 Flexible(loose) with zero-size child
    void testLooseZero()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(200).height(100).child(
                Row("row").addChild(Flexible().child(SizedBox("b").width(0).height(0))))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *b = screen.findByKey("b");
        assert(row && b);

        expectSize(row, 200, 100);
        expectSize(b, 0, 0);
        expectOffset(b, row, 0, 50);
    }

    // 3.2 Flexible(tight) with zero-size child
    void testTightZero()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(200).height(100).child(
                Row("row").addChild(Flexible()
                                        .fit(FlexFit::tight)
                                        .child(SizedBox("b").width(0).height(0))))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *b = screen.findByKey("b");
        assert(row && b);

        expectSize(row, 200, 100);
        expectSize(b, 200, 0);
        expectOffset(b, row, 0, 50);
    }
} // namespace test_Flexible_Edge

// =========================================================================
// main
// =========================================================================
void test_Flexible_api()
{
    {
        using namespace test_Row_Flexible;
        runStage("Row Flexible(loose): child keeps natural size", testLooseKeepsNatural);
        runStage("Row Flexible(tight): child fills allocated space", testTightFills);
        runStage("Row Two Flexible: flex factors divide", testTwoFlex);
        runStage("Row Mixed tight and loose Flexible", testMixedTightLoose);
        runStage("Row Flexible(loose): child smaller", testLooseChildSmaller);
        runStage("Row Flexible flex: 0 is treated as inflexible", testFlexZero);
        runStage("Row Flexible under crossAxisAlignment.stretch", testStretch);
        runStage("Row Flexible along with mainAxisAlignment.center", testCenter);
    }

    {
        using namespace test_Column_Flexible;
        runStage("Column Flexible(loose): child keeps natural height",
                 testLooseKeepsNatural);
        runStage("Column Flexible(tight): child fills allocated height", testTightFills);
        runStage("Column Two Flexible: flex factors divide", testTwoFlex);
    }

    {
        using namespace test_Flexible_Edge;
        runStage("Flexible(loose) with zero-size child", testLooseZero);
        runStage("Flexible(tight) with zero-size child", testTightZero);
    }
}

int main()
try
{
    test_Flexible_api();
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