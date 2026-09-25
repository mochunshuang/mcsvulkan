// 反射版 CustomSingleChildLayout2 —— 1:1 复现 layout_14_custom_single_child_layout_test.cpp
#include <cassert>
#include <cmath>
#include <exception>
#include <iostream>
#include <memory>
#include <vector>

#include "head.hpp"
#include "layout_engine.hpp"

using mcs::vulkan::meta::make_aggregate;

// =========================================================================
// 反射版 Widget：Agg 直接作成员，三方法用 requires + if constexpr 分派
// =========================================================================
namespace reflect_layout
{
    template <class Agg>
    struct CustomSingleChildLayout2Widget final : Widget
    {
        Agg agg;
        std::unique_ptr<Widget> child;

        CustomSingleChildLayout2Widget(std::string k, Agg a,
                                       std::unique_ptr<Widget> c = nullptr) noexcept
            : agg(std::move(a)), child(std::move(c))
        {
            this->key = std::move(k);
        }

        void layout(BoxConstraints c) override
        {
            this->size = c.constrain(getSize(c));
            if (child)
                child->layout(getChildConstraints(c));
        }

        void updateOffset(Offset o) noexcept override
        {
            this->offset = o;
            if (!child)
                return;
            const Offset p = getChildPosition(this->size, child->size);
            child->updateOffset({o.x + p.x, o.y + p.y});
        }

        std::vector<Widget *> children() override
        {
            if (!child)
                return {};
            return {child.get()};
        }

      private:
        Size getSize(BoxConstraints c) const
        {
            if constexpr (requires { agg.template invoke<"getSize">(c); })
                return agg.template invoke<"getSize">(c);
            else
                return c.biggest();
        }
        BoxConstraints getChildConstraints(BoxConstraints c) const
        {
            if constexpr (requires { agg.template invoke<"getConstraintsForChild">(c); })
                return agg.template invoke<"getConstraintsForChild">(c);
            else
                return c;
        }
        Offset getChildPosition(Size s, Size cs) const
        {
            if constexpr (requires { agg.template invoke<"getPositionForChild">(s, cs); })
                return agg.template invoke<"getPositionForChild">(s, cs);
            else
                return {0.0, 0.0};
        }
    };

    template <class Agg>
    [[nodiscard]] inline auto makeSingleChild2(std::string key, Agg &&a)
    {
        using D = std::decay_t<Agg>;
        return std::make_unique<CustomSingleChildLayout2Widget<D>>(std::move(key),
                                                                   std::forward<Agg>(a));
    }
} // namespace reflect_layout

// =========================================================================
// 断言辅助
// =========================================================================
namespace
{
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
} // namespace

// =========================================================================
// 1. 复现 test_Default::testLooseParent
//    原版: FixedSizeDelegate{800,600} → 只覆写 getSize
//    反射: agg{"fixedSize", "getSize"}
// =========================================================================
void testLooseParent()
{
    auto agg = make_aggregate<"FixedSize", "fixedSize", "getSize">(
        Size{800, 600},
        [](auto &&self, BoxConstraints c) { return c.constrain(self.fixedSize); });

    auto w = reflect_layout::makeSingleChild2("cl", std::move(agg));
    w->child = static_cast<std::unique_ptr<Widget>>(SizedBox("c").width(100).height(80));

    ScreenWidget screen{};
    screen.size(W, H).root(Center().child(std::move(w))).layout();

    auto *cl = screen.findByKey("cl");
    auto *c = screen.findByKey("c");
    assert(cl && c);
    expectSize(cl, 800, 600);
    expectSize(c, 100, 80);
    expectOffset(c, cl, 0, 0);
    expectTopLeft(cl, 0, 0);
    expectTopLeft(c, 0, 0);
}

// =========================================================================
// 2. 复现 test_Default::testNoChild
//    原版: FixedSizeDelegate{200,100}，无 child
// =========================================================================
void testNoChild()
{
    auto agg = make_aggregate<"FixedSize", "fixedSize", "getSize">(
        Size{200, 100},
        [](auto &&self, BoxConstraints c) { return c.constrain(self.fixedSize); });

    auto w = reflect_layout::makeSingleChild2("cl", std::move(agg));

    ScreenWidget screen{};
    screen.size(W, H).root(Center().child(std::move(w))).layout();

    auto *cl = screen.findByKey("cl");
    assert(cl);
    expectSize(cl, 200, 100);
    expectTopLeft(cl, 300, 250);
}

