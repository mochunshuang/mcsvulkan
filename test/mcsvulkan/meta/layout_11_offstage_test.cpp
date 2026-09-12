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
// 1. offstage = true
// =========================================================================
namespace test_Offstage_True
{
    void testLooseParent()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(
                Offstage("o").offstage(true).child(SizedBox("c").width(100).height(80))))
            .layout();

        auto *o = screen.findByKey("o");
        auto *c = screen.findByKey("c");
        assert(o && c);

        expectSize(o, 0, 0);
        expectSize(c, 100, 80);
        expectOffset(c, o, 0, 0);
        expectTopLeft(o, 400, 300);
        expectTopLeft(c, 400, 300);
    }

    void testTightParent()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(200).height(100).child(
                Offstage("o").offstage(true).child(SizedBox("c").width(100).height(80)))))
            .layout();

        auto *o = screen.findByKey("o");
        auto *c = screen.findByKey("c");
        assert(o && c);

        expectSize(o, 200, 100);
        expectSize(c, 200, 100);
        expectOffset(c, o, 0, 0);
        expectTopLeft(o, 300, 250);
    }

    void testLooseContainerExpand()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Offstage("o").offstage(true).child(Container("c"))))
            .layout();

        auto *o = screen.findByKey("o");
        auto *c = screen.findByKey("c");
        assert(o && c);

        expectSize(o, 0, 0);
        expectSize(c, 800, 600);
        expectOffset(c, o, 0, 0);
        expectTopLeft(o, 400, 300);
        expectTopLeft(c, 400, 300);
    }

    void testInRow()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(300).height(100).child(
                Row("row")
                    .addChild(SizedBox("a").width(50).height(30))
                    .addChild(Offstage("o").offstage(true).child(
                        SizedBox("oc").width(100).height(30)))
                    .addChild(SizedBox("b").width(50).height(30)))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *o = screen.findByKey("o");
        auto *oc = screen.findByKey("oc");
        auto *b = screen.findByKey("b");
        assert(row && a && o && oc && b);

        expectSize(o, 0, 0);
        expectSize(oc, 100, 30);

        expectOffset(a, row, 0, 35);
        expectOffset(o, row, 50, 50);
        expectOffset(b, row, 50, 35);
        expectOffset(oc, o, 0, 0);

        expectTopLeft(row, 250, 250);
        expectTopLeft(o, 300, 300);
        expectTopLeft(oc, 300, 300);
        expectTopLeft(b, 300, 285);
    }

    void testInColumn()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(100).height(300).child(
                Column("col")
                    .addChild(SizedBox("a").width(30).height(50))
                    .addChild(Offstage("o").offstage(true).child(
                        SizedBox("oc").width(30).height(100)))
                    .addChild(SizedBox("b").width(30).height(50)))))
            .layout();

        auto *col = screen.findByKey("col");
        auto *a = screen.findByKey("a");
        auto *o = screen.findByKey("o");
        auto *oc = screen.findByKey("oc");
        auto *b = screen.findByKey("b");
        assert(col && a && o && oc && b);

        expectSize(o, 0, 0);
        expectSize(oc, 30, 100);

        expectOffset(a, col, 35, 0);
        expectOffset(o, col, 50, 50);
        expectOffset(b, col, 35, 50);
        expectOffset(oc, o, 0, 0);

        expectTopLeft(col, 350, 150);
        expectTopLeft(o, 400, 200);
        expectTopLeft(oc, 400, 200);
        expectTopLeft(b, 385, 200);
    }
} // namespace test_Offstage_True

