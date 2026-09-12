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
    return std::abs(a - b) < 1e-6;
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
// 1. loose 父约束，child 100x50
// =========================================================================
namespace test_Loose
{
    void check(int qt, Size rbSize, Offset childOffset, Offset rbTopLeft,
               Offset childTopLeft)
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(RotatedBox("rb").quarterTurns(qt).child(
                SizedBox("child").width(100).height(50))))
            .layout();

        auto *rb = screen.findByKey("rb");
        auto *child = screen.findByKey("child");
        assert(rb && child);

        expectSize(rb, rbSize.width, rbSize.height);
        expectSize(child, 100, 50);
        expectOffset(child, rb, childOffset.x, childOffset.y);
        expectTopLeft(rb, rbTopLeft.x, rbTopLeft.y);
        expectTopLeft(child, childTopLeft.x, childTopLeft.y);
    }

    void testQt0()
    {
        check(0, {100, 50}, {0, 0}, {350, 275}, {350, 275});
    }

    void testQt1()
    {
        check(1, {50, 100}, {50, 0}, {375, 250}, {425, 250});
    }

    void testQt2()
    {
        check(2, {100, 50}, {100, 50}, {350, 275}, {450, 325});
    }

    void testQt3()
    {
        check(3, {50, 100}, {0, 100}, {375, 250}, {375, 350});
    }
} // namespace test_Loose

// =========================================================================
// 2. tight 父约束 200x100
//    child 收到 tight（或 flipped tight），被强制
// =========================================================================
namespace test_Tight
{
    void check(int qt, Size rbSize, Size childSize, Offset childOffset, Offset rbTopLeft,
               Offset childTopLeft)
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(200).height(100).child(
                RotatedBox("rb").quarterTurns(qt).child(
                    SizedBox("child").width(50).height(30)))))
            .layout();

        auto *rb = screen.findByKey("rb");
        auto *child = screen.findByKey("child");
        assert(rb && child);

        expectSize(rb, rbSize.width, rbSize.height);
        expectSize(child, childSize.width, childSize.height);
        expectOffset(child, rb, childOffset.x, childOffset.y);
        expectTopLeft(rb, rbTopLeft.x, rbTopLeft.y);
        expectTopLeft(child, childTopLeft.x, childTopLeft.y);
    }

    void testQt0()
    {
        check(0, {200, 100}, {200, 100}, {0, 0}, {300, 250}, {300, 250});
    }

    void testQt1()
    {
        check(1, {200, 100}, {100, 200}, {200, 0}, {300, 250}, {500, 250});
    }

    void testQt2()
    {
        check(2, {200, 100}, {200, 100}, {200, 100}, {300, 250}, {500, 350});
    }

    void testQt3()
    {
        check(3, {200, 100}, {100, 200}, {0, 100}, {300, 250}, {300, 350});
    }
} // namespace test_Tight

// =========================================================================
// 3. child 无尺寸：child 展开到（flipped）约束的 biggest
// =========================================================================
namespace test_NoSizeChild
{
    void testQt0()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(
                RotatedBox("rb").quarterTurns(0).child(Container("child"))))
            .layout();

        auto *rb = screen.findByKey("rb");
        auto *child = screen.findByKey("child");
        assert(rb && child);

        expectSize(rb, 800, 600);
        expectSize(child, 800, 600);
        expectOffset(child, rb, 0, 0);
        expectTopLeft(rb, 0, 0);
        expectTopLeft(child, 0, 0);
    }

    void testQt1()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(
                RotatedBox("rb").quarterTurns(1).child(Container("child"))))
            .layout();

        auto *rb = screen.findByKey("rb");
        auto *child = screen.findByKey("child");
        assert(rb && child);

        // child 收到 flipped loose 0..600 x 0..800 → Container 展开到 600x800
        // rbSize = constrain(flipped(600, 800)) = constrain(800, 600)
        expectSize(rb, 800, 600);
        expectSize(child, 600, 800);
        expectOffset(child, rb, 800, 0);
        expectTopLeft(rb, 0, 0);
        expectTopLeft(child, 800, 0);
    }
} // namespace test_NoSizeChild

// =========================================================================
// 4. 无 child
// =========================================================================
namespace test_NoChild
{
    void testLoose()
    {
        ScreenWidget screen{};
        screen.size(W, H).root(Center().child(RotatedBox("rb").quarterTurns(1))).layout();

        auto *rb = screen.findByKey("rb");
        assert(rb);
        expectSize(rb, 0, 0);
        expectTopLeft(rb, 400, 300);
    }

    void testTight()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(200).height(100).child(
                RotatedBox("rb").quarterTurns(1))))
            .layout();

        auto *rb = screen.findByKey("rb");
        assert(rb);
        expectSize(rb, 200, 100);
        expectTopLeft(rb, 300, 250);
    }
} // namespace test_NoChild

// =========================================================================
// main
// =========================================================================
void test_RotatedBox_api()
{
    {
        using namespace test_Loose;
        runStage("Loose: qt=0", testQt0);
        runStage("Loose: qt=1", testQt1);
        runStage("Loose: qt=2", testQt2);
        runStage("Loose: qt=3", testQt3);
    }

    {
        using namespace test_Tight;
        runStage("Tight: qt=0", testQt0);
        runStage("Tight: qt=1", testQt1);
        runStage("Tight: qt=2", testQt2);
        runStage("Tight: qt=3", testQt3);
    }
    using namespace test_NoSizeChild;
    runStage("NoSize: qt=0", testQt0);
    runStage("NoSize: qt=1", testQt1);

    using namespace test_NoChild;
    runStage("NoChild: loose", testLoose);
    runStage("NoChild: tight", testTight);
}

int main()
try
{
    test_RotatedBox_api();
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