#include "uhi.hpp"
struct fsm {
    uhi::step_t step(int, uhi::time_point) noexcept { return uhi::step_t::idle; }
    void reset() noexcept {}
};
using src = uhi::source_t<"s">;
using a = uhi::leaf<uhi::kind_t<"a">, src, fsm>;
using b = uhi::leaf<uhi::kind_t<"b">, src, fsm>;
using tree = uhi::one_of<a, b>;
uhi::ui<tree> g_ui;
int main() { g_ui.tick(uhi::time_point{}); return (int)sizeof(uhi::ui<tree>); }
