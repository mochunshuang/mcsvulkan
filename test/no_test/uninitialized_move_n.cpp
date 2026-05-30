#include <iostream>
#include <memory>
#include <utility>

struct Tracker
{
    int id;
    static inline int next_id = 0;
    static inline int alive = 0;

    Tracker(int v) : id(v)
    {
        ++alive;
        std::cout << "Constructor: id=" << id << " (alive=" << alive << ")\n";
    }
    Tracker(const Tracker &o) : id(o.id)
    {
        ++alive;
        std::cout << "Copy constructor: id=" << id << " (alive=" << alive << ")\n";
    }
    Tracker(Tracker &&o) noexcept : id(o.id)
    {
        o.id = -1; // 标记移动后状态
        ++alive;
        std::cout << "Move constructor: id=" << id << " (alive=" << alive << ")\n";
    }
    ~Tracker()
    {
        --alive;
        std::cout << "Destructor: id=" << id << " (alive=" << alive << ")\n";
    }
};

//  NOTE: 会调用移动构造函数
int main()
{
    // 在栈上创建三个对象
    Tracker arr[3] = {10, 20, 30};

    // 分配原始内存（未初始化）
    auto *raw = static_cast<Tracker *>(::operator new(3 * sizeof(Tracker)));

    std::cout << "\n--- Calling uninitialized_move_n ---\n";

    // 移动 3 个对象到 raw 内存
    auto [dest_end, src_end] = std::uninitialized_move_n(arr, 3, raw);

    std::cout << "\n--- After move ---\n";
    std::cout << "Source array elements ids: " << arr[0].id << ", " << arr[1].id << ", "
              << arr[2].id << "\n";

    // 手动析构目标区对象
    std::destroy(raw, raw + 3);
    ::operator delete(raw);

    // 源数组 arr 仍然需要析构（它们处于移动后状态，但析构无害）
    // 这里 arr 是栈数组，会自动析构
}