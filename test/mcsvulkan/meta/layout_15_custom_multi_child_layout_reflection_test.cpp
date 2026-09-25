// 反射版 CustomMultiChildLayout2 —— 1:1 复现 layout_15_custom_multi_child_layout_test.cpp
#include <cassert>
#include <cmath>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "head.hpp"
#include "layout_engine.hpp"

using mcs::vulkan::meta::make_aggregate;

// =========================================================================
// 反射版 Widget：Agg 直接作成员，
// performLayout(size, hasChild, layoutChild, positionChild) 由 agg 决定
// =========================================================================
namespace reflect_layout
{
    template <class Agg>
    struct CustomMultiChildLayout2Widget final : Widget
    {
        Agg agg;
        std::vector<std::string> ids;
        std::vector<std::unique_ptr<Widget>> kids;
        std::vector<Offset> positions;

        CustomMultiChildLayout2Widget(std::string k, Agg a) noexcept : agg(std::move(a))
        {
            this->key = std::move(k);
        }

        auto &addChild(std::string id, WidgetBuild auto &&b)
        {
            ids.push_back(std::move(id));
            kids.push_back(
                static_cast<std::unique_ptr<Widget>>(std::forward<decltype(b)>(b)));
            positions.emplace_back();
            return *this;
        }

        void layout(BoxConstraints c) override
        {
            this->size = c.constrain(getSize(c));
            std::fill(positions.begin(), positions.end(), Offset{0.0, 0.0});

            auto has_child = [&](std::string_view id) {
                for (auto &s : ids)
                    if (s == id)
                        return true;
                return false;
            };
            auto layout_child = [&](std::string_view id, BoxConstraints cc) -> Size {
                for (size_t i = 0; i < ids.size(); ++i)
                    if (ids[i] == id)
                    {
                        kids[i]->layout(cc);
                        return kids[i]->size;
                    }
                return {0.0, 0.0};
            };
            auto position_child = [&](std::string_view id, Offset o) {
                for (size_t i = 0; i < ids.size(); ++i)
                    if (ids[i] == id)
                    {
                        positions[i] = o;
                        return;
                    }
            };

            agg.template invoke<"performLayout">(this->size, has_child, layout_child,
                                                 position_child);
        }

        void updateOffset(Offset o) noexcept override
        {
            this->offset = o;
            for (size_t i = 0; i < kids.size(); ++i)
                kids[i]->updateOffset({o.x + positions[i].x, o.y + positions[i].y});
        }

        std::vector<Widget *> children() override
        {
            std::vector<Widget *> out;
            out.reserve(kids.size());
            for (auto &k : kids)
                if (k)
                    out.push_back(k.get());
            return out;
        }

      private:
        Size getSize(BoxConstraints c) const
        {
            if constexpr (requires { agg.template invoke<"getSize">(c); })
                return agg.template invoke<"getSize">(c);
            else
                return c.biggest();
        }
    };

