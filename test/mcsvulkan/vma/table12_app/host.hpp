#pragma once
// ============================================================================
// table12_app/host.hpp — window::Host / window::Binding（窗口 layer）
//
//   这一层只做窗口级的事：输入路由、焦点、z 序（draw_order）、帧号、cursor、
//   picking 回读。它不计算几何、不改 Model/State（只调 session 的门）。
//
//   实现放在 demo cpp（test_ui_ability12_table.cpp 的 table_ui）：窗口宿主需要
//   GLFW、Vulkan、输入栈和 demo 的 Widget 类型，这些都不属于核心。本头只把
//   规范名字暴露出来，让主线可读：
//
//     GLFW event → window::Host::route_event → Binding::handle_event
//                → session::dispatch（唯一写入口）
//     session::compose → frame::DrawList → platform::encode（唯一 GPU 出口）
//
//   依赖方向：host → session/command/platform；核心永远不知道 host。
//   ⚠ 本头必须在 demo cpp 定义了 table_ui::Binding / table_ui::Host 之后才能
//     include（它是 app 层的“命名适配器”，不是自包含的接口头）。
// ============================================================================

namespace table::window
{
using Binding = table_ui::Binding; // 一张表的窗口绑定（内含 session::Session）
using Host = table_ui::Host;       // 一个窗口的宿主（输入/焦点/z 序/帧号）
} // namespace table::window
