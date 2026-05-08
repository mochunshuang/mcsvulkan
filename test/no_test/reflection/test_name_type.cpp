#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <exception>
#include <meta>
#include <vector>
#include <string_view>

namespace meta = std::meta; // 必须的别名

struct type_registry // NOLINT
{
    using name_type = std::string_view;
    using id_type = std::uint32_t;

    // 编译期获取类型名，返回静态存储的 string_view（安全）
    template <typename T>
    static consteval name_type get_type_name()
    {
        // NOLINTNEXTLINE
        constexpr auto *ptr = std::define_static_string(meta::display_string_of(^^T));
        return name_type{ptr};
    }

    // 运行时自动注册/查询类型 ID（注意：不能是 constexpr）
    template <typename T>
    id_type registry() // 非 constexpr
    {
        constexpr name_type name = get_type_name<T>(); // NOLINT
        auto it = std::ranges::find(types_, name);
        if (it != types_.end())
            return ids_[std::distance(types_.begin(), it)];

        auto new_id = static_cast<id_type>(types_.size());
        types_.emplace_back(name);
        ids_.emplace_back(new_id); // ids_ 存的是 ID 值
        return new_id;
    }

  private:
    std::vector<name_type> types_; // 按注册顺序保存名字
    std::vector<id_type> ids_;     // 保存对应的 ID（下标）
};

// 验证编译期名字获取
static_assert(type_registry::get_type_name<int>() == std::string_view{"int"});

int main()
try
{
    type_registry types;
    auto id = types.registry<int>();
    assert(id == 0);
    id = types.registry<int>();
    assert(id == 0); // 重复注册返回相同 ID

    id = types.registry<double>();
    assert(id == 1);

    std::cout << "main done\n";
    return 0;
}
catch (const std::exception &e)
{
    std::cerr << "Exception: " << e.what() << '\n';
    return 1;
}
catch (...)
{
    std::cerr << "Unknown exception\n";
    return 1;
}