#pragma once
// ============================================================================
// ui_protocol.hpp —— Widget 与后端之间唯一的接口：一条命令记录流
//
//   [Cmd][Cmd][Cmd] ...        ← 每条记录 16B
//   payload 在 data、间接命令在 ind、命令常量在 cc（三个 blob，按偏移解耦）
//
//   编码（Encoder/Canvas）：前端把"要画什么"写成记录；状态只在变化时写 SetState。
//   解码（decodeCmd）      ：后端逐条展开成 vkCmd*，**不持有任何状态**。
//
//   本文件不含 Vulkan 类型。
// ============================================================================
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace ui_proto
{
    // ── 1. 记录种类 ─────────────────────────────────────────────────────
    enum class Kind : uint16_t
    {
        SetState = 1,  // a = lane, b = value
        Draw = 2,      // a = k, b = type, c = geo, offset = ind 下标, extra = cc 下标
        BeginPass = 3, // a = target id, b = clear
        EndPass = 4,
        Barrier = 5, // a = image id, b = from Usage, c = to Usage
        Blit = 6,    // a = src, b = dst, c = w, d = h（源/目标 usage 由目标表给）
    };

    // 资源用途：**语义**，不是 VkImageLayout。后端负责映射。
    enum class Usage : uint16_t
    {
        None = 0,
        ColorTarget,
        DepthTarget,
        Sampled,
        TransferSrc,
        TransferDst,
        Present,
    };

    enum class Lane : uint16_t
    {
        Pipeline = 0,
        Clip,
        Stencil,
        Count
    };

    constexpr uint16_t lane_bit(Lane l) noexcept { return static_cast<uint16_t>(1u << static_cast<uint32_t>(l)); }
    inline constexpr uint16_t kLanesAll = 0xFFFF;
    inline constexpr uint16_t kLanesBasic = lane_bit(Lane::Pipeline) | lane_bit(Lane::Clip);

    using TypeId = uint16_t; // shader ABI（app 自定义）
    using PipeId = uint16_t; // 管线 id（registry 下标）
    using ClipId = uint16_t;

    constexpr const char *name(Kind k) noexcept
    {
        switch (k)
        {
        case Kind::SetState: return "SetState";
        case Kind::Draw: return "Draw";
        case Kind::BeginPass: return "BeginPass";
        case Kind::EndPass: return "EndPass";
        case Kind::Barrier: return "Barrier";
        case Kind::Blit: return "Blit";
        }
        return "?";
    }

    constexpr const char *name(Usage u) noexcept
    {
        switch (u)
        {
        case Usage::None: return "None";
        case Usage::ColorTarget: return "ColorTarget";
        case Usage::DepthTarget: return "DepthTarget";
        case Usage::Sampled: return "Sampled";
        case Usage::TransferSrc: return "TransferSrc";
        case Usage::TransferDst: return "TransferDst";
        case Usage::Present: return "Present";
        }
        return "?";
    }
    constexpr const char *name(Lane l) noexcept
    {
        switch (l)
        {
        case Lane::Pipeline: return "Pipeline";
        case Lane::Clip: return "Clip";
        case Lane::Stencil: return "Stencil";
        default: return "?";
        }
    }

    // ── 2. 记录：16 字节，POD ───────────────────────────────────────────
    struct Cmd
    {
        Kind kind{Kind::SetState};
        uint16_t a{0};
        uint16_t b{0};
        uint16_t c{0};
        uint32_t offset{0}; // Blit: (w | h<<16)，其余见各处
        uint32_t extra{0};
    };
    static_assert(sizeof(Cmd) == 16);

    struct ClipRect
    {
        float x{}, y{}, w{}, h{};
        bool operator==(const ClipRect &) const = default;
    };

    struct Geometry
    {
        uint32_t firstIndex{0};
        uint32_t indexCount{0};
        uint32_t vertexOffset{0};
        bool operator==(const Geometry &) const = default;
    };

    // 与 GPU 结构逐字节同构（解码端 static_assert）
    struct DrawCmd
    {
        uint32_t indexCount{0};
        uint32_t instanceCount{0};
        uint32_t firstIndex{0};
        int32_t vertexOffset{0};
        uint32_t firstInstance{0};
    };
    static_assert(sizeof(DrawCmd) == 20);

    struct Constant
    {
        uint32_t typeId{0};
        uint32_t addrOffset{0};
        uint32_t slotCount{0};
    };
    static_assert(sizeof(Constant) == 12);

    // ── 3. 数据：四个平行数组 + 两张旁表 ────────────────────────────────
    struct List
    {
        std::vector<Cmd> cmds;
        std::vector<std::byte> data; // 实例 payload
        std::vector<DrawCmd> ind;    // 间接命令
        std::vector<Constant> cc;    // 命令常量
        std::vector<ClipRect> clips;
        std::vector<Geometry> geos;
        const char *(*type_name)(TypeId) noexcept = nullptr;

        void reset()
        {
            cmds.clear();
            data.clear();
            ind.clear();
            cc.clear();
            clips.clear();
            geos.clear();
        }
        void reserve(size_t nCmds, size_t nBytes, size_t nClips = 8)
        {
            cmds.reserve(nCmds);
            data.reserve(nBytes);
            ind.reserve(nCmds);
            cc.reserve(nCmds);
            clips.reserve(nClips);
            geos.reserve(8);
        }
        [[nodiscard]] size_t bytes() const noexcept { return data.size(); }
    };

    // ── 4. 编码器：Widget 只看见这个 ────────────────────────────────────
    // Program = shader 的三个侧面（管线 / 类型 / 记录步长）。加一个能力 = 加一行表。
    struct Program
    {
        PipeId pipe{0};
        TypeId type{0};
        uint32_t stride{0};
        const char *name{"?"};
        // 该 program 允许动态设置的轴。管线没开 dynamic 的轴不能碰（VUID 08608）。
        uint16_t lanes{kLanesAll};
    };

    class CmdEncoder
    {
      public:
        static constexpr uint16_t kDefaultGeo = 0xFFFFu;

        CmdEncoder(List &l, float w, float h, Geometry def = {0, 6, 0})
            : l_(l), w_(w), h_(h)
        {
            l_.reset();
            def_geo_ = geom(def);
        }

        [[nodiscard]] float width() const noexcept { return w_; }
        [[nodiscard]] float height() const noexcept { return h_; }
        [[nodiscard]] uint16_t default_geo() const noexcept { return def_geo_; }

        // ── pass 边界：也是记录，不是"库的组织结构" ────────────────────
        void begin_pass(uint16_t target, bool clear = false)
        {
            l_.cmds.push_back(Cmd{.kind = Kind::BeginPass,
                                  .a = target,
                                  .b = static_cast<uint16_t>(clear ? 1 : 0)});
            have_state_ = false; // 新 pass：状态视为未设置，下一条 draw 重新发
            run_open_ = false;
        }
        void end_pass()
        {
            l_.cmds.push_back(Cmd{.kind = Kind::EndPass});
            run_open_ = false;
        }

        // 屏障：只表达"从哪种用途到哪种用途"，不出现 layout
        void barrier(uint16_t image, Usage from, Usage to)
        {
            l_.cmds.push_back(
                Cmd{.kind = Kind::Barrier,
                    .a = image,
                    .b = static_cast<uint16_t>(from),
                    .c = static_cast<uint16_t>(to)});
        }
        void blit(uint16_t src, uint16_t dst, uint32_t w, uint32_t h)
        {
            l_.cmds.push_back(Cmd{.kind = Kind::Blit,
                                  .a = src,
                                  .b = dst,
                                  .offset = (w & 0xFFFFu) | (h << 16)});
        }

        ClipId clip(float x, float y, float w, float h)
        {
            const ClipRect r{x, y, w, h};
            for (size_t i = 0; i < l_.clips.size(); ++i)
                if (l_.clips[i] == r)
                    return static_cast<ClipId>(i);
            l_.clips.push_back(r);
            return static_cast<ClipId>(l_.clips.size() - 1);
        }
        ClipId full_clip() { return clip(0.0F, 0.0F, w_, h_); }

        uint16_t geom(const Geometry &g)
        {
            for (size_t i = 0; i < l_.geos.size(); ++i)
                if (l_.geos[i] == g)
                    return static_cast<uint16_t>(i);
            l_.geos.push_back(g);
            return static_cast<uint16_t>(l_.geos.size() - 1);
        }
        uint16_t geom(uint32_t firstIndex, uint32_t indexCount, uint32_t vertexOffset = 0)
        {
            return geom(Geometry{firstIndex, indexCount, vertexOffset});
        }

        // ── 唯一的绘制出口 ──────────────────────────────────────────────
        template <class T>
        void draw(const Program &p, ClipId clip, std::span<const T> xs,
                  uint16_t stencil = 0, uint16_t geo = kDefaultGeo)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            static_assert(alignof(T) <= 16);
            assert(p.stride == sizeof(T) && "payload 与 shader 的记录步长不一致");
            encode(p.pipe, clip, stencil, p.type, geo == kDefaultGeo ? def_geo_ : geo,
                   xs.data(), static_cast<uint32_t>(xs.size()),
                   static_cast<uint32_t>(sizeof(T)), p.lanes);
        }

        // 变长 payload（路径 blob 等）。lanes 指定该 draw 允许动哪些轴。
        void draw_raw(PipeId pipe, ClipId clip, TypeId type, const void *src,
                      uint32_t count, uint32_t stride, uint16_t geo = kDefaultGeo,
                      uint16_t stencil = 0, uint16_t lanes = kLanesAll)
        {
            encode(pipe, clip, stencil, type, geo == kDefaultGeo ? def_geo_ : geo, src,
                   count, stride, lanes);
        }

        [[nodiscard]] const List &list() const noexcept { return l_; }

      private:
        // 状态只在变化时写 SetState —— 这是编码器唯一的状态。
        // lanes 是该 program 允许动态设置、且管线确实开了 dynamic 的轴：
        // 对没开 dynamic 的轴调用 vkCmdSet* 是 VUID 违规（08608），所以必须过滤。
        void set_lane(Lane lane, uint16_t value, uint16_t lanes)
        {
            if ((lanes & lane_bit(lane)) == 0)
                return;
            uint16_t &slot = lane == Lane::Pipeline ? last_pipe_
                             : lane == Lane::Clip   ? last_clip_
                                                    : last_stencil_;
            if (slot == value)
                return;
            slot = value;
            l_.cmds.push_back(
                Cmd{.kind = Kind::SetState, .a = static_cast<uint16_t>(lane), .b = value});
            run_open_ = false; // 状态一变，multi-draw 的段就断了
        }

        void encode(PipeId pipe, ClipId clip, uint16_t stencil, TypeId type,
                    uint16_t geo, const void *src, uint32_t count, uint32_t stride,
                    uint16_t lanes)
        {
            if (count == 0)
                return;

            const bool same_state =
                have_state_ && pipe == last_pipe_ && clip == last_clip_ &&
                stencil == last_stencil_;
            set_lane(Lane::Pipeline, pipe, lanes);
            set_lane(Lane::Clip, clip, lanes);
            set_lane(Lane::Stencil, stencil, lanes);
            have_state_ = true;

            // 与上一条同 stride 时紧挨着放（保证可合并），否则 16 对齐
            const size_t off = (last_stride_ == stride && !l_.data.empty())
                                   ? l_.data.size()
                                   : (l_.data.size() + 15u) / 16u * 16u;
            l_.data.resize(off + size_t(count) * stride);
            std::memcpy(l_.data.data() + off, src, size_t(count) * stride);
            last_stride_ = stride;

            const Geometry &g = l_.geos[geo];

            // ① 同段 + 同几何 + 同类型 + payload 连续 → 合并进上一条间接命令
            if (same_state && run_open_ && last_geo_ == geo && last_type_ == type &&
                last_off_ + uint64_t(last_cnt_) * stride == off)
            {
                l_.ind.back().instanceCount += count;
                last_cnt_ += count;
                return;
            }

            // ② 追加一条间接命令 + 一条命令常量
            l_.ind.push_back(DrawCmd{g.indexCount, count, g.firstIndex,
                                     static_cast<int32_t>(g.vertexOffset), 0});
            l_.cc.push_back(Constant{type, static_cast<uint32_t>(off), 0});
            last_geo_ = geo;
            last_type_ = type;
            last_off_ = off;
            last_cnt_ = count;

            // ③ 同段内继续追加 → 只把当前 Draw 记录的 k 加一（一次 multi-draw）
            if (same_state && run_open_)
            {
                l_.cmds.back().a = static_cast<uint16_t>(l_.cmds.back().a + 1);
                return;
            }

            // ④ 开一个新段
            l_.cmds.push_back(
                Cmd{.kind = Kind::Draw,
                    .a = 1,
                    .b = type,
                    .c = geo,
                    .offset = static_cast<uint32_t>(l_.ind.size() - 1),
                    .extra = static_cast<uint32_t>(l_.cc.size() - 1)});
            run_open_ = true;
        }

        List &l_;
        float w_, h_;
        uint16_t def_geo_{0};
        uint16_t last_pipe_{0xFFFF}, last_clip_{0xFFFF}, last_stencil_{0xFFFF};
        uint16_t last_geo_{0xFFFF}, last_type_{0xFFFF};
        size_t last_off_{0};
        uint32_t last_cnt_{0}, last_stride_{0};
        bool have_state_{false}, run_open_{false};
    };

    // ── 5. 调试：数字 + 字符串并排 ──────────────────────────────────────
    inline std::string describe(const List &l, size_t i)
    {
        const Cmd &c = l.cmds[i];
        char buf[192];
        if (c.kind == Kind::SetState)
            std::snprintf(buf, sizeof buf, "#%zu SetState %s(%u) = %u", i,
                          name(static_cast<Lane>(c.a)), unsigned(c.a), unsigned(c.b));
        else if (c.kind == Kind::BeginPass)
            std::snprintf(buf, sizeof buf, "#%zu BeginPass target=%u clear=%u", i,
                          unsigned(c.a), unsigned(c.b));
        else if (c.kind == Kind::EndPass)
            std::snprintf(buf, sizeof buf, "#%zu EndPass", i);
        else if (c.kind == Kind::Barrier)
            std::snprintf(buf, sizeof buf, "#%zu Barrier image=%u %s->%s", i,
                          unsigned(c.a), name(static_cast<Usage>(c.b)),
                          name(static_cast<Usage>(c.c)));
        else if (c.kind == Kind::Blit)
            std::snprintf(buf, sizeof buf, "#%zu Blit %u->%u %ux%u", i, unsigned(c.a),
                          unsigned(c.b), unsigned(c.offset & 0xFFFF),
                          unsigned(c.offset >> 16));
        else
            std::snprintf(buf, sizeof buf,
                          "#%zu Draw type=%s(%u) geo=%u k=%u ind@%u cc@%u", i,
                          l.type_name ? l.type_name(c.b) : "?", unsigned(c.b),
                          unsigned(c.c), unsigned(c.a), c.offset, c.extra);
        return buf;
    }

} // namespace ui_proto
