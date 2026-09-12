// wrap_test.cpp
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
// 1. Wrap 基础
// =========================================================================
namespace test_Wrap_Basic
{
    constexpr auto W = 800.0, H = 600.0;

    // 1.1 单行，无 spacing，不换行
    void testSingleLine()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(300).height(100).child(
                Wrap("wrap")
                    .addChild(SizedBox("a").width(50).height(30))
                    .addChild(SizedBox("b").width(70).height(40))
                    .addChild(SizedBox("c").width(80).height(20)))))
            .layout();

        auto *wrap = screen.findByKey("wrap");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        auto *c = screen.findByKey("c");
        assert(wrap && a && b && c);

        expectSize(wrap, 300, 100);
        expectSize(a, 50, 30);
        expectSize(b, 70, 40);
        expectSize(c, 80, 20);
        expectOffset(a, wrap, 0, 0);
        expectOffset(b, wrap, 50, 0);
        expectOffset(c, wrap, 120, 0);
        expectTopLeft(wrap, 250, 250);
    }

    // 1.2 单行，spacing = 10
    void testSpacing()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(300).height(100).child(
                Wrap("wrap")
                    .spacing(10)
                    .addChild(SizedBox("a").width(50).height(30))
                    .addChild(SizedBox("b").width(70).height(40))
                    .addChild(SizedBox("c").width(80).height(20)))))
            .layout();

        auto *wrap = screen.findByKey("wrap");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        auto *c = screen.findByKey("c");
        assert(wrap && a && b && c);

        expectOffset(a, wrap, 0, 0);
        expectOffset(b, wrap, 60, 0);
        expectOffset(c, wrap, 140, 0);
    }

    // 1.3 多行换行，runSpacing = 0
    void testMultiLine()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(150).height(200).child(
                Wrap("wrap")
                    .addChild(SizedBox("a").width(60).height(30))
                    .addChild(SizedBox("b").width(60).height(30))
                    .addChild(SizedBox("c").width(60).height(30)))))
            .layout();

        auto *wrap = screen.findByKey("wrap");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        auto *c = screen.findByKey("c");
        assert(wrap && a && b && c);

        expectSize(wrap, 150, 200);
        expectOffset(a, wrap, 0, 0);
        expectOffset(b, wrap, 60, 0);
        expectOffset(c, wrap, 0, 30);
        expectTopLeft(wrap, 325, 200);
    }

    // 1.4 多行换行，runSpacing = 10
    void testRunSpacing()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(150).height(200).child(
                Wrap("wrap")
                    .runSpacing(10)
                    .addChild(SizedBox("a").width(60).height(30))
                    .addChild(SizedBox("b").width(60).height(30))
                    .addChild(SizedBox("c").width(60).height(30)))))
            .layout();

        auto *wrap = screen.findByKey("wrap");
        auto *c = screen.findByKey("c");
        assert(wrap && c);

        expectOffset(c, wrap, 0, 40);
    }
} // namespace test_Wrap_Basic

// =========================================================================
// 2. alignment：run 内主轴对齐
// =========================================================================
namespace test_Wrap_Alignment
{
    constexpr auto W = 800.0, H = 600.0;

