#include <optional>
#include <vector>
#include <string>
#include <ranges>
#include <algorithm>
#include <chrono>
#include <iostream>

struct task_info
{
    std::string name;
    int priority;
    std::optional<int> v;
};

// 封装好的 take_out
template <typename R, typename Pred>
consteval auto take_out(R &r, Pred pred)
{
    using value_type = std::ranges::range_value_t<R>;
    std::vector<value_type> taken;
    auto pivot = std::ranges::stable_partition(r, pred);
    std::ranges::move(std::ranges::subrange(r.begin(), pivot.begin()),
                      std::back_inserter(taken));
    r.erase(r.begin(), pivot.begin());
    return taken;
}

consteval auto test()
{
    std::vector<task_info> candidates = {
        {"fix_a", 1}, {"data_b", 2}, {"fix_c", 3}, {"data_d", 4}, {"fix_e", 5},
        {"fix_f", 6}, {"data_g", 7}, {"fix_h", 8}, {"data_i", 9}, {"fix_j", 10}};

    auto is_fixed = [](const task_info &t) {
        return t.name.starts_with("fix");
    };

    // 测试一：take_out
    auto removed = take_out(candidates, is_fixed);

    // std::cout << "Remaining (" << candidates.size() << "): ";
    // for (auto &t : candidates)
    //     std::cout << t.name << ' ';
    // std::cout << "\nTaken (" << removed.size() << "): ";
    // for (auto &t : removed)
    //     std::cout << t.name << ' ';
    // std::cout << '\n';
    return true;
}
int main()
{
    static_assert(test());
    return 0;
}