// =========================================================================
// 3. 复现 test_GetSize::testFixed200x100
// =========================================================================
void testGetSizeFixed200x100()
{
    auto agg = make_aggregate<"FixedSize", "fixedSize", "getSize">(
        Size{200, 100},
        [](auto &&self, BoxConstraints c) { return c.constrain(self.fixedSize); });

    auto w = reflect_layout::makeSingleChild2("cl", std::move(agg));
    w->child = static_cast<std::unique_ptr<Widget>>(SizedBox("c").width(50).height(30));

    ScreenWidget screen{};
    screen.size(W, H).root(Center().child(std::move(w))).layout();

    auto *cl = screen.findByKey("cl");
    auto *c = screen.findByKey("c");
    assert(cl && c);
    expectSize(cl, 200, 100);
    expectSize(c, 50, 30);
    expectOffset(c, cl, 0, 0);
    expectTopLeft(cl, 300, 250);
    expectTopLeft(c, 300, 250);
}

// =========================================================================
// 4. 复现 test_GetSize::testClamp1000x700
// =========================================================================
void testGetSizeClamp1000x700()
{
    auto agg = make_aggregate<"FixedSize", "fixedSize", "getSize">(
        Size{1000, 700},
        [](auto &&self, BoxConstraints c) { return c.constrain(self.fixedSize); });

    auto w = reflect_layout::makeSingleChild2("cl", std::move(agg));
    w->child = static_cast<std::unique_ptr<Widget>>(SizedBox("c").width(50).height(30));

    ScreenWidget screen{};
    screen.size(W, H).root(Center().child(std::move(w))).layout();

    auto *cl = screen.findByKey("cl");
    auto *c = screen.findByKey("c");
    assert(cl && c);
    expectSize(cl, 800, 600);
    expectSize(c, 50, 30);
    expectTopLeft(cl, 0, 0);
}

// =========================================================================
// 5. 复现 test_ChildConstraints::testTight150x50
//    原版: FixedChildConstraintsDelegate{tight(150,50)} → 只覆写 getConstraintsForChild
// =========================================================================
void testChildConstraintsTight150x50()
{
    auto agg = make_aggregate<"FixedCC", "childConstraints", "getConstraintsForChild">(
        BoxConstraints::tightFor({.width = 150.0, .height = 50.0}),
        [](auto &&self, BoxConstraints) { return self.childConstraints; });

    auto w = reflect_layout::makeSingleChild2("cl", std::move(agg));
    w->child = static_cast<std::unique_ptr<Widget>>(SizedBox("c").width(100).height(80));

    ScreenWidget screen{};
    screen.size(W, H).root(Center().child(std::move(w))).layout();

    auto *cl = screen.findByKey("cl");
    auto *c = screen.findByKey("c");
    assert(cl && c);
    expectSize(cl, 800, 600); // 默认 getSize = biggest
    expectSize(c, 150, 50);
    expectOffset(c, cl, 0, 0);
    expectTopLeft(cl, 0, 0);
    expectTopLeft(c, 0, 0);
}

// =========================================================================
// 6. 复现 test_ChildConstraints::testLoose200x100
// =========================================================================
void testChildConstraintsLoose200x100()
{
    auto agg = make_aggregate<"FixedCC", "childConstraints", "getConstraintsForChild">(
        BoxConstraints{0.0, 200.0, 0.0, 100.0},
        [](auto &&self, BoxConstraints) { return self.childConstraints; });

    auto w = reflect_layout::makeSingleChild2("cl", std::move(agg));
    w->child = static_cast<std::unique_ptr<Widget>>(SizedBox("c").width(100).height(80));

    ScreenWidget screen{};
    screen.size(W, H).root(Center().child(std::move(w))).layout();

    auto *cl = screen.findByKey("cl");
    auto *c = screen.findByKey("c");
    assert(cl && c);
    expectSize(cl, 800, 600);
    expectSize(c, 100, 80);
    expectOffset(c, cl, 0, 0);
}

