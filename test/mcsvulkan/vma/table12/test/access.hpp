#pragma once
// ============================================================================
// table12/test/access.hpp — 测试专用访问器（production 不使用）
//
//   Session::Impl 是私有的；selftest 需要直接驱动 feature（sy.get<>()）与
//   pipe，所以这里通过 friend 暴露引用，不改生产头。
// ============================================================================

#include "../detail/runtime.hpp"
#include "../session.hpp"

namespace table::test
{
struct SessionAccess
{
    static detail::AppRouter &router(session::Session &s) noexcept
    {
        return s.impl->router;
    }
    static frame::RowPipeline &pipe(session::Session &s) noexcept
    {
        return s.impl->pipe;
    }
};

struct Access
{
    detail::AppRouter &router;
    frame::RowPipeline &pipe;
};

inline Access access(session::Session &s) noexcept
{
    return Access{SessionAccess::router(s), SessionAccess::pipe(s)};
}
} // namespace table::test
