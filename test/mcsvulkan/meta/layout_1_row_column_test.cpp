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
// Row 测试组
// 子节点：a=50x30, b=70x40。总主轴 120，交叉轴最大 40。
// =========================================================================
namespace test_Row
{
    constexpr auto W = 800.0;
    constexpr auto H = 600.0;

    // Dart: 'Row: default, mainAxisSize.max'
    void testDefault()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Row("row")
                                     .addChild(SizedBox("a").width(50).height(30))
                                     .addChild(SizedBox("b").width(70).height(40))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(row && a && b);

        expectSize(row, 800, 40);
        expectSize(a, 50, 30);
        expectSize(b, 70, 40);
        expectOffset(a, row, 0, 5);
        expectOffset(b, row, 50, 0);
        expectTopLeft(row, 0, 280);
    }

    // Dart: 'Row: mainAxisSize.min'
    void testMin()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Row("row")
                                     .mainAxisSize(MainAxisSize::min)
                                     .addChild(SizedBox("a").width(50).height(30))
                                     .addChild(SizedBox("b").width(70).height(40))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(row && a && b);

        expectSize(row, 120, 40);
        expectOffset(a, row, 0, 5);
        expectOffset(b, row, 50, 0);
        expectTopLeft(row, 340, 280);
    }

    // Dart: 'Row: mainAxisAlignment.center'
    void testCenter()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Row("row")
                                     .mainAxisAlignment(MainAxisAlignment::center)
                                     .addChild(SizedBox("a").width(50).height(30))
                                     .addChild(SizedBox("b").width(70).height(40))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(row && a && b);

        expectSize(row, 800, 40);
        expectOffset(a, row, 340, 5);
        expectOffset(b, row, 390, 0);
    }

    // Dart: 'Row: mainAxisAlignment.end'
    void testEnd()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Row("row")
                                     .mainAxisAlignment(MainAxisAlignment::end)
                                     .addChild(SizedBox("a").width(50).height(30))
                                     .addChild(SizedBox("b").width(70).height(40))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(row && a && b);

        expectSize(row, 800, 40);
        expectOffset(a, row, 680, 5);
        expectOffset(b, row, 730, 0);
    }

    // Dart: 'Row: mainAxisAlignment.spaceBetween'
    void testSpaceBetween()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Row("row")
                                     .mainAxisAlignment(MainAxisAlignment::spaceBetween)
                                     .addChild(SizedBox("a").width(50).height(30))
                                     .addChild(SizedBox("b").width(70).height(40))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(row && a && b);

        expectSize(row, 800, 40);
        expectOffset(a, row, 0, 5);
        expectOffset(b, row, 730, 0);
    }

    // Dart: 'Row: crossAxisAlignment.start'
    void testCrossStart()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Row("row")
                                     .crossAxisAlignment(CrossAxisAlignment::start)
                                     .addChild(SizedBox("a").width(50).height(30))
                                     .addChild(SizedBox("b").width(70).height(40))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(row && a && b);

        expectSize(row, 800, 40);
        expectOffset(a, row, 0, 0);
        expectOffset(b, row, 50, 0);
    }

    // Dart: 'Row: crossAxisAlignment.end'
    void testCrossEnd()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Row("row")
                                     .crossAxisAlignment(CrossAxisAlignment::end)
                                     .addChild(SizedBox("a").width(50).height(30))
                                     .addChild(SizedBox("b").width(70).height(40))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(row && a && b);

        expectSize(row, 800, 40);
        expectOffset(a, row, 0, 10);
        expectOffset(b, row, 50, 0);
    }

    // Dart: 'Row: crossAxisAlignment.stretch'
    void testCrossStretch()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Row("row")
                                     .crossAxisAlignment(CrossAxisAlignment::stretch)
                                     .addChild(SizedBox("a").width(50).height(30))
                                     .addChild(SizedBox("b").width(70).height(40))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(row && a && b);

        expectSize(row, 800, 600);
        expectSize(a, 50, 600);
        expectSize(b, 70, 600);
        expectOffset(a, row, 0, 0);
        expectOffset(b, row, 50, 0);
        expectTopLeft(row, 0, 0);
    }

    // Dart: 'Row: textDirection.rtl, mainAxisAlignment.start'
    void testRtl()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Row("row")
                                     .textDirection(TextDirection::rtl)
                                     .addChild(SizedBox("a").width(50).height(30))
                                     .addChild(SizedBox("b").width(70).height(40))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(row && a && b);

        expectSize(row, 800, 40);
        expectOffset(a, row, 750, 5);
        expectOffset(b, row, 680, 0);
    }

    // Dart: 'Row: empty children, mainAxisSize.max'
    void testEmptyMax()
    {
        ScreenWidget screen{};
        screen.size(W, H).root(Center().child(Row("row"))).layout();

        auto *row = screen.findByKey("row");
        assert(row);

        expectSize(row, 800, 0);
        expectTopLeft(row, 0, 300);
    }

    // Dart: 'Row: empty children, mainAxisSize.min'
    void testEmptyMin()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Row("row").mainAxisSize(MainAxisSize::min)))
            .layout();

        auto *row = screen.findByKey("row");
        assert(row);

        expectSize(row, 0, 0);
        expectTopLeft(row, 400, 300);
    }
} // namespace test_Row

