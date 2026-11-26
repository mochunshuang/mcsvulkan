#pragma once
// ============================================================================
// table12/detail/runtime.hpp — Session 的私有实现（只给 session.cpp / test access）
//
//   AppRouter（15 feature 的有序组合）+ RowPipeline（行变换缓冲）。
//   session.hpp 不 include 本头：公共头只有 schema + 门声明。
// ============================================================================

#include "../frame.hpp"
#include "features/router.hpp"

namespace table::session
{
struct Session::Impl
{
    detail::AppRouter router{};   // CommandRouter
    frame::RowPipeline pipe{};    // filter/sort 写, compose 读
};
} // namespace table::session
