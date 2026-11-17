#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>

// NOLINTBEGIN

class ByteBuffer
{
  public:
    ByteBuffer() = default;
    explicit ByteBuffer(std::size_t cap)
        : data_(cap ? std::make_unique_for_overwrite<uint8_t[]>(cap) : nullptr),
          capacity_(cap)
    {
    }

    // 写入任意 trivially copyable 的 T
    template <class T>
    constexpr void write(const T &v)
    {
        static_assert(std::is_trivially_copyable_v<T>,
                      "ByteBuffer 只接受 trivially copyable 的类型");
        write_bytes(&v, sizeof(T));
    }

    // 写入 T 的数组
    template <class T>
    constexpr void write_array(const T *p, std::size_t n)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        write_bytes(p, n * sizeof(T));
    }

    // 原始字节写入（内部统一走这里）
    constexpr void write_bytes(const void *src, std::size_t n)
    {
        reserve(size_ + n);
        std::memcpy(data_.get() + size_, src, n);
        size_ += n;
    }

    // 按偏移读出 T
    template <class T>
    constexpr T read(std::size_t off) const
    {
        static_assert(std::is_trivially_copyable_v<T>);
        if (off + sizeof(T) > size_)
            throw std::out_of_range("ByteBuffer::read out of range");
        T v;
        std::memcpy(&v, data_.get() + off, sizeof(T));
        return v;
    }

    constexpr uint8_t *data() noexcept
    {
        return data_.get();
    }
    constexpr const uint8_t *data() const noexcept
    {
        return data_.get();
    }
    constexpr std::size_t size() const noexcept
    {
        return size_;
    }
    constexpr std::size_t capacity() const noexcept
    {
        return capacity_;
    }
    constexpr void clear() noexcept
    {
        size_ = 0;
    }

  private:
    // 自动扩容：容量不足时按 2 倍增长，只搬运 size_ 个有效字节
    void reserve(std::size_t need)
    {
        if (need <= capacity_)
            return;

        const std::size_t max = std::numeric_limits<std::size_t>::max();
        std::size_t newcap = capacity_ ? capacity_ : 64;
        while (newcap < need)
        {
            if (newcap > max / 2)
            {
                newcap = need;
                break;
            } // 防溢出
            newcap *= 2;
        }

        // 注意：make_unique_for_overwrite 不初始化内存
        auto nd = std::make_unique_for_overwrite<uint8_t[]>(newcap);
        if (size_)
            std::memcpy(nd.get(), data_.get(), size_);
        data_ = std::move(nd);
        capacity_ = newcap;
    }

    std::unique_ptr<uint8_t[]> data_;
    std::size_t size_ = 0;
    std::size_t capacity_ = 0;
};

struct Pod
{
    int a;
    char b;
    double c;
}; // 16 字节

int main()
{
    ByteBuffer bb;
    std::cout << "init  : size=" << bb.size() << " cap=" << bb.capacity() << "\n";

    auto log = [&](const char *what) {
        std::cout << "after " << what << " : size=" << bb.size()
                  << " cap=" << bb.capacity() << "\n";
    };

    // 1) 各种标量
    bb.write<uint8_t>(0xAB);
    log("uint8_t  (1B)");
    bb.write<int16_t>(-1234);
    log("int16_t  (2B)");
    bb.write<int32_t>(0x12345678);
    log("int32_t  (4B)");
    bb.write<uint64_t>(0xDEADBEEFCAFEBABEull);
    log("uint64_t (8B)");
    bb.write<float>(3.14f);
    log("float    (4B)");
    bb.write<double>(2.718281828459045);
    log("double   (8B)");
    bb.write<char>('Z');
    log("char     (1B)");

    // 2) POD 结构体
    Pod p{42, 'Q', 1.5};
    bb.write(p);
    log("Pod      (16B)");

    // 3) 数组
    int arr[5] = {1, 2, 3, 4, 5};
    bb.write_array(arr, 5);
    log("int[5]   (20B)");

    // 4) 字符串
    const char *s = "hello";
    bb.write_array(s, std::strlen(s) + 1);
    log("\"hello\" (6B)");

    // ---------- 回读全部校验 ----------
    std::size_t off = 0;
    assert(bb.read<uint8_t>(off) == 0xAB);
    off += sizeof(uint8_t);
    assert(bb.read<int16_t>(off) == -1234);
    off += sizeof(int16_t);
    assert(bb.read<int32_t>(off) == 0x12345678);
    off += sizeof(int32_t);
    assert(bb.read<uint64_t>(off) == 0xDEADBEEFCAFEBABEull);
    off += sizeof(uint64_t);
    assert(bb.read<float>(off) == 3.14f);
    off += sizeof(float);
    assert(bb.read<double>(off) == 2.718281828459045);
    off += sizeof(double);
    assert(bb.read<char>(off) == 'Z');
    off += sizeof(char);

    Pod rp = bb.read<Pod>(off);
    off += sizeof(Pod);
    assert(rp.a == 42 && rp.b == 'Q' && rp.c == 1.5);

    int rarr[5];
    std::memcpy(rarr, bb.data() + off, sizeof(rarr));
    off += sizeof(rarr);
    for (int i = 0; i < 5; ++i)
        assert(rarr[i] == i + 1);

    assert(std::strcmp(reinterpret_cast<const char *>(bb.data() + off), "hello") == 0);
    off += 6;

    assert(off == bb.size());
    std::cout << "all assertions passed, final size=" << bb.size()
              << ", cap=" << bb.capacity() << "\n";
} // NOLINTEND