// =========================================================================
// Column 测试组
// 子节点：a=30x50, b=40x70。总主轴 120，交叉轴最大 40。
// =========================================================================
namespace test_Column
{
    constexpr auto W = 800.0;
    constexpr auto H = 600.0;

    // Dart: 'Column: default, mainAxisSize.max'
    void testDefault()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Column("col")
                                     .addChild(SizedBox("a").width(30).height(50))
                                     .addChild(SizedBox("b").width(40).height(70))))
            .layout();

        auto *col = screen.findByKey("col");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(col && a && b);

        expectSize(col, 40, 600);
        expectSize(a, 30, 50);
        expectSize(b, 40, 70);
        expectOffset(a, col, 5, 0);
        expectOffset(b, col, 0, 50);
        expectTopLeft(col, 380, 0);
    }

    // Dart: 'Column: mainAxisSize.min'
    void testMin()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Column("col")
                                     .mainAxisSize(MainAxisSize::min)
                                     .addChild(SizedBox("a").width(30).height(50))
                                     .addChild(SizedBox("b").width(40).height(70))))
            .layout();

        auto *col = screen.findByKey("col");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(col && a && b);

        expectSize(col, 40, 120);
        expectOffset(a, col, 5, 0);
        expectOffset(b, col, 0, 50);
        expectTopLeft(col, 380, 240);
    }

    // Dart: 'Column: mainAxisAlignment.center'
    void testCenter()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Column("col")
                                     .mainAxisAlignment(MainAxisAlignment::center)
                                     .addChild(SizedBox("a").width(30).height(50))
                                     .addChild(SizedBox("b").width(40).height(70))))
            .layout();

        auto *col = screen.findByKey("col");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(col && a && b);

        expectSize(col, 40, 600);
        expectOffset(a, col, 5, 240);
        expectOffset(b, col, 0, 290);
    }

    // Dart: 'Column: mainAxisAlignment.end'
    void testEnd()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Column("col")
                                     .mainAxisAlignment(MainAxisAlignment::end)
                                     .addChild(SizedBox("a").width(30).height(50))
                                     .addChild(SizedBox("b").width(40).height(70))))
            .layout();

        auto *col = screen.findByKey("col");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(col && a && b);

        expectSize(col, 40, 600);
        expectOffset(a, col, 5, 480);
        expectOffset(b, col, 0, 530);
    }

    // Dart: 'Column: mainAxisAlignment.spaceBetween'
    void testSpaceBetween()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Column("col")
                                     .mainAxisAlignment(MainAxisAlignment::spaceBetween)
                                     .addChild(SizedBox("a").width(30).height(50))
                                     .addChild(SizedBox("b").width(40).height(70))))
            .layout();

        auto *col = screen.findByKey("col");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(col && a && b);

        expectSize(col, 40, 600);
        expectOffset(a, col, 5, 0);
        expectOffset(b, col, 0, 530);
    }

    // Dart: 'Column: crossAxisAlignment.start'
    void testCrossStart()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Column("col")
                                     .crossAxisAlignment(CrossAxisAlignment::start)
                                     .addChild(SizedBox("a").width(30).height(50))
                                     .addChild(SizedBox("b").width(40).height(70))))
            .layout();

        auto *col = screen.findByKey("col");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(col && a && b);

        expectSize(col, 40, 600);
        expectOffset(a, col, 0, 0);
        expectOffset(b, col, 0, 50);
    }

    // Dart: 'Column: crossAxisAlignment.end'
    void testCrossEnd()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Column("col")
                                     .crossAxisAlignment(CrossAxisAlignment::end)
                                     .addChild(SizedBox("a").width(30).height(50))
                                     .addChild(SizedBox("b").width(40).height(70))))
            .layout();

        auto *col = screen.findByKey("col");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(col && a && b);

        expectSize(col, 40, 600);
        expectOffset(a, col, 10, 0);
        expectOffset(b, col, 0, 50);
    }

    // Dart: 'Column: crossAxisAlignment.stretch'
    void testCrossStretch()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Column("col")
                                     .crossAxisAlignment(CrossAxisAlignment::stretch)
                                     .addChild(SizedBox("a").width(30).height(50))
                                     .addChild(SizedBox("b").width(40).height(70))))
            .layout();

        auto *col = screen.findByKey("col");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(col && a && b);

        expectSize(col, 800, 600);
        expectSize(a, 800, 50);
        expectSize(b, 800, 70);
        expectOffset(a, col, 0, 0);
        expectOffset(b, col, 0, 50);
        expectTopLeft(col, 0, 0);
    }

    // Dart: 'Column: verticalDirection.up, mainAxisAlignment.start'
    void testVerticalUp()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Column("col")
                                     .verticalDirection(VerticalDirection::up)
                                     .addChild(SizedBox("a").width(30).height(50))
                                     .addChild(SizedBox("b").width(40).height(70))))
            .layout();

        auto *col = screen.findByKey("col");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(col && a && b);

        expectSize(col, 40, 600);
        expectOffset(a, col, 5, 550);
        expectOffset(b, col, 0, 480);
    }

    // Dart: 'Column: empty children, mainAxisSize.max'
    void testEmptyMax()
    {
        ScreenWidget screen{};
        screen.size(W, H).root(Center().child(Column("col"))).layout();

        auto *col = screen.findByKey("col");
        assert(col);

        expectSize(col, 0, 600);
        expectTopLeft(col, 400, 0);
    }

    // Dart: 'Column: empty children, mainAxisSize.min'
    void testEmptyMin()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Column("col").mainAxisSize(MainAxisSize::min)))
            .layout();

        auto *col = screen.findByKey("col");
        assert(col);

        expectSize(col, 0, 0);
        expectTopLeft(col, 400, 300);
    }
} // namespace test_Column