// =========================================================================
// 2. offstage = false
// =========================================================================
namespace test_Offstage_False
{
    void testLooseParent()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(
                Offstage("o").offstage(false).child(SizedBox("c").width(100).height(80))))
            .layout();

        auto *o = screen.findByKey("o");
        auto *c = screen.findByKey("c");
        assert(o && c);

        expectSize(o, 100, 80);
        expectSize(c, 100, 80);
        expectOffset(c, o, 0, 0);
        expectTopLeft(o, 350, 260);
        expectTopLeft(c, 350, 260);
    }

    void testTightParent()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(200).height(100).child(
                Offstage("o").offstage(false).child(
                    SizedBox("c").width(100).height(80)))))
            .layout();

        auto *o = screen.findByKey("o");
        auto *c = screen.findByKey("c");
        assert(o && c);

        expectSize(o, 200, 100);
        expectSize(c, 200, 100);
        expectOffset(c, o, 0, 0);
        expectTopLeft(o, 300, 250);
    }

    void testLooseContainerExpand()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Offstage("o").offstage(false).child(Container("c"))))
            .layout();

        auto *o = screen.findByKey("o");
        auto *c = screen.findByKey("c");
        assert(o && c);

        expectSize(o, 800, 600);
        expectSize(c, 800, 600);
        expectOffset(c, o, 0, 0);
        expectTopLeft(o, 0, 0);
    }

    void testInRow()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(300).height(100).child(
                Row("row")
                    .addChild(SizedBox("a").width(50).height(30))
                    .addChild(Offstage("o").offstage(false).child(
                        SizedBox("oc").width(100).height(30)))
                    .addChild(SizedBox("b").width(50).height(30)))))
            .layout();

        auto *row = screen.findByKey("row");
        auto *a = screen.findByKey("a");
        auto *o = screen.findByKey("o");
        auto *b = screen.findByKey("b");
        assert(row && a && o && b);

        expectSize(o, 100, 30);

        expectOffset(a, row, 0, 35);
        expectOffset(o, row, 50, 35);
        expectOffset(b, row, 150, 35);

        expectTopLeft(o, 300, 285);
        expectTopLeft(b, 400, 285);
    }
} // namespace test_Offstage_False

// =========================================================================
// 3. 边界：无 child
// =========================================================================
namespace test_Offstage_Edge
{
    void testOffstageTrueNoChild()
    {
        ScreenWidget screen{};
        screen.size(W, H).root(Center().child(Offstage("o").offstage(true))).layout();

        auto *o = screen.findByKey("o");
        assert(o);
        expectSize(o, 0, 0);
        expectTopLeft(o, 400, 300);
    }

    void testOffstageFalseNoChild()
    {
        ScreenWidget screen{};
        screen.size(W, H).root(Center().child(Offstage("o").offstage(false))).layout();

        auto *o = screen.findByKey("o");
        assert(o);
        expectSize(o, 0, 0);
        expectTopLeft(o, 400, 300);
    }

    void testOffstageTrueNoChildTight()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(200).height(100).child(
                Offstage("o").offstage(true))))
            .layout();

        auto *o = screen.findByKey("o");
        assert(o);
        expectSize(o, 200, 100);
        expectTopLeft(o, 300, 250);
    }

    void testOffstageFalseNoChildTight()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(200).height(100).child(
                Offstage("o").offstage(false))))
            .layout();

        auto *o = screen.findByKey("o");
        assert(o);
        expectSize(o, 200, 100);
        expectTopLeft(o, 300, 250);
    }
} // namespace test_Offstage_Edge

// =========================================================================
// main
// =========================================================================
void test_Offstage_api()
{

    {
        using namespace test_Offstage_True;
        runStage("Offstage=true: loose parent", testLooseParent);
        runStage("Offstage=true: tight parent", testTightParent);
        runStage("Offstage=true: loose Container", testLooseContainerExpand);
        runStage("Offstage=true: in Row", testInRow);
        runStage("Offstage=true: in Column", testInColumn);
    }

    {
        using namespace test_Offstage_False;
        runStage("Offstage=false: loose parent", testLooseParent);
        runStage("Offstage=false: tight parent", testTightParent);
        runStage("Offstage=false: loose Container", testLooseContainerExpand);
        runStage("Offstage=false: in Row", testInRow);
    }

    using namespace test_Offstage_Edge;
    runStage("Offstage edge: true no child", testOffstageTrueNoChild);
    runStage("Offstage edge: false no child", testOffstageFalseNoChild);
    runStage("Offstage edge: true no child tight", testOffstageTrueNoChildTight);
    runStage("Offstage edge: false no child tight", testOffstageFalseNoChildTight);
}

int main()
try
{
    test_Offstage_api();
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