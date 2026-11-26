#pragma once
// ============================================================================
// table12/detail/features.hpp — aggregator（G3 起只做 include）
//
//   真正的 feature 在 features/*.hpp；本文件保持既有 include 路径不变
//   （session.hpp / test/access.hpp 不用改），G5 再收。
//   顺序 = AppRouter 的 tuple 顺序（F7 不动点）。
// ============================================================================

#include "features/filter_find.hpp"
#include "features/layout_ops.hpp"
#include "features/selection.hpp"
#include "features/edit.hpp"
#include "features/overlay.hpp"
#include "features/router.hpp"