    // 辅助：构造同一棵树，只换 alignment
    template <typename Builder>
    void checkAlignment(const char *name, Builder build)
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(
                SizedBox("box").width(320).height(100).child(build(Wrap("wrap")))))
            .layout();

        auto *wrap = screen.findByKey("wrap");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        auto *c = screen.findByKey("c");
        assert(wrap && a && b && c);

        runStage(name, [&] {
            // 具体断言在各子测试里
        });
        (void)wrap;
        (void)a;
        (void)b;
        (void)c;
    }

    void testCenter()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(320).height(100).child(
                Wrap("wrap")
                    .alignment(WrapAlignment::center)
                    .addChild(SizedBox("a").width(50).height(30))
                    .addChild(SizedBox("b").width(70).height(40))
                    .addChild(SizedBox("c").width(80).height(20)))))
            .layout();

        auto *wrap = screen.findByKey("wrap");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        auto *c = screen.findByKey("c");
        assert(wrap && a && b && c);

        expectOffset(a, wrap, 60, 0);
        expectOffset(b, wrap, 110, 0);
        expectOffset(c, wrap, 180, 0);
    }

    void testEnd()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(320).height(100).child(
                Wrap("wrap")
                    .alignment(WrapAlignment::end)
                    .addChild(SizedBox("a").width(50).height(30))
                    .addChild(SizedBox("b").width(70).height(40))
                    .addChild(SizedBox("c").width(80).height(20)))))
            .layout();

        auto *wrap = screen.findByKey("wrap");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        auto *c = screen.findByKey("c");
        assert(wrap && a && b && c);

        expectOffset(a, wrap, 120, 0);
        expectOffset(b, wrap, 170, 0);
        expectOffset(c, wrap, 240, 0);
    }

    void testSpaceBetween()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(320).height(100).child(
                Wrap("wrap")
                    .alignment(WrapAlignment::spaceBetween)
                    .addChild(SizedBox("a").width(50).height(30))
                    .addChild(SizedBox("b").width(70).height(40))
                    .addChild(SizedBox("c").width(80).height(20)))))
            .layout();

        auto *wrap = screen.findByKey("wrap");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        auto *c = screen.findByKey("c");
        assert(wrap && a && b && c);

        expectOffset(a, wrap, 0, 0);
        expectOffset(b, wrap, 110, 0);
        expectOffset(c, wrap, 240, 0);
    }

    void testSpaceAround()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(320).height(100).child(
                Wrap("wrap")
                    .alignment(WrapAlignment::spaceAround)
                    .addChild(SizedBox("a").width(50).height(30))
                    .addChild(SizedBox("b").width(70).height(40))
                    .addChild(SizedBox("c").width(80).height(20)))))
            .layout();

        auto *wrap = screen.findByKey("wrap");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        auto *c = screen.findByKey("c");
        assert(wrap && a && b && c);

        expectOffset(a, wrap, 20, 0);
        expectOffset(b, wrap, 110, 0);
        expectOffset(c, wrap, 220, 0);
    }

    void testSpaceEvenly()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(320).height(100).child(
                Wrap("wrap")
                    .alignment(WrapAlignment::spaceEvenly)
                    .addChild(SizedBox("a").width(50).height(30))
                    .addChild(SizedBox("b").width(70).height(40))
                    .addChild(SizedBox("c").width(80).height(20)))))
            .layout();

        auto *wrap = screen.findByKey("wrap");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        auto *c = screen.findByKey("c");
        assert(wrap && a && b && c);

        expectOffset(a, wrap, 30, 0);
        expectOffset(b, wrap, 110, 0);
        expectOffset(c, wrap, 210, 0);
    }
} // namespace test_Wrap_Alignment

// =========================================================================
// 3. runAlignment
// =========================================================================
namespace test_Wrap_RunAlignment
{
    constexpr auto W = 800.0, H = 600.0;

    void testCenter()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(150).height(200).child(
                Wrap("wrap")
                    .runAlignment(WrapAlignment::center)
                    .addChild(SizedBox("a").width(60).height(30))
                    .addChild(SizedBox("b").width(60).height(30))
                    .addChild(SizedBox("c").width(60).height(30)))))
            .layout();

        auto *wrap = screen.findByKey("wrap");
        auto *a = screen.findByKey("a");
        auto *c = screen.findByKey("c");
        assert(wrap && a && c);

        expectOffset(a, wrap, 0, 70);
        expectOffset(c, wrap, 0, 100);
    }

    void testEnd()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(150).height(200).child(
                Wrap("wrap")
                    .runAlignment(WrapAlignment::end)
                    .addChild(SizedBox("a").width(60).height(30))
                    .addChild(SizedBox("b").width(60).height(30))
                    .addChild(SizedBox("c").width(60).height(30)))))
            .layout();

        auto *wrap = screen.findByKey("wrap");
        auto *a = screen.findByKey("a");
        auto *c = screen.findByKey("c");
        assert(wrap && a && c);

        expectOffset(a, wrap, 0, 140);
        expectOffset(c, wrap, 0, 170);
    }
} // namespace test_Wrap_RunAlignment

// =========================================================================
// 4. crossAxisAlignment：run 内交叉轴对齐
// =========================================================================
namespace test_Wrap_CrossAxisAlignment
{
    constexpr auto W = 800.0, H = 600.0;