// =========================================================================
// 7. 复现 test_ChildConstraints::testChildOverflow
//    原版: CombinedDelegate{100x100, tight(200,200), (0,0)} → 三方法全写
// =========================================================================
void testChildOverflow()
{
    auto agg = make_aggregate<"Combined", "fixedSize", "childConstraints", "offset",
                              "getSize", "getConstraintsForChild", "getPositionForChild">(
        Size{100, 100}, BoxConstraints::tightFor({.width = 200.0, .height = 200.0}),
        Offset{0, 0},
        [](auto &&self, BoxConstraints c) { return c.constrain(self.fixedSize); },
        [](auto &&self, BoxConstraints) { return self.childConstraints; },
        [](auto &&self, Size, Size) { return self.offset; });

    auto w = reflect_layout::makeSingleChild2("cl", std::move(agg));
    w->child = static_cast<std::unique_ptr<Widget>>(SizedBox("c").width(50).height(50));

    ScreenWidget screen{};
    screen.size(W, H).root(Center().child(std::move(w))).layout();

    auto *cl = screen.findByKey("cl");
    auto *c = screen.findByKey("c");
    assert(cl && c);
    expectSize(cl, 100, 100);
    expectSize(c, 200, 200);
    expectOffset(c, cl, 0, 0);
    expectTopLeft(cl, 350, 250);
    expectTopLeft(c, 350, 250);
}

// =========================================================================
// 8. 复现 test_Position::testFixedOffset
//    原版: CombinedDelegate{200x100, BoxConstraints{}, (20,30)}
// =========================================================================
void testPositionFixedOffset()
{
    auto agg = make_aggregate<"Combined", "fixedSize", "childConstraints", "offset",
                              "getSize", "getConstraintsForChild", "getPositionForChild">(
        Size{200, 100}, BoxConstraints{}, Offset{20, 30},
        [](auto &&self, BoxConstraints c) { return c.constrain(self.fixedSize); },
        [](auto &&self, BoxConstraints) { return self.childConstraints; },
        [](auto &&self, Size, Size) { return self.offset; });

    auto w = reflect_layout::makeSingleChild2("cl", std::move(agg));
    w->child = static_cast<std::unique_ptr<Widget>>(SizedBox("c").width(50).height(30));

    ScreenWidget screen{};
    screen.size(W, H).root(Center().child(std::move(w))).layout();

    auto *cl = screen.findByKey("cl");
    auto *c = screen.findByKey("c");
    assert(cl && c);
    expectSize(cl, 200, 100);
    expectSize(c, 50, 30);
    expectOffset(c, cl, 20, 30);
    expectTopLeft(cl, 300, 250);
    expectTopLeft(c, 320, 280);
}

// =========================================================================
// 9. 复现 test_Position::testBottomRightLoose
//    原版: BottomRightDelegate → 只覆写 getPositionForChild
//    NOTE: aggregate 要求 names 比 values 多 1，所以放一个占位字段 dummy
// =========================================================================
void testBottomRightLoose()
{
    auto agg = make_aggregate<"BottomRight", "dummy", "getPositionForChild">(
        int{0}, [](auto &&, Size size, Size childSize) {
            return Offset{size.width - childSize.width, size.height - childSize.height};
        });

    auto w = reflect_layout::makeSingleChild2("cl", std::move(agg));
    w->child = static_cast<std::unique_ptr<Widget>>(SizedBox("c").width(100).height(80));

    ScreenWidget screen{};
    screen.size(W, H).root(Center().child(std::move(w))).layout();

    auto *cl = screen.findByKey("cl");
    auto *c = screen.findByKey("c");
    assert(cl && c);
    expectSize(cl, 800, 600);
    expectSize(c, 100, 80);
    expectOffset(c, cl, 700, 520);
    expectTopLeft(cl, 0, 0);
    expectTopLeft(c, 700, 520);
}

// =========================================================================
// 10. 复现 test_Position::testBottomRightTight
//     原版: CombinedDelegate{200x100, tight(50,30), (150,70)}
// =========================================================================
void testBottomRightTight()
{
    auto agg = make_aggregate<"Combined", "fixedSize", "childConstraints", "offset",
                              "getSize", "getConstraintsForChild", "getPositionForChild">(
        Size{200, 100}, BoxConstraints::tightFor({.width = 50.0, .height = 30.0}),
        Offset{150, 70},
        [](auto &&self, BoxConstraints c) { return c.constrain(self.fixedSize); },
        [](auto &&self, BoxConstraints) { return self.childConstraints; },
        [](auto &&self, Size, Size) { return self.offset; });

    auto w = reflect_layout::makeSingleChild2("cl", std::move(agg));
    w->child = static_cast<std::unique_ptr<Widget>>(SizedBox("c").width(100).height(80));

    ScreenWidget screen{};
    screen.size(W, H).root(Center().child(std::move(w))).layout();

    auto *cl = screen.findByKey("cl");
    auto *c = screen.findByKey("c");
    assert(cl && c);
    expectSize(cl, 200, 100);
    expectSize(c, 50, 30);
    expectOffset(c, cl, 150, 70);
}

