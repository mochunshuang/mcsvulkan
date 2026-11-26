#pragma once
// ============================================================================
// table12_app/platform.hpp — GPU/picking ABI 端口（唯一 GPU 入口）
//
//   resolve：PickingResult(TargetKey) -> Target；内部只调 model::resolve（纯函数）。
//   与 HANDOFF §6.4 草案的差异：Target 需要 row_id/column_id，必须查 model。
//
//   encode（唯一 GPU 出口）在 table12_app/encode.hpp；本头不依赖 ui_dao。
//   本头是 app 层：核心（table12/）永远不 include 它。
// ============================================================================

#include "../table12/frame.hpp"
#include "../table12/model.hpp"
#include "shader_abi.hpp"

namespace table::platform
{
// ── 唯一 GPU 入口：TargetKey → Target（picking 回读） ─────────────────
[[nodiscard]] inline Target resolve(const model::Table &m, const PickingResult &r)
{
    return model::resolve(m, r.key.entity_index);
}

// 调试/合并检查用：手上只有 entity_index（例如 shader instance），没有完整 PickingResult。
// G4 起改名为 resolve_key，保证 resolve 只有一处（五门唯一）。
[[nodiscard]] inline Target resolve(const model::Table &m, TargetKey key)
{
    return model::resolve(m, key);
}
} // namespace table::platform
