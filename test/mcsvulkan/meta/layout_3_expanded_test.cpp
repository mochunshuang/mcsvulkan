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
// 1. Expanded ≡ Flexible(tight)
// =========================================================================
namespace test_Expanded_Equiv
{
    constexpr auto W = 800.0, H = 600.0;

    // 单个 Expanded 填满剩余空间
    void testSingleExpanded()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(200).height(100).child(
                Row("row")
                    .addChild(SizedBox("a").width(50).height(30))
                    .addChild(Expanded().child(SizedBox("b").width(10).height(30))))))
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
        expectTopLeft(row, 300, 250);
    }

    // 与 Flexible(tight) 完全一致
    void testEquivFlexibleTight()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(200).height(100).child(
                Row("row")
                    .addChild(SizedBox("a").width(50).height(30))
                    .addChild(Flexible()
                                  .fit(FlexFit::tight)
                                  .child(SizedBox("b").width(10).height(30))))))
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
} // namespace test_Expanded_Equiv

// =========================================================================
// 2. Row 中的 Expanded
// =========================================================================
namespace test_Row_Expanded
{
    constexpr auto W = 800.0, H = 600.0;

    // 两个 Expanded flex 相同：均分
    void testTwoEqual()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(300).height(100).child(
                Row("row")
                    .addChild(Expanded().child(SizedBox("a").width(10).height(30)))
                    .addChild(Expanded().child(SizedBox("b").width(10).height(30))))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(row && a && b);

        expectSize(row, 300, 100);
        expectSize(a, 150, 30);
        expectSize(b, 150, 30);
        expectOffset(a, row, 0, 35);
        expectOffset(b, row, 150, 35);
    }

    // 两个 Expanded flex 1 / 2
    void testTwoRatio()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(300).height(100).child(
                Row("row")
                    .addChild(
                        Expanded().flex(1).child(SizedBox("a").width(10).height(30)))
                    .addChild(
                        Expanded().flex(2).child(SizedBox("b").width(10).height(30))))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(row && a && b);

        expectSize(a, 100, 30);
        expectSize(b, 200, 30);
        expectOffset(a, row, 0, 35);
        expectOffset(b, row, 100, 35);
    }

    // 三个 Expanded flex 1 / 2 / 3
    void testThreeRatio()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(600).height(100).child(
                Row("row")
                    .addChild(
                        Expanded().flex(1).child(SizedBox("a").width(10).height(30)))
                    .addChild(
                        Expanded().flex(2).child(SizedBox("b").width(10).height(30)))
                    .addChild(
                        Expanded().flex(3).child(SizedBox("c").width(10).height(30))))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        auto *c = screen.findByKey("c");
        assert(row && a && b && c);

        expectSize(a, 100, 30);
        expectSize(b, 200, 30);
        expectSize(c, 300, 30);
        expectOffset(a, row, 0, 35);
        expectOffset(b, row, 100, 35);
        expectOffset(c, row, 300, 35);
    }

    // 固定 + Expanded + 固定
    void testFixedExpandedFixed()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(300).height(100).child(
                Row("row")
                    .addChild(SizedBox("a").width(50).height(30))
                    .addChild(Expanded().child(SizedBox("b").width(10).height(30)))
                    .addChild(SizedBox("c").width(50).height(30)))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        auto *c = screen.findByKey("c");
        assert(row && a && b && c);

        expectSize(a, 50, 30);
        expectSize(b, 200, 30);
        expectSize(c, 50, 30);
        expectOffset(a, row, 0, 35);
        expectOffset(b, row, 50, 35);
        expectOffset(c, row, 250, 35);
    }

    // Expanded 与 Flexible(loose) 混合
    void testExpandedMixedLoose()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(300).height(100).child(
                Row("row")
                    .addChild(SizedBox("a").width(60).height(30))
                    .addChild(
                        Expanded().flex(1).child(SizedBox("b").width(10).height(30)))
                    .addChild(
                        Flexible().flex(2).child(SizedBox("c").width(100).height(30))))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        auto *c = screen.findByKey("c");
        assert(row && a && b && c);

        // 剩余 240 按 1:2 分 -> 80 / 160；b tight = 80；c loose 自然 100
        expectSize(a, 60, 30);
        expectSize(b, 80, 30);
        expectSize(c, 100, 30);
        expectOffset(a, row, 0, 35);
        expectOffset(b, row, 60, 35);
        expectOffset(c, row, 140, 35);
    }

    // Expanded flex: 0 等价于固定尺寸
    void testFlexZero()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(300).height(100).child(
                Row("row")
                    .addChild(SizedBox("a").width(60).height(30))
                    .addChild(
                        Expanded().flex(0).child(SizedBox("b").width(50).height(30)))
                    .addChild(
                        Expanded().flex(1).child(SizedBox("c").width(10).height(30))))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        auto *c = screen.findByKey("c");
        assert(row && a && b && c);

        // b flex:0 视为非 flex，自然 50；c 吃掉剩余 190
        expectSize(a, 60, 30);
        expectSize(b, 50, 30);
        expectSize(c, 190, 30);
        expectOffset(a, row, 0, 35);
        expectOffset(b, row, 60, 35);
        expectOffset(c, row, 110, 35);
    }

    // Expanded 在 mainAxisAlignment.center 下仍先吃掉剩余
    void testWithMainCenter()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(300).height(100).child(
                Row("row")
                    .mainAxisAlignment(MainAxisAlignment::center)
                    .addChild(SizedBox("a").width(60).height(30))
                    .addChild(Expanded().child(SizedBox("b").width(10).height(30))))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(row && a && b);

        // Expanded 吃掉全部剩余 240
        expectSize(a, 60, 30);
        expectSize(b, 240, 30);
        expectOffset(a, row, 0, 35);
        expectOffset(b, row, 60, 35);
    }

    // crossAxisAlignment.stretch 与 Expanded 组合
    void testWithCrossStretch()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(300).height(100).child(
                Row("row")
                    .crossAxisAlignment(CrossAxisAlignment::stretch)
                    .addChild(SizedBox("a").width(50).height(30))
                    .addChild(Expanded().child(SizedBox("b").width(10).height(30))))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(row && a && b);

        expectSize(a, 50, 100);
        expectSize(b, 250, 100);
        expectOffset(a, row, 0, 0);
        expectOffset(b, row, 50, 0);
    }
} // namespace test_Row_Expanded

