#include <algorithm>
#include <cassert>
#include <cstddef>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

// ============================================================
// IValue: 五个特殊成员显式给出，拷贝/移动设为 protected
// ============================================================
class IValue
{
  public:
    virtual ~IValue() = default;
    virtual std::unique_ptr<IValue> clone() const = 0;
    virtual void print(std::ostream &os) const = 0;

  protected:
    IValue() = default;
    IValue(const IValue &) = default;
    IValue(IValue &&) noexcept = default;
    IValue &operator=(const IValue &) = default;
    IValue &operator=(IValue &&) noexcept = default;
};

// ============================================================
// Model<T>: 五个特殊成员全部 = default
// ============================================================
template <class T>
class Model final : public IValue
{
    static_assert(std::is_copy_constructible_v<T>);
    static_assert(std::is_copy_assignable_v<T>);

  public:
    explicit Model(T v) : value_(std::move(v)) {}

    Model(const Model &) = default;
    Model(Model &&) noexcept = default;
    Model &operator=(const Model &) = default;
    Model &operator=(Model &&) noexcept = default;
    ~Model() override = default;

    [[nodiscard]] std::unique_ptr<IValue> clone() const override
    {
        return std::make_unique<Model>(*this);
    }
    void print(std::ostream &os) const override
    {
        os << value_;
    }

    const T &value() const noexcept
    {
        return value_;
    }
    T &value() noexcept
    {
        return value_;
    }

  private:
    T value_;
};

// ============================================================
// 带资源的类型
// ============================================================
struct Buffer
{
    std::unique_ptr<int[]> data;
    std::size_t size = 0;

    Buffer() = default;

    Buffer(std::initializer_list<int> init)
        : data(init.size() ? std::make_unique<int[]>(init.size()) : nullptr),
          size(init.size())
    {
        if (size)
            std::copy(init.begin(), init.end(), data.get());
    }

    Buffer(const Buffer &other)
        : data(other.size ? std::make_unique<int[]>(other.size) : nullptr),
          size(other.size)
    {
        if (size)
            std::copy(other.data.get(), other.data.get() + size, data.get());
    }

    Buffer(Buffer &&other) noexcept : data(std::move(other.data)), size(other.size)
    {
        other.size = 0;
    }

    Buffer &operator=(const Buffer &other)
    {
        if (this != &other)
        {
            Buffer tmp(other);
            *this = std::move(tmp);
        }
        return *this;
    }

    Buffer &operator=(Buffer &&other) noexcept
    {
        if (this != &other)
        {
            data = std::move(other.data);
            size = other.size;
            other.size = 0;
        }
        return *this;
    }

    ~Buffer() = default;

    int &operator[](std::size_t i) noexcept
    {
        return data[i];
    }
    const int &operator[](std::size_t i) const noexcept
    {
        return data[i];
    }

    friend bool operator==(const Buffer &a, const Buffer &b)
    {
        if (a.size != b.size)
            return false;
        for (std::size_t i = 0; i < a.size; ++i)
            if (a.data[i] != b.data[i])
                return false;
        return true;
    }

    friend std::ostream &operator<<(std::ostream &os, const Buffer &b)
    {
        os << '[';
        for (std::size_t i = 0; i < b.size; ++i)
        {
            if (i)
                os << ',';
            os << b.data[i];
        }
        return os << ']';
    }
};

// ============================================================
// AnyValue: 多态值语义包装器
// ============================================================
class AnyValue
{
  public:
    AnyValue() noexcept = default;

    template <class T, class D = std::decay_t<T>,
              class = std::enable_if_t<!std::is_same_v<D, AnyValue>>>
    AnyValue(T &&value) : ptr_(std::make_unique<Model<D>>(std::forward<T>(value)))
    {
    }

    AnyValue(const AnyValue &other) : ptr_(other.ptr_ ? other.ptr_->clone() : nullptr) {}
    AnyValue(AnyValue &&other) noexcept = default;

