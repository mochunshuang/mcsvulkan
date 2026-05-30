#include <iostream>
#include <memory>

struct Tracker
{
    int id;
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
        o.id = -1;
        ++alive;
        std::cout << "Move constructor: id=" << id << " (alive=" << alive << ")\n";
    }
    ~Tracker()
    {
        --alive;
        std::cout << "Destructor: id=" << id << " (alive=" << alive << ")\n";
    }
};

int main()
{
    Tracker src[3] = {10, 20, 30};

    auto *raw = static_cast<Tracker *>(::operator new(3 * sizeof(Tracker)));

    std::cout << "\n--- Calling uninitialized_copy_n ---\n";
    std::uninitialized_copy_n(src, 3, raw);

    std::cout << "\n--- Source after copy ---\n";
    std::cout << "src[0].id = " << src[0].id << '\n';
    std::cout << "src[1].id = " << src[1].id << '\n';
    std::cout << "src[2].id = " << src[2].id << '\n';

    std::destroy(raw, raw + 3);
    ::operator delete(raw);
}