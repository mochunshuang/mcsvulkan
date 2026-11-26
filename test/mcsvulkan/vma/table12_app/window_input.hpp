#pragma once
// ============================================================================
// table12_app/window_input.hpp — input aggregator（只 include，不再承载定义）
//
//   顺序：effects -> text -> events（events 依赖前两者的 helper）。
// ============================================================================

#include "host.hpp"
#include "window_input_effects.hpp"
#include "window_input_text.hpp"
#include "window_input_events.hpp"