// =========================================================================
// 11. 复现 test_Combined::testAllThree
// =========================================================================
void testAllThree()
{
    auto agg = make_aggregate<"Combined", "fixedSize", "childConstraints", "offset",
                              "getSize", "getConstraintsForChild", "getPositionForChild">(
        Size{200, 100}, BoxConstraints::tightFor({.width = 50.0, .height = 30.0}),
        Offset{75, 35},
        [](auto &&self, BoxConstraints c) { return c.constrain(self.fixedSize); },
        [](auto &&self, BoxConstraints) { return self.childConstraints; },
        [](auto &&self, Size, Size) { return self.offset; });

    auto w = reflect_layout::makeSingleChild2("cl", std::move(agg));
    w->child = static_cast<std::unique_ptr<Widget>>(SizedBox("c").width(100).height(80));

    ScreenWidget screen{};
    screen.size(W, H).root(Center().child(std::move(w))).layout();

    auto *cl = screen.findByKey("cl");
    auto *c = screen.findByKey("c");
    assert(cl && c);
    expectSize(cl, 200, 100);
    expectSize(c, 50, 30);
    expectOffset(c, cl, 75, 35);
    expectTopLeft(cl, 300, 250);
    expectTopLeft(c, 375, 285);
}

// =========================================================================
// 12. 复现 test_Tight::testDefaultDelegate
//     SizedBox{200x100} 内嵌 → FixedSizeDelegate{200,100}
// =========================================================================
void testTightDefaultDelegate()
{
    auto agg = make_aggregate<"FixedSize", "fixedSize", "getSize">(
        Size{200, 100},
        [](auto &&self, BoxConstraints c) { return c.constrain(self.fixedSize); });

    auto w = reflect_layout::makeSingleChild2("cl", std::move(agg));
    w->child = static_cast<std::unique_ptr<Widget>>(SizedBox("c").width(50).height(30));

    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(SizedBox("box").width(200).height(100).child(std::move(w))))
        .layout();

    auto *cl = screen.findByKey("cl");
    auto *c = screen.findByKey("c");
    assert(cl && c);
    expectSize(cl, 200, 100);
    expectSize(c, 200, 100); // childConstraints = 原样 tight
    expectOffset(c, cl, 0, 0);
    expectTopLeft(cl, 300, 250);
}

// =========================================================================
// 13. 复现 test_Tight::testGetSizeClamp
// =========================================================================
void testTightGetSizeClamp()
{
    auto agg = make_aggregate<"FixedSize", "fixedSize", "getSize">(
        Size{100, 50},
        [](auto &&self, BoxConstraints c) { return c.constrain(self.fixedSize); });

    auto w = reflect_layout::makeSingleChild2("cl", std::move(agg));
    w->child = static_cast<std::unique_ptr<Widget>>(SizedBox("c").width(50).height(30));

    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(SizedBox("box").width(200).height(100).child(std::move(w))))
        .layout();

    auto *cl = screen.findByKey("cl");
    auto *c = screen.findByKey("c");
    assert(cl && c);
    expectSize(cl, 200, 100); // constrain(100x50) → tight 200x100
    expectSize(c, 200, 100);
}

// =========================================================================
// main：runStage 名称与原版 test_CustomSingleChildLayout_api() 一一对应
// =========================================================================
int main()
try
{
    runStage("Default: loose parent", testLooseParent);
    runStage("Default: no child", testNoChild);
    runStage("GetSize: fixed 200x100", testGetSizeFixed200x100);
    runStage("GetSize: clamp 1000x700", testGetSizeClamp1000x700);
    runStage("ChildConstraints: tight 150x50", testChildConstraintsTight150x50);
    runStage("ChildConstraints: loose 200x100", testChildConstraintsLoose200x100);
    runStage("ChildConstraints: child overflow", testChildOverflow);
    runStage("Position: fixed offset", testPositionFixedOffset);
    runStage("Position: bottom right loose", testBottomRightLoose);
    runStage("Position: bottom right tight", testBottomRightTight);
    runStage("Combined: all three", testAllThree);
    runStage("Tight parent: default", testTightDefaultDelegate);
    runStage("Tight parent: getSize clamp", testTightGetSizeClamp);

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