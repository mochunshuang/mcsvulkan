#pragma once
// ============================================================================
// ui_backend.hpp —— 无状态解码器：记录 → vkCmd*
//
//   每条记录展开成**固定条数**的 C 调用；顺序不变、无 last*、无二次遍历。
//   本文件不含 Vulkan：它只认识 Handle 和 Sink 概念。
// ============================================================================
#include "ui_protocol.hpp"

#include <concepts>
#include <cstdint>
#include <span>

namespace ui_gfx
{
    struct Handle
    {
        uint64_t v{};
        const char *name{"-"};
    };

    struct PipelineDesc
    {
        Handle h{};
        uint32_t layout{0};
    };

    struct Registry
    {
        std::span<const PipelineDesc> pipelines;
        std::span<const Handle> layouts;
        std::span<const Handle> descSets;
        Handle ibo{};
    };

    // 帧内 arena：一帧的所有 pass 顺序追加，绝不覆盖
    struct Arena
    {
        uint64_t heap{0};
        uint64_t cc{0};
        uint64_t ind{0};
        bool upload{true}; // false = 复上一帧的数据，纯重放
    };

    // ── 解码器 ──────────────────────────────────────────────────────────
    template <class S>
    concept Sink = requires(S &s, Handle h, const ui_proto::ClipRect &cr,
                            std::span<const std::byte> bytes,
                            std::span<const ui_proto::Constant> consts,
                            std::span<const ui_proto::DrawCmd> cmds, uint64_t off,
                            uint32_t n) {
        { s.upload_data(bytes, off) } -> std::same_as<void>;
        { s.upload_cc(consts, off) } -> std::same_as<void>;
        { s.upload_ind(cmds, off) } -> std::same_as<void>;
        { s.set_pipeline(h) } -> std::same_as<void>;
        { s.set_descriptor_set(h) } -> std::same_as<void>;
        { s.set_index_buffer(h) } -> std::same_as<void>;
        { s.set_viewport() } -> std::same_as<void>;
        { s.set_scissor(cr) } -> std::same_as<void>;
        { s.set_stencil_ref(n) } -> std::same_as<void>;
        { s.begin_pass(n, true) } -> std::same_as<void>;
        { s.end_pass() } -> std::same_as<void>;
        { s.barrier(n, n, n) } -> std::same_as<void>;
        { s.blit(n, n, n, n) } -> std::same_as<void>;
        { s.push_segment(n) } -> std::same_as<void>;
        { s.draw(off, n) } -> std::same_as<void>;
    };

    // 一条记录 → 固定条数 vkCmd*
    template <class S>
    void decode_cmd(S &s, const ui_proto::Cmd &c, const ui_proto::List &l,
                    const Registry &r)
    {
        switch (c.kind)
        {
        case ui_proto::Kind::SetState:
            switch (static_cast<ui_proto::Lane>(c.a))
            {
            case ui_proto::Lane::Pipeline: s.set_pipeline(r.pipelines[c.b].h); break;
            case ui_proto::Lane::Clip: s.set_scissor(l.clips[c.b]); break;
            case ui_proto::Lane::Stencil: s.set_stencil_ref(c.b); break;
            default: break;
            }
            break;
        case ui_proto::Kind::Draw:
            s.push_segment(c.extra); // 段首常量下标 → 1× pushConstants
            s.draw(c.offset, c.a);   // 间接命令下标 + k → 1× drawIndexedIndirect
            break;
        case ui_proto::Kind::BeginPass: s.begin_pass(c.a, c.b != 0); break;
        case ui_proto::Kind::EndPass: s.end_pass(); break;
        case ui_proto::Kind::Barrier: s.barrier(c.a, c.b, c.c); break;
        case ui_proto::Kind::Blit:
            s.blit(c.a, c.b, c.offset & 0xFFFFu, c.offset >> 16);
            break;
        }
    }

    // 一个 pass：prologue（3 条）+ 逐条记录
    template <class S>
    void decode(S &s, const ui_proto::List &l, const Registry &r, Arena &a,
                uint32_t desc_set = 0)
    {
        if (a.upload)
        {
            s.upload_data(std::span<const std::byte>{l.data}, a.heap);
            s.upload_cc(std::span<const ui_proto::Constant>{l.cc}, a.cc);
            s.upload_ind(std::span<const ui_proto::DrawCmd>{l.ind}, a.ind);
        }

        // pass 级：不变量，只发一次（若流里第一条是 BeginPass，则由它负责）
        if (l.cmds.empty() || l.cmds.front().kind != ui_proto::Kind::BeginPass)
        {
            s.set_descriptor_set(r.descSets[desc_set]);
            s.set_index_buffer(r.ibo);
            s.set_viewport();
        }

        for (const auto &c : l.cmds)
            decode_cmd(s, c, l, r);

        // 游标推进：下一个 pass 从新位置开始
        a.heap += l.data.size();
        a.cc += l.cc.size() * sizeof(ui_proto::Constant);
        a.ind += l.ind.size() * sizeof(ui_proto::DrawCmd);
    }

} // namespace ui_gfx