    AnyValue &operator=(const AnyValue &other)
    {
        if (this != &other)
            ptr_ = other.ptr_ ? other.ptr_->clone() : nullptr;
        return *this;
    }
    AnyValue &operator=(AnyValue &&other) noexcept = default;
    ~AnyValue() = default;

    explicit operator bool() const noexcept
    {
        return static_cast<bool>(ptr_);
    }
    bool empty() const noexcept
    {
        return !ptr_;
    }

    const IValue *get() const noexcept
    {
        return ptr_.get();
    }
    IValue *get() noexcept
    {
        return ptr_.get();
    }

    template <class T>
    const Model<T> *get_if() const noexcept
    {
        return dynamic_cast<const Model<T> *>(ptr_.get());
    }
    template <class T>
    Model<T> *get_if() noexcept
    {
        return dynamic_cast<Model<T> *>(ptr_.get());
    }

    friend std::ostream &operator<<(std::ostream &os, const AnyValue &v)
    {
        if (v.ptr_)
            v.ptr_->print(os);
        else
            os << "<empty>";
        return os;
    }

  private:
    std::unique_ptr<IValue> ptr_;
};

// ============================================================
// 运行时测试
// ============================================================

// T1: 通过 IValue* / IValue& 多态分发 print
void test_ivalue_polymorphic_print()
{
    std::unique_ptr<IValue> a = std::make_unique<Model<int>>(42);
    std::unique_ptr<IValue> b = std::make_unique<Model<std::string>>("hi");
    std::unique_ptr<IValue> c = std::make_unique<Model<Buffer>>(Buffer{1, 2, 3});

    std::ostringstream os;
    a->print(os);
    os << '|';
    b->print(os);
    os << '|';
    c->print(os);
    assert(os.str() == "42|hi|[1,2,3]");

    IValue &ref = *a;
    std::ostringstream os2;
    ref.print(os2);
    assert(os2.str() == "42");
}

// T2: 通过 IValue* clone —— 运行时类型保持 + 深拷贝
void test_ivalue_clone_runtime()
{
    std::unique_ptr<IValue> p = std::make_unique<Model<Buffer>>(Buffer{1, 2, 3});

    std::unique_ptr<IValue> q = p->clone();
    assert(q);
    assert(q.get() != p.get());       // 不同对象
    assert(typeid(*q) == typeid(*p)); // 运行时类型一致

    auto *pq = dynamic_cast<Model<Buffer> *>(p.get());
    auto *qq = dynamic_cast<Model<Buffer> *>(q.get());
    assert(pq && qq);
    assert(pq->value() == (Buffer{1, 2, 3}));
    assert(qq->value() == (Buffer{1, 2, 3}));

    pq->value()[0] = 99;         // 改源
    assert(qq->value()[0] == 1); // 副本不变 => 深拷贝
    qq->value()[1] = 88;         // 改副本
    assert(pq->value()[1] == 2); // 源不变

    // 通过 IValue& 引用 clone
    IValue &ref = *p;
    std::unique_ptr<IValue> r = ref.clone();
    auto *rr = dynamic_cast<Model<Buffer> *>(r.get());
    assert(rr && rr->value()[0] == 99);
    rr->value()[0] = 5;
    assert(pq->value()[0] == 99); // 仍然独立
}

// T3: 直接对 Model<T> 测试五个特殊成员
void test_model_direct_special_members()
{
    // 拷贝构造
    Model<std::string> a(std::string("hello"));
    Model<std::string> b = a;
    assert(a.value() == "hello" && b.value() == "hello");
    b.value() = "world";
    assert(a.value() == "hello" && b.value() == "world");

    // 拷贝赋值
    Model<std::string> c(std::string("x"));
    c = a;
    assert(c.value() == "hello");
    c = c; // 自赋值
    assert(c.value() == "hello");

    // 移动构造
    Model<std::string> d = std::move(a);
    assert(d.value() == "hello");
    a.value() = "reused"; // moved-from 可重新赋值
    assert(a.value() == "reused");

    // 移动赋值
    Model<std::string> e(std::string("y"));
    e = std::move(d);
    assert(e.value() == "hello");
}

