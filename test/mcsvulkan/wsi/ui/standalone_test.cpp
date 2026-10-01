// 脱开 Vulkan / GLFW 的自测入口：只依赖 input 层与 ui 层的头，链接不需要任何第三方库。
//
// 编译（在 test/mcsvulkan/wsi 目录下执行）：
//   g++ -std=c++26 -D__cpp_lib_constexpr_exceptions=0 -DNOMINMAX -DWIN32_LEAN_AND_MEAN \
//       -I../../../include -o ui_selftest.exe ui/standalone_test.cpp
// 运行：
//   ./ui_selftest.exe          （退出码 0 = 全部通过）
//
// 说明：这里故意不 include mcsvulkan 的总头（那样会带进 Vulkan），
//       只把 event / input 层需要的最小头拉进来。

#include <algorithm> // NOTE: event_dispatcher.hpp 用了 std::remove_if，但自己只 include 了 <vector>
#include <print>

#include "ui_widget.hpp"
#include "self_test.hpp"

int main(int argc, char **argv)
{
    const bool trace = argc > 1 && std::string_view{argv[1]} == "--trace";
    return mcs::vulkan::ui::selftest::run(trace) ? 0 : 1;
}
