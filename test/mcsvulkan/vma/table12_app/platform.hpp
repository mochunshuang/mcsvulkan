#pragma once
// ============================================================================
// table12_app/platform.hpp — GPU/事件 ABI 端口（唯一 GPU 出口）
//
//   encode：DrawList -> shader instances（entity_index = TargetKey）
//     纯数据变换：不读窗口、不改 Model/State、不做 I/O。
//   resolve：TargetKey -> Target 在 table::model::resolve（纯函数，picking 回读用）。
//
//   hover_fn 由 window::Host 提供（ability11 的 hover_fn_ref 全局已删除）。
//
//   ⚠ 本头是 app 层：它认识 ui_dao 的 shader 实例类型，必须在 ui_dao 定义之后
//     被 include；核心（table12/）永远不 include 它。
// ============================================================================

#include "../table12/frame.hpp"
#include "../table12/model.hpp"
#include "shader_abi.hpp"

namespace table::platform
{
// ── 唯一 GPU 入口：TargetKey → Target（picking 回读） ─────────────────
//   与 HANDOFF §6.4 草案的差异：Target 需要 row_id/column_id，必须查 model；
//   model::resolve 仍是纯函数，但只有这里调用它。
[[nodiscard]] inline Target resolve(const model::Table &m, const PickingResult &r)
{
    return model::resolve(m, r.key.entity_index);
}

// 调试/合并检查用：手上只有 entity_index（例如 shader instance），没有完整 PickingResult
[[nodiscard]] inline Target resolve(const model::Table &m, TargetKey key)
{
    return model::resolve(m, key);
}
// ── 字形裁剪（encode 的私有工具，纯几何） ───────────────────────────
} // namespace table::platform