    void testCenter()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(300).height(100).child(
                Wrap("wrap")
                    .crossAxisAlignment(WrapCrossAlignment::center)
                    .addChild(SizedBox("a").width(50).height(30))
                    .addChild(SizedBox("b").width(70).height(60))
                    .addChild(SizedBox("c").width(80).height(20)))))
            .layout();

        auto *wrap = screen.findByKey("wrap");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        auto *c = screen.findByKey("c");
        assert(wrap && a && b && c);

        // 行高 60
        expectOffset(a, wrap, 0, 15);
        expectOffset(b, wrap, 50, 0);
        expectOffset(c, wrap, 120, 20);
    }

    void testEnd()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(300).height(100).child(
                Wrap("wrap")
                    .crossAxisAlignment(WrapCrossAlignment::end)
                    .addChild(SizedBox("a").width(50).height(30))
                    .addChild(SizedBox("b").width(70).height(60))
                    .addChild(SizedBox("c").width(80).height(20)))))
            .layout();

        auto *wrap = screen.findByKey("wrap");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        auto *c = screen.findByKey("c");
        assert(wrap && a && b && c);

        expectOffset(a, wrap, 0, 30);
        expectOffset(b, wrap, 50, 0);
        expectOffset(c, wrap, 120, 40);
    }
} // namespace test_Wrap_CrossAxisAlignment

// =========================================================================
// 5. direction: Axis.vertical
// =========================================================================
namespace test_Wrap_Vertical
{
    constexpr auto W = 800.0, H = 600.0;

    void testVertical()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(SizedBox("box").width(200).height(150).child(
                Wrap("wrap")
                    .direction(Axis::vertical)
                    .addChild(SizedBox("a").width(30).height(50))
                    .addChild(SizedBox("b").width(40).height(70))
                    .addChild(SizedBox("c").width(20).height(40)))))
            .layout();

        auto *wrap = screen.findByKey("wrap");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        auto *c = screen.findByKey("c");
        assert(wrap && a && b && c);

        expectSize(wrap, 200, 150);
        expectOffset(a, wrap, 0, 0);
        expectOffset(b, wrap, 0, 50);
        expectOffset(c, wrap, 40, 0);
    }
} // namespace test_Wrap_Vertical

// =========================================================================
// 6. 边界
// =========================================================================
namespace test_Wrap_Edge
{
    constexpr auto W = 800.0, H = 600.0;

    // 6.1 空 children
    void testEmpty()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(
                SizedBox("box").width(200).height(100).child(Wrap("wrap"))))
            .layout();

        auto *wrap = screen.findByKey("wrap");
        assert(wrap);

        expectSize(wrap, 200, 100);
        expectTopLeft(wrap, 300, 250);
    }

    // 6.2 父 loose 约束下收缩到内容
    void testShrinkToContent()
    {
        ScreenWidget screen{};
        screen.size(W, H)
            .root(Center().child(Wrap("wrap")
                                     .addChild(SizedBox("a").width(50).height(30))
                                     .addChild(SizedBox("b").width(70).height(40))))
            .layout();

        auto *wrap = screen.findByKey("wrap");
        auto *a = screen.findByKey("a");
        auto *b = screen.findByKey("b");
        assert(wrap && a && b);

        expectSize(wrap, 120, 40);
        expectOffset(a, wrap, 0, 0);
        expectOffset(b, wrap, 50, 0);
        expectTopLeft(wrap, 340, 280);
    }
} // namespace test_Wrap_Edge

// =========================================================================
// main
// =========================================================================
void test_Wrap_api()
{
    {
        using namespace test_Wrap_Basic;
        runStage("Wrap basic: single line", testSingleLine);
        runStage("Wrap basic: spacing=10", testSpacing);
        runStage("Wrap basic: multi-line", testMultiLine);
        runStage("Wrap basic: runSpacing=10", testRunSpacing);
    }

    {
        using namespace test_Wrap_Alignment;
        runStage("Wrap alignment: center", testCenter);
        runStage("Wrap alignment: end", testEnd);
        runStage("Wrap alignment: spaceBetween", testSpaceBetween);
        runStage("Wrap alignment: spaceAround", testSpaceAround);
        runStage("Wrap alignment: spaceEvenly", testSpaceEvenly);
    }
    {
        using namespace test_Wrap_RunAlignment;
        runStage("Wrap runAlignment: center", testCenter);
        runStage("Wrap runAlignment: end", testEnd);
    }

    {
        using namespace test_Wrap_CrossAxisAlignment;
        runStage("Wrap crossAxisAlignment: center", testCenter);
        runStage("Wrap crossAxisAlignment: end", testEnd);
    }

    using namespace test_Wrap_Vertical;
    runStage("Wrap direction: vertical", testVertical);

    using namespace test_Wrap_Edge;
    runStage("Wrap edge: empty children", testEmpty);
    runStage("Wrap edge: shrink to content", testShrinkToContent);
}

int main()
try
{
    test_Wrap_api();
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