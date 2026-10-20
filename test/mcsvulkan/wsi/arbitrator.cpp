#include <iostream>
#include <exception>
// NOLINTBEGIN
// NOTE: 判决: 证据 + 人 + 动作. 多个人. 这群人直接
// NOTE: [a b c d] 有必要吗?还是有必要的. 按类型区分如? 是的,还是丢失
// NOTE: 完成的时间,过了多少个事件,

struct related_timeline
{
};
struct arbitrator
{
};
int main()
try
{
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
} // NOLINTEND