// T4: Model<Buffer> 直接验证深拷贝
void test_model_resource_deep_copy()
{
    Model<Buffer> a(Buffer{1, 2, 3});
    Model<Buffer> b = a; // 拷贝构造

    a.value()[0] = 99;
    assert(b.value()[0] == 1); // 副本独立

    Model<Buffer> c(Buffer{7});
    c = b; // 拷贝赋值
    assert(c.value() == (Buffer{1, 2, 3}));

    c.value()[0] = 42;
    assert(b.value()[0] == 1); // 赋值也是深拷贝
}

// T5: 异构集合中的多态 clone —— 类型保持 + 值独立
void test_heterogeneous_clone()
{
    std::vector<std::unique_ptr<IValue>> src;
    src.push_back(std::make_unique<Model<int>>(1));
    src.push_back(std::make_unique<Model<std::string>>("two"));
    src.push_back(std::make_unique<Model<Buffer>>(Buffer{3, 3, 3}));

    std::vector<std::unique_ptr<IValue>> dst;
    for (auto &s : src)
        dst.push_back(s->clone());

    // 运行时类型一一对应
    assert(typeid(*src[0]) == typeid(*dst[0]));
    assert(typeid(*src[1]) == typeid(*dst[1]));
    assert(typeid(*src[2]) == typeid(*dst[2]));

    // 改源不影响副本
    dynamic_cast<Model<int> *>(src[0].get())->value() = 100;
    dynamic_cast<Model<std::string> *>(src[1].get())->value() = "changed";
    dynamic_cast<Model<Buffer> *>(src[2].get())->value()[0] = 999;

    assert(dynamic_cast<Model<int> *>(dst[0].get())->value() == 1);
    assert(dynamic_cast<Model<std::string> *>(dst[1].get())->value() == "two");
    assert(dynamic_cast<Model<Buffer> *>(dst[2].get())->value()[0] == 3);
}

// T6: AnyValue 层（保留，覆盖 wrapper 的值语义）
void test_anyvalue_runtime()
{
    AnyValue a = Buffer{1, 2, 3};
    AnyValue b = a; // 拷贝构造
    a.get_if<Buffer>()->value()[0] = 99;
    assert(b.get_if<Buffer>()->value()[0] == 1);

    AnyValue c;
    c = a; // 拷贝赋值
    a.get_if<Buffer>()->value()[1] = 77;
    assert(c.get_if<Buffer>()->value()[1] == 2);

    AnyValue d = std::move(c); // 移动构造
    assert(c.empty());
    assert(d.get_if<Buffer>()->value()[1] == 2);

    AnyValue e;
    e = std::move(d); // 移动赋值
    assert(d.empty());
    assert(!e.empty());

    AnyValue empty;
    assert(empty.empty());
    std::ostringstream os;
    os << empty;
    assert(os.str() == "<empty>");
}

int main()
{
    // 编译期：AnyValue 完整值语义，移动不抛
    static_assert(std::is_copy_constructible_v<AnyValue>);
    static_assert(std::is_move_constructible_v<AnyValue>);
    static_assert(std::is_copy_assignable_v<AnyValue>);
    static_assert(std::is_move_assignable_v<AnyValue>);
    static_assert(std::is_nothrow_move_constructible_v<AnyValue>);
    static_assert(std::is_nothrow_move_assignable_v<AnyValue>);

    // 编译期：IValue 对外不可拷贝（防切片），Model<T> 可自由复制
    static_assert(!std::is_copy_constructible_v<IValue>);
    static_assert(!std::is_copy_assignable_v<IValue>);
    static_assert(std::is_copy_constructible_v<Model<int>>);
    static_assert(std::is_copy_assignable_v<Model<int>>);

    // 运行时：先测接口/派生类本体
    test_ivalue_polymorphic_print();
    test_ivalue_clone_runtime();
    test_model_direct_special_members();
    test_model_resource_deep_copy();
    test_heterogeneous_clone();
    // 再测包装器
    test_anyvalue_runtime();

    std::cout << "all tests passed\n";
}