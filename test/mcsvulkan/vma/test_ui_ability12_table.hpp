#pragma once
// ============================================================================
// test_ui_ability12_table.hpp — ability12 的唯一测试入口（umbrella）
//
//   核心在 table12/：contract/model/intent/frame/command/session + detail/*
//   窗口/平台在 table12_app/：host.hpp（Host/Binding）、platform.hpp（encode）
//
//   本文件只做 include：任何实现都不放在这里。
//   ability11 的单头 6355 行换成“7 个 schema 头 + 1 个组合根”，依赖方向见各头注释。
// ============================================================================

#include "table12/session.hpp"
#include "table12/test/selftest.hpp"