void test_Row_Column_api()
{
    {
        using namespace test_Row;
        runStage("Row: default", testDefault);
        runStage("Row: mainAxisSize.min", testMin);
        runStage("Row: mainAxisAlignment.center", testCenter);
        runStage("Row: mainAxisAlignment.end", testEnd);
        runStage("Row: mainAxisAlignment.spaceBetween", testSpaceBetween);
        runStage("Row: crossAxisAlignment.start", testCrossStart);
        runStage("Row: crossAxisAlignment.end", testCrossEnd);
        runStage("Row: crossAxisAlignment.stretch", testCrossStretch);
        runStage("Row: textDirection.rtl", testRtl);
        runStage("Row: empty max", testEmptyMax);
        runStage("Row: empty min", testEmptyMin);
    }

    {
        using namespace test_Column;
        runStage("Column: default", testDefault);
        runStage("Column: mainAxisSize.min", testMin);
        runStage("Column: mainAxisAlignment.center", testCenter);
        runStage("Column: mainAxisAlignment.end", testEnd);
        runStage("Column: mainAxisAlignment.spaceBetween", testSpaceBetween);
        runStage("Column: crossAxisAlignment.start", testCrossStart);
        runStage("Column: crossAxisAlignment.end", testCrossEnd);
        runStage("Column: crossAxisAlignment.stretch", testCrossStretch);
        runStage("Column: verticalDirection.up", testVerticalUp);
        runStage("Column: empty max", testEmptyMax);
        runStage("Column: empty min", testEmptyMin);
    }
}

int main()
try
{
    test_Row_Column_api();
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