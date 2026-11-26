#pragma once
// ============================================================================
// table12_app/shader_abi.hpp — shader 侧 picking ABI（F2 不动点）
//
//   ObjectKey     8B  ：object_type(32) + entity_index(32)；entity_index 就是 TargetKey
//   RenderVersion 16b ：帧版本号（回绕到 mask 后归零），与 GLSL 侧位宽同步
//   PickingResult 16B ：R32G32B32A32_UINT 附件 + frag uvec4 输出严格对齐
//                        xy = ObjectKey；z = version<<16 | primitive；w = hover_fn
//
//   这个头只依赖 std，不认识 ui_new/GLFW/Vulkan；demo cpp 在框架类型之前 include 它。
//   与 ability11 的 types 同构（名字换成 platform::*），位布局一个 bit 都没改。
// ============================================================================

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>

namespace table::platform
{
// ── ObjectKey：8B，picking 的实体外键 ────────────────────────────────
struct ObjectKey
{
    std::uint32_t object_type{};
    std::uint32_t entity_index{}; // == table::TargetKey
    auto operator<=>(const ObjectKey &) const = default;
};
static_assert(sizeof(ObjectKey) == 2 * sizeof(std::uint32_t));

// ── RenderVersion：按位宽选底层整数，自增到 mask 回绕 ────────────────
template <std::size_t Bits>
class RenderVersion
{
    static_assert(Bits >= 1 && Bits <= 32, "RenderVersion: bit width must be in [1, 32]");

  public:
    template <std::size_t B>
    using best_uint_t = std::conditional_t<(B <= 8), std::uint8_t,
                                           std::conditional_t<(B <= 16), std::uint16_t,
                                                              std::uint32_t>>;

    using underlying_t = best_uint_t<Bits>;
    static constexpr std::size_t bit_width = Bits;

    static constexpr underlying_t mask = [] {
        if constexpr (Bits >= 32)
            return static_cast<underlying_t>(~std::uint32_t{0});
        else
            return static_cast<underlying_t>((std::uint32_t{1} << Bits) - 1u);
    }();

    constexpr RenderVersion() noexcept = default;
    constexpr explicit RenderVersion(std::uint32_t v) noexcept
        : value_{static_cast<underlying_t>(v & mask)}
    {
    }

    constexpr RenderVersion &operator++() noexcept
    {
        value_ = static_cast<underlying_t>((value_ + 1u) & mask);
        return *this;
    }
    constexpr RenderVersion operator++(int) noexcept
    {
        auto old = *this;
        ++*this;
        return old;
    }

    [[nodiscard]] constexpr operator underlying_t() const noexcept
    {
        return value_;
    }
    [[nodiscard]] constexpr std::uint32_t to_u32() const noexcept
    {
        return static_cast<std::uint32_t>(value_);
    }
    [[nodiscard]] constexpr bool operator==(const RenderVersion &) const noexcept =
        default;

  private:
    underlying_t value_ = 0;
};

// RENDER_VERSION_BIT_WIDTH = 16（与 GLSL 侧同步；F2 不动点）
using RenderVersion16 = RenderVersion<16>;

// ── PickingResult：16B，与 R32G32B32A32_UINT 附件逐字对齐 ─────────────
struct PickingResult
{
    using version_t = RenderVersion16;
    static constexpr std::size_t version_bits = version_t::bit_width;
    static constexpr std::size_t total_bits = 32;
    static constexpr std::size_t primitive_bits = total_bits - version_bits;
    static_assert(primitive_bits >= 1 && primitive_bits < total_bits,
                  "PickingResult: version_t leaves no room for primitive_id");

    using primitive_t = version_t::template best_uint_t<primitive_bits>;
    static constexpr std::uint32_t primitive_mask =
        (std::uint32_t{1} << primitive_bits) - 1u;

    ObjectKey key;          // outPicking.xy
    std::uint32_t packed;   // outPicking.z：高 version，低 primitive
    std::uint32_t hover_fn; // outPicking.w：池实体下标，0xFFFFFFFF = 未绑定

    [[nodiscard]] constexpr primitive_t primitive_id() const noexcept
    {
        return static_cast<primitive_t>(packed & primitive_mask);
    }
    [[nodiscard]] constexpr version_t::underlying_t render_version() const noexcept
    {
        return version_t{packed >> version_bits};
    }
    static constexpr std::uint32_t pack(version_t v, primitive_t p) noexcept
    {
        return (v.to_u32() << version_bits) |
               (static_cast<std::uint32_t>(p) & primitive_mask);
    }
};
static_assert(sizeof(PickingResult) == 16);
static_assert(PickingResult::version_bits == 16);
} // namespace table::platform

namespace std
{
template <>
struct hash<table::platform::ObjectKey>
{
    constexpr size_t operator()(const table::platform::ObjectKey &k) const noexcept
    {
        const size_t h1 = hash<std::uint32_t>{}(k.object_type);
        const size_t h2 = hash<std::uint32_t>{}(k.entity_index);
        return h1 ^ (h2 << 1);
    }
};
} // namespace std
