#pragma once
// ============================================================================
// ui_shader_abi.hpp —— GPU 侧协议（shader ABI）的公共概念
//
//   分层（单向依赖，不许反向）：
//     ui_proto（D：头 + 数据）        ← 不认识 GPU
//     ui_abi  （本文件：类型表）       ← 不认识 Vulkan
//     ui_gfx  （consume / execute）   ← 不认识具体 app
//     *.cpp   （每个 app 的 ABI 实例 + sink）
//
//   一条 ABI 记录 = { type id, stride, align, name }。
//   CPU 端写 (id, stride)，这里声明 stride 的**权威值**；
//   consume 会逐条比对，C++ 结构体 / 协议 / shader 三者只要有一处漂移就会在
//   第一帧断言，而不是靠肉眼。
// ============================================================================
#include "ui_protocol.hpp"

#include <cstdint>
#include <span>

namespace ui_abi
{
    struct TypeDesc
    {
        ui_proto::TypeId id{0};
        uint32_t stride{0}; // = sizeof(记录)；0 表示变长 blob（不校验）
        uint32_t align{0};
        const char *name{"?"};
    };

    struct TypeTable
    {
        std::span<const TypeDesc> types;

        [[nodiscard]] const TypeDesc *find(ui_proto::TypeId id) const noexcept
        {
            for (const auto &t : types)
                if (t.id == id)
                    return &t;
            return nullptr;
        }
    };

    // push 常量块的公共形状：3×u64 + 2×u32 = 32 字节。
    // 两个 app 的结构体字段名不同，但**布局必须同构**——各自用 static_assert 钉住。
    inline constexpr uint32_t kPushSize = 32;

    struct PushBlock
    {
        uint64_t s0{}, s1{}, s2{};
        uint32_t s3{}, s4{};
    };
    static_assert(sizeof(PushBlock) == kPushSize);

    // 说明：stride 现在由编码器写入间接命令/常量，不再存在记录里；
    // 表仍保留为“type id → 名字/大小”的权威声明，供日志与断言使用。
} // namespace ui_abi