// =========================================================================
// 3. Column 中的 Expanded
// =========================================================================
namespace test_Column_Expanded
{
    constexpr auto W = 800.0, H = 600.0;

    // 单个 Expanded 吃掉剩余高度
    void testSingleExpanded()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(100).height(300).child(
                Column("col")
                    .addChild(SizedBox("a").width(30).height(60))
                    .addChild(Expanded().child(SizedBox("b").width(30).height(10))))))
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
        expectTopLeft(col, 350, 150);
    }

    // 两个 Expanded flex 1 / 3
    void testTwoRatio()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(100).height(400).child(
                Column("col")
                    .addChild(
                        Expanded().flex(1).child(SizedBox("a").width(30).height(10)))
                    .addChild(
                        Expanded().flex(3).child(SizedBox("b").width(30).height(10))))))
            .layout();

        auto *col = screen.findByKey("col");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(col && a && b);

        expectSize(a, 30, 100);
        expectSize(b, 30, 300);
        expectOffset(a, col, 35, 0);
        expectOffset(b, col, 35, 100);
    }

    // 固定 + Expanded + 固定
    void testFixedExpandedFixed()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(100).height(300).child(
                Column("col")
                    .addChild(SizedBox("a").width(30).height(50))
                    .addChild(Expanded().child(SizedBox("b").width(30).height(10)))
                    .addChild(SizedBox("c").width(30).height(50)))))
            .layout();

        auto *col = screen.findByKey("col");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        auto *c = screen.findByKey("c");
        assert(col && a && b && c);

        expectSize(a, 30, 50);
        expectSize(b, 30, 200);
        expectSize(c, 30, 50);
        expectOffset(a, col, 35, 0);
        expectOffset(b, col, 35, 50);
        expectOffset(c, col, 35, 250);
    }
} // namespace test_Column_Expanded

// =========================================================================
// 4. 边界
// =========================================================================
namespace test_Expanded_Edge
{
    constexpr auto W = 800.0, H = 600.0;

    // Expanded 无兄弟节点：吃掉全部主轴空间
    void testOnlyChild()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(
                SizedBox("box").width(300).height(100).child(Row("row").addChild(
                    Expanded().child(SizedBox("a").width(10).height(10))))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        assert(row && a);

        expectSize(row, 300, 100);
        expectSize(a, 300, 10);
        expectOffset(a, row, 0, 45);
    }

    // Expanded 零尺寸 child
    void testZeroSizeChild()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(200).height(100).child(
                Row("row").addChild(Expanded().child(SizedBox("a").width(0).height(0))))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        assert(row && a);

        expectSize(a, 200, 0);
        expectOffset(a, row, 0, 50);
    }

    // 全部固定、无 Expanded：主轴上剩余空间按 alignment 处理
    void testNoExpanded()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(300).height(100).child(
                Row("row")
                    .mainAxisAlignment(MainAxisAlignment::end)
                    .addChild(SizedBox("a").width(50).height(30))
                    .addChild(SizedBox("b").width(50).height(30)))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(row && a && b);

        expectOffset(a, row, 200, 35);
        expectOffset(b, row, 250, 35);
    }
} // namespace test_Expanded_Edge

// =========================================================================
// main
// =========================================================================
void test_Expanded_api()
{
    {
        using namespace test_Expanded_Equiv;
        runStage("Expanded ≡ Flexible(tight): single", testSingleExpanded);
        runStage("Expanded ≡ Flexible(tight): equiv", testEquivFlexibleTight);
    }

    {
        using namespace test_Row_Expanded;
        runStage("Row Expanded: two equal", testTwoEqual);
        runStage("Row Expanded: flex 1/2", testTwoRatio);
        runStage("Row Expanded: flex 1/2/3", testThreeRatio);
        runStage("Row Expanded: fixed+expanded+fixed", testFixedExpandedFixed);
        runStage("Row Expanded mixed with loose", testExpandedMixedLoose);
        runStage("Row Expanded flex:0", testFlexZero);
        runStage("Row Expanded with mainAxisAlignment.center", testWithMainCenter);
        runStage("Row Expanded with crossAxisAlignment.stretch", testWithCrossStretch);
    }

    {
        using namespace test_Column_Expanded;
        runStage("Column Expanded: single", testSingleExpanded);
        runStage("Column Expanded: flex 1/3", testTwoRatio);
        runStage("Column Expanded: fixed+expanded+fixed", testFixedExpandedFixed);
    }

    using namespace test_Expanded_Edge;
    runStage("Expanded edge: only child", testOnlyChild);
    runStage("Expanded edge: zero-size child", testZeroSizeChild);
    runStage("Expanded edge: no expanded", testNoExpanded);
}

int main()
try
{
    test_Expanded_api();
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