    template <class Agg>
    [[nodiscard]] inline auto makeMultiChild2(std::string key, Agg &&a)
    {
        using D = std::decay_t<Agg>;
        return std::make_unique<CustomMultiChildLayout2Widget<D>>(std::move(key),
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
// 1. 复现 test_Single::testFixedSizeSingle
//    原版: FixedSizeDelegate{200,100}, slot a
// =========================================================================
void testFixedSizeSingle()
{
    auto agg = make_aggregate<"FixedSize", "fixedSize", "getSize", "performLayout">(
        Size{200, 100},
        [](auto &&self, BoxConstraints c) { return c.constrain(self.fixedSize); },
        [](auto &&, Size size, auto &&hasChild, auto &&layoutChild,
           auto &&positionChild) {
            for (auto *slot : {"a", "b", "c"})
                if (hasChild(slot))
                {
                    layoutChild(slot, BoxConstraints::loose(size));
                    positionChild(slot, {0.0, 0.0});
                }
        });

    auto w = reflect_layout::makeMultiChild2("cml", std::move(agg));
    w->addChild("a", SizedBox("a").width(50).height(30));

    ScreenWidget screen{};
    screen.size(W, H).root(Center().child(std::move(w))).layout();

    auto *cml = screen.findByKey("cml");
    auto *a = screen.findByKey("a");
    assert(cml && a);
    expectSize(cml, 200, 100);
    expectSize(a, 50, 30);
    expectOffset(a, cml, 0, 0);
    expectTopLeft(cml, 300, 250);
    expectTopLeft(a, 300, 250);
}

// =========================================================================
// 2. 复现 test_Single::testGetSizeClamped
// =========================================================================
void testGetSizeClamped()
{
    auto agg = make_aggregate<"FixedSize", "fixedSize", "getSize", "performLayout">(
        Size{1000, 700},
        [](auto &&self, BoxConstraints c) { return c.constrain(self.fixedSize); },
        [](auto &&, Size size, auto &&hasChild, auto &&layoutChild,
           auto &&positionChild) {
            for (auto *slot : {"a", "b", "c"})
                if (hasChild(slot))
                {
                    layoutChild(slot, BoxConstraints::loose(size));
                    positionChild(slot, {0.0, 0.0});
                }
        });

    auto w = reflect_layout::makeMultiChild2("cml", std::move(agg));
    w->addChild("a", SizedBox("a").width(50).height(30));

    ScreenWidget screen{};
    screen.size(W, H).root(Center().child(std::move(w))).layout();

    auto *cml = screen.findByKey("cml");
    auto *a = screen.findByKey("a");
    assert(cml && a);
    expectSize(cml, 800, 600);
    expectSize(a, 50, 30);
    expectOffset(a, cml, 0, 0);
    expectTopLeft(cml, 0, 0);
}

// =========================================================================
// 3. 复现 test_Single::testNoChild
// =========================================================================
void testNoChild()
{
    auto agg = make_aggregate<"FixedSize", "fixedSize", "getSize", "performLayout">(
        Size{200, 100},
        [](auto &&self, BoxConstraints c) { return c.constrain(self.fixedSize); },
        [](auto &&, Size, auto &&, auto &&, auto &&) { /* 无 child */ });

    auto w = reflect_layout::makeMultiChild2("cml", std::move(agg));

    ScreenWidget screen{};
    screen.size(W, H).root(Center().child(std::move(w))).layout();

    auto *cml = screen.findByKey("cml");
    assert(cml);
    expectSize(cml, 200, 100);
    expectTopLeft(cml, 300, 250);
}

// =========================================================================
// 4. 复现 test_Single::testHasChildFalseSkipped
//    只有 slot b，没有 a/c
// =========================================================================
void testHasChildFalseSkipped()
{
    auto agg = make_aggregate<"FixedSize", "fixedSize", "getSize", "performLayout">(
        Size{200, 100},
        [](auto &&self, BoxConstraints c) { return c.constrain(self.fixedSize); },
        [](auto &&, Size size, auto &&hasChild, auto &&layoutChild,
           auto &&positionChild) {
            for (auto *slot : {"a", "b", "c"})
                if (hasChild(slot))
                {
                    layoutChild(slot, BoxConstraints::loose(size));
                    positionChild(slot, {0.0, 0.0});
                }
        });

    auto w = reflect_layout::makeMultiChild2("cml", std::move(agg));
    w->addChild("b", SizedBox("b").width(50).height(30)); // 只有 b

    ScreenWidget screen{};
    screen.size(W, H).root(Center().child(std::move(w))).layout();

    auto *cml = screen.findByKey("cml");
    auto *b = screen.findByKey("b");
    assert(cml && b);
    expectSize(cml, 200, 100);
    expectSize(b, 50, 30);
    expectOffset(b, cml, 0, 0);
}

// =========================================================================
// 5. 复现 test_Single::testNoPositionKeepsZero
//    原版 NoPositionDelegate：只 layoutChild，不调 positionChild
//    NOTE: aggregate 至少一字段，加 dummy
// =========================================================================
void testNoPositionKeepsZero()
{
    auto agg = make_aggregate<"NoPosition", "dummy", "performLayout">(
        int{0}, [](auto &&, Size size, auto &&hasChild, auto &&layoutChild, auto &&) {
            if (hasChild("a"))
                layoutChild("a", BoxConstraints::loose(size));
        });

    auto w = reflect_layout::makeMultiChild2("cml", std::move(agg));
    w->addChild("a", SizedBox("a").width(50).height(30));

    ScreenWidget screen{};
    screen.size(W, H).root(Center().child(std::move(w))).layout();

    auto *cml = screen.findByKey("cml");
    auto *a = screen.findByKey("a");
    assert(cml && a);
    expectSize(cml, 800, 600);
    expectOffset(a, cml, 0, 0);
}

// =========================================================================
// 6. 复现 test_Horizontal::testTwoChildren
// =========================================================================
void testHorizontalTwoChildren()
{
    auto agg = make_aggregate<"Horizontal", "dummy", "performLayout">(
        int{0}, [](auto &&, Size size, auto &&hasChild, auto &&layoutChild,
                   auto &&positionChild) {
            double x = 0.0;
            for (auto *slot : {"a", "b", "c"})
                if (hasChild(slot))
                {
                    const Size s = layoutChild(slot, BoxConstraints::loose(size));
                    positionChild(slot, {x, 0.0});
                    x += s.width;
                }
        });

    auto w = reflect_layout::makeMultiChild2("cml", std::move(agg));
    w->addChild("a", SizedBox("a").width(50).height(30));
    w->addChild("b", SizedBox("b").width(70).height(40));

    ScreenWidget screen{};
    screen.size(W, H).root(Center().child(std::move(w))).layout();

    auto *cml = screen.findByKey("cml");
    auto *a = screen.findByKey("a");
    auto *b = screen.findByKey("b");
    assert(cml && a && b);
    expectSize(cml, 800, 600);
    expectSize(a, 50, 30);
    expectSize(b, 70, 40);
    expectOffset(a, cml, 0, 0);
    expectOffset(b, cml, 50, 0);
    expectTopLeft(cml, 0, 0);
}

// =========================================================================
// 7. 复现 test_Horizontal::testThreeChildren
// =========================================================================
void testHorizontalThreeChildren()
{
    auto agg = make_aggregate<"Horizontal", "dummy", "performLayout">(
        int{0}, [](auto &&, Size size, auto &&hasChild, auto &&layoutChild,
                   auto &&positionChild) {
            double x = 0.0;
            for (auto *slot : {"a", "b", "c"})
                if (hasChild(slot))
                {
                    const Size s = layoutChild(slot, BoxConstraints::loose(size));
                    positionChild(slot, {x, 0.0});
                    x += s.width;
                }
        });

    auto w = reflect_layout::makeMultiChild2("cml", std::move(agg));
    w->addChild("a", SizedBox("a").width(50).height(30));
    w->addChild("b", SizedBox("b").width(70).height(40));
    w->addChild("c", SizedBox("c").width(60).height(20));

    ScreenWidget screen{};
    screen.size(W, H).root(Center().child(std::move(w))).layout();

    auto *cml = screen.findByKey("cml");
    auto *a = screen.findByKey("a");
    auto *b = screen.findByKey("b");
    auto *c = screen.findByKey("c");
    assert(cml && a && b && c);
    expectOffset(a, cml, 0, 0);
    expectOffset(b, cml, 50, 0);
    expectOffset(c, cml, 120, 0);
}

// =========================================================================
// 8. 复现 test_Horizontal::testLooseChildExpand
// =========================================================================
void testHorizontalLooseChildExpand()
{
    auto agg = make_aggregate<"Horizontal", "dummy", "performLayout">(
        int{0}, [](auto &&, Size size, auto &&hasChild, auto &&layoutChild,
                   auto &&positionChild) {
            double x = 0.0;
            for (auto *slot : {"a", "b", "c"})
                if (hasChild(slot))
                {
                    const Size s = layoutChild(slot, BoxConstraints::loose(size));
                    positionChild(slot, {x, 0.0});
                    x += s.width;
                }
        });

    auto w = reflect_layout::makeMultiChild2("cml", std::move(agg));
    w->addChild("a", Container("a"));

    ScreenWidget screen{};
    screen.size(W, H).root(Center().child(std::move(w))).layout();

    auto *cml = screen.findByKey("cml");
    auto *a = screen.findByKey("a");
    assert(cml && a);
    // loose(800x600)，无尺寸 Container 展开到 800x600
    expectSize(a, 800, 600);
    expectOffset(a, cml, 0, 0);
}

// =========================================================================
// 9. 复现 test_FollowTheLeader::testLeaderDecidesFollower
// =========================================================================
void testLeaderDecidesFollower()
{
    auto agg = make_aggregate<"FollowTheLeader", "dummy", "performLayout">(
        int{0}, [](auto &&, Size size, auto &&hasChild, auto &&layoutChild,
                   auto &&positionChild) {
            Size leaderSize{0.0, 0.0};
            if (hasChild("a"))
            {
                leaderSize = layoutChild("a", BoxConstraints::loose(size));
                positionChild("a", {0.0, 0.0});
            }
            if (hasChild("b"))
            {
                layoutChild("b", BoxConstraints::tight(leaderSize));
                positionChild("b", {size.width - leaderSize.width,
                                    size.height - leaderSize.height});
            }
        });

    auto w = reflect_layout::makeMultiChild2("cml", std::move(agg));
    w->addChild("a", SizedBox("a").width(100).height(80));
    w->addChild("b", Container("b"));

    ScreenWidget screen{};
    screen.size(W, H).root(Center().child(std::move(w))).layout();

    auto *cml = screen.findByKey("cml");
    auto *a = screen.findByKey("a");
    auto *b = screen.findByKey("b");
    assert(cml && a && b);
    expectSize(cml, 800, 600);
    expectSize(a, 100, 80);
    expectSize(b, 100, 80);
    expectOffset(a, cml, 0, 0);
    expectOffset(b, cml, 700, 520);
    expectTopLeft(b, 700, 520);
}

// =========================================================================
// 10. 复现 test_FollowTheLeader::testOnlyLeader
// =========================================================================
void testOnlyLeader()
{
    auto agg = make_aggregate<"FollowTheLeader", "dummy", "performLayout">(
        int{0}, [](auto &&, Size size, auto &&hasChild, auto &&layoutChild,
                   auto &&positionChild) {
            Size leaderSize{0.0, 0.0};
            if (hasChild("a"))
            {
                leaderSize = layoutChild("a", BoxConstraints::loose(size));
                positionChild("a", {0.0, 0.0});
            }
            if (hasChild("b"))
            {
                layoutChild("b", BoxConstraints::tight(leaderSize));
                positionChild("b", {size.width - leaderSize.width,
                                    size.height - leaderSize.height});
            }
        });

    auto w = reflect_layout::makeMultiChild2("cml", std::move(agg));
    w->addChild("a", SizedBox("a").width(100).height(80));

    ScreenWidget screen{};
    screen.size(W, H).root(Center().child(std::move(w))).layout();

    auto *cml = screen.findByKey("cml");
    auto *a = screen.findByKey("a");
    assert(cml && a);
    expectSize(a, 100, 80);
    expectOffset(a, cml, 0, 0);
}

// =========================================================================
// 11. 复现 test_ChildConstraints::testTight50x30
//     原版: FixedLayoutDelegate{200x100, tight(50,30), (0,0)}
// =========================================================================
void testChildConstraintsTight50x30()
{
    auto agg = make_aggregate<"FixedLayout", "fixedSize", "childConstraints", "offset",
                              "getSize", "performLayout">(
        Size{200, 100}, BoxConstraints::tightFor({.width = 50.0, .height = 30.0}),
        Offset{0, 0},
        [](auto &&self, BoxConstraints c) { return c.constrain(self.fixedSize); },
        [](auto &&self, Size, auto &&hasChild, auto &&layoutChild, auto &&positionChild) {
            if (hasChild("a"))
            {
                layoutChild("a", self.childConstraints);
                positionChild("a", self.offset);
            }
        });

    auto w = reflect_layout::makeMultiChild2("cml", std::move(agg));
    w->addChild("a", SizedBox("a").width(100).height(80));

    ScreenWidget screen{};
    screen.size(W, H).root(Center().child(std::move(w))).layout();

    auto *cml = screen.findByKey("cml");
    auto *a = screen.findByKey("a");
    assert(cml && a);
    expectSize(cml, 200, 100);
    expectSize(a, 50, 30);
    expectOffset(a, cml, 0, 0);
    expectTopLeft(cml, 300, 250);
    expectTopLeft(a, 300, 250);
}

// =========================================================================
// 12. 复现 test_ChildConstraints::testChildOverflow
//     原版: FixedLayoutDelegate{100x100, tight(200,200), (0,0)}
// =========================================================================
void testChildConstraintsChildOverflow()
{
    auto agg = make_aggregate<"FixedLayout", "fixedSize", "childConstraints", "offset",
                              "getSize", "performLayout">(
        Size{100, 100}, BoxConstraints::tightFor({.width = 200.0, .height = 200.0}),
        Offset{0, 0},
        [](auto &&self, BoxConstraints c) { return c.constrain(self.fixedSize); },
        [](auto &&self, Size, auto &&hasChild, auto &&layoutChild, auto &&positionChild) {
            if (hasChild("a"))
            {
                layoutChild("a", self.childConstraints);
                positionChild("a", self.offset);
            }
        });

    auto w = reflect_layout::makeMultiChild2("cml", std::move(agg));
    w->addChild("a", Container("a"));

    ScreenWidget screen{};
    screen.size(W, H).root(Center().child(std::move(w))).layout();

    auto *cml = screen.findByKey("cml");
    auto *a = screen.findByKey("a");
    assert(cml && a);
    expectSize(cml, 100, 100);
    expectSize(a, 200, 200);
    expectOffset(a, cml, 0, 0);
}

// =========================================================================
// 13. 复现 test_ChildConstraints::testOffsetCustom
//     原版: FixedLayoutDelegate{200x100, BoxConstraints{}, (20,30)}
// =========================================================================
void testChildConstraintsOffsetCustom()
{
    auto agg = make_aggregate<"FixedLayout", "fixedSize", "childConstraints", "offset",
                              "getSize", "performLayout">(
        Size{200, 100}, BoxConstraints{}, Offset{20, 30},
        [](auto &&self, BoxConstraints c) { return c.constrain(self.fixedSize); },
        [](auto &&self, Size, auto &&hasChild, auto &&layoutChild, auto &&positionChild) {
            if (hasChild("a"))
            {
                layoutChild("a", self.childConstraints);
                positionChild("a", self.offset);
            }
        });

    auto w = reflect_layout::makeMultiChild2("cml", std::move(agg));
    w->addChild("a", SizedBox("a").width(50).height(30));

    ScreenWidget screen{};
    screen.size(W, H).root(Center().child(std::move(w))).layout();

    auto *cml = screen.findByKey("cml");
    auto *a = screen.findByKey("a");
    assert(cml && a);
    expectSize(cml, 200, 100);
    expectSize(a, 50, 30);
    expectOffset(a, cml, 20, 30);
    expectTopLeft(cml, 300, 250);
    expectTopLeft(a, 320, 280);
}

// =========================================================================
// 14. 复现 test_Tight::testGetSizeClamp
// =========================================================================
void testTightGetSizeClamp()
{
    auto agg = make_aggregate<"FixedSize", "fixedSize", "getSize", "performLayout">(
        Size{100, 50},
        [](auto &&self, BoxConstraints c) { return c.constrain(self.fixedSize); },
        [](auto &&, Size size, auto &&hasChild, auto &&layoutChild,
           auto &&positionChild) {
            for (auto *slot : {"a", "b", "c"})
                if (hasChild(slot))
                {
                    layoutChild(slot, BoxConstraints::loose(size));
                    positionChild(slot, {0.0, 0.0});
                }
        });

    auto w = reflect_layout::makeMultiChild2("cml", std::move(agg));
    w->addChild("a", SizedBox("a").width(50).height(30));

    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(SizedBox("box").width(200).height(100).child(std::move(w))))
        .layout();

    auto *cml = screen.findByKey("cml");
    auto *a = screen.findByKey("a");
    assert(cml && a);
    expectSize(cml, 200, 100);
    expectSize(a, 50, 30);
    expectOffset(a, cml, 0, 0);
    expectTopLeft(cml, 300, 250);
}

// =========================================================================
// 15. 复现 test_Tight::testDelegateReturnsTight
// =========================================================================
void testTightDelegateReturnsTight()
{
    auto agg = make_aggregate<"FixedSize", "fixedSize", "getSize", "performLayout">(
        Size{200, 100},
        [](auto &&self, BoxConstraints c) { return c.constrain(self.fixedSize); },
        [](auto &&, Size size, auto &&hasChild, auto &&layoutChild,
           auto &&positionChild) {
            for (auto *slot : {"a", "b", "c"})
                if (hasChild(slot))
                {
                    layoutChild(slot, BoxConstraints::loose(size));
                    positionChild(slot, {0.0, 0.0});
                }
        });

    auto w = reflect_layout::makeMultiChild2("cml", std::move(agg));
    w->addChild("a", SizedBox("a").width(50).height(30));

    ScreenWidget screen{};
    screen.size(W, H)
        .root(Center().child(SizedBox("box").width(200).height(100).child(std::move(w))))
        .layout();

    auto *cml = screen.findByKey("cml");
    auto *a = screen.findByKey("a");
    assert(cml && a);
    expectSize(cml, 200, 100);
    expectSize(a, 50, 30);
    expectOffset(a, cml, 0, 0);
}

// =========================================================================
// 16. 复现 test_Edge::testZeroSizeChild
// =========================================================================
void testZeroSizeChild()
{
    auto agg = make_aggregate<"FixedSize", "fixedSize", "getSize", "performLayout">(
        Size{200, 100},
        [](auto &&self, BoxConstraints c) { return c.constrain(self.fixedSize); },
        [](auto &&, Size size, auto &&hasChild, auto &&layoutChild,
           auto &&positionChild) {
            for (auto *slot : {"a", "b", "c"})
                if (hasChild(slot))
                {
                    layoutChild(slot, BoxConstraints::loose(size));
                    positionChild(slot, {0.0, 0.0});
                }
        });

    auto w = reflect_layout::makeMultiChild2("cml", std::move(agg));
    w->addChild("a", SizedBox("a").width(0).height(0));

    ScreenWidget screen{};
    screen.size(W, H).root(Center().child(std::move(w))).layout();

    auto *cml = screen.findByKey("cml");
    auto *a = screen.findByKey("a");
    assert(cml && a);
    expectSize(a, 0, 0);
    expectOffset(a, cml, 0, 0);
}

// =========================================================================
// main：runStage 名称与原版 test_CustomMultiChildLayout_api() 一一对应
// =========================================================================
int main()
try
{
    runStage("Single: fixed size 200x100", testFixedSizeSingle);
    runStage("Single: getSize clamped", testGetSizeClamped);
    runStage("Single: no child", testNoChild);
    runStage("Single: hasChild false skipped", testHasChildFalseSkipped);
    runStage("Single: no positionChild -> (0,0)", testNoPositionKeepsZero);

    runStage("Horizontal: two children", testHorizontalTwoChildren);
    runStage("Horizontal: three children", testHorizontalThreeChildren);
    runStage("Horizontal: loose child expand", testHorizontalLooseChildExpand);

    runStage("FollowTheLeader: leader+follower", testLeaderDecidesFollower);
    runStage("FollowTheLeader: only leader", testOnlyLeader);

    runStage("ChildConstraints: tight 50x30", testChildConstraintsTight50x30);
    runStage("ChildConstraints: child overflow", testChildConstraintsChildOverflow);
    runStage("ChildConstraints: offset (20,30)", testChildConstraintsOffsetCustom);

    runStage("Tight: getSize clamp", testTightGetSizeClamp);
    runStage("Tight: delegate returns tight", testTightDelegateReturnsTight);

    runStage("Edge: zero-size child", testZeroSizeChild);

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